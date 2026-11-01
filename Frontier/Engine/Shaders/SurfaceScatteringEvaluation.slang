//==========================================================================================================================================
//                                           SURFACE SCATTERING EVALUATION
//==========================================================================================================================================
// 🧩 Slang GPU counterpart of Engine/Render/ScatteringSequence.h + SurfaceMaterial.h + AutomotivePaint.h:
//    - Authored TextureStudio PBR texture array evaluation (Albedo, tangent-space Normal, ORM) with 3-tier Material LOD
//    - Multi-stratum anisotropic Smith-GGX basecoat, discrete Voronoi automotive metallic paint micro-flakes,
//      Estevez-Kulla Charlie fabric sheen, and Beer-Lambert absorbing polyurethane clearcoat
//    - Heitz 2018 VNDF specular reflection importance sampling & Fresnel clearcoat/basecoat lobe mixture PDF
//    - Exact unpolarized dielectric Fresnel (R_s, R_p) + Total Internal Reflection (TIR) boundary evaluation

#pragma once

static const uint MaterialSlotFloat4Stride = 8u;

struct UnpackedSurfaceMaterial
{
    float3 BaseAlbedo;
    float  BaseRoughness;
    float  Metallic;
    float  BaseIor;
    float  CoatCoverage;
    float  CoatRoughness;
    float3 CoatOpticalDepth;
    float  CoatIor;
    float3 SheenTint;
    float  SheenIntensity;
    float3 SelfRadiance;
    float  SheenRoughness;
    float  FlakeCoverage;
    float  FlakeFrequency;
    bool   TransmissiveGlass;
    float  GlassIor;
    float  Anisotropy;
    float  AnisotropyRotation;
    float  FlakeTiltSpread;
    float  UvTilingScale;
    int    AlbedoLayer;
    int    NormalLayer;
    int    OrmLayer;
    float  NormalStrength;
    float  AmbientOcclusion;
    uint   MaterialLodTier;
};

UnpackedSurfaceMaterial FetchSurfaceMaterial(StructuredBuffer<float4> MaterialTable, uint ClassId)
{
    uint   Clamped = min(ClassId, 63u);
    uint   Base    = Clamped * MaterialSlotFloat4Stride;
    float4 M0      = MaterialTable[Base + 0u];
    float4 M1      = MaterialTable[Base + 1u];
    float4 M2      = MaterialTable[Base + 2u];
    float4 M3      = MaterialTable[Base + 3u];
    float4 M4      = MaterialTable[Base + 4u];
    float4 M5      = MaterialTable[Base + 5u];
    float4 M6      = MaterialTable[Base + 6u];
    float4 M7      = MaterialTable[Base + 7u];

    UnpackedSurfaceMaterial Out;
    Out.BaseAlbedo         = M0.xyz;
    Out.BaseRoughness      = clamp(M0.w, 0.045f, 1.0f);
    Out.Metallic           = saturate(M1.x);
    Out.BaseIor            = max(1.02f, M1.y);
    Out.CoatCoverage       = saturate(M1.z);
    Out.CoatRoughness      = clamp(M1.w, 0.045f, 1.0f);
    Out.CoatOpticalDepth   = max(float3(0.0f), M2.xyz);
    Out.CoatIor            = max(1.02f, M2.w);
    Out.SheenTint          = max(float3(0.0f), M3.xyz);
    Out.SheenIntensity     = saturate(M3.w);
    Out.SelfRadiance       = max(float3(0.0f), M4.xyz);
    Out.SheenRoughness     = clamp(M4.w, 0.05f, 1.0f);
    Out.FlakeCoverage      = saturate(M5.x);
    Out.FlakeFrequency     = max(1.0f, M5.y);
    Out.TransmissiveGlass  = (M5.z > 0.5f);
    Out.GlassIor           = max(1.02f, M5.w);
    Out.Anisotropy         = clamp(M6.x, -0.9f, 0.9f);
    Out.AnisotropyRotation = M6.y;
    Out.FlakeTiltSpread    = clamp(M6.z, 0.0f, 0.5f);
    Out.UvTilingScale      = max(0.05f, M6.w);
    Out.AlbedoLayer        = int(floor(M7.x + 0.5f));
    Out.NormalLayer        = int(floor(M7.y + 0.5f));
    Out.OrmLayer           = int(floor(M7.z + 0.5f));
    Out.NormalStrength     = clamp(M7.w, 0.0f, 4.0f);
    Out.AmbientOcclusion   = 1.0f;
    Out.MaterialLodTier    = 0u;
    return Out;
}

