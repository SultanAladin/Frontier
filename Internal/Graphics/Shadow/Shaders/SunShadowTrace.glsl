/*==============================================================================================================================================
                                                           SUNSHADOWTRACE.GLSL
==============================================================================================================================================*/
// 🧩 P6.5 — the READ side of the sun shadow atlas: turns a shaded world position into a sun-visibility scalar in [0,1]. Everything before this
//    decided which tiles exist (S1/S3/S5), primed their pages (S6) and rasterized caster depth into them (S7); this is the first unit that
//    consumes any of it, and therefore the first thing that puts a shadow on screen.
//
//    The addressing chain is the EXACT inverse of ShadowDepthRaster.frag's write chain, and that symmetry is the correctness argument: world
//    position -> light space -> light tile -> toroidal slot -> page index (mapping SSBO) -> page origin -> texel in page. If the two chains
//    disagree anywhere, a receiver reads a texel a caster never wrote.
//
// 🔴 THIS FILE IS AN #include BODY, NOT A COMPILE TARGET. ShaderPlan.ps1 discovers only *.vert / *.frag / *.comp, so the .glsl extension is what
//    keeps the build from trying to compile it as a stage with no entry point. Same rule as ShadowTileStore.glsl.
//
// 🔴 IT DECLARES NO DESCRIPTOR BINDINGS, AND THAT IS DELIBERATE RATHER THAN INCOMPLETE. ShadowTileStore.glsl hard-binds its tile table at set 0
//    binding 0, which is exactly why BOTH S7 stages had to refuse to include it: the binding decoration is emitted into the SPIR-V even when the
//    block is dead-stripped from the name table, so two descriptor types collide at one binding and glslc returns 0 anyway (verified with
//    spirv-dis — see ShadowDepthRaster.frag's banner). A shared include that names its own bindings can therefore only ever be included by ONE
//    unit. This one takes the atlas sampler, the mapping array and the page-coverage array as PARAMETERS, so every consumer binds them wherever its
//    own set has room.
//
// 🔴 THE ATLAS IS SAMPLED WITH texelFetch AND MUST NEVER BE FILTERED. It holds a monotonic uint depth ENCODING, not a colour: the average of two
//    encoded depths is not the depth of anything, and a bilinear tap across a page BOUNDARY averages depth from two unrelated regions of the
//    world. That reads as a shadow with soft wrong-coloured fringes rather than as a filtering mistake. Softness comes from multiple discrete
//    taps (the SMRT follow-up), never from the sampler.
//
// ⚠️ DEPTH IS ENCODED INCREASING WITH DISTANCE FROM THE SUN, so a receiver is occluded when the stored depth is LESS than its own. Any sign flip
//    inverts the test into "lit only where something blocks it" — an inside-out image rather than a blank one.

#ifndef FRONTIER_SUNSHADOW_TRACE_GLSL
#define FRONTIER_SUNSHADOW_TRACE_GLSL

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// ⚠️ Mirrored from SunShadowClipmap.h / ShadowPageAtlas.h and cross-checked by NO compiler. Drift produces shadows that land in the wrong place
//    rather than a build failure. ShadowTileStore.glsl carries the same warning for the same reason.
#define ShadowTraceTilemapResolution 32
#define ShadowTraceLodCount          6
#define ShadowTracePageResolution    128
#define ShadowTraceAtlasPageEdge     32

// 📝 The atomic-min identity: a texel holding this had nothing rasterized into it. Mirrors ShadowPageClearIdentity.
// 🔴 A texel at the identity means UNOCCLUDED, and it must be tested for EXPLICITLY rather than left to the depth compare. The identity is
//    0xFFFFFFFF, the LARGEST code, so a plain `Stored < Receiver` already returns false there and the pixel reads lit by luck. That luck ends the
//    moment the comparison gains a bias: subtracting a bias from the receiver keeps it below the identity, but ADDING one to the stored depth
//    would wrap it to 0 — the NEAREST depth — and every empty texel would shadow everything. Naming the case keeps the encoding's top code
//    reserved in the reader as it already is in the writer.
#define ShadowTraceClearIdentity     0xFFFFFFFFu

// 📝 The page sentinel, mirroring ShadowPageUnmapped. A tile with no page has no depth to read; the receiver must fall back to a coarser level.
#define ShadowTracePageUnmapped      0xFFFFFFFFu

