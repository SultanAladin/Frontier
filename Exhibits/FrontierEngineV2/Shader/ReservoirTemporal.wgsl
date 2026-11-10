//============================================================================================================================================
//                                                          RESERVOIRTEMPORAL.WGSL
//============================================================================================================================================
// 🧩 ReSTIR PT Enhanced initial path-tree candidate generation + temporal GRIS reuse (Lin, Kettunen & Wyman 2026, doi:10.1145/3804494):
//    - §2.3 & §6.1 : Low-discrepancy R2 path-tree candidate in a 64-byte reservoir (with 1/B² multi-bounce NEE + screen-trace walk)
//    - §4 Eq. (5)  : Scene-independent dual ray-footprint + single-vertex roughness reconnection vertex selection
//    - §5          : Adaptive temporal confidence cap c_Cap driven by prior 17×17 PathSeed duplication score D
//    - §6.2.3–6.2.4: Forced terminal NEE light reconnection & initial-only external Russian roulette
//    - §6.4        : Dual motion vectors (primary surface motion + static camera reprojection fallback on disocclusions)

//! include "CardSampling.wgsl"
//! include "ScreenTraversal.wgsl"
//! include "ReservoirRecords.wgsl"
//! include "DiscrepancySequence.wgsl"
//! include "RayTraversal.wgsl"

@group(0) @binding(1) var SurfaceAttributes                      : texture_2d<f32>;
@group(0) @binding(2) var SurfaceDepth                           : texture_depth_2d;
@group(0) @binding(3) var SurfaceMotion                          : texture_2d<f32>;
@group(0) @binding(4) var PreviousGuide                          : texture_2d<f32>;
@group(0) @binding(5) var GuideTarget                            : texture_storage_2d<rgba16float, write>;
@group(0) @binding(6) var<storage, read> PreviousReservoirs      : array<ReservoirRecord>;
@group(0) @binding(7) var<storage, read_write> CurrentReservoirs : array<ReservoirRecord>;

const DiffuseConeSlope : f32 = 0.006;

fn ResolveScreenPixel(TracePixel: vec2u) -> vec2i
{
    return min(vec2i(TracePixel * Frame.TraceScale), vec2i(Frame.ScreenExtent) - vec2i(1));
}

fn CarryReservoirSample(Sample: ReservoirSample) -> ReservoirSample
{
    var Carried = Sample;
    if ((Sample.Flags & PathFlagDirectional) == 0u && Sample.Identity < arrayLength(&Occupants))
    {
        let Occupant = Occupants[Sample.Identity];
        if ((Occupant.Roles & RoleDynamic) != 0u)
        {
            let Local        = Occupant.PreviousWorldToLocal * vec4f(Sample.Position, 1.0);
            Carried.Position = (Occupant.LocalToWorld * Local).xyz;
            let LocalNormal  = (Occupant.PreviousWorldToLocal * vec4f(Sample.Normal, 0.0)).xyz;
            Carried.Normal   = normalize((Occupant.LocalToWorld * vec4f(LocalNormal, 0.0)).xyz);
        }
    }
    return Carried;
}

fn RefreshSampleRadiance(Sample: ReservoirSample) -> vec3f
{
    if (Sample.Identity == SkyIdentity)
    {
        return SampleSkyRadiance(-Sample.Normal);
    }
    if (FeatureEnabled(Frame, FeatureRayTracing))
    {
        return Sample.Radiance;
    }
    let Level    = ResolveFinestClip(Sample.Position, 1.0);
    let Exitance = ResolveSampleExitance(Sample.Position, Sample.Normal, Sample.Identity, ResolveCardTolerance(Level));
    return select(Sample.Radiance, Exitance.rgb, Exitance.w > 0.0);
}