// Applies 3-tier Material LOD + authored TextureStudio PBR maps + discrete Voronoi automotive paint micro-flakes
void ResolveAuthoredMaterialAtHit(inout UnpackedSurfaceMaterial Mat,
                                  Sampler2DArray                MaterialMaps,
                                  float3                        WorldPos,
                                  float2                        SurfaceUv,
                                  float                         HitDistanceM,
                                  float3                        GeoNormal,
                                  float3                        WorldTangent,
                                  out float3                    OutShadingNormal,
                                  out float3                    OutRotatedTangent,
                                  out float3                    OutRotatedBitangent)
{
    uint Tier       = (HitDistanceM < 18.0f) ? 0u : ((HitDistanceM < 52.0f) ? 1u : 2u);
    Mat.MaterialLodTier = Tier;

    float3 N = normalize(GeoNormal);
    float3 T = WorldTangent - N * dot(N, WorldTangent);
    T        = (length(T) > 1.0e-5f)
                   ? normalize(T)
                   : normalize(cross(abs(N.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f), N));
    float3 B = cross(N, T);

    float2 TiledUv = SurfaceUv * Mat.UvTilingScale;
    float  AnalyticMip = clamp(log2(max(1.0f, HitDistanceM * 0.16f * Mat.UvTilingScale)), 0.0f, 8.0f);

    if (Tier <= 1u)
    {
        if (Mat.AlbedoLayer >= 0)
        {
            float3 SampledAlbedo = MaterialMaps.SampleLevel(float3(TiledUv, float(Mat.AlbedoLayer)), AnalyticMip).rgb;
            Mat.BaseAlbedo       = clamp(Mat.BaseAlbedo * SampledAlbedo, float3(0.0f), float3(0.99f));
        }
        if (Mat.OrmLayer >= 0)
        {
            float3 Orm           = MaterialMaps.SampleLevel(float3(TiledUv, float(Mat.OrmLayer)), AnalyticMip).rgb;
            Mat.AmbientOcclusion = clamp(Orm.x, 0.15f, 1.0f);
            Mat.BaseRoughness    = clamp(Mat.BaseRoughness * lerp(0.65f, 1.35f, Orm.y), 0.045f, 1.0f);
            Mat.Metallic         = saturate(Mat.Metallic * Orm.z);
        }
    }

    if (Tier == 0u && Mat.NormalLayer >= 0 && Mat.NormalStrength > 1.0e-3f)
    {
        float3 EncodedN = MaterialMaps.SampleLevel(float3(TiledUv, float(Mat.NormalLayer)), AnalyticMip).rgb;
        float2 TanXY    = (EncodedN.xy * 2.0f - 1.0f) * Mat.NormalStrength * saturate(1.0f - AnalyticMip * 0.18f);
        float  TanZ     = sqrt(max(0.04f, 1.0f - dot(TanXY, TanXY)));
        N               = normalize(T * TanXY.x + B * TanXY.y + N * TanZ);
        T               = normalize(T - N * dot(N, T));
        B               = cross(N, T);
    }

    // Distance-filtered Voronoi micro-flake normal for secondary/compute fallback hits
    if (Tier == 0u && Mat.FlakeCoverage > 0.01f)
    {
        float CellsPerCone = HitDistanceM * 0.0022f * Mat.FlakeFrequency;
        float FlakeResolve = 1.0f - smoothstep(0.45f, 2.25f, CellsPerCone);
        if (FlakeResolve > 0.02f)
        {
            int3  FlakeCell = int3(floor(WorldPos * Mat.FlakeFrequency));
            uint  H         = uint(FlakeCell.x * 73856093) ^ uint(FlakeCell.y * 19349663) ^ uint(FlakeCell.z * 83492791);
            H               = (H ^ (H >> 13u)) * 1274126177u;
            float Sparkle   = float(H & 0xFFFFu) / 65535.0f;
            Mat.BaseAlbedo *= lerp(1.0f, lerp(0.96f, 1.06f, smoothstep(1.0f - Mat.FlakeCoverage, 1.0f, Sparkle)), FlakeResolve);

            if (Mat.FlakeTiltSpread > 1.0e-3f)
            {
                float2 Jitter = (float2(float((H >> 8u) & 0xFFu), float((H >> 16u) & 0xFFu)) / 255.0f) * 2.0f - 1.0f;
                N             = normalize(N + (T * Jitter.x + B * Jitter.y) * (Mat.FlakeTiltSpread * FlakeResolve));
                T             = normalize(T - N * dot(N, T));
                B             = cross(N, T);
            }
        }
        float UnresolvedVar = (1.0f - FlakeResolve) * 0.65f * Mat.FlakeTiltSpread * Mat.FlakeTiltSpread;
        Mat.BaseRoughness   = clamp(sqrt(Mat.BaseRoughness * Mat.BaseRoughness + UnresolvedVar), 0.045f, 1.0f);
    }
    else if (Tier == 2u)
    {
        // Far-tier specular anti-aliasing: widen micro-roughness slightly and suppress sub-pixel anisotropy
        Mat.BaseRoughness = clamp(sqrt(Mat.BaseRoughness * Mat.BaseRoughness + 0.02f), 0.08f, 1.0f);
        Mat.Anisotropy    = 0.0f;
    }

    float CosR          = cos(Mat.AnisotropyRotation);
    float SinR          = sin(Mat.AnisotropyRotation);
    OutShadingNormal    = N;
    OutRotatedTangent   = CosR * T + SinR * B;
    OutRotatedBitangent = -SinR * T + CosR * B;
}

