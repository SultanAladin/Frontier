/*==============================================================================================================================================
                                                            SURFELGRID.GLSL
==============================================================================================================================================*/
// 🧩 The camera-relative cascaded hash grid that buckets surfels by cell — the shared spatial index every surfel stage reads. A surfel at world
//    position P is placed into a grid cell; the cell's cascade level grows with distance from the eye, so near surfels get fine cells and far ones
//    coarse cells, all in one flat hash. Both the grid BUILD (slotting counts + scatters surfels into cells) and the grid LOOKUP (a gather reads a
//    cell's surfel list) MUST derive the same bucket for the same position, or a surfel is written to one cell and read from another.
//
//    🔴 THIS IS AN #include MODULE AND HAS NO main() AND NO BINDINGS OF ITS OWN. It carries pure functions only: pos -> grid coord -> cascade ->
//       clamped c4 -> flat hash, plus the radius model. Every consumer (the slotting passes, the lifecycle spawn, and later the trace gather) owns
//       its own dispatch and buffers and calls these. Declaring buffers here would force every one of them onto a single descriptor layout.
//
//    🔴 F1 — THE SIGNED-SHIFT CONTRACT (settled by _ClaudeScratch/tmp/F1CascadeShiftProbe.cpp). The per-cascade shift `coord >> cascade` MUST be an
//       ARITHMETIC (sign-extending) shift. webgiya does it over a signed vec3i in both its build (surfelIntegratePass.ts:766-768) and its lookup
//       (surfelHashGrid.ts:199) paths; GLSL `>>` on a signed int is arithmetic, so keeping the coordinate a SIGNED ivec3 all the way to the final
//       clamp reproduces it exactly. Reinterpreting to uvec3 anywhere before the clamp turns it into a LOGICAL shift, and then a surfel behind the
//       world origin (any negative cell) is slotted into one bucket and looked up in another — the probe measured 2352 of 31024 behind-origin cells
//       diverging (7.6%), silent on any positive-octant scene. NEVER introduce uvec3 for the cell coordinate before surfel_grid_coord_to_c4's clamp.
//
//    ⚠️ COMPILE WITH glslc AND AN -I PATH, NOT glslangValidator. A bare .glsl is never discovered by ShaderPlan.ps1's .comp glob; it is compiled only
//       through whichever .comp #includes it, and only glslc resolves #include at all. Same constraint TwoLevelTrace.glsl / ShadowTileStore.glsl carry.

#ifndef FRONTIER_SURFEL_SURFELGRID_GLSL
#define FRONTIER_SURFEL_SURFELGRID_GLSL

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 STRUCTURAL constants — ported 1:1 from webgiya constants.ts. These describe the grid's shape, not the world's scale, so they transfer verbatim.
//    SURFEL_CS is the per-cascade cell edge count (32^3 cells per cascade); SURFEL_CASCADES is the number of nested cascades; the hash packs
//    (x, y, z, cascade) into one uint, so TOTAL_CELLS = SURFEL_CS^3 * SURFEL_CASCADES.
const int  SURFEL_CS       = 32;   // [-] - cells per axis within one cascade (webgiya SURFEL_CS)
const int  SURFEL_CASCADES = 8;    // [-] - nested cascade count (webgiya CASCADES, desktop)
const uint SURFEL_TOTAL_CELLS = uint(SURFEL_CS) * uint(SURFEL_CS) * uint(SURFEL_CS) * uint(SURFEL_CASCADES);

// 📝 Slotting constants — ported 1:1 from webgiya constants.ts. TTL is the age at which a surfel is considered dead (the count/slot skip it);
//    MAX_SURFELS_PER_CELL caps a cell's list slice; NORMAL_DIRECTION_SQUISH is the Mahalanobis anisotropy that thins the surfel disc along its normal
//    so it hugs the surface rather than filling a sphere.
const int   SURFEL_TTL                     = 500;   // [-] - age >= TTL means dead (webgiya SURFEL_TTL)
const int   SURFEL_MAX_SURFELS_PER_CELL    = 64;    // [-] - per-cell list capacity (webgiya MAX_SURFELS_PER_CELL)
const float SURFEL_NORMAL_DIRECTION_SQUISH = 2.0;   // [-] - Mahalanobis squish along the surfel normal (webgiya SURFEL_NORMAL_DIRECTION_SQUISH)

