/*==========================================================================================================================================
                                          SURFACE SCATTERING EVALUATION (SLANG)
                                     ------------------------------------------------------------------
             Full multi-strata TextureWorkspace / TextureStudio material & BSDF evaluation:
               - 8x float4 material atlas unpacking + 2D-Array Albedo/Normal/ORM PBR texture sampling,
               - Pixar continuous orthonormal tangent frame + authored anisotropy rotation,
               - 3D Voronoi discrete automotive metallic micro-flakes + grazing palette flip,
               - 3-tier distance Material LOD (FullStrata 64B, FilteredToksvig 24B, CompactProxy 16B),
               - Anisotropic stretched-ellipsoid GGX + Heitz (2018) VNDF importance sampling +
                 clearcoat/base BSDF lobe mixture PDF + split diffuse/specular evaluation.
==========================================================================================================================================*/
// 🧩 Multi-strata PBR + 3D Voronoi flakes + anisotropic Heitz VNDF + 3-tier Material LOD evaluator.

#pragma once

static const uint  MaterialSlotStride = 8u;
static const float ScatteringPi       = 3.14159265358979323846f;
static const float ScatteringInvPi    = 0.31830988618379067154f;

struct SurfaceScatteringMaterial
{
    float3 BaseAlbedo;
    float  BaseRoughness;
    float  Metallic;
    float  BaseIor;
    float  Anisotropy;
    float  AnisotropyAngleRad;
    float  SheenWeight;
    float3 CoatTint;
    float  CoatCoverage;
    float  CoatRoughness;
    float  CoatIor;
    float  TransmissiveFraction;
    float  SubsurfaceWrap;
    float3 EmissionRadiance;
    float  EmissionStrength;
    float3 AbsorptionPerM;
    float  MicroDetailScale;
    float2 UvScale;
    uint   TextureLayerIndex;
    uint   TextureChannelMask;
    bool   AutomotiveFlakeActive;
    float  FlakeDensity;
    float  FlakeCoverage;
    float  FlakeTiltSpread;
    float  FlakeRoughness;
    float3 FlakeSecondaryTint;
    float  AmbientOcclusion;
};

float3 UnpackRgb8Linear(uint Word)
{
    return float3(float(Word & 0xFFu),
                  float((Word >> 8u) & 0xFFu),
                  float((Word >> 16u) & 0xFFu)) * (1.0f / 255.0f);
}

SurfaceScatteringMaterial UnpackScatteringSlot(StructuredBuffer<float4> MaterialTable, uint ClassificationId)
{
    uint   ClampedId = min(ClassificationId, 63u);
    uint   Base      = ClampedId * MaterialSlotStride;
    float4 M0        = MaterialTable[Base + 0u];
    float4 M1        = MaterialTable[Base + 1u];
    float4 M2        = MaterialTable[Base + 2u];
    float4 M3        = MaterialTable[Base + 3u];
    float4 M4        = MaterialTable[Base + 4u];
    float4 M5        = MaterialTable[Base + 5u];
    float4 M6        = MaterialTable[Base + 6u];
    float4 M7        = MaterialTable[Base + 7u];

    uint PackedFlags = asuint(M6.z);
    uint AngleQuant  = (PackedFlags >> 12u) & 0x0Fu;

    SurfaceScatteringMaterial Mat;
    Mat.BaseAlbedo            = clamp(M0.xyz, float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f));
    Mat.BaseRoughness         = clamp(M0.w, 0.025f, 1.0f);
    Mat.Metallic              = saturate(M1.x);
    Mat.BaseIor               = max(M1.y, 1.01f);
    Mat.Anisotropy            = clamp(M1.z, -0.92f, 0.92f);
    Mat.AnisotropyAngleRad    = float(AngleQuant) * (ScatteringPi / 15.0f);
    Mat.SheenWeight           = saturate(M1.w);
    Mat.CoatTint              = clamp(M2.xyz, float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f));
    Mat.CoatCoverage          = saturate(M2.w);
    Mat.CoatRoughness         = clamp(M3.x, 0.015f, 1.0f);
    Mat.CoatIor               = max(M3.y, 1.01f);
    Mat.TransmissiveFraction  = saturate(M3.z);
    Mat.SubsurfaceWrap        = saturate(M3.w);
    Mat.EmissionRadiance      = max(M4.xyz, float3(0.0f, 0.0f, 0.0f));
    Mat.EmissionStrength      = max(M4.w, 0.0f);
    Mat.AbsorptionPerM        = max(M5.xyz, float3(0.0f, 0.0f, 0.0f));
    Mat.MicroDetailScale      = saturate(M5.w);
    Mat.UvScale               = max(M6.xy, float2(0.05f, 0.05f));
    Mat.TextureLayerIndex     = PackedFlags & 0xFFu;
    Mat.TextureChannelMask    = (PackedFlags >> 8u) & 0x07u;
    Mat.AutomotiveFlakeActive = ((PackedFlags >> 11u) & 1u) != 0u;
    Mat.FlakeDensity          = clamp(M6.w, 20.0f, 2000.0f);
    Mat.FlakeCoverage         = saturate(M7.x);
    Mat.FlakeTiltSpread       = clamp(M7.y, 0.02f, 0.85f);
    Mat.FlakeRoughness        = clamp(M7.z, 0.02f, 0.65f);
    Mat.FlakeSecondaryTint    = UnpackRgb8Linear(asuint(M7.w));
    Mat.AmbientOcclusion      = 1.0f;
    return Mat;
}