// Hardware fragment quad derivative (ddx/ddy) trilinear mipmap + LEAN/Toksvig micro-flake & curvature specular AA resolver
void ResolveAuthoredMaterialHardwareDerivatives(inout UnpackedSurfaceMaterial Mat,
                                                Sampler2DArray                MaterialMaps,
                                                float3                        WorldPos,
                                                float3                        WorldPosDdx,
                                                float3                        WorldPosDdy,
                                                float2                        SurfaceUv,
                                                float2                        SurfaceUvDdx,
                                                float2                        SurfaceUvDdy,
                                                float                         HitDistanceM,
                                                float3                        GeoNormal,
                                                float3                        GeoNormalDdx,
                                                float3                        GeoNormalDdy,
                                                float3                        WorldTangent,
                                                out float                     OutHardwareMipLod,
                                                out float3                    OutShadingNormal,
                                                out float3                    OutRotatedTangent,
                                                out float3                    OutRotatedBitangent)
{
    uint Tier           = (HitDistanceM < 18.0f) ? 0u : ((HitDistanceM < 52.0f) ? 1u : 2u);
    Mat.MaterialLodTier = Tier;

    float3 N = normalize(GeoNormal);
    float3 T = WorldTangent - N * dot(N, WorldTangent);
    T        = (length(T) > 1.0e-5f)
                   ? normalize(T)
                   : normalize(cross(abs(N.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f), N));
    float3 B = cross(N, T);

    float2 TiledUv  = SurfaceUv * Mat.UvTilingScale;
    float2 GradX    = SurfaceUvDdx * Mat.UvTilingScale;
    float2 GradY    = SurfaceUvDdy * Mat.UvTilingScale;
    float2 TexGradX = GradX * 256.0f;
    float2 TexGradY = GradY * 256.0f;
    float  MaxRhoSq = max(dot(TexGradX, TexGradX), dot(TexGradY, TexGradY));
    OutHardwareMipLod = clamp(0.5f * log2(max(1.0e-8f, MaxRhoSq)), 0.0f, 8.0f);

    if (Mat.AlbedoLayer >= 0)
    {
        float3 SampledAlbedo = MaterialMaps.SampleGrad(float3(TiledUv, float(Mat.AlbedoLayer)), GradX, GradY).rgb;
        Mat.BaseAlbedo       = clamp(Mat.BaseAlbedo * SampledAlbedo, float3(0.0f), float3(0.99f));
    }
    if (Mat.OrmLayer >= 0)
    {
        float3 Orm           = MaterialMaps.SampleGrad(float3(TiledUv, float(Mat.OrmLayer)), GradX, GradY).rgb;
        Mat.AmbientOcclusion = clamp(Orm.x, 0.15f, 1.0f);
        Mat.BaseRoughness    = clamp(Mat.BaseRoughness * lerp(0.65f, 1.35f, Orm.y), 0.045f, 1.0f);
        Mat.Metallic         = saturate(Mat.Metallic * Orm.z);
    }

    if (Tier <= 1u && Mat.NormalLayer >= 0 && Mat.NormalStrength > 1.0e-3f)
    {
        float3 EncodedN   = MaterialMaps.SampleGrad(float3(TiledUv, float(Mat.NormalLayer)), GradX, GradY).rgb;
        float  MipFade    = saturate(1.0f - OutHardwareMipLod * 0.16f);
        float2 TanXY      = (EncodedN.xy * 2.0f - 1.0f) * (Mat.NormalStrength * MipFade);
        float  NormalLen2 = dot(TanXY, TanXY);
        float  TanZ       = sqrt(max(0.04f, 1.0f - NormalLen2));
        N                 = normalize(T * TanXY.x + B * TanXY.y + N * TanZ);
        T                 = normalize(T - N * dot(N, T));
        B                 = cross(N, T);
        // Toksvig mip normal-length variance folding into base roughness
        float UnresolvedNormalVar = (1.0f - MipFade) * 0.035f * Mat.NormalStrength * Mat.NormalStrength;
        Mat.BaseRoughness         = clamp(sqrt(Mat.BaseRoughness * Mat.BaseRoughness + UnresolvedNormalVar), 0.045f, 1.0f);
    }

    // Hardware ddx(WorldPos)/ddy(WorldPos) pixel-extent filtering for discrete Voronoi automotive paint micro-flakes
    if (Mat.FlakeCoverage > 0.01f)
    {
        float PixelExtentM  = max(length(WorldPosDdx), length(WorldPosDdy));
        float CellsPerPixel = PixelExtentM * Mat.FlakeFrequency;
        float FlakeResolve  = (Tier == 0u) ? (1.0f - smoothstep(0.42f, 2.15f, CellsPerPixel)) : 0.0f;

        if (FlakeResolve > 0.02f)
        {
            int3  FlakeCell = int3(floor(WorldPos * Mat.FlakeFrequency));
            uint  H         = uint(FlakeCell.x * 73856093) ^ uint(FlakeCell.y * 19349663) ^ uint(FlakeCell.z * 83492791);
            H               = (H ^ (H >> 13u)) * 1274126177u;
            float Sparkle   = float(H & 0xFFFFu) / 65535.0f;
            Mat.BaseAlbedo *= lerp(1.0f, lerp(0.96f, 1.06f, smoothstep(1.0f - Mat.FlakeCoverage, 1.0f, Sparkle)), FlakeResolve);

            if (Mat.FlakeTiltSpread > 1.0e-3f)
            {
                float2 Jitter = (float2(float((H >> 8u) & 0xFFu), float((H >> 16u) & 0xFFu)) / 255.0f) * 2.0f - 1.0f;
                N             = normalize(N + (T * Jitter.x + B * Jitter.y) * (Mat.FlakeTiltSpread * FlakeResolve));
                T             = normalize(T - N * dot(N, T));
                B             = cross(N, T);
            }
        }
        float UnresolvedFlakeVar = (1.0f - FlakeResolve) * 0.65f * Mat.FlakeTiltSpread * Mat.FlakeTiltSpread;
        Mat.BaseRoughness        = clamp(sqrt(Mat.BaseRoughness * Mat.BaseRoughness + UnresolvedFlakeVar), 0.045f, 1.0f);
    }

    // Kaplanyan-Antonov 2016 geometric normal screen-space derivative specular anti-aliasing
    float CurvatureVar = 0.20f * min(0.06f, dot(GeoNormalDdx, GeoNormalDdx) + dot(GeoNormalDdy, GeoNormalDdy));
    Mat.BaseRoughness  = clamp(sqrt(Mat.BaseRoughness * Mat.BaseRoughness + CurvatureVar), 0.045f, 1.0f);
    if (Mat.CoatCoverage > 0.01f)
    {
        Mat.CoatRoughness = clamp(sqrt(Mat.CoatRoughness * Mat.CoatRoughness + 0.45f * CurvatureVar), 0.045f, 1.0f);
    }
    if (Tier == 2u)
    {
        Mat.Anisotropy = 0.0f;
    }

    float CosR          = cos(Mat.AnisotropyRotation);
    float SinR          = sin(Mat.AnisotropyRotation);
    OutShadingNormal    = N;
    OutRotatedTangent   = CosR * T + SinR * B;
    OutRotatedBitangent = -SinR * T + CosR * B;
}

