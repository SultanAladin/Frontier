//==========================================================================================================================================
//                                              SPATIOTEMPORAL ILLUMINATION
//==========================================================================================================================================
// 🧩 GTX-first Vulkan 1.3 compute illumination shaders (zero hardware RTX raytracing required):
//    1. TracePrimarySurfaceMain      - Software SAH BoundingVolumeAtlas primary ray + 2-interface curved Snell glass refraction
//    2. SampleDirectReservoirsMain   - Weighted RIS over solar/area/point luminaires + BVH glass-tinted shadow + M-clamped temporal reuse
//    3. ReuseSpatialReservoirsMain   - Jacobian-weighted spatial reservoir reuse + .sdfspace SDF diffuse GI/AO + BVH clearcoat reflections
//    4. FilterSpatiotemporalAtrousMain - Edge-stopping variance-guided à-trous spatial filter & dielectric glass composite

#include "BoundingVolumeSearch.slang"
#include "PlanetaryRecords.slang"
#include "SurfaceScatteringEvaluation.slang"
#include "VolumeDistanceSampling.slang"

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4>                     FrameHeader;
[[vk::binding(1, 0)]] StructuredBuffer<float4>                     BvhCells;
[[vk::binding(2, 0)]] StructuredBuffer<float4>                     BvhTriangles;
[[vk::binding(3, 0)]] StructuredBuffer<float4>                     MaterialTable;
[[vk::binding(4, 0)]] StructuredBuffer<float4>                     DistanceVolumeTable;
[[vk::binding(5, 0)]] RWStructuredBuffer<float4>                   ReservoirCurr;
[[vk::binding(6, 0)]] RWStructuredBuffer<float4>                   ReservoirPrev;
[[vk::binding(7, 0)]] [format("rgba16f")] RWTexture2DArray<float4> SurfaceAtlas;
[[vk::binding(8, 0)]] [format("rgba16f")] RWTexture2DArray<float4> IlluminationWork;
[[vk::binding(9, 0)]] [format("rgba16f")] RWTexture2DArray<float4> OutputImage;

static const uint kLuminaireBaseSlot = 8u;
static const uint kLumSlotStride     = 4u;

uint PcgNext(inout uint RngSeed)
{
    uint Prev = RngSeed * 747796405u + 2891336453u;
    uint Word = ((Prev >> ((Prev >> 28u) + 4u)) ^ Prev) * 277803737u;
    RngSeed   = Prev;
    return (Word >> 22u) ^ Word;
}

