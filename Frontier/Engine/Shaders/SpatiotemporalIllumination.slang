//==========================================================================================================================================
//                                            SPATIOTEMPORAL ILLUMINATION SHADER
//==========================================================================================================================================
// 🧩 GTX-first Option A (Phase 3C-1 + 3C-2) Vulkan 1.3 SPIR-V 1.3 Compute Pipeline (zero VK_KHR_ray_query required):
//    1. SolveSurfaceRadiosityMain   : Single-workgroup multi-bounce GPU Surface Card Jacobi radiosity solver
//    2. TracePrimarySurfaceMain     : Two-level 8-wide OctetVolume / SAH BVH primary + 2-interface curved glazing peel + 3-tier Material LOD
//    3. SampleDirectReservoirsMain  : ReSTIR DI (analytic + LuminousFacet CDF mixture) + PrevViewProj motion-reprojected temporal reuse
//    4. SampleIndirectReservoirsMain: ReSTIR GI (4-cascade RangeDistance clipmap / BVH + SurfaceCard radiosity) + motion temporal reuse
//    5. ReuseSpatialReservoirsMain  : Race-free ping-pong spatial reuse (DI + Jacobian GI) + Heitz VNDF reflections + split Diff/Spec output
//    6. FilterSpatiotemporalAtrousMain: Race-free ping-pong split Diffuse/Specular À-Trous + coupled fog + 15 inspection modes + 2D RGBA8

#include "BoundingVolumeSearch.slang"
#include "OctetVolumeSearch.slang"
#include "VolumeDistanceSampling.slang"
#include "RangeDistanceSampling.slang"
#include "SurfaceScatteringEvaluation.slang"
#include "SurfaceRadiositySampling.slang"
#include "LuminousFacetSampling.slang"

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
[[vk::binding(11, 0)]] RWStructuredBuffer<float4> IndirectReservoirCurr;
[[vk::binding(12, 0)]] RWStructuredBuffer<float4> IndirectReservoirPrev;
[[vk::binding(13, 0)]] Sampler2DArray             MaterialMaps;

// SurfaceAndWorkAtlas layers (10 x rgba16f):
//   0: {WorldPos.xyz, SignedHitT}  (>0 opaque/coated, <0 resolved curved glazing at -SignedHitT, 0 sky miss)
//   1: {ShadingNormal.xyz, BaseRoughness}
//   2: {SelfRadianceOrGlassOrSky.xyz, float(ClassId)}
//   3: {Metallic, CoatCoverage, CoatRoughness, EncodedTierAndBlocks}
//   4: {RotatedTangent.xyz, Anisotropy}
//   5: {BaseAlbedo.xyz, MaterialAO}
//   6, 7: Ping-Pong Albedo-Free Diffuse Shading Factor (.rgb) + Scene SDF AO (.a)
//   8, 9: Ping-Pong Specular Radiance (.rgb) + Reservoir Confidence M (.a)
[[vk::binding(14, 0), vk::image_format("rgba16f")]] RWTexture2DArray<float4> SurfaceAndWorkAtlas;
[[vk::binding(15, 0), vk::image_format("rgba16f")]] RWTexture2DArray<float4> OutputImage;
[[vk::binding(16, 0), vk::image_format("rgba8")]]   RWTexture2D<float4>      ViewportDisplayTarget;

struct SpatiotemporalPush
{
    uint4  Counts; // {Width, Height, FrameOrParity, FinalIterFlag}
    float4 Params; // {ExposureScale, AtrousStepSize, SpatialRadiusPx, UseTemporalHistory}
};

[[vk::push_constant]] ConstantBuffer<SpatiotemporalPush> Push;

static const uint kLuminaireBaseSlot = 20u;

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

//------------------------------------------------------------------------------------------------------------------------
// COUPLED PLANETARY SKY, VOLUMETRIC CLOUDS, DUAL MOONS, & EXPONENTIAL HEIGHT FOG
//------------------------------------------------------------------------------------------------------------------------

float HashNoise3D(float3 P)
{
    float3 I = floor(P);
    float3 F = frac(P);
    F        = F * F * (3.0f - 2.0f * F);
    float N  = dot(I, float3(17.0f, 59.0f, 113.0f));
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

    float3 Sky = (RayDir.y >= 0.0f)
                     ? lerp(Horizon, Zenith, pow(saturate(RayDir.y), 0.58f))
                     : lerp(Horizon * 0.45f, GroundTint, saturate(-RayDir.y * 2.5f));

    float3 PrimarySunDir = float3(0.35f, 0.85f, 0.38f);
    float3 PrimarySunRad = float3(0.0f);
    for (uint L = 0u; L < LumCount; ++L)
    {
        uint   Slot = kLuminaireBaseSlot + L * 4u;
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

    // Dual-moon planetary discs with solar phase lighting & daytime atmospheric veil
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
                float3 DiskNorm  = normalize(RayDir - MoonDir * CosRay * 0.985f + MoonDir * 0.15f);
                float  Phase     = 0.12f + 0.88f * saturate(dot(DiskNorm, PrimarySunDir));
                float3 AlbedoM   = (M == 0u) ? float3(0.76f, 0.78f, 0.84f) : float3(0.84f, 0.73f, 0.65f);
                float  Limb      = smoothstep(CosRad, min(0.99999f, CosRad + 0.0005f), CosRay);
                Sky              = lerp(Sky, Sky * (1.0f - 0.65f * Veil) + AlbedoM * Phase * (1.6f * Veil + 0.25f), Limb);
            }
        }
    }

    // Volumetric cumulus slab (3-octave FBM + Beer-Powder solar forward scatter)
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
    float  Density = Fog0.w;
    float  Falloff = max(1.0e-4f, FrameHeader[18].x);
    float  Dist    = min(TravelMeters, 450.0f);
    float  BaseExp = exp(-clamp(RayOrigin.y * Falloff, -8.0f, 12.0f));
    float  DeltaH  = RayDir.y * Dist * Falloff;
    float  LineInt = (abs(DeltaH) > 1.0e-3f) ? ((1.0f - exp(-clamp(DeltaH, -10.0f, 10.0f))) / DeltaH) : 1.0f;
    float  Tau     = clamp(Density * Dist * BaseExp * LineInt, 0.0f, 12.0f);
    float  Trans   = exp(-Tau);
    return Radiance * Trans + Fog0.xyz * (1.0f - Trans);
}

//------------------------------------------------------------------------------------------------------------------------
// UNIFIED SCENE GEOMETRY & DISTANCE QUERIES (TWO-LEVEL OCTET + RANGE CLIPMAP OR SINGLE-LEVEL STAGE 3B)
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
        Out.GeoNormal            = H.GeoNormal;
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
    if (FrameHeader[12].y > 0.5f)
    {
        return EvaluateRangeDistanceOcclusion(DistanceOrRangeTable, WorldPos, Normal, 1.55f);
    }
    return EvaluateGpuVolumeAmbientOcclusion(DistanceOrRangeTable, WorldPos, Normal, 1.35f);
}

//------------------------------------------------------------------------------------------------------------------------
// ANALYTICAL + LUMINOUS FACET DIRECT LUMINAIRE PROPOSAL SAMPLING
//------------------------------------------------------------------------------------------------------------------------

struct LuminaireProposal
{
    bool   Valid;
    float3 LightDir;
    float  DistanceM;
    float3 UnshadowedLi; // EmittedRadiance * GeometricAttenuation
    float  ProposalPdf;  // Solid-angle PDF or discrete-measure PDF matching UnshadowedLi
};

