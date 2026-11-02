//==========================================================================================================================================
//                                            SPATIOTEMPORAL ILLUMINATION SHADER
//==========================================================================================================================================
// 🧩 Unified Dual-Option Vulkan 1.3 Illumination Pipeline (Hardware Raster + Hybrid SDF-GI / Raytraced Reflections + Full Raytracing Option):
//    0. SolveSurfaceRadiosityMain      : Multi-bounce GPU Surface Card Jacobi radiosity solver
//    1. SurfaceRaster + ForwardRaster  : Hardware triangle Reversed-Z visibility + ddx/ddy mipmaps & flake AA + Snell glazing (SurfaceRaster.slang)
//    2. ShadowRaster                   : Hardware 3-cascade D32_SFLOAT solar depth rasterizer (ShadowRaster.slang)
//    3. EvaluateDirectRadianceMain     : Deterministic ShadowRaster PCF + Karis area softboxes (zero DI noise!) | Option 2: Full-RT RIS + BVH Shadow
//    4. TraceIndirectDiffuseMain       : Half-Res (TraceScale=2, 1/4 px) 1-ray Hardware Sampler3D SDF Clipmaps + SurfaceCards | Option 2: Full-RT BVH GI
//    5. TraceSpecularReflectionsMain   : Raytraced Heitz VNDF CWBVH Reflections + SurfaceCards (both modes; full ray-shadowed in Option 2)
//    6. IntegrateTemporalVarianceMain  : SVGF VarianceTemporal (motion-reprojected 2x2 moments mu1/mu2 + 3x3 disocclusion bootstrap)
//    7. FilterSpatiotemporalAtrousMain : SVGF VarianceWavelet 5x5 B3-spline + VarianceUpsample joint bilateral half-res GI upsample + ACES

#include "BoundingVolumeSearch.slang"
#include "OctetVolumeSearch.slang"
#include "VolumeDistanceSampling.slang"
#include "RangeDistanceSampling.slang"
#include "SurfaceScatteringEvaluation.slang"
#include "SurfaceRadiositySampling.slang"
#include "SurfaceReprojection.slang"
#include "LuminousFacetSampling.slang"
#include "DistanceTraversal.slang"
#include "DirectRadiance.slang"
#include "ReflectionTraversal.slang"

[[vk::binding(0, 0)]]  StructuredBuffer<float4>   FrameHeader;
[[vk::binding(1, 0)]]  StructuredBuffer<float4>   BvhOrTopCells;
[[vk::binding(2, 0)]]  StructuredBuffer<float4>   BvhTriangles;
[[vk::binding(3, 0)]]  StructuredBuffer<float4>   MaterialTable;
[[vk::binding(4, 0)]]  StructuredBuffer<float4>   DistanceOrRangeTable;
[[vk::binding(5, 0)]]  StructuredBuffer<float4>   OctetInstances;
[[vk::binding(6, 0)]]  StructuredBuffer<float4>   OctetBlocks;
[[vk::binding(7, 0)]]  StructuredBuffer<float4>   LuminousFacets;
[[vk::binding(8, 0)]]  RWStructuredBuffer<float4> SurfaceCardPack;
[[vk::binding(9, 0)]]  RWStructuredBuffer<float4> DirectReservoirCurr;
[[vk::binding(10, 0)]] RWStructuredBuffer<float4> DirectReservoirPrev;
[[vk::binding(11, 0)]] Sampler2DArray             MaterialMaps;
[[vk::binding(12, 0)]] Sampler3D                  SdfClipmapVolume;
[[vk::binding(13, 0)]] Sampler2DArray             ShadowDepthArray;

// TemporalHistoryPrev / TemporalHistoryCurr (6 layers x rgba16f, double-buffered across frames -> 100% race-free!):
//   0: PriorDiffSignal  {IntegratedDiff.rgb, DiffSpread}  (at GI TraceExtent)
//   1: PriorDiffMoments {MeanLuma, MeanLumaSq, TallyN, SceneAo}
//   2: PriorDiffGuide   {OctNormal.xy, HitT, ClassIdPlusOne}
//   3: PriorSpecSignal  {IntegratedSpec.rgb, SpecSpread}  (at Full ScreenExtent)
//   4: PriorSpecMoments {MeanLuma, MeanLumaSq, TallyN, Roughness}
//   5: PriorSpecGuide   {OctNormal.xy, HitT, ClassIdPlusOne}
[[vk::binding(14, 0), vk::image_format("rgba16f")]] RWTexture2DArray<float4> TemporalHistoryPrev;
[[vk::binding(15, 0), vk::image_format("rgba16f")]] RWTexture2DArray<float4> TemporalHistoryCurr;

// SurfaceAndWorkAtlas layers (14 x rgba16f):
//   0..5 : G-Buffer (written by Hardware SurfaceRaster + ForwardRaster, or TracePrimarySurfaceMain compute fallback)
//   6    : DirectRadiance (.rgb = linear direct diffuse+specular radiance, .a = primary solar shadow visibility [0..1])
//   7    : Raw Diffuse GI (.rgb = demodulated diffuse irradiance at TraceExtent, .a = SDF AO)
//   8    : Raw Specular Reflection (.rgb = specular reflection radiance at ScreenExtent, .a = HitFlag: >0 traced, <0 rough fallback)
//   9, 10: Diffuse GI Wavelet Ping-Pong (.rgb = filtered GI, .a = variance) at TraceExtent
//  11, 12: Specular Reflection Wavelet Ping-Pong (.rgb = filtered Spec, .a = variance) at ScreenExtent
//  13    : Current Half-Res GI Guide {OctNormal.xy, HitT, ClassIdPlusOne}
[[vk::binding(16, 0), vk::image_format("rgba16f")]] RWTexture2DArray<float4> SurfaceAndWorkAtlas;
[[vk::binding(17, 0), vk::image_format("rgba16f")]] RWTexture2DArray<float4> OutputImage;
[[vk::binding(18, 0), vk::image_format("rgba8")]]   RWTexture2D<float4>      ViewportDisplayTarget;

struct SpatiotemporalPush
{
    uint4  Counts; // {Width, Height, FrameOrParity, FinalIterFlag}
    float4 Params; // {ExposureScale, AtrousStepSize, ColourSigma, UseTemporalHistory}
};

[[vk::push_constant]] ConstantBuffer<SpatiotemporalPush> Push;

uint ResolveLuminaireBaseSlot()
{
    return max(20u, uint(FrameHeader[19].w + 0.5f));
}

bool IsFullRaytracingEnabled()
{
    return (FrameHeader[18].z > 0.5f);
}

uint ResolveGiTraceScale()
{
    return max(1u, uint(FrameHeader[18].w + 0.5f));
}

//------------------------------------------------------------------------------------------------------------------------
// DETERMINISTIC PRNG, OCTAHEDRAL ENCODING, & ACES TONEMAPPING
//------------------------------------------------------------------------------------------------------------------------

uint AdvancePcg32(inout uint Rng)
{
    uint Prev = Rng * 747796405u + 2891336453u;
    uint Word = ((Prev >> ((Prev >> 28u) + 4u)) ^ Prev) * 277803737u;
    Rng       = Prev;
    return (Word >> 22u) ^ Word;
}