float EvaluateGgxD(float NormalHalf, float Roughness)
{
    float α   = Roughness * Roughness;
    float αSq = α * α;
    float Den = NormalHalf * NormalHalf * (αSq - 1.0f) + 1.0f;
    return αSq / max(3.14159265f * Den * Den, 1.0e-6f);
}

float EvaluateSmithCorrelatedV(float NormalView, float NormalLight, float Roughness)
{
    float α   = Roughness * Roughness;
    float αSq = α * α;
    float Gl  = NormalLight * sqrt(NormalView * NormalView * (1.0f - αSq) + αSq);
    float Gv  = NormalView * sqrt(NormalLight * NormalLight * (1.0f - αSq) + αSq);
    return 0.5f / max(Gl + Gv, 1.0e-6f);
}

float EvaluateAnisotropicGgxD(float3 H, float3 N, float3 T, float3 B, float Roughness, float Anisotropy)
{
    float α   = max(0.002f, Roughness * Roughness);
    float K   = sqrt(max(0.1f, 1.0f - 0.9f * abs(Anisotropy)));
    float αt  = (Anisotropy >= 0.0f) ? (α / K) : (α * K);
    float αb  = (Anisotropy >= 0.0f) ? (α * K) : (α / K);
    float Th  = dot(T, H);
    float Bh  = dot(B, H);
    float Nh  = max(0.0f, dot(N, H));
    float A2  = αt * αb;
    float3 V  = float3(αb * Th, αt * Bh, A2 * Nh);
    float S2  = dot(V, V);
    return (A2 * A2 * A2) / max(3.14159265f * S2 * S2, 1.0e-7f);
}

