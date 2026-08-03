/*==============================================================================================================================================
                                                            SURFELMOMENTS.GLSL
==============================================================================================================================================*/
// 🧩 The per-surfel temporal estimator — the Multi-Scale Mean Estimator (MSME) from Ray Tracing Gems ch.25 (Barré-Brisebois et al.). Each frame the
//    integrate shoots a handful of rays into a surfel's hemisphere, averages the bounce radiance into a raw estimate, and folds that estimate here
//    into a running mean that survives across frames. The estimator carries a SHORT mean and a variance alongside the long mean so it can suppress
//    fireflies (clamp a raw estimate that spikes far above the recent short mean) and CATCH UP fast when the true value genuinely shifts (large,
//    sustained short-vs-long disagreement raises the blend). This is what makes the surfel cache converge to a stable irradiance rather than boil.
//
//    🔴 THIS IS AN #include MODULE WITH NO main() AND NO BINDINGS. It is pure math over an MSMEData value the includer decodes from / encodes to the
//       moments SSBO (five vec4 rows, 20 floats). The read half and write half of that ping-pong buffer, and the element offsets into it, are the
//       includer's concern — this module only transforms the struct. Compile through the .comp that #includes it with glslc -I, never standalone.
//
//    🔴 PORTED 1:1 from surfelIntegratePass.ts msmeHelpers (:377-428). Every constant is exact: firefly highThreshold = 0.1 + shortMean + 8·stddev;
//       varianceBlend = shortWindowBlend·0.5; inconsistency EMA 0.08; vbbr EMA 0.1; the catch-up smoothstep and the "scale by previous vbbr THEN
//       update vbbr" ordering. Changing any of these changes convergence behaviour silently — the gates (I1 finite/converge, I2 firefly clamp) guard it.

#ifndef FRONTIER_SURFEL_SURFELMOMENTS_GLSL
#define FRONTIER_SURFEL_SURFELMOMENTS_GLSL

// The luma weights + PI the estimator leans on. Guarded so a translation unit that already pulled another module defining them does not redefine.
#ifndef FRONTIER_SURFEL_LUMA_WEIGHTS
#define FRONTIER_SURFEL_LUMA_WEIGHTS
const vec3 SurfelLumaWeights = vec3(0.2126, 0.7152, 0.0722);
#endif

#ifndef FRONTIER_SURFEL_PI
#define FRONTIER_SURFEL_PI
const float SURFEL_PI = 3.141592653589793238462;
#endif

const float SurfelMaxTemporalM = 200.0;   // [-] - MAX_TEMPORAL_M: the temporal sample count saturates here (webgiya :73)

//------------------------------------------------------------------------------------------------------------------------
//                                                          MSME STATE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The estimator's live state for one surfel. mean is the long-term irradiance the gather reads; shortMean is the fast-reacting window used for the
//    firefly threshold and the inconsistency signal; variance is the per-channel spread; vbbr is the variance-based blend reduction (throttles the
//    long mean when noisy); inconsistency is the short-vs-long disagreement that drives catch-up. Matches surfelIntegratePass.ts MSMEData exactly.
struct MSMEData
{
    vec3  Mean;
    vec3  ShortMean;
    float Vbbr;
    vec3  Variance;
    float Inconsistency;
};