// The scale the writer encodes with. 🔴 MUST BE BIT-IDENTICAL TO ShadowDepthRaster.frag's DepthEncodeScale — the reader and the writer share one
// encoding, and 0xFFFFFF00 is chosen because it has 24 significant bits and so is exactly representable in float (0xFFFFFFFE is not; it rounds up
// to 2^32 and converts as undefined). See that shader's 🔴 block for the measurement.
#define ShadowTraceDepthEncodeScale  4294967040.0

// 📝 How far along its own normal a receiver is pushed before it is looked up, measured in TEXELS OF THE LEVEL BEING SAMPLED (the macro converts to
//    metres per level, because a texel's world size doubles every level).
// 🔴 THIS IS WHAT FIXES SURFACE ACNE ON GRAZING GEOMETRY, AND A DEPTH BIAS CANNOT DO ITS JOB. SunShadowDepthBiasMetres is a constant slack along the
//    light's travel, so the depth error it must absorb is texel_size * tan(incidence) — unbounded as the sun drops toward the horizon, which means no
//    finite constant works for a 100 m floor under a low sun. Offsetting along the NORMAL instead moves the lookup off the silhouette by an amount
//    that grows with incidence automatically, because a grazing surface's normal is nearly perpendicular to the light. This is EEVEE's and Unreal's
//    approach for the same reason.
// ⚠️ Too large detaches contact shadows (peter-panning) and can push a lookup into the NEIGHBOURING tile — which is legal (the walk handles a
//    non-resident neighbour by falling outward) but costs sharpness. 1.5 texels is the usual starting point; it is a tuned value, not a derived one.
#define ShadowTraceNormalOffsetTexels 1.5

//------------------------------------------------------------------------------------------------------------------------
//                                                          ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Arithmetic floor-modulo into [0, Modulus). 🔴 GLSL's `%` takes the sign of the dividend, and light tiles are routinely NEGATIVE because the
//    lattice is centred on the observer rather than the world origin — so this is the common path, not an edge case. Must match
//    ShadowTileStore.glsl's wrap and ToroidalAddressing.h's exactly.
// ⚠️ Suffixed `Trace` only to avoid colliding with ShadowTileStore.glsl's identical function when a consumer includes both.
int WrapShadowTraceTile(int Value, int Modulus)
{
    const int Remainder = Value % Modulus;
    return (Remainder < 0) ? (Remainder + Modulus) : Remainder;
}

// 📝 Arithmetic floor to int. `int(floor(x))` rather than a cast: a cast truncates toward zero and folds -0.5 and +0.5 both onto 0, which shifts
//    every tile left of and below the origin by one.
int FloorShadowTraceInt(float Value)
{
    return int(floor(Value));
}

// 📝 Flat slot index for a light tile at one level. 🔴 THE ORIGIN IS ADDED, NOT SUBTRACTED — ToroidalOrigin is stored in ADD form (the negated
//    window corner). Subtracting compiles, stays in range, and still maps distinct tiles to distinct slots, so nothing looks wrong; it simply
//    disagrees with the CPU and with S7 about which tile owns which slot, and the shadows land somewhere else entirely.
// 📝 Whether a light tile lies inside this level's 32-tile window AT ALL, tested on the SHIFTED-BUT-UNWRAPPED coordinate.
//
// 🔴 THIS EXISTS BECAUSE THE WRAP BELOW HAS NO FAILURE PATH, AND THEREFORE NEITHER DOES ANY TEST PLACED AFTER IT. A floor-modulo folds EVERY input
//    into [0, 32) — tile 32 lands on slot 0, tile 100 lands on slot 4 — so `TraceSlot < MappingArray.length()` is unreachable dead code for an
//    out-of-window receiver, and so is the page-sentinel test after it. The page those receivers name is allocated, cleared, rasterized and read
//    CORRECTLY; it simply describes a different patch of ground. Every counter in the pipeline stays healthy while the floor is wrong.
//
// 🔴 THE WRITE SIDE IS GUARDED BY HARDWARE AND THAT IS WHY THIS GAP LOOKED SYMMETRIC. ShadowDepthRaster.vert maps casters to NDC against the window
//    centre and lets the rasterizer CLIP anything outside — its banner says so outright. The read side has no rasterizer, computes its tile with
//    arithmetic, and so needed the same exclusion written by hand. Both sides call an identically-named Resolve*Slot with an identical wrap, which
//    reads as parity; only one of them is actually protected.
//
// 🔴 THE RESULT MUST GATE `PageResident`, NOT MERELY THE FETCH. An aliased page genuinely holds depth, so reporting it resident makes the level walk
//    break on a wrong SHARP answer and suppress the coarser level holding the right one — which is why the artefact got worse when zooming out
//    instead of degrading. Reporting a miss instead falls outward to a wider window, and past the coarsest level to honestly lit.
// ⚠️ Must be evaluated BEFORE the wrap and short-circuited ahead of the range test; folding it in after the modulo reproduces the bug exactly.
bool ShadowTraceTileInsideWindow(ivec2 LightTile, ivec2 ToroidalOrigin)
{
    const ivec2 Shifted = LightTile + ToroidalOrigin;

    return Shifted.x >= 0 && Shifted.x < ShadowTraceTilemapResolution
        && Shifted.y >= 0 && Shifted.y < ShadowTraceTilemapResolution;
}