float EvaluateAnisotropicSmithV(float3 VDir, float3 LDir, float3 N, float3 T, float3 B, float Roughness, float Anisotropy)
{
    float α   = max(0.002f, Roughness * Roughness);
    float K   = sqrt(max(0.1f, 1.0f - 0.9f * abs(Anisotropy)));
    float αt  = (Anisotropy >= 0.0f) ? (α / K) : (α * K);
    float αb  = (Anisotropy >= 0.0f) ? (α * K) : (α / K);
    float Tv  = dot(T, VDir), Bv = dot(B, VDir), Nv = max(1.0e-4f, dot(N, VDir));
    float Tl  = dot(T, LDir), Bl = dot(B, LDir), Nl = max(1.0e-4f, dot(N, LDir));
    float Lv  = Nl * length(float3(αt * Tv, αb * Bv, Nv));
    float Ll  = Nv * length(float3(αt * Tl, αb * Bl, Nl));
    return 0.5f / max(Lv + Ll, 1.0e-6f);
}

float EvaluateCharlieSheenD(float Roughness, float NormalHalf)
{
    float InvAlpha = 1.0f / max(Roughness * Roughness, 1.0e-3f);
    float SinSq    = max(1.0f - NormalHalf * NormalHalf, 0.0078125f);
    return (2.0f + InvAlpha) * pow(SinSq, InvAlpha * 0.5f) / (2.0f * 3.14159265f);
}

// Exact unpolarized Fresnel R = 0.5 * (Rs + Rp) with Total Internal Reflection detection
void EvaluateExactDielectricBoundary(float3 IncidentDir,
                                     float3 InterfaceNormal,
                                     float  EtaIncident,
                                     float  EtaTransmitted,
                                     out float  OutReflectance,
                                     out float3 OutRefractedDir,
                                     out bool   OutTotalInternalReflection)
{
    float CosI  = saturate(dot(-IncidentDir, InterfaceNormal));
    float Eta   = EtaIncident / max(1.0e-4f, EtaTransmitted);
    float Sin2T = Eta * Eta * max(0.0f, 1.0f - CosI * CosI);
    if (Sin2T >= 1.0f)
    {
        OutReflectance             = 1.0f;
        OutRefractedDir            = reflect(IncidentDir, InterfaceNormal);
        OutTotalInternalReflection = true;
        return;
    }
    float CosT = sqrt(max(0.0f, 1.0f - Sin2T));
    float Rs   = (EtaIncident * CosI - EtaTransmitted * CosT) / max(1.0e-5f, EtaIncident * CosI + EtaTransmitted * CosT);
    float Rp   = (EtaTransmitted * CosI - EtaIncident * CosT) / max(1.0e-5f, EtaTransmitted * CosI + EtaIncident * CosT);
    OutReflectance             = clamp(0.5f * (Rs * Rs + Rp * Rp), 0.0f, 1.0f);
    OutRefractedDir            = normalize(Eta * IncidentDir + (Eta * CosI - CosT) * InterfaceNormal);
    OutTotalInternalReflection = false;
}

