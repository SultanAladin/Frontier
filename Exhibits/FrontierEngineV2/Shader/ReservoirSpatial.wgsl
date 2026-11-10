//============================================================================================================================================
//                                                          RESERVOIRSPATIAL.WGSL
//============================================================================================================================================
// 🧩 ReSTIR PT Enhanced §3 Reciprocal Spatial Reuse (Pass 2: Pairwise-MIS GRIS Merge) + §6.3 Marginalized RGB Vector Shading
//    (Lin, Kettunen & Wyman 2026, PACMCGIT 9(1), Art. 13, doi:10.1145/3804494):
//    - Consumes coupled 16-byte shifts X_{B_i} -> A and X_A -> B_i from PairedShiftExchange with exact canonical pairwise MIS (∑ m_i ≡ 1).
//    - Accumulates RGB vector resampling weights w_i = m_i(Y_i) · F_rgb(Y_i) · W_{X_i} · |J_i| (§6.3) into IrradianceTarget,
//      tracing at most 1 post-merge visibility ray across the entire kernel when Spatial.Adopted is true.
//    - Emits post-spatial winning PathSeed & diagnostics into PostSpatialMetaTarget for §5 17×17 Sample Duplication Map estimation.

//! include "CardSampling.wgsl"
//! include "ReservoirRecords.wgsl"
//! include "DiscrepancySequence.wgsl"
//! include "RayTraversal.wgsl"

@group(0) @binding(1) var<uniform> PairingTransforms         : PairingTransformUniform;
@group(0) @binding(2) var Guide                              : texture_2d<f32>;
@group(0) @binding(3) var SurfaceDepth                       : texture_depth_2d;
@group(0) @binding(4) var SurfaceAttributes                  : texture_2d<f32>;
@group(0) @binding(5) var PairingMaps                        : texture_2d_array<u32>;
@group(0) @binding(6) var<storage, read> CurrentReservoirs   : array<ReservoirRecord>;
@group(0) @binding(7) var<storage, read> PairedShiftExchange : array<vec4f>;
@group(0) @binding(8) var IrradianceTarget                   : texture_storage_2d<rgba16float, write>;
@group(0) @binding(9) var PostSpatialMetaTarget              : texture_storage_2d<rgba32uint, write>;

const ProposalCapacity : u32 = 8u;
const MaxPairedLayers  : u32 = 3u;

fn ResolveSpatialScreenPixel(TracePixel: vec2i) -> vec2i
{
    return min(TracePixel * i32(Frame.TraceScale), vec2i(Frame.ScreenExtent) - vec2i(1));
}

fn ResolveTracePosition(TracePixel: vec2i) -> vec3f
{
    let ScreenPixel = ResolveSpatialScreenPixel(TracePixel);
    return ReconstructWorldPosition(vec2f(ScreenPixel) + 0.5, textureLoad(SurfaceDepth, ScreenPixel, 0));
}

fn ResolveReconnectionJacobian(Sample: ReservoirSample, Receiver: vec3f, Donor: vec3f) -> f32
{
    if (Sample.Identity == SkyIdentity || (Sample.Flags & PathFlagDirectional) != 0u)
    {
        return 1.0;
    }
    let ToDonor         = Donor - Sample.Position;
    let ToReceiver      = Receiver - Sample.Position;
    let DonorSquared    = max(dot(ToDonor, ToDonor), 1.0e-6);
    let ReceiverSquared = max(dot(ToReceiver, ToReceiver), 1.0e-6);
    let CosineDonor     = abs(dot(Sample.Normal, ToDonor)) * inverseSqrt(DonorSquared);
    let CosineReceiver  = abs(dot(Sample.Normal, ToReceiver)) * inverseSqrt(ReceiverSquared);
    return (CosineReceiver / max(CosineDonor, 1.0e-4)) * (DonorSquared / ReceiverSquared);
}

fn ClampSignalLuminance(Signal: vec3f, Scale: f32) -> vec3f
{
    if (any(Signal != Signal))
    {
        return vec3f(0.0);
    }
    let Safe  = max(Signal, vec3f(0.0));
    let Lum   = ResolveLuminance(Safe);
    let Limit = Scale * max(ResolveLuminance(Frame.SunIlluminance), 0.75);
    if (Lum > Limit)
    {
        return Safe * (Limit / Lum);
    }
    return Safe;
}