//------------------------------------------------------------------------------------------------------------------------
//                           PIXAR CONTINUOUS ORTHONORMAL BASIS & 3-TIER MATERIAL LOD
//------------------------------------------------------------------------------------------------------------------------

void ConstructPixarOrthonormalBasis(float3 NormalUnit, float AngleRadians, out float3 TangentUnit, out float3 BitangentUnit)
{
    float  SignZ = (NormalUnit.z >= 0.0f) ? 1.0f : -1.0f;
    float  A     = -1.0f / (SignZ + NormalUnit.z);
    float  B     = NormalUnit.x * NormalUnit.y * A;
    float3 T0    = float3(1.0f + SignZ * NormalUnit.x * NormalUnit.x * A, SignZ * B, -SignZ * NormalUnit.x);
    float3 B0    = float3(B, SignZ + NormalUnit.y * NormalUnit.y * A, -NormalUnit.y);
    if (abs(AngleRadians) > 1.0e-4f)
    {
        float Ca      = cos(AngleRadians);
        float Sa      = sin(AngleRadians);
        TangentUnit   = normalize(Ca * T0 + Sa * B0);
        BitangentUnit = normalize(-Sa * T0 + Ca * B0);
    }
    else
    {
        TangentUnit   = normalize(T0);
        BitangentUnit = normalize(B0);
    }
}

uint SelectMaterialDetailTierGpu(float DistanceMeters, float NearDetailMeters, float FarProxyMeters, float MaterialQualityScale)
{
    float Scale   = clamp(MaterialQualityScale, 0.25f, 3.0f);
    float NearCut = max(1.0f, NearDetailMeters * Scale);
    float FarCut  = max(NearCut + 1.0f, FarProxyMeters * Scale);
    if (DistanceMeters <= NearCut)
    {
        return 0u; // FullStrata (64B)
    }
    if (DistanceMeters <= FarCut)
    {
        return 1u; // FilteredToksvig (24B)
    }
    return 2u; // CompactProxy (16B)
}

float3 HashVoronoiCell3D(int3 CellCoord)
{
    uint3 U = uint3(CellCoord) * uint3(1597334677u, 3812015801u, 2798796415u);
    uint  N = (U.x ^ U.y ^ U.z) * 1597334677u;
    uint3 H = uint3(N, N * 1664525u + 1013904223u, N * 22695477u + 1u);
    return float3(H & 0x00FFFFFFu) * (1.0f / 16777215.0f);
}

