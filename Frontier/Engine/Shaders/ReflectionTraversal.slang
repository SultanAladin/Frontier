//==========================================================================================================================================
//                                                  REFLECTION TRAVERSAL
//==========================================================================================================================================
// 🧩 Heitz 2018 VNDF raytraced specular reflections (car clearcoat, brushed titanium rims, gold & crimson spheres):
//    - Hybrid Mode (default): raytraces Two-Level 8-Wide CWBVH triangles for glossy/clearcoat surfaces (Roughness <= 0.52 or Coat > 0.02),
//      evaluates deterministic ShadowRaster cascade visibility + SurfaceCard radiosity at hit, and substitutes analytical EnvBRDF * DiffuseGI
//      on rough matte ground/concrete surfaces (zero wasted BVH rays on matte asphalt, zero rough-specular fireflies!)
//    - Full Raytracing Mode (Option 2): raytraces VNDF reflection rays across all surfaces with CWBVH Beer-Lambert shadow rays at hit

#pragma once

#include "DirectRadiance.slang"
#include "SurfaceRadiositySampling.slang"
#include "SurfaceScatteringEvaluation.slang"

float3 EvaluateEnvBrdfApprox(float3 F0, float Roughness, float NdotV)
{
    const float4 C0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 C1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4       R  = Roughness * C0 + C1;
    float        A0 = min(R.x * R.x, exp2(-9.28f * NdotV)) * R.x + R.y;
    float2       AB = float2(-1.04f, 1.04f) * A0 + R.zw;
    return F0 * AB.x + float3(AB.y);
}

// Spatially decorrelated blue/white hash + Roberts R2 quasi-random temporal Cranley-Patterson rotation
float2 SampleR2Sequence(uint2 Pixel, uint FrameIndex, uint Salt)
{
    uint H = (Pixel.x * 1973u) ^ (Pixel.y * 9277u) ^ (Salt * 26699u) ^ 0x9E3779B9u;
    H      = ((H >> 16u) ^ H) * 0x45D9F3Bu;
    H      = ((H >> 16u) ^ H) * 0x45D9F3Bu;
    H      = (H >> 16u) ^ H;
    float2 Spatial = float2(H & 0xFFFFu, H >> 16u) * (1.0f / 65536.0f);
    float2 Anim    = frac(float(FrameIndex) * float2(0.754877666f, 0.569840291f));
    return frac(Spatial + Anim);
}

// Malley's disk-to-hemisphere cosine-weighted hemisphere sampler in tangent basis (T, B, N)
float3 SampleCosineHemisphere(float3 N, float3 T, float3 B, float2 Xi)
{
    float R   = sqrt(saturate(Xi.x));
    float Phi = 6.2831853f * Xi.y;
    float X   = R * cos(Phi);
    float Y   = R * sin(Phi);
    float Z   = sqrt(max(0.0f, 1.0f - Xi.x));
    return normalize(T * X + B * Y + N * Z);
}

float EvaluateDielectricFresnelUnpolarized(float CosTheta, float EtaIncident, float EtaTransmitted)
{
    float R0 = (EtaIncident - EtaTransmitted) / max(1.0e-4f, EtaIncident + EtaTransmitted);
    R0       = R0 * R0;
    return R0 + (1.0f - R0) * pow(1.0f - saturate(CosTheta), 5.0f);
}