// Clamp a decoded state into the sane band the reference applies right after the read (surfelIntegratePass.ts:916-918). A moments buffer that still
// carries garbage (never written, or a stray NaN) is defanged here so the blend below can never propagate a non-finite value into the mean.
void SurfelSanitiseMSME(inout MSMEData Data)
{
    // 🔴 Mean and ShortMean must be scrubbed to finite too, not just clamped-range. The reference's comment says this sanitise exists "to avoid NaNs",
    //    but it only guards vbbr/inconsistency/variance — leaving a non-finite mean/shortMean to ride mix() forward every frame and poison the surfel
    //    permanently (mix(NaN, x, b) == NaN). Zeroing a non-finite mean lets the running-average re-seed it from live samples next frame. isnan/isinf
    //    are only reliable with the default (non-fast) float mode, which glslc uses here; a mix-select keeps it branchless per channel.
    bvec3 MeanFinite      = equal(Data.Mean,      Data.Mean)      /* !isnan */ ;
    bvec3 ShortMeanFinite = equal(Data.ShortMean, Data.ShortMean);
    Data.Mean      = mix(vec3(0.0), Data.Mean,      MeanFinite);
    Data.ShortMean = mix(vec3(0.0), Data.ShortMean, ShortMeanFinite);
    Data.Mean      = clamp(Data.Mean,      vec3(0.0), vec3(1e6));   // defang +inf as well as NaN
    Data.ShortMean = clamp(Data.ShortMean, vec3(0.0), vec3(1e6));

    Data.Vbbr          = clamp(Data.Vbbr, 1.0 / 32.0, 1.0);
    Data.Inconsistency = clamp(Data.Inconsistency, 0.0, 10.0);
    Data.Variance      = max(Data.Variance, vec3(0.0));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           THE ESTIMATOR
//------------------------------------------------------------------------------------------------------------------------

// runMSME (surfelIntegratePass.ts:386-427). Fold one raw frame estimate y into the running state, blended by ShortWindowBlend (the includer derives
// that from how many samples contributed this frame). Firefly suppression, short mean, variance, inconsistency, vbbr, and catch-up — in that exact order.
MSMEData SurfelRunMSME(vec3 RawEstimate, MSMEData DataIn, float ShortWindowBlend)
{
    MSMEData Data = DataIn;

    // 1) Firefly suppression — a per-channel high threshold above which a raw sample is a spike, not signal, and is clamped down.
    vec3 Deviation     = sqrt(max(vec3(1e-5), Data.Variance));
    vec3 HighThreshold = vec3(0.1) + Data.ShortMean + Deviation * 8.0;
    vec3 Clamped       = min(RawEstimate, HighThreshold);

    // 2) Short mean — the fast window.
    Data.ShortMean = mix(Data.ShortMean, Clamped, ShortWindowBlend);
    vec3 Delta2    = Clamped - Data.ShortMean;
    vec3 Delta     = Clamped - DataIn.ShortMean;   // delta vs the PRE-blend short mean (matches the reference's delta/delta2 pairing)

    // 3) Variance — a slower blend than the short mean.
    float VarianceBlend = ShortWindowBlend * 0.5;
    Data.Variance = mix(Data.Variance, Delta * Delta2, VarianceBlend);
    Data.Variance = max(Data.Variance, vec3(0.0));

    // 4) Inconsistency — short-vs-long disagreement, normalised by the new deviation, EMA'd slowly.
    vec3  DeviationNew = sqrt(max(vec3(1e-5), Data.Variance));
    vec3  ShortDiff    = Data.Mean - Data.ShortMean;
    float RelativeDiff = dot(SurfelLumaWeights, abs(ShortDiff) / max(vec3(1e-5), DeviationNew));
    Data.Inconsistency = mix(Data.Inconsistency, RelativeDiff, 0.08);

    // 5) VBBR — reduce long-mean blending where variance is high.
    vec3  Term = (0.5 * Data.ShortMean) / max(vec3(1e-5), DeviationNew);
    float VarianceBasedBlendReduction = clamp(dot(SurfelLumaWeights, Term), 1.0 / 32.0, 1.0);

    // 6) Catch-up — react quickly when the short mean has drifted far from the long mean, but gated by the previous vbbr.
    float CatchUpFactor = smoothstep(0.0, 1.0, RelativeDiff * max(0.02, Data.Inconsistency - 0.2));
    float CatchUpBlend  = clamp(CatchUpFactor, 1.0 / 256.0, 1.0);

    // IMPORTANT: scale by the PREVIOUS vbbr, THEN update vbbr — ordering matches the reference (surfelIntegratePass.ts:422-424).
    CatchUpBlend *= Data.Vbbr;
    Data.Vbbr     = mix(Data.Vbbr, VarianceBasedBlendReduction, 0.1);

    Data.Mean = mix(Data.Mean, Clamped, clamp(CatchUpBlend, 0.0, 1.0));
    return Data;
}

#endif
