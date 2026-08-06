/*==============================================================================================================================================
                                                    SURFELIRRADIANCEACCUMULATION.GLSL
==============================================================================================================================================*/
// 🧩 The READ side of the surfel field — the one-bounce indirect a shaded pixel collects from the surfels sitting on its own surface patch. Phase 8 of
//    the W298/SurfelGI port, and the counterpart to SurfelRadianceIntegrate.comp: the integrate publishes each surfel's filtered radiance and its
//    depth moments, and this file turns that cache back into irradiance at an arbitrary world position. Ported from W298/SurfelGI's surfel evaluation
//    (SurfelGIRenderPass's gather over getSurfelCoverage).
//
// 🔴 NO BINDINGS OF ITS OWN, BY DESIGN. Like TwoLevelTrace.glsl, this is a guarded module with no main() that reads its includer's set-1 declarations by
//    BARE NAME — SurfelRecords / SurfelCellSpans / SurfelCellOrdinals / SurfelDepthAtlas. The includer (SurfaceShade.frag) owns the set index and the
//    binding numbers, so the same gather can be pulled into a second consumer without either one agreeing on a descriptor layout. ⚠️ Include it AFTER
//    those four declarations are in scope; a missing one reads as an undeclared-identifier error at the first use, not at the include.
//
// 🔴 THE CENTRE CELL ONLY, NOT THE 125-CELL NEIGHBOURHOOD THE CENSUS WALKS. A surfel is written into the cell list of EVERY cell its disc reaches
//    (SurfelCellScatter.comp), so a point's own cell already lists every surfel that can possibly cover it — walking the neighbourhood would re-find the
//    same surfels through their other cells and weight each of them several times over. ⚠️ This is only true while ResolveSurfelRadius clamps the radius
//    to CellEdge * 2 AND the scatter keeps writing every intersected cell; changing either makes the single-cell read incomplete rather than merely
//    cheaper.
//
// ⚠️ EVERY POSITION HERE IS WORLD-SPACE METRES AND THE GRID IS CAMERA-RELATIVE. CameraOrigin must be the SAME value the census and the scatter were
//    pushed this frame, or the cell coordinate resolved here names a cell the field was never written into — which returns an empty run rather than an
//    error, so the indirect simply vanishes with nothing to point at.

#ifndef FRONTIER_SURFEL_IRRADIANCE_ACCUMULATION_GLSL
#define FRONTIER_SURFEL_IRRADIANCE_ACCUMULATION_GLSL

#include "SurfelTypes.glsl"
#include "SurfelCellGrid.glsl"
#include "SurfelAtlasAddressing.glsl"

//------------------------------------------------------------------------------------------------------------------------
//                                                      ACCUMULATION LIMITS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The per-pixel walk ceiling, and it is a FRAME-BUDGET guard rather than a correctness one. A cell's run is bounded only by the cell list's capacity,
//    so a pathological pile-up (a concave corner every surfel's disc reaches into) would make one pixel walk hundreds of records while its neighbour
//    walks four — divergence inside a fragment quad, paid by the whole quad. ⚠️ Raising it costs worst-case pixels, never average ones; lowering it
//    truncates the run, which reads as the indirect going dim in exactly the crowded corners it should be strongest.
const uint SurfelAccumulationCeiling = 64u;

// A surfel nearer than this to the shading point has no meaningful direction toward it, so the occlusion test is skipped rather than run against a
// normalize() of a near-zero vector.
const float SurfelAccumulationEpsilon = 1e-5;   // [m]

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE OCCLUSION TEST
//------------------------------------------------------------------------------------------------------------------------

