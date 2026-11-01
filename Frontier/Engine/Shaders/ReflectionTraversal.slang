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

// 📝 Matches the reference's PCG permutation and independently rotated R2 coordinates.
uint PermuteSampleIndex(uint Input)
{
    uint Advanced = Input * 747796405u + 2891336453u;
    uint Word     = ((Advanced >> ((Advanced >> 28u) + 4u)) ^ Advanced) * 277803737u;
    return (Word >> 22u) ^ Word;
}

float2 SampleR2Sequence(uint2 Pixel, uint FrameIndex, uint Salt)
{
    uint First  = PermuteSampleIndex((Pixel.x * 73856093u) ^ (Pixel.y * 19349663u) ^ (Salt * 83492791u));
    uint Second = PermuteSampleIndex((Pixel.y * 73856093u) ^ (Pixel.x * 19349663u) ^ (Salt * 2654435761u));
    float2 Start = float2(First >> 8u, Second >> 8u) * (1.0f / 16777216.0f);
    return frac(Start + float(FrameIndex % 65536u) * float2(0.7548776662466927f, 0.5698402909980532f));
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