// Heitz 2018 Sampling the GGX Distribution of Visible Normals (VNDF) in tangent basis (T, B, N)
float3 SampleGgxVndfHalfVector(float3 ViewDirWorld,
                               float3 T,
                               float3 B,
                               float3 N,
                               float  Roughness,
                               float  Anisotropy,
                               float2 Xi)
{
    float α  = max(0.02f, Roughness * Roughness);
    float K  = sqrt(max(0.1f, 1.0f - 0.9f * abs(Anisotropy)));
    float αt = (Anisotropy >= 0.0f) ? (α / K) : (α * K);
    float αb = (Anisotropy >= 0.0f) ? (α * K) : (α / K);

    float3 VLocal = float3(dot(ViewDirWorld, T), dot(ViewDirWorld, B), max(1.0e-4f, dot(ViewDirWorld, N)));
    float3 Vh     = normalize(float3(αt * VLocal.x, αb * VLocal.y, VLocal.z));

    float  Lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1    = (Lensq > 1.0e-7f) ? (float3(-Vh.y, Vh.x, 0.0f) / sqrt(Lensq)) : float3(1.0f, 0.0f, 0.0f);
    float3 T2    = cross(Vh, T1);

    float R   = sqrt(saturate(Xi.x));
    float Phi = 6.2831853f * Xi.y;
    float P1  = R * cos(Phi);
    float P2  = R * sin(Phi);
    float S   = 0.5f * (1.0f + Vh.z);
    P2        = (1.0f - S) * sqrt(max(0.0f, 1.0f - P1 * P1)) + S * P2;

    float3 Nh     = P1 * T1 + P2 * T2 + sqrt(max(0.0f, 1.0f - P1 * P1 - P2 * P2)) * Vh;
    float3 HLocal = normalize(float3(αt * Nh.x, αb * Nh.y, max(1.0e-4f, Nh.z)));
    return normalize(T * HLocal.x + B * HLocal.y + N * HLocal.z);
}

