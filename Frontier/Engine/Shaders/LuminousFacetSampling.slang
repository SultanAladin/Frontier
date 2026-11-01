//==========================================================================================================================================
//                                                LUMINOUS FACET SAMPLING
//==========================================================================================================================================
// 🧩 O(log N) GPU binary-search importance sampler over flux-weighted self-radiant triangles (LuminousFacetAtlas)
//    returning exact uniform triangle area sample, geometric normal, radiant exitance, & solid-angle PDF.

#pragma once

struct GpuLuminousFacetSample
{
    bool   Valid;
    uint   FacetIndex;
    float3 WorldPoint;
    float3 LightNormal;
    float3 SelfRadiance;
    float  SolidAnglePdf;
};

GpuLuminousFacetSample SampleGpuLuminousFacet(StructuredBuffer<float4> LuminousTable,
                                              uint                     FacetCount,
                                              float3                   ReceiverPos,
                                              float                    XiCdf,
                                              float                    XiU,
                                              float                    XiV)
{
    GpuLuminousFacetSample Out;
    Out.Valid         = false;
    Out.FacetIndex    = 0u;
    Out.WorldPoint    = ReceiverPos;
    Out.LightNormal   = float3(0.0f, -1.0f, 0.0f);
    Out.SelfRadiance  = float3(0.0f);
    Out.SolidAnglePdf = 0.0f;

    if (FacetCount == 0u)
    {
        return Out;
    }

    float Target = clamp(XiCdf, 0.0f, 0.999999f);
    uint  Lo     = 0u;
    uint  Hi     = FacetCount - 1u;
    for (int Iter = 0; Iter < 12; ++Iter)
    {
        if (Lo >= Hi)
        {
            break;
        }
        uint  Mid = Lo + ((Hi - Lo) >> 1u);
        float Cdf = LuminousTable[Mid * 4u + 1u].w;
        if (Target <= Cdf)
        {
            Hi = Mid;
        }
        else
        {
            Lo = Mid + 1u;
        }
    }

    uint   Base = Lo * 4u;
    float4 L0   = LuminousTable[Base + 0u];
    float4 L1   = LuminousTable[Base + 1u];
    float4 L2   = LuminousTable[Base + 2u];
    float4 L3   = LuminousTable[Base + 3u];

    float Su = sqrt(saturate(XiU));
    float B1 = 1.0f - Su;
    float B2 = saturate(XiV) * Su;

    Out.Valid        = true;
    Out.FacetIndex   = Lo;
    Out.WorldPoint   = L0.xyz + B1 * L1.xyz + B2 * L2.xyz;
    float3 CrossVec  = cross(L1.xyz, L2.xyz);
    float  CrossLen  = length(CrossVec);
    Out.LightNormal  = (CrossLen > 1.0e-8f) ? (CrossVec / CrossLen) : float3(0.0f, 1.0f, 0.0f);
    Out.SelfRadiance = L3.xyz;

    float3 ToLight = Out.WorldPoint - ReceiverPos;
    float  Dist2   = max(1.0e-5f, dot(ToLight, ToLight));
    float  Dist    = sqrt(Dist2);
    float  AbsCosL = max(1.0e-4f, abs(dot(Out.LightNormal, ToLight / Dist)));
    float  Area    = max(1.0e-6f, L0.w);
    float  Mass    = max(1.0e-7f, L2.w);

    Out.SolidAnglePdf = (Mass / Area) * (Dist2 / AbsCosL);
    return Out;
}
