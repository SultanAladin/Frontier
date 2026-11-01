//==========================================================================================================================================
//                                               SURFACE RADIOSITY SAMPLING
//==========================================================================================================================================
// 🧩 Spatial + normal-aligned interpolation of multi-bounce GPU surface card radiosity (B = R_0 + rho * F * B)
//    at arbitrary world-space SDF sphere-trace or secondary ray hit locations.

#pragma once

float3 SampleGpuSurfaceRadiosity(StructuredBuffer<float4> PatchGeometry,
                                 StructuredBuffer<float4> PatchRadiosity,
                                 uint                     PatchCount,
                                 float3                   WorldPoint,
                                 float3                   SurfaceNormal)
{
    if (PatchCount == 0u)
    {
        return float3(0.0f);
    }

    float3 Accum  = float3(0.0f);
    float  Weight = 0.0f;

    for (uint Idx = 0u; Idx < PatchCount; ++Idx)
    {
        uint   Base  = Idx * 4u;
        float4 G0    = PatchGeometry[Base + 0u];
        float4 G1    = PatchGeometry[Base + 1u];
        float  Align = max(0.0f, dot(G1.xyz, SurfaceNormal));
        if (Align <= 0.05f)
        {
            continue;
        }
        float3 Diff  = WorldPoint - G0.xyz;
        float  Dist2 = dot(Diff, Diff);
        float  Sigma = max(0.75f, G1.w * 1.8f);
        float  W     = (Align * Align) / (1.0f + Dist2 / (Sigma * Sigma));
        Accum       += W * PatchRadiosity[Idx].xyz;
        Weight      += W;
    }

    return (Weight > 1.0e-6f) ? (Accum / Weight) : float3(0.0f);
}

float3 SampleGpuCombinedSurfaceRadiosity(RWStructuredBuffer<float4> CombinedPack,
                                         uint                       PatchCount,
                                         float3                     WorldPoint,
                                         float3                     SurfaceNormal)
{
    if (PatchCount == 0u)
    {
        return float3(0.0f);
    }

    uint   RadBase = PatchCount * 4u;
    float3 Accum   = float3(0.0f);
    float  Weight  = 0.0f;

    for (uint Idx = 0u; Idx < PatchCount; ++Idx)
    {
        uint   Base  = Idx * 4u;
        float4 G0    = CombinedPack[Base + 0u];
        float4 G1    = CombinedPack[Base + 1u];
        float  Align = max(0.0f, dot(G1.xyz, SurfaceNormal));
        if (Align <= 0.05f)
        {
            continue;
        }
        float3 Diff  = WorldPoint - G0.xyz;
        float  Dist2 = dot(Diff, Diff);
        float  Sigma = max(0.75f, G1.w * 1.8f);
        float  W     = (Align * Align) / (1.0f + Dist2 / (Sigma * Sigma));
        Accum       += W * CombinedPack[RadBase + Idx].xyz;
        Weight      += W;
    }

    return (Weight > 1.0e-6f) ? (Accum / Weight) : float3(0.0f);
}