// 📐 §6.4 Dual Motion Vectors: resolves primary surface-motion reprojection first, then falls back to static world-to-prior-camera
//    reprojection (and sub-texel 5-tap dilation) when dynamic silhouettes disocclude the primary motion vector.
fn ResolveTemporalPriorTrace(
    ScreenPixel   : vec2i,
    WorldPosition : vec3f,
    Normal        : vec3f,
    ExpectedDepth : f32,
    ViewDepth     : f32,
    Extent        : vec2i,
    OutValid      : ptr<function, bool>
) -> vec2i
{
    *OutValid = false;
    let Motion       = textureLoad(SurfaceMotion, ScreenPixel, 0).xy;
    let PriorScreenA = vec2f(ScreenPixel) + 0.5 - Motion;
    let PriorTraceA  = vec2i(floor((PriorScreenA - 0.5) / f32(Frame.TraceScale) + 0.5));
    if (all(PriorTraceA >= vec2i(0)) && all(PriorTraceA < Extent))
    {
        let GuideA  = textureLoad(PreviousGuide, PriorTraceA, 0);
        let NormalA = DecodeOctahedral(GuideA.xy);
        if (GuideA.w > 0.5 && dot(NormalA, Normal) > 0.85 && ClassifyPriorDepth(GuideA.z, ExpectedDepth, ViewDepth))
        {
            *OutValid = true;
            return PriorTraceA;
        }
    }

    if (!EnhancedOptionEnabled(Frame, EnhancedDualMotion))
    {
        return PriorTraceA;
    }

    let PriorClip = Frame.PreviousUnjitteredViewProjection * vec4f(WorldPosition, 1.0);
    if (PriorClip.w > 1.0e-4)
    {
        let PriorNdc     = PriorClip.xyz / PriorClip.w;
        let PriorUv      = vec2f(PriorNdc.x * 0.5 + 0.5, 0.5 - PriorNdc.y * 0.5);
        let PriorScreenB = PriorUv * Frame.ScreenExtent;
        let BaseTraceB   = vec2i(floor((PriorScreenB - 0.5) / f32(Frame.TraceScale) + 0.5));

        let Offsets = array<vec2i, 5>(vec2i(0, 0), vec2i(1, 0), vec2i(-1, 0), vec2i(0, 1), vec2i(0, -1));
        for (var Tap = 0u; Tap < 5u; Tap += 1u)
        {
            let Cand = BaseTraceB + Offsets[Tap];
            if (all(Cand >= vec2i(0)) && all(Cand < Extent))
            {
                let GuideB  = textureLoad(PreviousGuide, Cand, 0);
                let NormalB = DecodeOctahedral(GuideB.xy);
                if (GuideB.w > 0.5 && dot(NormalB, Normal) > 0.86 && ClassifyPriorDepth(GuideB.z, ExpectedDepth, ViewDepth))
                {
                    *OutValid = true;
                    return Cand;
                }
            }
        }
    }

    return PriorTraceA;
}

