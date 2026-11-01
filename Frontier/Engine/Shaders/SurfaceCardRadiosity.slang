/*==========================================================================================================================================
                                            SURFACE CARD RADIOSITY (SLANG)
                                     ------------------------------------------------------------------
             GPU 6-axis RGB multi-bounce surface-card radiosity solver:
               1. InjectDirectCardRadianceMain — evaluates SDF-shadowed solar, sky, and local-luminaire
                  direct exitance E_i = L_{e,i} + (ρ_i / π) * Irradiance_i per card patch.
               2. IterateCardRadiosityMain    — Jacobi multi-bounce radiosity iteration
                  B_i^(k+1) = E_i + g * ρ_i * Σ_{j≠i} F_{ij} * V_{ij} * B_j^(k) with bounded disk-area
                  form factors F_{ij} = cosθ_i cosθ_j A_j / (π (d^2 + A_j/π)).
             Also provides QuerySurfaceCardExitanceGpu(x, n) for ReSTIR GI secondary-hit lookup.
==========================================================================================================================================*/
// 🧩 GPU multi-bounce surface-card radiosity injection, Jacobi bounce solver, & world-space exitance query.

#pragma once

#include "VolumeDistanceSampling.slang"

static const uint CardDescriptorStride = 4u; // 4 × float4 per patch: Pos+Area, Normal+SkyVis, Rho+Radius, SelfEmission

// Evaluates multi-bounce diffuse exitant radiance [W / (sr * m^2)] from active surface cards near (WorldPos, WorldNormal).
float3 QuerySurfaceCardExitanceGpu(StructuredBuffer<float4> CardDescriptors,
                                   StructuredBuffer<float4> CardExitance,
                                   uint                     CardCount,
                                   float3                   WorldPos,
                                   float3                   WorldNormal)
{
    if (CardCount == 0u)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 WeightedRadiance = float3(0.0f, 0.0f, 0.0f);
    float  WeightSum        = 0.0f;

    [loop]
    for (uint I = 0u; I < CardCount; ++I)
    {
        uint   Base      = I * CardDescriptorStride;
        float4 Row0      = CardDescriptors[Base + 0u]; // Center.xyz, AreaM2
        float4 Row1      = CardDescriptors[Base + 1u]; // Normal.xyz, SkyVis
        float4 Row2      = CardDescriptors[Base + 2u]; // Reflectance.rgb, InfluenceRadiusM
        float  NormalSim = saturate(dot(WorldNormal, Row1.xyz) * 0.65f + 0.35f);
        if (NormalSim <= 1.0e-3f)
        {
            continue;
        }

        float  Radius    = max(Row2.w, 0.75f);
        float3 Delta     = WorldPos - Row0.xyz;
        float  DistSq    = dot(Delta, Delta);
        float  SpatialW  = 1.0f / (1.0f + DistSq / max(Radius * Radius, 0.25f));
        float  W         = NormalSim * SpatialW * SpatialW;
        WeightedRadiance += CardExitance[I].xyz * W;
        WeightSum += W;
    }

    return (WeightSum > 1.0e-6f) ? (WeightedRadiance / WeightSum) : float3(0.0f, 0.0f, 0.0f);
}

#ifdef RADIOSITY_COMPUTE_ENTRY

struct CardRadiosityPushConstants
{
    uint   CardCount;
    uint   LuminaireCount;
    float  IndirectBounceGain;
    float  SkyVisibilityScale;
    float3 SunDirection;
    float  SunIlluminanceLux;
    float3 SunRadianceTint;
    float  SkyAmbientLuminance;
    float3 SkyZenithRadiance;
    float  Padding0;
    float3 SkyHorizonRadiance;
    uint   EnableVisibilityCones;
};

[[vk::push_constant]]
ConstantBuffer<CardRadiosityPushConstants> gCardPush;

[[vk::binding(0, 0)]] StructuredBuffer<float4>   gCardDescriptors;
[[vk::binding(1, 0)]] StructuredBuffer<float4>   gDistanceVolume;
[[vk::binding(2, 0)]] StructuredBuffer<float4>   gLuminaires;
[[vk::binding(3, 0)]] StructuredBuffer<float4>   gSourceExitance;
[[vk::binding(4, 0)]] RWStructuredBuffer<float4> gDirectExitance;
[[vk::binding(5, 0)]] RWStructuredBuffer<float4> gTargetExitance;