float Random01(inout uint RngSeed)
{
    return float(PcgNext(RngSeed) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

float LuminanceRgb(float3 Rgb)
{
    return dot(Rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 EvaluateEnvironmentDome(float3 RayDir)
{
    float3 ZenithRgb  = FrameHeader[4].xyz;
    float3 HorizRgb   = FrameHeader[5].xyz;
    float3 GroundRgb  = FrameHeader[6].xyz;
    uint   LumCount   = uint(FrameHeader[3].w + 0.5f);

    float  Up         = saturate(RayDir.y);
    float3 SkyGrad    = lerp(HorizRgb, ZenithRgb, pow(Up, 0.56f));
    float  HorizonMix = smoothstep(-0.06f, 0.02f, RayDir.y);
    float3 Dome       = lerp(GroundRgb * 0.45f, SkyGrad, HorizonMix);

    if (LumCount > 0u)
    {
        float4 L0 = FrameHeader[kLuminaireBaseSlot + 0u];
        if (uint(L0.w + 0.5f) == 0u)
        {
            float3 SunDir  = SafeNormalize(L0.xyz);
            float3 SunRad  = FrameHeader[kLuminaireBaseSlot + 3u].xyz;
            float  CosS    = dot(RayDir, SunDir);
            float  Corona  = pow(max(0.0f, CosS), 20.0f) * 0.14f + pow(max(0.0f, CosS), 160.0f) * 0.65f;
            float  Disc    = smoothstep(0.99945f, 0.99985f, CosS) * 4.5f;
            Dome          += SunRad * (Corona + Disc) * HorizonMix;
        }
    }
    return Dome;
}

void BuildOrthonormalBasis(float3 Normal, out float3 Tangent, out float3 Bitangent)
{
    float3 Up = (abs(Normal.y) < 0.94f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    Tangent   = normalize(cross(Up, Normal));
    Bitangent = cross(Normal, Tangent);
}

// Evaluates a point sample (SampleU, SampleV) on LumIdx from ReceiverPos: returns LightDir, DistM, and unshadowed Li * G / pdf_source
float3 EvaluateLuminaireSample(uint   LumIdx,
                               float  SampleU,
                               float  SampleV,
                               float3 ReceiverPos,
                               out float3 OutLightDir,
                               out float  OutMaxDistM,
                               out float3 OutSamplePointOnLight,
                               out float3 OutLightNormal,
                               out bool   OutIsAreaOrPoint)
{
    uint   Base = kLuminaireBaseSlot + LumIdx * kLumSlotStride;
    float4 L0   = FrameHeader[Base + 0u];
    float4 L1   = FrameHeader[Base + 1u];
    float4 L2   = FrameHeader[Base + 2u];
    float4 L3   = FrameHeader[Base + 3u];
    uint   Kind = uint(L0.w + 0.5f);

    if (Kind == 0u) // DirectionalSolar disc
    {
        float3 AxisDir = SafeNormalize(L0.xyz);
        float3 TanU, TanV;
        BuildOrthonormalBasis(AxisDir, TanU, TanV);
        float  DiscRad = max(0.001f, L1.x);
        float  R       = DiscRad * sqrt(SampleU);
        float  Phi     = 6.2831853f * SampleV;
        OutLightDir           = SafeNormalize(AxisDir + (TanU * cos(Phi) + TanV * sin(Phi)) * R);
        OutMaxDistM           = 250.0f;
        OutSamplePointOnLight = ReceiverPos + OutLightDir * 250.0f;
        OutLightNormal        = -OutLightDir;
        OutIsAreaOrPoint      = false;
        return L3.xyz;
    }
    else if (Kind == 1u) // RectangularArea softbox luminaire
    {
        float3 HalfU   = L1.xyz;
        float3 HalfV   = L2.xyz;
        float3 CrossUV = cross(HalfU, HalfV);
        float  Area    = 4.0f * length(CrossUV);
        float3 EmitN   = SafeNormalize(CrossUV);
        float3 Pt      = L0.xyz + HalfU * (SampleU * 2.0f - 1.0f) + HalfV * (SampleV * 2.0f - 1.0f);
        float3 ToLight = Pt - ReceiverPos;
        float  DistSq  = max(dot(ToLight, ToLight), 0.04f);
        float  Dist    = sqrt(DistSq);
        OutLightDir           = ToLight / Dist;
        OutMaxDistM           = max(0.01f, Dist - 0.018f);
        OutSamplePointOnLight = Pt;
        OutLightNormal        = EmitN;
        OutIsAreaOrPoint      = true;
        // Two-sided architectural softbox emission with smooth cosine falloff
        float CosEmit = max(0.15f, abs(dot(EmitN, -OutLightDir)));
        return L3.xyz * (Area * CosEmit / DistSq);
    }
    else // OmniPoint
    {
        float3 Pt      = L0.xyz;
        float3 ToLight = Pt - ReceiverPos;
        float  DistSq  = max(dot(ToLight, ToLight), 0.05f);
        float  Dist    = sqrt(DistSq);
        OutLightDir           = ToLight / Dist;
        OutMaxDistM           = max(0.01f, Dist - 0.015f);
        OutSamplePointOnLight = Pt;
        OutLightNormal        = -OutLightDir;
        OutIsAreaOrPoint      = true;
        return L3.xyz / DistSq;
    }
}

float3 GenerateCameraRay(uint2 PixelCoord)
{
    float  TanHalfV    = FrameHeader[0].w;
    float3 CamRight    = FrameHeader[1].xyz;
    float  TanHalfH    = FrameHeader[1].w;
    float3 CamUp       = FrameHeader[2].xyz;
    float3 CamFwd      = FrameHeader[3].xyz;
    float2 PixelCenter = float2(PixelCoord) + 0.5f;
    float  NdcX        = (PixelCenter.x / float(Push.Counts.x)) * 2.0f - 1.0f;
    float  NdcY        = 1.0f - (PixelCenter.y / float(Push.Counts.y)) * 2.0f;
    return SafeNormalize(CamFwd + CamRight * (NdcX * TanHalfH) + CamUp * (NdcY * TanHalfV));
}

//==========================================================================================================================================
// 1. PRIMARY VISIBILITY + TWO-INTERFACE CURVED SNELL GLASS REFRACTION
//==========================================================================================================================================
[shader("compute")]
[numthreads(8, 8, 1)]
void TracePrimarySurfaceMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    float3 CamOrigin = FrameHeader[0].xyz;
    uint   CellCount = uint(FrameHeader[2].w + 0.5f);
    float3 RayDir    = GenerateCameraRay(Tid.xy);

    GpuSurfaceHit   OpaqueHit = SearchBoundingVolumeOpaque(BvhCells, BvhTriangles, CellCount, CamOrigin, RayDir, 0.03f, 500.0f);
    float           MaxGlassT = OpaqueHit.Hit ? OpaqueHit.DistanceM : 500.0f;
    GpuGlassPeelHit GlassHit  = SearchBoundingVolumeGlassPeel(BvhCells, BvhTriangles, CellCount, CamOrigin, RayDir, 0.03f, MaxGlassT);

    float3 GlassThroughput = float3(1.0f);
    float  GlassReflWeight = 0.0f;
    float3 GlassReflRad    = float3(0.0f);

    if (GlassHit.HitGlass)
    {
        float  CosI     = saturate(dot(-RayDir, GlassHit.FrontNormal));
        float  F0       = pow((GlassHit.Ior - 1.0f) / (GlassHit.Ior + 1.0f), 2.0f);
        GlassReflWeight = F0 + (1.0f - F0) * pow(1.0f - CosI, 5.0f);
        GlassThroughput = exp(-GlassHit.AbsorptionPerM * (GlassHit.ThicknessM * 4.5f));

        // Trace reflection ray off curved automotive glazing (catches sky dome + glowing overhead softbox bars!)
        float3 GlassEntryPos = CamOrigin + RayDir * GlassHit.FrontT;
        float3 ReflDir       = reflect(RayDir, GlassHit.FrontNormal);
        GpuSurfaceHit ReflHit = SearchBoundingVolumeOpaque(
            BvhCells, BvhTriangles, CellCount, GlassEntryPos + GlassHit.FrontNormal * 0.015f, ReflDir, 0.01f, 120.0f);
        if (ReflHit.Hit)
        {
            UnpackedSurfaceMaterial ReflMat = FetchSurfaceMaterial(MaterialTable, ReflHit.ClassId);
            GlassReflRad = ReflMat.EmissiveRadiance + ReflMat.BaseAlbedo * EvaluateEnvironmentDome(ReflHit.GeoNormal) * 0.65f;
        }
        else
        {
            GlassReflRad = EvaluateEnvironmentDome(ReflDir);
        }

        // Two-interface curved dielectric pane Snell displacement & angular deflection:
        // An thin curved shell of thickness d and curvature bends & laterally offsets the transmitted ray!
        float3 RefractInside = refract(RayDir, GlassHit.FrontNormal, 1.0f / GlassHit.Ior);
        if (length(RefractInside) > 0.1f)
        {
            float3 ExitOrigin = GlassEntryPos + RefractInside * GlassHit.ThicknessM;
            float3 BentDir    = SafeNormalize(lerp(RayDir, RefractInside, 0.28f));
            OpaqueHit         = SearchBoundingVolumeOpaque(BvhCells, BvhTriangles, CellCount, ExitOrigin, BentDir, 0.005f, 500.0f);
        }
    }

    if (!OpaqueHit.Hit)
    {
        float3 SkyOut = EvaluateEnvironmentDome(RayDir) * GlassThroughput * (1.0f - GlassReflWeight) +
                        GlassReflRad * GlassReflWeight;
        SurfaceAtlas[uint3(Tid.xy, 0u)] = float4(CamOrigin + RayDir * 200.0f, -1.0f);
        SurfaceAtlas[uint3(Tid.xy, 1u)] = float4(0.0f, 1.0f, 0.0f, 0.0f);
        SurfaceAtlas[uint3(Tid.xy, 2u)] = float4(GlassThroughput, GlassReflWeight);
        SurfaceAtlas[uint3(Tid.xy, 3u)] = float4(SkyOut, 0.0f);
        return;
    }

    SurfaceAtlas[uint3(Tid.xy, 0u)] = float4(OpaqueHit.WorldPos, OpaqueHit.DistanceM);
    SurfaceAtlas[uint3(Tid.xy, 1u)] = float4(OpaqueHit.GeoNormal, float(OpaqueHit.ClassId));
    SurfaceAtlas[uint3(Tid.xy, 2u)] = float4(GlassThroughput, GlassReflWeight);
    SurfaceAtlas[uint3(Tid.xy, 3u)] = float4(GlassReflRad, OpaqueHit.TexCoord.x);
}

//==========================================================================================================================================
// 2. WEIGHTED RIS + SOFTWARE BVH SHADOW VISIBILITY + M-CLAMPED TEMPORAL RESERVOIR REUSE
//==========================================================================================================================================
[shader("compute")]
[numthreads(8, 8, 1)]
void SampleDirectReservoirsMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    uint   PixelLinear = Tid.y * Push.Counts.x + Tid.x;
    uint   ResSlot     = PixelLinear * 2u;
    float4 Surf0       = SurfaceAtlas[uint3(Tid.xy, 0u)];
    if (Surf0.w < 0.0f)
    {
        ReservoirCurr[ResSlot + 0u] = float4(0.0f);
        ReservoirCurr[ResSlot + 1u] = float4(0.0f);
        return;
    }

    float3 CamOrigin = FrameHeader[0].xyz;
    uint   CellCount = uint(FrameHeader[2].w + 0.5f);
    uint   LumCount  = uint(FrameHeader[3].w + 0.5f);
    uint   MInit     = uint(FrameHeader[4].w + 0.5f);

    float3 WorldPos  = Surf0.xyz;
    float4 Surf1     = SurfaceAtlas[uint3(Tid.xy, 1u)];
    float3 Normal    = SafeNormalize(Surf1.xyz);
    uint   ClassId   = uint(Surf1.w + 0.5f);
    float3 ViewToEye = SafeNormalize(CamOrigin - WorldPos);

    UnpackedSurfaceMaterial Mat = FetchSurfaceMaterial(MaterialTable, ClassId);
    if (LumCount == 0u)
    {
        ReservoirCurr[ResSlot + 0u] = float4(0.0f);
        ReservoirCurr[ResSlot + 1u] = float4(0.0f);
        return;
    }

    uint Rng = (Tid.x * 1973u) ^ (Tid.y * 9277u) ^ (Push.Counts.z * 26699u) ^ 0x9E3779B9u;

    uint  WinLum   = 0u;
    float WinU     = 0.5f;
    float WinV     = 0.5f;
    float WinPHat  = 0.0f;
    float WeightSm = 0.0f;
    float CountM   = 0.0f;

    float InvUniformLumPdf = float(LumCount);

    for (uint Cand = 0u; Cand < MInit; ++Cand)
    {
        uint  CandLum = min(uint(Random01(Rng) * float(LumCount)), LumCount - 1u);
        float CandU   = Random01(Rng);
        float CandV   = Random01(Rng);

        float3 LightDir, SamplePt, LightN;
        float  MaxDistM;
        bool   IsArea;
        float3 UnshadowedLi = EvaluateLuminaireSample(CandLum, CandU, CandV, WorldPos, LightDir, MaxDistM, SamplePt, LightN, IsArea);
        float  NdotL        = max(0.0f, dot(Normal, LightDir));
        float3 Brdf         = (NdotL > 0.0f) ? EvaluateSurfaceScatteringBrdf(Mat, WorldPos, Normal, ViewToEye, LightDir) : float3(0.0f);
        float  PHat         = LuminanceRgb(Brdf * UnshadowedLi * NdotL);
        float  RisWeight    = PHat * InvUniformLumPdf;

        CountM   += 1.0f;
        WeightSm += RisWeight;
        if (WeightSm > 0.0f && Random01(Rng) < (RisWeight / WeightSm))
        {
            WinLum  = CandLum;
            WinU    = CandU;
            WinV    = CandV;
            WinPHat = PHat;
        }
    }

    float WinW = (WinPHat > 1.0e-7f && CountM > 0.0f) ? (WeightSm / (CountM * WinPHat)) : 0.0f;

    // Verify visibility of winning initial RIS sample via software SAH BoundingVolumeAtlas (including glass tint!)
    if (WinW > 0.0f)
    {
        float3 LightDir, SamplePt, LightN;
        float  MaxDistM;
        bool   IsArea;
        (void)EvaluateLuminaireSample(WinLum, WinU, WinV, WorldPos, LightDir, MaxDistM, SamplePt, LightN, IsArea);
        float3 ShadowTr = SearchBoundingVolumeShadowTransmittance(
            BvhCells, BvhTriangles, CellCount, WorldPos + Normal * 0.012f, LightDir, 0.004f, MaxDistM);
        if (LuminanceRgb(ShadowTr) <= 1.0e-5f)
        {
            WinW     = 0.0f;
            WeightSm = 0.0f;
        }
    }

    // M-clamped temporal reservoir reuse from previous frame at matching pixel
    if (Push.Tuning.w > 0.5f)
    {
        float4 Prev0 = ReservoirPrev[ResSlot + 0u];
        float4 Prev1 = ReservoirPrev[ResSlot + 1u];
        float  PrevM = min(Prev1.y, 16.0f);
        if (PrevM > 0.0f && Prev0.w > 0.0f)
        {
            uint   PLum = min(uint(Prev0.x + 0.5f), LumCount - 1u);
            float3 LightDir, SamplePt, LightN;
            float  MaxDistM;
            bool   IsArea;
            float3 Li    = EvaluateLuminaireSample(PLum, Prev0.y, Prev0.z, WorldPos, LightDir, MaxDistM, SamplePt, LightN, IsArea);
            float  NdotL = max(0.0f, dot(Normal, LightDir));
            float3 Brdf  = (NdotL > 0.0f) ? EvaluateSurfaceScatteringBrdf(Mat, WorldPos, Normal, ViewToEye, LightDir) : float3(0.0f);
            float3 VisTr = (NdotL > 0.0f)
                               ? SearchBoundingVolumeShadowTransmittance(
                                     BvhCells, BvhTriangles, CellCount, WorldPos + Normal * 0.012f, LightDir, 0.004f, MaxDistM)
                               : float3(0.0f);
            float  TargetHere = LuminanceRgb(Brdf * Li * VisTr * NdotL);
            float  MetaWeight = TargetHere * Prev0.w * PrevM;

            float CombinedSum = WinPHat * WinW * CountM + MetaWeight;
            CountM += PrevM;
            if (CombinedSum > 0.0f && Random01(Rng) < (MetaWeight / CombinedSum))
            {
                WinLum  = PLum;
                WinU    = Prev0.y;
                WinV    = Prev0.z;
                WinPHat = TargetHere;
            }
            WinW = (WinPHat > 1.0e-7f && CountM > 0.0f) ? (CombinedSum / (CountM * WinPHat)) : 0.0f;
        }
    }

    ReservoirCurr[ResSlot + 0u] = float4(float(WinLum), WinU, WinV, min(WinW, 24.0f));
    ReservoirCurr[ResSlot + 1u] = float4(WinPHat * WinW * CountM, CountM, WinPHat, 0.0f);
}

//==========================================================================================================================================
// 3. JACOBIAN SPATIAL RESERVOIR REUSE + .SDFSPACE DIFFUSE GI/AO + SOFTWARE BVH REFLECTIONS
//==========================================================================================================================================
[shader("compute")]
[numthreads(8, 8, 1)]
void ReuseSpatialReservoirsMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    float4 Surf0 = SurfaceAtlas[uint3(Tid.xy, 0u)];
    if (Surf0.w < 0.0f)
    {
        IlluminationWork[uint3(Tid.xy, 0u)] = SurfaceAtlas[uint3(Tid.xy, 3u)];
        return;
    }

    float3 CamOrigin       = FrameHeader[0].xyz;
    uint   CellCount       = uint(FrameHeader[2].w + 0.5f);
    uint   LumCount        = uint(FrameHeader[3].w + 0.5f);
    uint   SpatialCount    = uint(FrameHeader[5].w + 0.5f);
    uint   SdfRayCount     = uint(FrameHeader[6].w + 0.5f);
    bool   DoReflections   = (FrameHeader[7].x > 0.5f);
    bool   DoSpatialReuse  = (FrameHeader[7].y > 0.5f);
    bool   HasSdfVolume    = (FrameHeader[7].z > 0.5f);

    float3 WorldPos  = Surf0.xyz;
    float4 Surf1     = SurfaceAtlas[uint3(Tid.xy, 1u)];
    float3 Normal    = SafeNormalize(Surf1.xyz);
    uint   ClassId   = uint(Surf1.w + 0.5f);
    float3 ViewToEye = SafeNormalize(CamOrigin - WorldPos);

    UnpackedSurfaceMaterial Mat = FetchSurfaceMaterial(MaterialTable, ClassId);

    uint   CenterLinear = Tid.y * Push.Counts.x + Tid.x;
    uint   CenterSlot   = CenterLinear * 2u;
    float4 Res0         = ReservoirCurr[CenterSlot + 0u];
    float4 Res1         = ReservoirCurr[CenterSlot + 1u];

    uint  WinLum  = uint(Res0.x + 0.5f);
    float WinU    = Res0.y;
    float WinV    = Res0.z;
    float WinW    = Res0.w;
    float WinPHat = Res1.z;
    float CountM  = Res1.y;
    float WSum    = WinPHat * WinW * CountM;

    uint Rng = (Tid.x * 6151u) ^ (Tid.y * 3079u) ^ (Push.Counts.z * 12289u) ^ 0x85EBCA6Bu;

    // Spatial reservoir neighbor reuse with receiver-shift geometry Jacobian determinant ratio
    if (DoSpatialReuse && LumCount > 0u && SpatialCount > 0u)
    {
        for (uint NIdx = 0u; NIdx < SpatialCount; ++NIdx)
        {
            float Angle = 6.2831853f * Random01(Rng);
            float Rad   = Push.Tuning.z * sqrt(Random01(Rng));
            int2  NCoord = int2(Tid.xy) + int2(round(float2(cos(Angle), sin(Angle)) * Rad));
            if (NCoord.x < 0 || NCoord.y < 0 || NCoord.x >= int(Push.Counts.x) || NCoord.y >= int(Push.Counts.y))
            {
                continue;
            }
            float4 NSurf0 = SurfaceAtlas[uint3(uint2(NCoord), 0u)];
            float4 NSurf1 = SurfaceAtlas[uint3(uint2(NCoord), 1u)];
            if (NSurf0.w < 0.0f)
            {
                continue;
            }
            float3 NNorm  = SafeNormalize(NSurf1.xyz);
            float  PlaneD = abs(dot(Normal, NSurf0.xyz - WorldPos));
            if (dot(Normal, NNorm) < 0.88f || PlaneD > 0.12f * max(1.0f, Surf0.w * 0.25f))
            {
                continue;
            }

            uint   NSlot = (uint(NCoord.y) * Push.Counts.x + uint(NCoord.x)) * 2u;
            float4 NR0   = ReservoirCurr[NSlot + 0u];
            float4 NR1   = ReservoirCurr[NSlot + 1u];
            float  NM    = min(NR1.y, 12.0f);
            if (NM <= 0.0f || NR0.w <= 0.0f)
            {
                continue;
            }

            uint   NLum = min(uint(NR0.x + 0.5f), LumCount - 1u);
            float3 LightDirP, SamplePt, LightN;
            float  MaxDistP;
            bool   IsArea;
            float3 LiP = EvaluateLuminaireSample(NLum, NR0.y, NR0.z, WorldPos, LightDirP, MaxDistP, SamplePt, LightN, IsArea);

            float Jacobian = 1.0f;
            if (IsArea)
            {
                float3 VecQ  = SamplePt - NSurf0.xyz;
                float3 VecP  = SamplePt - WorldPos;
                float  DistQ = max(length(VecQ), 0.08f);
                float  DistP = max(length(VecP), 0.08f);
                float  CosQ  = max(0.05f, abs(dot(LightN, -VecQ / DistQ)));
                float  CosP  = max(0.05f, abs(dot(LightN, -VecP / DistP)));
                Jacobian     = clamp((CosP * DistQ * DistQ) / max(CosQ * DistP * DistP, 1.0e-4f), 0.1f, 6.0f);
            }

            float  NdotLP = max(0.0f, dot(Normal, LightDirP));
            float3 BrdfP  = (NdotLP > 0.0f) ? EvaluateSurfaceScatteringBrdf(Mat, WorldPos, Normal, ViewToEye, LightDirP) : float3(0.0f);
            float3 VisP   = (NdotLP > 0.0f)
                                ? SearchBoundingVolumeShadowTransmittance(
                                      BvhCells, BvhTriangles, CellCount, WorldPos + Normal * 0.012f, LightDirP, 0.004f, MaxDistP)
                                : float3(0.0f);
            float TargetP = LuminanceRgb(BrdfP * LiP * VisP * NdotLP);
            float MetaW   = TargetP * Jacobian * NR0.w * NM;

            WSum   += MetaW;
            CountM += NM;
            if (WSum > 0.0f && Random01(Rng) < (MetaW / WSum))
            {
                WinLum  = NLum;
                WinU    = NR0.y;
                WinV    = NR0.z;
                WinPHat = TargetP;
            }
            WinW = (WinPHat > 1.0e-7f && CountM > 0.0f) ? min(WSum / (CountM * WinPHat), 24.0f) : 0.0f;
        }
    }

    // Evaluate finalized ReSTIR direct radiance
    float3 DirectRadiance = Mat.EmissiveRadiance;
    if (LumCount > 0u && WinW > 0.0f)
    {
        uint   SLum = min(WinLum, LumCount - 1u);
        float3 LightDir, SamplePt, LightN;
        float  MaxDistM;
        bool   IsArea;
        float3 Li      = EvaluateLuminaireSample(SLum, WinU, WinV, WorldPos, LightDir, MaxDistM, SamplePt, LightN, IsArea);
        float  NdotL   = max(0.0f, dot(Normal, LightDir));
        float3 Brdf    = (NdotL > 0.0f) ? EvaluateSurfaceScatteringBrdf(Mat, WorldPos, Normal, ViewToEye, LightDir) : float3(0.0f);
        float3 VisTr   = (NdotL > 0.0f)
                             ? SearchBoundingVolumeShadowTransmittance(
                                   BvhCells, BvhTriangles, CellCount, WorldPos + Normal * 0.012f, LightDir, 0.004f, MaxDistM)
                             : float3(0.0f);
        DirectRadiance += Brdf * Li * VisTr * (NdotL * WinW);
    }

    // Evaluate headless-baked .sdfspace Signed Distance Field Diffuse GI + 5-tap Ambient Occlusion
    float  SdfAo            = HasSdfVolume ? EvaluateGpuVolumeAmbientOcclusion(DistanceVolumeTable, WorldPos, Normal, 0.95f) : 1.0f;
    float3 IndirectDiffuse  = float3(0.0f);
    float3 GroundBounceTint = FrameHeader[6].xyz;
    if (SdfRayCount > 0u)
    {
        float3 TanU, TanV;
        BuildOrthonormalBasis(Normal, TanU, TanV);
        for (uint RayIdx = 0u; RayIdx < SdfRayCount; ++RayIdx)
        {
            float U1  = frac((float(RayIdx) + Random01(Rng)) / float(SdfRayCount));
            float U2  = Random01(Rng);
            float R   = sqrt(U1);
            float Phi = 6.2831853f * U2;
            float3 HemiDir = SafeNormalize(TanU * (R * cos(Phi)) + TanV * (R * sin(Phi)) + Normal * sqrt(max(0.0f, 1.0f - U1)));

            float HitDistM;
            if (HasSdfVolume && TraceGpuVolumeRay(DistanceVolumeTable, WorldPos + Normal * 0.04f, HemiDir, 7.5f, 16, HitDistM))
            {
                float3 BouncePos = WorldPos + HemiDir * HitDistM;
                float3 BounceN   = SampleGpuVolumeNormal(DistanceVolumeTable, BouncePos);
                IndirectDiffuse += GroundBounceTint * EvaluateEnvironmentDome(BounceN) * 0.55f;
            }
            else
            {
                IndirectDiffuse += EvaluateEnvironmentDome(HemiDir);
            }
        }
        IndirectDiffuse /= float(SdfRayCount);
    }
    else
    {
        IndirectDiffuse = EvaluateEnvironmentDome(Normal);
    }

    float3 DiffuseShare = Mat.BaseAlbedo * (1.0f - Mat.Metallic) * IndirectDiffuse * SdfAo * 0.72f;

    // Software SAH BoundingVolumeAtlas Specular / Clearcoat Reflections (zero RTX cores required!)
    float3 SpecularBounce = float3(0.0f);
    float  EffRoughness   = (Mat.CoatCoverage > 0.1f) ? min(Mat.BaseRoughness, Mat.CoatRoughness * 1.5f) : Mat.BaseRoughness;
    if (DoReflections && (EffRoughness < 0.62f || Mat.Metallic > 0.25f || Mat.CoatCoverage > 0.2f))
    {
        float3 TanU, TanV;
        BuildOrthonormalBasis(Normal, TanU, TanV);
        float  JitterScale = EffRoughness * EffRoughness * 0.35f;
        float3 PerturbedN  = SafeNormalize(Normal + (TanU * (Random01(Rng) - 0.5f) + TanV * (Random01(Rng) - 0.5f)) * JitterScale);
        float3 ReflDir     = reflect(-ViewToEye, PerturbedN);
        if (dot(ReflDir, Normal) > 0.01f)
        {
            GpuSurfaceHit ReflHit = SearchBoundingVolumeOpaque(
                BvhCells, BvhTriangles, CellCount, WorldPos + Normal * 0.016f, ReflDir, 0.01f, 90.0f);
            float3 IncomingRefl;
            if (ReflHit.Hit)
            {
                UnpackedSurfaceMaterial RMat = FetchSurfaceMaterial(MaterialTable, ReflHit.ClassId);
                IncomingRefl = RMat.EmissiveRadiance + RMat.BaseAlbedo * EvaluateEnvironmentDome(ReflHit.GeoNormal) * 0.75f;
                if (LumCount > 0u)
                {
                    float3 LDir, SPt, LN;
                    float  MDist;
                    bool   IsA;
                    float3 SunLi = EvaluateLuminaireSample(0u, 0.5f, 0.5f, ReflHit.WorldPos, LDir, MDist, SPt, LN, IsA);
                    float  RNL   = max(0.0f, dot(ReflHit.GeoNormal, LDir));
                    if (RNL > 0.0f)
                    {
                        float3 RVis = SearchBoundingVolumeShadowTransmittance(
                            BvhCells, BvhTriangles, CellCount, ReflHit.WorldPos + ReflHit.GeoNormal * 0.015f, LDir, 0.005f, MDist);
                        IncomingRefl += RMat.BaseAlbedo * (1.0f / 3.14159265f) * SunLi * RVis * RNL;
                    }
                }
            }
            else
            {
                IncomingRefl = EvaluateEnvironmentDome(ReflDir);
            }

            float  NdotV   = saturate(dot(Normal, ViewToEye));
            float  Refl0   = pow((Mat.BaseIor - 1.0f) / (Mat.BaseIor + 1.0f), 2.0f);
            float3 F0      = lerp(float3(Refl0), Mat.BaseAlbedo, Mat.Metallic);
            float3 Fresnel = F0 + (1.0f - F0) * pow(1.0f - NdotV, 5.0f);
            float  CoatF   = Mat.CoatCoverage * (0.04f + 0.96f * pow(1.0f - NdotV, 5.0f));
            float3 WeightF = max(Fresnel * (1.0f - 0.65f * Mat.BaseRoughness), float3(CoatF));
            SpecularBounce = IncomingRefl * WeightF * lerp(0.55f, 1.0f, SdfAo);
        }
    }

    float3 CombinedSurface = DirectRadiance + DiffuseShare + SpecularBounce;
    IlluminationWork[uint3(Tid.xy, 0u)] = float4(max(CombinedSurface, float3(0.0f)), 1.0f);
}

//==========================================================================================================================================
// 4. EDGE-STOPPING À-TROUS VARIANCE RECONSTRUCTION & CURVED GLASS COMPOSITE
//==========================================================================================================================================
[shader("compute")]
[numthreads(8, 8, 1)]
void FilterSpatiotemporalAtrousMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    uint   SrcLayer  = Push.Counts.z & 1u;
    uint   DstLayer  = 1u - SrcLayer;
    bool   FinalStep = (Push.Counts.w != 0u);
    int    StepWidth = max(1, int(Push.Tuning.y + 0.5f));

    float4 CenterSurf0 = SurfaceAtlas[uint3(Tid.xy, 0u)];
    if (CenterSurf0.w < 0.0f)
    {
        float4 SkyVal = IlluminationWork[uint3(Tid.xy, SrcLayer)];
        if (FinalStep)
        {
            OutputImage[uint3(Tid.xy, 0u)] = SkyVal;
        }
        else
        {
            IlluminationWork[uint3(Tid.xy, DstLayer)] = SkyVal;
        }
        return;
    }

    float4 CenterSurf1 = SurfaceAtlas[uint3(Tid.xy, 1u)];
    float3 CenterPos   = CenterSurf0.xyz;
    float3 CenterNorm  = SafeNormalize(CenterSurf1.xyz);
    uint   CenterClass = uint(CenterSurf1.w + 0.5f);
    float3 CenterRad   = IlluminationWork[uint3(Tid.xy, SrcLayer)].rgb;
    float  CenterLum   = LuminanceRgb(CenterRad);

    static const float Kernel1D[5] = {0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f};

    float3 FilteredSum = float3(0.0f);
    float  WeightSum   = 0.0f;

    for (int Ky = -2; Ky <= 2; ++Ky)
    {
        for (int Kx = -2; Kx <= 2; ++Kx)
        {
            int2 SampleCoord = int2(Tid.xy) + int2(Kx, Ky) * StepWidth;
            if (SampleCoord.x < 0 || SampleCoord.y < 0 ||
                SampleCoord.x >= int(Push.Counts.x) || SampleCoord.y >= int(Push.Counts.y))
            {
                continue;
            }
            float4 NSurf0 = SurfaceAtlas[uint3(uint2(SampleCoord), 0u)];
            if (NSurf0.w < 0.0f)
            {
                continue;
            }
            float4 NSurf1 = SurfaceAtlas[uint3(uint2(SampleCoord), 1u)];
            uint   NClass = uint(NSurf1.w + 0.5f);
            if (NClass != CenterClass)
            {
                continue;
            }
            float3 NNorm  = SafeNormalize(NSurf1.xyz);
            float  NormW  = pow(max(0.0f, dot(CenterNorm, NNorm)), 32.0f);
            float  PlaneD = abs(dot(CenterNorm, NSurf0.xyz - CenterPos));
            float  DepthW = exp(-PlaneD * 22.0f);

            float3 NRad   = IlluminationWork[uint3(uint2(SampleCoord), SrcLayer)].rgb;
            float  LumW   = exp(-abs(LuminanceRgb(NRad) - CenterLum) / max(0.35f + 0.65f * CenterLum, 0.1f));
            float  W      = Kernel1D[Kx + 2] * Kernel1D[Ky + 2] * NormW * DepthW * LumW;

            FilteredSum += NRad * W;
            WeightSum   += W;
        }
    }

    float3 Filtered = (WeightSum > 1.0e-5f) ? (FilteredSum / WeightSum) : CenterRad;

    if (!FinalStep)
    {
        IlluminationWork[uint3(Tid.xy, DstLayer)] = float4(Filtered, 1.0f);
        return;
    }

    // Final composite: apply Beer-Lambert curved glass transmittance + Fresnel glazing reflection
    float4 GlassTrAndW = SurfaceAtlas[uint3(Tid.xy, 2u)];
    float3 GlassRefl   = SurfaceAtlas[uint3(Tid.xy, 3u)].rgb;
    float3 ResolvedHdr = Filtered * GlassTrAndW.xyz * (1.0f - GlassTrAndW.w) + GlassRefl * GlassTrAndW.w;
    OutputImage[uint3(Tid.xy, 0u)] = float4(max(ResolvedHdr, float3(0.0f)), 1.0f);
}
