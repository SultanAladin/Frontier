/*==========================================================================================================================================
                                         SPATIOTEMPORAL ILLUMINATION (SLANG)
                                     ------------------------------------------------------------------
             Phase 3C GTX-First Vulkan Compute Illumination Pipeline (6 compact low-VGPR kernels):
               1. TracePrimarySurfaceMain            — Two-level TLAS + 8-wide CWBVH primary ray, curved
                  2-interface dielectric Snell/TIR peel, 3D motion vectors, PBR textures & Voronoi flakes.
               2. SampleDirectReservoirsMain         — Solar + analytical + flux-weighted Emissive Facet
                  CDF RIS, CWBVH Beer-Lambert shadow ray, & motion-reprojected temporal DI reuse.
               3. SampleIndirectReservoirsMain       — True ReSTIR GI over 4-cascade clipmap SDF + CWBVH,
                  shaded by GPU Surface Card multi-bounce radiosity, with motion temporal GI reuse.
               4. ReuseSpatialReservoirsMain         — Spatial DI + Jacobian-determinant GI reuse, Heitz
                  VNDF base/coat mixture raytraced reflections, & split diffuse/specular demodulation.
               5. FilterSpatiotemporalAtrousMain     — Split diffuse irradiance & roughness/hit-distance
                  guided specular variance à-trous wavelet reconstruction.
               6. ResolveSpatiotemporalDisplayMain   — Albedo remodulation, curved glass composite,
                  Atmosphere LUT + lunar corona haze + volumetric clouds + height/ForceField dust fog +
                  optical lens flare, all 15 RenderInspectionModes, ACES tonemap, & dithered RGBA8 output.
==========================================================================================================================================*/
// 🧩 Complete Phase 3C GTX-first ReSTIR DI+GI, 8-wide CWBVH, 4-cascade SDF, SurfaceCards, VNDF, & split denoiser.

#include "VolumeDistanceSampling.slang"
#include "BoundingVolumeSearch.slang"
#include "SurfaceScatteringEvaluation.slang"
#include "SurfaceCardRadiosity.slang"

// 16-byte push constant block (100% compliant with Vulkan 128-byte minimum push constant guarantee!)
struct IlluminationDispatchPush
{
    uint Width;
    uint Height;
    uint FrameIndex;
    uint AtrousStepWidth;
};

struct DecodedFrameUniforms
{
    uint   Width;
    uint   Height;
    uint   FrameIndex;
    uint   AtrousStepWidth;

    float3 CameraOrigin;
    float  TanHalfVerticalFov;
    float3 CameraRight;
    float  AspectRatio;
    float3 CameraUp;
    uint   TlasCellCount;
    float3 CameraForward;
    uint   LuminaireCount;

    float3 PrevCameraOrigin;
    uint   EmissiveFacetCount;
    float3 PrevCameraRight;
    uint   SurfaceCardCount;
    float3 PrevCameraUp;
    uint   CandidateLightSamples;
    float3 PrevCameraForward;
    uint   SpatialNeighborCount;

    float3 SunDirection;
    float  SunIrradianceLux;
    float3 SunRadianceTint;
    float  SkyAmbientGain;
    float3 MoonDirection;
    float  MoonCoronaStrength;

    float  NearMaterialMeters;
    float  FarMaterialMeters;
    float  MaterialQualityScale;
    float  TemporalHistoryCap;

    float  CloudCoverage;
    float  CloudAltitudeMeters;
    float  FogBaseDensity;
    float  FogDustBoost;

    float  ExposureScale;
    float  FlareIntensity;
    uint   InspectionMode;
    uint   FeatureFlags; // bit0=GI, bit1=Spatial, bit2=Reflections, bit3=AtmosphereLut, bit4=Clouds, bit5=Fog, bit6=Flare, bit7=SecondBounce
};

[[vk::push_constant]]
ConstantBuffer<IlluminationDispatchPush> gDispatch;

[[vk::binding(0, 0)]]  StructuredBuffer<float4>                     gTlasCells;
[[vk::binding(1, 0)]]  StructuredBuffer<float4>                     gInstances;
[[vk::binding(2, 0)]]  StructuredBuffer<float4>                     gWideCells;
[[vk::binding(3, 0)]]  StructuredBuffer<float4>                     gWideTriangles;
[[vk::binding(4, 0)]]  StructuredBuffer<float4>                     gMaterialTable;
[[vk::binding(5, 0)]]  StructuredBuffer<float4>                     gDistanceVolume;
[[vk::binding(6, 0)]]  StructuredBuffer<float4>                     gLuminaires;
[[vk::binding(7, 0)]]  StructuredBuffer<float4>                     gEmissiveFacets;
[[vk::binding(8, 0)]]  StructuredBuffer<float4>                     gCardDescriptors;
[[vk::binding(9, 0)]]  StructuredBuffer<float4>                     gCardExitance;
[[vk::binding(10, 0)]] Sampler2DArray<float4>                       gAlbedoSrgbArray;
[[vk::binding(11, 0)]] Sampler2DArray<float4>                       gNormalUnormArray;
[[vk::binding(12, 0)]] Sampler2DArray<float4>                       gOrmUnormArray;
[[vk::binding(13, 0)]] Sampler2DArray<float4>                       gAtmosphereSkyLut;
[[vk::binding(14, 0)]] [format("rgba16f")] RWTexture2DArray<float4> gGBufferCurr;
[[vk::binding(15, 0)]] [format("rgba16f")] RWTexture2DArray<float4> gGBufferPrev;
[[vk::binding(16, 0)]] [format("rgba16f")] RWTexture2DArray<float4> gReservoirRead;
[[vk::binding(17, 0)]] [format("rgba16f")] RWTexture2DArray<float4> gReservoirWrite;
[[vk::binding(18, 0)]] [format("rgba16f")] RWTexture2DArray<float4> gRadianceRead;
[[vk::binding(19, 0)]] [format("rgba16f")] RWTexture2DArray<float4> gRadianceWrite;
[[vk::binding(20, 0)]] [format("rgba8")]   RWTexture2DArray<float4> gDisplayRgba8;
[[vk::binding(21, 0)]] StructuredBuffer<float4>                     gFrameUniformBuffer;