// How much of SourceOrdinal's light reaches a point Separation metres away along TowardPoint. Chebyshev's inequality over the two depth moments the
// integrate stored in that surfel's tile — the same test SurfelRadianceIntegrate.comp's ResolveSurfelVisibility runs, with one deliberate difference.
//
// 🔴 THIS TAP IS BILINEAR WHERE THE INTEGRATE'S IS NEAREST, AND THAT IS WHAT THE TILE BORDER EXISTS FOR. The integrate holds the atlas as a STORAGE
//    image it is writing, so it can only imageLoad a texel; this pass holds it as a SAMPLED image and can filter. A shading point slides continuously
//    across a surfel's hemisphere, so a nearest tap would quantize its occlusion into the 5x5 payload's cells and stair-step every contact shadow. The
//    one-texel border CopySurfelDepthBorder writes each frame is precisely the margin this filter's edge taps reach into.
//
// ⚠️ A tile that has never been written reads as zero moments, which literally means "an occluder at distance 0". Full visibility is returned for that
//    case deliberately: an unwritten tile is an ABSENCE of evidence, and treating it as total occlusion would make every freshly spawned surfel invisible
//    at exactly the moment its neighbourhood most needs it.
float ResolveSurfelAccumulationVisibility(uint SourceOrdinal, vec3 SourceNormal, vec3 TowardPoint, float Separation)
{
    const vec2 Moments = texture(SurfelDepthAtlas, ResolveSurfelAtlasSampleUv(SourceOrdinal, TowardPoint, SourceNormal)).rg;

    if (Moments.x <= 0.0)
        return 1.0;                 // never written; see the note above
    if (Separation <= Moments.x)
        return 1.0;                 // nearer than the mean occluder — unambiguously visible, no inequality needed

    const float Variance   = max(0.0, Moments.y - Moments.x * Moments.x);
    const float Difference = Separation - Moments.x;
    return clamp(Variance / (Variance + Difference * Difference), 0.0, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE GATHER
//------------------------------------------------------------------------------------------------------------------------

// 🧩 The indirect radiance arriving at WorldPosition on a surface facing SurfaceNormal, plus how much of the answer the field could actually account for.
//    Returns rgb = the coverage-weighted MEAN of the contributing surfels' radiance [W/m²/sr], a = coverage ∈ [0,1].
//
// 🔴 THE RETURN IS A MEAN AND A CONFIDENCE, NOT A SUM. Dividing by the total weight is what makes the result independent of how many surfels happen to
//    sit on this patch — a point covered by two converged surfels and a point covered by twenty must read the same brightness, or the indirect would
//    pulse as the field spawns and recycles around it. The separate coverage channel carries "how much did the field actually know here", which is what
//    the caller blends the flat fill back in with. ⚠️ Folding the two together (returning an unnormalized sum) makes a sparsely covered region DARK
//    rather than UNKNOWN, and the two want opposite treatment.
//
// 📝 Three rejection tests, in ascending cost, and each answers a different failure:
//       ① radius   — the point is outside the surfel's disc, so the surfel says nothing about it
//       ② facing   — the surfel is on another face of the same wall; its light is the other room's
//       ③ Chebyshev— the two face the same way and are close, but something stands between them (a doorway's two jambs)
vec4 AccumulateSurfelIrradiance(vec3 WorldPosition, vec3 SurfaceNormal, vec3 CameraOrigin)
{
    const ivec3 CentreCell = ResolveCellCoordinate(WorldPosition, CameraOrigin);
    if (!CellCoordinateValid(CentreCell))
        return vec4(0.0);           // outside the grid's 32 m reach — the field has no cell here, which is not the same as no light

    const SurfelCellSpan Span = SurfelCellSpans[FlattenCellIndex(CentreCell)];

    vec3  Accumulated = vec3(0.0);
    float TotalWeight = 0.0;

    const uint EntryCount = min(Span.SurfelCount, SurfelAccumulationCeiling);
    for (uint Entry = 0u; Entry < EntryCount; ++Entry)
    {
        const uint TableSlot = Span.TableOffset + Entry;
        if (TableSlot >= SurfelCellListCapacity)
            break;                  // a corrupt span must not read past the table

        const uint SurfelOrdinal = SurfelCellOrdinals[TableSlot];
        if (SurfelOrdinal >= SurfelTotalLimit)
            continue;

        const Surfel Record = SurfelRecords[SurfelOrdinal];
        if (Record.Radius <= 0.0)
            continue;               // a dead slot the recycle has not reclaimed yet

        const vec3  SurfelPosition = FetchSurfelPosition(Record);
        const vec3  Separation     = WorldPosition - SurfelPosition;
        const float Reach          = length(Separation);
        if (Reach >= Record.Radius)
            continue;               // ① outside the disc

        const vec3  SurfelNormal = normalize(FetchSurfelNormal(Record));
        const float Facing       = dot(SurfelNormal, SurfaceNormal);
        if (Facing <= 0.0)
            continue;               // ② the far face of the same wall

        // ③ The donor's own depth tile is the only record of anything standing between the two, so the test runs from ITS frame toward this point.
        float Visibility = 1.0;
        if (Reach > SurfelAccumulationEpsilon)
            Visibility = ResolveSurfelAccumulationVisibility(SurfelOrdinal, SurfelNormal, Separation / Reach, Reach);
        if (Visibility <= 0.0)
            continue;

        // 📝 Linear in the disc's own radius, matching the integrate's sharing weight — so a surfel's influence dies exactly at its rim and two adjacent
        //    surfels hand the surface over to each other without a seam. Squaring it would darken the mid-disc for no physical reason.
        const float Weight = Facing * (1.0 - Reach / Record.Radius) * Visibility;

        Accumulated += FetchSurfelRadiance(Record) * Weight;
        TotalWeight += Weight;
    }

    if (TotalWeight <= 0.0)
        return vec4(0.0);

    // Coverage saturates at one full-weight surfel. 💡 A point directly under one converged surfel facing straight at it is fully explained; anything
    // less is a partial answer the caller must fade the flat fill back into, and anything more is still just "fully explained".
    return vec4(Accumulated / TotalWeight, clamp(TotalWeight, 0.0, 1.0));
}

#endif
