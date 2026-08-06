/*==============================================================================================================================================
                                                          SURFELMEANESTIMATOR.GLSL
==============================================================================================================================================*/
// 🧩 The Multiscale Mean Estimator — the temporal filter that turns a surfel's handful of noisy rays per frame into a stable radiance. Ported 1:1 from
//    W298/SurfelGI's MultiscaleMeanEstimator.slang, which is itself the Ray Tracing Gems Ch.25 (Hybrid Rendering for Real-Time Ray Tracing) estimator.
//    No bindings, no storage — one function over the 44-byte carried state SurfelTypes.glsl declares, so the integrate and any future consumer share it.
//
// 💡 WHY TWO MEANS. A single exponential average must choose between converging fast (and staying noisy) and converging slowly (and lagging when the
//    light actually changes). This carries BOTH: a fast `ShortMean` that follows the samples, and a slow `Mean` that is the output. The gap between them
//    is the signal that something real changed rather than that a ray got lucky — which is what `Inconsistency` measures and what opens the catch-up
//    blend. So the estimator is quiet when the light is steady and quick when it is not, without a per-surfel tuning knob.
//
// 🔴 vbbr STARTS AT ZERO, AND THAT FREEZES Mean FOR THE FIRST FEW FRAMES. The catch-up blend is multiplied by vbbr, so on a freshly seeded estimator the
//    blend is exactly 0 and `Mean` does not move at all — no matter what the rays report. vbbr then climbs toward its target at 0.1 per frame, so the
//    surfel starts responding after a handful of frames. ⚠️ This is upstream's behaviour and it is NOT a defect to be tuned away: it is why the spawn
//    SEEDS Mean and ShortMean from the surrounding surfels' light instead of from black. A surfel seeded at zero would sit black on screen for its first
//    frames and read as a flickering dark speck. Any new spawn path must seed both means the same way.
//
// 🔴 THE ORDER OF THE LAST FOUR LINES IS LOAD-BEARING. `CatchUpBlend` is multiplied by the OLD vbbr, and vbbr is advanced only afterwards. Advancing vbbr
//    first makes the estimator respond a frame early and, more importantly, changes its response permanently rather than by one frame — the two are in a
//    feedback loop through Mean. Upstream's ordering is preserved exactly; do not tidy it into "update all state, then blend".
//
// ⚠️ EVERY DIVISION HERE IS GUARDED BY max(1e-5, ...) AND THAT IS NOT DECORATION. A surfel whose rays all return the same value has zero variance, so the
//    deviation is zero and both `relativeDiff` and the blend reduction divide by it. Without the floor the estimator produces a NaN, the NaN rides mix()
//    into Mean, and from there it is PERMANENT — every subsequent frame mixes against a NaN and stays NaN. That is what SanitiseSurfelMeanEstimator below
//    exists to catch; the floors are what stop it being needed.

#ifndef FRONTIER_SURFEL_MEAN_ESTIMATOR_GLSL
#define FRONTIER_SURFEL_MEAN_ESTIMATOR_GLSL

#include "SurfelTypes.glsl"

//------------------------------------------------------------------------------------------------------------------------
//                                                       ESTIMATOR CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Rec.709 luma weights. Upstream uses them to collapse a per-channel disagreement into one scalar before comparing it against a threshold — a green
//    shift matters more to the eye than an equal blue one, and the estimator's responsiveness should follow that.
const vec3 SurfelLumaWeights = vec3(0.299, 0.587, 0.114);

// The firefly clamp's shoulder. A sample above ShortMean + 8 deviations is trimmed back to the shoulder rather than dropped, so a genuine step change
// still moves the estimator — just at a bounded rate.
const float SurfelFireflyDeviations = 8.0;
const float SurfelFireflyFloor      = 0.1;   // [W/m²/sr] - added to the shoulder so a near-black surfel is not clamped by its own noise floor

