//============================================================================================================================================
//                                                           DISPLAYEXPOSURE.WGSL
//============================================================================================================================================
// 🧩 Eye adaptation: log-average luminance of a 64×64 lattice of the frame, approached exponentially by the exposure.
//    When ExtendedCockpitExposure is active (camera inside the car cabin), metering weights the windshield sightline and
//    exterior sky/road samples so dark dashboard & pillar trim cannot trick auto-exposure into blowing out the view.

//! include "FrameDescriptor.wgsl"

@group(0) @binding(1) var ResolvedRadiance                 : texture_2d<f32>;
@group(0) @binding(2) var<storage, read_write> Adaptation  : array<f32, 4>;

var<workgroup> LaneSums    : array<f32, 256>;
var<workgroup> LaneWeights : array<f32, 256>;

@compute @workgroup_size(256)
fn IntegrateExposure(@builtin(local_invocation_index) Lane: u32)
{
    let Cockpit = ExtendedEnabled(Frame, ExtendedCockpitExposure);
    var Sum = 0.0;
    var WeightSum = 0.0;
    for (var Sample = 0u; Sample < 16u; Sample++)
    {
        let Index = Lane * 16u + Sample;
        let Lattice = vec2f(f32(Index % 64u) + 0.5, f32(Index / 64u) + 0.5) / 64.0;
        let Pixel = vec2i(Lattice * Frame.ScreenExtent);
        let Luminance = ResolveLuminance(textureLoad(ResolvedRadiance, Pixel, 0).rgb);
        var Weight = 1.0;
        if (Cockpit)
        {
            let InWindshield = Lattice.x >= 0.14 && Lattice.x <= 0.86 && Lattice.y >= 0.06 && Lattice.y <= 0.48;
            if (InWindshield && Luminance > 0.06)
            {
                Weight = 8.0;
            }
            else if (Luminance > 0.18)
            {
                Weight = 5.0;
            }
            else
            {
                Weight = 0.06;
            }
        }
        Sum += Weight * log2(max(Luminance, 1.0e-5));
        WeightSum += Weight;
    }
    LaneSums[Lane] = Sum;
    LaneWeights[Lane] = WeightSum;
    workgroupBarrier();
    for (var Stride = 128u; Stride > 0u; Stride >>= 1u)
    {
        if (Lane < Stride)
        {
            LaneSums[Lane] += LaneSums[Lane + Stride];
            LaneWeights[Lane] += LaneWeights[Lane + Stride];
        }
        workgroupBarrier();
    }
    if (Lane == 0u)
    {
        let Logarithm = LaneSums[0] / max(LaneWeights[0], 1.0);
        let Average = exp2(Logarithm);
        // 📐 Middle grey 0.18 at the log-average luminance — below 0.1 the eye adapts only partially so night courses
        //    stay nocturnal, and in cockpit mode gain is capped so shaded cabin leather stays rich dark charcoal.
        let Floor = select(log2(0.1), log2(0.22), Cockpit);
        let Adapted = select(Logarithm, Floor + (Logarithm - Floor) * 0.66, Logarithm < Floor);
        let MaxGain = select(60.0, 1.05, Cockpit);
        let Target = clamp(0.18 / exp2(Adapted), 0.02, MaxGain) * exp2(Frame.ExposureCompensation);
        let Prior = Adaptation[0];
        var Exposure = Target;
        if (Prior > 0.0 && Frame.FrameIndex > 2u)
        {
            let Speed = select(0.06, 0.14, Cockpit);
            Exposure = exp2(mix(log2(Prior), log2(Target), Speed));
        }
        if (!FeatureEnabled(Frame, FeatureAutoExposure))
        {
            Exposure = 0.55 * exp2(Frame.ExposureCompensation);
        }
        Adaptation[0] = Exposure;
        Adaptation[1] = Average;
    }
}