// Samples clearcoat vs basecoat specular reflection ray using Fresnel lobe mixture selection
bool SampleLayeredSpecularReflection(UnpackedSurfaceMaterial Mat,
                                     float3                  ViewDirToEye,
                                     float3                  N,
                                     float3                  T,
                                     float3                  B,
                                     float3                  Xi3,
                                     out float3              OutRayDir,
                                     out float3              OutThroughputWeight,
                                     out float               OutSelectedRoughness)
{
    float NdotV = saturate(dot(N, ViewDirToEye));
    float CoatF = 0.0f;
    if (Mat.CoatCoverage > 1.0e-3f)
    {
        float CoatR0 = pow((Mat.CoatIor - 1.0f) / (Mat.CoatIor + 1.0f), 2.0f);
        CoatF        = Mat.CoatCoverage * (CoatR0 + (1.0f - CoatR0) * pow(1.0f - NdotV, 5.0f));
    }
    float  BaseR0    = pow((Mat.BaseIor - 1.0f) / (Mat.BaseIor + 1.0f), 2.0f);
    float3 BaseF0    = lerp(float3(BaseR0), Mat.BaseAlbedo, Mat.Metallic);
    float3 BaseF     = BaseF0 + (1.0f - BaseF0) * pow(1.0f - NdotV, 5.0f);
    float  BaseLuma  = max(0.02f, dot(BaseF, float3(0.2126f, 0.7152f, 0.0722f)));
    float  ProbCoat  = (CoatF > 1.0e-4f) ? clamp(CoatF / (CoatF + BaseLuma * (1.0f - CoatF)), 0.12f, 0.88f) : 0.0f;

    bool ChooseCoat = (Xi3.x < ProbCoat);
    float3 HalfVec  = ChooseCoat
                          ? SampleGgxVndfHalfVector(ViewDirToEye, T, B, N, Mat.CoatRoughness, 0.0f, Xi3.yz)
                          : SampleGgxVndfHalfVector(ViewDirToEye, T, B, N, Mat.BaseRoughness, Mat.Anisotropy, Xi3.yz);
    OutRayDir = reflect(-ViewDirToEye, HalfVec);
    float NdotL = dot(N, OutRayDir);
    if (NdotL <= 1.0e-4f)
    {
        OutThroughputWeight  = float3(0.0f);
        OutSelectedRoughness = Mat.BaseRoughness;
        return false;
    }

    float VdotH = saturate(dot(ViewDirToEye, HalfVec));
    if (ChooseCoat)
    {
        float CoatR0         = pow((Mat.CoatIor - 1.0f) / (Mat.CoatIor + 1.0f), 2.0f);
        float Fc             = Mat.CoatCoverage * (CoatR0 + (1.0f - CoatR0) * pow(1.0f - VdotH, 5.0f));
        float αSq            = max(0.002f, Mat.CoatRoughness * Mat.CoatRoughness);
        αSq                  = αSq * αSq;
        float G1l            = 2.0f * NdotL / max(1.0e-4f, NdotL + sqrt(αSq + (1.0f - αSq) * NdotL * NdotL));
        OutThroughputWeight  = float3(Fc * G1l / max(0.05f, ProbCoat));
        OutSelectedRoughness = Mat.CoatRoughness;
    }
    else
    {
        float3 Fb    = BaseF0 + (1.0f - BaseF0) * pow(1.0f - VdotH, 5.0f);
        float  αSq   = max(0.002f, Mat.BaseRoughness * Mat.BaseRoughness);
        αSq          = αSq * αSq;
        float  G1l   = 2.0f * NdotL / max(1.0e-4f, NdotL + sqrt(αSq + (1.0f - αSq) * NdotL * NdotL));
        float3 CoatAtten = float3(1.0f);
        if (Mat.CoatCoverage > 1.0e-3f)
        {
            float CoatR0 = pow((Mat.CoatIor - 1.0f) / (Mat.CoatIor + 1.0f), 2.0f);
            float Cin    = CoatR0 + (1.0f - CoatR0) * pow(1.0f - NdotL, 5.0f);
            float Cout   = CoatR0 + (1.0f - CoatR0) * pow(1.0f - NdotV, 5.0f);
            float3 Abs   = exp(-Mat.CoatOpticalDepth * (1.0f / max(0.04f, NdotV) + 1.0f / max(0.04f, NdotL)));
            CoatAtten    = lerp(float3(1.0f), ((1.0f - Cin) * (1.0f - Cout)) * Abs, Mat.CoatCoverage);
        }
        OutThroughputWeight  = Fb * CoatAtten * (G1l / max(0.05f, 1.0f - ProbCoat));
        OutSelectedRoughness = Mat.BaseRoughness;
    }
    return true;
}