// Blend rates, all upstream's.
const float SurfelVarianceBlendScale  = 0.5;    // [-] - variance follows on a window twice as long as the short mean (see upstream's note)
const float SurfelInconsistencyBlend  = 0.08;   // [-] - how fast the mean/short-mean disagreement itself is smoothed
const float SurfelBlendReductionRate  = 0.1;    // [-] - how fast vbbr follows its target
const float SurfelCatchUpFloor        = 1.0 / 256.0;   // [-] - the slowest the long mean may move once vbbr has opened
const float SurfelBlendReductionFloor = 1.0 / 32.0;    // [-] - vbbr's own floor; zero here would latch the mean forever

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE ESTIMATOR
//------------------------------------------------------------------------------------------------------------------------

// Advance Estimator by one frame's observation and return the filtered mean. Upstream MSME(y, data, shortWindowBlend).
// 📝 ShortWindowBlend is upstream's gShortMeanWindow, pushed from the host (0.08 by default): larger follows the samples faster and is noisier.
vec3 AdvanceSurfelMeanEstimator(vec3 Observation, inout SurfelMeanEstimator Estimator, float ShortWindowBlend)
{
    vec3  Mean          = vec3(Estimator.MeanX, Estimator.MeanY, Estimator.MeanZ);
    vec3  ShortMean     = vec3(Estimator.ShortMeanX, Estimator.ShortMeanY, Estimator.ShortMeanZ);
    float BlendReduction = Estimator.VarianceBlendReduction;
    vec3  Variance      = vec3(Estimator.VarianceX, Estimator.VarianceY, Estimator.VarianceZ);
    float Inconsistency = Estimator.Inconsistency;

    //-- Suppress fireflies ---------------------------------------------------------------------------------------------
    // A single ray that struck a bright specular path can be orders of magnitude above the rest. Trimming it to the shoulder keeps its DIRECTION of
    // influence while removing its magnitude, which is what stops one ray dragging a whole surfel bright for a hundred frames.
    {
        const vec3 Deviation     = sqrt(max(vec3(1e-5), Variance));
        const vec3 HighThreshold = SurfelFireflyFloor + ShortMean + Deviation * SurfelFireflyDeviations;
        const vec3 Overflow      = max(vec3(0.0), Observation - HighThreshold);
        Observation -= Overflow;
    }

    //-- Advance the short mean and the variance ------------------------------------------------------------------------
    // ⚠️ Delta is measured BEFORE the short mean moves and Delta2 AFTER it; their product is the incremental-variance form (Welford-style). Measuring
    //    both on the same side collapses to a squared residual and biases the variance high, which inflates every ray-count ladder that reads it.
    const vec3 Delta = Observation - ShortMean;
    ShortMean = mix(ShortMean, Observation, ShortWindowBlend);
    const vec3 Delta2 = Observation - ShortMean;

    Variance = mix(Variance, Delta * Delta2, ShortWindowBlend * SurfelVarianceBlendScale);
    const vec3 Deviation = sqrt(max(vec3(1e-5), Variance));

    //-- How much the two means disagree ----------------------------------------------------------------------------------
    const vec3  ShortDifference = Mean - ShortMean;
    const float RelativeDifference = dot(SurfelLumaWeights, abs(ShortDifference) / max(vec3(1e-5), Deviation));
    Inconsistency = mix(Inconsistency, RelativeDifference, SurfelInconsistencyBlend);

    //-- The blend ---------------------------------------------------------------------------------------------------------
    // 💡 The reduction is the short mean measured in deviations: a surfel whose signal is large compared to its noise gets a blend near 1 (trust the
    //    samples), and a surfel lost in noise gets the 1/32 floor (trust the history).
    const float ReductionTarget = clamp(dot(SurfelLumaWeights, 0.5 * ShortMean / max(vec3(1e-5), Deviation)),
                                        SurfelBlendReductionFloor, 1.0);

    // The -0.2 offset is a dead band: a small, steady disagreement is noise and must not open the blend at all. Only a disagreement that survives the
    // inconsistency smoothing pushes the smoothstep off zero.
    vec3 CatchUpBlend = clamp(smoothstep(0.0, 1.0, vec3(RelativeDifference * max(0.02, Inconsistency - 0.2))),
                              vec3(SurfelCatchUpFloor), vec3(1.0));

    // 🔴 The OLD vbbr, then the advance. See the file header.
    CatchUpBlend  *= BlendReduction;
    BlendReduction = mix(BlendReduction, ReductionTarget, SurfelBlendReductionRate);

    Mean = mix(Mean, Observation, clamp(CatchUpBlend, vec3(0.0), vec3(1.0)));

    //-- Carry the state forward -------------------------------------------------------------------------------------------
    Estimator.MeanX = Mean.x; Estimator.MeanY = Mean.y; Estimator.MeanZ = Mean.z;
    Estimator.ShortMeanX = ShortMean.x; Estimator.ShortMeanY = ShortMean.y; Estimator.ShortMeanZ = ShortMean.z;
    Estimator.VarianceBlendReduction = BlendReduction;
    Estimator.VarianceX = Variance.x; Estimator.VarianceY = Variance.y; Estimator.VarianceZ = Variance.z;
    Estimator.Inconsistency = Inconsistency;

    return Mean;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE SANITISE
//------------------------------------------------------------------------------------------------------------------------

// 🔴 THE LAST LINE OF DEFENCE, AND IT MUST SCRUB Mean AND ShortMean — NOT ONLY THE THREE BOOKKEEPING FIELDS. A NaN anywhere in this state is PERMANENT:
//    every later frame runs it through mix() and mix(NaN, x, t) is NaN, so one bad frame poisons the surfel for the rest of its life. The previous port
//    learned this the expensive way — its sanitise clamped vbbr, variance and inconsistency but left the two means alone, and a single NaN mean survived
//    every clamp and rode the estimator forever.
// 📝 isnan() is used rather than a self-comparison because some drivers optimize `x != x` away under fast-math assumptions; isinf() catches the overflow
//    a runaway firefly can produce before it becomes a NaN.
void SanitiseSurfelMeanEstimator(inout SurfelMeanEstimator Estimator)
{
    if (isnan(Estimator.MeanX) || isinf(Estimator.MeanX)) Estimator.MeanX = 0.0;
    if (isnan(Estimator.MeanY) || isinf(Estimator.MeanY)) Estimator.MeanY = 0.0;
    if (isnan(Estimator.MeanZ) || isinf(Estimator.MeanZ)) Estimator.MeanZ = 0.0;

    if (isnan(Estimator.ShortMeanX) || isinf(Estimator.ShortMeanX)) Estimator.ShortMeanX = 0.0;
    if (isnan(Estimator.ShortMeanY) || isinf(Estimator.ShortMeanY)) Estimator.ShortMeanY = 0.0;
    if (isnan(Estimator.ShortMeanZ) || isinf(Estimator.ShortMeanZ)) Estimator.ShortMeanZ = 0.0;

    if (isnan(Estimator.VarianceX) || isinf(Estimator.VarianceX) || Estimator.VarianceX < 0.0) Estimator.VarianceX = 0.0;
    if (isnan(Estimator.VarianceY) || isinf(Estimator.VarianceY) || Estimator.VarianceY < 0.0) Estimator.VarianceY = 0.0;
    if (isnan(Estimator.VarianceZ) || isinf(Estimator.VarianceZ) || Estimator.VarianceZ < 0.0) Estimator.VarianceZ = 0.0;

    if (isnan(Estimator.VarianceBlendReduction) || isinf(Estimator.VarianceBlendReduction))
        Estimator.VarianceBlendReduction = 0.0;
    Estimator.VarianceBlendReduction = clamp(Estimator.VarianceBlendReduction, 0.0, 1.0);

    // ⚠️ Inconsistency is DIVIDED BY nowhere but is the argument of the dead-band max(); a zero here is legal, a NaN is not. It seeds to 1.0 and a reset
    //    must restore that seed rather than zero — see SeedSurfelMeanEstimator in SurfelTypes.glsl.
    if (isnan(Estimator.Inconsistency) || isinf(Estimator.Inconsistency))
        Estimator.Inconsistency = 1.0;
}

#endif