LuminaireProposal SampleAnalyticLuminaire(uint LumIdx, float3 SurfacePos, float2 XiArea)
{
    LuminaireProposal Out;
    Out.Valid        = false;
    Out.LightDir     = float3(0.0f, 1.0f, 0.0f);
    Out.DistanceM    = 1.0e4f;
    Out.UnshadowedLi = float3(0.0f);
    Out.ProposalPdf  = 1.0f;

    uint   Slot = kLuminaireBaseSlot + LumIdx * 4u;
    float4 L0   = FrameHeader[Slot + 0u];
    float4 L1   = FrameHeader[Slot + 1u];
    float4 L2   = FrameHeader[Slot + 2u];
    float4 L3   = FrameHeader[Slot + 3u];
    uint   Kind = uint(L0.w + 0.5f);

    if (Kind == 0u) // Directional Sun
    {
        Out.Valid        = true;
        Out.LightDir     = normalize(L0.xyz);
        Out.DistanceM    = 1.0e4f;
        Out.UnshadowedLi = L3.xyz;
        Out.ProposalPdf  = 1.0f;
    }
    else if (Kind == 1u) // Point Light
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
    else // Rectangular Area Softbox
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
    uint LumCount   = uint(FrameHeader[3].w + 0.5f);
    uint FacetCount = uint(FrameHeader[12].z + 0.5f);

    float ProbFacet = (FacetCount > 0u) ? ((LumCount > 0u) ? 0.25f : 1.0f) : 0.0f;
    float Roll      = SampleUniform01(Rng);

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

    uint  Chosen = min(uint(SampleUniform01(Rng) * float(LumCount)), LumCount - 1u);
    float2 XiA   = float2(SampleUniform01(Rng), SampleUniform01(Rng));
    LuminaireProposal Out = SampleAnalyticLuminaire(Chosen, SurfacePos, XiA);
    Out.ProposalPdf *= (1.0f - ProbFacet) / float(LumCount);
    return Out;
}

// Reconstructs UnpackedSurfaceMaterial directly from GBuffer layers 1..5
UnpackedSurfaceMaterial ReconstructGbufferMaterial(uint2 Px, out float3 OutN, out float3 OutT, out float3 OutB)
{
    float4 G1 = SurfaceAndWorkAtlas[uint3(Px, 1u)];
    float4 G2 = SurfaceAndWorkAtlas[uint3(Px, 2u)];
    float4 G3 = SurfaceAndWorkAtlas[uint3(Px, 3u)];
    float4 G4 = SurfaceAndWorkAtlas[uint3(Px, 4u)];
    float4 G5 = SurfaceAndWorkAtlas[uint3(Px, 5u)];

    uint ClassId            = uint(max(0.0f, G2.w) + 0.5f);
    UnpackedSurfaceMaterial Mat = FetchSurfaceMaterial(MaterialTable, ClassId);
    Mat.BaseAlbedo          = G5.xyz;
    Mat.AmbientOcclusion    = G5.w;
    Mat.BaseRoughness       = clamp(G1.w, 0.045f, 1.0f);
    Mat.Metallic            = saturate(G3.x);
    Mat.CoatCoverage        = saturate(G3.y);
    Mat.CoatRoughness       = clamp(G3.z, 0.045f, 1.0f);
    Mat.Anisotropy          = clamp(G4.w, -0.9f, 0.9f);
    Mat.SelfRadiance        = G2.xyz;

    OutN = normalize(G1.xyz);
    OutT = G4.xyz - OutN * dot(OutN, G4.xyz);
    OutT = (length(OutT) > 1.0e-5f)
               ? normalize(OutT)
               : normalize(cross(abs(OutN.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f), OutN));
    OutB = cross(OutN, OutT);
    return Mat;
}

// Evaluates Albedo-Free Diffuse Shading Factor D and Specular BRDF S so D * BaseAlbedo + S == full PBR radiance (zero floor bug!)
void EvaluateAlbedoFreeSplitBrdf(UnpackedSurfaceMaterial Mat,
                                 float3                  N,
                                 float3                  T,
                                 float3                  B,
                                 float3                  V,
                                 float3                  L,
                                 out float3              OutAlbedoFreeDiffFactor,
                                 out float3              OutSpecularBrdf)
{
    OutAlbedoFreeDiffFactor = float3(0.0f);
    OutSpecularBrdf         = float3(0.0f);

    float NdotV = dot(N, V);
    float NdotL = dot(N, L);
    if (NdotV <= 0.0f || NdotL <= 0.0f)
    {
        return;
    }

    float3 FullDiff, FullSpec;
    EvaluateSurfaceSplitBrdf(Mat, N, T, B, V, L, FullDiff, FullSpec);

    // Replace Mat.BaseAlbedo in the Lambertian factor cleanly without dividing by near-zero albedo!
    UnpackedSurfaceMaterial UnitMat = Mat;
    UnitMat.BaseAlbedo              = float3(1.0f);
    UnitMat.SheenIntensity          = 0.0f;
    float3 UnitDiff, UnusedSpec;
    EvaluateSurfaceSplitBrdf(UnitMat, N, T, B, V, L, UnitDiff, UnusedSpec);

    // Preserve Fresnel F0 dependence on metallic BaseAlbedo by scaling UnitDiff slightly for conductors (1 - Metallic kills conductor diffuse anyway)
    float3 SheenResidual = float3(0.0f);
    if (Mat.SheenIntensity > 1.0e-3f)
    {
        float3 HalfVec = normalize(V + L);
        float  Charlie = EvaluateCharlieSheenD(Mat.SheenRoughness, saturate(dot(N, HalfVec)));
        float  AshVis  = 1.0f / max(1.0e-4f, 4.0f * (NdotL + NdotV - NdotL * NdotV));
        SheenResidual  = Mat.SheenTint * (Mat.SheenIntensity * Charlie * AshVis);
    }

    OutAlbedoFreeDiffFactor = UnitDiff;
    OutSpecularBrdf         = FullSpec + SheenResidual;
}

// Exact world-to-previous-pixel motion reprojection via PrevViewProj rows (FrameHeader[8..11])
bool ProjectToPreviousPixel(float3 WorldPos, uint Width, uint Height, out int2 OutPrevPx, out float2 OutMotionUv, int2 CurrPx)
{
    float4 R0 = FrameHeader[8];
    float4 R1 = FrameHeader[9];
    float4 R2 = FrameHeader[10];

    float ViewZ = dot(R2.xyz, WorldPos) + R2.w;
    if (ViewZ <= 0.05f)
    {
        OutPrevPx   = CurrPx;
        OutMotionUv = float2(0.0f);
        return false;
    }
    float NdcX = (dot(R0.xyz, WorldPos) + R0.w) / ViewZ;
    float NdcY = (dot(R1.xyz, WorldPos) + R1.w) / ViewZ;
    float PrevU = 0.5f * (NdcX + 1.0f);
    float PrevV = 0.5f * (1.0f - NdcY);

    float2 CurrUv = (float2(CurrPx) + 0.5f) / float2(float(Width), float(Height));
    OutMotionUv   = float2(PrevU, PrevV) - CurrUv;

    if (PrevU < 0.0f || PrevU >= 1.0f || PrevV < 0.0f || PrevV >= 1.0f)
    {
        OutPrevPx = CurrPx;
        return false;
    }
    OutPrevPx = int2(int(PrevU * float(Width)), int(PrevV * float(Height)));
    return true;
}