// 🔴 WORLD-SCALE constants — CALIBRATED, not transcribed (PLAN §14). webgiya's SURFEL_GRID_CELL_DIAMETER = 0.2 and SURFEL_BASE_RADIUS = 0.24 are ITS
//    scene units (a small cm-ish room). Frontier's clipmap base cell is 1 m and the head/floor scene spans metres, so the base cell diameter is
//    lifted to keep cascade 0 covering the near field at a comparable cell-to-surfel ratio. Structure (SURFEL_CS, cascades, overscale) is unchanged;
//    only these two metre-denominated numbers move. Tune against the debug splat: cells too small -> pool starves far out; too large -> coarse near GI.
const float SURFEL_GRID_CELL_DIAMETER = 1.0;    // 🔴 [m] - base (cascade-0) cell edge. webgiya 0.2 (their scene); 1.0 matches the 1 m clipmap base cell.
const float SURFEL_BASE_RADIUS        = 1.2;    // 🔴 [m] - surfel disc radius at cascade 0. webgiya 0.24; scaled by the same 5x as the cell diameter.
const float SURFEL_RADIUS_OVERSCALE   = 1.25;   // [-]  - structural, ported 1:1 (webgiya SURFEL_RADIUS_OVERSCALE)

// 🔴 LIVE-TUNING GLOBALS — the world-scale knobs the F10 tuning window drives (PLAN §3). They start EQUAL to the baked consts above, so any shader that
//    does NOT call SurfelSetTuning() behaves byte-identically to the pre-tuning build. A consumer that wants live values calls SurfelSetTuning() once at
//    the top of main() from its push block; every grid/radius helper below reads these globals instead of the consts, so radius/cell/bias reach the WHOLE
//    pipeline (spawn, slotting, integrate, gather, debug) from one seam. NearFieldBias defaults to 1.0 (inert); the spawn throttle multiplies by it.
float g_SurfelCellDiameter = SURFEL_GRID_CELL_DIAMETER;   // [m] - live base cell edge (defaults to the baked const)
float g_SurfelBaseRadius   = SURFEL_BASE_RADIUS;          // [m] - live cascade-0 disc radius (defaults to the baked const)
float g_SurfelNearFieldBias = 1.0;                        // [-] - live near-field spawn lift (1.0 = current behaviour, inert)

// 🔴 LIVE PER-CELL CAP — the F10 window's 32/64/128/256 selector, applied on the explicit Apply button (PLAN §4). It is the spawn-throttle CEILING and the
//    dedup loop bound, NOT a per-cell buffer stride: the List SSBO is packed by the scanned Offsets, so a cell's slice length is whatever the count pass
//    tallied. The List is allocated once at the 256 MAX cap (host-side SurfelMaxPerCell), so raising the cap never over-runs it — this uniform only lets a
//    cell fill FURTHER before the spawn gate stops it. Defaults to the baked const so a shader that never seats it behaves byte-identically.
int g_SurfelPerCellCap = SURFEL_MAX_SURFELS_PER_CELL;    // [-] - live per-cell fill ceiling (defaults to the baked const == max cap)

// Seat the live tuning globals from a consumer's push block. Call once at the top of main() BEFORE any grid/radius helper. A shader that never calls this
// keeps the baked consts, so the module is inert until a stage opts in.
void SurfelSetTuning(float CellDiameter, float BaseRadius, float NearFieldBias)
{
    g_SurfelCellDiameter  = CellDiameter;
    g_SurfelBaseRadius    = BaseRadius;
    g_SurfelNearFieldBias = NearFieldBias;
}