[shader("compute")]
[numthreads(64, 1, 1)]
void InjectDirectCardRadianceMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint CardIdx = DispatchThreadId.x;
    if (CardIdx >= gCardPush.CardCount)
    {
        return;
    }

    uint   Base = CardIdx * CardDescriptorStride;
    float4 Row0 = gCardDescriptors[Base + 0u]; // Center.xyz, AreaM2
    float4 Row1 = gCardDescriptors[Base + 1u]; // Normal.xyz, SkyVis
    float4 Row2 = gCardDescriptors[Base + 2u]; // Rho.rgb, Radius
    float4 Row3 = gCardDescriptors[Base + 3u]; // SelfEmission.rgb, 0

    float3 Pos    = Row0.xyz;
    float3 Norm   = normalize(Row1.xyz);
    float3 RhoOverPi = clamp(Row2.xyz, float3(0.0f, 0.0f, 0.0f), float3(0.95f, 0.95f, 0.95f)) * 0.318309886f;

    float3 Irradiance = float3(0.0f, 0.0f, 0.0f);

    // Solar direct contribution with 4-cascade SDF cone visibility
    float3 SunDir = normalize(gCardPush.SunDirection);
    float  NdotS  = max(dot(Norm, SunDir), 0.0f);
    if (NdotS > 0.0f && gCardPush.SunIlluminanceLux > 0.0f)
    {
        float SunVis = (gCardPush.EnableVisibilityCones != 0u)
                           ? TraceVolumeConeVisibilityGpu(gDistanceVolume, Pos + Norm * 0.08f, SunDir, 36.0f, 16.0f, 20u)
                           : 1.0f;
        Irradiance += gCardPush.SunRadianceTint * gCardPush.SunIlluminanceLux * NdotS * SunVis;
    }

    // Hemispherical sky dome irradiance modulated by SDF AO + card sky weight
    if (gCardPush.SkyAmbientLuminance > 0.0f)
    {
        float SkyAo     = (gCardPush.EnableVisibilityCones != 0u)
                              ? EvaluateVolumeAmbientOcclusionGpu(gDistanceVolume, Pos + Norm * 0.05f, Norm)
                              : 1.0f;
        float UpFactor  = saturate(Norm.y * 0.5f + 0.5f);
        float3 SkyColor = lerp(gCardPush.SkyHorizonRadiance, gCardPush.SkyZenithRadiance, UpFactor);
        Irradiance += SkyColor * (3.14159265f * gCardPush.SkyAmbientLuminance * Row1.w * gCardPush.SkyVisibilityScale * SkyAo);
    }

    // Analytical local luminaires (3 × float4 per luminaire: Pos+Radius, Rad+Kind, Axis+Cos)
    [loop]
    for (uint L = 0u; L < gCardPush.LuminaireCount; ++L)
    {
        float4 L0 = gLuminaires[L * 3u + 0u];
        float4 L1 = gLuminaires[L * 3u + 1u];
        if (L1.w < 0.5f)
        {
            continue;
        }
        float3 ToLight = L0.xyz - Pos;
        float  DistSq  = max(dot(ToLight, ToLight), 0.25f);
        float  Dist    = sqrt(DistSq);
        float3 LDir    = ToLight / Dist;
        float  NdotL   = max(dot(Norm, LDir), 0.0f);
        if (NdotL > 0.0f)
        {
            float Vis = (gCardPush.EnableVisibilityCones != 0u)
                            ? TraceVolumeConeVisibilityGpu(gDistanceVolume, Pos + Norm * 0.08f, LDir, Dist, 12.0f, 14u)
                            : 1.0f;
            float SolidAngle = min(3.14159265f * max(L0.w * L0.w, 0.04f) / DistSq, 1.5f);
            Irradiance += L1.xyz * SolidAngle * NdotL * Vis;
        }
    }

    float3 DirectExitance = Row3.xyz + RhoOverPi * Irradiance;
    gDirectExitance[CardIdx] = float4(DirectExitance, Row0.w);
    gTargetExitance[CardIdx] = float4(DirectExitance, Row0.w);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void IterateCardRadiosityMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint CardIdx = DispatchThreadId.x;
    if (CardIdx >= gCardPush.CardCount)
    {
        return;
    }

    uint   BaseI = CardIdx * CardDescriptorStride;
    float4 Row0I = gCardDescriptors[BaseI + 0u];
    float4 Row1I = gCardDescriptors[BaseI + 1u];
    float4 Row2I = gCardDescriptors[BaseI + 2u];

    float3 PosI  = Row0I.xyz;
    float3 NormI = normalize(Row1I.xyz);
    float3 RhoI  = clamp(Row2I.xyz, float3(0.0f, 0.0f, 0.0f), float3(0.92f, 0.92f, 0.92f));
    float  Gain  = clamp(gCardPush.IndirectBounceGain, 0.0f, 1.35f);

    float3 GatheredIrradianceOverPi = float3(0.0f, 0.0f, 0.0f);

    [loop]
    for (uint J = 0u; J < gCardPush.CardCount; ++J)
    {
        if (J == CardIdx)
        {
            continue;
        }
        uint   BaseJ = J * CardDescriptorStride;
        float4 Row0J = gCardDescriptors[BaseJ + 0u];
        float4 Row1J = gCardDescriptors[BaseJ + 1u];

        float3 Delta  = Row0J.xyz - PosI;
        float  DistSq = dot(Delta, Delta);
        if (DistSq <= 1.0e-4f || DistSq > 6400.0f)
        {
            continue;
        }
        float  Dist  = sqrt(DistSq);
        float3 Omega = Delta / Dist;
        float  CosI  = max(dot(NormI, Omega), 0.0f);
        float  CosJ  = max(dot(normalize(Row1J.xyz), -Omega), 0.0f);
        if (CosI <= 1.0e-4f || CosJ <= 1.0e-4f)
        {
            continue;
        }

        float AreaJ      = max(Row0J.w, 0.02f);
        float FormFactor = (CosI * CosJ * AreaJ) / (3.14159265f * (DistSq + AreaJ * 0.318309886f));
        FormFactor       = min(FormFactor, 0.22f);

        float Vis = 1.0f;
        if (gCardPush.EnableVisibilityCones != 0u && FormFactor > 0.002f)
        {
            Vis = TraceVolumeConeVisibilityGpu(gDistanceVolume, PosI + NormI * 0.08f, Omega, max(Dist - 0.12f, 0.1f), 10.0f, 10u);
        }

        GatheredIrradianceOverPi += gSourceExitance[J].xyz * (FormFactor * Vis);
    }

    float3 NextExitance = gDirectExitance[CardIdx].xyz + (Gain * RhoI) * GatheredIrradianceOverPi;
    gTargetExitance[CardIdx] = float4(max(NextExitance, float3(0.0f, 0.0f, 0.0f)), Row0I.w);
}

#endif