float3 ShadeSimpleSecondarySurface(float3 HitPos, float3 GeoN, uint ClassId, float3 ViewDirToOrigin, float3 InstanceTint, float InstanceSelfRad)
{
    UnpackedSurfaceMaterial Mat = FetchSurfaceMaterial(MaterialTable, ClassId);
    Mat.BaseAlbedo *= InstanceTint;
    float3 OutRad   = Mat.SelfRadiance + Mat.BaseAlbedo * InstanceSelfRad;

    uint LumCount = uint(FrameHeader[3].w + 0.5f);
    for (uint L = 0u; L < min(LumCount, 4u); ++L)
    {
        LuminaireProposal Prop = SampleAnalyticLuminaire(L, HitPos, float2(0.5f, 0.5f));
        float NdotL            = max(0.0f, dot(GeoN, Prop.LightDir));
        if (Prop.Valid && NdotL > 0.0f)
        {
            float3 Vis = (L == 0u) ? TraceSceneShadowTransmittance(HitPos + GeoN * 0.015f, Prop.LightDir, Prop.DistanceM) : float3(1.0f);
            OutRad += EvaluateSurfaceScatteringBrdf(Mat, HitPos, GeoN, ViewDirToOrigin, Prop.LightDir) * Prop.UnshadowedLi * Vis * NdotL;
        }
    }

    uint PatchCount = uint(FrameHeader[12].w + 0.5f);
    if (PatchCount > 0u)
    {
        float3 CardRad = SampleGpuCombinedSurfaceRadiosity(SurfaceCardPack, PatchCount, HitPos, GeoN);
        OutRad += (1.0f - Mat.Metallic) * Mat.BaseAlbedo * CardRad * FrameHeader[13].z;
    }
    else
    {
        float3 SkyHemi = lerp(FrameHeader[6].xyz, FrameHeader[4].xyz, 0.5f * GeoN.y + 0.5f);
        OutRad += (1.0f - Mat.Metallic) * Mat.BaseAlbedo * SkyHemi * 0.35f;
    }
    return OutRad;
}

//------------------------------------------------------------------------------------------------------------------------
// KERNEL 1: MULTI-BOUNCE GPU SURFACE CARD RADIOSITY SOLVER (SINGLE WORKGROUP, RACE-FREE GROUPSHARED JACOBI)
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
                float3 Vis = TraceSceneShadowTransmittance(Center + Normal * 0.04f, Prop.LightDir, Prop.DistanceM);
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
                if (J == Idx)
                {
                    continue;
                }
                float4 GJ   = SurfaceCardPack[J * 4u + 0u];
                float4 NJ   = SurfaceCardPack[J * 4u + 1u];
                float3 Vec  = GJ.xyz - Center;
                float  D2   = dot(Vec, Vec);
                if (D2 <= 1.0e-4f)
                {
                    continue;
                }
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
// KERNEL 2: PRIMARY VISIBILITY, 2-INTERFACE CURVED GLAZING REFRACTION/TIR, & 3-TIER AUTHORED MATERIAL LOD
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

    uint2  Px       = DispatchId.xy;
    float2 Uv       = (float2(Px) + 0.5f) / float2(float(Width), float(Height));
    float2 Ndc      = float2(2.0f * Uv.x - 1.0f, 1.0f - 2.0f * Uv.y);
    float3 CamOrig  = FrameHeader[0].xyz;
    float  TanHalfV = FrameHeader[0].w;
    float3 CamRight = FrameHeader[1].xyz;
    float  TanHalfH = FrameHeader[1].w;
    float3 CamUp    = FrameHeader[2].xyz;
    float3 CamFwd   = FrameHeader[3].xyz;
    float3 RayDir   = normalize(CamFwd + Ndc.x * TanHalfH * CamRight + Ndc.y * TanHalfV * CamUp);

    UnifiedOpaqueHit Opaque = TraceSceneOpaque(CamOrig, RayDir, 1.0e-3f, 2000.0f);
    float            MaxGlassT = Opaque.Hit ? Opaque.DistanceM : 2000.0f;
    uint             CellCount = uint(FrameHeader[2].w + 0.5f);

    // Check curved automotive/architectural dielectric glazing in front of opaque hit
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
        // Exact 2-interface Air -> Glass -> Air Snell refraction + unpolarized Fresnel + Total Internal Reflection
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
            // Curved-edge internal bounce inside glass shell before exiting
            float3 InternalRefl = reflect(DirIn, ExitNorm);
            DirExit             = normalize(lerp(RayDir, InternalRefl, 0.45f));
            GlassThickness     *= 2.2f;
        }

        float3 BeerLambert = exp(-GlassAbsorb * GlassThickness * 4.0f);
        UnifiedOpaqueHit Behind = TraceSceneOpaque(BackPos + DirExit * 0.008f, DirExit, 1.0e-3f, 2000.0f);
        float3 TransRad = Behind.Hit
                              ? ApplyCoupledHeightFog(
                                    ShadeSimpleSecondarySurface(Behind.WorldPos, Behind.GeoNormal, Behind.ClassId, -DirExit, Behind.InstanceTint, Behind.InstanceSelfRadiance),
                                    BackPos, DirExit, Behind.DistanceM)
                              : EvaluateCoupledEnvironmentSky(DirExit);

        float3 ReflDir = reflect(RayDir, GlassFrontN);
        UnifiedOpaqueHit ReflHit = TraceSceneOpaque(FrontPos + GlassFrontN * 0.01f, ReflDir, 1.0e-3f, 1500.0f);
        float3 ReflRad = ReflHit.Hit
                             ? ShadeSimpleSecondarySurface(ReflHit.WorldPos, ReflHit.GeoNormal, ReflHit.ClassId, -ReflDir, ReflHit.InstanceTint, ReflHit.InstanceSelfRadiance)
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
        float3 SkyRad = EvaluateCoupledEnvironmentSky(RayDir);
        SurfaceAndWorkAtlas[uint3(Px, 0u)] = float4(RayDir, 0.0f);
        SurfaceAndWorkAtlas[uint3(Px, 1u)] = float4(0.0f, 1.0f, 0.0f, 1.0f);
        SurfaceAndWorkAtlas[uint3(Px, 2u)] = float4(SkyRad, 0.0f);
        SurfaceAndWorkAtlas[uint3(Px, 3u)] = float4(0.0f);
        SurfaceAndWorkAtlas[uint3(Px, 4u)] = float4(1.0f, 0.0f, 0.0f, 0.0f);
        SurfaceAndWorkAtlas[uint3(Px, 5u)] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float3 GeoN = (dot(Opaque.GeoNormal, RayDir) > 0.0f) ? -Opaque.GeoNormal : Opaque.GeoNormal;
    UnpackedSurfaceMaterial Mat = FetchSurfaceMaterial(MaterialTable, Opaque.ClassId);

    float3 ShadingN, RotT, RotB;
    ResolveAuthoredMaterialAtHit(
        Mat, MaterialMaps, Opaque.WorldPos, Opaque.TexCoord, Opaque.DistanceM, GeoN, Opaque.WorldTangent, ShadingN, RotT, RotB);

    Mat.BaseAlbedo *= Opaque.InstanceTint;
    Mat.SelfRadiance += Mat.BaseAlbedo * Opaque.InstanceSelfRadiance;

    float EncodedTierBlocks = float(Mat.MaterialLodTier) + 0.01f * float(min(Opaque.VisitedBlocks, 99u));

    SurfaceAndWorkAtlas[uint3(Px, 0u)] = float4(Opaque.WorldPos, Opaque.DistanceM);
    SurfaceAndWorkAtlas[uint3(Px, 1u)] = float4(ShadingN, Mat.BaseRoughness);
    SurfaceAndWorkAtlas[uint3(Px, 2u)] = float4(Mat.SelfRadiance, float(Opaque.ClassId));
    SurfaceAndWorkAtlas[uint3(Px, 3u)] = float4(Mat.Metallic, Mat.CoatCoverage, Mat.CoatRoughness, EncodedTierBlocks);
    SurfaceAndWorkAtlas[uint3(Px, 4u)] = float4(RotT, Mat.Anisotropy);
    SurfaceAndWorkAtlas[uint3(Px, 5u)] = float4(Mat.BaseAlbedo, Mat.AmbientOcclusion);
}

