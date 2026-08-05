/*==============================================================================================================================================
                                                           SURFELCOVERAGE.GLSL
==============================================================================================================================================*/
// 🧩 The GIBS "rule 2" coverage measure: how well the surfels already in a cell cover one surface point. Given a point (world position + normal)
//    and one candidate surfel, it returns that surfel's CONTRIBUTION WEIGHT — a smoothstep falloff over the Mahalanobis distance (radial distance
//    inflated by the along-normal offset, so a surfel on a parallel surface a short distance away counts for little) multiplied by the normal
//    alignment. Summing the weight over a cell's surfels gives the cell's coverage at that point; the sum is the quantity the spawn gate, the
//    despawn vote, and the eviction gate all threshold.
//
//    🔴 THIS WAS EXTRACTED FROM SurfelSpawnRequest.comp:218-246, WHICH IS THE ONLY PLACE IT EVER LIVED, AND THE EXTRACTION MUST NOT CHANGE THE
//       ARITHMETIC. The tile-election spawn path is the shipped, measured behaviour — its population (~1017 alive) is the baseline every A/B is
//       read against. If this module's weight differs from the inlined original by even a smoothstep argument order, that baseline moves and every
//       comparison silently re-bases. The extraction exists to let a SECOND consumer (the eviction gate) measure coverage the SAME way, which is
//       the same reason SurfelVisibilityReconstruct.glsl exists: that math was duplicated between the spawn pass and SurfaceShade.frag and could
//       drift. One definition, several consumers.
//
//    🔴 TWO RADII, NOT ONE, AND THEY ARE NOT INTERCHANGEABLE. The original computes the weight TWICE per surfel against different radii:
//         • WEIGHT  uses radius * SURFEL_RADIUS_OVERSCALE — the LOOSE measure. Feeds the spawn gate's second-best test and the despawn victim pick.
//         • SCORING uses the bare radius — the TIGHT measure. Feeds the "is this pixel covered at all" and over-coverage thresholds.
//       The overscaled sum runs higher than the tight sum for the same configuration, so a threshold fitted against one is meaningless against the
//       other. SurfelCoverageSample returns BOTH and names them, because the inlined version distinguished them only by variable name and a
//       consumer reading the wrong field gets a plausible number rather than an error.
//
//    🔴 THIS IS AN #include MODULE — NO main, NO bindings. It needs SurfelGrid.glsl (for SURFEL_NORMAL_DIRECTION_SQUISH, SURFEL_RADIUS_OVERSCALE and
//       the radius helpers) already included by the consumer, and the consumer owns the loop over its own cell's surfel list — this module holds no
//       opinion about where the surfels come from, so the world-space passes and the screen-space passes can both call it.

#ifndef FRONTIER_SURFEL_COVERAGE_GLSL
#define FRONTIER_SURFEL_COVERAGE_GLSL

// One candidate surfel's contribution to the coverage of one surface point, at both radii.
struct SurfelCoverageWeights
{
    float Weight;    // [-] - loose measure, radius * SURFEL_RADIUS_OVERSCALE (spawn second-best test, despawn victim pick)
    float Scoring;   // [-] - tight measure, bare radius (covered-at-all test, over-coverage threshold)
    float DotNormal; // [-] - the alignment factor both weights carry, exposed for the keep-alive test's own 0.8 cut
};

// Measure how much SurfelPosition/SurfelNormal (a surfel with the given eye-relative radius) covers the surface point Point/PointNormal.
// 🔴 The arithmetic is byte-for-byte the inlined original: Mahalanobis = |offset| * (1 + |offset.n| * SQUISH), both smoothsteps run
//    HIGH-to-LOW (smoothstep(edge, 0, x) — descending, so a distance of 0 scores 1 and a distance past the edge scores 0), and the
//    alignment is clamped at 0 rather than abs()'d, so a back-facing surfel contributes NOTHING instead of contributing positively.
SurfelCoverageWeights SurfelCoverageSample(vec3  Point,
                                           vec3  PointNormal,
                                           vec3  SurfelPosition,
                                           vec3  SurfelNormal,
                                           float SurfelRadius)
{
    vec3  Offset       = Point - SurfelPosition;
    float DistanceLen  = length(Offset);
    float AlignPenalty = abs(dot(Offset, SurfelNormal)) * SURFEL_NORMAL_DIRECTION_SQUISH;
    float Mahalanobis  = DistanceLen * (1.0 + AlignPenalty);

    SurfelCoverageWeights Result;
    Result.DotNormal = max(dot(SurfelNormal, PointNormal), 0.0);
    Result.Weight    = smoothstep(SurfelRadius * SURFEL_RADIUS_OVERSCALE, 0.0, Mahalanobis) * Result.DotNormal;
    Result.Scoring   = smoothstep(SurfelRadius, 0.0, Mahalanobis) * Result.DotNormal;
    return Result;
}

// The running accumulator a consumer folds each SurfelCoverageSample into, mirroring the four values the inlined loop kept.
// Highest/Second track the two largest LOOSE weights — the spawn gate's "is there a strong second opinion" test needs the runner-up, not the max.
struct SurfelCoverageTotals
{
    float TotalWeight;     // [-] - sum of the loose weights over the cell
    float ScoringWeight;   // [-] - sum of the tight weights over the cell
    float Highest;         // [-] - largest single loose weight
    float Second;          // [-] - second largest loose weight
};

SurfelCoverageTotals SurfelCoverageBegin()
{
    SurfelCoverageTotals Totals;
    Totals.TotalWeight   = 0.0;
    Totals.ScoringWeight = 0.0;
    Totals.Highest       = 0.0;
    Totals.Second        = 0.0;
    return Totals;
}

// 🔴 The Highest/Second update is the original's if/else-if CHAIN, not two independent tests. A weight that beats Highest must push the old Highest
//    down into Second; writing them as separate ifs would let one sample set both and lose the previous runner-up.
void SurfelCoverageAccumulate(inout SurfelCoverageTotals Totals, SurfelCoverageWeights Sample)
{
    Totals.TotalWeight   += Sample.Weight;
    Totals.ScoringWeight += Sample.Scoring;

    if (Sample.Weight > Totals.Highest)     { Totals.Second = Totals.Highest; Totals.Highest = Sample.Weight; }
    else if (Sample.Weight > Totals.Second) { Totals.Second = Sample.Weight; }
}

#endif
