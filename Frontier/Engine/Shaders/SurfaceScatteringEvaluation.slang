//==========================================================================================================================================
//                                           SURFACE SCATTERING EVALUATION
//==========================================================================================================================================
// 🧩 Slang GPU counterpart of Engine/Render/ScatteringSequence.h + AutomotivePaint.h:
//    Evaluates multi-stratum GGX dielectric/metallic basecoat, procedural automotive metallic paint micro-flakes,
//    Estevez-Kulla Charlie fabric sheen, and absorbing polyurethane clearcoat.

#pragma once

static const uint kMaterialSlotStride = 6u;

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
    float3 EmissiveRadiance;
    float  SheenRoughness;
    float  FlakeCoverage;
    float  FlakeFrequency;
    bool   TransmissiveGlass;
    float  GlassIor;
};

UnpackedSurfaceMaterial FetchSurfaceMaterial(StructuredBuffer<float4> MaterialTable, uint ClassId)
{
    uint   Clamped = min(ClassId, 63u);
    uint   Base    = Clamped * kMaterialSlotStride;
    float4 M0      = MaterialTable[Base + 0u];
    float4 M1      = MaterialTable[Base + 1u];
    float4 M2      = MaterialTable[Base + 2u];
    float4 M3      = MaterialTable[Base + 3u];
    float4 M4      = MaterialTable[Base + 4u];
    float4 M5      = MaterialTable[Base + 5u];

    UnpackedSurfaceMaterial Out;
    Out.BaseAlbedo        = M0.xyz;
    Out.BaseRoughness     = clamp(M0.w, 0.045f, 1.0f);
    Out.Metallic          = saturate(M1.x);
    Out.BaseIor           = max(1.02f, M1.y);
    Out.CoatCoverage      = saturate(M1.z);
    Out.CoatRoughness     = clamp(M1.w, 0.045f, 1.0f);
    Out.CoatOpticalDepth  = max(float3(0.0f), M2.xyz);
    Out.CoatIor           = max(1.02f, M2.w);
    Out.SheenTint         = max(float3(0.0f), M3.xyz);
    Out.SheenIntensity    = saturate(M3.w);
    Out.EmissiveRadiance  = max(float3(0.0f), M4.xyz);
    Out.SheenRoughness    = clamp(M4.w, 0.05f, 1.0f);
    Out.FlakeCoverage     = saturate(M5.x);
    Out.FlakeFrequency    = max(1.0f, M5.y);
    Out.TransmissiveGlass = (M5.z > 0.5f);
    Out.GlassIor          = max(1.02f, M5.w);
    return Out;
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

float EvaluateCharlieSheenD(float Roughness, float NormalHalf)
{
    float InvAlpha = 1.0f / max(Roughness * Roughness, 1.0e-3f);
    float SinSq    = max(1.0f - NormalHalf * NormalHalf, 0.0078125f);
    return (2.0f + InvAlpha) * pow(SinSq, InvAlpha * 0.5f) / (2.0f * 3.14159265f);
}

// Exact GPU mirror of Engine/Render/ScatteringSequence.h::EvaluateScatteringSequence + AutomotivePaint flake modulation
float3 EvaluateSurfaceScatteringBrdf(UnpackedSurfaceMaterial Mat,
                                     float3                  WorldPos,
                                     float3                  Normal,
                                     float3                  ViewDirToEye,
                                     float3                  LightDirToSource)
{
    float NdotV = dot(Normal, ViewDirToEye);
    float NdotL = dot(Normal, LightDirToSource);
    if (NdotV <= 0.0f || NdotL <= 0.0f)
    {
        return float3(0.0f);
    }

    float3 BaseAlbedo = Mat.BaseAlbedo;
    if (Mat.FlakeCoverage > 0.01f)
    {
        // Deterministic 3D spatial cell hash for automotive metallic paint micro-flakes
        int3  FlakeCell = int3(floor(WorldPos * Mat.FlakeFrequency));
        uint  H         = uint(FlakeCell.x * 73856093) ^ uint(FlakeCell.y * 19349663) ^ uint(FlakeCell.z * 83492791);
        H               = (H ^ (H >> 13u)) * 1274126177u;
        float Sparkle   = float(H & 0xFFFFu) / 65535.0f;
        BaseAlbedo     *= lerp(0.88f, 1.24f, smoothstep(1.0f - Mat.FlakeCoverage, 1.0f, Sparkle));
    }

    float3 HalfVec = normalize(ViewDirToEye + LightDirToSource);
    float  NdotH   = saturate(dot(Normal, HalfVec));
    float  VdotH   = saturate(dot(ViewDirToEye, HalfVec));

    float  Refl0    = pow((Mat.BaseIor - 1.0f) / (Mat.BaseIor + 1.0f), 2.0f);
    float3 F0       = lerp(float3(Refl0), BaseAlbedo, Mat.Metallic);
    float3 Fresnel  = F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);
    float  DistBase = EvaluateGgxD(NdotH, Mat.BaseRoughness);
    float  VisBase  = EvaluateSmithCorrelatedV(NdotV, NdotL, Mat.BaseRoughness);

    float3 BaseEval = Fresnel * (DistBase * VisBase) +
                      (1.0f - Fresnel) * (1.0f - Mat.Metallic) * BaseAlbedo * (1.0f / 3.14159265f);

    if (Mat.SheenIntensity > 1.0e-3f)
    {
        float CharlieD = EvaluateCharlieSheenD(Mat.SheenRoughness, NdotH);
        float AshVis   = 1.0f / max(1.0e-4f, 4.0f * (NdotL + NdotV - NdotL * NdotV));
        BaseEval      += Mat.SheenTint * (Mat.SheenIntensity * CharlieD * AshVis);
    }

    if (Mat.CoatCoverage > 1.0e-3f)
    {
        float  CoatRefl = pow((Mat.CoatIor - 1.0f) / (Mat.CoatIor + 1.0f), 2.0f);
        float  CoatF    = CoatRefl + (1.0f - CoatRefl) * pow(1.0f - VdotH, 5.0f);
        float  CoatIn   = CoatRefl + (1.0f - CoatRefl) * pow(1.0f - NdotL, 5.0f);
        float  CoatOut  = CoatRefl + (1.0f - CoatRefl) * pow(1.0f - NdotV, 5.0f);
        float3 Absorb   = exp(-Mat.CoatOpticalDepth * (1.0f / max(0.02f, NdotV) + 1.0f / max(0.02f, NdotL)));
        float  CoatSpec = EvaluateGgxD(NdotH, Mat.CoatRoughness) * EvaluateSmithCorrelatedV(NdotV, NdotL, Mat.CoatRoughness) * CoatF;
        float3 Coated   = float3(CoatSpec) + BaseEval * ((1.0f - CoatIn) * (1.0f - CoatOut)) * Absorb;
        BaseEval        = lerp(BaseEval, Coated, Mat.CoatCoverage);
    }

    return BaseEval;
}