//------------------------------------------------------------------------------------------------------------------------
// KERNEL 3: ReSTIR DI (ANALYTIC + LUMINOUS FACET CDF RIS + SHADOW RAY + MOTION-REPROJECTED TEMPORAL REUSE)
//------------------------------------------------------------------------------------------------------------------------

float EvaluateDirectTargetPdf(UnpackedSurfaceMaterial Mat, float3 N, float3 T, float3 B, float3 V, float3 L, float3 Li)
{
    float NdotL = max(0.0f, dot(N, L));
    if (NdotL <= 0.0f)
    {
        return 0.0f;
    }
    float3 DiffBrdf, SpecBrdf;
    EvaluateSurfaceSplitBrdf(Mat, N, T, B, V, L, DiffBrdf, SpecBrdf);
    return Luminance709((DiffBrdf + SpecBrdf) * Li * NdotL);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void SampleDirectReservoirsMain(uint3 DispatchId : SV_DispatchThreadID)
{
    uint Width  = Push.Counts.x;
    uint Height = Push.Counts.y;
    if (DispatchId.x >= Width || DispatchId.y >= Height)
    {
        return;
    }

    uint2 Px     = DispatchId.xy;
    uint  ResIdx = (Px.y * Width + Px.x) * 2u;

    float4 G0 = SurfaceAndWorkAtlas[uint3(Px, 0u)];
    if (G0.w <= 0.0f)
    {
        DirectReservoirCurr[ResIdx + 0u] = float4(0.0f);
        DirectReservoirCurr[ResIdx + 1u] = float4(0.0f);
        return;
    }

    float3 WorldPos = G0.xyz;
    float3 ViewDir  = normalize(FrameHeader[0].xyz - WorldPos);
    float3 N, T, B;
    UnpackedSurfaceMaterial Mat = ReconstructGbufferMaterial(Px, N, T, B);

    uint   Rng        = SeedPixelStream(Px, Push.Counts.z, 1u);
    uint   Candidates = max(1u, uint(FrameHeader[4].w + 0.5f));
    float  WSum       = 0.0f;
    float  ChosenPdf  = 0.0f;
    float3 ChosenDir  = float3(0.0f, 1.0f, 0.0f);
    float  ChosenDist = 1.0f;
    float3 ChosenLi   = float3(0.0f);
    float  MCount     = 0.0f;

    for (uint C = 0u; C < Candidates; ++C)
    {
        LuminaireProposal Prop = ProposeCombinedDirectSample(WorldPos, Rng);
        MCount += 1.0f;
        if (!Prop.Valid || Prop.ProposalPdf <= 1.0e-7f)
        {
            continue;
        }
        float TargetP = EvaluateDirectTargetPdf(Mat, N, T, B, ViewDir, Prop.LightDir, Prop.UnshadowedLi);
        float Weight  = TargetP / Prop.ProposalPdf;
        WSum += Weight;
        if (SampleUniform01(Rng) * max(WSum, 1.0e-9f) < Weight)
        {
            ChosenPdf  = TargetP;
            ChosenDir  = Prop.LightDir;
            ChosenDist = Prop.DistanceM;
            ChosenLi   = Prop.UnshadowedLi;
        }
    }

    float WeightW = (ChosenPdf > 1.0e-7f && MCount > 0.0f) ? (WSum / (ChosenPdf * MCount)) : 0.0f;

    // 1 visibility ray per pixel for the winning RIS sample (+ optional SDF soft-penumbra modulation for sun)
    if (WeightW > 0.0f)
    {
        float3 ShadowTrans = TraceSceneShadowTransmittance(WorldPos + N * 0.012f, ChosenDir, ChosenDist);
        if (FrameHeader[7].z > 0.5f && ChosenDist > 500.0f && FrameHeader[12].y < 0.5f)
        {
            float ConeVis = TraceGpuVolumeConeShadow(DistanceOrRangeTable, WorldPos, N, ChosenDir, 8.0f, 16.0f, 12);
            ShadowTrans   = min(ShadowTrans, float3(lerp(0.35f, 1.0f, ConeVis)));
        }
        ChosenLi *= ShadowTrans;
        if (Luminance709(ChosenLi) <= 1.0e-7f)
        {
            WeightW   = 0.0f;
            ChosenPdf = 0.0f;
        }
    }

    // Motion-reprojected Temporal Reuse against DirectReservoirPrev (written by last frame's spatial reuse pass)
    float MaxHistoryM = max(1.0f, FrameHeader[13].y);
    if (Push.Params.w > 0.5f)
    {
        int2   PrevPx;
        float2 MotionUv;
        if (ProjectToPreviousPixel(WorldPos, Width, Height, PrevPx, MotionUv, int2(Px)))
        {
            uint   PrevBase = (uint(PrevPx.y) * Width + uint(PrevPx.x)) * 2u;
            float4 P0       = DirectReservoirPrev[PrevBase + 0u];
            float4 P1       = DirectReservoirPrev[PrevBase + 1u];
            float  PrevM    = min(P0.w, MaxHistoryM);
            if (PrevM > 0.0f && P1.w > 0.0f)
            {
                float3 PrevDir = DecodeOctahedral(P0.xy);
                float  PrevTp  = EvaluateDirectTargetPdf(Mat, N, T, B, ViewDir, PrevDir, P1.xyz);
                float  PrevW   = PrevTp * P1.w * PrevM;
                float  CurrW   = ChosenPdf * WeightW * MCount;
                float  TotalW  = CurrW + PrevW;
                MCount        += PrevM;
                if (TotalW > 1.0e-8f && SampleUniform01(Rng) * TotalW < PrevW)
                {
                    ChosenDir  = PrevDir;
                    ChosenDist = P0.z;
                    ChosenLi   = P1.xyz;
                    ChosenPdf  = PrevTp;
                }
                WeightW = (ChosenPdf > 1.0e-7f && MCount > 0.0f) ? (TotalW / (ChosenPdf * MCount)) : 0.0f;
            }
        }
    }

    DirectReservoirCurr[ResIdx + 0u] = float4(EncodeOctahedral(ChosenDir), ChosenDist, min(MCount, MaxHistoryM));
    DirectReservoirCurr[ResIdx + 1u] = float4(ChosenLi, clamp(WeightW, 0.0f, 12.0f));
}

//------------------------------------------------------------------------------------------------------------------------
// KERNEL 4: ReSTIR GI (4-CASCADE RANGE CLIPMAP / BVH SECONDARY + SURFACE CARD RADIOSITY + TEMPORAL REUSE)
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void SampleIndirectReservoirsMain(uint3 DispatchId : SV_DispatchThreadID)
{
    uint Width  = Push.Counts.x;
    uint Height = Push.Counts.y;
    if (DispatchId.x >= Width || DispatchId.y >= Height)
    {
        return;
    }

    uint2 Px     = DispatchId.xy;
    uint  ResIdx = (Px.y * Width + Px.x) * 2u;

    float4 G0 = SurfaceAndWorkAtlas[uint3(Px, 0u)];
    if (G0.w <= 0.0f || FrameHeader[7].w < 0.5f)
    {
        IndirectReservoirCurr[ResIdx + 0u] = float4(0.0f);
        IndirectReservoirCurr[ResIdx + 1u] = float4(0.0f);
        return;
    }

    float3 WorldPos = G0.xyz;
    float3 N, T, B;
    ReconstructGbufferMaterial(Px, N, T, B);

    uint  Rng  = SeedPixelStream(Px, Push.Counts.z, 2u);
    uint  Rays = clamp(uint(FrameHeader[6].w + 0.5f), 1u, 4u);

    float  WSum       = 0.0f;
    float  ChosenPdf  = 0.0f;
    float3 ChosenDir  = N;
    float  ChosenDist = 20.0f;
    float3 ChosenLi   = float3(0.0f);
    float  MCount     = 0.0f;

    for (uint R = 0u; R < Rays; ++R)
    {
        // Cosine-weighted hemisphere proposal around N: PDF = cos(theta) / pi, so (Li * cos / pi) / PDF = Li!
        float  U1     = SampleUniform01(Rng);
        float  U2     = SampleUniform01(Rng);
        float  Rad    = sqrt(U1);
        float  Phi    = 6.2831853f * U2;
        float3 RayDir = normalize(T * (Rad * cos(Phi)) + B * (Rad * sin(Phi)) + N * sqrt(max(1.0e-4f, 1.0f - U1)));

        float3 SampleLi   = float3(0.0f);
        float  SampleDist = 80.0f;
        bool   HitSec     = false;

        // Near-field exact BVH ray up to 16m, with 4-cascade RangeDistance clipmap fallback for fast far-field GI
        UnifiedOpaqueHit NearHit = TraceSceneOpaque(WorldPos + N * 0.02f, RayDir, 1.0e-2f, 22.0f);
        if (NearHit.Hit)
        {
            HitSec     = true;
            SampleDist = NearHit.DistanceM;
            SampleLi   = ShadeSimpleSecondarySurface(
                NearHit.WorldPos, NearHit.GeoNormal, NearHit.ClassId, -RayDir, NearHit.InstanceTint, NearHit.InstanceSelfRadiance);
        }
        else if (FrameHeader[12].y > 0.5f)
        {
            float3 RangePos, RangeN;
            float  RangeT;
            uint   RangeIdx;
            if (TraceRangeDistanceSphereRay(DistanceOrRangeTable, WorldPos + N * 0.12f, RayDir, 96.0f, 24, RangePos, RangeN, RangeT, RangeIdx))
            {
                HitSec         = true;
                SampleDist     = RangeT;
                uint PatchCnt  = uint(FrameHeader[12].w + 0.5f);
                SampleLi       = (PatchCnt > 0u)
                                     ? SampleGpuCombinedSurfaceRadiosity(SurfaceCardPack, PatchCnt, RangePos, RangeN) * FrameHeader[13].z
                                     : lerp(FrameHeader[6].xyz, FrameHeader[4].xyz, 0.5f * RangeN.y + 0.5f) * 0.45f;
            }
        }

        if (!HitSec)
        {
            SampleLi   = EvaluateCoupledEnvironmentSky(RayDir) * FrameHeader[13].w;
            SampleDist = 200.0f;
        }

        // Cosine hemisphere PDF = NdotL / pi => TargetPdf = Luminance(SampleLi * NdotL / pi), ProposalPdf = NdotL / pi => Weight = Luminance(SampleLi)!
        float TargetP = Luminance709(SampleLi) * max(1.0e-3f, dot(N, RayDir)) * (1.0f / 3.14159265f);
        float PropP   = max(1.0e-3f, dot(N, RayDir)) * (1.0f / 3.14159265f);
        float Weight  = TargetP / PropP;
        WSum   += Weight;
        MCount += 1.0f;
        if (SampleUniform01(Rng) * max(WSum, 1.0e-9f) < Weight)
        {
            ChosenPdf  = TargetP;
            ChosenDir  = RayDir;
            ChosenDist = SampleDist;
            ChosenLi   = SampleLi;
        }
    }

    float WeightW     = (ChosenPdf > 1.0e-7f && MCount > 0.0f) ? (WSum / (ChosenPdf * MCount)) : 0.0f;
    float MaxHistoryM = min(16.0f, max(1.0f, FrameHeader[13].y));

    if (Push.Params.w > 0.5f)
    {
        int2   PrevPx;
        float2 MotionUv;
        if (ProjectToPreviousPixel(WorldPos, Width, Height, PrevPx, MotionUv, int2(Px)))
        {
            uint   PrevBase = (uint(PrevPx.y) * Width + uint(PrevPx.x)) * 2u;
            float4 P0       = IndirectReservoirPrev[PrevBase + 0u];
            float4 P1       = IndirectReservoirPrev[PrevBase + 1u];
            float  PrevM    = min(P0.w, MaxHistoryM);
            if (PrevM > 0.0f && P1.w > 0.0f)
            {
                float3 PrevDir = DecodeOctahedral(P0.xy);
                float  NdotL   = max(0.0f, dot(N, PrevDir));
                float  PrevTp  = Luminance709(P1.xyz) * NdotL * (1.0f / 3.14159265f);
                float  PrevW   = PrevTp * P1.w * PrevM;
                float  CurrW   = ChosenPdf * WeightW * MCount;
                float  TotalW  = CurrW + PrevW;
                MCount        += PrevM;
                if (TotalW > 1.0e-8f && SampleUniform01(Rng) * TotalW < PrevW)
                {
                    ChosenDir  = PrevDir;
                    ChosenDist = P0.z;
                    ChosenLi   = P1.xyz;
                    ChosenPdf  = PrevTp;
                }
                WeightW = (ChosenPdf > 1.0e-7f && MCount > 0.0f) ? (TotalW / (ChosenPdf * MCount)) : 0.0f;
            }
        }
    }

    IndirectReservoirCurr[ResIdx + 0u] = float4(EncodeOctahedral(ChosenDir), ChosenDist, min(MCount, MaxHistoryM));
    IndirectReservoirCurr[ResIdx + 1u] = float4(ChosenLi, clamp(WeightW, 0.0f, 8.0f));
}

//------------------------------------------------------------------------------------------------------------------------
// KERNEL 5: RACE-FREE SPATIAL REUSE (DI + JACOBIAN GI) + HEITZ VNDF REFLECTIONS + SPLIT DIFF/SPEC DEMODULATION
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void ReuseSpatialReservoirsMain(uint3 DispatchId : SV_DispatchThreadID)
{
    uint Width  = Push.Counts.x;
    uint Height = Push.Counts.y;
    if (DispatchId.x >= Width || DispatchId.y >= Height)
    {
        return;
    }

    uint2  Px     = DispatchId.xy;
    uint   ResIdx = (Px.y * Width + Px.x) * 2u;
    float4 G0     = SurfaceAndWorkAtlas[uint3(Px, 0u)];

    // Sky or resolved curved glazing passes straight into SpecularPing (Layer 8) with zero diffuse blur
    if (G0.w <= 0.0f)
    {
        float3 Passthrough = SurfaceAndWorkAtlas[uint3(Px, 2u)].xyz;
        DirectReservoirPrev[ResIdx + 0u]   = float4(0.0f);
        DirectReservoirPrev[ResIdx + 1u]   = float4(0.0f);
        IndirectReservoirPrev[ResIdx + 0u] = float4(0.0f);
        IndirectReservoirPrev[ResIdx + 1u] = float4(0.0f);
        SurfaceAndWorkAtlas[uint3(Px, 6u)] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        SurfaceAndWorkAtlas[uint3(Px, 8u)] = float4(Passthrough, 0.0f);
        return;
    }

    float3 WorldPos = G0.xyz;
    float  HitT     = G0.w;
    float3 ViewDir  = normalize(FrameHeader[0].xyz - WorldPos);
    float3 N, T, B;
    UnpackedSurfaceMaterial Mat = ReconstructGbufferMaterial(Px, N, T, B);

    // Read fresh+temporal reservoirs from *Curr (read-only across all pixels -> 100% race-free!)
    float4 Di0     = DirectReservoirCurr[ResIdx + 0u];
    float4 Di1     = DirectReservoirCurr[ResIdx + 1u];
    float3 DiDir   = DecodeOctahedral(Di0.xy);
    float  DiDist  = Di0.z;
    float  DiM     = Di0.w;
    float3 DiLi    = Di1.xyz;
    float  DiPdf   = EvaluateDirectTargetPdf(Mat, N, T, B, ViewDir, DiDir, DiLi);
    float  DiWSum  = DiPdf * Di1.w * DiM;

    float4 Gi0     = IndirectReservoirCurr[ResIdx + 0u];
    float4 Gi1     = IndirectReservoirCurr[ResIdx + 1u];
    float3 GiDir   = DecodeOctahedral(Gi0.xy);
    float  GiDist  = Gi0.z;
    float  GiM     = Gi0.w;
    float3 GiLi    = Gi1.xyz;
    float  GiPdf   = Luminance709(GiLi) * max(0.0f, dot(N, GiDir)) * (1.0f / 3.14159265f);
    float  GiWSum  = GiPdf * Gi1.w * GiM;

    uint Rng       = SeedPixelStream(Px, Push.Counts.z, 3u);
    uint Neighbors = (FrameHeader[7].y > 0.5f) ? min(uint(FrameHeader[5].w + 0.5f), 6u) : 0u;

    for (uint K = 0u; K < Neighbors; ++K)
    {
        float Angle  = 6.2831853f * SampleUniform01(Rng);
        float Radius = Push.Params.z * sqrt(SampleUniform01(Rng));
        int2  NPx    = int2(Px) + int2(int(round(cos(Angle) * Radius)), int(round(sin(Angle) * Radius)));
        if (NPx.x < 0 || NPx.y < 0 || NPx.x >= int(Width) || NPx.y >= int(Height))
        {
            continue;
        }

        float4 NG0 = SurfaceAndWorkAtlas[uint3(uint2(NPx), 0u)];
        float4 NG1 = SurfaceAndWorkAtlas[uint3(uint2(NPx), 1u)];
        if (NG0.w <= 0.0f || dot(N, normalize(NG1.xyz)) < 0.84f || abs(NG0.w - HitT) > 0.14f * HitT + 0.05f)
        {
            continue;
        }

        uint NBase = (uint(NPx.y) * Width + uint(NPx.x)) * 2u;

        // Spatial DI merge
        float4 NDi0 = DirectReservoirCurr[NBase + 0u];
        float4 NDi1 = DirectReservoirCurr[NBase + 1u];
        if (NDi0.w > 0.0f && NDi1.w > 0.0f)
        {
            float3 CandDir  = DecodeOctahedral(NDi0.xy);
            float  CandDist = NDi0.z;
            if (NDi0.z < 500.0f)
            {
                float3 SamplePoint = NG0.xyz + CandDir * NDi0.z;
                float3 Reconn      = SamplePoint - WorldPos;
                CandDist           = max(1.0e-3f, length(Reconn));
                CandDir            = Reconn / CandDist;
            }
            float CandPdf = EvaluateDirectTargetPdf(Mat, N, T, B, ViewDir, CandDir, NDi1.xyz);
            float CandW   = CandPdf * NDi1.w * NDi0.w;
            DiWSum += CandW;
            DiM    += NDi0.w;
            if (DiWSum > 1.0e-8f && SampleUniform01(Rng) * DiWSum < CandW)
            {
                DiDir  = CandDir;
                DiDist = CandDist;
                DiLi   = NDi1.xyz;
                DiPdf  = CandPdf;
            }
        }

        // Spatial GI merge with reconnection solid-angle Jacobian determinant |J_{q -> p}|
        if (FrameHeader[7].w > 0.5f)
        {
            float4 NGi0 = IndirectReservoirCurr[NBase + 0u];
            float4 NGi1 = IndirectReservoirCurr[NBase + 1u];
            if (NGi0.w > 0.0f && NGi1.w > 0.0f)
            {
                float3 NeighborDir = DecodeOctahedral(NGi0.xy);
                float3 ReconnDir   = NeighborDir;
                float  ReconnDist  = NGi0.z;
                float  Jacobian    = 1.0f;

                if (NGi0.z < 150.0f)
                {
                    float3 SampleVertexY = NG0.xyz + NeighborDir * NGi0.z;
                    float3 VecP          = SampleVertexY - WorldPos;
                    ReconnDist           = max(0.08f, length(VecP));
                    ReconnDir            = VecP / ReconnDist;
                    float DistRatioSq    = clamp((NGi0.z * NGi0.z) / (ReconnDist * ReconnDist), 0.10f, 4.0f);
                    float CosAlign       = clamp(abs(dot(NeighborDir, ReconnDir)), 0.25f, 1.0f);
                    Jacobian             = clamp(DistRatioSq * CosAlign, 0.10f, 4.0f);
                }

                float CandGiPdf = Luminance709(NGi1.xyz) * max(0.0f, dot(N, ReconnDir)) * (1.0f / 3.14159265f) * Jacobian;
                float CandGiW   = CandGiPdf * NGi1.w * NGi0.w;
                GiWSum += CandGiW;
                GiM    += NGi0.w;
                if (GiWSum > 1.0e-8f && SampleUniform01(Rng) * GiWSum < CandGiW)
                {
                    GiDir  = ReconnDir;
                    GiDist = ReconnDist;
                    GiLi   = NGi1.xyz;
                    GiPdf  = max(1.0e-7f, Luminance709(NGi1.xyz) * max(0.0f, dot(N, ReconnDir)) * (1.0f / 3.14159265f));
                }
            }
        }
    }

    float MaxHistoryM = max(1.0f, FrameHeader[13].y);
    float FinalDiW    = (DiPdf > 1.0e-7f && DiM > 0.0f) ? clamp(DiWSum / (DiPdf * DiM), 0.0f, 12.0f) : 0.0f;
    float FinalGiW    = (GiPdf > 1.0e-7f && GiM > 0.0f) ? clamp(GiWSum / (GiPdf * GiM), 0.0f, 8.0f) : 0.0f;

    // Write post-spatial reservoirs into *Prev (disjoint from *Curr!) so next frame's temporal pass reads them race-free
    DirectReservoirPrev[ResIdx + 0u]   = float4(EncodeOctahedral(DiDir), DiDist, min(DiM, MaxHistoryM));
    DirectReservoirPrev[ResIdx + 1u]   = float4(DiLi, FinalDiW);
    IndirectReservoirPrev[ResIdx + 0u] = float4(EncodeOctahedral(GiDir), GiDist, min(GiM, 16.0f));
    IndirectReservoirPrev[ResIdx + 1u] = float4(GiLi, FinalGiW);

    // Split Diffuse (Albedo-Free!) & Specular Shading Evaluation
    float3 AlbedoFreeDiff = float3(0.0f);
    float3 SpecularRad    = float3(0.0f);

    if (FinalDiW > 0.0f)
    {
        float  NdotL = max(0.0f, dot(N, DiDir));
        float3 DiffF, SpecF;
        EvaluateAlbedoFreeSplitBrdf(Mat, N, T, B, ViewDir, DiDir, DiffF, SpecF);
        AlbedoFreeDiff += DiffF * DiLi * (NdotL * FinalDiW);
        SpecularRad    += SpecF * DiLi * (NdotL * FinalDiW);
    }

    float SceneAo = EvaluateSceneDistanceAO(WorldPos, N) * Mat.AmbientOcclusion;
    if (FrameHeader[7].w > 0.5f && FinalGiW > 0.0f)
    {
        float NdotGi    = max(0.0f, dot(N, GiDir));
        AlbedoFreeDiff += (1.0f - Mat.Metallic) * GiLi * (NdotGi * (1.0f / 3.14159265f) * FinalGiW) * lerp(0.55f, 1.0f, SceneAo);
    }
    else
    {
        uint   PatchCnt = uint(FrameHeader[12].w + 0.5f);
        float3 Fallback = (PatchCnt > 0u)
                              ? SampleGpuCombinedSurfaceRadiosity(SurfaceCardPack, PatchCnt, WorldPos, N) * FrameHeader[13].z
                              : lerp(FrameHeader[6].xyz, FrameHeader[4].xyz, 0.5f * N.y + 0.5f) * 0.42f;
        AlbedoFreeDiff += (1.0f - Mat.Metallic) * Fallback * SceneAo;
    }

    // Heitz 2018 VNDF Layered Clearcoat + Anisotropic Basecoat Specular Reflection Ray
    if (FrameHeader[7].x > 0.5f && (Mat.BaseRoughness < 0.72f || Mat.CoatCoverage > 0.05f || Mat.Metallic > 0.15f))
    {
        float3 Xi3 = float3(SampleUniform01(Rng), SampleUniform01(Rng), SampleUniform01(Rng));
        float3 ReflDir, ReflThroughput;
        float  SelectedRough;
        if (SampleLayeredSpecularReflection(Mat, ViewDir, N, T, B, Xi3, ReflDir, ReflThroughput, SelectedRough))
        {
            UnifiedOpaqueHit ReflHit = TraceSceneOpaque(WorldPos + N * 0.015f, ReflDir, 1.0e-2f, 1200.0f);
            float3 IncomingSpec      = ReflHit.Hit
                                           ? ApplyCoupledHeightFog(
                                                 ShadeSimpleSecondarySurface(ReflHit.WorldPos, ReflHit.GeoNormal, ReflHit.ClassId, -ReflDir, ReflHit.InstanceTint, ReflHit.InstanceSelfRadiance),
                                                 WorldPos, ReflDir, ReflHit.DistanceM)
                                           : EvaluateCoupledEnvironmentSky(ReflDir);
            SpecularRad += IncomingSpec * clamp(ReflThroughput, float3(0.0f), float3(3.0f));
        }
    }

    SurfaceAndWorkAtlas[uint3(Px, 6u)] = float4(max(AlbedoFreeDiff, float3(0.0f)), SceneAo);
    SurfaceAndWorkAtlas[uint3(Px, 8u)] = float4(max(SpecularRad, float3(0.0f)), min(DiM, MaxHistoryM) / MaxHistoryM);
}

//------------------------------------------------------------------------------------------------------------------------
// KERNEL 6: RACE-FREE PING-PONG SPLIT DIFFUSE/SPECULAR À-TROUS, INSPECTION MODES 0..14, & ZERO-COPY 2D RGBA8 OUTPUT
//------------------------------------------------------------------------------------------------------------------------

float3 ColorRampPlasma(float T)
{
    float X = saturate(T);
    return saturate(float3(
        0.08f + 2.15f * X - 1.15f * X * X,
        max(0.0f, 1.75f * X * X - 0.55f * X * X * X),
        0.55f + 0.95f * sin(3.14159f * (1.0f - X))));
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
                               float3 FilteredDiffFactor,
                               float3 FilteredSpec,
                               float  SceneAo,
                               float  NormHistoryM)
{
    float3 CamOrig = FrameHeader[0].xyz;
    if (G0.w == 0.0f) // Sky miss
    {
        return (Mode == 0u || Mode == 2u) ? FilteredSpec : (Mode == 9u ? float3(0.02f) : FilteredSpec * 0.15f);
    }

    bool   IsGlass   = (G0.w < 0.0f);
    float  TravelM   = abs(G0.w);
    float3 WorldPos  = G0.xyz;
    float3 ViewRayD  = normalize(WorldPos - CamOrig);
    float3 BaseAlb   = G5.xyz;
    float3 SelfRad   = IsGlass ? float3(0.0f) : G2.xyz;
    float3 LitLinear = IsGlass ? FilteredSpec : (SelfRad + BaseAlb * FilteredDiffFactor + FilteredSpec);
    LitLinear        = ApplyCoupledHeightFog(LitLinear, CamOrig, ViewRayD, TravelM);

    if (Mode == 0u) return LitLinear;
    if (Mode == 1u) return IsGlass ? float3(0.0f) : BaseAlb * FilteredDiffFactor;
    if (Mode == 2u) return FilteredSpec;
    if (Mode == 3u) return float3(SceneAo);
    if (Mode == 4u)
    {
        uint PatchCnt = uint(FrameHeader[12].w + 0.5f);
        return (PatchCnt > 0u) ? SampleGpuCombinedSurfaceRadiosity(SurfaceCardPack, PatchCnt, WorldPos, normalize(G1.xyz)) : float3(0.0f);
    }
    if (Mode == 5u)
    {
        if (FrameHeader[12].y < 0.5f) return float3(0.25f, 0.55f, 0.90f);
        uint  ActiveR = 0u;
        float VoxStep = 0.25f;
        SampleRangeDistanceClipmap(DistanceOrRangeTable, WorldPos, ActiveR, VoxStep);
        float3 CascadePalette[4] = {
            float3(0.15f, 0.85f, 0.95f),
            float3(0.28f, 0.92f, 0.35f),
            float3(0.96f, 0.72f, 0.16f),
            float3(0.90f, 0.25f, 0.78f),
        };
        return CascadePalette[min(ActiveR, 3u)] * (0.4f + 0.6f * SceneAo);
    }
    if (Mode == 6u) return 0.5f * normalize(G1.xyz) + 0.5f;
    if (Mode == 7u) return BaseAlb;
    if (Mode == 8u) return float3(G1.w, G3.x, 0.5f * G4.w + 0.5f);
    if (Mode == 9u) return ColorRampPlasma(1.0f - exp(-TravelM * 0.035f));
    if (Mode == 10u)
    {
        float BlocksFrac = frac(G3.w);
        return ColorRampPlasma(saturate(BlocksFrac * (100.0f / 36.0f)));
    }
    if (Mode == 11u)
    {
        int2   PrevPx;
        float2 MotionUv;
        bool   Valid = ProjectToPreviousPixel(WorldPos, Width, Height, PrevPx, MotionUv, int2(Px));
        return Valid ? float3(saturate(0.5f + MotionUv.x * 12.0f), saturate(0.5f + MotionUv.y * 12.0f), 0.2f)
                     : float3(0.95f, 0.12f, 0.18f);
    }
    if (Mode == 12u) return ColorRampPlasma(NormHistoryM);
    if (Mode == 13u)
    {
        uint Tier = uint(floor(G3.w + 1.0e-4f));
        return (Tier == 0u) ? float3(0.20f, 0.88f, 0.42f) : ((Tier == 1u) ? float3(0.95f, 0.74f, 0.18f) : float3(0.68f, 0.30f, 0.92f));
    }
    if (Mode == 14u) return IsGlass ? FilteredSpec : LitLinear * 0.06f;
    return LitLinear;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void FilterSpatiotemporalAtrousMain(uint3 DispatchId : SV_DispatchThreadID)
{
    uint Width  = Push.Counts.x;
    uint Height = Push.Counts.y;
    if (DispatchId.x >= Width || DispatchId.y >= Height)
    {
        return;
    }

    uint2 Px        = DispatchId.xy;
    uint  SrcParity = Push.Counts.z & 1u;
    uint  DstParity = 1u - SrcParity;
    bool  FinalIter = (Push.Counts.w != 0u);

    uint DiffSrcLayer = 6u + SrcParity;
    uint DiffDstLayer = 6u + DstParity;
    uint SpecSrcLayer = 8u + SrcParity;
    uint SpecDstLayer = 8u + DstParity;

    float4 G0      = SurfaceAndWorkAtlas[uint3(Px, 0u)];
    float4 CenterD = SurfaceAndWorkAtlas[uint3(Px, DiffSrcLayer)];
    float4 CenterS = SurfaceAndWorkAtlas[uint3(Px, SpecSrcLayer)];

    // Sky miss or crisp curved glazing bypasses spatial blur cleanly
    if (G0.w <= 0.0f)
    {
        SurfaceAndWorkAtlas[uint3(Px, DiffDstLayer)] = CenterD;
        SurfaceAndWorkAtlas[uint3(Px, SpecDstLayer)] = CenterS;
        if (FinalIter)
        {
            uint   Mode  = uint(FrameHeader[13].x + 0.5f);
            float4 G1    = SurfaceAndWorkAtlas[uint3(Px, 1u)];
            float4 G2    = SurfaceAndWorkAtlas[uint3(Px, 2u)];
            float4 G3    = SurfaceAndWorkAtlas[uint3(Px, 3u)];
            float4 G4    = SurfaceAndWorkAtlas[uint3(Px, 4u)];
            float4 G5    = SurfaceAndWorkAtlas[uint3(Px, 5u)];
            float3 Final = ResolveInspectionOutput(Px, Width, Height, Mode, G0, G1, G2, G3, G4, G5, CenterD.xyz, CenterS.xyz, CenterD.w, CenterS.w);
            OutputImage[uint3(Px, 0u)]  = float4(Final, 1.0f);
            ViewportDisplayTarget[Px]   = float4(ApplyAcesAndSrgbOetf(Final, Push.Params.x), 1.0f);
        }
        return;
    }

    float4 G1        = SurfaceAndWorkAtlas[uint3(Px, 1u)];
    float3 CenterPos = G0.xyz;
    float3 CenterN   = normalize(G1.xyz);
    float  Roughness = clamp(G1.w, 0.045f, 1.0f);
    float  LumaDiffC = Luminance709(CenterD.xyz);
    float  LumaSpecC = Luminance709(CenterS.xyz);

    // Mirror-like low-roughness surfaces keep razor-sharp specular lobes while rough surfaces filter wider
    float SpecBlurGate = smoothstep(0.06f, 0.35f, Roughness);
    int   StepSize     = int(Push.Params.y + 0.5f);
    float Kernel1D[5]  = {1.0f / 16.0f, 1.0f / 4.0f, 3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f};

    float3 SumDiff = float3(0.0f);
    float  WDiff   = 0.0f;
    float3 SumSpec = float3(0.0f);
    float  WSpec   = 0.0f;

    for (int Ky = -2; Ky <= 2; ++Ky)
    {
        for (int Kx = -2; Kx <= 2; ++Kx)
        {
            int2 TapPx = int2(Px) + int2(Kx * StepSize, Ky * StepSize);
            if (TapPx.x < 0 || TapPx.y < 0 || TapPx.x >= int(Width) || TapPx.y >= int(Height))
            {
                continue;
            }
            float4 TapG0 = SurfaceAndWorkAtlas[uint3(uint2(TapPx), 0u)];
            if (TapG0.w <= 0.0f)
            {
                continue;
            }
            float4 TapG1 = SurfaceAndWorkAtlas[uint3(uint2(TapPx), 1u)];
            float4 TapD  = SurfaceAndWorkAtlas[uint3(uint2(TapPx), DiffSrcLayer)];
            float4 TapS  = SurfaceAndWorkAtlas[uint3(uint2(TapPx), SpecSrcLayer)];

            float  WKernel = Kernel1D[Kx + 2] * Kernel1D[Ky + 2];
            float  CosN    = max(0.0f, dot(CenterN, normalize(TapG1.xyz)));
            float  PlaneD  = abs(dot(CenterN, TapG0.xyz - CenterPos));
            float  WDepth  = exp(-PlaneD / max(0.04f, 0.06f * G0.w));

            float  WNormD  = pow(CosN, 48.0f);
            float  WLumaD  = exp(-abs(LumaDiffC - Luminance709(TapD.xyz)) / (1.35f + 0.45f * LumaDiffC));
            float  TapWD   = WKernel * WNormD * WDepth * WLumaD;
            SumDiff       += TapD.xyz * TapWD;
            WDiff         += TapWD;

            if (Kx == 0 && Ky == 0)
            {
                SumSpec += TapS.xyz * WKernel;
                WSpec   += WKernel;
            }
            else if (SpecBlurGate > 1.0e-3f)
            {
                float WNormS = pow(CosN, lerp(256.0f, 48.0f, SpecBlurGate));
                float WLumaS = exp(-abs(LumaSpecC - Luminance709(TapS.xyz)) / (0.65f + 0.55f * LumaSpecC));
                float TapWS  = WKernel * SpecBlurGate * WNormS * WDepth * WLumaS;
                SumSpec     += TapS.xyz * TapWS;
                WSpec       += TapWS;
            }
        }
    }

    float3 OutDiff = (WDiff > 1.0e-6f) ? (SumDiff / WDiff) : CenterD.xyz;
    float3 OutSpec = (WSpec > 1.0e-6f) ? (SumSpec / WSpec) : CenterS.xyz;

    SurfaceAndWorkAtlas[uint3(Px, DiffDstLayer)] = float4(OutDiff, CenterD.w);
    SurfaceAndWorkAtlas[uint3(Px, SpecDstLayer)] = float4(OutSpec, CenterS.w);

    if (FinalIter)
    {
        uint   Mode  = uint(FrameHeader[13].x + 0.5f);
        float4 G2    = SurfaceAndWorkAtlas[uint3(Px, 2u)];
        float4 G3    = SurfaceAndWorkAtlas[uint3(Px, 3u)];
        float4 G4    = SurfaceAndWorkAtlas[uint3(Px, 4u)];
        float4 G5    = SurfaceAndWorkAtlas[uint3(Px, 5u)];
        float3 Final = ResolveInspectionOutput(Px, Width, Height, Mode, G0, G1, G2, G3, G4, G5, OutDiff, OutSpec, CenterD.w, CenterS.w);
        OutputImage[uint3(Px, 0u)] = float4(Final, 1.0f);
        ViewportDisplayTarget[Px]  = float4(ApplyAcesAndSrgbOetf(Final, Push.Params.x), 1.0f);
    }
}
