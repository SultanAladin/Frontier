/*==============================================================================================================================================
                                                            SURFELGATHER.GLSL
==============================================================================================================================================*/
// 🧩 lookupSurfelGI — the recursive one-bounce gather. Given a world point + normal (a trace HIT during integrate, or a shade point during Phase-3
//    resolve), it finds the grid cell that point falls in, walks that cell's surfel list, and blends each nearby surfel's stored irradiance by a
//    geometric weight (radius falloff × normal alignment × Mahalanobis squish) gated by the surfel's own radial-depth occlusion, returning the
//    weighted-average irradiance. This is what closes the light transport loop: a surfel's integrate calls this at each ray hit to pick up the light
//    ALREADY accumulated by the surfels near that hit, so bounces compound across frames instead of costing a fresh ray tree each time. It also
//    atomicMax'es a per-surfel "touched" importance so the lifecycle keeps the surfels that actually contribute.
//
//    🔴 THIS IS AN #include MODULE WITH NO main(). It reads FOUR SSBOs by bare array name — `SurfelOffsets[]`, `SurfelList[]`, `Surfels[]` (an array of
//       SurfelRecord), `SurfelMoments[]` (the moments struct array) — and atomicMax'es `SurfelTouched[]`. The includer MUST declare all five before
//       #include'ing this, and MUST #include SurfelRecord.glsl, SurfelGrid.glsl, SurfelGuiding.glsl, and SurfelRadialDepth.glsl FIRST (this module calls
//       into all four). Compile through the .comp with glslc -I, never standalone.
//
//    🔴 F6 — OFFSETS/LIST SPLIT. webgiya packs cell offsets and the surfel list into ONE `offsetsAndList` SSBO and indexes the list part past a
//       OFFSETS_AND_LIST_START base. Frontier splits them into two buffers (SurfelGridSlotting owns them separately — the merge was a WebGPU 10-SSBO
//       workaround absent here). So `offsetsAndList[cellIdx]`→`SurfelOffsets[cellIdx]`, and `offsetsAndList[OFFSETS_AND_LIST_START + start + i]`→
//       `SurfelList[start + i]`. There is NO base offset on the list here — that is the whole point of the split. Everything else is 1:1 with
//       surfelIntegratePass.ts lookupSurfelGI (:753-862).

#ifndef FRONTIER_SURFEL_SURFELGATHER_GLSL
#define FRONTIER_SURFEL_SURFELGATHER_GLSL

// Per-cell lookup cap (webgiya MAX_SURFELS_PER_CELL_LOOKUP = 32) — the gather visits at most this many surfels in a hot cell to bound work. Structural.
const int SURFEL_MAX_SURFELS_PER_CELL_LOOKUP = 32;

// webgiya SURFEL_IMPORTANCE_INDIRECT_MAX = 50 — the scale the best contributor's length is mapped through before the touched atomicMax.
const float SURFEL_IMPORTANCE_INDIRECT_MAX = 50.0;

// 🔴 NEWBORN FADE-IN WINDOW. Falcor's blendingDelay is 240 frames, which suits a static-camera offline-ish accumulation; this cache is driven by a moving
//    eye and a ~500-frame TTL (SurfelGrid.glsl), so a 240-frame ramp would keep a surfel dim for half its life and starve the very coverage the spawn
//    logic just paid for. 16 frames is matched to the estimator instead: the responsive branch in SurfelIntegrate blends as a plain running average until
//    32 samples, and the guiding ramp there is already clamp(SinceBirth/16). At 60fps this is ~0.27s — long enough to hide the pop, short enough that a
//    newly-covered region does not read dark while the camera moves through it.
const float SURFEL_FADE_IN_FRAMES = 16.0;