DecodedFrameUniforms LoadFrameUniforms()
{
    DecodedFrameUniforms U;
    U.Width           = gDispatch.Width;
    U.Height          = gDispatch.Height;
    U.FrameIndex      = gDispatch.FrameIndex;
    U.AtrousStepWidth = gDispatch.AtrousStepWidth;

    float4 F0  = gFrameUniformBuffer[0u];
    float4 F1  = gFrameUniformBuffer[1u];
    float4 F2  = gFrameUniformBuffer[2u];
    float4 F3  = gFrameUniformBuffer[3u];
    float4 F4  = gFrameUniformBuffer[4u];
    float4 F5  = gFrameUniformBuffer[5u];
    float4 F6  = gFrameUniformBuffer[6u];
    float4 F7  = gFrameUniformBuffer[7u];
    float4 F8  = gFrameUniformBuffer[8u];
    float4 F9  = gFrameUniformBuffer[9u];
    float4 F10 = gFrameUniformBuffer[10u];
    float4 F11 = gFrameUniformBuffer[11u];
    float4 F12 = gFrameUniformBuffer[12u];
    float4 F13 = gFrameUniformBuffer[13u];

    U.CameraOrigin          = F0.xyz;
    U.TanHalfVerticalFov    = F0.w;
    U.CameraRight           = F1.xyz;
    U.AspectRatio           = F1.w;
    U.CameraUp              = F2.xyz;
    U.TlasCellCount         = uint(F2.w + 0.5f);
    U.CameraForward         = F3.xyz;
    U.LuminaireCount        = uint(F3.w + 0.5f);

    U.PrevCameraOrigin      = F4.xyz;
    U.EmissiveFacetCount    = uint(F4.w + 0.5f);
    U.PrevCameraRight       = F5.xyz;
    U.SurfaceCardCount      = uint(F5.w + 0.5f);
    U.PrevCameraUp          = F6.xyz;
    U.CandidateLightSamples = uint(F6.w + 0.5f);
    U.PrevCameraForward     = F7.xyz;
    U.SpatialNeighborCount  = uint(F7.w + 0.5f);

    U.SunDirection          = F8.xyz;
    U.SunIrradianceLux      = F8.w;
    U.SunRadianceTint       = F9.xyz;
    U.SkyAmbientGain        = F9.w;
    U.MoonDirection         = F10.xyz;
    U.MoonCoronaStrength    = F10.w;

    U.NearMaterialMeters    = F11.x;
    U.FarMaterialMeters     = F11.y;
    U.MaterialQualityScale  = F11.z;
    U.TemporalHistoryCap    = F11.w;

    U.CloudCoverage         = F12.x;
    U.CloudAltitudeMeters   = F12.y;
    U.FogBaseDensity        = F12.z;
    U.FogDustBoost          = F12.w;

    U.ExposureScale         = F13.x;
    U.FlareIntensity        = F13.y;
    U.InspectionMode        = uint(F13.z + 0.5f);
    U.FeatureFlags          = uint(F13.w + 0.5f);
    return U;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  DETERMINISTIC RNG, PACKING, & ENVIRONMENT HELPERS
//------------------------------------------------------------------------------------------------------------------------

static const float RepresentableAlbedoFloor = 6.0f / 255.0f;

uint AdvancePcgState(inout uint State)
{
    State     = State * 747796405u + 2891336453u;
    uint Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

float NextUniform01(inout uint State)
{
    return float(AdvancePcgState(State) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

bool IsFiniteVec3(float3 V)
{
    return all(isfinite(V));
}

float3 SanitizeRadiance(float3 V)
{
    if (!IsFiniteVec3(V))
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return clamp(V, float3(0.0f, 0.0f, 0.0f), float3(48.0f, 48.0f, 48.0f));
}

float PerceptualLuminance(float3 LinearRgb)
{
    return max(dot(LinearRgb, float3(0.2126f, 0.7152f, 0.0722f)), 0.0f);
}

float PackOctNormalAsFloat16Safe(float3 UnitN)
{
    float3 N    = normalize(UnitN);
    float  InvL = 1.0f / max(abs(N.x) + abs(N.y) + abs(N.z), 1.0e-6f);
    float2 Oct  = N.xy * InvL;
    if (N.z < 0.0f)
    {
        Oct = (1.0f - abs(Oct.yx)) * float2(Oct.x >= 0.0f ? 1.0f : -1.0f, Oct.y >= 0.0f ? 1.0f : -1.0f);
    }
    uint Qx = uint(clamp(int(round((Oct.x * 0.5f + 0.5f) * 31.0f)), 0, 31));
    uint Qy = uint(clamp(int(round((Oct.y * 0.5f + 0.5f) * 31.0f)), 0, 31));
    return float(Qx | (Qy << 5u));
}

float3 UnpackOctNormalFromFloat16Safe(float Code)
{
    uint   Bits = uint(max(Code, 0.0f) + 0.5f);
    float2 Oct  = float2(float(Bits & 31u), float((Bits >> 5u) & 31u)) * (2.0f / 31.0f) - 1.0f;
    float3 N    = float3(Oct.x, Oct.y, 1.0f - abs(Oct.x) - abs(Oct.y));
    if (N.z < 0.0f)
    {
        float Fold = -N.z;
        N.x += (N.x >= 0.0f) ? -Fold : Fold;
        N.y += (N.y >= 0.0f) ? -Fold : Fold;
    }
    return normalize(N);
}

float3 SampleCosineHemisphere(float3 NormalUnit, float U1, float U2)
{
    float  Rad = sqrt(saturate(U1));
    float  Phi = 6.28318530718f * U2;
    float  Lx  = Rad * cos(Phi);
    float  Ly  = Rad * sin(Phi);
    float  Lz  = sqrt(max(0.0f, 1.0f - U1));
    float3 T, B;
    ConstructPixarOrthonormalBasis(NormalUnit, 0.0f, T, B);
    return normalize(Lx * T + Ly * B + Lz * NormalUnit);
}

float3 GeneratePrimaryCameraRay(const DecodedFrameUniforms U, uint2 Pixel)
{
    float2 Uv  = (float2(Pixel) + 0.5f) / float2(float(U.Width), float(U.Height));
    float2 Ndc = float2(Uv.x * 2.0f - 1.0f, 1.0f - Uv.y * 2.0f);
    return normalize(U.CameraForward +
                     U.CameraRight * (Ndc.x * U.TanHalfVerticalFov * U.AspectRatio) +
                     U.CameraUp * (Ndc.y * U.TanHalfVerticalFov));
}

float2 ProjectPreviousPixelCoordinates(const DecodedFrameUniforms U, float3 PreviousWorldPosition)
{
    float3 Rel  = PreviousWorldPosition - U.PrevCameraOrigin;
    float  FwdZ = dot(Rel, U.PrevCameraForward);
    if (FwdZ <= 1.0e-3f)
    {
        return float2(-1000.0f, -1000.0f);
    }
    float NdcX = dot(Rel, U.PrevCameraRight) / (FwdZ * max(U.TanHalfVerticalFov * U.AspectRatio, 1.0e-5f));
    float NdcY = dot(Rel, U.PrevCameraUp) / (FwdZ * max(U.TanHalfVerticalFov, 1.0e-5f));
    float UvX  = NdcX * 0.5f + 0.5f;
    float UvY  = 0.5f - NdcY * 0.5f;
    return float2(UvX * float(U.Width) - 0.5f, UvY * float(U.Height) - 0.5f);
}

float3 EvaluatePlanetarySkyRadiance(const DecodedFrameUniforms U, float3 RayDir, bool IncludeBodiesAndClouds)
{
    if (U.SkyAmbientGain <= 1.0e-5f && U.SunIrradianceLux <= 1.0e-5f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 Dir    = normalize(RayDir);
    float3 SunDir = normalize(U.SunDirection);
    float3 Sky    = float3(0.0f, 0.0f, 0.0f);

    if ((U.FeatureFlags & 8u) != 0u)
    {
        float GroundR   = 6360.0f;
        float ObserverR = 6360.05f;
        float ThetaLimb = -asin(clamp(GroundR / ObserverR, 0.0f, 1.0f));
        float ThetaElev = asin(clamp(Dir.y, -1.0f, 1.0f));
        float UvY       = 0.5f;
        if (ThetaElev < ThetaLimb)
        {
            UvY = 0.5f * (1.0f - sqrt(saturate((ThetaLimb - ThetaElev) / max(ThetaLimb + 1.5707963f, 1.0e-4f))));
        }
        else
        {
            UvY = 0.5f * (1.0f + sqrt(saturate((ThetaElev - ThetaLimb) / max(1.5707963f - ThetaLimb, 1.0e-4f))));
        }

        float2 SunHoriz = float2(SunDir.x, SunDir.z);
        float2 DirHoriz = float2(Dir.x, Dir.z);
        float  SunHLen  = length(SunHoriz);
        float  DirHLen  = length(DirHoriz);
        float  CosPhi   = (SunHLen > 1.0e-5f && DirHLen > 1.0e-5f)
                              ? clamp(dot(SunHoriz / SunHLen, DirHoriz / DirHLen), -1.0f, 1.0f)
                              : 1.0f;
        float  UvX      = acos(CosPhi) * 0.318309886f;
        Sky             = gAtmosphereSkyLut.SampleLevel(float3(UvX, UvY, 0.0f), 0.0f).rgb * max(U.SkyAmbientGain, 0.25f);
    }
    else
    {
        float  SunElev = clamp(SunDir.y, -0.25f, 1.0f);
        float  DayT    = saturate((SunElev + 0.08f) / 0.35f);
        float3 Zenith  = lerp(float3(0.02f, 0.04f, 0.10f), float3(0.16f, 0.34f, 0.78f), DayT);
        float3 Horizon = lerp(float3(0.42f, 0.16f, 0.08f), float3(0.56f, 0.66f, 0.80f), DayT);
        Sky            = lerp(Horizon, Zenith, pow(saturate(max(Dir.y, 0.0f)), 0.55f)) * U.SkyAmbientGain;
        if (Dir.y < 0.0f)
        {
            Sky *= exp(Dir.y * 6.0f);
        }
        float CosSun = dot(Dir, SunDir);
        float MieG   = 0.76f;
        float PhaseM = (1.0f - MieG * MieG) / (12.56637f * pow(max(1.0f + MieG * MieG - 2.0f * MieG * CosSun, 1.0e-3f), 1.5f));
        Sky += U.SunRadianceTint * (U.SunIrradianceLux * 0.045f * min(PhaseM, 6.0f));
    }

    // Hazy lunar corona & moon disc (lunar atmospheric haze support)
    if (IncludeBodiesAndClouds && U.MoonCoronaStrength > 1.0e-4f)
    {
        float3 MoonDir = normalize(U.MoonDirection);
        float  CosMoon = dot(Dir, MoonDir);
        float  AngMoon = acos(clamp(CosMoon, -1.0f, 1.0f));
        float  MoonRad = 0.0092f;
        if (AngMoon < MoonRad && Dir.y > -0.02f)
        {
            float Limb  = sqrt(saturate(1.0f - (AngMoon * AngMoon) / (MoonRad * MoonRad)));
            float Phase = saturate(dot(MoonDir, -SunDir) * 0.5f + 0.65f);
            Sky += float3(0.85f, 0.88f, 0.96f) * (1.8f * Limb * Phase * U.MoonCoronaStrength);
        }
        float Corona = exp(-AngMoon * 26.0f) * 0.35f + exp(-AngMoon * 7.5f) * 0.06f;
        Sky += float3(0.48f, 0.58f, 0.82f) * (Corona * U.MoonCoronaStrength);
    }

    // Limb-darkened solar disc
    if (IncludeBodiesAndClouds && U.SunIrradianceLux > 1.0e-4f && Dir.y > -0.02f)
    {
        float CosSun = dot(Dir, SunDir);
        if (CosSun > 0.99986f)
        {
            float Limb = sqrt(saturate((CosSun - 0.99986f) / 0.00014f));
            Sky += U.SunRadianceTint * (U.SunIrradianceLux * 18.0f * (0.4f + 0.6f * Limb));
        }
    }

    // Procedural 3D stratocumulus cloud deck
    if (IncludeBodiesAndClouds && (U.FeatureFlags & 16u) != 0u && U.CloudCoverage > 0.01f && Dir.y > 0.015f)
    {
        float  CloudBaseM = max(U.CloudAltitudeMeters, 400.0f);
        float  TCloud     = (CloudBaseM - U.CameraOrigin.y) / max(Dir.y, 0.02f);
        float2 CloudUv    = (U.CameraOrigin.xz + Dir.xz * TCloud) * 0.00065f + float2(float(U.FrameIndex) * 0.00015f, 0.0f);

        float2 P0  = floor(CloudUv * 7.0f);
        float2 F0  = frac(CloudUv * 7.0f);
        F0         = F0 * F0 * (3.0f - 2.0f * F0);
        float N00  = frac(sin(dot(P0, float2(127.1f, 311.7f))) * 43758.5453f);
        float N10  = frac(sin(dot(P0 + float2(1.0f, 0.0f), float2(127.1f, 311.7f))) * 43758.5453f);
        float N01  = frac(sin(dot(P0 + float2(0.0f, 1.0f), float2(127.1f, 311.7f))) * 43758.5453f);
        float N11  = frac(sin(dot(P0 + float2(1.0f, 1.0f), float2(127.1f, 311.7f))) * 43758.5453f);
        float Low  = lerp(lerp(N00, N10, F0.x), lerp(N01, N11, F0.x), F0.y);

        float2 P1  = floor(CloudUv * 23.0f);
        float2 F1  = frac(CloudUv * 23.0f);
        F1         = F1 * F1 * (3.0f - 2.0f * F1);
        float M00  = frac(sin(dot(P1, float2(269.5f, 183.3f))) * 43758.5453f);
        float M10  = frac(sin(dot(P1 + float2(1.0f, 0.0f), float2(269.5f, 183.3f))) * 43758.5453f);
        float M01  = frac(sin(dot(P1 + float2(0.0f, 1.0f), float2(269.5f, 183.3f))) * 43758.5453f);
        float M11  = frac(sin(dot(P1 + float2(1.0f, 1.0f), float2(269.5f, 183.3f))) * 43758.5453f);
        float High = lerp(lerp(M00, M10, F1.x), lerp(M01, M11, F1.x), F1.y);

        float Billow  = Low * 0.72f + High * 0.28f;
        float Density = saturate((Billow - (1.0f - U.CloudCoverage * 0.85f)) * 3.2f) * smoothstep(0.015f, 0.14f, Dir.y);
        if (Density > 1.0e-3f)
        {
            float  SilverLining = pow(saturate(dot(Dir, SunDir)), 6.0f) * (1.0f - Density * 0.55f);
            float3 CloudLit     = U.SunRadianceTint * (U.SunIrradianceLux * (0.26f + 0.55f * SilverLining)) + Sky * 0.45f;
            Sky                 = lerp(Sky, CloudLit, 1.0f - exp(-Density * 2.4f));
        }
    }

    return max(Sky, float3(0.0f, 0.0f, 0.0f));
}

float EvaluateProjectedCloudGroundShadow(const DecodedFrameUniforms U, float3 WorldPos)
{
    if ((U.FeatureFlags & 16u) == 0u || U.CloudCoverage <= 0.02f)
    {
        return 1.0f;
    }
    float3 SunDir   = normalize(U.SunDirection);
    float  Travel   = max(U.CloudAltitudeMeters - WorldPos.y, 100.0f) / max(SunDir.y, 0.12f);
    float2 CloudUv  = (WorldPos.xz + SunDir.xz * Travel) * 0.00065f;
    float2 P0       = floor(CloudUv * 7.0f);
    float2 F0       = frac(CloudUv * 7.0f);
    F0              = F0 * F0 * (3.0f - 2.0f * F0);
    float N00       = frac(sin(dot(P0, float2(127.1f, 311.7f))) * 43758.5453f);
    float N10       = frac(sin(dot(P0 + float2(1.0f, 0.0f), float2(127.1f, 311.7f))) * 43758.5453f);
    float N01       = frac(sin(dot(P0 + float2(0.0f, 1.0f), float2(127.1f, 311.7f))) * 43758.5453f);
    float N11       = frac(sin(dot(P0 + float2(1.0f, 1.0f), float2(127.1f, 311.7f))) * 43758.5453f);
    float Low       = lerp(lerp(N00, N10, F0.x), lerp(N01, N11, F0.x), F0.y);
    float CloudMask = saturate((Low - (1.0f - U.CloudCoverage * 0.80f)) * 2.8f);
    return lerp(1.0f, 0.38f, CloudMask * 0.65f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  KERNEL 1: PRIMARY VISIBILITY & GLASS PEEL
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void TracePrimarySurfaceMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    DecodedFrameUniforms U     = LoadFrameUniforms();
    uint2                Pixel = DispatchThreadId.xy;
    if (Pixel.x >= U.Width || Pixel.y >= U.Height)
    {
        return;
    }

    float3 RayDir = GeneratePrimaryCameraRay(U, Pixel);

    BoundingRayHit FirstHit = SearchInstancedClosestRay(
        gTlasCells, U.TlasCellCount, gInstances, gWideCells, gWideTriangles,
        U.CameraOrigin, RayDir, 1.0e-3f, 2.0e4f, true);

    if (!FirstHit.Hit)
    {
        gGBufferCurr[uint3(Pixel, 0u)] = float4(RayDir, -1.0f);
        gGBufferCurr[uint3(Pixel, 1u)] = float4(0.0f, 1.0f, 0.0f, 1.0f);
        gGBufferCurr[uint3(Pixel, 2u)] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        gGBufferCurr[uint3(Pixel, 3u)] = float4(0.0f, 1.0f, 0.0f, 0.0f);
        gGBufferCurr[uint3(Pixel, 4u)] = float4(-1000.0f, -1000.0f, 0.0f, 0.0f);
        gGBufferCurr[uint3(Pixel, 5u)] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    DielectricGlassPeel Peel = PeelInstancedDielectricGlass(
        gTlasCells, U.TlasCellCount, gInstances, gWideCells, gWideTriangles, FirstHit, RayDir);

    BoundingRayHit ActiveHit      = FirstHit;
    float3         ActiveViewDir  = -RayDir;
    float3         GlassTint      = float3(1.0f, 1.0f, 1.0f);
    float          GlassFresnel   = 0.0f;
    float4         GlassLayerData = float4(0.0f, 0.0f, 0.0f, 0.0f);

    if (Peel.EncounteredGlass)
    {
        GlassTint      = Peel.BeerLambertThroughput;
        GlassFresnel   = Peel.EntryFresnelReflectance;
        GlassLayerData = float4(Peel.EntryWorldPosition, PackOctNormalAsFloat16Safe(Peel.EntryReflectedRay));

        BoundingRayHit BehindHit = SearchInstancedClosestRay(
            gTlasCells, U.TlasCellCount, gInstances, gWideCells, gWideTriangles,
            Peel.TransmittedRayOrigin, Peel.TransmittedRayDirection, 1.5e-3f, 2.0e4f, false);

        if (!BehindHit.Hit)
        {
            gGBufferCurr[uint3(Pixel, 0u)] = float4(Peel.TransmittedRayDirection, -2.0f);
            gGBufferCurr[uint3(Pixel, 1u)] = float4(Peel.EntryNormal, 0.02f);
            gGBufferCurr[uint3(Pixel, 2u)] = float4(GlassTint, 0.0f);
            gGBufferCurr[uint3(Pixel, 3u)] = float4(float(FirstHit.ClassificationId), PerceptualLuminance(GlassTint), GlassFresnel, 0.0f);
            float2 PrevGlassPx             = ProjectPreviousPixelCoordinates(U, FirstHit.PreviousWorldPosition);
            gGBufferCurr[uint3(Pixel, 4u)] = float4(PrevGlassPx.x, PrevGlassPx.y, PackOctNormalAsFloat16Safe(FirstHit.GeometricNormal), float(FirstHit.InstanceIndex + 1u));
            gGBufferCurr[uint3(Pixel, 5u)] = GlassLayerData;
            return;
        }

        ActiveHit                 = BehindHit;
        ActiveHit.DistanceMeters += FirstHit.DistanceMeters;
        ActiveViewDir             = -Peel.TransmittedRayDirection;
    }

    uint DetailTier = SelectMaterialDetailTierGpu(
        ActiveHit.DistanceMeters, U.NearMaterialMeters, U.FarMaterialMeters, U.MaterialQualityScale);

    SurfaceScatteringMaterial Mat    = UnpackScatteringSlot(gMaterialTable, ActiveHit.ClassificationId);
    float3                    ShadeN = ActiveHit.ShadingNormal;
    EvaluateSurfaceMaterialDetail(Mat, ShadeN, ActiveHit.GeometricNormal, ActiveHit.WorldPosition,
                                  ActiveViewDir, ActiveHit.TextureCoord, DetailTier,
                                  gAlbedoSrgbArray, gNormalUnormArray, gOrmUnormArray);

    if (!IsFiniteVec3(ShadeN))
    {
        ShadeN = ActiveHit.GeometricNormal;
    }
    if (!IsFiniteVec3(ShadeN))
    {
        ShadeN = float3(0.0f, 1.0f, 0.0f);
    }

    float3 EffectiveAlbedo = clamp(Mat.BaseAlbedo * GlassTint, float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f));
    float2 PrevPx          = ProjectPreviousPixelCoordinates(U, ActiveHit.PreviousWorldPosition);
    uint   CascadeIdx      = QueryActiveCascadeIndexGpu(gDistanceVolume, ActiveHit.WorldPosition);
    float  PackedTierCasc  = float(DetailTier + CascadeIdx * 4u);
    float  ClusterHash     = float(((ActiveHit.InstanceIndex * 131u) ^ (ActiveHit.TriangleIndex / 64u)) & 1023u);

    gGBufferCurr[uint3(Pixel, 0u)] = float4(ActiveHit.WorldPosition, ActiveHit.DistanceMeters);
    gGBufferCurr[uint3(Pixel, 1u)] = float4(ShadeN, Mat.BaseRoughness);
    gGBufferCurr[uint3(Pixel, 2u)] = float4(EffectiveAlbedo, Mat.Metallic);
    gGBufferCurr[uint3(Pixel, 3u)] = float4(float(ActiveHit.ClassificationId), Mat.AmbientOcclusion, GlassFresnel, PackedTierCasc);
    gGBufferCurr[uint3(Pixel, 4u)] = float4(PrevPx.x, PrevPx.y, PackOctNormalAsFloat16Safe(ActiveHit.GeometricNormal), ClusterHash);
    gGBufferCurr[uint3(Pixel, 5u)] = GlassLayerData;
}

//------------------------------------------------------------------------------------------------------------------------
//                           KERNEL 2: DIRECT RESERVOIR RIS + FACET CDF + TEMPORAL REUSE
//------------------------------------------------------------------------------------------------------------------------

struct LuminaireSampleCandidate
{
    uint   LightCode;
    float3 DirectionToLight;
    float  DistanceMeters;
    float3 IncidentRadiance;
};

LuminaireSampleCandidate SampleAnalyticalOrSunLight(const DecodedFrameUniforms U, uint LightSlot, float3 ShadingPos, inout uint Rng)
{
    LuminaireSampleCandidate Out;
    Out.LightCode        = LightSlot;
    Out.DirectionToLight = float3(0.0f, 1.0f, 0.0f);
    Out.DistanceMeters   = 1.0e4f;
    Out.IncidentRadiance = float3(0.0f, 0.0f, 0.0f);

    if (LightSlot == 0u)
    {
        float3 SunDir = normalize(U.SunDirection);
        float3 T, B;
        ConstructPixarOrthonormalBasis(SunDir, 0.0f, T, B);
        float Rad            = 0.00465f * sqrt(NextUniform01(Rng));
        float Phi            = 6.2831853f * NextUniform01(Rng);
        Out.DirectionToLight = normalize(SunDir + T * (Rad * cos(Phi)) + B * (Rad * sin(Phi)));
        Out.DistanceMeters   = 1.0e4f;
        Out.IncidentRadiance = U.SunRadianceTint * U.SunIrradianceLux * EvaluateProjectedCloudGroundShadow(U, ShadingPos);
        return Out;
    }

    uint   LocalIdx = LightSlot - 1u;
    float4 L0       = gLuminaires[LocalIdx * 3u + 0u];
    float4 L1       = gLuminaires[LocalIdx * 3u + 1u];
    float4 L2       = gLuminaires[LocalIdx * 3u + 2u];

    float3 ToLight = L0.xyz - ShadingPos;
    float  DistSq  = max(dot(ToLight, ToLight), 1.0e-2f);
    float  Dist    = sqrt(DistSq);
    float3 Dir     = ToLight / Dist;
    float  Atten   = min(3.14159265f * max(L0.w * L0.w, 0.04f) / DistSq, 4.0f);

    if (L1.w > 1.5f)
    {
        float CosCone = dot(-Dir, normalize(L2.xyz));
        Atten *= saturate((CosCone - L2.w) / max(1.0f - L2.w, 0.05f));
    }

    Out.DirectionToLight = Dir;
    Out.DistanceMeters   = max(Dist - L0.w * 0.25f, 0.02f);
    Out.IncidentRadiance = L1.xyz * Atten;
    return Out;
}

float3 EvaluateStoredDirectLightRadiance(const DecodedFrameUniforms U, uint LightCode, float3 ShadingPos, out float3 OutLightDir, out float OutMaxDist)
{
    if ((LightCode & 0x8000u) != 0u)
    {
        uint   FacetIdx = min(LightCode & 0x7FFFu, max(U.EmissiveFacetCount, 1u) - 1u);
        uint   Base     = FacetIdx * 4u;
        float4 E0       = gEmissiveFacets[Base + 0u];
        float4 E1       = gEmissiveFacets[Base + 1u];
        float4 E2       = gEmissiveFacets[Base + 2u];
        float4 E3       = gEmissiveFacets[Base + 3u];
        float3 Center   = E0.xyz + (E1.xyz + E2.xyz) * (1.0f / 3.0f);
        float3 Delta    = Center - ShadingPos;
        float  DistSq   = max(dot(Delta, Delta), 0.04f);
        float  Dist     = sqrt(DistSq);
        OutLightDir     = Delta / Dist;
        OutMaxDist      = max(Dist - 0.03f, 0.02f);
        float3 Norm     = normalize(cross(E1.xyz, E2.xyz));
        float  CosL     = max(abs(dot(Norm, -OutLightDir)), 1.0e-3f);
        float  Omega    = min((E0.w * CosL) / DistSq, 2.5f);
        return E3.xyz * Omega;
    }

    if (LightCode == 0u)
    {
        OutLightDir = normalize(U.SunDirection);
        OutMaxDist  = 1.0e4f;
        return U.SunRadianceTint * U.SunIrradianceLux * EvaluateProjectedCloudGroundShadow(U, ShadingPos);
    }

    uint                     DummyRng = 1u;
    LuminaireSampleCandidate S        = SampleAnalyticalOrSunLight(U, LightCode, ShadingPos, DummyRng);
    OutLightDir                       = S.DirectionToLight;
    OutMaxDist                        = S.DistanceMeters;
    return S.IncidentRadiance;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void SampleDirectReservoirsMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    DecodedFrameUniforms U     = LoadFrameUniforms();
    uint2                Pixel = DispatchThreadId.xy;
    if (Pixel.x >= U.Width || Pixel.y >= U.Height)
    {
        return;
    }

    float4 G0 = gGBufferCurr[uint3(Pixel, 0u)];
    if (G0.w < 0.0f)
    {
        gReservoirWrite[uint3(Pixel, 0u)] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float4 G1    = gGBufferCurr[uint3(Pixel, 1u)];
    float4 G2    = gGBufferCurr[uint3(Pixel, 2u)];
    float4 G3    = gGBufferCurr[uint3(Pixel, 3u)];
    float4 G4    = gGBufferCurr[uint3(Pixel, 4u)];
    uint   Class = uint(G3.x + 0.5f);

    SurfaceScatteringMaterial Mat = UnpackScatteringSlot(gMaterialTable, Class);
    Mat.BaseAlbedo                = G2.xyz;
    Mat.BaseRoughness             = G1.w;
    Mat.Metallic                  = G2.w;
    Mat.AmbientOcclusion          = G3.y;

    float3 WorldPos  = G0.xyz;
    float3 ShadeNorm = normalize(G1.xyz);
    float3 GeomNorm  = UnpackOctNormalFromFloat16Safe(G4.z);
    float3 ViewDir   = normalize(U.CameraOrigin - WorldPos);

    uint  Rng             = (Pixel.x * 1973u) ^ (Pixel.y * 9277u) ^ (U.FrameIndex * 26699u) ^ 0x9E3779B9u;
    uint  AnalyticalTotal = 1u + U.LuminaireCount;
    bool  EmissiveActive  = (U.EmissiveFacetCount > 0u);
    float EmissiveMixProb = EmissiveActive ? 0.35f : 0.0f;

    uint  ChosenCode      = 0u;
    float ChosenTargetPdf = 0.0f;
    float WeightSum       = 0.0f;
    uint  CandidateCount  = clamp(U.CandidateLightSamples, 1u, 16u);

    for (uint C = 0u; C < CandidateCount; ++C)
    {
        uint   CandCode    = 0u;
        float3 CandContrib = float3(0.0f, 0.0f, 0.0f);
        float  InvSourceP  = 1.0f;

        if (EmissiveActive && NextUniform01(Rng) < EmissiveMixProb)
        {
            float Selector = NextUniform01(Rng);
            uint  Lo = 0u, Hi = U.EmissiveFacetCount - 1u;
            [loop]
            while (Lo < Hi)
            {
                uint Mid = Lo + ((Hi - Lo) >> 1u);
                if (Selector >= gEmissiveFacets[Mid * 4u + 2u].w) Lo = Mid + 1u;
                else Hi = Mid;
            }
            SampledEmissiveFacet Ef = SampleEmissiveFacetCdf(
                gEmissiveFacets, U.EmissiveFacetCount, WorldPos, Selector, NextUniform01(Rng), NextUniform01(Rng));
            if (Ef.Valid)
            {
                CandCode    = 0x8000u | min(Lo, 0x7FFFu);
                float NdotL = max(dot(ShadeNorm, Ef.DirectionToLight), 0.0f);
                if (NdotL > 0.0f)
                {
                    CandContrib = EvaluateScatteringBrdf(Mat, ShadeNorm, ViewDir, Ef.DirectionToLight) * Ef.EmittedRadiance * NdotL;
                    InvSourceP  = 1.0f / max(EmissiveMixProb * Ef.SolidAnglePdf, 1.0e-5f);
                }
            }
        }
        else
        {
            uint                     Slot = min(uint(NextUniform01(Rng) * float(AnalyticalTotal)), AnalyticalTotal - 1u);
            LuminaireSampleCandidate Lc   = SampleAnalyticalOrSunLight(U, Slot, WorldPos, Rng);
            CandCode                      = Slot;
            float NdotL                   = max(dot(ShadeNorm, Lc.DirectionToLight), 0.0f);
            if (NdotL > 0.0f)
            {
                CandContrib = EvaluateScatteringBrdf(Mat, ShadeNorm, ViewDir, Lc.DirectionToLight) * Lc.IncidentRadiance * NdotL;
                InvSourceP  = float(AnalyticalTotal) / max(1.0f - EmissiveMixProb, 0.1f);
            }
        }

        float TargetP = PerceptualLuminance(CandContrib);
        float RisW    = TargetP * min(InvSourceP, 24.0f);
        WeightSum += RisW;
        if (WeightSum > 0.0f && NextUniform01(Rng) * WeightSum < RisW)
        {
            ChosenCode      = CandCode;
            ChosenTargetPdf = TargetP;
        }
    }

    uint  MCount = CandidateCount;
    float BigW   = (ChosenTargetPdf > 1.0e-6f) ? (WeightSum / (float(CandidateCount) * ChosenTargetPdf)) : 0.0f;

    if (BigW > 0.0f)
    {
        float3 WinDir;
        float  WinDist;
        EvaluateStoredDirectLightRadiance(U, ChosenCode, WorldPos, WinDir, WinDist);
        float3 Vis    = SearchInstancedShadowTransmittance(
            gTlasCells, U.TlasCellCount, gInstances, gWideCells, gWideTriangles,
            WorldPos + GeomNorm * 2.5e-3f, WinDir, WinDist);
        float  VisLum = PerceptualLuminance(Vis);
        if (VisLum <= 1.0e-4f)
        {
            BigW            = 0.0f;
            WeightSum       = 0.0f;
            ChosenTargetPdf = 0.0f;
        }
        else
        {
            BigW *= VisLum;
        }
    }

    float MaxHistoryM = clamp(U.TemporalHistoryCap, 0.0f, 32.0f);
    if (U.FrameIndex > 0u && MaxHistoryM > 0.5f)
    {
        int2 PrevPx = int2(round(G4.xy));
        if (PrevPx.x >= 0 && PrevPx.y >= 0 && PrevPx.x < int(U.Width) && PrevPx.y < int(U.Height))
        {
            float4 PrevG0 = gGBufferPrev[uint3(uint2(PrevPx), 0u)];
            float4 PrevG1 = gGBufferPrev[uint3(uint2(PrevPx), 1u)];
            if (PrevG0.w > 0.0f &&
                dot(ShadeNorm, normalize(PrevG1.xyz)) > 0.90f &&
                abs(dot(GeomNorm, WorldPos - PrevG0.xyz)) < 0.14f)
            {
                float4 PrevRes  = gReservoirRead[uint3(uint2(PrevPx), 0u)];
                uint   PrevCode = uint(PrevRes.x + 0.5f);
                float  PrevW    = PrevRes.y;
                uint   PrevM    = min(uint(PrevRes.z + 0.5f), uint(MaxHistoryM));
                if (PrevM > 0u && PrevW > 0.0f)
                {
                    float3 PrevDir;
                    float  PrevDist;
                    float3 PrevLi       = EvaluateStoredDirectLightRadiance(U, PrevCode, WorldPos, PrevDir, PrevDist);
                    float  NdotL        = max(dot(ShadeNorm, PrevDir), 0.0f);
                    float  TargetAtCurr = 0.0f;
                    if (NdotL > 0.0f)
                    {
                        float3 Vis   = SearchInstancedShadowTransmittance(
                            gTlasCells, U.TlasCellCount, gInstances, gWideCells, gWideTriangles,
                            WorldPos + GeomNorm * 2.5e-3f, PrevDir, PrevDist);
                        TargetAtCurr = PerceptualLuminance(EvaluateScatteringBrdf(Mat, ShadeNorm, ViewDir, PrevDir) * PrevLi * Vis * NdotL);
                    }

                    float CurStreamW  = ChosenTargetPdf * BigW * float(MCount);
                    float PrevStreamW = TargetAtCurr * PrevW * float(PrevM);
                    float CombinedSum = CurStreamW + PrevStreamW;
                    uint  CombinedM   = MCount + PrevM;
                    if (CombinedSum > 0.0f && NextUniform01(Rng) * CombinedSum < PrevStreamW)
                    {
                        ChosenCode      = PrevCode;
                        ChosenTargetPdf = TargetAtCurr;
                    }
                    MCount = CombinedM;
                    BigW   = (ChosenTargetPdf > 1.0e-6f && CombinedM > 0u)
                                 ? min(CombinedSum / (float(CombinedM) * ChosenTargetPdf), 16.0f)
                                 : 0.0f;
                }
            }
        }
    }

    gReservoirWrite[uint3(Pixel, 0u)] = float4(float(ChosenCode), BigW, float(min(MCount, 24u)), ChosenTargetPdf);
}

//------------------------------------------------------------------------------------------------------------------------
//                KERNEL 3: TRUE ReSTIR GI (4-CASCADE SDF + SURFACE CARDS + TEMPORAL GI REUSE)
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void SampleIndirectReservoirsMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    DecodedFrameUniforms U     = LoadFrameUniforms();
    uint2                Pixel = DispatchThreadId.xy;
    if (Pixel.x >= U.Width || Pixel.y >= U.Height)
    {
        return;
    }

    float4 G0 = gGBufferCurr[uint3(Pixel, 0u)];
    if (G0.w < 0.0f || (U.FeatureFlags & 1u) == 0u)
    {
        gReservoirWrite[uint3(Pixel, 1u)] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        gReservoirWrite[uint3(Pixel, 2u)] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float4 G1        = gGBufferCurr[uint3(Pixel, 1u)];
    float4 G4        = gGBufferCurr[uint3(Pixel, 4u)];
    float3 WorldPos  = G0.xyz;
    float3 ShadeNorm = normalize(G1.xyz);
    float3 GeomNorm  = UnpackOctNormalFromFloat16Safe(G4.z);

    uint   Rng    = (Pixel.x * 6151u) ^ (Pixel.y * 3079u) ^ (U.FrameIndex * 49157u) ^ 0x85EBCA6Bu;
    float3 GiDir  = SampleCosineHemisphere(ShadeNorm, NextUniform01(Rng), NextUniform01(Rng));
    float  CosGi  = max(dot(ShadeNorm, GiDir), 1.0e-3f);
    float  InvPdf = 3.14159265f / CosGi;

    float3 SamplePoint    = WorldPos + GiDir * 48.0f;
    float3 SampleNormal   = -GiDir;
    float3 SampleRadiance = float3(0.0f, 0.0f, 0.0f);

    BoundingRayHit NearHit = SearchInstancedClosestRay(
        gTlasCells, U.TlasCellCount, gInstances, gWideCells, gWideTriangles,
        WorldPos + GeomNorm * 3.5e-3f, GiDir, 1.0e-2f, 4.5f, false);

    if (NearHit.Hit)
    {
        SamplePoint                    = NearHit.WorldPosition;
        SampleNormal                   = NearHit.ShadingNormal;
        SurfaceScatteringMaterial SecM = UnpackScatteringSlot(gMaterialTable, NearHit.ClassificationId);
        float3                    CardB = QuerySurfaceCardExitanceGpu(gCardDescriptors, gCardExitance, U.SurfaceCardCount, SamplePoint, SampleNormal);

        float3 SunDir       = normalize(U.SunDirection);
        float  NdotS        = max(dot(SampleNormal, SunDir), 0.0f);
        float3 DirectBounce = float3(0.0f, 0.0f, 0.0f);
        if (NdotS > 0.0f && U.SunIrradianceLux > 0.0f)
        {
            float SunVis = TraceVolumeConeVisibilityGpu(gDistanceVolume, SamplePoint + SampleNormal * 0.06f, SunDir, 28.0f, 14.0f, 14u);
            DirectBounce = (SecM.BaseAlbedo * (1.0f - SecM.Metallic) * 0.318309886f) *
                           (U.SunRadianceTint * U.SunIrradianceLux * NdotS * SunVis);
        }
        SampleRadiance = SecM.EmissionRadiance * SecM.EmissionStrength + DirectBounce +
                         CardB * SecM.BaseAlbedo * (1.0f - SecM.Metallic);
    }
    else
    {
        VolumeSphereTraceHit SdfHit = TraceVolumeRayHitGpu(
            gDistanceVolume, WorldPos + GeomNorm * 0.06f, GiDir, 4.4f, 64.0f, 20u);
        if (SdfHit.Hit)
        {
            SamplePoint   = SdfHit.WorldPosition;
            SampleNormal  = SdfHit.WorldNormal;
            float3 CardB  = QuerySurfaceCardExitanceGpu(gCardDescriptors, gCardExitance, U.SurfaceCardCount, SamplePoint, SampleNormal);
            float3 SunDir = normalize(U.SunDirection);
            float  NdotS  = max(dot(SampleNormal, SunDir), 0.0f);
            float  SunVis = (NdotS > 0.0f)
                                ? TraceVolumeConeVisibilityGpu(gDistanceVolume, SamplePoint + SampleNormal * 0.08f, SunDir, 32.0f, 12.0f, 12u)
                                : 0.0f;
            SampleRadiance = CardB + (0.28f * 0.318309886f) * (U.SunRadianceTint * U.SunIrradianceLux * NdotS * SunVis);
        }
        else
        {
            SampleRadiance = EvaluatePlanetarySkyRadiance(U, GiDir, false);
        }
    }

    SampleRadiance   = min(SampleRadiance, float3(12.0f, 12.0f, 12.0f));
    float TargetCurr = PerceptualLuminance(SampleRadiance) * (CosGi * 0.318309886f);
    float BigW       = (TargetCurr > 1.0e-6f) ? InvPdf : 0.0f;
    uint  MCount     = 1u;

    float MaxGiM = min(clamp(U.TemporalHistoryCap, 0.0f, 20.0f), 16.0f);
    if (U.FrameIndex > 0u && MaxGiM > 0.5f)
    {
        int2 PrevPx = int2(round(G4.xy));
        if (PrevPx.x >= 0 && PrevPx.y >= 0 && PrevPx.x < int(U.Width) && PrevPx.y < int(U.Height))
        {
            float4 PrevG0 = gGBufferPrev[uint3(uint2(PrevPx), 0u)];
            float4 PrevG1 = gGBufferPrev[uint3(uint2(PrevPx), 1u)];
            if (PrevG0.w > 0.0f &&
                dot(ShadeNorm, normalize(PrevG1.xyz)) > 0.88f &&
                abs(dot(GeomNorm, WorldPos - PrevG0.xyz)) < 0.16f)
            {
                float4 PrevLo   = gReservoirRead[uint3(uint2(PrevPx), 1u)];
                float4 PrevHi   = gReservoirRead[uint3(uint2(PrevPx), 2u)];
                uint   PrevM    = min(uint(floor(PrevHi.w / 64.0f)), uint(MaxGiM));
                float  PrevBigW = max(PrevHi.w - float(uint(floor(PrevHi.w / 64.0f))) * 64.0f, 0.0f);

                if (PrevM > 0u && PrevBigW > 0.0f)
                {
                    float3 PrevY    = PrevLo.xyz;
                    float3 PrevNY   = UnpackOctNormalFromFloat16Safe(PrevLo.w);
                    float3 PrevRad  = PrevHi.xyz;
                    float3 ToPrevY  = PrevY - WorldPos;
                    float  DistPrev = length(ToPrevY);
                    if (DistPrev > 0.03f)
                    {
                        float3 DirPrev    = ToPrevY / DistPrev;
                        float  CosAtCurr  = max(dot(ShadeNorm, DirPrev), 0.0f);
                        float  TargetPrev = PerceptualLuminance(PrevRad) * (CosAtCurr * 0.318309886f);
                        if (TargetPrev > 1.0e-6f)
                        {
                            float CurW  = TargetCurr * BigW * float(MCount);
                            float HistW = TargetPrev * PrevBigW * float(PrevM);
                            float Total = CurW + HistW;
                            uint  SumM  = MCount + PrevM;
                            if (NextUniform01(Rng) * Total < HistW)
                            {
                                SamplePoint    = PrevY;
                                SampleNormal   = PrevNY;
                                SampleRadiance = PrevRad;
                                TargetCurr     = TargetPrev;
                            }
                            MCount = min(SumM, uint(MaxGiM));
                            BigW   = clamp(Total / max(float(SumM) * TargetCurr, 1.0e-6f), 0.0f, 16.0f);
                        }
                    }
                }
            }
        }
    }

    float PackedWAndM                 = min(BigW, 63.0f) + float(min(MCount, 16u)) * 64.0f;
    gReservoirWrite[uint3(Pixel, 1u)] = float4(SamplePoint, PackOctNormalAsFloat16Safe(SampleNormal));
    gReservoirWrite[uint3(Pixel, 2u)] = float4(SampleRadiance, PackedWAndM);
}

//------------------------------------------------------------------------------------------------------------------------
//      KERNEL 4: SPATIAL DI + JACOBIAN GI REUSE, HEITZ VNDF REFLECTIONS, & SPLIT DEMODULATION
//------------------------------------------------------------------------------------------------------------------------

float3 TraceSingleSpecularBounce(const DecodedFrameUniforms U, float3 Origin, float3 GeomNormal, float3 RayDir, bool AllowSecondMirrorBounce, inout float OutHitDist)
{
    BoundingRayHit ReflHit = SearchInstancedClosestRay(
        gTlasCells, U.TlasCellCount, gInstances, gWideCells, gWideTriangles,
        Origin + GeomNormal * 3.0e-3f, RayDir, 2.0e-3f, 2.0e4f, false);

    if (!ReflHit.Hit)
    {
        OutHitDist = 200.0f;
        // 📝 Do not sample the solar disc here: the direct specular lobe already carries the sun highlight, and
        // hitting the small disc with one VNDF ray is exactly the single-sample firefly this clamp removes.
        return min(EvaluatePlanetarySkyRadiance(U, RayDir, false), float3(4.0f, 4.0f, 4.0f));
    }

    OutHitDist                     = ReflHit.DistanceMeters;
    SurfaceScatteringMaterial RMat = UnpackScatteringSlot(gMaterialTable, ReflHit.ClassificationId);
    uint   Tier                    = SelectMaterialDetailTierGpu(ReflHit.DistanceMeters, U.NearMaterialMeters, U.FarMaterialMeters, U.MaterialQualityScale);
    float3 RNorm                   = ReflHit.ShadingNormal;
    EvaluateSurfaceMaterialDetail(RMat, RNorm, ReflHit.GeometricNormal, ReflHit.WorldPosition,
                                  -RayDir, ReflHit.TextureCoord, min(Tier + 1u, 2u),
                                  gAlbedoSrgbArray, gNormalUnormArray, gOrmUnormArray);

    float3 Rad    = RMat.EmissionRadiance * RMat.EmissionStrength;
    float3 SunDir = normalize(U.SunDirection);
    float  NdotS  = max(dot(RNorm, SunDir), 0.0f);
    if (NdotS > 0.0f && U.SunIrradianceLux > 0.0f)
    {
        float3 SunVis = SearchInstancedShadowTransmittance(
            gTlasCells, U.TlasCellCount, gInstances, gWideCells, gWideTriangles,
            ReflHit.WorldPosition + ReflHit.GeometricNormal * 3.0e-3f, SunDir, 1.0e4f);
        Rad += EvaluateScatteringBrdf(RMat, RNorm, -RayDir, SunDir) *
               (U.SunRadianceTint * U.SunIrradianceLux * EvaluateProjectedCloudGroundShadow(U, ReflHit.WorldPosition)) *
               SunVis * NdotS;
    }

    float3 CardExit = QuerySurfaceCardExitanceGpu(gCardDescriptors, gCardExitance, U.SurfaceCardCount, ReflHit.WorldPosition, RNorm);
    float3 SkyRefl  = EvaluatePlanetarySkyRadiance(U, RNorm, false) * 0.25f;
    Rad += (CardExit + SkyRefl) * RMat.BaseAlbedo * (1.0f - RMat.Metallic) * RMat.AmbientOcclusion;

    if (AllowSecondMirrorBounce && (RMat.BaseRoughness < 0.14f || RMat.CoatCoverage > 0.5f))
    {
        float3         Dir2 = reflect(RayDir, RNorm);
        BoundingRayHit Hit2 = SearchInstancedClosestRay(
            gTlasCells, U.TlasCellCount, gInstances, gWideCells, gWideTriangles,
            ReflHit.WorldPosition + ReflHit.GeometricNormal * 3.0e-3f, Dir2, 2.0e-3f, 2.0e3f, false);
        float3 Rad2 = Hit2.Hit
                          ? (UnpackScatteringSlot(gMaterialTable, Hit2.ClassificationId).BaseAlbedo * 0.35f)
                          : min(EvaluatePlanetarySkyRadiance(U, Dir2, false), float3(4.0f, 4.0f, 4.0f));
        float3 F02  = lerp(float3(0.04f, 0.04f, 0.04f), RMat.BaseAlbedo, RMat.Metallic);
        Rad += FresnelSchlickConductor(saturate(dot(RNorm, -RayDir)), F02) * Rad2;
    }

    return min(Rad, float3(6.0f, 6.0f, 6.0f));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ReuseSpatialReservoirsMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    DecodedFrameUniforms U     = LoadFrameUniforms();
    uint2                Pixel = DispatchThreadId.xy;
    if (Pixel.x >= U.Width || Pixel.y >= U.Height)
    {
        return;
    }

    float4 G0 = gGBufferCurr[uint3(Pixel, 0u)];
    if (G0.w < 0.0f)
    {
        gReservoirWrite[uint3(Pixel, 0u)] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        gReservoirWrite[uint3(Pixel, 1u)] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        gReservoirWrite[uint3(Pixel, 2u)] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        gRadianceWrite[uint3(Pixel, 0u)]  = float4(0.0f, 0.0f, 0.0f, 0.0f);
        gRadianceWrite[uint3(Pixel, 1u)]  = float4(0.0f, 0.0f, 0.0f, 200.0f);
        return;
    }

    float4 G1    = gGBufferCurr[uint3(Pixel, 1u)];
    float4 G2    = gGBufferCurr[uint3(Pixel, 2u)];
    float4 G3    = gGBufferCurr[uint3(Pixel, 3u)];
    float4 G4    = gGBufferCurr[uint3(Pixel, 4u)];
    uint   Class = uint(G3.x + 0.5f);

    SurfaceScatteringMaterial Mat = UnpackScatteringSlot(gMaterialTable, Class);
    Mat.BaseAlbedo                = G2.xyz;
    Mat.BaseRoughness             = G1.w;
    Mat.Metallic                  = G2.w;
    Mat.AmbientOcclusion          = G3.y;

    float3 WorldPos  = G0.xyz;
    float3 ShadeNorm = normalize(G1.xyz);
    float3 GeomNorm  = UnpackOctNormalFromFloat16Safe(G4.z);
    float3 ViewDir   = normalize(U.CameraOrigin - WorldPos);

    uint Rng = (Pixel.x * 9277u) ^ (Pixel.y * 1973u) ^ (U.FrameIndex * 12289u) ^ 0xA341316Cu;

    float4 SelfDi   = gReservoirRead[uint3(Pixel, 0u)];
    uint   DiCode   = uint(SelfDi.x + 0.5f);
    float  DiBigW   = SelfDi.y;
    uint   DiM      = max(uint(SelfDi.z + 0.5f), 1u);
    float  DiTarget = SelfDi.w;
    float  DiRunSum = DiTarget * DiBigW * float(DiM);
    uint   DiTotalM = DiM;

    float4 SelfGiLo  = gReservoirRead[uint3(Pixel, 1u)];
    float4 SelfGiHi  = gReservoirRead[uint3(Pixel, 2u)];
    uint   GiM       = max(uint(floor(SelfGiHi.w / 64.0f)), 1u);
    float  GiBigW    = max(SelfGiHi.w - float(uint(floor(SelfGiHi.w / 64.0f))) * 64.0f, 0.0f);
    float3 GiPos     = SelfGiLo.xyz;
    float3 GiNorm    = UnpackOctNormalFromFloat16Safe(SelfGiLo.w);
    float3 GiRad     = SelfGiHi.xyz;
    float3 SelfGiDir = normalize(GiPos - WorldPos);
    float  GiTarget  = PerceptualLuminance(GiRad) * max(dot(ShadeNorm, SelfGiDir), 0.0f) * 0.318309886f;
    float  GiRunSum  = GiTarget * GiBigW * float(GiM);
    uint   GiTotalM  = GiM;

    uint NeighborBudget = ((U.FeatureFlags & 2u) != 0u) ? clamp(U.SpatialNeighborCount, 0u, 8u) : 0u;
    for (uint K = 0u; K < NeighborBudget; ++K)
    {
        float Angle  = 6.2831853f * NextUniform01(Rng);
        float Radius = 2.0f + 12.0f * sqrt(NextUniform01(Rng));
        int2  NbPx   = int2(Pixel) + int2(round(float2(cos(Angle), sin(Angle)) * Radius));
        if (NbPx.x < 0 || NbPx.y < 0 || NbPx.x >= int(U.Width) || NbPx.y >= int(U.Height))
        {
            continue;
        }

        float4 NbG0 = gGBufferCurr[uint3(uint2(NbPx), 0u)];
        if (NbG0.w < 0.0f)
        {
            continue;
        }
        float4 NbG1 = gGBufferCurr[uint3(uint2(NbPx), 1u)];
        if (dot(ShadeNorm, normalize(NbG1.xyz)) < 0.86f ||
            abs(dot(GeomNorm, WorldPos - NbG0.xyz)) > 0.16f ||
            abs(Mat.BaseRoughness - NbG1.w) > 0.28f)
        {
            continue;
        }

        float4 NbDi   = gReservoirRead[uint3(uint2(NbPx), 0u)];
        uint   NbCode = uint(NbDi.x + 0.5f);
        float  NbW    = NbDi.y;
        uint   NbM    = min(uint(NbDi.z + 0.5f), 12u);
        if (NbM > 0u && NbW > 0.0f)
        {
            float3 NbLDir;
            float  NbLDist;
            float3 NbLi  = EvaluateStoredDirectLightRadiance(U, NbCode, WorldPos, NbLDir, NbLDist);
            float  NdotL = max(dot(ShadeNorm, NbLDir), 0.0f);
            if (NdotL > 0.0f)
            {
                float3 Vis        = SearchInstancedShadowTransmittance(
                    gTlasCells, U.TlasCellCount, gInstances, gWideCells, gWideTriangles,
                    WorldPos + GeomNorm * 2.5e-3f, NbLDir, NbLDist);
                float  TargetHere = PerceptualLuminance(EvaluateScatteringBrdf(Mat, ShadeNorm, ViewDir, NbLDir) * NbLi * Vis * NdotL);
                float  CandW      = TargetHere * NbW * float(NbM);
                DiRunSum += CandW;
                DiTotalM += NbM;
                if (DiRunSum > 0.0f && NextUniform01(Rng) * DiRunSum < CandW)
                {
                    DiCode   = NbCode;
                    DiTarget = TargetHere;
                }
            }
        }

        if ((U.FeatureFlags & 1u) != 0u)
        {
            float4 NbGiLo = gReservoirRead[uint3(uint2(NbPx), 1u)];
            float4 NbGiHi = gReservoirRead[uint3(uint2(NbPx), 2u)];
            uint   NbGiM  = min(uint(floor(NbGiHi.w / 64.0f)), 8u);
            float  NbGiW  = max(NbGiHi.w - float(uint(floor(NbGiHi.w / 64.0f))) * 64.0f, 0.0f);
            if (NbGiM > 0u && NbGiW > 0.0f)
            {
                float3 Y       = NbGiLo.xyz;
                float3 NY      = UnpackOctNormalFromFloat16Safe(NbGiLo.w);
                float3 VecP    = Y - WorldPos;
                float3 VecQ    = Y - NbG0.xyz;
                float  DistP2  = max(dot(VecP, VecP), 1.0e-3f);
                float  DistQ2  = max(dot(VecQ, VecQ), 1.0e-3f);
                float  DistP   = sqrt(DistP2);
                float3 OmegaP  = VecP / DistP;
                float3 OmegaQ  = VecQ * rsqrt(DistQ2);
                float  CosPAtX = max(dot(ShadeNorm, OmegaP), 0.0f);
                if (CosPAtX > 1.0e-3f)
                {
                    float CosYAtP      = max(abs(dot(NY, -OmegaP)), 0.05f);
                    float CosYAtQ      = max(abs(dot(NY, -OmegaQ)), 0.05f);
                    float Jacobian     = clamp((CosYAtP * DistQ2) / max(CosYAtQ * DistP2, 1.0e-4f), 0.08f, 6.0f);
                    float GiVis        = (DistP < 24.0f)
                                             ? step(0.15f, TraceVolumeConeVisibilityGpu(gDistanceVolume, WorldPos + GeomNorm * 0.06f, OmegaP, min(DistP - 0.1f, 8.0f), 10.0f, 8u))
                                             : 1.0f;
                    float TargetGiHere = PerceptualLuminance(NbGiHi.xyz) * (CosPAtX * 0.318309886f) * GiVis;
                    float CandGiW      = TargetGiHere * Jacobian * NbGiW * float(NbGiM);
                    GiRunSum += CandGiW;
                    GiTotalM += NbGiM;
                    if (GiRunSum > 0.0f && NextUniform01(Rng) * GiRunSum < CandGiW)
                    {
                        GiPos    = Y;
                        GiNorm   = NY;
                        GiRad    = NbGiHi.xyz;
                        GiTarget = TargetGiHere;
                    }
                }
            }
        }
    }

    float FinalDiW = (DiTarget > 1.0e-6f && DiTotalM > 0u) ? min(DiRunSum / (float(DiTotalM) * DiTarget), 16.0f) : 0.0f;
    float FinalGiW = (GiTarget > 1.0e-6f && GiTotalM > 0u) ? min(GiRunSum / (float(GiTotalM) * GiTarget), 16.0f) : 0.0f;

    gReservoirWrite[uint3(Pixel, 0u)] = float4(float(DiCode), FinalDiW, float(min(DiTotalM, 24u)), DiTarget);
    gReservoirWrite[uint3(Pixel, 1u)] = float4(GiPos, PackOctNormalAsFloat16Safe(GiNorm));
    gReservoirWrite[uint3(Pixel, 2u)] = float4(GiRad, min(FinalGiW, 63.0f) + float(min(GiTotalM, 16u)) * 64.0f);

    float3 DirectDiffuse  = float3(0.0f, 0.0f, 0.0f);
    float3 DirectSpecular = float3(0.0f, 0.0f, 0.0f);
    if (FinalDiW > 0.0f)
    {
        float3 LDir;
        float  LDist;
        float3 Li    = EvaluateStoredDirectLightRadiance(U, DiCode, WorldPos, LDir, LDist);
        float  NdotL = max(dot(ShadeNorm, LDir), 0.0f);
        if (NdotL > 0.0f)
        {
            float3 Vis           = SearchInstancedShadowTransmittance(
                gTlasCells, U.TlasCellCount, gInstances, gWideCells, gWideTriangles,
                WorldPos + GeomNorm * 2.5e-3f, LDir, LDist);
            SplitBrdfTerms Split = EvaluateSplitScatteringBrdf(Mat, ShadeNorm, ViewDir, LDir);
            float3 CommonIrr     = Li * Vis * (NdotL * FinalDiW);
            DirectDiffuse        = Split.DiffuseTerm * CommonIrr;
            DirectSpecular       = Split.SpecularTerm * CommonIrr;
        }
    }

    float3 IndirectDiffuse = float3(0.0f, 0.0f, 0.0f);
    if ((U.FeatureFlags & 1u) != 0u && FinalGiW > 0.0f)
    {
        float3 GiOmega  = normalize(GiPos - WorldPos);
        float  CosGi    = max(dot(ShadeNorm, GiOmega), 0.0f);
        float  SdfAo    = EvaluateVolumeAmbientOcclusionGpu(gDistanceVolume, WorldPos + GeomNorm * 0.03f, ShadeNorm);
        float3 DiffRho  = Mat.BaseAlbedo * (1.0f - Mat.Metallic) * (1.0f - Mat.TransmissiveFraction) * Mat.AmbientOcclusion;
        IndirectDiffuse = DiffRho * GiRad * (CosGi * 0.318309886f * FinalGiW * lerp(0.45f, 1.0f, SdfAo));
    }

    float3 IndirectSpecular = float3(0.0f, 0.0f, 0.0f);
    float  ReflectionHitD   = 50.0f;
    if ((U.FeatureFlags & 4u) != 0u)
    {
        SampledSpecularReflection SpecSample = SampleBsdfSpecularMixtureVndf(
            Mat, ShadeNorm, ViewDir, NextUniform01(Rng), NextUniform01(Rng), NextUniform01(Rng));
        if (SpecSample.Valid)
        {
            bool   AllowSecond = (U.FeatureFlags & 128u) != 0u;
            float3 SpecEnv     = TraceSingleSpecularBounce(U, WorldPos, GeomNorm, SpecSample.RayDirection, AllowSecond, ReflectionHitD);
            IndirectSpecular   = SpecSample.ThroughputWeight * SpecEnv;
        }
    }

    float3 DemodAlbedo     = max(Mat.BaseAlbedo, float3(RepresentableAlbedoFloor, RepresentableAlbedoFloor, RepresentableAlbedoFloor));
    float3 TotalDiffuse    = DirectDiffuse + IndirectDiffuse;
    float3 DemodDiffuseIrr = min(TotalDiffuse / DemodAlbedo, float3(24.0f, 24.0f, 24.0f));
    float  DiffLum         = PerceptualLuminance(DemodDiffuseIrr);
    // 📝 Bound the single-sample specular estimate (the firefly clamp documented in SurfaceScatteringEvaluation):
    // a 2-frame temporal window cannot average a D ~ 1/(pi alpha^2) clearcoat peak, so an unbounded estimate
    // either clips a whole hood white or leaves it black.
    float3 TotalSpecular = DirectSpecular + IndirectSpecular;
    float  SpecLum       = PerceptualLuminance(TotalSpecular);
    if (SpecLum > 3.2f)
    {
        TotalSpecular *= 3.2f / SpecLum;
    }

    // Channel w carries the estimated per-pixel luminance variance: (single-sample luminance)^2,
    // which the à-trous filter turns into sigma = sqrt(E[L^2] - E[L]^2) over its tap window.
    gRadianceWrite[uint3(Pixel, 0u)] = float4(DemodDiffuseIrr, DiffLum * DiffLum);
    gRadianceWrite[uint3(Pixel, 1u)] = float4(min(TotalSpecular, float3(32.0f, 32.0f, 32.0f)), clamp(ReflectionHitD, 0.05f, 250.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                      KERNEL 5: SPLIT DIFFUSE / SPECULAR VARIANCE-GUIDED À-TROUS DENOISER
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void FilterSpatiotemporalAtrousMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    DecodedFrameUniforms U     = LoadFrameUniforms();
    uint2                Pixel = DispatchThreadId.xy;
    if (Pixel.x >= U.Width || Pixel.y >= U.Height)
    {
        return;
    }

    float4 G0   = gGBufferCurr[uint3(Pixel, 0u)];
    float4 Diff = gRadianceRead[uint3(Pixel, 0u)];
    float4 Spec = gRadianceRead[uint3(Pixel, 1u)];
    if (G0.w < 0.0f || U.AtrousStepWidth == 0u)
    {
        gRadianceWrite[uint3(Pixel, 0u)] = Diff;
        gRadianceWrite[uint3(Pixel, 1u)] = Spec;
        return;
    }

    float4 G1        = gGBufferCurr[uint3(Pixel, 1u)];
    float3 CenterPos = G0.xyz;
    float3 CenterN   = normalize(G1.xyz);
    float  Roughness = G1.w;

    float CenterDiffLum = PerceptualLuminance(Diff.xyz);

    // Local 3x3 luminance moments give sigma from real neighbour disagreement rather than a constant.
    float SumLum  = 0.0f;
    float SumLum2 = 0.0f;
    float MomentN = 0.0f;
    for (int Wz = -1; Wz <= 1; ++Wz)
    {
        for (int Wx = -1; Wx <= 1; ++Wx)
        {
            int2 WinPx = clamp(int2(Pixel) + int2(Wx, Wz), int2(0, 0), int2(int(U.Width) - 1, int(U.Height) - 1));
            float TapL = PerceptualLuminance(gRadianceRead[uint3(uint2(WinPx), 0u)].xyz);
            SumLum  += TapL;
            SumLum2 += TapL * TapL;
            MomentN += 1.0f;
        }
    }
    const float MeanLum  = SumLum / max(MomentN, 1.0f);
    const float MeanLum2 = SumLum2 / max(MomentN, 1.0f);
    float DiffStdDev     = sqrt(max(MeanLum2 - MeanLum * MeanLum, 0.0f));
    DiffStdDev           = max(DiffStdDev, 0.035f + 0.12f * CenterDiffLum);
    float CenterSpecLum = PerceptualLuminance(Spec.xyz);
    float SpecHitDist   = Spec.w;

    bool  BlurSpecular  = (Roughness >= 0.065f);
    float SpecNormalExp = lerp(256.0f, 24.0f, saturate(Roughness));
    float SpecSigmaL    = max(0.35f, CenterSpecLum * lerp(0.35f, 1.5f, saturate(Roughness)));

    static const float Kernel1D[5] = { 1.0f / 16.0f, 1.0f / 4.0f, 3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f };

    float3 AccumDiff  = float3(0.0f, 0.0f, 0.0f);
    float  AccumMom2  = 0.0f;
    float  WeightDiff = 0.0f;

    float3 AccumSpec  = BlurSpecular ? float3(0.0f, 0.0f, 0.0f) : Spec.xyz;
    float  WeightSpec = BlurSpecular ? 0.0f : 1.0f;

    int Step = int(U.AtrousStepWidth);
    for (int Ky = -2; Ky <= 2; ++Ky)
    {
        for (int Kx = -2; Kx <= 2; ++Kx)
        {
            int2 Tap = clamp(int2(Pixel) + int2(Kx * Step, Ky * Step),
                             int2(0, 0),
                             int2(int(U.Width) - 1, int(U.Height) - 1));

            float4 TapG0 = gGBufferCurr[uint3(uint2(Tap), 0u)];
            if (TapG0.w < 0.0f)
            {
                continue;
            }
            float4 TapG1     = gGBufferCurr[uint3(uint2(Tap), 1u)];
            float3 TapN      = normalize(TapG1.xyz);
            float  PlaneDist = abs(dot(CenterN, TapG0.xyz - CenterPos));
            if (PlaneDist > 0.25f)
            {
                continue;
            }

            float BaseKernel = Kernel1D[Kx + 2] * Kernel1D[Ky + 2];
            float CosN       = max(dot(CenterN, TapN), 0.0f);
            float PlaneW     = exp(-PlaneDist * 14.0f);

            float4 TapDiff  = gRadianceRead[uint3(uint2(Tap), 0u)];
            float  TapDiffL = PerceptualLuminance(TapDiff.xyz);
            float  WDiff    = BaseKernel * pow(CosN, 32.0f) * PlaneW *
                              exp(-abs(TapDiffL - CenterDiffLum) / (1.9f * DiffStdDev + 0.06f));
            AccumDiff += TapDiff.xyz * WDiff;
            AccumMom2 += TapDiff.w * WDiff;
            WeightDiff += WDiff;

            if (BlurSpecular && abs(TapG1.w - Roughness) < 0.20f)
            {
                float4 TapSpec  = gRadianceRead[uint3(uint2(Tap), 1u)];
                float  TapSpecL = PerceptualLuminance(TapSpec.xyz);
                float  HitMatch = exp(-abs(TapSpec.w - SpecHitDist) / max(0.35f * SpecHitDist + 0.5f, 0.5f));
                float  WSpec    = BaseKernel * pow(CosN, SpecNormalExp) * PlaneW * HitMatch *
                                  exp(-abs(TapSpecL - CenterSpecLum) / SpecSigmaL);
                AccumSpec += TapSpec.xyz * WSpec;
                WeightSpec += WSpec;
            }
        }
    }

    float3 OutDiff = (WeightDiff > 1.0e-5f) ? (AccumDiff / WeightDiff) : Diff.xyz;
    float  OutMom2 = (WeightDiff > 1.0e-5f) ? (AccumMom2 / WeightDiff) : Diff.w;
    float3 OutSpec = (WeightSpec > 1.0e-5f) ? (AccumSpec / WeightSpec) : Spec.xyz;

    gRadianceWrite[uint3(Pixel, 0u)] = float4(OutDiff, OutMom2);
    gRadianceWrite[uint3(Pixel, 1u)] = float4(OutSpec, SpecHitDist);
}

//------------------------------------------------------------------------------------------------------------------------
//      KERNEL 6: REMODULATION, ENVIRONMENT COUPLING, 15 INSPECTION MODES, & ZERO-COPY DISPLAY
//------------------------------------------------------------------------------------------------------------------------

float3 ApplyAerialFogAndDust(const DecodedFrameUniforms U, float3 LinearRadiance, float3 RayDir, float SegmentMeters)
{
    if ((U.FeatureFlags & 32u) == 0u || (U.FogBaseDensity <= 1.0e-6f && U.FogDustBoost <= 1.0e-6f))
    {
        return LinearRadiance;
    }

    float  MaxSeg     = min(SegmentMeters, 600.0f);
    float  MidY       = max(U.CameraOrigin.y + RayDir.y * (0.4f * MaxSeg), 0.0f);
    float  HeightTerm = exp(-MidY * 0.035f);
    float  SigmaExt   = (U.FogBaseDensity + U.FogDustBoost * 0.008f) * HeightTerm;
    float  Trans      = exp(-SigmaExt * MaxSeg);

    float3 SunDir    = normalize(U.SunDirection);
    float  CosSun    = dot(RayDir, SunDir);
    float  G         = 0.58f;
    float  Phase     = (1.0f - G * G) / (12.56637f * pow(max(1.0f + G * G - 2.0f * G * CosSun, 1.0e-3f), 1.5f));
    float3 DustTint  = lerp(float3(0.62f, 0.70f, 0.82f), float3(0.84f, 0.68f, 0.48f), saturate(U.FogDustBoost));
    float3 InScatter = DustTint * (EvaluatePlanetarySkyRadiance(U, RayDir, false) * 0.65f +
                                   U.SunRadianceTint * U.SunIrradianceLux * min(Phase, 2.5f) * 0.18f);
    return LinearRadiance * Trans + InScatter * (1.0f - Trans);
}

float3 EvaluateScreenOpticalLensFlare(const DecodedFrameUniforms U, uint2 Pixel)
{
    if ((U.FeatureFlags & 64u) == 0u || U.FlareIntensity <= 1.0e-4f || U.SunIrradianceLux <= 1.0e-4f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 SunDir = normalize(U.SunDirection);
    float  Fwd    = dot(SunDir, U.CameraForward);
    if (Fwd <= 0.08f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float2 SunNdc = float2(dot(SunDir, U.CameraRight) / (Fwd * max(U.TanHalfVerticalFov * U.AspectRatio, 1.0e-5f)),
                           dot(SunDir, U.CameraUp) / (Fwd * max(U.TanHalfVerticalFov, 1.0e-5f)));
    if (abs(SunNdc.x) > 1.35f || abs(SunNdc.y) > 1.35f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 SunVis    = SearchInstancedShadowTransmittance(
        gTlasCells, U.TlasCellCount, gInstances, gWideCells, gWideTriangles, U.CameraOrigin, SunDir, 1.0e4f);
    float  VisFactor = PerceptualLuminance(SunVis);
    if (VisFactor <= 0.02f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float2 Uv  = (float2(Pixel) + 0.5f) / float2(float(U.Width), float(U.Height));
    float2 Ndc = float2(Uv.x * 2.0f - 1.0f, 1.0f - Uv.y * 2.0f);
    float2 Vec = Ndc - SunNdc;
    Vec.x *= U.AspectRatio;
    float Dist = length(Vec);
    float Ang  = atan2(Vec.y, Vec.x);

    float  Burst  = pow(abs(cos(Ang * 3.0f)), 18.0f) * exp(-Dist * 7.5f) * 0.35f;
    float  Halo   = exp(-pow((Dist - 0.38f) / 0.035f, 2.0f)) * 0.08f;
    float3 Ghosts = float3(0.0f, 0.0f, 0.0f);
    static const float GhostScales[3] = { -0.45f, -0.95f, -1.45f };
    static const float GhostRadii[3]  = {  0.07f,  0.12f,  0.055f };
    [unroll]
    for (int G = 0; G < 3; ++G)
    {
        float2 GPos = SunNdc * GhostScales[G];
        float2 GDel = Ndc - GPos;
        GDel.x *= U.AspectRatio;
        float  GD   = length(GDel);
        float  Disc = smoothstep(GhostRadii[G], GhostRadii[G] * 0.55f, GD);
        float3 Tint = (G == 0) ? float3(1.0f, 0.55f, 0.35f) : ((G == 1) ? float3(0.35f, 0.85f, 1.0f) : float3(0.65f, 0.45f, 1.0f));
        Ghosts += Tint * (Disc * 0.045f);
    }

    float EdgeFade = saturate(1.25f - max(abs(SunNdc.x), abs(SunNdc.y)));
    return (U.SunRadianceTint * (Burst + Halo) + Ghosts) * (U.FlareIntensity * VisFactor * EdgeFade);
}

float3 GoldenRatioPaletteColor(uint Identifier)
{
    float  Hue = frac(float(Identifier) * 0.61803398875f + 0.13f);
    float3 Rgb = saturate(abs(fmod(Hue * 6.0f + float3(0.0f, 4.0f, 2.0f), 6.0f) - 3.0f) - 1.0f);
    return lerp(float3(0.22f, 0.24f, 0.28f), Rgb, 0.78f);
}

float3 AcesFilmicTonemap(float3 LinearHdr, float Exposure)
{
    float3 X     = max(LinearHdr * Exposure, float3(0.0f, 0.0f, 0.0f));
    float3 Num   = X * (2.51f * X + 0.03f);
    float3 Denom = X * (2.43f * X + 0.59f) + 0.14f;
    return saturate(Num / Denom);
}

float3 LinearToSrgbExact(float3 LinearRgb)
{
    float3 Clamped = saturate(LinearRgb);
    float3 Lo      = Clamped * 12.92f;
    float3 Hi      = 1.055f * pow(max(Clamped, float3(1.0e-5f, 1.0e-5f, 1.0e-5f)), 1.0f / 2.4f) - 0.055f;
    return float3(Clamped.r <= 0.0031308f ? Lo.r : Hi.r,
                  Clamped.g <= 0.0031308f ? Lo.g : Hi.g,
                  Clamped.b <= 0.0031308f ? Lo.b : Hi.b);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ResolveSpatiotemporalDisplayMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    DecodedFrameUniforms U     = LoadFrameUniforms();
    uint2                Pixel = DispatchThreadId.xy;
    if (Pixel.x >= U.Width || Pixel.y >= U.Height)
    {
        return;
    }

    float4 G0     = gGBufferCurr[uint3(Pixel, 0u)];
    float3 CamRay = GeneratePrimaryCameraRay(U, Pixel);

    float3 FinalLinearHdr = float3(0.0f, 0.0f, 0.0f);
    bool   BypassTonemap  = false;
    float3 DiagnosticSrgb = float3(0.0f, 0.0f, 0.0f);

    if (G0.w < 0.0f)
    {
        float3 SkyRay  = normalize(G0.xyz);
        FinalLinearHdr = EvaluatePlanetarySkyRadiance(U, SkyRay, true);
        if (G0.w < -1.5f)
        {
            float3 GlassTint  = gGBufferCurr[uint3(Pixel, 2u)].xyz;
            float  GlassF     = gGBufferCurr[uint3(Pixel, 3u)].z;
            float4 GlassLayer = gGBufferCurr[uint3(Pixel, 5u)];
            float3 ReflDir    = UnpackOctNormalFromFloat16Safe(GlassLayer.w);
            float  DummyDist  = 100.0f;
            float3 GlassRefl  = ((U.FeatureFlags & 4u) != 0u)
                                    ? TraceSingleSpecularBounce(U, GlassLayer.xyz, gGBufferCurr[uint3(Pixel, 1u)].xyz, ReflDir, false, DummyDist)
                                    : EvaluatePlanetarySkyRadiance(U, ReflDir, true);
            FinalLinearHdr    = FinalLinearHdr * GlassTint + GlassRefl * GlassF;
        }
        FinalLinearHdr = ApplyAerialFogAndDust(U, FinalLinearHdr, CamRay, 450.0f);
    }
    else
    {
        float4 G1    = gGBufferCurr[uint3(Pixel, 1u)];
        float4 G2    = gGBufferCurr[uint3(Pixel, 2u)];
        float4 G3    = gGBufferCurr[uint3(Pixel, 3u)];
        float4 G4    = gGBufferCurr[uint3(Pixel, 4u)];
        float4 G5    = gGBufferCurr[uint3(Pixel, 5u)];
        uint   Class = uint(G3.x + 0.5f);

        SurfaceScatteringMaterial Mat = UnpackScatteringSlot(gMaterialTable, Class);
        float3 DemodAlbedo            = max(SanitizeRadiance(G2.xyz),
                                           float3(RepresentableAlbedoFloor, RepresentableAlbedoFloor, RepresentableAlbedoFloor));
        float3 FilteredIrradiance     = SanitizeRadiance(gRadianceRead[uint3(Pixel, 0u)].xyz);
        float3 FilteredSpecular       = SanitizeRadiance(gRadianceRead[uint3(Pixel, 1u)].xyz);
        float3 Emissive               = Mat.EmissionRadiance * Mat.EmissionStrength;

        float3 SurfaceRadiance = FilteredIrradiance * DemodAlbedo + FilteredSpecular + Emissive;

        if (G3.z > 1.0e-3f)
        {
            float3 GlassReflDir = UnpackOctNormalFromFloat16Safe(G5.w);
            float  DummyDist    = 100.0f;
            float3 GlassRefl    = ((U.FeatureFlags & 4u) != 0u)
                                      ? TraceSingleSpecularBounce(U, G5.xyz, normalize(G1.xyz), GlassReflDir, false, DummyDist)
                                      : EvaluatePlanetarySkyRadiance(U, GlassReflDir, true);
            SurfaceRadiance += GlassRefl * G3.z;
        }

        FinalLinearHdr = ApplyAerialFogAndDust(U, SurfaceRadiance, CamRay, G0.w);

        uint Mode = U.InspectionMode;
        if (Mode == 1u) // Clay
        {
            FinalLinearHdr = FilteredIrradiance * float3(0.58f, 0.59f, 0.61f) + FilteredSpecular * 0.25f;
        }
        else if (Mode == 2u) // Wireframe
        {
            uint2 RightPx  = uint2(min(Pixel.x + 1u, U.Width - 1u), Pixel.y);
            uint2 DownPx   = uint2(Pixel.x, min(Pixel.y + 1u, U.Height - 1u));
            float EdgeHash = abs(G4.w - gGBufferCurr[uint3(RightPx, 4u)].w) + abs(G4.w - gGBufferCurr[uint3(DownPx, 4u)].w);
            float EdgeNorm = 1.0f - min(dot(normalize(G1.xyz), normalize(gGBufferCurr[uint3(RightPx, 1u)].xyz)),
                                        dot(normalize(G1.xyz), normalize(gGBufferCurr[uint3(DownPx, 1u)].xyz)));
            float Wire     = saturate(step(0.5f, EdgeHash) * 0.75f + smoothstep(0.02f, 0.12f, EdgeNorm));
            FinalLinearHdr = lerp(FinalLinearHdr * 0.45f, float3(0.85f, 0.92f, 1.0f), Wire);
        }
        else if (Mode == 3u) // BaseColor
        {
            BypassTonemap  = true;
            DiagnosticSrgb = LinearToSrgbExact(G2.xyz);
        }
        else if (Mode == 4u) // Roughness
        {
            BypassTonemap  = true;
            DiagnosticSrgb = float3(G1.w, G1.w, G1.w);
        }
        else if (Mode == 5u) // Metallic
        {
            BypassTonemap  = true;
            DiagnosticSrgb = float3(G2.w, G2.w, G2.w);
        }
        else if (Mode == 6u) // WorldNormals
        {
            BypassTonemap  = true;
            DiagnosticSrgb = normalize(G1.xyz) * 0.5f + 0.5f;
        }
        else if (Mode == 7u) // AmbientOcclusion
        {
            float Ao       = G3.y * EvaluateVolumeAmbientOcclusionGpu(gDistanceVolume, G0.xyz + normalize(G1.xyz) * 0.04f, normalize(G1.xyz));
            BypassTonemap  = true;
            DiagnosticSrgb = float3(Ao, Ao, Ao);
        }
        else if (Mode == 8u) // Reflections
        {
            FinalLinearHdr = FilteredSpecular;
        }
        else if (Mode == 9u) // GlobalIllumination
        {
            float4 GiHi    = gReservoirWrite[uint3(Pixel, 2u)];
            FinalLinearHdr = GiHi.xyz * DemodAlbedo +
                             QuerySurfaceCardExitanceGpu(gCardDescriptors, gCardExitance, U.SurfaceCardCount, G0.xyz, normalize(G1.xyz)) * 0.5f;
        }
        else if (Mode == 10u) // Shadows
        {
            float3 Vis     = SearchInstancedShadowTransmittance(
                gTlasCells, U.TlasCellCount, gInstances, gWideCells, gWideTriangles,
                G0.xyz + UnpackOctNormalFromFloat16Safe(G4.z) * 3.0e-3f, normalize(U.SunDirection), 1.0e4f);
            BypassTonemap  = true;
            DiagnosticSrgb = Vis * EvaluateProjectedCloudGroundShadow(U, G0.xyz);
        }
        else if (Mode == 11u) // MotionVectors
        {
            float2 DeltaPx = (float2(Pixel) - G4.xy) * 0.05f;
            BypassTonemap  = true;
            DiagnosticSrgb = float3(saturate(0.5f + DeltaPx.x), saturate(0.5f + DeltaPx.y), saturate(length(DeltaPx)));
        }
        else if (Mode == 12u) // Clusters
        {
            uint   ClusterId = uint(G4.w + 0.5f);
            uint   TierId    = uint(G3.w + 0.5f) & 3u;
            float3 ClusterC  = GoldenRatioPaletteColor(ClusterId);
            float3 TierTint  = (TierId == 0u) ? float3(1.0f, 1.0f, 1.0f) : ((TierId == 1u) ? float3(0.85f, 0.92f, 1.0f) : float3(0.70f, 0.72f, 0.80f));
            float  Shade     = 0.35f + 0.65f * saturate(dot(normalize(G1.xyz), normalize(U.SunDirection)) * 0.5f + 0.5f);
            BypassTonemap    = true;
            DiagnosticSrgb   = saturate(ClusterC * TierTint * Shade);
        }
        else if (Mode == 13u) // FogDensity
        {
            float FogAmount = 1.0f - exp(-(max(U.FogBaseDensity, 0.002f) + U.FogDustBoost * 0.01f) * min(G0.w, 300.0f));
            BypassTonemap   = true;
            DiagnosticSrgb  = lerp(float3(0.08f, 0.12f, 0.22f), float3(0.95f, 0.76f, 0.42f), FogAmount);
        }
        else if (Mode == 14u) // Overdraw / SDF+BVH Cascade Cost Heatmap
        {
            uint  Casc     = (uint(G3.w + 0.5f) >> 2u) & 3u;
            float Cost     = saturate(G0.w / 80.0f) * 0.5f + float(3u - Casc) * 0.16f + (G3.z > 0.0f ? 0.35f : 0.0f);
            BypassTonemap  = true;
            DiagnosticSrgb = lerp(float3(0.12f, 0.38f, 0.86f), float3(0.96f, 0.28f, 0.18f), saturate(Cost));
        }
    }

    if (U.InspectionMode == 0u)
    {
        FinalLinearHdr += EvaluateScreenOpticalLensFlare(U, Pixel);
    }

    FinalLinearHdr = SanitizeRadiance(FinalLinearHdr);
    if (!IsFiniteVec3(DiagnosticSrgb))
    {
        BypassTonemap  = true;
        DiagnosticSrgb = float3(0.0f, 0.0f, 0.0f);
    }

    float3 StoredLinear = BypassTonemap ? pow(saturate(DiagnosticSrgb), 2.2f) : FinalLinearHdr;
    gRadianceWrite[uint3(Pixel, 0u)] = float4(StoredLinear, 1.0f);

    float3 DisplaySrgb = BypassTonemap
                             ? DiagnosticSrgb
                             : LinearToSrgbExact(AcesFilmicTonemap(FinalLinearHdr, max(U.ExposureScale, 0.05f)));

    uint  DitherSeed = (Pixel.x * 1664525u + Pixel.y * 1013904223u + U.FrameIndex * 374761393u);
    float TriDither  = (float(DitherSeed & 255u) + float((DitherSeed >> 8u) & 255u)) * (1.0f / 255.0f) - 1.0f;
    DisplaySrgb      = saturate(DisplaySrgb + TriDither * (0.75f / 255.0f));

    gDisplayRgba8[uint3(Pixel, 0u)] = float4(DisplaySrgb, 1.0f);
}
