//============================================================================================================================================
//                                                       RESERVOIRPAIREDSHIFT.WGSL
//============================================================================================================================================
// 🧩 ReSTIR PT Enhanced §3 Reciprocal Spatial Reuse — Pass 1 (Zero-Ray Analytic Coupled Shift Evaluation)
//    (Lin, Kettunen & Wyman 2026, PACMCGIT 9(1), Art. 13, doi:10.1145/3804494):
//    - Each pixel A looks up its involutive Gaussian neighbor B_i = Pair(A, i) for i ∈ [0 .. N−1] (where Pair(B_i, i) === A).
//    - Thread A evaluates ONLY the analytic shift X_{B_i} -> A (0 BVH rays; thread B_i simultaneously evaluates X_A -> B_i).
//    - Writes a compact 16-byte vec4f slot per (layer, pixel): (DiffuseIrradiance * JacBA, TargetPdf * JacBA).

//! include "FrameDescriptor.wgsl"
//! include "ReservoirRecords.wgsl"

@group(0) @binding(1) var<uniform> PairingTransforms               : PairingTransformUniform;
@group(0) @binding(2) var Guide                                    : texture_2d<f32>;
@group(0) @binding(3) var SurfaceDepth                             : texture_depth_2d;
@group(0) @binding(4) var PairingMaps                              : texture_2d_array<u32>;
@group(0) @binding(5) var<storage, read> TemporalReservoirs        : array<ReservoirRecord>;
@group(0) @binding(6) var<storage, read_write> PairedShiftExchange : array<vec4f>;

const MaxPairedLayers : u32 = 3u;

fn ResolvePairedScreenPixel(TracePixel: vec2i) -> vec2i
{
    return min(TracePixel * i32(Frame.TraceScale), vec2i(Frame.ScreenExtent) - vec2i(1));
}

@compute @workgroup_size(8, 8)
fn EvaluatePairedShifts(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Extent     = vec2i(Frame.TraceExtent);
    let TracePixel = vec2i(Invocation.xy);
    if (any(TracePixel >= Extent))
    {
        return;
    }

    let PixelCount = u32(Extent.x * Extent.y);
    let SelfIndex  = u32(TracePixel.y * Extent.x + TracePixel.x);
    let LayerCount = min(Frame.SpatialCount, MaxPairedLayers);

    let UseRayTrace  = FeatureEnabled(Frame, FeatureRayTracing);
    let UsePaired    = FeatureEnabled(Frame, FeatureSpatialReuse) && UseRayTrace && EnhancedOptionEnabled(Frame, EnhancedPairedReuse);
    let UseFootprint = UseRayTrace && EnhancedOptionEnabled(Frame, EnhancedFootprintCriteria);
    let MaxJac       = select(10.0, 6.0, FeatureEnabled(Frame, FeatureFireflyFilter));

    if (!UsePaired)
    {
        for (var Layer = 0u; Layer < LayerCount; Layer += 1u)
        {
            PairedShiftExchange[Layer * PixelCount + SelfIndex] = vec4f(0.0);
        }
        return;
    }

    let GuideTexel = textureLoad(Guide, TracePixel, 0);
    if (GuideTexel.w < 0.5)
    {
        for (var Layer = 0u; Layer < LayerCount; Layer += 1u)
        {
            PairedShiftExchange[Layer * PixelCount + SelfIndex] = vec4f(0.0);
        }
        return;
    }

    let ScreenA = ResolvePairedScreenPixel(TracePixel);
    let DepthA  = textureLoad(SurfaceDepth, ScreenA, 0);
    if (DepthA <= 0.0)
    {
        for (var Layer = 0u; Layer < LayerCount; Layer += 1u)
        {
            PairedShiftExchange[Layer * PixelCount + SelfIndex] = vec4f(0.0);
        }
        return;
    }

    let NormalA    = DecodeOctahedral(GuideTexel.xy);
    let ViewDepthA = GuideTexel.z;
    let PositionA  = ReconstructWorldPosition(vec2f(ScreenA) + 0.5, DepthA);
    let ThresholdA = ResolvePrimaryFootprintThreshold(Frame.CameraPosition, PositionA, NormalA, ResolveFootprintConstant(Frame));

    for (var Layer = 0u; Layer < LayerCount; Layer += 1u)
    {
        let SlotIdx = Layer * PixelCount + SelfIndex;
        let DonorPx = ResolvePairedNeighbor(PairingMaps, PairingTransforms.Layers[Layer], i32(Layer), TracePixel);
        if (any(DonorPx < vec2i(0)) || any(DonorPx >= Extent) || all(DonorPx == TracePixel))
        {
            PairedShiftExchange[SlotIdx] = vec4f(0.0);
            continue;
        }

        let DonorGuide = textureLoad(Guide, DonorPx, 0);
        if (DonorGuide.w < 0.5 || abs(DonorGuide.z - ViewDepthA) > 0.10 * max(ViewDepthA, 0.5))
        {
            PairedShiftExchange[SlotIdx] = vec4f(0.0);
            continue;
        }
        let DonorNormal = DecodeOctahedral(DonorGuide.xy);
        if (dot(DonorNormal, NormalA) < 0.90)
        {
            PairedShiftExchange[SlotIdx] = vec4f(0.0);
            continue;
        }
        let DonorScreen = ResolvePairedScreenPixel(DonorPx);
        let DonorDepth  = textureLoad(SurfaceDepth, DonorScreen, 0);
        if (DonorDepth <= 0.0)
        {
            PairedShiftExchange[SlotIdx] = vec4f(0.0);
            continue;
        }
        let DonorPos = ReconstructWorldPosition(vec2f(DonorScreen) + 0.5, DonorDepth);
        if (abs(dot(DonorPos - PositionA, NormalA)) > 0.08 * max(ViewDepthA, 0.5))
        {
            PairedShiftExchange[SlotIdx] = vec4f(0.0);
            continue;
        }

        let DonorIdx = u32(DonorPx.y * Extent.x + DonorPx.x);
        let DonorRes = UnpackReservoir(TemporalReservoirs[DonorIdx]);
        if (DonorRes.Tally <= 0.0 || DonorRes.Weight <= 0.0)
        {
            PairedShiftExchange[SlotIdx] = vec4f(0.0);
            continue;
        }

        PairedShiftExchange[SlotIdx] = EvaluateAnalyticPairedShift(
            DonorRes.Sample,
            PositionA,
            NormalA,
            ThresholdA,
            UseRayTrace,
            UseFootprint,
            MaxJac
        );
    }
}