// radius_based_epsilon (surfelIntegratePass.ts:148-153). A radius-proportional offset pushing the surfel sample point off its own surface so the depth
// learning and the gather agree on the origin. Guarded so the includer can reuse the same helper for its own trace-origin offset.
#ifndef FRONTIER_SURFEL_RADIUS_EPSILON
#define FRONTIER_SURFEL_RADIUS_EPSILON
float SurfelRadiusBasedEpsilon(float SurfelRadius)
{
    return clamp(SurfelRadius * 0.01, 0.0005, 0.01);
}
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE GATHER
//------------------------------------------------------------------------------------------------------------------------

// lookupSurfelGI (:753-862). PointWorld/NormalWorld is the shade/hit point; CameraPosition is the raw eye (the radius model measures from it);
// GridOrigin is the snapped grid origin (matching the build); ReadOffsetElements is the moments read-half element base (MomentsParity*Capacity — the
// caller passes the ELEMENT index, not the byte offset, Phase-2 fact 4); OcclusionParams is (shadowStrength, bleedReduction, grazingBiasScale, varianceBleedScale).
//
// 📝 An uncovered point returns HARD ZERO. A tiered low-coverage fallback (mixing toward a cell-average where TotalWeight was thin) was tried and
//    REMOVED with the rest of the convergence work, so the black dots at grazing incidence are back and are a property of the coverage, not a bug here.
//
// 🔴 CurrentFrame DRIVES THE NEWBORN FADE-IN and is the ONLY reason this signature grew. A freshly spawned surfel has integrated almost nothing, so at
//    full weight it POPS: its slot is now zeroed at birth (see SurfelIntegrate's birth reset), which cures the inherited-radiance error but means the
//    first frames legitimately read near-black. Ramping a young surfel's weight in over SURFEL_FADE_IN_FRAMES lets the incumbent coverage carry the pixel
//    while the newcomer converges, instead of the pixel lurching to the newcomer's un-converged value. This is GIBS/Falcor's blendingDelay in miniature.
//    📝 NO PUSH BLOCK CHANGED FOR THIS. Every one of the three callers already had a frame index to hand — SurfaceShade.frag has Constants.ShadowFrame,
//       SurfelIntegrate.comp has Constants.Frame, and SurfelGatherProbe.comp spends one of its two existing pads. So the documented push-block hazard
//       (a mid-struct insert silently re-offsetting every field below it) is simply not in play here.
vec3 SurfelLookupGI(vec3 PointWorld, vec3 NormalWorld, vec3 CameraPosition, vec3 GridOrigin, uint ReadOffsetElements, vec4 OcclusionParams,
                    uint CurrentFrame)
{
    // Position relative to the snapped grid origin — matches the grid build exactly.
    vec3 PositionRelative = PointWorld - GridOrigin;

    ivec3 GridCoord = SurfelPositionToGridCoord(PositionRelative);
    uvec4 Cell      = SurfelGridCoordToCell(GridCoord);
    uint  Hash      = SurfelCellToHash(Cell);
    int   CellIndex = int(Hash % SURFEL_TOTAL_CELLS);

    int Start = SurfelOffsets[CellIndex];
    int End   = SurfelOffsets[CellIndex + 1];
    int Count = max(End - Start, 0);

    // Clamp to avoid insane work in a hot cell.
    int MaxCount = min(Count, SURFEL_MAX_SURFELS_PER_CELL_LOOKUP);
    if (MaxCount <= 0)
    {
        return vec3(0.0);   // the point's own cell is empty
    }

    vec3  TotalColour = vec3(0.0);
    float TotalWeight = 0.0;

    float BestContribution = 0.0;
    int   BestSurfelId     = 0;

    for (int i = 0; i < MaxCount; i = i + 1)
    {
        int SurfelId = SurfelList[Start + i];       // F6: no OFFSETS_AND_LIST_START base — the split list is its own buffer
        if (SurfelId < 0)
        {
            continue;
        }

        SurfelRecord Surfel = Surfels[uint(SurfelId)];
        vec3 SurfelPosition = Surfel.PositionAndSpare.xyz;
        vec3 SurfelNormal   = normalize(Surfel.Normal);

        // Surfel radius + falloff.
        vec3  PositionRelativeSurfel = SurfelPosition - CameraPosition;
        float SurfelRadius = SurfelRadiusForPosition(PositionRelativeSurfel) * SURFEL_RADIUS_OVERSCALE;

        // Match the origin offset the depth learning uses.
        float Epsilon           = SurfelRadiusBasedEpsilon(SurfelRadius);
        vec3  SurfelPositionOff = SurfelPosition + SurfelNormal * Epsilon;

        vec3  Offset   = PointWorld - SurfelPositionOff;
        float Distance = length(Offset);
        vec3  Direction = Offset / Distance;

        float Alignment  = abs(dot(Offset, SurfelNormal));
        float Mahalanobis = Distance * (1.0 + Alignment * SURFEL_NORMAL_DIRECTION_SQUISH);

        float Directional = max(0.0, dot(SurfelNormal, NormalWorld));
        float Weight = smoothstep(SurfelRadius, 0.0, Mahalanobis) * Directional;

        // 🔴 NEWBORN FADE-IN — ramp a young surfel's INFLUENCE, not its irradiance. Scaling the weight (rather than the colour) is what makes this a true
        //    cross-fade: the weighted average re-normalises by TotalWeight below, so a half-faded newborn simply cedes half its say to the surfels already
        //    covering the point. Scaling the colour instead would drag the average toward black, which is the very artefact this is meant to remove.
        //    The birth frame rides PositionAndSpare.w — the same field SurfelAllocate stamps and the despawn immunity reads, so all three agree on "age".
        //    ⚠️ Applied BEFORE the BestContribution comparison below on purpose: the winner of that comparison gets the keep-alive importance atomicMax, and
        //       a newborn that has not earned its place must not out-vote an established contributor for that income on its first frame.
        float AgeFrames = float(CurrentFrame) - max(Surfel.PositionAndSpare.w, 0.0);
        Weight *= clamp(AgeFrames / SURFEL_FADE_IN_FRAMES, 0.0, 1.0);

        if (Weight <= 0.0)
        {
            continue;
        }

        // MSM radial-depth visibility gate (0..1). Perf guard: only run the 4MSM where the geometric weight matters (webgiya's 0.02 threshold).
        if (Weight > 0.02)
        {
            float Visibility = SurfelRadialOcclusionRw(uint(SurfelId), Direction, SurfelNormal, Distance, OcclusionParams);
            Weight *= Visibility;
            if (Weight <= 0.0) { continue; }
        }

        uint ReadIndex = uint(SurfelId) + ReadOffsetElements;
        vec3 SurfelIrradiance = SurfelMoments[ReadIndex].Irradiance.xyz;

        vec3  Contribution = SurfelIrradiance * Weight;
        float ContributionLength = length(Contribution);
        if (ContributionLength > BestContribution)
        {
            BestContribution = ContributionLength;
            BestSurfelId     = SurfelId;
        }

        TotalWeight += Weight;
        TotalColour += Contribution;
    }

    // Bump the best contributor's importance so the lifecycle keeps it alive (indirect range 1..50).
    if (BestSurfelId >= 0 && BestContribution > 0.01)
    {
        int Importance = clamp(int(BestContribution * SURFEL_IMPORTANCE_INDIRECT_MAX), 0, 50);
        atomicMax(SurfelTouched[BestSurfelId], Importance);
    }

    // 🔴 THE TEST IS ON TotalWeight, NOT ON THE SURFEL COUNT. A cell can hold 30 surfels that all face away from this point: Count = 30 but every
    //    Directional term is 0, so TotalWeight is 0. Gating on emptiness alone (the MaxCount check above) catches only the empty-cell case, and dividing
    //    by a near-zero weight amplifies one barely-aligned surfel's irradiance into pure noise.
    if (TotalWeight < 1e-5) { return vec3(0.0); }   // surfels present, none of them cover this point at all
    return TotalColour / TotalWeight;
}

#endif