uint ResolveShadowTraceSlot(uint Level, ivec2 LightTile, ivec2 ToroidalOrigin)
{
    const ivec2 Shifted     = LightTile + ToroidalOrigin;
    const int   WrappedX    = WrapShadowTraceTile(Shifted.x, ShadowTraceTilemapResolution);
    const int   WrappedY    = WrapShadowTraceTile(Shifted.y, ShadowTraceTilemapResolution);
    const uint  WithinLevel = uint(WrappedY * ShadowTraceTilemapResolution + WrappedX);

    return Level * uint(ShadowTraceTilemapResolution * ShadowTraceTilemapResolution) + WithinLevel;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        LIGHT SPACE
//------------------------------------------------------------------------------------------------------------------------

// 📝 One world point in the sun's orthonormal frame: xy lateral (what the tile lattice is addressed in), z depth along the light's travel.
//    Three dot products, no matrix and no perspective divide — the same projection ShadowDepthRaster.vert performs, which is what makes the
//    reader's z directly comparable to the writer's.
vec3 ProjectShadowTraceLightSpace(vec3 WorldPosition, vec3 RightAxis, vec3 UpAxis, vec3 ForwardAxis)
{
    return vec3(dot(WorldPosition, RightAxis),
                dot(WorldPosition, UpAxis),
                dot(WorldPosition, ForwardAxis));
}

// 📝 Encode a light-space depth in metres to the atlas's uint code. The inverse of the writer's normalize-then-scale.
// ⚠️ Clamped, matching the writer: an over-range receiver saturates rather than wrapping to the opposite end of the encoding.
uint EncodeShadowTraceDepth(float DepthMetres, float DepthOriginMetres, float DepthRangeMetres)
{
    const float Normalized = clamp((DepthMetres - DepthOriginMetres) / DepthRangeMetres, 0.0, 1.0);
    return uint(Normalized * ShadowTraceDepthEncodeScale);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE TAP
//------------------------------------------------------------------------------------------------------------------------

// 📝 What one atlas lookup found. Separating "there was no page" from "the page says lit" is the whole reason this is a struct rather than a float:
//    the two are indistinguishable in a 0..1 return, and collapsing them makes an unmapped tile read as LIT — so a receiver whose fine tile simply
//    was not resident would be brightly lit inside an otherwise correct shadow. The coarse fallback exists precisely to catch that case, and it can
//    only fire if the miss is reported.
struct ShadowTraceTap
{
    bool  PageResident;      // [-] - false when the tile has no page OR the page was never drawn into; the caller must fall back to a coarser level
    bool  OccludedCondition; // [-] - true when a caster sits between this point and the sun
    float DepthMargin;       // [0-1] - receiver depth minus stored depth, in encoded units normalized to the range; < 0 when occluded
};

// 📝 Whether a page was actually rasterized into this image, read from the coverage word S7's fragment stage raised.
//
// 🔴 THIS EXISTS BECAUSE A MAPPED PAGE IS NOT THE SAME THING AS A DRAWN PAGE, and conflating the two is what made the whole scene render lit. Pages are
//    allocated from RECEIVER demand (S1 marks a tile per screen pixel) but filled from CASTER coverage (S7 rasterizes the caster mesh), so every tile a
//    receiver looks at but no caster projects onto gets a page that is cleared and then drawn into by nothing. It keeps the clear identity, and a tap
//    that reported it resident would answer "nothing occludes me" from a page that holds no information — masking the coarser level that DOES have
//    depth. Treating it as a miss lets the walk continue outward, which is the same degradation an unmapped tile already gets.
//
// 🔴 THIS READS A RECORDED FACT AND THE PREVIOUS VERSION INFERRED ONE, WHICH IS THE WHOLE FIX. The earlier implementation sampled the tapped texel plus
//    four interior quarter-points and declared the page empty if all five held the clear identity. That is unsound in the common direction: a caster
//    covering only PART of a tile — every silhouette edge, every object smaller than a tile — leaves all five probes untouched while the page is
//    genuinely drawn. The tap then reported a miss, the walk fell out through every level, TraceSunShadowVisibility kept its initial 1.0, and the
//    receiver came out PAGE-SIZED WHITE, clustered where partial coverage is common and drifting as the window scrolls tile boundaries across caster
//    silhouettes. Nothing in the chain could see it — the page was allocated, cleared, drawn and marked rendered, all correctly.
// ⚠️ The coverage array is a macro ARGUMENT, never a binding declared here: this file binds nothing by design (see the banner), so each consumer names
//    its own buffer at the call site.
#define ShadowPageHoldsDepth(OutHoldsDepth, CoverageArray, PageIndex)                                                                    \
{                                                                                                                                       \
    (OutHoldsDepth) = ((PageIndex) < (CoverageArray).length()) && ((CoverageArray)[(PageIndex)] != 0u);                                   \
}

// 📝 The parts of the chain that need no descriptor access, factored out so the macro below stays small enough to read. Returns the atlas texel a
//    light position maps to WITHIN a given page, plus the light tile it fell in — everything except the page lookup itself.
// 🔴 The in-tile fraction is `LateralTile - LightTile`, not fract(), so it agrees with the floor above by CONSTRUCTION for negative coordinates.
//    fract() and floor() are consistent in the spec, but the writer computes it this way and the two must not merely happen to match.
// ⚠️ The in-page texel is CLAMPED to the last texel rather than discarded, matching the writer: the fraction can reach exactly 1.0 through
//    floating-point rounding at a tile boundary, and rejecting it there would punch a one-texel seam along every tile edge.
void ResolveShadowTraceTexel(vec2   LateralMetres,
                             float  TileMetres,
                             out ivec2 OutLightTile,
                             out uvec2 OutWithinPage)
{
    const vec2  LateralTile = LateralMetres / TileMetres;
    OutLightTile            = ivec2(FloorShadowTraceInt(LateralTile.x), FloorShadowTraceInt(LateralTile.y));

    const vec2 WithinTile = LateralTile - vec2(OutLightTile);
    OutWithinPage         = min(uvec2(WithinTile * float(ShadowTracePageResolution)),
                                uvec2(ShadowTracePageResolution - 1u));
}

// 📝 The atlas texel of a page's top-left corner. Pages are row-major in a ShadowTraceAtlasPageEdge² grid — mirrors ResolveShadowPageOrigin.
uvec2 ResolveShadowTracePageOrigin(uint PageIndex)
{
    return uvec2((PageIndex % ShadowTraceAtlasPageEdge) * ShadowTracePageResolution,
                 (PageIndex / ShadowTraceAtlasPageEdge) * ShadowTracePageResolution);
}

// Sample one clipmap level for one light-space position.
//
// 🔴 THIS IS A MACRO, NOT A FUNCTION, AND GLSL LEAVES NO CHOICE. The page lookup must index a storage buffer, and an SSBO's unsized array member
//    cannot be a function parameter in GLSL 450 — neither can the interface block that contains it. A function would therefore have to name a
//    specific buffer, which is exactly the hard-coded-binding trap this file's banner exists to avoid: the include would become usable by one unit
//    only. Textual substitution keeps the buffer name at the CALL site, so each consumer supplies its own.
// ⚠️ Every argument is parenthesized on use — `MappingArray` and `Level` arrive as arbitrary expressions.
//
// 🔴 THE TEXEL IS RESOLVED THROUGH THE MAPPING TABLE, NEVER FROM A UV. A page's position in the atlas is whatever the allocator handed out, so
//    there is no affine map from a world position to an atlas texel — the indirection IS the addressing. Mirrors the write chain step for step.
//
// 🔴 THE BIAS IS SUBTRACTED FROM THE RECEIVER, NEVER ADDED TO THE STORED DEPTH. Adding to a stored depth already at the clear identity would WRAP
//    it to 0 — the nearest code — and every texel no caster ever touched would occlude everything in its page. Pulling the receiver toward the sun
//    instead cannot wrap: it saturates at 0 through the clamp in the encode.
#define SampleShadowTraceLevel(OutTap, ShadowAtlas, MappingArray, CoverageArray, Level, LightPosition, ToroidalOrigin, BaseTileMetres,   \
                               DepthOriginMetres, DepthRangeMetres, DepthBias, NormalOffsetTexels)                                       \
{                                                                                                                                       \
    (OutTap).PageResident      = false;                                                                                                  \
    (OutTap).OccludedCondition = false;                                                                                                  \
    (OutTap).DepthMargin       = 1.0;                                                                                                    \
                                                                                                                                        \
    const float TraceTileMetres = (BaseTileMetres) * float(1u << (Level));                                                               \
                                                                                                                                        \
    /* 🔴 THE NORMAL OFFSET IS APPLIED PER LEVEL, IN LIGHT SPACE, BEFORE THE TILE IS RESOLVED — and it must be per level because a texel's  \
          world size DOUBLES every level, so one offset sized for L0 is 32x too small at L5 and one sized for L5 detaches every contact    \
          shadow at L0. It arrives already projected (NormalOffsetTexels is the receiver normal in LIGHT space, magnitude in texels) so    \
          this stays three multiplies with no basis to re-apply. */                                                                       \
    const float TraceTexelMetres    = TraceTileMetres / float(ShadowTracePageResolution);                                                 \
    const vec3  TraceLightPosition  = (LightPosition) + (NormalOffsetTexels) * TraceTexelMetres;                                          \
                                                                                                                                        \
    ivec2 TraceLightTile;                                                                                                                \
    uvec2 TraceWithinPage;                                                                                                               \
    ResolveShadowTraceTexel(TraceLightPosition.xy, TraceTileMetres, TraceLightTile, TraceWithinPage);                                     \
                                                                                                                                        \
    /* 🔴 CONTAINMENT FIRST, ON THE UNWRAPPED TILE — see ShadowTraceTileInsideWindow. The `&&` short-circuits, so the wrapped slot is never  \
          consulted for a receiver outside this level's window; without this the modulo silently aliases distant ground onto a real page and  \
          stamps this level's shadows across the floor with a period of exactly one window. */                                                \
    const bool TraceInsideWindow = ShadowTraceTileInsideWindow(TraceLightTile, (ToroidalOrigin));                                          \
    const uint TraceSlot         = ResolveShadowTraceSlot((Level), TraceLightTile, (ToroidalOrigin));                                      \
    if (TraceInsideWindow && TraceSlot < (MappingArray).length())                                                                          \
    {                                                                                                                                   \
        const uint TracePage = (MappingArray)[TraceSlot];                                                                                \
        /* 🔴 The sentinel test gates ALL arithmetic on the page index. ShadowTracePageUnmapped is 0xFFFFFFFF, so a page origin computed  \
              from it wraps to a plausible in-range texel and would silently read page 15's depth for an unmapped tile. */                \
        if (TracePage < uint(ShadowTraceAtlasPageEdge * ShadowTraceAtlasPageEdge))                                                       \
        {                                                                                                                               \
            const uvec2 TracePageOrigin = ResolveShadowTracePageOrigin(TracePage);                                                       \
            const ivec2 TraceTexel  = ivec2(TracePageOrigin + TraceWithinPage);                                                          \
            const uint  TraceStored = texelFetch(ShadowAtlas, TraceTexel, 0).r;                                                          \
                                                                                                                                        \
            /* 🔴 RESIDENT MEANS DRAWN INTO, NOT MERELY MAPPED. A page allocated from receiver demand that no caster covers is cleared    \
                  and then never rasterized, so it holds the identity everywhere; reporting it resident would answer "nothing occludes    \
                  me" from a page carrying no information AND suppress the coarser level that does have depth. See ShadowPageHoldsDepth. */\
            bool TracePageHoldsDepth;                                                                                                    \
            ShadowPageHoldsDepth(TracePageHoldsDepth, CoverageArray, TracePage);                                                          \
            (OutTap).PageResident = TracePageHoldsDepth;                                                                                  \
                                                                                                                                        \
            /* 🔴 The identity is named explicitly rather than left to the compare — see the constant's note on why the luck runs out. */ \
            if (TraceStored != ShadowTraceClearIdentity)                                                                                 \
            {                                                                                                                           \
                /* ⚠️ The OFFSET position's z, not the original — the offset moved this point laterally into a different texel, and pairing  \
                      that texel with the unoffset depth compares a sample from one place against a depth from another. */                 \
                const uint TraceReceiver = EncodeShadowTraceDepth(TraceLightPosition.z - (DepthBias),                                     \
                                                                  (DepthOriginMetres), (DepthRangeMetres));                              \
                /* ⚠️ Compared as uint, and the subtraction that follows is guarded by the branch — uint underflow would make an          \
                      unoccluded receiver report an enormous margin. Depth increases AWAY from the sun, so stored < receiver = occluded. */\
                (OutTap).OccludedCondition = (TraceStored < TraceReceiver);                                                              \
                (OutTap).DepthMargin       = (TraceStored < TraceReceiver)                                                               \
                                           ? -float(TraceReceiver - TraceStored) / ShadowTraceDepthEncodeScale                           \
                                           :  float(TraceStored - TraceReceiver) / ShadowTraceDepthEncodeScale;                          \
            }                                                                                                                           \
        }                                                                                                                               \
    }                                                                                                                                   \
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ANALYTIC LEVEL SELECTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 One tile of guard band, so a level's boundary sits INSIDE its valid data rather than on its edge. Mirrors EEVEE's `narrowing`:
//    RES / (RES - 1), the reference's own comment being "we need to hide one tile worth of data to hide the moving transition".
// 🔴 THIS IS HYSTERESIS, NOT A BLEND. Production VSMs do not cross-fade or dither between clipmap levels — a correct transition is invisible because
//    both levels describe the SAME shadow and differ only in filtering detail. Widening the band costs sharpness; removing it puts the boundary exactly
//    where the outermost tile's data ends, so the seam reappears whenever the window scrolls.
#define ShadowTraceLevelNarrowing (float(ShadowTraceTilemapResolution) / (float(ShadowTraceTilemapResolution) - 1.0001))

// The level whose texel size suits a receiver at this light-space offset from the clipmap centre. Each level doubles its world tile edge, so the index
// is the log2 of the distance expressed in windows — `ceil` rather than `floor` so the chosen level always CONTAINS the receiver.
//
// 🔴 THE LEVEL MUST BE A FUNCTION OF GEOMETRY ALONE, AND THE PREVIOUS FINEST-FIRST WALK MADE IT A FUNCTION OF ALLOCATION STATE INSTEAD. The walk began
//    at level 0 and took the first RESIDENT page, so a receiver 200 m away was answered by an L0 page whenever one happened to be resident over it,
//    while its neighbour at the same distance took L3 because that tile missed. Residency is itself driven by view-dependent marking, so the level for a
//    STATIONARY receiver changed as the camera moved — temporal flicker with no geometric cause — and level boundaries followed the ragged outline of
//    whatever got marked rather than concentric rings about the centre. Both symptoms are structural, not tuning.
// 🔴 MEASURED FROM THE CLIPMAP CENTRE, NOT FROM THE EYE, and that is what keeps the choice stable under pure rotation. The centre is derived from this
//    level's toroidal origin (below), which is world-anchored, so orbiting the camera cannot change any receiver's level. Feeding an eye-derived
//    distance here would reintroduce the orbit coupling: at Distance = 18 m a yaw sweep moves the eye +-36 tiles at L0, more than the whole window.
// ⚠️ The distance is LATERAL (light-plane xy) only. The forward axis is depth, which the atlas stores per texel rather than addressing tiles by;
//    including z would make a receiver's level depend on how far it is along the sun's travel, which no level boundary should care about.
//
// 🔴 THE NORM IS CHEBYSHEV (PER-AXIS MAX), NOT EUCLIDEAN, BECAUSE THE WINDOW IS A SQUARE. `length()` is smaller than the per-axis extent everywhere off
//    the axes — worst on the diagonal by a factor of sqrt(2) — so a Euclidean test hands a diagonal receiver a level whose square window does NOT contain
//    it. The containment gate then rejects the tile, the walk falls outward, and the receiver comes out lit: a bright wedge along both diagonals, widest
//    where the levels change. Measured in _ClaudeScratch/tmp/lvlcmp.py: Euclidean escapes on the diagonals, Chebyshev escapes ZERO of 33344 sampled
//    receivers inside L5's reach. Boundaries land at 7.75 / 15.5 / 31 / 62 / 124 m, each just inside the previous level's half-span.
uint SelectShadowTraceLevel(vec2 LateralMetres, vec2 CentreMetres, float BaseTileMetres, uint LevelCount)
{
    // The window's half-extent at level 0, in metres: the radius L0 can actually serve.
    const float BaseHalfSpan = BaseTileMetres * float(ShadowTraceTilemapResolution) * 0.5;
    if (BaseHalfSpan <= 0.0)
        return 0u;

    const vec2  Offset       = abs(LateralMetres - CentreMetres);
    const float OffsetMetres = max(Offset.x, Offset.y) * ShadowTraceLevelNarrowing;

    // 📝 Inside L0's half-span the ratio is below 1 and the log is negative, so clamp to 0 rather than letting the cast wrap.
    const float Ratio = OffsetMetres / BaseHalfSpan;
    if (Ratio <= 1.0)
        return 0u;

    const uint Level = uint(ceil(log2(Ratio)));
    return min(Level, LevelCount - 1u);
}

// This level's window centre in light-space METRES, recovered from its toroidal origin. 📝 The origin is the NEGATED window corner (ADD form), so the
// corner is -Origin tiles and the centre is -Origin + Resolution/2 tiles — the same expression ShadowDepthRasterSubmission uses to place the raster
// viewport, which is why the two agree by construction.
vec2 ResolveShadowTraceCentreMetres(ivec2 ToroidalOrigin, float BaseTileMetres, uint Level)
{
    const float TileMetres = BaseTileMetres * float(1u << Level);
    const vec2  CentreTile = vec2(-ToroidalOrigin) + vec2(float(ShadowTraceTilemapResolution) * 0.5);

    return CentreTile * TileMetres;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE LEVEL WALK
//------------------------------------------------------------------------------------------------------------------------

// Resolve sun visibility for one world point: walk from the finest level outward and use the first level that has a resident page.
//
// 🔴 FINEST-FIRST HERE IS THE OPPOSITE OF THE ALLOCATOR'S COARSEST-FIRST, AND BOTH ARE RIGHT. DriveShadowPageAllocation requests coarse levels
//    first so the coarse fallback is guaranteed to EXIST under pool pressure (its reclaim only ever evicts a level coarser than the requester, so
//    a fine-first drive starves the coarse tail — see ShadowPageAtlas.h). The reader wants the SHARPEST shadow available, so it walks the other
//    way and takes the first hit. The allocator guarantees the fallback exists; the reader prefers not to need it.
//
// 🔴 AN UNMAPPED TILE MUST NOT READ AS LIT, which is the entire reason the walk exists rather than a single level-0 tap. A tile with no page has no
//    depth, and treating that as "nothing occludes me" puts a brightly lit hole in the middle of a correct shadow wherever the pool came up short
//    — the exact artefact that reads as a shadow bug rather than as an allocation one. Falling back to a coarser page yields a blurrier shadow,
//    which is a degradation instead of a contradiction.
//
// ⚠️ Returns FULLY LIT when no level at all is resident. That is a deliberate last resort and not a silent success: with the whole column missing
//    there is no depth anywhere to consult, and darkening the pixel instead would paint shadow onto geometry the sun demonstrably reaches on the
//    next image. `OutResolvedLevel` reports ShadowTraceLodCount in that case so a caller (or a debug view) can tell the two apart.
//
// 📝 The bias is in light-space METRES and constant across levels. A coarser page covers more world per texel, so the same bias is proportionally
//    weaker there — acceptable while the tap is hard, and the reason the SMRT follow-up scales it per level.
// 📝 OutDepthMargin carries the winning tap's margin out for the debug view. 🔴 It is what separates the two failures a bare visibility cannot: a
//    resident page holding the CLEAR IDENTITY leaves the margin at the tap's initial 1.0, which is indistinguishable from "a caster is behind me" in
//    the 0..1 visibility but obvious as a margin pinned to exactly +1.0 across every texel of the page.
#define TraceSunShadowVisibility(OutVisibility, OutResolvedLevel, OutDepthMargin, ShadowAtlas, MappingArray, CoverageArray, LevelCount,  \
                                 LightPosition, ToroidalOriginArray, BaseTileMetres, DepthOriginMetres, DepthRangeMetres, DepthBias,      \
                                 LightNormal)                                                                                            \
{                                                                                                                                       \
    (OutVisibility)    = 1.0;                                                                                                            \
    (OutResolvedLevel) = ShadowTraceLodCount;                                                                                            \
    (OutDepthMargin)   = 1.0;                                                                                                            \
                                                                                                                                        \
    /* 📝 Sized once here rather than per level: the DIRECTION is level-independent, only the metres-per-texel scaling below varies. */    \
    const vec3 TraceNormalOffset = (LightNormal) * ShadowTraceNormalOffsetTexels;                                                        \
                                                                                                                                        \
    /* 🔴 THE ANALYTIC LEVEL IS THE STARTING POINT, AND THE WALK IS NOW ONLY A FALLBACK. Beginning at 0 made the level a function of which pages \
          happened to be resident — see SelectShadowTraceLevel. */                                                                          \
    /* ⚠️ THE CENTRE IS TAKEN FROM L0 AND THE LEVELS DO NOT SHARE ONE, which is a deliberate approximation rather than an oversight. Every level  \
          floors the SAME observer position at its OWN tile size before centring, so the centres agree only to within that level's tile — up to  \
          8 m apart at L5. L0's centre is the most precise of the six (0.5 m tiles), and the guard band above absorbs a fraction of a tile of    \
          error, so selecting AGAINST L0's centre is both stable and conservative. Using the candidate level's own centre would make the         \
          selection self-referential: the level determines the centre that determines the level. */                                            \
    const vec2 TraceCentreMetres = ResolveShadowTraceCentreMetres((ToroidalOriginArray)[0], (BaseTileMetres), 0u);                        \
    const uint TraceStartLevel   = SelectShadowTraceLevel((LightPosition).xy, TraceCentreMetres, (BaseTileMetres), (LevelCount));         \
                                                                                                                                        \
    /* ⚠️ Still walks OUTWARD from the analytic level rather than sampling it alone: the analytically-correct page can be genuinely absent under  \
          pool pressure, and a single tap would then report fully lit — a bright hole in a correct shadow. Falling to a coarser page is a       \
          degradation instead of a contradiction, which is the same reason the walk existed before. It just no longer STARTS below the level    \
          geometry calls for, so a resident fine page can never hijack a distant receiver. */                                                 \
    for (uint TraceLevel = TraceStartLevel; TraceLevel < (LevelCount); ++TraceLevel)                                                      \
    {                                                                                                                                   \
        ShadowTraceTap TraceTap;                                                                                                         \
        SampleShadowTraceLevel(TraceTap, ShadowAtlas, MappingArray, CoverageArray, TraceLevel, LightPosition,                            \
                               (ToroidalOriginArray)[TraceLevel], BaseTileMetres,                                                        \
                               DepthOriginMetres, DepthRangeMetres, DepthBias, TraceNormalOffset);                                        \
                                                                                                                                        \
        if (TraceTap.PageResident)                                                                                                        \
        {                                                                                                                               \
            (OutVisibility)    = TraceTap.OccludedCondition ? 0.0 : 1.0;                                                                 \
            (OutResolvedLevel) = TraceLevel;                                                                                             \
            (OutDepthMargin)   = TraceTap.DepthMargin;                                                                                    \
            break;                                                                                                                       \
        }                                                                                                                               \
    }                                                                                                                                   \
}

#endif
