/*==============================================================================================================================================
                                                            SURFELCELLGRID.GLSL
==============================================================================================================================================*/
// 🧩 The camera-relative uniform cell grid that indexes the surfel field, ported 1:1 from W298/SurfelGI (SurfelUtils.slang). Every pass that asks
//    "which cells does this surfel touch?" or "which surfels are near this point?" goes through this file: the census counts per cell through it, the
//    scatter writes the cell lists through it, and the spawn and shade both search through it. No storage of its own — pure functions over a position
//    and the camera origin.
//
// 💡 WHY CAMERA-RELATIVE AND FLAT, rather than the cascaded hash grid this replaced. Cell coordinates are computed as an offset from the CAMERA, so the
//    grid slides with the view and a fixed CellDimension³ table always covers the volume in front of the observer. That buys two things a cascade
//    cannot: every cell is the same size, so a surfel's neighbourhood is always the same fixed 5³ = 125 cells (no cascade walk, no level selection);
//    and there is no hashing, so no collisions to resolve. The cost is that the grid has a hard REACH — a surfel further than half the grid from the
//    camera has no cell and is simply not found. At CellEdge 0.25 m x CellDimension 128 that reach is 16 m in every direction, a 32 m cube.
//    ⚠️ The grid moving with the camera means a cell index is only meaningful WITHIN one frame, against the origin it was computed from. Never persist
//       a flattened cell index across frames, and never mix indices computed against two different camera origins in one comparison.
//
// 🔴 SIGNEDNESS IS THE TRAP IN THIS FILE, AND IT BIT THE PREVIOUS PORT. Cell coordinates are SIGNED (a surfel behind or left of the camera has negative
//    components) and every comparison here must stay in signed arithmetic. Upstream writes `abs(cellPos.x) >= kCellDimension / 2`, comparing an int
//    against a uint — which in both HLSL and GLSL converts the INT to UINT. That happens to work upstream only because abs() has already made the left
//    side non-negative. Transcribing the same line without the abs, or comparing a raw component against an unsigned bound, makes every negative
//    coordinate read as a huge positive one: cells behind the camera silently pass the validity test and then flatten out of bounds. So the bound below
//    is declared as an int, and the validity test is spelled out per axis over signed values.

#ifndef FRONTIER_SURFEL_CELL_GRID_GLSL
#define FRONTIER_SURFEL_CELL_GRID_GLSL

#include "SurfelTypes.glsl"

//------------------------------------------------------------------------------------------------------------------------
//                                                        GRID PROPORTIONS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 CALIBRATED, not transcribed — and pinned by SurfelGridProportions in SurfelTypes.h, which is what the host sizes CellSpan and Reservation from.
//    Upstream runs CellEdge 0.05 m x CellDimension 250: a 12.5 m reach across 15.6M cells, sized for a Falcor test scene held entirely in front of the
//    camera. 0.25 m x 128 spans 32 m — matching clipmap level 0's footprint, so the GI reach agrees with a structure the renderer already maintains —
//    for 2.1M cells instead. ⚠️ Changing either number here without changing SurfelTypes.h desynchronizes the shader's bounds from the allocation.
const float SurfelCellEdge      = 0.25;    // [m] - world edge of one cell (upstream kCellUnit = 0.05)
const int   SurfelCellDimension = 128;     // [-] - cells per axis, SIGNED so the bound arithmetic below never converts (upstream kCellDimension = 250)
const int   SurfelCellHalfSpan  = SurfelCellDimension / 2;   // [-] - the ± coordinate bound; a cell at exactly this is OUT of range