// Seat the live per-cell cap. Only the spawn throttle + the debug occupancy heatmap opt in (they gate/normalize on the cap); every other stage ignores it
// and the packed List is unaffected. A shader that never calls this keeps SURFEL_MAX_SURFELS_PER_CELL (the max), so it is inert until a stage opts in.
void SurfelSetPerCellCap(int PerCellCap)
{
    g_SurfelPerCellCap = PerCellCap;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        GRID COORDINATE MATH
//------------------------------------------------------------------------------------------------------------------------

// pos -> integer grid coordinate at cascade 0 (surfel_pos_to_grid_coord). pRel is the position RELATIVE TO THE EYE (camera-relative grid). The floor
// is signed: a position behind the origin yields a NEGATIVE coord, which is the whole point of the F1 contract above.
ivec3 SurfelPositionToGridCoord(vec3 PositionRelative)
{
    return ivec3(floor(PositionRelative / g_SurfelCellDiameter));
}

// integer grid coord -> the FLOAT cascade level it falls in (surfel_grid_coord_to_cascade_float). The cascade grows with the largest absolute axis:
// once a coord leaves the central SURFEL_CS/2 shell it steps up a cascade, doubling the effective cell size.
float SurfelGridCoordToCascadeFloat(ivec3 Coord)
{
    vec3  FloatCoord = vec3(Coord) + vec3(0.5);
    float MaxComponent = max(abs(FloatCoord.x), max(abs(FloatCoord.y), abs(FloatCoord.z)));
    return log2(MaxComponent / (float(SURFEL_CS) * 0.5));
}

// float cascade -> clamped uint cascade in [0, SURFEL_CASCADES-1] (surfel_cascade_float_to_cascade).
uint SurfelCascadeFloatToCascade(float CascadeFloat)
{
    float Ceiled  = ceil(max(0.0, CascadeFloat));
    float Clamped = clamp(Ceiled, 0.0, float(SURFEL_CASCADES - 1));
    return uint(Clamped);
}

// 🔴 F1 SITE. Shift the signed grid coord down by the cascade level, then bias into the [0, SURFEL_CS) window (surfel_grid_coord_within_cascade). The
//    operand STAYS SIGNED so `>>` is arithmetic — see the F1 contract at the top of this file. This is the ONE helper both build and lookup route
//    through, so they can never disagree on the shift.
ivec3 SurfelGridCoordWithinCascade(ivec3 Coord, uint Cascade)
{
    return (Coord >> int(Cascade)) + SURFEL_CS / 2;
}

// integer grid coord -> the packed cell key (x, y, z within-cascade, cascade) (surfel_grid_coord_to_c4). The clamp to [0, SURFEL_CS-1] is the LAST
// step, and only here does the coordinate become unsigned — after the arithmetic shift has already done its work on the signed value.
uvec4 SurfelGridCoordToCell(ivec3 Coord)
{
    uint  Cascade = SurfelCascadeFloatToCascade(SurfelGridCoordToCascadeFloat(Coord));
    ivec3 Within  = SurfelGridCoordWithinCascade(Coord, Cascade);
    ivec3 Clamped = clamp(Within, ivec3(0), ivec3(SURFEL_CS - 1));
    return uvec4(uint(Clamped.x), uint(Clamped.y), uint(Clamped.z), Cascade);
}

// packed cell -> flat hash into [0, SURFEL_TOTAL_CELLS) (surfel_grid_c4_to_hash). Row-major over (x, y, z, cascade).
uint SurfelCellToHash(uvec4 Cell)
{
    uint Cs = uint(SURFEL_CS);
    return Cell.x + Cell.y * Cs + Cell.z * Cs * Cs + Cell.w * Cs * Cs * Cs;
}

// The whole chain in one call — this is what BOTH build and lookup invoke, so the bucket is derived identically on each side (surfelHashGrid.ts:199).
uint SurfelHashOfCoord(ivec3 Coord)
{
    return SurfelCellToHash(SurfelGridCoordToCell(Coord));
}

// Convenience: eye-relative world position straight to its bucket.
uint SurfelHashOfPosition(vec3 PositionRelative)
{
    return SurfelHashOfCoord(SurfelPositionToGridCoord(PositionRelative));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          RADIUS MODEL
//------------------------------------------------------------------------------------------------------------------------

// surfel disc radius for an eye-relative position (surfel_radius_for_pos): the base radius, grown so a surfel far from the eye covers its (coarser)
// cascade. Distance is measured in the same eye-relative metres as the grid.
float SurfelRadiusForPosition(vec3 PositionRelative)
{
    float Distance       = length(PositionRelative);
    float CascadeRadius  = g_SurfelCellDiameter * float(SURFEL_CS) * 0.5;
    return g_SurfelBaseRadius * max(1.0, Distance / CascadeRadius);
}

// The two-argument radius the count/slot passes actually call: surfel_radius_for_pos(worldPos, camPos). Distance is |worldPos - camPos|, i.e. the true
// eye distance, NOT the grid-origin-relative one — webgiya deliberately measures from the raw camera position here (surfelHashGrid.ts:398/597), while
// the grid indices come from the snapped-origin-relative position. The single-arg form above is the pRel==worldPos-origin, camPos==0 special case.
float SurfelRadiusForPositionEye(vec3 WorldPosition, vec3 CameraPosition)
{
    float Distance      = length(WorldPosition - CameraPosition);
    float CascadeRadius = g_SurfelCellDiameter * float(SURFEL_CS) * 0.5;
    return g_SurfelBaseRadius * max(1.0, Distance / CascadeRadius);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CELL CENTRE + INTERSECTION
//------------------------------------------------------------------------------------------------------------------------

// The WORLD-space centre of a packed cell (surfel_grid_coord_center). The cell's within-cascade coord is recentred to [-CS/2, CS/2), scaled by the base
// cell diameter, then by the cascade scale (2^cascade), and offset by the eye position (the grid is camera-relative). EyePosition is the SNAPPED grid
// origin the caller passes (U_GRID_ORIGIN), matching webgiya's surfel_grid_coord_center(c4, gridOrigin) call sites.
vec3 SurfelGridCoordCenter(uvec4 Cell, vec3 EyePosition)
{
    vec3  GridPosition  = (vec3(Cell.xyz) + vec3(0.5)) - float(SURFEL_CS) * 0.5;
    vec3  PositionInCascade = GridPosition * g_SurfelCellDiameter;
    float CascadeScale  = float(1u << Cell.w);
    return EyePosition + PositionInCascade * CascadeScale;
}

// Does a surfel's oriented disc intersect a cell's box (surfel_intersects_grid_coord)? Box test in the cell's local frame, then a Mahalanobis "squish"
// that stretches distance along the surfel normal so the disc is thin out-of-plane. GridOrigin is the snapped eye origin (same value fed to the centre).
bool SurfelIntersectsGridCoord(vec3 SurfelWorldPosition, vec3 Normal, float Radius, uvec4 Cell, vec3 GridOrigin)
{
    vec3  CellCentreWorld = SurfelGridCoordCenter(Cell, GridOrigin);
    float CascadeScale    = float(1 << int(Cell.w));
    float CellRadius      = SURFEL_GRID_CELL_DIAMETER * 0.5 * CascadeScale;

    vec3  CellLocalPosition = SurfelWorldPosition - CellCentreWorld;
    vec3  ClosestPoint      = clamp(CellLocalPosition, vec3(-CellRadius), vec3(CellRadius));
    vec3  PositionOffset    = CellLocalPosition - ClosestPoint;

    float DistanceLength   = length(PositionOffset);
    float DotNormal        = abs(dot(PositionOffset, Normal));
    float MahalanobisDist  = DistanceLength * (1.0 + DotNormal * SURFEL_NORMAL_DIRECTION_SQUISH);
    return MahalanobisDist < Radius;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       MULTI-CASCADE GRID BOX
//------------------------------------------------------------------------------------------------------------------------

// The multi-cascade bounding box of a surfel's disc over the grid (get_surfel_grid_box_min_max). PositionRelative is the surfel position relative to the
// SNAPPED grid origin. Returns the within-cascade clamped [min, max] for up to TWO cascades (the centre cascade and its hysteresis neighbour). The .w of
// each min/max carries that box's cascade index; CascadeCount is 1 or 2. The count/slot passes iterate cascade 0..CascadeCount-1, then x/y/z in [min,max].
//
// 🔴 The ±0.2 cascade HYSTERESIS is load-bearing and ported exactly: a surfel straddling a cascade boundary is slotted into BOTH neighbouring cascades so
//    the lookup never misses it near the seam. c0 = cascade(cf-0.2), c1 = cascade(cf+0.2); if they differ, both boxes are emitted.
struct SurfelGridBox
{
    ivec4 C0Min; ivec4 C0Max;   // [-] - within-cascade [min,max] for cascade C0Min.w (== C0Max.w)
    ivec4 C1Min; ivec4 C1Max;   // [-] - within-cascade [min,max] for cascade C1Min.w (== C1Max.w)
    int   CascadeCount;         // [-] - 1 (C0 only) or 2 (C0 and C1 differ)
};

SurfelGridBox SurfelGridBoxMinMax(vec3 PositionRelative)
{
    float DiscRadius = SurfelRadiusForPosition(PositionRelative);   // pRel-relative radius (camPos == 0), matching the box helper's own call

    ivec3 GridMin     = SurfelPositionToGridCoord(PositionRelative - vec3(DiscRadius));
    ivec3 GridMax     = SurfelPositionToGridCoord(PositionRelative + vec3(DiscRadius));
    ivec3 CentreCoord = SurfelPositionToGridCoord(PositionRelative);

    float CascadeFloat = SurfelGridCoordToCascadeFloat(CentreCoord);
    uint  C0 = SurfelCascadeFloatToCascade(CascadeFloat - 0.2);
    uint  C1 = SurfelCascadeFloatToCascade(CascadeFloat + 0.2);

    ivec3 MinC0 = SurfelGridCoordWithinCascade(GridMin, C0);
    ivec3 MaxC0 = SurfelGridCoordWithinCascade(GridMax, C0);
    ivec3 MinC1 = SurfelGridCoordWithinCascade(GridMin, C1);
    ivec3 MaxC1 = SurfelGridCoordWithinCascade(GridMax, C1);

    ivec3 Lo = ivec3(0);
    ivec3 Hi = ivec3(SURFEL_CS - 1);

    SurfelGridBox Box;
    Box.C0Min = ivec4(clamp(MinC0, Lo, Hi), int(C0));
    Box.C0Max = ivec4(clamp(MaxC0, Lo, Hi), int(C0));
    Box.C1Min = ivec4(clamp(MinC1, Lo, Hi), int(C1));
    Box.C1Max = ivec4(clamp(MaxC1, Lo, Hi), int(C1));
    Box.CascadeCount = (C0 != C1) ? 2 : 1;
    return Box;
}

#endif