@compute @workgroup_size(8, 8)
fn ResampleTemporal(@builtin(global_invocation_id) Invocation: vec3u)
{
    let TracePixel = Invocation.xy;
    let Extent     = vec2u(Frame.TraceExtent);
    if (any(TracePixel >= Extent))
    {
        return;
    }
    let Index       = TracePixel.y * Extent.x + TracePixel.x;
    let ScreenPixel = ResolveScreenPixel(TracePixel);
    let Depth       = textureLoad(SurfaceDepth, ScreenPixel, 0);
    if (Depth <= 0.0)
    {
        textureStore(GuideTarget, vec2i(TracePixel), vec4f(0.0));
        CurrentReservoirs[Index] = PackReservoir(ConstructEmptyReservoir());
        return;
    }

    // ① Primary surface geometry & material
    let Position         = ReconstructWorldPosition(vec2f(ScreenPixel) + 0.5, Depth);
    let Attr             = textureLoad(SurfaceAttributes, ScreenPixel, 0);
    let Normal           = DecodeOctahedral(Attr.xy);
    let MaterialIdx      = min(u32(Attr.z + 0.5), arrayLength(&RayMaterials) - 1u);
    let Surface          = ProjectSurfaceSample(RayMaterials[MaterialIdx]);
    let View             = normalize(Frame.CameraPosition - Position);
    let ViewDepth        = ResolveViewDepth(Position);
    let ExpectedDepth    = ResolvePriorViewDepth(Position);
    let PrimaryThreshold = ResolvePrimaryFootprintThreshold(Frame.CameraPosition, Position, Normal, ResolveFootprintConstant(Frame));

    textureStore(GuideTarget, vec2i(TracePixel), vec4f(EncodeOctahedral(Normal), ViewDepth, 1.0 + ExpectedDepth));
    var Seed = ConstructPixelSeed(TracePixel, Frame.FrameIndex, 11u);

    let UseRayTrace  = FeatureEnabled(Frame, FeatureRayTracing);
    let Bias         = ResolveSurfaceBias(Position);
    let Basis        = ConstructTangentFrame(Normal);
    let InitialCount = select(1u, ResolveInitialCandidates(Frame), UseRayTrace);
    var Current      = ConstructEmptyReservoir();
    var SelectedTarget = 0.0;

    // ② Unified single-loop initial candidate generation (covers ReSTIR PT Enhanced, 2022 BVH Baseline, and SDF Mode)
    for (var CandIdx = 0u; CandIdx < InitialCount; CandIdx += 1u)
    {
        let PathSeed  = (ConstructPixelSeed(TracePixel, Frame.FrameIndex * 13u + CandIdx * 37u, 29u) | 1u);
        let Pair      = ProjectTemporalPair(TracePixel, Frame.FrameIndex + CandIdx * 37u, 3u + CandIdx * 2u);
        let Direction = normalize(Basis * ProjectCosineHemisphere(Pair));

        var Strike : ScreenHit;
        if (ScreenConsumerEnabled(Frame, ExtendedScreenIndirect))
        {
            Strike = TraverseScreenDepth(Position + Normal * ResolveScreenBias(ViewDepth), Direction, 300.0, ScreenTraversalSteps);
        }
        if (Strike.Hit && UseRayTrace && IsListedOccupant(ResolveEmissiveCell(Position), Strike.Identity))
        {
            Strike.Hit = false;
        }

        var Candidate    = ConstructEmptySample();
        var InitResponse : PrimaryPathResponse;
        var SelectionPdf = max(dot(Normal, Direction), 1.0e-4) * InversePi;
        var FootRatio    = 1.0;

        if (Strike.Hit)
        {
            Candidate.Position           = Strike.Position;
            Candidate.Normal             = Strike.Normal;
            Candidate.Identity           = Strike.Identity;
            Candidate.Radiance           = ResolveStrikeRadiance(Strike);
            Candidate.PrefixPosition     = Position;
            Candidate.OutgoingDirection  = -Direction;
            Candidate.PathSeed           = PathSeed;
            let DistSq                   = max(dot(Strike.Position - Position, Strike.Position - Position), 1.0e-4);
            Candidate.ForwardAreaPdf     = max(abs(dot(Strike.Normal, -Direction)), 1.0e-4) / DistSq;
            Candidate.ExitSolidPdf       = 1.0;
            Candidate.PrefixRoughness    = 1.0;
            Candidate.ReconnectRoughness = 0.8;
            Candidate.ReconnectVertex    = 2u;
            Candidate.PathLength         = 2u;
            Candidate.Flags              = 0u;
            InitResponse                 = EvaluatePrimaryPathIntegrand(Candidate, Surface, Position, Normal, View, true);
        }
        else if (UseRayTrace)
        {
            Candidate = TraceUnifiedPathCandidate(
                Position,
                Normal,
                View,
                Surface,
                Pair,
                PathSeed,
                PrimaryThreshold,
                &InitResponse,
                &SelectionPdf,
                &FootRatio
            );
        }
        else
        {
            Candidate.PathSeed           = PathSeed;
            Candidate.PrefixPosition     = Position;
            Candidate.PrefixRoughness    = 1.0;
            Candidate.ReconnectRoughness = 1.0;
            Candidate.ReconnectVertex    = 2u;
            Candidate.PathLength         = 2u;

            let Resume = max(0.5 * Bias, Strike.Travel - Bias);
            let Hit    = TraverseSceneDistance(Position + Normal * Bias, Direction, Resume, 300.0, DiffuseConeSlope);
            if (Hit.Hit)
            {
                Candidate.Position = Hit.Position;
                Candidate.Normal   = Hit.Normal;
                Candidate.Identity = Hit.Identity;
                var Gathered       = ResolveContinuedRadiance(Hit, Direction);
                let MaxGiBounces   = ResolveMaxBounces(Frame);
                var BouncePos      = Hit.Position;
                var BounceNormal   = Hit.Normal;
                var BounceWeight   = 0.45;
                for (var GiBounce = 1u; GiBounce < MaxGiBounces; GiBounce++)
                {
                    let BouncePair = vec2f(AdvanceUniform(&Seed), AdvanceUniform(&Seed));
                    let BounceDir  = ConstructTangentFrame(BounceNormal) * ProjectCosineHemisphere(BouncePair);
                    let BounceBias = ResolveSurfaceBias(BouncePos);
                    let NextHit    = TraverseSceneDistance(BouncePos + BounceNormal * BounceBias, BounceDir, 0.5 * BounceBias, 120.0, DiffuseConeSlope * 2.0);
                    if (!NextHit.Hit)
                    {
                        Gathered += BounceWeight * SampleSkyRadiance(BounceDir);
                        break;
                    }
                    Gathered += BounceWeight * ResolveContinuedRadiance(NextHit, BounceDir);
                    BouncePos     = NextHit.Position;
                    BounceNormal  = NextHit.Normal;
                    BounceWeight *= 0.42;
                }
                Candidate.Radiance       = Gathered;
                let DistSq               = max(dot(Candidate.Position - Position, Candidate.Position - Position), 1.0e-5);
                Candidate.ForwardAreaPdf = max(abs(dot(Candidate.Normal, -Direction)), 1.0e-4) / DistSq;
            }
            else
            {
                Candidate.Position       = Position + Direction * SkyReach;
                Candidate.Normal         = -Direction;
                Candidate.Identity       = SkyIdentity;
                Candidate.Radiance       = SampleSkyRadiance(Direction);
                Candidate.Flags          = PathFlagDirectional;
                Candidate.ForwardAreaPdf = 1.0;
            }
            InitResponse = EvaluatePrimaryPathIntegrand(Candidate, Surface, Position, Normal, View, false);
        }

        let InitialWeight = select(0.0, InitResponse.TargetPdf / SelectionPdf, SelectionPdf > 1.0e-6);
        if (MergeReservoir(&Current, Candidate, InitialWeight, 1.0, AdvanceUniform(&Seed)))
        {
            SelectedTarget = InitResponse.TargetPdf;
        }
    }

    if (InitialCount > 1u && Current.Tally > 0.0)
    {
        let InitW         = select(0.0, Current.WeightSum / (Current.Tally * SelectedTarget), SelectedTarget > 1.0e-8);
        Current.WeightSum = SelectedTarget * InitW;
        Current.Weight    = InitW;
        Current.Tally     = 1.0;
    }

    // ③ Temporal GRIS Reuse (§5 adaptive c_Cap + §6.4 Dual Motion Vectors + §2.3 Hybrid Shift)
    if (FeatureEnabled(Frame, FeatureTemporalReuse) && !FeatureEnabled(Frame, FeatureTemporalRestart))
    {
        var HasPrior   = false;
        let PriorTrace = ResolveTemporalPriorTrace(ScreenPixel, Position, Normal, ExpectedDepth, ViewDepth, vec2i(Extent), &HasPrior);
        if (HasPrior)
        {
            var Prior = UnpackReservoir(PreviousReservoirs[u32(PriorTrace.y) * Extent.x + u32(PriorTrace.x)]);
            if (Prior.Tally > 0.0)
            {
                let EffectiveCap = ResolveAdaptiveConfidenceCap(
                    Frame.TemporalTallyLimit,
                    Prior.Sample.DuplicationScore,
                    UseRayTrace && EnhancedOptionEnabled(Frame, EnhancedDuplicationMap)
                );
                let Tally = min(Prior.Tally, EffectiveCap);

                if (!(Prior.Weight > 0.0))
                {
                    Current.Tally += Tally;
                }
                else
                {
                    var Reused = CarryReservoirSample(Prior.Sample);
                    if (FeatureEnabled(Frame, FeatureRadianceRefresh))
                    {
                        Reused.Radiance = RefreshSampleRadiance(Reused);
                    }

                    let UseFootprint  = UseRayTrace && EnhancedOptionEnabled(Frame, EnhancedFootprintCriteria);
                    let MaxJac        = select(10.0, 6.0, FeatureEnabled(Frame, FeatureFireflyFilter));
                    let AnalyticShift = EvaluateAnalyticPairedShift(Reused, Position, Normal, PrimaryThreshold, UseRayTrace, UseFootprint, MaxJac);
                    if (AnalyticShift.w > 1.0e-8)
                    {
                        let Validate = FeatureEnabled(Frame, FeatureVisibilityReuse)
                                    && (((TracePixel.x + TracePixel.y * 3u + Frame.FrameIndex) & 3u) == 0u);
                        var ShiftJac = 0.0;
                        let Shifted  = ShiftReservoirPath(Reused, Position, Normal, View, Surface, PrimaryThreshold, Validate, &ShiftJac);
                        let UseJac   = select(1.0, ShiftJac, UseRayTrace && EnhancedOptionEnabled(Frame, EnhancedMaster));
                        if (Shifted.TargetPdf > 1.0e-8 && UseJac > 0.0)
                        {
                            if (MergeReservoir(&Current, Reused, Shifted.TargetPdf * Prior.Weight * UseJac * Tally, Tally, AdvanceUniform(&Seed)))
                            {
                                SelectedTarget = Shifted.TargetPdf;
                            }
                        }
                        else
                        {
                            // Analytic shift succeeded, so failure came from occlusion: count Tally to darken newly shadowed regions
                            Current.Tally += Tally;
                        }
                    }
                }
            }
        }
    }

    // ④ Finalize unbiased contribution weight W_X and canonical SourceTargetPdf p̂_X(X)
    Current.Sample.SourceTargetPdf = SelectedTarget;
    let RawW       = select(0.0, Current.WeightSum / (Current.Tally * SelectedTarget), SelectedTarget > 1.0e-8 && Current.Tally > 0.0);
    Current.Weight = select(0.0, min(RawW, 32.0), RawW == RawW && RawW > 0.0);
    CurrentReservoirs[Index] = PackReservoir(Current);
}