// 📝 The 5³ neighbourhood, generated rather than transcribed as upstream's 125-line literal table. The nesting order (x outermost, z innermost) matches
//    upstream's neighborOffset[] exactly, so an index into this and an index into that table name the same cell — which matters only if the two are
//    ever diffed, but costs nothing to preserve.
// 💡 5³ is not arbitrary: a surfel's radius is capped at CellEdge * 2 by ResolveSurfelRadius below, so a disc can never reach beyond ±2 cells from the
//    one holding its centre. The neighbourhood is exactly large enough to be exhaustive, and no larger. 🔴 The cap and this radius are one decision —
//    raising the radius clamp without widening this search silently loses the cells at the rim.
ivec3 ResolveNeighbourCellOffset(uint Ordinal)
{
    const int Span = 5;
    int Linear = int(Ordinal);
    int OffsetX = (Linear / (Span * Span)) - 2;
    int OffsetY = ((Linear / Span) % Span) - 2;
    int OffsetZ = (Linear % Span) - 2;
    return ivec3(OffsetX, OffsetY, OffsetZ);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CELL ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

// The signed cell coordinate holding WorldPosition, relative to CameraPosition. Upstream getCellPos.
// 📝 round(), NOT floor() — so a cell is CENTRED on its coordinate rather than starting at it, which is what makes the ±0.5·CellEdge half-extent in
//    SurfelIntersectsCell below correct. Using floor() here shifts every cell by half a cell against the intersection test, and the resulting
//    mis-assignment looks like a mild, plausible spatial offset rather than an error.
// 🔴 GLSL round() IS FREE TO GO EITHER WAY AT EXACTLY .5, AND THE MEASURED GPU DOES NOT AGREE WITH THE C RUNTIME. The Phase-4 gate put a surfel at
//    +0.5 cells on all three axes: std::round (half-away-from-zero) says cell (1,1,1); this device's round() (half-to-even) says (0,0,0). Both are
//    conformant, so a surfel on a cell boundary has NO canonical coordinate. ⚠️ Consequence for every consumer: call this function once and pass the
//    result along. A pass that re-derives the coordinate from the position — or a host that predicts it — can land on the other side of the tie, and
//    then the census counts the surfel in one cell while the scatter writes it into another. The count and the list disagree by one entry, which reads
//    downstream as a single missing or duplicated surfel in a cell, not as a rounding question.
ivec3 ResolveCellCoordinate(vec3 WorldPosition, vec3 CameraPosition)
{
    vec3 CameraRelative = (WorldPosition - CameraPosition) / SurfelCellEdge;
    return ivec3(round(CameraRelative));
}

// Whether CellCoordinate lies inside the grid. Upstream isCellValid.
// 🔴 Signed comparison per axis, deliberately not `abs(c) >= uint(bound)`. See the file header: the unsigned form passes cells behind the camera.
//    ⚠️ MUST be called before every FlattenCellIndex — the flatten biases by the half-span and an out-of-range coordinate wraps into a valid-looking
//       index somewhere else in the table, corrupting an unrelated cell rather than failing.
bool CellCoordinateValid(ivec3 CellCoordinate)
{
    if (CellCoordinate.x <= -SurfelCellHalfSpan || CellCoordinate.x >= SurfelCellHalfSpan) return false;
    if (CellCoordinate.y <= -SurfelCellHalfSpan || CellCoordinate.y >= SurfelCellHalfSpan) return false;
    if (CellCoordinate.z <= -SurfelCellHalfSpan || CellCoordinate.z >= SurfelCellHalfSpan) return false;
    return true;
}

// The linear index of CellCoordinate in the CellDimension³ table. Upstream getFlattenCellIndex.
// 📝 The +HalfSpan bias is what maps the signed range (-64, 64) onto the unsigned range [0, 128) — negative cells are handled by this bias, not by any
//    shift, which is why the previous port's signed-shift hazard does not recur here. ⚠️ Undefined unless CellCoordinateValid passed first.
uint FlattenCellIndex(ivec3 CellCoordinate)
{
    ivec3 Biased = CellCoordinate + ivec3(SurfelCellHalfSpan);
    return uint((Biased.z * SurfelCellDimension * SurfelCellDimension) + (Biased.y * SurfelCellDimension) + Biased.x);
}

// The world-space centre of a cell. The inverse of ResolveCellCoordinate, used by the debug overlays and by the spawn's reservation snap.
vec3 ResolveCellCentre(ivec3 CellCoordinate, vec3 CameraPosition)
{
    return vec3(CellCoordinate) * SurfelCellEdge + CameraPosition;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERSECTION & RADIUS
//------------------------------------------------------------------------------------------------------------------------

// Whether a surfel of Radius centred at SurfelPosition reaches into CellCoordinate. Upstream isSurfelIntersectCell.
// 📝 Sphere-versus-box: clamp the centre into the cell's bounds to get the closest point on the box, then compare that distance against the radius.
//    ⚠️ This treats the surfel as a SPHERE, not the oriented disc it actually is — upstream does the same. It over-reports at the rim (a disc edge-on to
//       a cell is counted as touching it), which costs a few redundant cell-list entries and never misses a real overlap. Conservative in the safe
//       direction; do not "fix" it to an exact disc test without re-checking that the census and the scatter agree, since they must count identically.
bool SurfelIntersectsCell(vec3 SurfelPosition, float Radius, ivec3 CellCoordinate, vec3 CameraPosition)
{
    if (!CellCoordinateValid(CellCoordinate)) return false;

    vec3 Centre     = ResolveCellCentre(CellCoordinate, CameraPosition);
    vec3 HalfExtent = vec3(SurfelCellEdge * 0.5);
    vec3 ClosestPoint = clamp(SurfelPosition, Centre - HalfExtent, Centre + HalfExtent);

    return distance(ClosestPoint, SurfelPosition) < Radius;
}

// The screen-projected radius a surfel at Distance should carry to cover TargetArea pixels. Upstream calcRadiusApprox.
// 📝 Inverts the projection: a disc of screen area A subtends an angular radius of sqrt(A/π) pixels, converted to radians by the vertical FOV over the
//    larger screen axis, then to world units by tan() at that distance. So a surfel far from the camera is physically larger, holding the pixel cost of
//    the field roughly flat regardless of depth.
float ResolveScreenProjectedRadius(float Distance, float VerticalFieldOfView, uvec2 Resolution, float TargetArea)
{
    const float Pi = 3.14159265358979323846;
    float LargerAxis = float(max(Resolution.x, Resolution.y));
    return Distance * tan(sqrt(TargetArea / Pi) * VerticalFieldOfView / LargerAxis);
}

// The radius a surfel actually takes. Upstream calcSurfelRadius.
// 🔴 THE CLAMP TO CellEdge * 2 IS STRUCTURAL, NOT A QUALITY KNOB. It is what guarantees a surfel cannot reach past ±2 cells, which is what makes the
//    125-cell neighbourhood an exhaustive search. Raising this bound without widening that neighbourhood means the cells at a large surfel's rim never
//    get an entry — the surfel is then invisible from exactly the positions it should light most, with no error anywhere.
float ResolveSurfelRadius(float Distance, float VerticalFieldOfView, uvec2 Resolution, float TargetArea)
{
    return min(ResolveScreenProjectedRadius(Distance, VerticalFieldOfView, Resolution, TargetArea), SurfelCellEdge * 2.0);
}

// The radius floor a SLEEPING surfel keeps. Upstream applies this after inflating a sleeping surfel's target area 16x.
// 📝 A sleeping surfel is one few screen tiles still reference; it is retained cheaply as history rather than re-converged. Inflating its area makes it
//    coarse, and this floor stops it collapsing to nothing and being recycled while it is still the only cache of that region's light.
float ResolveSleepingRadiusFloor()
{
    return SurfelCellEdge * 0.5;
}

// The screen area a surfel of Radius covers at Distance. Upstream calcProjectArea — the forward direction of the radius solve, used by the spawn to
// decide whether existing coverage already accounts for a pixel.
float ResolveProjectedArea(float Radius, float Distance, float VerticalFieldOfView, uvec2 Resolution)
{
    const float Pi = 3.14159265358979323846;
    float LargerAxis      = float(max(Resolution.x, Resolution.y));
    float ProjectedRadius = atan(Radius / Distance) * LargerAxis / VerticalFieldOfView;
    return Pi * ProjectedRadius * ProjectedRadius;
}

#endif