float SampleUniform01(inout uint Rng)
{
    return float(AdvancePcg32(Rng) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

uint SeedPixelStream(uint2 Pixel, uint FrameIdx, uint PassSalt)
{
    uint Seed = (Pixel.x * 1973u) ^ (Pixel.y * 9277u) ^ (FrameIdx * 26699u) ^ (PassSalt * 31847u) ^ 0x9E3779B9u;
    AdvancePcg32(Seed);
    return Seed;
}

float2 EncodeOctahedral(float3 Dir)
{
    float  InvL1 = 1.0f / max(1.0e-6f, abs(Dir.x) + abs(Dir.y) + abs(Dir.z));
    float2 P     = Dir.xy * InvL1;
    if (Dir.z < 0.0f)
    {
        float2 SignNotZero = float2((P.x >= 0.0f) ? 1.0f : -1.0f, (P.y >= 0.0f) ? 1.0f : -1.0f);
        P                  = (1.0f - abs(P.yx)) * SignNotZero;
    }
    return P;
}

float3 DecodeOctahedral(float2 E)
{
    float3 V = float3(E.x, E.y, 1.0f - abs(E.x) - abs(E.y));
    if (V.z < 0.0f)
    {
        float2 SignNotZero = float2((V.x >= 0.0f) ? 1.0f : -1.0f, (V.y >= 0.0f) ? 1.0f : -1.0f);
        V.xy               = (1.0f - abs(V.yx)) * SignNotZero;
    }
    return normalize(V);
}

float Luminance709(float3 Rgb)
{
    return dot(max(Rgb, float3(0.0f)), float3(0.2126f, 0.7152f, 0.0722f));
}

float3 ApplyAcesAndSrgbOetf(float3 LinearHdr, float ExposureScale)
{
    float3 X      = max(float3(0.0f), LinearHdr * ExposureScale);
    float3 Mapped = saturate((X * (2.51f * X + 0.03f)) / max(X * (2.43f * X + 0.59f) + 0.14f, float3(1.0e-5f)));
    float3 Lo     = Mapped * 12.92f;
    float3 Hi     = 1.055f * pow(max(Mapped, float3(1.0e-6f)), float3(1.0f / 2.4f)) - 0.055f;
    return select(Mapped <= float3(0.0031308f), Lo, Hi);
}

float3 ReconstructPrimaryRayDirection(uint2 Px, uint Width, uint Height)
{
    float2 Uv       = (float2(Px) + 0.5f) / float2(float(Width), float(Height));
    float2 Ndc      = float2(2.0f * Uv.x - 1.0f, 1.0f - 2.0f * Uv.y);
    float  TanHalfV = FrameHeader[0].w;
    float3 CamRight = FrameHeader[1].xyz;
    float  TanHalfH = FrameHeader[1].w;
    float3 CamUp    = FrameHeader[2].xyz;
    float3 CamFwd   = FrameHeader[3].xyz;
    return normalize(CamFwd + Ndc.x * TanHalfH * CamRight + Ndc.y * TanHalfV * CamUp);
}

//------------------------------------------------------------------------------------------------------------------------
// COUPLED PLANETARY SKY, VOLUMETRIC CLOUDS, DUAL MOONS, & EXPONENTIAL HEIGHT FOG
//------------------------------------------------------------------------------------------------------------------------

float HashNoise3D(float3 P)
{
    float3 I = floor(P);
    float3 F = frac(P);
    F        = F * F * (3.0f - 2.0f * F);
    float  N = dot(I, float3(17.0f, 59.0f, 113.0f));
    float4 A = frac(sin(N + float4(0.0f, 17.0f, 59.0f, 76.0f)) * 43758.5453f);
    float4 B = frac(sin(N + 113.0f + float4(0.0f, 17.0f, 59.0f, 76.0f)) * 43758.5453f);
    float4 C = lerp(A, B, F.z);
    float2 D = lerp(C.xy, C.zw, F.y);
    return lerp(D.x, D.y, F.x);
}

float3 EvaluateCoupledEnvironmentSky(float3 RayDir)
{
    float3 Zenith     = FrameHeader[4].xyz;
    float3 Horizon    = FrameHeader[5].xyz;
    float3 GroundTint = FrameHeader[6].xyz;
    uint   LumCount   = uint(FrameHeader[3].w + 0.5f);
    uint   LumBase    = ResolveLuminaireBaseSlot();

    float3 Sky = (RayDir.y >= 0.0f)
                     ? lerp(Horizon, Zenith, pow(saturate(RayDir.y), 0.58f))
                     : lerp(Horizon * 0.45f, GroundTint, saturate(-RayDir.y * 2.5f));

    float3 PrimarySunDir = float3(0.35f, 0.85f, 0.38f);
    float3 PrimarySunRad = float3(0.0f);
    for (uint L = 0u; L < LumCount; ++L)
    {
        uint   Slot = LumBase + L * 4u;
        float4 L0   = FrameHeader[Slot + 0u];
        if (uint(L0.w + 0.5f) == 0u)
        {
            float3 SunDir = normalize(L0.xyz);
            float3 SunEm  = FrameHeader[Slot + 3u].xyz;
            PrimarySunDir = SunDir;
            PrimarySunRad = SunEm;
            float  CosS   = dot(RayDir, SunDir);
            Sky += SunEm * (0.018f * pow(saturate(CosS), 18.0f));
            if (CosS > 0.9993f)
            {
                Sky += SunEm * 3.5f;
            }
        }
    }

    float4 CloudMoonCfg = FrameHeader[14];
    if (CloudMoonCfg.w >= 0.0f && RayDir.y > -0.02f)
    {
        float Veil = saturate(1.0f - CloudMoonCfg.w * saturate(PrimarySunDir.y * 1.4f));
        [unroll]
        for (uint M = 0u; M < 2u; ++M)
        {
            float4 MoonSpec = FrameHeader[15u + M];
            float3 MoonDir  = normalize(MoonSpec.xyz);
            float  CosRad   = cos(max(0.004f, MoonSpec.w));
            float  CosRay   = dot(RayDir, MoonDir);
            if (CosRay >= CosRad)
            {
                float3 DiskNorm = normalize(RayDir - MoonDir * CosRay * 0.985f + MoonDir * 0.15f);
                float  Phase    = 0.12f + 0.88f * saturate(dot(DiskNorm, PrimarySunDir));
                float3 AlbedoM  = (M == 0u) ? float3(0.76f, 0.78f, 0.84f) : float3(0.84f, 0.73f, 0.65f);
                float  Limb     = smoothstep(CosRad, min(0.99999f, CosRad + 0.0005f), CosRay);
                Sky             = lerp(Sky, Sky * (1.0f - 0.65f * Veil) + AlbedoM * Phase * (1.6f * Veil + 0.25f), Limb);
            }
        }
    }

    if (CloudMoonCfg.x > 0.01f && RayDir.y > 0.035f)
    {
        float  Coverage  = saturate(CloudMoonCfg.x);
        float  DensityS  = max(0.0f, CloudMoonCfg.y);
        float  BottomM   = max(200.0f, CloudMoonCfg.z);
        float  SlabDist  = BottomM / max(0.06f, RayDir.y);
        float3 SamplePos = (FrameHeader[0].xyz + RayDir * SlabDist) * 0.00095f;
        float  Fbm       = 0.55f * HashNoise3D(SamplePos)
                         + 0.30f * HashNoise3D(SamplePos * 2.13f + 4.7f)
                         + 0.15f * HashNoise3D(SamplePos * 4.41f + 11.3f);
        float  Billow    = saturate((Fbm - (1.0f - Coverage * 0.88f)) / max(0.12f, Coverage * 0.55f));
        float  OptDepth  = Billow * Billow * DensityS * 2.8f;
        float  Trans     = exp(-OptDepth);
        float  Powder    = 1.0f - exp(-OptDepth * 2.0f);
        float  SunPhase  = 0.35f + 0.65f * pow(saturate(dot(RayDir, PrimarySunDir)), 6.0f);
        float3 CloudLit  = Zenith * 0.55f + (PrimarySunRad * 0.22f + Horizon * 0.65f) * SunPhase * (0.45f + 0.55f * Powder);
        Sky              = Sky * Trans + CloudLit * (1.0f - Trans);
    }

    return max(Sky, float3(0.0f));
}

float3 ApplyCoupledHeightFog(float3 Radiance, float3 RayOrigin, float3 RayDir, float TravelMeters)
{
    float4 Fog0 = FrameHeader[17];
    if (Fog0.w <= 1.0e-6f || TravelMeters <= 0.05f)
    {
        return Radiance;
    }
    float Density = Fog0.w;
    float Falloff = max(1.0e-4f, FrameHeader[18].x);
    float Dist    = min(TravelMeters, 450.0f);
    float BaseExp = exp(-clamp(RayOrigin.y * Falloff, -8.0f, 12.0f));
    float DeltaH  = RayDir.y * Dist * Falloff;
    float LineInt = (abs(DeltaH) > 1.0e-3f) ? ((1.0f - exp(-clamp(DeltaH, -10.0f, 10.0f))) / DeltaH) : 1.0f;
    float Tau     = clamp(Density * Dist * BaseExp * LineInt, 0.0f, 12.0f);
    float Trans   = exp(-Tau);
    return Radiance * Trans + Fog0.xyz * (1.0f - Trans);
}

//------------------------------------------------------------------------------------------------------------------------
// UNIFIED SCENE GEOMETRY & DISTANCE QUERIES
//------------------------------------------------------------------------------------------------------------------------

struct UnifiedOpaqueHit
{
    bool   Hit;
    float  DistanceM;
    uint   ClassId;
    uint   VisitedBlocks;
    float3 WorldPos;
    float3 GeoNormal;
    float3 WorldTangent;
    float2 TexCoord;
    float3 InstanceTint;
    float  InstanceSelfRadiance;
};

UnifiedOpaqueHit TraceSceneOpaque(float3 Origin, float3 RayDir, float MinT, float MaxT)
{
    uint CellCount = uint(FrameHeader[2].w + 0.5f);
    if (FrameHeader[12].x > 0.5f)
    {
        GpuOctetSurfaceHit H = SearchOctetInstancesOpaque(
            BvhOrTopCells, OctetInstances, OctetBlocks, BvhTriangles, CellCount, Origin, RayDir, MinT, MaxT);
        UnifiedOpaqueHit Out;
        Out.Hit                  = H.Hit;
        Out.DistanceM            = H.DistanceM;
        Out.ClassId              = H.ClassId;
        Out.VisitedBlocks        = H.VisitedBlocks;
        Out.WorldPos             = H.WorldPos;
        Out.GeoNormal            = (dot(H.GeoNormal, RayDir) > 0.0f) ? -H.GeoNormal : H.GeoNormal;
        Out.WorldTangent         = H.WorldTangent;
        Out.TexCoord             = H.TexCoord;
        Out.InstanceTint         = H.InstanceTint;
        Out.InstanceSelfRadiance = H.InstanceSelfRadiance;
        return Out;
    }

    GpuSurfaceHit B = SearchBoundingVolumeOpaque(BvhOrTopCells, BvhTriangles, CellCount, Origin, RayDir, MinT, MaxT);
    UnifiedOpaqueHit Out;
    Out.Hit                  = B.Hit;
    Out.DistanceM            = B.DistanceM;
    Out.ClassId              = B.ClassId;
    Out.VisitedBlocks        = 1u;
    Out.WorldPos             = B.WorldPos;
    Out.GeoNormal            = B.GeoNormal;
    Out.WorldTangent         = normalize(cross(abs(B.GeoNormal.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f), B.GeoNormal));
    Out.TexCoord             = B.TexCoord;
    Out.InstanceTint         = float3(1.0f);
    Out.InstanceSelfRadiance = 0.0f;
    return Out;
}

float3 TraceSceneShadowTransmittance(float3 Origin, float3 RayDir, float MaxT)
{
    uint CellCount = uint(FrameHeader[2].w + 0.5f);
    if (FrameHeader[12].x > 0.5f)
    {
        return SearchOctetInstancesShadow(BvhOrTopCells, OctetInstances, OctetBlocks, BvhTriangles, CellCount, Origin, RayDir, MaxT);
    }
    return SearchBoundingVolumeShadowTransmittance(BvhOrTopCells, BvhTriangles, CellCount, Origin, RayDir, 1.5e-3f, MaxT);
}

float EvaluateSceneDistanceAO(float3 WorldPos, float3 Normal)
{
    if (FrameHeader[7].z < 0.5f)
    {
        return 1.0f;
    }
    return EvaluateHardwareSdfAmbientOcclusion(
        SdfClipmapVolume, DistanceOrRangeTable, FrameHeader[12].y > 0.5f, FrameHeader[19].z, WorldPos, Normal);
}

struct LuminaireProposal
{
    bool   Valid;
    float3 LightDir;
    float  DistanceM;
    float3 UnshadowedLi;
    float  ProposalPdf;
};

LuminaireProposal SampleAnalyticLuminaire(uint LumIdx, float3 SurfacePos, float2 XiArea)
{
    LuminaireProposal Out;
    Out.Valid        = false;
    Out.LightDir     = float3(0.0f, 1.0f, 0.0f);
    Out.DistanceM    = 1.0e4f;
    Out.UnshadowedLi = float3(0.0f);
    Out.ProposalPdf  = 1.0f;

    uint   Slot = ResolveLuminaireBaseSlot() + LumIdx * 4u;
    float4 L0   = FrameHeader[Slot + 0u];
    float4 L1   = FrameHeader[Slot + 1u];
    float4 L2   = FrameHeader[Slot + 2u];
    float4 L3   = FrameHeader[Slot + 3u];
    uint   Type = uint(L0.w + 0.5f);

    if (Type == 0u)
    {
        Out.Valid        = true;
        Out.LightDir     = normalize(L0.xyz);
        Out.DistanceM    = 1.0e4f;
        Out.UnshadowedLi = L3.xyz;
        Out.ProposalPdf  = 1.0f;
    }
    else if (Type == 2u)
    {
        float3 ToLight   = L0.xyz - SurfacePos;
        float  DistSq    = max(1.0e-3f, dot(ToLight, ToLight));
        float  Dist      = sqrt(DistSq);
        Out.Valid        = true;
        Out.LightDir     = ToLight / Dist;
        Out.DistanceM    = Dist - 1.0e-2f;
        Out.UnshadowedLi = L3.xyz / (1.0f + DistSq);
        Out.ProposalPdf  = 1.0f;
    }
    else
    {
        float2 Uv      = XiArea * 2.0f - 1.0f;
        float3 SampleP = L0.xyz + Uv.x * L1.xyz + Uv.y * L2.xyz;
        float3 CrossUV = cross(L1.xyz, L2.xyz);
        float  Area    = max(1.0e-4f, 4.0f * length(CrossUV));
        float3 LightN  = normalize(CrossUV);
        float3 ToLight = SampleP - SurfacePos;
        float  DistSq  = max(1.0e-3f, dot(ToLight, ToLight));
        float  Dist    = sqrt(DistSq);
        float3 Dir     = ToLight / Dist;
        float  CosL    = abs(dot(LightN, -Dir));
        if (CosL > 1.0e-4f)
        {
            Out.Valid        = true;
            Out.LightDir     = Dir;
            Out.DistanceM    = Dist - 1.5e-2f;
            Out.UnshadowedLi = L3.xyz * (Area * CosL / DistSq);
            Out.ProposalPdf  = 1.0f;
        }
    }
    return Out;
}

LuminaireProposal ProposeCombinedDirectSample(float3 SurfacePos, inout uint Rng)
{
    uint  LumCount   = uint(FrameHeader[3].w + 0.5f);
    uint  FacetCount = uint(FrameHeader[12].z + 0.5f);
    float ProbFacet  = (FacetCount > 0u) ? ((LumCount > 0u) ? 0.25f : 1.0f) : 0.0f;
    float Roll       = SampleUniform01(Rng);

    if (ProbFacet > 0.0f && Roll < ProbFacet)
    {
        GpuLuminousFacetSample S = SampleGpuLuminousFacet(
            LuminousFacets, FacetCount, SurfacePos, SampleUniform01(Rng), SampleUniform01(Rng), SampleUniform01(Rng));
        LuminaireProposal Out;
        Out.Valid        = S.Valid && (S.SolidAnglePdf > 1.0e-6f);
        float3 ToL       = S.WorldPoint - SurfacePos;
        float  Dist      = max(1.0e-3f, length(ToL));
        Out.LightDir     = ToL / Dist;
        Out.DistanceM    = max(1.0e-3f, Dist - 1.5e-2f);
        Out.UnshadowedLi = S.SelfRadiance;
        Out.ProposalPdf  = ProbFacet * S.SolidAnglePdf;
        return Out;
    }

    if (LumCount == 0u)
    {
        LuminaireProposal Empty;
        Empty.Valid        = false;
        Empty.LightDir     = float3(0.0f, 1.0f, 0.0f);
        Empty.DistanceM    = 1.0f;
        Empty.UnshadowedLi = float3(0.0f);
        Empty.ProposalPdf  = 1.0f;
        return Empty;
    }

    uint   Chosen = min(uint(SampleUniform01(Rng) * float(LumCount)), LumCount - 1u);
    float2 XiA    = float2(SampleUniform01(Rng), SampleUniform01(Rng));
    LuminaireProposal Out = SampleAnalyticLuminaire(Chosen, SurfacePos, XiA);
    Out.ProposalPdf *= (1.0f - ProbFacet) / float(LumCount);
    return Out;
}

UnpackedSurfaceMaterial ReconstructGbufferMaterial(uint2 Px, out float3 OutN, out float3 OutT, out float3 OutB)
{
    float4 G1 = SurfaceAndWorkAtlas[uint3(Px, 1u)];
    float4 G2 = SurfaceAndWorkAtlas[uint3(Px, 2u)];
    float4 G3 = SurfaceAndWorkAtlas[uint3(Px, 3u)];
    float4 G4 = SurfaceAndWorkAtlas[uint3(Px, 4u)];
    float4 G5 = SurfaceAndWorkAtlas[uint3(Px, 5u)];

    uint ClassId                = uint(max(0.0f, G2.w) + 0.5f);
    UnpackedSurfaceMaterial Mat = FetchSurfaceMaterial(MaterialTable, ClassId);
    Mat.BaseAlbedo              = G5.xyz;
    Mat.AmbientOcclusion        = G5.w;
    Mat.BaseRoughness           = clamp(G1.w, 0.045f, 1.0f);
    Mat.Metallic                = saturate(G3.x);
    Mat.CoatCoverage            = saturate(G3.y);
    Mat.CoatRoughness           = clamp(G3.z, 0.045f, 1.0f);
    Mat.Anisotropy              = clamp(G4.w, -0.9f, 0.9f);
    Mat.SelfRadiance            = G2.xyz;

    OutN = normalize(G1.xyz);
    OutT = G4.xyz - OutN * dot(OutN, G4.xyz);
    OutT = (length(OutT) > 1.0e-5f)
               ? normalize(OutT)
               : normalize(cross(abs(OutN.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f), OutN));
    OutB = cross(OutN, OutT);
    return Mat;
}

bool ProjectToPreviousScreenPosition(float3 WorldPos, uint Width, uint Height, out float2 OutPrevScreenPos, out float2 OutMotionUv, int2 CurrPx)
{
    float4 R0    = FrameHeader[8];
    float4 R1    = FrameHeader[9];
    float4 R2    = FrameHeader[10];
    float  ViewZ = dot(R2.xyz, WorldPos) + R2.w;
    if (ViewZ <= 0.05f)
    {
        OutPrevScreenPos = float2(CurrPx) + 0.5f;
        OutMotionUv      = float2(0.0f);
        return false;
    }
    float  NdcX   = (dot(R0.xyz, WorldPos) + R0.w) / ViewZ;
    float  NdcY   = (dot(R1.xyz, WorldPos) + R1.w) / ViewZ;
    float  PrevU  = 0.5f * (NdcX + 1.0f);
    float  PrevV  = 0.5f * (1.0f - NdcY);
    float2 CurrUv = (float2(CurrPx) + 0.5f) / float2(float(Width), float(Height));
    OutMotionUv   = float2(PrevU, PrevV) - CurrUv;
    OutPrevScreenPos = float2(PrevU * float(Width), PrevV * float(Height));
    return (PrevU >= 0.0f && PrevU < 1.0f && PrevV >= 0.0f && PrevV < 1.0f);
}

float3 ShadeSecondarySurface(float3 HitPos,
                             float2 HitUv,
                             float  HitDistM,
                             float3 GeoN,
                             uint   ClassId,
                             float3 ViewDirToOrigin,
                             float3 InstanceTint,
                             float  InstanceSelfRad,
                             bool   UseRaytracedShadow)
{
    UnpackedSurfaceMaterial Mat = FetchSurfaceMaterial(MaterialTable, ClassId);
    if (Mat.AlbedoLayer >= 0)
    {
        float Mip = clamp(log2(max(1.0f, HitDistM * 0.20f * Mat.UvTilingScale)), 0.0f, 8.0f);
        Mat.BaseAlbedo *= MaterialMaps.SampleLevel(float3(HitUv * Mat.UvTilingScale, float(Mat.AlbedoLayer)), Mip).rgb;
    }
    Mat.BaseAlbedo *= InstanceTint;
    float3 OutRad   = Mat.SelfRadiance + Mat.BaseAlbedo * InstanceSelfRad;

    uint LumCount = uint(FrameHeader[3].w + 0.5f);
    for (uint L = 0u; L < min(LumCount, 3u); ++L)
    {
        LuminaireProposal Prop = SampleAnalyticLuminaire(L, HitPos, float2(0.5f, 0.5f));
        float             NdotL = max(0.0f, dot(GeoN, Prop.LightDir));
        if (Prop.Valid && NdotL > 0.0f)
        {
            float3 Vis = float3(1.0f);
            if (L == 0u)
            {
                if (UseRaytracedShadow || FrameHeader[32].w < 0.5f)
                {
                    Vis = TraceSceneShadowTransmittance(HitPos + GeoN * 0.015f, Prop.LightDir, Prop.DistanceM);
                }
                else
                {
                    float CamDist = length(HitPos - FrameHeader[0].xyz);
                    Vis           = float3(ResolveDeterministicSolarVisibility(ShadowDepthArray, FrameHeader, HitPos, GeoN, CamDist));
                }
            }
            OutRad += EvaluateSurfaceScatteringBrdf(Mat, HitPos, GeoN, ViewDirToOrigin, Prop.LightDir) * Prop.UnshadowedLi * Vis * NdotL;
        }
    }

    uint PatchCount = uint(FrameHeader[12].w + 0.5f);
    if (PatchCount > 0u && FrameHeader[13].z > 0.0f)
    {
        float3 CardRad = SampleGpuCombinedSurfaceRadiosity(SurfaceCardPack, PatchCount, HitPos, GeoN);
        OutRad += (1.0f - Mat.Metallic) * Mat.BaseAlbedo * CardRad * FrameHeader[13].z;
    }
    else
    {
        float3 SkyHemi = lerp(FrameHeader[6].xyz, FrameHeader[4].xyz, 0.5f * GeoN.y + 0.5f);
        OutRad += (1.0f - Mat.Metallic) * Mat.BaseAlbedo * SkyHemi * 0.32f;
    }
    return OutRad;
}

//------------------------------------------------------------------------------------------------------------------------
// KERNEL 0: MULTI-BOUNCE GPU SURFACE CARD RADIOSITY SOLVER
//------------------------------------------------------------------------------------------------------------------------

groupshared float3 SharedCardR0[256];
groupshared float3 SharedCardB[256];

[shader("compute")]
[numthreads(256, 1, 1)]
void SolveSurfaceRadiosityMain(uint3 Tid : SV_DispatchThreadID)
{
    uint Idx        = Tid.x;
    uint PatchCount = min(uint(FrameHeader[12].w + 0.5f), 256u);
    uint Bounces    = min(uint(FrameHeader[18].y + 0.5f), 8u);

    float3 Center = float3(0.0f);
    float3 Normal = float3(0.0f, 1.0f, 0.0f);
    float3 Refl   = float3(0.0f);
    float3 Direct = float3(0.0f);

    if (Idx < PatchCount)
    {
        uint   Base = Idx * 4u;
        float4 G0   = SurfaceCardPack[Base + 0u];
        float4 G1   = SurfaceCardPack[Base + 1u];
        float4 G2   = SurfaceCardPack[Base + 2u];
        float4 G3   = SurfaceCardPack[Base + 3u];

        Center = G0.xyz;
        Normal = normalize(G1.xyz);
        Refl   = clamp(G2.xyz, float3(0.0f), float3(0.95f));

        float3 Irrad    = float3(0.0f);
        uint   LumCount = uint(FrameHeader[3].w + 0.5f);
        for (uint L = 0u; L < LumCount; ++L)
        {
            LuminaireProposal Prop = SampleAnalyticLuminaire(L, Center, float2(0.5f, 0.5f));
            float             CosT = max(0.0f, dot(Normal, Prop.LightDir));
            if (Prop.Valid && CosT > 0.0f)
            {
                // 📝 The reference shadows card sunlight with its SDF; local lights remain unshadowed.
                float3 Vis = float3(1.0f);
                uint LuminaireType = uint(FrameHeader[ResolveLuminaireBaseSlot() + L * 4u].w + 0.5f);
                if (IsFullRaytracingEnabled())
                {
                    Vis = TraceSceneShadowTransmittance(Center + Normal * 0.04f, Prop.LightDir, Prop.DistanceM);
                }
                else if (LuminaireType == 0u && FrameHeader[7].z > 0.5f && FrameHeader[33].w > 0.5f)
                {
                    float Visibility = (FrameHeader[12].y > 0.5f)
                        ? EvaluateCardSolarVisibility(SdfClipmapVolume, DistanceOrRangeTable, Center, Normal, Prop.LightDir, 400.0f)
                        : EvaluateHardwareSdfConeVisibility(SdfClipmapVolume, DistanceOrRangeTable, false, FrameHeader[19].z,
                                                            Center, Normal, Prop.LightDir, Prop.DistanceM, 0.04f);
                    Vis = float3(Visibility);
                }
                Irrad += Prop.UnshadowedLi * Vis * CosT;
            }
        }

        float  SkyWeight = FrameHeader[13].w;
        float3 SkyIrrad  = lerp(FrameHeader[6].xyz, FrameHeader[4].xyz, 0.5f * Normal.y + 0.5f) * 3.14159265f * SkyWeight * 0.32f;
        Irrad += SkyIrrad * EvaluateSceneDistanceAO(Center, Normal);
        Direct = G3.xyz + (Refl * (1.0f / 3.14159265f)) * Irrad;
    }

    SharedCardR0[Idx] = Direct;
    SharedCardB[Idx]  = Direct;
    GroupMemoryBarrierWithGroupSync();

    for (uint Bounce = 0u; Bounce < Bounces; ++Bounce)
    {
        float3 NextB = Direct;
        if (Idx < PatchCount)
        {
            float3 IndirectIrrad = float3(0.0f);
            for (uint J = 0u; J < PatchCount; ++J)
            {
                if (J == Idx) continue;
                float4 GJ  = SurfaceCardPack[J * 4u + 0u];
                float4 NJ  = SurfaceCardPack[J * 4u + 1u];
                float3 Vec = GJ.xyz - Center;
                float  D2  = dot(Vec, Vec);
                if (D2 <= 1.0e-4f) continue;
                float  InvD = rsqrt(D2);
                float3 Dir  = Vec * InvD;
                float  CosI = dot(Normal, Dir);
                float  CosJ = dot(NJ.xyz, -Dir);
                if (CosI > 0.0f && CosJ > 0.0f)
                {
                    float FormFactor = min(0.22f, (CosI * CosJ * GJ.w) / (3.14159265f * D2 + GJ.w));
                    IndirectIrrad += SharedCardB[J] * (3.14159265f * FormFactor);
                }
            }
            NextB = SharedCardR0[Idx] + (Refl * (1.0f / 3.14159265f)) * IndirectIrrad;
        }
        GroupMemoryBarrierWithGroupSync();
        SharedCardB[Idx] = NextB;
        GroupMemoryBarrierWithGroupSync();
    }

    if (Idx < PatchCount)
    {
        SurfaceCardPack[PatchCount * 4u + Idx] = float4(SharedCardB[Idx], 1.0f);
    }
}

//------------------------------------------------------------------------------------------------------------------------
// EMERGENCY COMPUTE PRIMARY FALLBACK (ONLY USED ON HEADLESS COMPUTE-ONLY QUEUES WITHOUT GRAPHICS BIT)
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void TracePrimarySurfaceMain(uint3 DispatchId : SV_DispatchThreadID)
{
    uint Width  = Push.Counts.x;
    uint Height = Push.Counts.y;
    if (DispatchId.x >= Width || DispatchId.y >= Height)
    {
        return;
    }

    uint2  Px      = DispatchId.xy;
    float3 CamOrig = FrameHeader[0].xyz;
    float3 RayDir  = ReconstructPrimaryRayDirection(Px, Width, Height);

    UnifiedOpaqueHit Opaque    = TraceSceneOpaque(CamOrig, RayDir, 1.0e-3f, 2000.0f);
    float            MaxGlassT = Opaque.Hit ? Opaque.DistanceM : 2000.0f;
    uint             CellCount = uint(FrameHeader[2].w + 0.5f);

    bool   HitGlass       = false;
    float  GlassFrontT    = MaxGlassT;
    float  GlassThickness = 0.014f;
    float  GlassIor       = 1.52f;
    float3 GlassFrontN    = -RayDir;
    float3 GlassBackN     = RayDir;
    float3 GlassAbsorb    = float3(0.08f, 0.03f, 0.05f);

    if (FrameHeader[12].x > 0.5f)
    {
        GpuOctetGlassPeel P = SearchOctetInstancesGlassPeel(
            BvhOrTopCells, OctetInstances, OctetBlocks, BvhTriangles, CellCount, CamOrig, RayDir, MaxGlassT);
        HitGlass       = P.HitGlass;
        GlassFrontT    = P.FrontT;
        GlassThickness = P.ThicknessM;
        GlassIor       = P.Ior;
        GlassFrontN    = P.FrontNormal;
        GlassBackN     = P.BackNormal;
        GlassAbsorb    = P.AbsorptionPerM;
    }
    else
    {
        GpuGlassPeelHit P = SearchBoundingVolumeGlassPeel(
            BvhOrTopCells, BvhTriangles, CellCount, CamOrig, RayDir, 1.0e-3f, MaxGlassT);
        HitGlass       = P.HitGlass;
        GlassFrontT    = P.FrontT;
        GlassThickness = P.ThicknessM;
        GlassIor       = P.Ior;
        GlassFrontN    = P.FrontNormal;
        GlassBackN     = -P.FrontNormal;
        GlassAbsorb    = P.AbsorptionPerM;
    }

    if (HitGlass)
    {
        float3 FrontPos = CamOrig + RayDir * GlassFrontT;
        float  R1       = 0.04f;
        float3 DirIn    = RayDir;
        bool   Tir1     = false;
        EvaluateExactDielectricBoundary(RayDir, GlassFrontN, 1.0f, GlassIor, R1, DirIn, Tir1);
        float3 BackPos  = FrontPos + DirIn * GlassThickness;
        float3 ExitNorm = (dot(-DirIn, GlassBackN) > 0.05f) ? GlassBackN : GlassFrontN;
        float  R2       = 0.04f;
        float3 DirExit  = DirIn;
        bool   Tir2     = false;
        EvaluateExactDielectricBoundary(DirIn, ExitNorm, GlassIor, 1.0f, R2, DirExit, Tir2);
        if (Tir2)
        {
            DirExit         = normalize(lerp(RayDir, reflect(DirIn, ExitNorm), 0.45f));
            GlassThickness *= 2.2f;
        }
        float3           BeerLambert = exp(-GlassAbsorb * GlassThickness * 4.0f);
        UnifiedOpaqueHit Behind      = TraceSceneOpaque(BackPos + DirExit * 0.008f, DirExit, 1.0e-3f, 2000.0f);
        float3           TransRad    = Behind.Hit
                                           ? ApplyCoupledHeightFog(
                                                 ShadeSecondarySurface(Behind.WorldPos, Behind.TexCoord, Behind.DistanceM, Behind.GeoNormal, Behind.ClassId, -DirExit, Behind.InstanceTint, Behind.InstanceSelfRadiance, true),
                                                 BackPos, DirExit, Behind.DistanceM)
                                           : EvaluateCoupledEnvironmentSky(DirExit);
        float3           ReflDir = reflect(RayDir, GlassFrontN);
        UnifiedOpaqueHit ReflHit = TraceSceneOpaque(FrontPos + GlassFrontN * 0.01f, ReflDir, 1.0e-3f, 1500.0f);
        float3           ReflRad = ReflHit.Hit
                                       ? ShadeSecondarySurface(ReflHit.WorldPos, ReflHit.TexCoord, ReflHit.DistanceM, ReflHit.GeoNormal, ReflHit.ClassId, -ReflDir, ReflHit.InstanceTint, ReflHit.InstanceSelfRadiance, true)
                                       : EvaluateCoupledEnvironmentSky(ReflDir);
        float  EffectiveR = saturate(R1 + (1.0f - R1) * R2 * 0.65f);
        float3 GlassRad   = ReflRad * EffectiveR + TransRad * BeerLambert * (1.0f - EffectiveR);

        SurfaceAndWorkAtlas[uint3(Px, 0u)] = float4(FrontPos, -max(1.0e-3f, GlassFrontT));
        SurfaceAndWorkAtlas[uint3(Px, 1u)] = float4(GlassFrontN, 0.045f);
        SurfaceAndWorkAtlas[uint3(Px, 2u)] = float4(GlassRad, 7.0f);
        SurfaceAndWorkAtlas[uint3(Px, 3u)] = float4(0.0f, 0.0f, 0.045f, EffectiveR);
        SurfaceAndWorkAtlas[uint3(Px, 4u)] = float4(1.0f, 0.0f, 0.0f, 0.0f);
        SurfaceAndWorkAtlas[uint3(Px, 5u)] = float4(BeerLambert, 1.0f);
        return;
    }

    if (!Opaque.Hit)
    {
        SurfaceAndWorkAtlas[uint3(Px, 0u)] = float4(0.0f);
        SurfaceAndWorkAtlas[uint3(Px, 1u)] = float4(0.0f, 1.0f, 0.0f, 1.0f);
        SurfaceAndWorkAtlas[uint3(Px, 2u)] = float4(0.0f);
        SurfaceAndWorkAtlas[uint3(Px, 3u)] = float4(0.0f);
        SurfaceAndWorkAtlas[uint3(Px, 4u)] = float4(1.0f, 0.0f, 0.0f, 0.0f);
        SurfaceAndWorkAtlas[uint3(Px, 5u)] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float3                  GeoN = (dot(Opaque.GeoNormal, RayDir) > 0.0f) ? -Opaque.GeoNormal : Opaque.GeoNormal;
    UnpackedSurfaceMaterial Mat  = FetchSurfaceMaterial(MaterialTable, Opaque.ClassId);
    float3                  ShadingN, RotT, RotB;
    ResolveAuthoredMaterialAtHit(
        Mat, MaterialMaps, Opaque.WorldPos, Opaque.TexCoord, Opaque.DistanceM, GeoN, Opaque.WorldTangent, ShadingN, RotT, RotB);
    Mat.BaseAlbedo   *= Opaque.InstanceTint;
    Mat.SelfRadiance += Mat.BaseAlbedo * Opaque.InstanceSelfRadiance;

    float EncodedTierBlocks            = float(Mat.MaterialLodTier) + 0.01f * float(min(Opaque.VisitedBlocks, 99u));
    SurfaceAndWorkAtlas[uint3(Px, 0u)] = float4(Opaque.WorldPos, Opaque.DistanceM);
    SurfaceAndWorkAtlas[uint3(Px, 1u)] = float4(ShadingN, Mat.BaseRoughness);
    SurfaceAndWorkAtlas[uint3(Px, 2u)] = float4(Mat.SelfRadiance, float(Opaque.ClassId));
    SurfaceAndWorkAtlas[uint3(Px, 3u)] = float4(Mat.Metallic, Mat.CoatCoverage, Mat.CoatRoughness, EncodedTierBlocks);
    SurfaceAndWorkAtlas[uint3(Px, 4u)] = float4(RotT, Mat.Anisotropy);
    SurfaceAndWorkAtlas[uint3(Px, 5u)] = float4(Mat.BaseAlbedo, Mat.AmbientOcclusion);
}

//------------------------------------------------------------------------------------------------------------------------
// KERNEL 3: DIRECT RADIANCE (HYBRID: ZERO-NOISE SHADOWRASTER + KARIS AREA LIGHTS | OPTION 2: FULL RAYTRACED DI)
//------------------------------------------------------------------------------------------------------------------------

float EvaluateDirectTargetPdf(UnpackedSurfaceMaterial Mat, float3 N, float3 T, float3 B, float3 V, float3 L, float3 Li)
{
    float NdotL = max(0.0f, dot(N, L));
    if (NdotL <= 0.0f) return 0.0f;
    float3 DiffBrdf, SpecBrdf;
    EvaluateSurfaceSplitBrdf(Mat, N, T, B, V, L, DiffBrdf, SpecBrdf);
    return Luminance709((DiffBrdf + SpecBrdf) * Li * NdotL);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void EvaluateDirectRadianceMain(uint3 DispatchId : SV_DispatchThreadID)
{
    uint Width  = Push.Counts.x;
    uint Height = Push.Counts.y;
    if (DispatchId.x >= Width || DispatchId.y >= Height)
    {
        return;
    }

    uint2  Px = DispatchId.xy;
    float4 G0 = SurfaceAndWorkAtlas[uint3(Px, 0u)];
    if (G0.w <= 0.0f)
    {
        SurfaceAndWorkAtlas[uint3(Px, 6u)] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float3 WorldPos = G0.xyz;
    float  HitT     = G0.w;
    float3 ViewDir  = normalize(FrameHeader[0].xyz - WorldPos);
    float3 N, T, B;
    UnpackedSurfaceMaterial Mat = ReconstructGbufferMaterial(Px, N, T, B);

    // Default Hybrid Mode (or whenever ShadowRaster cascades are populated and FullRaytracingMode == false):
    // 100% deterministic, zero Monte Carlo noise!
    if (!IsFullRaytracingEnabled() && FrameHeader[32].w > 0.5f)
    {
        float  PrimaryShadow = 1.0f;
        float3 DirectRgb     = EvaluateDeterministicDirectRadiance(
            FrameHeader, ShadowDepthArray, SdfClipmapVolume, DistanceOrRangeTable, Mat, WorldPos, HitT, N, T, B, ViewDir, PrimaryShadow);
        SurfaceAndWorkAtlas[uint3(Px, 6u)] = float4(DirectRgb, PrimaryShadow);
        return;
    }

    // Option 2 (Full Raytracing Mode, or compute-only fallback without ShadowRaster):
    // Raytraced Beer-Lambert shadow transmittance across solar + area softboxes + luminous facets
    uint   Rng          = SeedPixelStream(Px, Push.Counts.z, 1u);
    uint   LumCount     = uint(FrameHeader[3].w + 0.5f);
    float3 RayDirectSum = float3(0.0f);
    float  PrimaryVis   = 1.0f;

    for (uint L = 0u; L < LumCount; ++L)
    {
        float2            Xi   = SampleR2Sequence(Px, Push.Counts.z, L + 1u);
        LuminaireProposal Prop = SampleAnalyticLuminaire(L, WorldPos, Xi);
        float             NdL  = max(0.0f, dot(N, Prop.LightDir));
        if (Prop.Valid && NdL > 0.0f)
        {
            float3 Vis = TraceSceneShadowTransmittance(WorldPos + N * 0.012f, Prop.LightDir, Prop.DistanceM);
            if (L == 0u) PrimaryVis = Luminance709(Vis);
            float3 DiffB, SpecB;
            EvaluateSurfaceSplitBrdf(Mat, N, T, B, ViewDir, Prop.LightDir, DiffB, SpecB);
            RayDirectSum += (DiffB + SpecB) * Prop.UnshadowedLi * Vis * NdL;
        }
    }

    uint FacetCount = uint(FrameHeader[12].z + 0.5f);
    if (FacetCount > 0u)
    {
        GpuLuminousFacetSample S = SampleGpuLuminousFacet(
            LuminousFacets, FacetCount, WorldPos, SampleUniform01(Rng), SampleUniform01(Rng), SampleUniform01(Rng));
        if (S.Valid && S.SolidAnglePdf > 1.0e-5f)
        {
            float3 ToF  = S.WorldPoint - WorldPos;
            float  Dist = max(1.0e-3f, length(ToF));
            float3 LDir = ToF / Dist;
            float  NdL  = max(0.0f, dot(N, LDir));
            if (NdL > 0.0f)
            {
                float3 Vis = TraceSceneShadowTransmittance(WorldPos + N * 0.012f, LDir, max(1.0e-3f, Dist - 0.02f));
                float3 DiffB, SpecB;
                EvaluateSurfaceSplitBrdf(Mat, N, T, B, ViewDir, LDir, DiffB, SpecB);
                RayDirectSum += min((DiffB + SpecB) * S.SelfRadiance * Vis * (NdL / S.SolidAnglePdf), float3(6.0f));
            }
        }
    }

    // Temporal accumulation of Full-RT Direct Radiance in DirectReservoirPrev/Curr
    uint   ResIdx = Px.y * Width + Px.x;
    float2 PrevPos, MotionUv;
    if (Push.Params.w > 0.5f && ProjectToPreviousScreenPosition(WorldPos, Width, Height, PrevPos, MotionUv, int2(Px)))
    {
        uint2 PrevPx = uint2(clamp(int2(PrevPos), int2(0), int2(int(Width) - 1, int(Height) - 1)));
        float4 PrevD = DirectReservoirPrev[PrevPx.y * Width + PrevPx.x];
        if (PrevD.w > 0.5f)
        {
            float HistN  = min(PrevD.w + 1.0f, 12.0f);
            RayDirectSum = lerp(PrevD.xyz, RayDirectSum, 1.0f / HistN);
            DirectReservoirCurr[ResIdx] = float4(RayDirectSum, HistN);
        }
        else
        {
            DirectReservoirCurr[ResIdx] = float4(RayDirectSum, 1.0f);
        }
    }
    else
    {
        DirectReservoirCurr[ResIdx] = float4(RayDirectSum, 1.0f);
    }
    SurfaceAndWorkAtlas[uint3(Px, 6u)] = float4(RayDirectSum, PrimaryVis);
}

//------------------------------------------------------------------------------------------------------------------------
// KERNEL 4: DIFFUSE GLOBAL ILLUMINATION
//   - Hybrid Mode (default): Half-Res (TraceScale = 2, 1/4 pixels) + 1 cosine ray/px marching Hardware Sampler3D SDF ONLY + SurfaceCards
//   - Option 2 (Full Raytracing): Full-Res (TraceScale = 1) cosine rays/px traced against Two-Level 8-Wide CWBVH + SurfaceCards
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void TraceIndirectDiffuseMain(uint3 DispatchId : SV_DispatchThreadID)
{
    uint FullW      = Push.Counts.x;
    uint FullH      = Push.Counts.y;
    uint TraceScale = ResolveGiTraceScale();
    uint TraceW     = (FullW + TraceScale - 1u) / TraceScale;
    uint TraceH     = (FullH + TraceScale - 1u) / TraceScale;

    if (DispatchId.x >= TraceW || DispatchId.y >= TraceH)
    {
        return;
    }

    uint2 TracePx  = DispatchId.xy;
    uint2 ScreenPx = min(TracePx * TraceScale, uint2(FullW - 1u, FullH - 1u));
    float4 G0      = SurfaceAndWorkAtlas[uint3(ScreenPx, 0u)];

    // On half-res silhouette quads, pick the first valid opaque sub-pixel if top-left corner hit sky/glass
    if (G0.w <= 0.0f && TraceScale > 1u)
    {
        for (uint Sub = 1u; Sub < 4u; ++Sub)
        {
            uint2 CandPx = min(TracePx * TraceScale + uint2(Sub & 1u, Sub >> 1u), uint2(FullW - 1u, FullH - 1u));
            float4 CG0   = SurfaceAndWorkAtlas[uint3(CandPx, 0u)];
            if (CG0.w > 0.0f)
            {
                ScreenPx = CandPx;
                G0       = CG0;
                break;
            }
        }
    }

    if (G0.w <= 0.0f)
    {
        SurfaceAndWorkAtlas[uint3(TracePx, 7u)]  = float4(0.0f, 0.0f, 0.0f, 1.0f);
        SurfaceAndWorkAtlas[uint3(TracePx, 13u)] = float4(0.0f);
        return;
    }

    float3 WorldPos = G0.xyz;
    float  HitT     = G0.w;
    float3 N, T, B;
    UnpackedSurfaceMaterial Mat = ReconstructGbufferMaterial(ScreenPx, N, T, B);
    uint ClassId                = uint(max(0.0f, SurfaceAndWorkAtlas[uint3(ScreenPx, 2u)].w) + 0.5f);

    SurfaceAndWorkAtlas[uint3(TracePx, 13u)] = float4(EncodeOctahedral(N), HitT, float(ClassId + 1u));

    float SceneAo = EvaluateSceneDistanceAO(WorldPos, N) * Mat.AmbientOcclusion;
    if (FrameHeader[7].w < 0.5f)
    {
        SurfaceAndWorkAtlas[uint3(TracePx, 7u)] = float4(0.0f, 0.0f, 0.0f, SceneAo);
        return;
    }

    // Coat Beer-Lambert attenuation & metallic diffuse suppression (albedo-demodulated irradiance!)
    float3 ViewDir   = normalize(FrameHeader[0].xyz - WorldPos);
    float  NdotV     = max(0.05f, dot(N, ViewDir));
    float3 CoatTrans = float3(1.0f);
    if (Mat.CoatCoverage > 0.01f)
    {
        float CoatF = EvaluateDielectricFresnelUnpolarized(NdotV, 1.0f, Mat.CoatIor);
        CoatTrans   = lerp(float3(1.0f), (1.0f - CoatF) * exp(-Mat.CoatOpticalDepth * (1.0f / NdotV)), Mat.CoatCoverage);
    }
    float3 DiffModulation = (1.0f - Mat.Metallic) * CoatTrans;

    bool   FullRt        = IsFullRaytracingEnabled();
    bool   HasSdf        = (FrameHeader[7].z > 0.5f);
    bool   ClipmapActive = (FrameHeader[12].y > 0.5f);
    float  GroundHeightM = FrameHeader[19].z;
    uint   PatchCount    = uint(FrameHeader[12].w + 0.5f);
    float  CardGain      = FrameHeader[13].z;
    float  SkyWeight     = FrameHeader[13].w;

    // Default Hybrid Mode: strictly 1 cosine-weighted hemisphere ray/px marching Hardware Sampler3D SDF clipmaps ONLY + SurfaceCards!
    if (!FullRt && HasSdf)
    {
        float2 Xi     = SampleR2Sequence(TracePx, Push.Counts.z, 5u);
        float3 RayDir = SampleCosineHemisphere(N, T, B, Xi);

        HardwareSdfHit SdfHit = TraverseHardwareSdfClipmap(
            SdfClipmapVolume, DistanceOrRangeTable, ClipmapActive, GroundHeightM, WorldPos, N, RayDir, 180.0f);

        float3 IncomingLi = float3(0.0f);
        if (SdfHit.Hit)
        {
            if (PatchCount > 0u && CardGain > 0.0f)
            {
                IncomingLi = SampleGpuCombinedSurfaceRadiosity(SurfaceCardPack, PatchCount, SdfHit.WorldPos, SdfHit.Normal) * CardGain;
            }
            // Add direct sun bounce at SDF hit gated by deterministic solar cascade visibility
            uint LumCount = uint(FrameHeader[3].w + 0.5f);
            if (LumCount > 0u)
            {
                uint   LumBase = ResolveLuminaireBaseSlot();
                float4 Sun0    = FrameHeader[LumBase + 0u];
                if (uint(Sun0.w + 0.5f) == 0u)
                {
                    float3 SunDir = normalize(Sun0.xyz);
                    float  SunNdL = max(0.0f, dot(SdfHit.Normal, SunDir));
                    if (SunNdL > 0.0f)
                    {
                        float CamDist = length(SdfHit.WorldPos - FrameHeader[0].xyz);
                        float SunVis  = ResolveDeterministicSolarVisibility(ShadowDepthArray, FrameHeader, SdfHit.WorldPos, SdfHit.Normal, CamDist);
                        IncomingLi   += FrameHeader[LumBase + 3u].xyz * (0.24f * SunVis * SunNdL);
                    }
                }
            }
        }
        else
        {
            IncomingLi = EvaluateCoupledEnvironmentSky(RayDir) * SkyWeight;
        }

        // Cosine hemisphere PDF = cos(theta)/pi cancels Lambertian cos(theta)/pi -> estimator is IncomingLi!
        float3 GiFactor = min(IncomingLi * DiffModulation * SceneAo, float3(8.0f));
        SurfaceAndWorkAtlas[uint3(TracePx, 7u)] = float4(GiFactor, SceneAo);
        return;
    }

    // Option 2 (Full Raytracing Mode): Cosine hemisphere rays traced against Two-Level 8-Wide CWBVH triangles + SurfaceCards
    uint   RayBudget = clamp(uint(FrameHeader[6].w + 0.5f), 1u, 4u);
    float3 SumGi     = float3(0.0f);
    for (uint R = 0u; R < RayBudget; ++R)
    {
        float2           Xi     = SampleR2Sequence(TracePx, Push.Counts.z, 11u + R * 7u);
        float3           RayDir = SampleCosineHemisphere(N, T, B, Xi);
        UnifiedOpaqueHit Hit    = TraceSceneOpaque(WorldPos + N * 0.015f, RayDir, 1.0e-3f, 220.0f);
        float3           Li     = Hit.Hit
                                      ? ShadeSecondarySurface(Hit.WorldPos, Hit.TexCoord, Hit.DistanceM, Hit.GeoNormal, Hit.ClassId, -RayDir, Hit.InstanceTint, Hit.InstanceSelfRadiance, true)
                                      : (EvaluateCoupledEnvironmentSky(RayDir) * SkyWeight);
        SumGi += min(Li, float3(8.0f));
    }
    float3 FullRtGi = (SumGi / float(RayBudget)) * DiffModulation * SceneAo;
    SurfaceAndWorkAtlas[uint3(TracePx, 7u)] = float4(FullRtGi, SceneAo);
}

//------------------------------------------------------------------------------------------------------------------------
// KERNEL 5: RAYTRACED REFLECTION TRAVERSAL (HEITZ 2018 VNDF CWBVH RAYTRACE + SURFACECARDS)
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void TraceSpecularReflectionsMain(uint3 DispatchId : SV_DispatchThreadID)
{
    uint Width  = Push.Counts.x;
    uint Height = Push.Counts.y;
    if (DispatchId.x >= Width || DispatchId.y >= Height)
    {
        return;
    }

    uint2  Px = DispatchId.xy;
    float4 G0 = SurfaceAndWorkAtlas[uint3(Px, 0u)];

    // Sky miss or resolved curved glazing passes straight into Specular layer 8
    if (G0.w <= 0.0f)
    {
        float3 SkyOrGlass = (G0.w == 0.0f)
                                ? EvaluateCoupledEnvironmentSky(ReconstructPrimaryRayDirection(Px, Width, Height))
                                : SurfaceAndWorkAtlas[uint3(Px, 2u)].xyz;
        SurfaceAndWorkAtlas[uint3(Px, 8u)] = float4(SkyOrGlass, 0.0f);
        return;
    }

    if (FrameHeader[7].x < 0.5f)
    {
        SurfaceAndWorkAtlas[uint3(Px, 8u)] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float3 WorldPos = G0.xyz;
    float3 ViewDir  = normalize(FrameHeader[0].xyz - WorldPos);
    float3 N, T, B;
    UnpackedSurfaceMaterial Mat = ReconstructGbufferMaterial(Px, N, T, B);

    bool  FullRt       = IsFullRaytracingEnabled();
    float EffRoughness = (Mat.CoatCoverage > 0.25f) ? min(Mat.BaseRoughness, Mat.CoatRoughness) : Mat.BaseRoughness;

    // Hybrid Mode rough-surface optimization (matches ReflectionTraversal.wgsl):
    // Rough matte surfaces (> 0.52 without clearcoat) reuse upsampled diffuse irradiance * EnvBRDF in the resolve pass!
    if (!FullRt && Mat.CoatCoverage < 0.02f && Mat.BaseRoughness > 0.52f)
    {
        SurfaceAndWorkAtlas[uint3(Px, 8u)] = float4(0.0f, 0.0f, 0.0f, -1.0f);
        return;
    }

    float  NdotV      = max(1.0e-4f, dot(N, ViewDir));
    float  DielecF0   = pow((Mat.BaseIor - 1.0f) / (Mat.BaseIor + 1.0f), 2.0f);
    float3 BaseF0     = lerp(float3(DielecF0), Mat.BaseAlbedo, Mat.Metallic);
    float  CoatFres   = Mat.CoatCoverage * EvaluateDielectricFresnelUnpolarized(NdotV, 1.0f, Mat.CoatIor);
    float  BaseWeight = (1.0f - CoatFres) * max(0.08f, Luminance709(BaseF0));
    float  CoatProb   = (Mat.CoatCoverage > 0.01f) ? clamp(CoatFres / max(1.0e-4f, CoatFres + BaseWeight), 0.15f, 0.90f) : 0.0f;

    uint   Rng        = SeedPixelStream(Px, Push.Counts.z, 19u);
    bool   SampleCoat = (SampleUniform01(Rng) < CoatProb);
    float  LobeRough  = SampleCoat ? Mat.CoatRoughness : Mat.BaseRoughness;

    float3 ReflDir;
    if (LobeRough < 0.055f)
    {
        ReflDir = reflect(-ViewDir, N);
    }
    else
    {
        float2 Xi        = SampleR2Sequence(Px, Push.Counts.z, 23u);
        float3 HalfWorld = SampleGgxVndfHalfVector(ViewDir, T, B, N, LobeRough, SampleCoat ? 0.0f : Mat.Anisotropy, Xi);
        ReflDir          = reflect(-ViewDir, HalfWorld);
    }

    if (dot(N, ReflDir) <= 1.0e-4f)
    {
        SurfaceAndWorkAtlas[uint3(Px, 8u)] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // Raytraced reflection against Two-Level 8-Wide CWBVH triangles (OctetVolumeSearch)
    UnifiedOpaqueHit ReflHit = TraceSceneOpaque(WorldPos + N * 0.014f, ReflDir, 1.0e-3f, 500.0f);
    float3           ReflLi  = ReflHit.Hit
                                   ? ApplyCoupledHeightFog(
                                         ShadeSecondarySurface(ReflHit.WorldPos, ReflHit.TexCoord, ReflHit.DistanceM, ReflHit.GeoNormal, ReflHit.ClassId, -ReflDir, ReflHit.InstanceTint, ReflHit.InstanceSelfRadiance, FullRt),
                                         WorldPos, ReflDir, ReflHit.DistanceM)
                                   : EvaluateCoupledEnvironmentSky(ReflDir);

    float3 HalfR = normalize(ViewDir + ReflDir);
    float  VdotH = saturate(dot(ViewDir, HalfR));
    float  NdotL = saturate(dot(N, ReflDir));
    float3 Throughput;
    if (SampleCoat)
    {
        float Fc   = Mat.CoatCoverage * EvaluateDielectricFresnelUnpolarized(VdotH, 1.0f, Mat.CoatIor);
        float G1L  = 2.0f * NdotL / max(1.0e-4f, NdotL + sqrt(Mat.CoatRoughness * Mat.CoatRoughness + (1.0f - Mat.CoatRoughness * Mat.CoatRoughness) * NdotL * NdotL));
        Throughput = float3(Fc * G1L / max(0.15f, CoatProb));
    }
    else
    {
        float3 Fb   = BaseF0 + (1.0f - BaseF0) * pow(1.0f - VdotH, 5.0f);
        float  G1L  = 2.0f * NdotL / max(1.0e-4f, NdotL + sqrt(Mat.BaseRoughness * Mat.BaseRoughness + (1.0f - Mat.BaseRoughness * Mat.BaseRoughness) * NdotL * NdotL));
        float  AttV = 1.0f - CoatFres;
        float  AttL = 1.0f - Mat.CoatCoverage * EvaluateDielectricFresnelUnpolarized(NdotL, 1.0f, Mat.CoatIor);
        float3 Beer = (Mat.CoatCoverage > 0.01f)
                          ? lerp(float3(1.0f), exp(-Mat.CoatOpticalDepth * (1.0f / NdotV + 1.0f / max(0.08f, NdotL))), Mat.CoatCoverage)
                          : float3(1.0f);
        Throughput  = Fb * Beer * (AttV * AttL * G1L / max(0.10f, 1.0f - CoatProb));
    }

    float3 SpecRad  = ReflLi * Throughput * (0.45f + 0.55f * Mat.AmbientOcclusion);
    float  SpecLuma = Luminance709(SpecRad);
    if (SpecLuma > 14.0f)
    {
        SpecRad *= (14.0f / SpecLuma);
    }
    SurfaceAndWorkAtlas[uint3(Px, 8u)] = float4(SpecRad, max(0.05f, ReflHit.Hit ? ReflHit.DistanceM : 200.0f));
}

//------------------------------------------------------------------------------------------------------------------------
// KERNEL 6: SVGF VARIANCE TEMPORAL INTEGRATOR (VarianceTemporal.wgsl PARITY FOR HALF-RES GI & FULL-RES REFLECTIONS)
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateTemporalVarianceMain(uint3 DispatchId : SV_DispatchThreadID)
{
    uint Width      = Push.Counts.x;
    uint Height     = Push.Counts.y;
    uint TraceScale = ResolveGiTraceScale();
    uint TraceW     = (Width + TraceScale - 1u) / TraceScale;
    uint TraceH     = (Height + TraceScale - 1u) / TraceScale;

    if (DispatchId.x >= Width || DispatchId.y >= Height)
    {
        return;
    }

    uint2 Px          = DispatchId.xy;
    float MaxHistoryM = clamp(FrameHeader[13].y, 1.0f, 64.0f);
    bool  UseTemporal = (Push.Params.w > 0.5f);

    // 1. Integrate Half-Res (or Full-Res) Diffuse GI + Moments when Px is within TraceExtent
    if (Px.x < TraceW && Px.y < TraceH)
    {
        float4 CurrGuide  = SurfaceAndWorkAtlas[uint3(Px, 13u)];
        float4 RawDiff    = SurfaceAndWorkAtlas[uint3(Px, 7u)];
        float3 Signal     = RawDiff.xyz;
        float  SceneAo    = RawDiff.w;
        float  Luma       = Luminance709(Signal);

        if (CurrGuide.w < 0.5f)
        {
            SurfaceAndWorkAtlas[uint3(Px, 9u)]  = float4(0.0f);
            TemporalHistoryCurr[uint3(Px, 0u)]  = float4(0.0f);
            TemporalHistoryCurr[uint3(Px, 1u)]  = float4(0.0f, 0.0f, 0.0f, 1.0f);
            TemporalHistoryCurr[uint3(Px, 2u)]  = float4(0.0f);
        }
        else
        {
            float3 CurrN    = DecodeOctahedral(CurrGuide.xy);
            float  CurrZ    = CurrGuide.z;
            uint2  ScreenPx = min(Px * TraceScale, uint2(Width - 1u, Height - 1u));
            float3 WorldPos = SurfaceAndWorkAtlas[uint3(ScreenPx, 0u)].xyz;
            if (SurfaceAndWorkAtlas[uint3(ScreenPx, 0u)].w <= 0.0f && TraceScale > 1u)
            {
                for (uint Sub = 1u; Sub < 4u; ++Sub)
                {
                    uint2 Cand = min(Px * TraceScale + uint2(Sub & 1u, Sub >> 1u), uint2(Width - 1u, Height - 1u));
                    if (SurfaceAndWorkAtlas[uint3(Cand, 0u)].w > 0.0f)
                    {
                        ScreenPx = Cand;
                        WorldPos = SurfaceAndWorkAtlas[uint3(Cand, 0u)].xyz;
                        break;
                    }
                }
            }

            float2 PrevScreenPos = float2(ScreenPx) + 0.5f;
            float2 MotionUv      = float2(0.0f);
            bool   InBounds      = UseTemporal && ProjectToPreviousScreenPosition(WorldPos, Width, Height, PrevScreenPos, MotionUv, int2(ScreenPx));
            float3 PriorColour = float3(0.0f);
            float3 PriorMoment = float3(0.0f);
            float  PriorAo     = 0.0f;
            float  TotalWeight = 0.0f;

            if (InBounds)
            {
                float2 PriorTracePos = PrevScreenPos / float(TraceScale) - 0.5f;
                int2   Corner        = int2(floor(PriorTracePos));
                float2 Frac          = PriorTracePos - float2(Corner);

                for (uint Tap = 0u; Tap < 4u; ++Tap)
                {
                    int2 Offset = int2(int(Tap & 1u), int(Tap >> 1u));
                    int2 Texel  = Corner + Offset;
                    if (Texel.x < 0 || Texel.y < 0 || Texel.x >= int(TraceW) || Texel.y >= int(TraceH))
                    {
                        continue;
                    }
                    float4 PGuide = TemporalHistoryPrev[uint3(uint2(Texel), 2u)];
                    bool   BadTap = (PGuide.w < 0.5f) || (dot(DecodeOctahedral(PGuide.xy), CurrN) < 0.85f) ||
                                    !ClassifyPreviousSurfaceDistance(PGuide.z, ResolvePreviousSurfaceDistance(FrameHeader, WorldPos), CurrZ);
                    if (BadTap)
                    {
                        continue;
                    }
                    float W      = ((Offset.x == 1) ? Frac.x : (1.0f - Frac.x)) * ((Offset.y == 1) ? Frac.y : (1.0f - Frac.y));
                    PriorColour += TemporalHistoryPrev[uint3(uint2(Texel), 0u)].xyz * W;
                    float4 PMom  = TemporalHistoryPrev[uint3(uint2(Texel), 1u)];
                    PriorMoment += PMom.xyz * W;
                    PriorAo     += PMom.w * W;
                    TotalWeight += W;
                }
            }

            float  Tally   = 1.0f;
            float3 Colour  = Signal;
            float2 Moments = float2(Luma, Luma * Luma);
            float  OutAo   = SceneAo;

            if (TotalWeight > 0.01f)
            {
                PriorColour /= TotalWeight;
                PriorMoment /= TotalWeight;
                PriorAo     /= TotalWeight;
                Tally        = min(PriorMoment.z + 1.0f, MaxHistoryM);
                float Alpha  = max(1.0f / Tally, 0.04f);
                Colour       = lerp(PriorColour, Signal, Alpha);
                Moments      = lerp(PriorMoment.xy, Moments, max(Alpha, 0.20f));
                OutAo        = lerp(PriorAo, SceneAo, Alpha);
            }

            float Spread = max(0.0f, Moments.y - Moments.x * Moments.x);
            // SVGF 3x3 spatial moment bootstrap on newly disoccluded pixels (Tally < 4)
            if (Tally < 4.0f)
            {
                float2 SpatMom = float2(0.0f);
                float  SpatW   = 0.0f;
                for (int Ky = -1; Ky <= 1; ++Ky)
                {
                    for (int Kx = -1; Kx <= 1; ++Kx)
                    {
                        int2 TPx = int2(Px) + int2(Kx, Ky);
                        if (TPx.x < 0 || TPx.y < 0 || TPx.x >= int(TraceW) || TPx.y >= int(TraceH)) continue;
                        float4 TG = SurfaceAndWorkAtlas[uint3(uint2(TPx), 13u)];
                        if (TG.w < 0.5f) continue;
                        float W = pow(max(0.0f, dot(CurrN, DecodeOctahedral(TG.xy))), 32.0f) *
                                  exp(-abs(TG.z - CurrZ) / (0.08f * CurrZ + 0.02f));
                        float TL = Luminance709(SurfaceAndWorkAtlas[uint3(uint2(TPx), 7u)].xyz);
                        SpatMom += float2(TL, TL * TL) * W;
                        SpatW   += W;
                    }
                }
                if (SpatW > 1.0e-4f)
                {
                    SpatMom /= SpatW;
                    Spread   = max(Spread, max(0.0f, SpatMom.y - SpatMom.x * SpatMom.x)) * (4.0f / max(1.0f, Tally));
                }
            }

            SurfaceAndWorkAtlas[uint3(Px, 9u)] = float4(Colour, Spread);
            TemporalHistoryCurr[uint3(Px, 0u)] = float4(Colour, Spread);
            TemporalHistoryCurr[uint3(Px, 1u)] = float4(Moments, Tally, OutAo);
            TemporalHistoryCurr[uint3(Px, 2u)] = CurrGuide;
        }
    }

    // 2. Integrate Full-Res Specular Reflections + Moments across ScreenExtent
    float4 G0   = SurfaceAndWorkAtlas[uint3(Px, 0u)];
    float4 RawS = SurfaceAndWorkAtlas[uint3(Px, 8u)];
    if (G0.w <= 0.0f || RawS.w < 0.0f)
    {
        // Sky, glazing, or rough-fallback marker passes through cleanly
        SurfaceAndWorkAtlas[uint3(Px, 11u)] = RawS;
        TemporalHistoryCurr[uint3(Px, 3u)]  = float4(RawS.xyz, 0.0f);
        TemporalHistoryCurr[uint3(Px, 4u)]  = float4(0.0f, 0.0f, 0.0f, 1.0f);
        TemporalHistoryCurr[uint3(Px, 5u)]  = float4(0.0f);
        return;
    }

    float4 G1           = SurfaceAndWorkAtlas[uint3(Px, 1u)];
    float4 G3           = SurfaceAndWorkAtlas[uint3(Px, 3u)];
    float3 CurrN        = normalize(G1.xyz);
    float  CurrZ        = G0.w;
    float  EffRoughness = (G3.y > 0.25f) ? min(G1.w, G3.z) : G1.w;
    float4 SpecGuide    = float4(EncodeOctahedral(CurrN), CurrZ, 1.0f + EffRoughness);
    float3 SpecSignal   = RawS.xyz;
    float  SpecLuma     = Luminance709(SpecSignal);

    float2 PrevScreenPos = float2(Px) + 0.5f;
    float2 MotionUv      = float2(0.0f);
    bool   InBounds      = UseTemporal && ProjectToPreviousScreenPosition(G0.xyz, Width, Height, PrevScreenPos, MotionUv, int2(Px));
    float3 PriorSpec = float3(0.0f);
    float3 PriorSMom = float3(0.0f);
    float  SpecWTot  = 0.0f;

    // Limit mirror history on fast camera/object motion so curved car reflections never ghost
    float MotionPx     = length(MotionUv * float2(float(Width), float(Height)));
    float SpecMaxTally = lerp(min(MaxHistoryM, 6.0f), MaxHistoryM, saturate(EffRoughness * 3.5f));
    if (EffRoughness < 0.12f && MotionPx > 1.5f)
    {
        SpecMaxTally = min(SpecMaxTally, 3.0f);
    }

    if (InBounds)
    {
        float2 PriorPos = PrevScreenPos - 0.5f;
        int2   Corner   = int2(floor(PriorPos));
        float2 Frac     = PriorPos - float2(Corner);

        for (uint Tap = 0u; Tap < 4u; ++Tap)
        {
            int2 Offset = int2(int(Tap & 1u), int(Tap >> 1u));
            int2 Texel  = Corner + Offset;
            if (Texel.x < 0 || Texel.y < 0 || Texel.x >= int(Width) || Texel.y >= int(Height)) continue;
            float4 PGuide = TemporalHistoryPrev[uint3(uint2(Texel), 5u)];
            bool   BadTap = (PGuide.w < 0.5f) || (dot(DecodeOctahedral(PGuide.xy), CurrN) < 0.92f) ||
                            !ClassifyPreviousSurfaceDistance(PGuide.z, ResolvePreviousSurfaceDistance(FrameHeader, G0.xyz), CurrZ);
            if (BadTap)
            {
                continue;
            }
            float W    = ((Offset.x == 1) ? Frac.x : (1.0f - Frac.x)) * ((Offset.y == 1) ? Frac.y : (1.0f - Frac.y));
            PriorSpec += TemporalHistoryPrev[uint3(uint2(Texel), 3u)].xyz * W;
            PriorSMom += TemporalHistoryPrev[uint3(uint2(Texel), 4u)].xyz * W;
            SpecWTot  += W;
        }
    }

    float  SpecTally   = 1.0f;
    float3 SpecColour  = SpecSignal;
    float2 SpecMoments = float2(SpecLuma, SpecLuma * SpecLuma);
    if (SpecWTot > 0.01f)
    {
        PriorSpec  /= SpecWTot;
        PriorSMom  /= SpecWTot;
        SpecTally   = min(PriorSMom.z + 1.0f, SpecMaxTally);
        float Alpha = max(1.0f / SpecTally, 0.06f);
        SpecColour  = lerp(PriorSpec, SpecSignal, Alpha);
        SpecMoments = lerp(PriorSMom.xy, SpecMoments, max(Alpha, 0.25f));
    }
    float SpecSpread = max(0.0f, SpecMoments.y - SpecMoments.x * SpecMoments.x) * (4.0f / min(SpecTally, 4.0f));

    SurfaceAndWorkAtlas[uint3(Px, 11u)] = float4(SpecColour, SpecSpread);
    TemporalHistoryCurr[uint3(Px, 3u)]  = float4(SpecColour, SpecSpread);
    TemporalHistoryCurr[uint3(Px, 4u)]  = float4(SpecMoments, SpecTally, EffRoughness);
    TemporalHistoryCurr[uint3(Px, 5u)]  = SpecGuide;
}

//------------------------------------------------------------------------------------------------------------------------
// KERNEL 7: SVGF VARIANCE WAVELET + JOINT BILATERAL HALF-RES GI UPSAMPLE + 15 INSPECTION MODES + ACES DISPLAY OUTPUT
//------------------------------------------------------------------------------------------------------------------------

float3 ColorRampPlasma(float T)
{
    float X = saturate(T);
    return saturate(float3(
        0.05f + 2.15f * X - 1.20f * X * X,
        0.02f + 0.35f * X + 0.95f * X * X * X,
        0.53f + 1.10f * X - 1.45f * X * X));
}

float3 UpsampleHalfResDiffuseGi(uint2 ScreenPx, float3 FullNormal, float FullHitT, uint TraceScale, uint TraceW, uint TraceH, uint DiffLayer, out float OutAo, out float OutNormTally)
{
    if (TraceScale <= 1u)
    {
        float4 Mom   = TemporalHistoryCurr[uint3(ScreenPx, 1u)];
        OutAo        = Mom.w;
        OutNormTally = saturate(Mom.z / max(1.0f, FrameHeader[13].y));
        return SurfaceAndWorkAtlas[uint3(ScreenPx, DiffLayer)].xyz;
    }

    float2 Pos          = float2(ScreenPx) / float(TraceScale);
    int2   Corner       = int2(floor(Pos));
    float2 Frac         = Pos - float2(Corner);
    float3 Sum          = float3(0.0f);
    float  AoSum        = 0.0f;
    float  TallySum     = 0.0f;
    float  WeightSum    = 0.0f;
    float3 Nearest      = float3(0.0f);
    float  NearestAo    = 1.0f;
    float  NearestTally = 0.0f;
    float  NearestScore = 1.0e30f;

    for (uint Tap = 0u; Tap < 4u; ++Tap)
    {
        int2  Offset = int2(int(Tap & 1u), int(Tap >> 1u));
        uint2 Texel  = uint2(clamp(Corner + Offset, int2(0), int2(int(TraceW) - 1, int(TraceH) - 1)));
        float4 Guide = SurfaceAndWorkAtlas[uint3(Texel, 13u)];
        if (Guide.w < 0.5f)
        {
            continue;
        }
        float3 Sig      = SurfaceAndWorkAtlas[uint3(Texel, DiffLayer)].xyz;
        float4 Mom      = TemporalHistoryCurr[uint3(Texel, 1u)];
        float  DepthRel = abs(Guide.z - FullHitT) / max(FullHitT, 1.0e-3f);
        float  Facing   = max(0.0f, dot(DecodeOctahedral(Guide.xy), FullNormal));
        float  Bilinear = ((Offset.x == 1) ? Frac.x : (1.0f - Frac.x)) * ((Offset.y == 1) ? Frac.y : (1.0f - Frac.y));
        float  Weight   = (Bilinear + 1.0e-3f) * exp(-DepthRel * 36.0f) * pow(Facing, 16.0f);

        Sum       += Sig * Weight;
        AoSum     += Mom.w * Weight;
        TallySum  += Mom.z * Weight;
        WeightSum += Weight;

        float Score = DepthRel + (1.0f - Facing);
        if (Score < NearestScore)
        {
            NearestScore = Score;
            Nearest      = Sig;
            NearestAo    = Mom.w;
            NearestTally = Mom.z;
        }
    }

    float MaxHist = max(1.0f, FrameHeader[13].y);
    if (WeightSum > 1.0e-4f)
    {
        OutAo        = AoSum / WeightSum;
        OutNormTally = saturate((TallySum / WeightSum) / MaxHist);
        return Sum / WeightSum;
    }
    OutAo        = NearestAo;
    OutNormTally = saturate(NearestTally / MaxHist);
    return Nearest;
}

float3 ResolveInspectionOutput(uint2  Px,
                               uint   Width,
                               uint   Height,
                               uint   Mode,
                               float4 G0,
                               float4 G1,
                               float4 G2,
                               float4 G3,
                               float4 G4,
                               float4 G5,
                               float4 DirectAndShadow,
                               float3 UpsampledGi,
                               float3 ResolvedSpec,
                               float  SceneAo,
                               float  NormHistoryM)
{
    float3 CamOrig = FrameHeader[0].xyz;
    if (G0.w == 0.0f)
    {
        return (Mode == 0u || Mode == 2u) ? ResolvedSpec : (Mode == 9u ? float3(0.02f) : ResolvedSpec * 0.15f);
    }

    bool   IsGlass   = (G0.w < 0.0f);
    float  TravelM   = abs(G0.w);
    float3 WorldPos  = G0.xyz;
    float3 ViewRayD  = normalize(WorldPos - CamOrig);
    float3 BaseAlb   = G5.xyz;
    float3 SelfRad   = IsGlass ? float3(0.0f) : G2.xyz;
    float3 DiffGiRgb = BaseAlb * UpsampledGi;
    float3 LitLinear = IsGlass ? ResolvedSpec : (SelfRad + DirectAndShadow.xyz + DiffGiRgb + ResolvedSpec);
    LitLinear        = ApplyCoupledHeightFog(LitLinear, CamOrig, ViewRayD, TravelM);

    if (Mode == 0u) return LitLinear;
    if (Mode == 1u) return IsGlass ? float3(0.0f) : DiffGiRgb + DirectAndShadow.xyz * 0.35f;
    if (Mode == 2u) return ResolvedSpec;
    if (Mode == 3u) return float3(SceneAo);
    if (Mode == 4u)
    {
        uint PatchCnt = uint(FrameHeader[12].w + 0.5f);
        return (PatchCnt > 0u) ? SampleGpuCombinedSurfaceRadiosity(SurfaceCardPack, PatchCnt, WorldPos, normalize(G1.xyz)) : float3(0.0f);
    }
    if (Mode == 5u)
    {
        if (FrameHeader[7].z < 0.5f) return float3(0.25f, 0.55f, 0.90f);
        uint  ActiveR = 0u;
        float VoxStep = 0.25f;
        SampleHardwareSceneDistance(SdfClipmapVolume, DistanceOrRangeTable, FrameHeader[12].y > 0.5f, FrameHeader[19].z, WorldPos, ActiveR, VoxStep);
        float3 CascadePalette[5] = {
            float3(0.15f, 0.85f, 0.95f),
            float3(0.28f, 0.92f, 0.35f),
            float3(0.96f, 0.72f, 0.16f),
            float3(0.90f, 0.25f, 0.78f),
            float3(0.45f, 0.48f, 0.55f),
        };
        return CascadePalette[min(ActiveR, 4u)] * (0.4f + 0.6f * SceneAo);
    }
    if (Mode == 6u) return 0.5f * normalize(G1.xyz) + 0.5f;
    if (Mode == 7u) return BaseAlb;
    if (Mode == 8u) return float3(G1.w, G3.x, 0.5f * G4.w + 0.5f);
    if (Mode == 9u) return ColorRampPlasma(1.0f - exp(-TravelM * 0.035f));
    if (Mode == 10u) return ColorRampPlasma(1.0f - DirectAndShadow.w);
    if (Mode == 11u)
    {
        float2 PrevPos, MotionUv;
        bool   Valid = ProjectToPreviousScreenPosition(WorldPos, Width, Height, PrevPos, MotionUv, int2(Px));
        return Valid ? float3(saturate(0.5f + MotionUv.x * 12.0f), saturate(0.5f + MotionUv.y * 12.0f), 0.2f)
                     : float3(0.95f, 0.12f, 0.18f);
    }
    if (Mode == 12u) return ColorRampPlasma(NormHistoryM);
    if (Mode == 13u)
    {
        uint  Tier   = uint(floor(G3.w + 1.0e-4f));
        float MipBar = saturate(frac(G3.w) * (100.0f / 96.0f));
        float3 BaseC = (Tier == 0u) ? float3(0.20f, 0.88f, 0.42f) : ((Tier == 1u) ? float3(0.95f, 0.74f, 0.18f) : float3(0.68f, 0.30f, 0.92f));
        return lerp(BaseC, float3(0.95f, 0.22f, 0.28f), MipBar * 0.55f);
    }
    if (Mode == 14u) return IsGlass ? ResolvedSpec : LitLinear * 0.06f;
    return LitLinear;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void FilterSpatiotemporalAtrousMain(uint3 DispatchId : SV_DispatchThreadID)
{
    uint Width      = Push.Counts.x;
    uint Height     = Push.Counts.y;
    uint TraceScale = ResolveGiTraceScale();
    uint TraceW     = (Width + TraceScale - 1u) / TraceScale;
    uint TraceH     = (Height + TraceScale - 1u) / TraceScale;

    if (DispatchId.x >= Width || DispatchId.y >= Height)
    {
        return;
    }

    uint2 Px          = DispatchId.xy;
    uint  SrcParity   = Push.Counts.z & 1u;
    uint  DstParity   = 1u - SrcParity;
    bool  FinalIter   = (Push.Counts.w != 0u);
    int   StepSize    = int(Push.Params.y + 0.5f);
    float ColourSigma = max(0.5f, Push.Params.z);

    uint DiffSrcLayer = 9u + SrcParity;
    uint DiffDstLayer = 9u + DstParity;
    uint SpecSrcLayer = 11u + SrcParity;
    uint SpecDstLayer = 11u + DstParity;

    static const float B3Spline[3] = {0.375f, 0.25f, 0.0625f};

    // 1. VarianceWavelet 5x5 B3-spline on Half-Res (or Full-Res) Diffuse GI when !FinalIter
    //    (Note: when FinalIter == true, we convolve into DiffDstLayer only if Iterations == 1; otherwise we convolve first AND upsample?
    //     WAIT: another workgroup might read DiffDstLayer during upsample if we wrote and upsampled in the same dispatch!
    //     So FinalIter == 2u performs the final full-res upsample+compose pass cleanly after all wavelet iterations finish!)
    if (Push.Counts.w != 2u)
    {
        if (Px.x < TraceW && Px.y < TraceH)
        {
            float4 Guide  = SurfaceAndWorkAtlas[uint3(Px, 13u)];
            float4 Centre = SurfaceAndWorkAtlas[uint3(Px, DiffSrcLayer)];
            if (Guide.w < 0.5f)
            {
                SurfaceAndWorkAtlas[uint3(Px, DiffDstLayer)] = Centre;
            }
            else
            {
                float3 Normal    = DecodeOctahedral(Guide.xy);
                float  LumaC     = Luminance709(Centre.xyz);
                float  Deviation = sqrt(max(0.0f, Centre.w));
                float3 SumRgb    = float3(0.0f);
                float  WeightSum = 0.0f;
                float  SpreadSum = 0.0f;

                for (int Ky = -2; Ky <= 2; ++Ky)
                {
                    for (int Kx = -2; Kx <= 2; ++Kx)
                    {
                        int2 TapPx = int2(Px) + int2(Kx * StepSize, Ky * StepSize);
                        if (TapPx.x < 0 || TapPx.y < 0 || TapPx.x >= int(TraceW) || TapPx.y >= int(TraceH)) continue;
                        float4 TapGuide = SurfaceAndWorkAtlas[uint3(uint2(TapPx), 13u)];
                        if (TapGuide.w < 0.5f) continue;
                        float4 Tap     = SurfaceAndWorkAtlas[uint3(uint2(TapPx), DiffSrcLayer)];
                        float  Kernel  = B3Spline[abs(Kx)] * B3Spline[abs(Ky)];
                        float  WDepth  = exp(-abs(TapGuide.z - Guide.z) / (0.02f * float(StepSize) * Guide.z + 1.0e-3f));
                        float  WNorm   = pow(max(0.0f, dot(DecodeOctahedral(TapGuide.xy), Normal)), 64.0f);
                        float  WLuma   = exp(-abs(Luminance709(Tap.xyz) - LumaC) / (ColourSigma * Deviation + 1.0e-2f));
                        float  W       = Kernel * WDepth * WNorm * WLuma;
                        SumRgb        += Tap.xyz * W;
                        SpreadSum     += W * W * Tap.w;
                        WeightSum     += W;
                    }
                }
                SurfaceAndWorkAtlas[uint3(Px, DiffDstLayer)] =
                    (WeightSum > 1.0e-6f) ? float4(SumRgb / WeightSum, SpreadSum / (WeightSum * WeightSum)) : Centre;
            }
        }

        // Roughness-gated VarianceWavelet on Full-Res Specular Reflections (mirror clearcoat < 0.07 bypasses blur!)
        float4 G0      = SurfaceAndWorkAtlas[uint3(Px, 0u)];
        float4 CentreS = SurfaceAndWorkAtlas[uint3(Px, SpecSrcLayer)];
        if (G0.w <= 0.0f || SurfaceAndWorkAtlas[uint3(Px, 8u)].w < 0.0f)
        {
            SurfaceAndWorkAtlas[uint3(Px, SpecDstLayer)] = CentreS;
            return;
        }
        float4 G1           = SurfaceAndWorkAtlas[uint3(Px, 1u)];
        float4 G3           = SurfaceAndWorkAtlas[uint3(Px, 3u)];
        float  EffRoughness = (G3.y > 0.25f) ? min(G1.w, G3.z) : G1.w;
        float  SpecGate     = smoothstep(0.065f, 0.34f, EffRoughness);
        if (SpecGate <= 1.0e-3f)
        {
            SurfaceAndWorkAtlas[uint3(Px, SpecDstLayer)] = CentreS;
            return;
        }

        float3 NormalS   = normalize(G1.xyz);
        float  LumaS     = Luminance709(CentreS.xyz);
        float  DevS      = sqrt(max(0.0f, CentreS.w));
        float3 SumSpec   = float3(0.0f);
        float  WSpecSum  = 0.0f;
        float  VarSpec   = 0.0f;

        for (int Ky = -2; Ky <= 2; ++Ky)
        {
            for (int Kx = -2; Kx <= 2; ++Kx)
            {
                int2 TapPx = int2(Px) + int2(Kx * StepSize, Ky * StepSize);
                if (TapPx.x < 0 || TapPx.y < 0 || TapPx.x >= int(Width) || TapPx.y >= int(Height)) continue;
                float4 TapG0 = SurfaceAndWorkAtlas[uint3(uint2(TapPx), 0u)];
                if (TapG0.w <= 0.0f || SurfaceAndWorkAtlas[uint3(uint2(TapPx), 8u)].w < 0.0f) continue;
                float4 TapG1  = SurfaceAndWorkAtlas[uint3(uint2(TapPx), 1u)];
                float4 TapS   = SurfaceAndWorkAtlas[uint3(uint2(TapPx), SpecSrcLayer)];
                float  Kernel = B3Spline[abs(Kx)] * B3Spline[abs(Ky)];
                float  W      = Kernel;
                if (Kx != 0 || Ky != 0)
                {
                    float WDepth = exp(-abs(TapG0.w - G0.w) / (0.015f * float(StepSize) * G0.w + 1.0e-3f));
                    float WNorm  = pow(max(0.0f, dot(normalize(TapG1.xyz), NormalS)), lerp(256.0f, 64.0f, SpecGate));
                    float WLuma  = exp(-abs(Luminance709(TapS.xyz) - LumaS) / (ColourSigma * 0.75f * DevS + 2.0e-2f));
                    W            = Kernel * SpecGate * WDepth * WNorm * WLuma;
                }
                SumSpec  += TapS.xyz * W;
                VarSpec  += W * W * TapS.w;
                WSpecSum += W;
            }
        }
        SurfaceAndWorkAtlas[uint3(Px, SpecDstLayer)] =
            (WSpecSum > 1.0e-6f) ? float4(SumSpec / WSpecSum, VarSpec / (WSpecSum * WSpecSum)) : CentreS;
        return;
    }

    // 2. Final Joint-Bilateral Half-Res GI Upsample (VarianceUpsample) + Rough Specular Fallback + Display Output (Counts.w == 2u)
    float4 G0 = SurfaceAndWorkAtlas[uint3(Px, 0u)];
    uint   Mode = uint(FrameHeader[13].x + 0.5f);

    if (G0.w <= 0.0f)
    {
        float3 SkyOrGlass = SurfaceAndWorkAtlas[uint3(Px, SpecSrcLayer)].xyz;
        float4 G1         = SurfaceAndWorkAtlas[uint3(Px, 1u)];
        float4 G2         = SurfaceAndWorkAtlas[uint3(Px, 2u)];
        float4 G3         = SurfaceAndWorkAtlas[uint3(Px, 3u)];
        float4 G4         = SurfaceAndWorkAtlas[uint3(Px, 4u)];
        float4 G5         = SurfaceAndWorkAtlas[uint3(Px, 5u)];
        float3 Final      = ResolveInspectionOutput(Px, Width, Height, Mode, G0, G1, G2, G3, G4, G5, float4(0.0f, 0.0f, 0.0f, 1.0f), float3(0.0f), SkyOrGlass, 1.0f, 0.0f);
        OutputImage[uint3(Px, 0u)] = float4(Final, 1.0f);
        ViewportDisplayTarget[Px]  = float4(ApplyAcesAndSrgbOetf(Final, Push.Params.x), 1.0f);
        return;
    }

    float4 G1           = SurfaceAndWorkAtlas[uint3(Px, 1u)];
    float4 G2           = SurfaceAndWorkAtlas[uint3(Px, 2u)];
    float4 G3           = SurfaceAndWorkAtlas[uint3(Px, 3u)];
    float4 G4           = SurfaceAndWorkAtlas[uint3(Px, 4u)];
    float4 G5           = SurfaceAndWorkAtlas[uint3(Px, 5u)];
    float4 DirectShadow = SurfaceAndWorkAtlas[uint3(Px, 6u)];
    float3 FullN        = normalize(G1.xyz);

    float  SceneAo      = G5.w;
    float  NormHistoryM = 0.0f;
    float3 UpsampledGi  = UpsampleHalfResDiffuseGi(Px, FullN, G0.w, TraceScale, TraceW, TraceH, DiffSrcLayer, SceneAo, NormHistoryM);

    float3 ResolvedSpec = SurfaceAndWorkAtlas[uint3(Px, SpecSrcLayer)].xyz;
    // Rough matte surface reflection substitution (Lumen / ReflectionTraversal.wgsl rough-reflection reuse)
    if (SurfaceAndWorkAtlas[uint3(Px, 8u)].w < 0.0f && FrameHeader[7].x > 0.5f)
    {
        float3 ViewDir = normalize(FrameHeader[0].xyz - G0.xyz);
        float  NdotV   = max(0.05f, dot(FullN, ViewDir));
        float3 F0      = lerp(float3(0.04f), G5.xyz, saturate(G3.x));
        ResolvedSpec   = EvaluateEnvBrdfApprox(F0, clamp(G1.w, 0.045f, 1.0f), NdotV) * UpsampledGi;
    }

    float3 Final = ResolveInspectionOutput(
        Px, Width, Height, Mode, G0, G1, G2, G3, G4, G5, DirectShadow, UpsampledGi, ResolvedSpec, SceneAo, NormHistoryM);
    OutputImage[uint3(Px, 0u)] = float4(Final, 1.0f);
    ViewportDisplayTarget[Px]  = float4(ApplyAcesAndSrgbOetf(Final, Push.Params.x), 1.0f);
}