// Split Diffuse + Specular multi-stratum BRDF evaluator (enables split diffuse/specular ReSTIR & denoising!)
void EvaluateSurfaceSplitBrdf(UnpackedSurfaceMaterial Mat,
                              float3                  Normal,
                              float3                  Tangent,
                              float3                  Bitangent,
                              float3                  ViewDirToEye,
                              float3                  LightDirToSource,
                              out float3              OutDiffuseBrdf,
                              out float3              OutSpecularBrdf)
{
    OutDiffuseBrdf  = float3(0.0f);
    OutSpecularBrdf = float3(0.0f);

    float NdotV = dot(Normal, ViewDirToEye);
    float NdotL = dot(Normal, LightDirToSource);
    if (NdotV <= 0.0f || NdotL <= 0.0f)
    {
        return;
    }

    float3 HalfVec = normalize(ViewDirToEye + LightDirToSource);
    float  NdotH   = saturate(dot(Normal, HalfVec));
    float  VdotH   = saturate(dot(ViewDirToEye, HalfVec));

    float  Refl0   = pow((Mat.BaseIor - 1.0f) / (Mat.BaseIor + 1.0f), 2.0f);
    float3 F0      = lerp(float3(Refl0), Mat.BaseAlbedo, Mat.Metallic);
    float3 Fresnel = F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);

    float DistBase = (abs(Mat.Anisotropy) > 1.0e-3f)
                         ? EvaluateAnisotropicGgxD(HalfVec, Normal, Tangent, Bitangent, Mat.BaseRoughness, Mat.Anisotropy)
                         : EvaluateGgxD(NdotH, Mat.BaseRoughness);
    float VisBase  = (abs(Mat.Anisotropy) > 1.0e-3f)
                         ? EvaluateAnisotropicSmithV(ViewDirToEye, LightDirToSource, Normal, Tangent, Bitangent, Mat.BaseRoughness, Mat.Anisotropy)
                         : EvaluateSmithCorrelatedV(NdotV, NdotL, Mat.BaseRoughness);

    float3 Diff = (1.0f - Fresnel) * (1.0f - Mat.Metallic) * Mat.BaseAlbedo * (1.0f / 3.14159265f);
    float3 Spec = Fresnel * (DistBase * VisBase);

    if (Mat.SheenIntensity > 1.0e-3f)
    {
        float CharlieD = EvaluateCharlieSheenD(Mat.SheenRoughness, NdotH);
        float AshVis   = 1.0f / max(1.0e-4f, 4.0f * (NdotL + NdotV - NdotL * NdotV));
        Diff += Mat.SheenTint * (Mat.SheenIntensity * CharlieD * AshVis);
    }

    if (Mat.CoatCoverage > 1.0e-3f)
    {
        float  CoatRefl = pow((Mat.CoatIor - 1.0f) / (Mat.CoatIor + 1.0f), 2.0f);
        float  CoatF    = CoatRefl + (1.0f - CoatRefl) * pow(1.0f - VdotH, 5.0f);
        float  CoatIn   = CoatRefl + (1.0f - CoatRefl) * pow(1.0f - NdotL, 5.0f);
        float  CoatOut  = CoatRefl + (1.0f - CoatRefl) * pow(1.0f - NdotV, 5.0f);
        float3 Absorb   = exp(-Mat.CoatOpticalDepth * (1.0f / max(0.02f, NdotV) + 1.0f / max(0.02f, NdotL)));
        float3 UnderAtt = ((1.0f - CoatIn) * (1.0f - CoatOut)) * Absorb;
        float  CoatSpec = EvaluateGgxD(NdotH, Mat.CoatRoughness) * EvaluateSmithCorrelatedV(NdotV, NdotL, Mat.CoatRoughness) * CoatF;

        Diff = lerp(Diff, Diff * UnderAtt, Mat.CoatCoverage);
        Spec = lerp(Spec, float3(CoatSpec) + Spec * UnderAtt, Mat.CoatCoverage);
    }

    OutDiffuseBrdf  = Diff;
    OutSpecularBrdf = Spec;
}

// Combined wrapper preserving existing callers
float3 EvaluateSurfaceScatteringBrdf(UnpackedSurfaceMaterial Mat,
                                     float3                  WorldPos,
                                     float3                  Normal,
                                     float3                  ViewDirToEye,
                                     float3                  LightDirToSource)
{
    if (Mat.FlakeCoverage > 0.01f && Mat.MaterialLodTier == 0u)
    {
        int3  FlakeCell = int3(floor(WorldPos * Mat.FlakeFrequency));
        uint  H         = uint(FlakeCell.x * 73856093) ^ uint(FlakeCell.y * 19349663) ^ uint(FlakeCell.z * 83492791);
        H               = (H ^ (H >> 13u)) * 1274126177u;
        float Sparkle   = float(H & 0xFFFFu) / 65535.0f;
        Mat.BaseAlbedo *= lerp(0.88f, 1.24f, smoothstep(1.0f - Mat.FlakeCoverage, 1.0f, Sparkle));
    }
    float3 T = normalize(cross(abs(Normal.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f), Normal));
    float3 B = cross(Normal, T);
    float3 Diff, Spec;
    EvaluateSurfaceSplitBrdf(Mat, Normal, T, B, ViewDirToEye, LightDirToSource, Diff, Spec);
    return Diff + Spec;
}
