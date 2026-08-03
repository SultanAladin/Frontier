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
vec3 SurfelLookupGI(vec3 PointWorld, vec3 NormalWorld, vec3 CameraPosition, vec3 GridOrigin, uint ReadOffsetElements, vec4 OcclusionParams)
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
        return vec3(0.0);
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

    if (TotalWeight < 1e-5) { return vec3(0.0); }
    return TotalColour / TotalWeight;
}

#endif