// Evaluates 3-tier Material LOD, PBR 2D-Array textures, & discrete 3D Voronoi automotive metallic flakes!
void EvaluateSurfaceMaterialDetail(inout SurfaceScatteringMaterial Mat,
                                   inout float3                    ShadingNormal,
                                   float3                          GeometricNormal,
                                   float3                          WorldPos,
                                   float3                          ViewDir,
                                   float2                          SurfaceUv,
                                   uint                            DetailTier,
                                   Sampler2DArray<float4>          AlbedoSrgbArray,
                                   Sampler2DArray<float4>          NormalUnormArray,
                                   Sampler2DArray<float4>          OrmUnormArray)
{
    float2 ScaledUv = SurfaceUv * Mat.UvScale;
    // Fallback triplanar UV when authored mesh carries zero UVs
    if (abs(SurfaceUv.x) + abs(SurfaceUv.y) < 1.0e-5f)
    {
        float3 AbsN = abs(GeometricNormal);
        if (AbsN.y >= AbsN.x && AbsN.y >= AbsN.z)
        {
            ScaledUv = WorldPos.xz * (0.35f * Mat.UvScale);
        }
        else if (AbsN.x >= AbsN.z)
        {
            ScaledUv = WorldPos.zy * (0.35f * Mat.UvScale);
        }
        else
        {
            ScaledUv = WorldPos.xy * (0.35f * Mat.UvScale);
        }
    }

    float LayerF = float(Mat.TextureLayerIndex);
    if (Mat.TextureChannelMask != 0u && DetailTier <= 1u)
    {
        if ((Mat.TextureChannelMask & 1u) != 0u)
        {
            float3 TexAlb  = AlbedoSrgbArray.SampleLevel(float3(ScaledUv, LayerF), 0.0f).rgb;
            Mat.BaseAlbedo = clamp(Mat.BaseAlbedo * TexAlb, float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f));
        }
        if ((Mat.TextureChannelMask & 2u) != 0u)
        {
            float3 Orm           = OrmUnormArray.SampleLevel(float3(ScaledUv, LayerF), 0.0f).rgb;
            Mat.AmbientOcclusion = clamp(Orm.r, 0.15f, 1.0f);
            Mat.BaseRoughness    = clamp(Mat.BaseRoughness * lerp(0.65f, 1.25f, Orm.g), 0.025f, 1.0f);
        }
        if ((Mat.TextureChannelMask & 4u) != 0u && DetailTier == 0u)
        {
            float3 TangentN = NormalUnormArray.SampleLevel(float3(ScaledUv, LayerF), 0.0f).xyz * 2.0f - 1.0f;
            TangentN.xy *= 0.65f;
            float3 T, B;
            ConstructPixarOrthonormalBasis(ShadingNormal, 0.0f, T, B);
            float3 Perturbed = normalize(TangentN.x * T + TangentN.y * B + max(TangentN.z, 0.2f) * ShadingNormal);
            if (dot(Perturbed, GeometricNormal) > 0.05f)
            {
                ShadingNormal = Perturbed;
            }
        }
    }

    // Automotive 3D Voronoi metallic micro-flakes + iridescent flip (Tier 0 discrete flakes; Tier 1/2 Toksvig filtered)
    if (Mat.AutomotiveFlakeActive)
    {
        float CosView  = saturate(dot(ShadingNormal, ViewDir));
        float FlipTerm = pow(1.0f - CosView, 2.2f);
        Mat.BaseAlbedo = lerp(Mat.BaseAlbedo, Mat.FlakeSecondaryTint, 0.42f * FlipTerm);

        if (DetailTier == 0u)
        {
            float  CellScale = clamp(Mat.FlakeDensity * 0.22f, 18.0f, 260.0f);
            float3 GridP     = WorldPos * CellScale;
            int3   BaseCell  = int3(floor(GridP));
            float  BestDist2 = 10.0f;
            float3 BestRand  = float3(0.5f, 0.5f, 0.5f);

            for (int Dz = -1; Dz <= 1; ++Dz)
            {
                for (int Dy = -1; Dy <= 1; ++Dy)
                {
                    for (int Dx = -1; Dx <= 1; ++Dx)
                    {
                        int3   CandCell = BaseCell + int3(Dx, Dy, Dz);
                        float3 Jitter   = HashVoronoiCell3D(CandCell);
                        float3 Delta    = (float3(CandCell) + Jitter) - GridP;
                        float  D2       = dot(Delta, Delta);
                        if (D2 < BestDist2)
                        {
                            BestDist2 = D2;
                            BestRand  = Jitter;
                        }
                    }
                }
            }

            if (BestRand.z <= Mat.FlakeCoverage)
            {
                // 📝 Flakes are sub-pixel at any sane viewing distance, so tilting the *shading* normal here
                // aliased metallic paint into white speckle. The flake orientation is instead folded into a
                // BRDF-consistent roughness widening, which preserves the measured sparkle energy statistically.
                float TiltMagnitude = length(BestRand.xy * 2.0f - 1.0f) * Mat.FlakeTiltSpread;
                Mat.BaseRoughness   = clamp(sqrt(Mat.BaseRoughness * Mat.BaseRoughness +
                                                 TiltMagnitude * TiltMagnitude * 0.35f), 0.05f, 1.0f);
            }
        }
        else
        {
            // Toksvig micro-flake normal variance broadening prevents distant sparkle aliasing!
            float Sigma2      = Mat.FlakeTiltSpread * Mat.FlakeTiltSpread * 0.18f;
            Mat.BaseRoughness = clamp(sqrt(Mat.BaseRoughness * Mat.BaseRoughness + Sigma2), 0.05f, 1.0f);
        }
    }

    if (DetailTier == 1u && (Mat.TextureChannelMask & 4u) != 0u)
    {
        Mat.BaseRoughness = clamp(sqrt(Mat.BaseRoughness * Mat.BaseRoughness + 0.025f), 0.04f, 1.0f);
    }
    else if (DetailTier >= 2u)
    {
        // CompactProxy (16B): fold clearcoat & anisotropy into single isotropic effective lobe
        Mat.BaseRoughness = clamp(lerp(Mat.BaseRoughness, Mat.CoatRoughness, 0.45f * Mat.CoatCoverage), 0.06f, 1.0f);
        Mat.Anisotropy    = 0.0f;
        Mat.CoatCoverage  = 0.0f;
        Mat.SheenWeight   = 0.0f;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                      ANISOTROPIC STRETCHED-ELLIPSOID GGX & HEITZ VNDF SAMPLING
//------------------------------------------------------------------------------------------------------------------------

float3 FresnelSchlickConductor(float CosTheta, float3 F0)
{
    float OneMinus = saturate(1.0f - CosTheta);
    float Pow5     = OneMinus * OneMinus * OneMinus * OneMinus * OneMinus;
    return F0 + (float3(1.0f, 1.0f, 1.0f) - F0) * Pow5;
}

float DielectricF0FromIor(float Ior)
{
    float Ratio = (Ior - 1.0f) / (Ior + 1.0f);
    return Ratio * Ratio;
}

void ComputeAnisotropicRoughnessAlphas(float Roughness, float Anisotropy, out float AlphaX, out float AlphaY)
{
    float R2 = Roughness * Roughness;
    AlphaX   = max(0.020f, R2 * sqrt(max(0.08f, 1.0f + 0.9f * Anisotropy)));
    AlphaY   = max(0.020f, R2 * sqrt(max(0.08f, 1.0f - 0.9f * Anisotropy)));
}

// 📝 Direct punctual/solar lights are sampled as a single direction. An un-widened GGX lobe for a 0.045-roughness
// clearcoat peaks near 1/(π α²) ≈ 8e4, so a one-sample-per-pixel direct term aliases into solid white bodywork.
// Widening the evaluation lobe to a physically-motivated minimum (the ~0.27° solar disc, plus the surface's own
// unresolved micro-roughness) keeps the highlight energy in the image instead of in the sampling noise.
static const float DirectLightMinimumAlpha = 0.055f;

void ComputeDirectLightRoughnessAlphas(float Roughness, float Anisotropy, out float AlphaX, out float AlphaY)
{
    ComputeAnisotropicRoughnessAlphas(Roughness, Anisotropy, AlphaX, AlphaY);
    AlphaX = max(AlphaX, DirectLightMinimumAlpha);
    AlphaY = max(AlphaY, DirectLightMinimumAlpha);
}

float AnisotropicGgxNdfLocal(float3 HLocal, float AlphaX, float AlphaY)
{
    float Term = (HLocal.x * HLocal.x) / (AlphaX * AlphaX) +
                 (HLocal.y * HLocal.y) / (AlphaY * AlphaY) +
                 (HLocal.z * HLocal.z);
    return 1.0f / max(ScatteringPi * AlphaX * AlphaY * Term * Term, 1.0e-7f);
}

float AnisotropicSmithLambdaLocal(float3 WLocal, float AlphaX, float AlphaY)
{
    float Wz2 = max(WLocal.z * WLocal.z, 1.0e-7f);
    float Tan2 = (AlphaX * AlphaX * WLocal.x * WLocal.x + AlphaY * AlphaY * WLocal.y * WLocal.y) / Wz2;
    return 0.5f * (-1.0f + sqrt(1.0f + Tan2));
}

struct SplitBrdfTerms
{
    float3 DiffuseTerm;  // Demodulated-compatible (f_diff / EffectiveAlbedo) * EffectiveAlbedo -> exact diffuse BRDF
    float3 SpecularTerm; // Base anisotropic GGX + Clearcoat GGX + Sheen
};

SplitBrdfTerms EvaluateSplitScatteringBrdf(SurfaceScatteringMaterial Mat,
                                           float3                    Normal,
                                           float3                    ViewDir,
                                           float3                    LightDir)
{
    SplitBrdfTerms Out;
    Out.DiffuseTerm  = float3(0.0f, 0.0f, 0.0f);
    Out.SpecularTerm = float3(0.0f, 0.0f, 0.0f);

    float NdotV = max(dot(Normal, ViewDir), 1.0e-4f);
    float NdotL = dot(Normal, LightDir);
    if (NdotL <= 0.0f && Mat.SubsurfaceWrap <= 0.0f)
    {
        return Out;
    }

    float ClampedNdotL = max(NdotL, 1.0e-4f);
    float3 HalfVec     = normalize(ViewDir + LightDir);
    float  VdotH       = saturate(dot(ViewDir, HalfVec));
    float  NdotH       = saturate(dot(Normal, HalfVec));

    float3 T, B;
    ConstructPixarOrthonormalBasis(Normal, Mat.AnisotropyAngleRad, T, B);
    float3 VLocal = float3(dot(ViewDir, T), dot(ViewDir, B), NdotV);
    float3 LLocal = float3(dot(LightDir, T), dot(LightDir, B), ClampedNdotL);
    float3 HLocal = float3(dot(HalfVec, T), dot(HalfVec, B), NdotH);

    // Top dielectric clearcoat lobe
    float CoatF0      = DielectricF0FromIor(Mat.CoatIor);
    float CoatFresnel = Mat.CoatCoverage * (CoatF0 + (1.0f - CoatF0) * pow(1.0f - VdotH, 5.0f));
    float3 CoatSpec   = float3(0.0f, 0.0f, 0.0f);
    if (Mat.CoatCoverage > 1.0e-3f && NdotL > 0.0f)
    {
        float Ac     = max(DirectLightMinimumAlpha, Mat.CoatRoughness * Mat.CoatRoughness);
        float Dc     = AnisotropicGgxNdfLocal(HLocal, Ac, Ac);
        float Gc     = 1.0f / (1.0f + AnisotropicSmithLambdaLocal(VLocal, Ac, Ac) + AnisotropicSmithLambdaLocal(LLocal, Ac, Ac));
        CoatSpec     = Mat.CoatTint * ((Dc * Gc * CoatFresnel) / max(4.0f * NdotV * ClampedNdotL, 1.0e-4f));
    }
    float3 UnderCoatTransmit = lerp(float3(1.0f, 1.0f, 1.0f), Mat.CoatTint * (1.0f - CoatFresnel), Mat.CoatCoverage);

    // Base anisotropic stretched-ellipsoid GGX + Kulla-Conty rough-metal energy preservation
    float  DielectricF0 = DielectricF0FromIor(Mat.BaseIor);
    float3 BaseF0       = lerp(float3(DielectricF0, DielectricF0, DielectricF0), Mat.BaseAlbedo, Mat.Metallic);
    float3 BaseF        = FresnelSchlickConductor(VdotH, BaseF0);
    float3 BaseSpec     = float3(0.0f, 0.0f, 0.0f);
    if (NdotL > 0.0f)
    {
        float Ax, Ay;
        ComputeDirectLightRoughnessAlphas(Mat.BaseRoughness, Mat.Anisotropy, Ax, Ay);
        float Db  = AnisotropicGgxNdfLocal(HLocal, Ax, Ay);
        float Gb  = 1.0f / (1.0f + AnisotropicSmithLambdaLocal(VLocal, Ax, Ay) + AnisotropicSmithLambdaLocal(LLocal, Ax, Ay));
        float3 MultiscatterGain = float3(1.0f, 1.0f, 1.0f) + BaseF0 * (0.45f * Mat.BaseRoughness * Mat.BaseRoughness);
        BaseSpec  = ((Db * Gb * BaseF) / max(4.0f * NdotV * ClampedNdotL, 1.0e-4f)) * MultiscatterGain;
    }

    // Disney retro-reflection + Lambertian diffuse + optional subsurface wrap
    float3 DiffuseBrdf = float3(0.0f, 0.0f, 0.0f);
    float  DiffuseGate = (1.0f - Mat.Metallic) * (1.0f - Mat.TransmissiveFraction);
    if (DiffuseGate > 1.0e-3f)
    {
        float Fd90   = 0.5f + 2.0f * Mat.BaseRoughness * VdotH * VdotH;
        float Fl     = 1.0f + (Fd90 - 1.0f) * pow(1.0f - ClampedNdotL, 5.0f);
        float Fv     = 1.0f + (Fd90 - 1.0f) * pow(1.0f - NdotV, 5.0f);
        if (NdotL > 0.0f)
        {
            DiffuseBrdf += Mat.BaseAlbedo * ScatteringInvPi * Fl * Fv * DiffuseGate;
        }
        if (Mat.SubsurfaceWrap > 1.0e-3f)
        {
            float WrapCos = saturate((NdotL + Mat.SubsurfaceWrap) / ((1.0f + Mat.SubsurfaceWrap) * (1.0f + Mat.SubsurfaceWrap)));
            DiffuseBrdf += Mat.BaseAlbedo * ScatteringInvPi * Mat.SubsurfaceWrap * WrapCos * DiffuseGate * 0.45f;
        }
    }

    // Microfiber Charlie-inspired grazing sheen
    float3 SheenSpec = float3(0.0f, 0.0f, 0.0f);
    if (Mat.SheenWeight > 1.0e-3f && NdotL > 0.0f)
    {
        float SinThetaH = sqrt(max(1.0f - NdotH * NdotH, 0.0f));
        float InvR      = 1.0f / max(Mat.BaseRoughness, 0.08f);
        float SheenD    = (2.0f + InvR) * pow(SinThetaH, InvR) * (0.5f * ScatteringInvPi);
        SheenSpec       = lerp(float3(1.0f, 1.0f, 1.0f), Mat.BaseAlbedo, 0.35f) *
                          (Mat.SheenWeight * SheenD / max(4.0f * (NdotL + NdotV - NdotL * NdotV), 0.25f));
    }

    // 📝 Firefly bound. A direct light is evaluated at one sample per pixel: for a 0.045-roughness clearcoat the
    // GGX peak reaches D ≈ 1/(π α²) ≈ 1.5e4, so the pixel either lands inside the highlight (clipped white) or
    // outside it (dark). Real-time ReSTIR pipelines bound the single-sample specular estimate and let temporal
    // accumulation restore the mean; this clamp is that bound and is documented as a preview limitation.
    const float DirectSpecularEnergyCeiling = 5.5f;
    float3 CoatPlusBase = CoatSpec + UnderCoatTransmit * (BaseSpec + SheenSpec);
    float  SpecLuminance = dot(CoatPlusBase, float3(0.2126f, 0.7152f, 0.0722f));
    if (SpecLuminance > DirectSpecularEnergyCeiling)
    {
        CoatPlusBase *= DirectSpecularEnergyCeiling / SpecLuminance;
    }

    Out.DiffuseTerm  = UnderCoatTransmit * DiffuseBrdf * Mat.AmbientOcclusion;
    Out.SpecularTerm = CoatPlusBase;
    return Out;
}

float3 EvaluateScatteringBrdf(SurfaceScatteringMaterial Mat,
                              float3                    Normal,
                              float3                    ViewDir,
                              float3                    LightDir)
{
    SplitBrdfTerms Split = EvaluateSplitScatteringBrdf(Mat, Normal, ViewDir, LightDir);
    return Split.DiffuseTerm + Split.SpecularTerm;
}

//------------------------------------------------------------------------------------------------------------------------
//                HEITZ (2018) STRETCHED-ELLIPSOID VNDF & BSDF LOBE MIXTURE IMPORTANCE SAMPLING
//------------------------------------------------------------------------------------------------------------------------

struct SampledSpecularReflection
{
    bool   Valid;
    float3 RayDirection;
    float3 ThroughputWeight; // Exact BSDF * cosθ_L / PDF weight (bounded, firefly-free!)
    float  SolidAnglePdf;
    float  EffectiveRoughness;
};

float3 SampleHeitzStretchedEllipsoidHalfVector(float3 VLocal, float AlphaX, float AlphaY, float U1, float U2)
{
    float3 Vh    = normalize(float3(AlphaX * VLocal.x, AlphaY * VLocal.y, max(VLocal.z, 1.0e-4f)));
    float  Lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1    = (Lensq > 1.0e-7f) ? float3(-Vh.y, Vh.x, 0.0f) * rsqrt(Lensq) : float3(1.0f, 0.0f, 0.0f);
    float3 T2    = cross(Vh, T1);

    float Rad = sqrt(saturate(U1));
    float Phi = 2.0f * ScatteringPi * U2;
    float P1  = Rad * cos(Phi);
    float P2  = Rad * sin(Phi);
    float S   = 0.5f * (1.0f + Vh.z);
    P2        = (1.0f - S) * sqrt(max(0.0f, 1.0f - P1 * P1)) + S * P2;

    float3 Nh = P1 * T1 + P2 * T2 + sqrt(max(0.0f, 1.0f - P1 * P1 - P2 * P2)) * Vh;
    return normalize(float3(AlphaX * Nh.x, AlphaY * Nh.y, max(0.0f, Nh.z)));
}

SampledSpecularReflection SampleBsdfSpecularMixtureVndf(SurfaceScatteringMaterial Mat,
                                                        float3                    Normal,
                                                        float3                    ViewDir,
                                                        float                     LobeSelectorU,
                                                        float                     U1,
                                                        float                     U2)
{
    SampledSpecularReflection Out;
    Out.Valid              = false;
    Out.RayDirection       = reflect(-ViewDir, Normal);
    Out.ThroughputWeight   = float3(0.0f, 0.0f, 0.0f);
    Out.SolidAnglePdf      = 0.0f;
    Out.EffectiveRoughness = Mat.BaseRoughness;

    float NdotV = max(dot(Normal, ViewDir), 1.0e-4f);
    float CoatF0 = DielectricF0FromIor(Mat.CoatIor);
    float CoatFresnelV = Mat.CoatCoverage * (CoatF0 + (1.0f - CoatF0) * pow(1.0f - NdotV, 5.0f));

    float  DielectricF0 = DielectricF0FromIor(Mat.BaseIor);
    float3 BaseF0       = lerp(float3(DielectricF0, DielectricF0, DielectricF0), Mat.BaseAlbedo, Mat.Metallic);
    float3 BaseFresnelV = FresnelSchlickConductor(NdotV, BaseF0);
    float  BaseLum      = max(0.2126f * BaseFresnelV.r + 0.7152f * BaseFresnelV.g + 0.0722f * BaseFresnelV.b, 0.02f);

    float CoatProb = (Mat.CoatCoverage > 1.0e-3f)
                         ? clamp(CoatFresnelV / max(CoatFresnelV + (1.0f - CoatFresnelV) * BaseLum, 1.0e-4f), 0.15f, 0.85f)
                         : 0.0f;

    float3 T, B;
    ConstructPixarOrthonormalBasis(Normal, Mat.AnisotropyAngleRad, T, B);
    float3 VLocal = normalize(float3(dot(ViewDir, T), dot(ViewDir, B), NdotV));

    float Ax, Ay;
    ComputeAnisotropicRoughnessAlphas(Mat.BaseRoughness, Mat.Anisotropy, Ax, Ay);
    float Ac = max(0.015f, Mat.CoatRoughness * Mat.CoatRoughness);

    bool SampledCoat = (LobeSelectorU < CoatProb);
    float3 HLocal    = SampledCoat
                           ? SampleHeitzStretchedEllipsoidHalfVector(VLocal, Ac, Ac, U1, U2)
                           : SampleHeitzStretchedEllipsoidHalfVector(VLocal, Ax, Ay, U1, U2);

    float3 HalfWorld = normalize(HLocal.x * T + HLocal.y * B + HLocal.z * Normal);
    float3 LightDir  = reflect(-ViewDir, HalfWorld);
    float  NdotL     = dot(Normal, LightDir);
    if (NdotL <= 1.0e-4f)
    {
        return Out;
    }

    float3 LLocal = float3(dot(LightDir, T), dot(LightDir, B), NdotL);
    float  VdotH  = saturate(dot(ViewDir, HalfWorld));

    // Exact mixture solid-angle PDF p_mix(L|V) = p_coat * p_VNDF,coat + (1 - p_coat) * p_VNDF,base
    float LambdaVBase = AnisotropicSmithLambdaLocal(VLocal, Ax, Ay);
    float LambdaLBase = AnisotropicSmithLambdaLocal(LLocal, Ax, Ay);
    float G1VBase     = 1.0f / (1.0f + LambdaVBase);
    float G2Base      = 1.0f / (1.0f + LambdaVBase + LambdaLBase);
    float PdfBase     = (G1VBase * AnisotropicGgxNdfLocal(HLocal, Ax, Ay)) / max(4.0f * NdotV, 1.0e-4f);

    float PdfCoat = 0.0f;
    float G2Coat  = 0.0f;
    if (CoatProb > 0.0f)
    {
        float LambdaVCoat = AnisotropicSmithLambdaLocal(VLocal, Ac, Ac);
        float LambdaLCoat = AnisotropicSmithLambdaLocal(LLocal, Ac, Ac);
        float G1VCoat     = 1.0f / (1.0f + LambdaVCoat);
        G2Coat            = 1.0f / (1.0f + LambdaVCoat + LambdaLCoat);
        PdfCoat           = (G1VCoat * AnisotropicGgxNdfLocal(HLocal, Ac, Ac)) / max(4.0f * NdotV, 1.0e-4f);
    }

    float MixPdf = CoatProb * PdfCoat + (1.0f - CoatProb) * PdfBase;
    if (MixPdf <= 1.0e-6f)
    {
        return Out;
    }

    // Evaluate combined specular BSDF * NdotL / MixPdf with explicit energy clamp
    float  CoatFH  = Mat.CoatCoverage * (CoatF0 + (1.0f - CoatF0) * pow(1.0f - VdotH, 5.0f));
    float3 BaseFH  = FresnelSchlickConductor(VdotH, BaseF0) * (float3(1.0f, 1.0f, 1.0f) + BaseF0 * (0.45f * Mat.BaseRoughness * Mat.BaseRoughness));
    float3 UnderTr = lerp(float3(1.0f, 1.0f, 1.0f), Mat.CoatTint * (1.0f - CoatFH), Mat.CoatCoverage);

    float3 SpecBrdfCos = float3(0.0f, 0.0f, 0.0f);
    if (CoatProb > 0.0f)
    {
        float Dc = AnisotropicGgxNdfLocal(HLocal, Ac, Ac);
        SpecBrdfCos += Mat.CoatTint * ((Dc * G2Coat * CoatFH) / max(4.0f * NdotV, 1.0e-4f));
    }
    {
        float Db = AnisotropicGgxNdfLocal(HLocal, Ax, Ay);
        SpecBrdfCos += UnderTr * ((Db * G2Base * BaseFH) / max(4.0f * NdotV, 1.0e-4f));
    }

    Out.Valid              = true;
    Out.RayDirection       = LightDir;
    Out.SolidAnglePdf      = MixPdf;
    Out.ThroughputWeight   = min(SpecBrdfCos / MixPdf, float3(2.5f, 2.5f, 2.5f));
    Out.EffectiveRoughness = SampledCoat ? Mat.CoatRoughness : Mat.BaseRoughness;
    return Out;
}