@compute @workgroup_size(8, 8)
fn ResampleSpatial(@builtin(global_invocation_id) Invocation: vec3u)
{
    let TracePixel = vec2i(Invocation.xy);
    let Extent     = vec2i(Frame.TraceExtent);
    if (any(TracePixel >= Extent))
    {
        return;
    }

    let GuideTexel = textureLoad(Guide, TracePixel, 0);
    if (GuideTexel.w < 0.5)
    {
        textureStore(IrradianceTarget, TracePixel, vec4f(0.0));
        textureStore(PostSpatialMetaTarget, TracePixel, vec4u(0u));
        return;
    }

    let UseRayTrace  = FeatureEnabled(Frame, FeatureRayTracing);
    let UseEnhanced  = UseRayTrace && EnhancedOptionEnabled(Frame, EnhancedMaster);
    let UseUnified   = UseEnhanced && EnhancedOptionEnabled(Frame, EnhancedUnifiedDiGi);
    let UsePaired    = UseUnified && EnhancedOptionEnabled(Frame, EnhancedPairedReuse);
    let UseVecShade  = UseUnified && EnhancedOptionEnabled(Frame, EnhancedVectorShading);
    let UseFootprint = UseEnhanced && EnhancedOptionEnabled(Frame, EnhancedFootprintCriteria);
    let MaxJac       = select(10.0, 6.0, FeatureEnabled(Frame, FeatureFireflyFilter));

    let ScreenPixel = ResolveSpatialScreenPixel(TracePixel);
    let Normal      = DecodeOctahedral(GuideTexel.xy);
    let ViewDepth   = GuideTexel.z;
    let Position    = ReconstructWorldPosition(vec2f(ScreenPixel) + 0.5, textureLoad(SurfaceDepth, ScreenPixel, 0));
    let View        = normalize(Frame.CameraPosition - Position);
    let Attr        = textureLoad(SurfaceAttributes, ScreenPixel, 0);
    let MatIdx      = min(u32(Attr.z + 0.5), arrayLength(&RayMaterials) - 1u);
    let Surface     = ProjectSurfaceSample(RayMaterials[MatIdx]);
    let ThresholdA  = ResolvePrimaryFootprintThreshold(Frame.CameraPosition, Position, Normal, ResolveFootprintConstant(Frame));

    let PixelCount = u32(Extent.x * Extent.y);
    let SelfIdx    = u32(TracePixel.y * Extent.x + TracePixel.x);
    let Own        = UnpackReservoir(CurrentReservoirs[SelfIdx]);
    var Seed       = ConstructPixelSeed(vec2u(TracePixel), Frame.FrameIndex, 23u);

    let OwnResp       = EvaluatePrimaryPathIntegrand(Own.Sample, Surface, Position, Normal, View, UseUnified);
    var Spatial       = ConstructEmptyReservoir();
    var ChosenDiff    = OwnResp.DiffuseIrradiance;
    var ChosenTarget  = OwnResp.TargetPdf;
    var NeighborVec     = vec3f(0.0);
    var OwnVec          = OwnResp.DiffuseIrradiance * Own.Weight;
    var MeanPairDelta   = 0.0;
    var FinalIrr        = vec3f(0.0);
    var BestDonorSample = Own.Sample;
    var BestDonorWeight = 0.0;

    // ── Mode 1 & 2: ReSTIR PT Enhanced (§3 Coupled Gaussian Paired Reuse OR Uncoupled Disk A/B Comparison) ──────────
    if (UseUnified)
    {
        if (!FeatureEnabled(Frame, FeatureSpatialReuse) || Frame.SpatialCount == 0u)
        {
            Spatial      = Own;
            ChosenTarget = OwnResp.TargetPdf;
        }
        else if (UsePaired)
        {
            let LayerCount   = min(Frame.SpatialCount, MaxPairedLayers);
            let InvLayers    = 1.0 / f32(max(LayerCount, 1u));
            var CanonicalMis = 0.0;

            for (var Layer = 0u; Layer < LayerCount; Layer += 1u)
            {
                let DonorPx    = ResolvePairedNeighbor(PairingMaps, PairingTransforms.Layers[Layer], i32(Layer), TracePixel);
                MeanPairDelta += length(vec2f(DonorPx - TracePixel)) * InvLayers;

                if (any(DonorPx < vec2i(0)) || any(DonorPx >= Extent) || all(DonorPx == TracePixel))
                {
                    CanonicalMis += InvLayers;
                    continue;
                }

                let DonorGuide  = textureLoad(Guide, DonorPx, 0);
                let DonorNormal = DecodeOctahedral(DonorGuide.xy);
                if (DonorGuide.w < 0.5 || dot(DonorNormal, Normal) < 0.90 || abs(DonorGuide.z - ViewDepth) > 0.10 * max(ViewDepth, 0.5))
                {
                    CanonicalMis += InvLayers;
                    continue;
                }
                let DonorPos = ResolveTracePosition(DonorPx);
                if (abs(dot(DonorPos - Position, Normal)) > 0.08 * max(ViewDepth, 0.5))
                {
                    CanonicalMis += InvLayers;
                    continue;
                }

                let DonorIdx = u32(DonorPx.y * Extent.x + DonorPx.x);
                let DonorRes = UnpackReservoir(CurrentReservoirs[DonorIdx]);
                if (DonorRes.Tally <= 0.0 || DonorRes.Weight <= 0.0)
                {
                    CanonicalMis += InvLayers;
                    continue;
                }

                let SlotAB         = PairedShiftExchange[Layer * PixelCount + DonorIdx];
                let TargetJacAB    = max(SlotAB.w, 0.0);
                let CanonicalNum   = Own.Tally * OwnResp.TargetPdf;
                let CanonicalDenom = CanonicalNum + f32(LayerCount) * DonorRes.Tally * TargetJacAB;
                CanonicalMis += select(InvLayers, (CanonicalNum / CanonicalDenom) * InvLayers,
                                       CanonicalDenom > 1.0e-8 && OwnResp.TargetPdf > 1.0e-8);

                let SlotBA      = PairedShiftExchange[Layer * PixelCount + SelfIdx];
                let TargetJacBA = SlotBA.w;
                if (TargetJacBA > 1.0e-8 && DonorRes.Weight > 0.0 && DonorRes.Tally > 0.0)
                {
                    let DonorNum   = DonorRes.Tally * max(DonorRes.Sample.SourceTargetPdf, 1.0e-6);
                    let ShiftDenom = Own.Tally * TargetJacBA + f32(LayerCount) * DonorNum;
                    let MisB       = select(0.0, DonorNum / ShiftDenom, ShiftDenom > 1.0e-8);
                    let ScalarW    = MisB * TargetJacBA * DonorRes.Weight;

                    NeighborVec += MisB * SlotBA.xyz * DonorRes.Weight;
                    if (ScalarW > BestDonorWeight)
                    {
                        BestDonorWeight = ScalarW;
                        BestDonorSample = DonorRes.Sample;
                    }
                    if (MergeReservoir(&Spatial, DonorRes.Sample, ScalarW, DonorRes.Tally, AdvanceUniform(&Seed)))
                    {
                        ChosenDiff      = SlotBA.xyz;
                        ChosenTarget    = TargetJacBA;
                        Spatial.Adopted = true;
                    }
                }
            }

            let OwnScalarW = CanonicalMis * OwnResp.TargetPdf * Own.Weight;
            OwnVec         = CanonicalMis * OwnResp.DiffuseIrradiance * Own.Weight;
            if (MergeReservoir(&Spatial, Own.Sample, OwnScalarW, Own.Tally, AdvanceUniform(&Seed)))
            {
                ChosenDiff      = OwnResp.DiffuseIrradiance;
                ChosenTarget    = OwnResp.TargetPdf;
                Spatial.Adopted = false;
            }
            Spatial.Weight = select(0.0, Spatial.WeightSum / ChosenTarget, ChosenTarget > 1.0e-8);
        }
        else
        {
            let Count        = min(Frame.SpatialCount, ProposalCapacity);
            let InvCount     = 1.0 / f32(max(Count, 1u));
            var CanonicalMis = 0.0;

            for (var Prop = 0u; Prop < Count; Prop += 1u)
            {
                let Offset  = ProjectUniformDisk(vec2f(AdvanceUniform(&Seed), AdvanceUniform(&Seed))) * Frame.SpatialRadius;
                let DonorPx = clamp(TracePixel + vec2i(round(Offset)), vec2i(0), Extent - vec2i(1));
                if (all(DonorPx == TracePixel))
                {
                    CanonicalMis += InvCount;
                    continue;
                }
                let DonorGuide  = textureLoad(Guide, DonorPx, 0);
                let DonorNormal = DecodeOctahedral(DonorGuide.xy);
                if (DonorGuide.w < 0.5 || dot(DonorNormal, Normal) < 0.85 || abs(DonorGuide.z - ViewDepth) > 0.12 * max(ViewDepth, 0.5))
                {
                    CanonicalMis += InvCount;
                    continue;
                }
                let DonorPos   = ResolveTracePosition(DonorPx);
                let ThresholdB = ResolvePrimaryFootprintThreshold(Frame.CameraPosition, DonorPos, DonorNormal, ResolveFootprintConstant(Frame));
                let DonorRes   = UnpackReservoir(CurrentReservoirs[u32(DonorPx.y * Extent.x + DonorPx.x)]);

                let SlotAB = EvaluateAnalyticPairedShift(Own.Sample, DonorPos, DonorNormal, ThresholdB, UseRayTrace, UseFootprint, MaxJac);
                let NumA   = Own.Tally * OwnResp.TargetPdf;
                let DenA   = NumA + f32(Count) * DonorRes.Tally * max(SlotAB.w, 0.0);
                CanonicalMis += select(InvCount, (NumA / DenA) * InvCount, DenA > 1.0e-8 && OwnResp.TargetPdf > 1.0e-8);

                if (DonorRes.Weight > 0.0 && DonorRes.Tally > 0.0)
                {
                    let SlotBA = EvaluateAnalyticPairedShift(DonorRes.Sample, Position, Normal, ThresholdA, UseRayTrace, UseFootprint, MaxJac);
                    if (SlotBA.w > 1.0e-8)
                    {
                        let NumB = DonorRes.Tally * max(DonorRes.Sample.SourceTargetPdf, 1.0e-6);
                        let DenB = Own.Tally * SlotBA.w + f32(Count) * NumB;
                        let MisB = select(0.0, NumB / DenB, DenB > 1.0e-8);
                        NeighborVec += MisB * SlotBA.xyz * DonorRes.Weight;
                        if (MergeReservoir(&Spatial, DonorRes.Sample, MisB * SlotBA.w * DonorRes.Weight, DonorRes.Tally, AdvanceUniform(&Seed)))
                        {
                            ChosenDiff      = SlotBA.xyz;
                            ChosenTarget    = SlotBA.w;
                            Spatial.Adopted = true;
                        }
                    }
                }
            }

            OwnVec = CanonicalMis * OwnResp.DiffuseIrradiance * Own.Weight;
            if (MergeReservoir(&Spatial, Own.Sample, CanonicalMis * OwnResp.TargetPdf * Own.Weight, Own.Tally, AdvanceUniform(&Seed)))
            {
                ChosenDiff      = OwnResp.DiffuseIrradiance;
                ChosenTarget    = OwnResp.TargetPdf;
                Spatial.Adopted = false;
            }
            Spatial.Weight = select(0.0, Spatial.WeightSum / ChosenTarget, ChosenTarget > 1.0e-8);
        }

        FinalIrr = select(ChosenDiff * Spatial.Weight, OwnVec + NeighborVec, UseVecShade);
    }
    else
    {
        // ── Mode 3: 2022 Diffuse Baseline & SDF Sphere-Tracing Mode ──────────────────────────────────────────────────
        MergeReservoir(&Spatial, Own.Sample, ResolveReservoirTarget(Own.Sample, Position, Normal) * Own.Weight * Own.Tally, Own.Tally,
                       AdvanceUniform(&Seed));

        var DonorPositions : array<vec3f, 8>;
        var DonorNormals   : array<vec3f, 8>;
        var DonorTallies   : array<f32, 8>;
        var DonorCount     = 0u;
        if (FeatureEnabled(Frame, FeatureSpatialReuse))
        {
            let Count = min(Frame.SpatialCount, ProposalCapacity);
            for (var Proposal = 0u; Proposal < Count; Proposal++)
            {
                let Offset = ProjectUniformDisk(vec2f(AdvanceUniform(&Seed), AdvanceUniform(&Seed))) * Frame.SpatialRadius;
                let Donor  = clamp(TracePixel + vec2i(round(Offset)), vec2i(0), Extent - vec2i(1));
                if (all(Donor == TracePixel)) { continue; }
                let DonorGuide  = textureLoad(Guide, Donor, 0);
                let DonorNormal = DecodeOctahedral(DonorGuide.xy);
                if (DonorGuide.w < 0.5 || dot(DonorNormal, Normal) < 0.9 || abs(DonorGuide.z - ViewDepth) > 0.1 * ViewDepth) { continue; }
                let Reservoir = UnpackReservoir(CurrentReservoirs[u32(Donor.y * Extent.x + Donor.x)]);
                if (Reservoir.Tally <= 0.0 || Reservoir.Weight <= 0.0) { continue; }
                let DonorPosition = ResolveTracePosition(Donor);
                if (abs(dot(DonorPosition - Position, Normal)) > 0.08 * max(ViewDepth, 0.5)) { continue; }
                let Jacobian = ResolveReconnectionJacobian(Reservoir.Sample, Position, DonorPosition);
                let MaxJ     = select(8.0, 5.0, FeatureEnabled(Frame, FeatureFireflyFilter));
                if (Jacobian > MaxJ || Jacobian < (1.0 / MaxJ) || Jacobian != Jacobian) { continue; }
                let Target = ResolveReservoirTarget(Reservoir.Sample, Position, Normal);
                if (MergeReservoir(&Spatial, Reservoir.Sample, Target * Reservoir.Weight * Reservoir.Tally * Jacobian, Reservoir.Tally,
                                   AdvanceUniform(&Seed)))
                {
                    Spatial.Adopted = true;
                }
                DonorPositions[DonorCount] = DonorPosition;
                DonorNormals[DonorCount]   = DonorNormal;
                DonorTallies[DonorCount]   = Reservoir.Tally;
                DonorCount++;
            }
        }

        let Chosen = Spatial.Sample;
        var Z      = select(0.0, Own.Tally, ResolveReservoirTarget(Chosen, Position, Normal) > 0.0);
        for (var Donor = 0u; Donor < DonorCount; Donor++)
        {
            if (ResolveReservoirTarget(Chosen, DonorPositions[Donor], DonorNormals[Donor]) > 0.0)
            {
                Z += DonorTallies[Donor];
            }
        }
        let Target = ResolveReservoirTarget(Chosen, Position, Normal);
        let Weight = select(0.0, Spatial.WeightSum / (Z * Target), Target > 0.0 && Z > 0.0);
        let Cosine = max(dot(Normal, ResolveSampleDirection(Chosen, Position)), 0.0);
        FinalIrr   = Chosen.Radiance * Cosine * Weight;
    }

    // ── ONE unified post-merge visibility check across all modes (1 visibility call site total in the shader!) ──────
    let TestDonorVec = UseUnified && UseVecShade && BestDonorWeight > 0.35 * OwnResp.TargetPdf * Own.Weight;
    if ((Spatial.Adopted || TestDonorVec) && FeatureEnabled(Frame, FeatureVisibilityReuse))
    {
        var SampleToTest = BestDonorSample;
        if (Spatial.Adopted)
        {
            SampleToTest = Spatial.Sample;
        }
        var VisJac  = 0.0;
        let VisResp      = ShiftReservoirPath(SampleToTest, Position, Normal, View, Surface, ThresholdA, true, &VisJac);
        if (!(VisResp.TargetPdf > 1.0e-8))
        {
            Spatial  = Own;
            FinalIrr = OwnResp.DiffuseIrradiance * Own.Weight;
        }
    }

    let ClampScale = select(40.0, 22.0, FeatureEnabled(Frame, FeatureFireflyFilter));
    let OutDiff    = ClampSignalLuminance(FinalIrr, ClampScale);
    textureStore(IrradianceTarget, TracePixel, vec4f(OutDiff, 1.0));

    let FootViz  = clamp(f32(Spatial.Sample.ReconnectVertex - 2u) * 0.5, 0.0, 1.0);
    let DeltaViz = clamp(MeanPairDelta / max(Frame.SpatialRadius, 1.0), 0.0, 2.0);
    let MetaWord = PackOutgoingAndMeta(
        Spatial.Sample.OutgoingDirection,
        Spatial.Sample.Flags,
        Spatial.Sample.ReconnectVertex,
        Spatial.Sample.PathLength,
        0.0
    );
    textureStore(PostSpatialMetaTarget, TracePixel, vec4u(
        Spatial.Sample.PathSeed,
        MetaWord,
        pack2x16float(vec2f(FootViz, DeltaViz)),
        1u
    ));
}
