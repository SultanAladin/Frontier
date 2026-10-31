//==========================================================================================================================================
//                                                  VOLUMETRIC CLOUDS SLANG
//==========================================================================================================================================
// 1. ProjectCloudShadowMain: 256x256xR orthographic optical-depth & transmittance projection per realm.
// 2. TraceVolumetricCloudsMain: 3D Perlin-Worley global slabs + bounded local boxes with Beers-Powder multi-scattering.
// 3. ReconstructVolumetricCloudsMain: RealmId-aware & reversed-Z depth-aware bilateral spatial + temporal filter.

#include "SolarSkyEnvironment.slang"

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4> Ensemble;
[[vk::binding(1, 0)]] Sampler2DArray<float4>   TransLut;
[[vk::binding(2, 0)]] [format("rgba16f")] RWTexture2DArray<float4> Gbuffer;
[[vk::binding(3, 0)]] [format("rgba16f")] RWTexture2DArray<float4> CloudWorkTarget; // Shadow map OR CloudRaw OR CloudHistory
[[vk::binding(4, 0)]] [format("rgba16f")] RWTexture2DArray<float4> SecondaryTarget; // Unused OR CloudHistoryRead OR HdrFrame

float EvaluateCloudExtinctionAtPoint(StructuredBuffer<float4> Ens, uint CB, float3 PosM)
{
    float4 C0 = Ens[CB + 0u]; // BaseAltM, ThicknessM, Coverage, DensityScale
    float4 C1 = Ens[CB + 1u]; // DetailErosion, ExtinctionPerM, ScatterAlbedo, ForwardG
    float4 C2 = Ens[CB + 2u]; // BackG, ForwardWeight, PowderStrength, IsLocalBox
    float4 C3 = Ens[CB + 3u]; // BoxCenterXYZ, EdgeSoftnessM
    float4 C4 = Ens[CB + 4u]; // BoxHalfXYZ, WindOffsetX

    float HFrac    = 0.0f;
    float BoxMask  = 1.0f;

    if (C2.w > 0.5f)
    {
        float3 Local = PosM - C3.xyz;
        float3 Half  = max(C4.xyz, float3(1.0e-2f));
        if (abs(Local.x) >= Half.x || abs(Local.y) >= Half.y || abs(Local.z) >= Half.z)
        {
            return 0.0f;
        }
        float Soft = max(C3.w, 1.0e-2f);
        float Mx   = saturate((Half.x - abs(Local.x)) / Soft);
        float My   = saturate((Half.y - abs(Local.y)) / Soft);
        float Mz   = saturate((Half.z - abs(Local.z)) / Soft);
        BoxMask    = Mx * Mx * (3.0f - 2.0f * Mx) * My * My * (3.0f - 2.0f * My) * Mz * Mz * (3.0f - 2.0f * Mz);
        HFrac      = saturate((Local.y + Half.y) / (2.0f * Half.y));
    }
    else
    {
        if (C0.y <= 0.0f || PosM.y < C0.x || PosM.y > C0.x + C0.y)
        {
            return 0.0f;
        }
        HFrac = saturate((PosM.y - C0.x) / C0.y);
    }

    float Grad = smoothstep(0.0f, 0.14f, HFrac) * smoothstep(1.0f, 0.72f, HFrac);
    if (Grad <= 1.0e-5f || BoxMask <= 1.0e-5f)
    {
        return 0.0f;
    }

    float3 Advected = PosM + float3(C4.w, 0.0f, 0.35f * C4.w);
    float  Freq     = (C2.w > 0.5f) ? (1.8f / max(max(C4.x, C4.z), 4.0f)) : 3.2e-4f;
    float3 SampleP  = Advected * Freq;

    float BaseFbm = FbmValue3D(SampleP, 4, 17u);
    float Worley0 = WorleyInverted3D(SampleP * 2.1f, 53u);
    float Shape   = 0.62f * BaseFbm + 0.38f * Worley0;
    float Thresh  = 1.0f - saturate(C0.z);
    float Remap   = saturate((Shape - Thresh) / max(1.0f - Thresh, 1.0e-3f)) * Grad * BoxMask;
    if (Remap <= 1.0e-5f)
    {
        return 0.0f;
    }

    float Detail = 0.65f * WorleyInverted3D(SampleP * 6.4f, 131u) + 0.35f * FbmValue3D(SampleP * 11.5f, 3, 257u);
    float Eroded = saturate((Remap - C1.x * (1.0f - Detail) * (1.0f - 0.55f * HFrac)) / max(1.0f - 0.5f * C1.x, 0.15f));
    return Eroded * max(0.0f, C0.w) * max(0.0f, C1.y);
}

//------------------------------------------------------------------------------------------------------------------------
//                          1. ORTHOGRAPHIC CLOUD SHADOW PROJECTION (256 x 256 per Realm)
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void ProjectCloudShadowMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }
    uint   RealmId    = Push.Counts.z;
    uint   RBase      = RealmSlotBase(Ensemble, RealmId);
    float4 R8         = Ensemble[RBase + 8u];
    float4 R9         = Ensemble[RBase + 9u];
    float  HalfSpanM  = max(Ensemble[RBase + 13u].z, 1.0f);

    uint   SunFirst   = uint(R8.x + 0.5f);
    uint   SunCount   = uint(R8.y + 0.5f);
    uint   CloudTable = uint(Ensemble[6].x + 0.5f);
    uint   CloudFirst = uint(R9.z + 0.5f);
    uint   CloudCount = uint(R9.w + 0.5f);

    if (CloudCount == 0u || SunCount == 0u)
    {
        CloudWorkTarget[uint3(Tid.xy, RealmId)] = float4(0.0f, 1.0f, 0.0f, 1.0f);
        return;
    }

    float3 PrimSun = Ensemble[SunSlotBase(Ensemble, SunFirst)].xyz;
    if (PrimSun.y <= 0.02f)
    {
        CloudWorkTarget[uint3(Tid.xy, RealmId)] = float4(0.0f, 1.0f, 0.0f, 1.0f);
        return;
    }

    float2 Uv     = (float2(Tid.xy) + 0.5f) / float2(Push.Counts.xy);
    float3 GndPt  = float3((Uv.x * 2.0f - 1.0f) * HalfSpanM, Ensemble[RBase + 4u].y, (Uv.y * 2.0f - 1.0f) * HalfSpanM);

    floatTauAccum :
    float Tau       = 0.0f;
    float WeightedH = 0.0f;
    for (uint C = 0u; C < CloudCount; ++C)
    {
        uint   CB   = CloudTable + (CloudFirst + C) * kCloudStride;
        float4 C0   = Ensemble[CB + 0u];
        float4 C2   = Ensemble[CB + 2u];
        float4 C3   = Ensemble[CB + 3u];
        float4 C4   = Ensemble[CB + 4u];

        float LowY  = (C2.w > 0.5f) ? (C3.y - C4.y) : C0.x;
        float HighY = (C2.w > 0.5f) ? (C3.y + C4.y) : (C0.x + C0.y);
        float T0    = max(0.0f, (LowY - GndPt.y) / PrimSun.y);
        float T1    = max(T0, (HighY - GndPt.y) / PrimSun.y);
        if (T1 <= T0)
        {
            continue;
        }
        const int kSteps = 12;
        float     Ds     = (T1 - T0) / float(kSteps);
        for (int S = 0; S < kSteps; ++S)
        {
            float3 P    = GndPt + PrimSun * (T0 + (float(S) + 0.5f) * Ds);
            float  SigT = EvaluateCloudExtinctionAtPoint(Ensemble, CB, P);
            if (SigT > 0.0f)
            {
                float Dtau = SigT * Ds;
                Tau       += Dtau;
                WeightedH += Dtau * P.y;
            }
        }
    }

    float MeanH = (Tau > 1.0e-5f) ? (WeightedH / Tau) : 0.0f;
    float Trans = clamp(0.75f * exp(-Tau) + 0.25f * exp(-0.35f * Tau), 0.0f, 1.0f);
    CloudWorkTarget[uint3(Tid.xy, RealmId)] = float4(Tau, Trans, MeanH, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  2. RAY-MARCH GLOBAL & LOCAL 3D VOLUMETRIC CLOUDS
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void TraceVolumetricCloudsMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    float4 Gb1       = Gbuffer[uint3(Tid.xy, 1u)];
    float4 Gb2       = Gbuffer[uint3(Tid.xy, 2u)];
    float3 GlassTint = Gbuffer[uint3(Tid.xy, 3u)].rgb;
    uint   RealmId   = uint(Gb1.w + 0.5f);
    float3 RayOrigin = Gb1.xyz;
    float3 RayDir    = SafeNormalize(Gb2.xyz);
    float  HitDistM  = Gb2.w;
    float  MaxDistM  = (HitDistM > 0.0f) ? HitDistM : 45000.0f;

    uint   RBase      = RealmSlotBase(Ensemble, RealmId);
    float4 R0         = Ensemble[RBase + 0u];
    float4 R8         = Ensemble[RBase + 8u];
    float4 R9         = Ensemble[RBase + 9u];
    uint   SunFirst   = uint(R8.x + 0.5f);
    uint   SunCount   = uint(R8.y + 0.5f);
    uint   CloudTable = uint(Ensemble[6].x + 0.5f);
    uint   CloudFirst = uint(R9.z + 0.5f);
    uint   CloudCount = uint(R9.w + 0.5f);

    if (CloudCount == 0u)
    {
        CloudWorkTarget[uint3(Tid.xy, 0u)] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // Interleaved gradient noise spatial jitter (smooth, zero salt-and-pepper stipple)
    float  Jitter        = frac(52.9829189f * frac(0.06711056f * float(Tid.x) + 0.00583715f * float(Tid.y)));
    float  Transmittance = 1.0f;
    float3 InScatter     = float3(0.0f);

    // March near bounded local 3D cloud boxes first (pass 0), then distant planetary cloud slabs (pass 1)
    for (int DomainOrder = 0; DomainOrder < 2; ++DomainOrder)
    {
        for (uint C = 0u; C < CloudCount; ++C)
        {
            if (Transmittance < 0.01f)
            {
                break;
            }
            uint   CB    = CloudTable + (CloudFirst + C) * kCloudStride;
            float4 C0    = Ensemble[CB + 0u];
            float4 C1    = Ensemble[CB + 1u];
            float4 C2    = Ensemble[CB + 2u];
            float4 C3    = Ensemble[CB + 3u];
            float4 C4    = Ensemble[CB + 4u];
            bool   IsBox = (C2.w > 0.5f);
            if ((DomainOrder == 0) != IsBox)
            {
                continue;
            }

            float Near = 1.0e30f;
            float Far  = 0.0f;
            if (IsBox)
            {
                float3 InvD = 1.0f / select(abs(RayDir) > 1.0e-6f, RayDir, float3(1.0e-6f));
                float3 T0v  = (C3.xyz - C4.xyz - RayOrigin) * InvD;
                float3 T1v  = (C3.xyz + C4.xyz - RayOrigin) * InvD;
                float3 Tmin = min(T0v, T1v);
                float3 Tmax = max(T0v, T1v);
                Near        = max(0.0f, max(Tmin.x, max(Tmin.y, Tmin.z)));
                Far         = min(MaxDistM, min(Tmax.x, min(Tmax.y, Tmax.z)));
            }
            else if (C0.y > 0.0f)
            {
                if (abs(RayDir.y) > 1.0e-4f)
                {
                    float Ta = (C0.x - RayOrigin.y) / RayDir.y;
                    float Tb = (C0.x + C0.y - RayOrigin.y) / RayDir.y;
                    Near     = max(0.0f, min(Ta, Tb));
                    Far      = min(MaxDistM, max(Ta, Tb));
                }
                else if (RayOrigin.y >= C0.x && RayOrigin.y <= C0.x + C0.y)
                {
                    Near = 0.0f;
                    Far  = min(MaxDistM, 18000.0f);
                }
            }

            if (Far <= Near)
            {
                continue;
            }

            int   MarchSteps = IsBox ? 14 : 18;
            float Ds         = (Far - Near) / float(MarchSteps);
            float LightBaseM = IsBox ? 3.5f : 45.0f;

            for (int Step = 0; Step < MarchSteps; ++Step)
            {
                if (Transmittance < 0.01f)
                {
                    break;
                }
                float3 PosM = RayOrigin + RayDir * (Near + (float(Step) + Jitter) * Ds);
                float  SigT = EvaluateCloudExtinctionAtPoint(Ensemble, CB, PosM);
                if (SigT <= 1.0e-6f)
                {
                    continue;
                }

                float Albedo  = C1.z;
                float FwdG    = C1.w;
                float BackG   = C2.x;
                float FwdWt   = C2.y;
                float PowderK = C2.z;

                float3 StepLum = float3(0.0f);
                for (uint S = 0u; S < SunCount; ++S)
                {
                    uint   SB     = SunSlotBase(Ensemble, SunFirst + S);
                    float3 SunDir = Ensemble[SB + 0u].xyz;
                    float3 SunIrr = Ensemble[SB + 1u].xyz;
                    if (SunDir.y <= -0.10f)
                    {
                        continue;
                    }

                    float TauSun = 0.0f;
                    [unroll]
                    for (int Ls = 0; Ls < 4; ++Ls)
                    {
                        float  DistL = LightBaseM * float(1 << Ls);
                        float3 LPos  = PosM + SunDir * DistL;
                        TauSun      += EvaluateCloudExtinctionAtPoint(Ensemble, CB, LPos) * DistL;
                    }

                    float  CosTh  = dot(RayDir, SunDir);
                    float  Phase0 = lerp(HenyeyGreensteinVal(CosTh, BackG), HenyeyGreensteinVal(CosTh, FwdG), FwdWt);
                    float  Powder = 1.0f - PowderK * exp(-2.0f * TauSun);
                    float3 AtmoTr = SampleTransmittanceLutArray(TransLut, RealmId, R0.x, R0.y, R0.x + max(PosM.y * 1.0e-3f, 1.0e-3f), SunDir.y);

                    float Oct0 = exp(-TauSun) * Phase0 * Powder;
                    float Oct1 = 0.55f * exp(-0.35f * TauSun) * lerp(kInv4Pi, Phase0, 0.45f);
                    StepLum   += SunIrr * AtmoTr * Albedo * (Oct0 + Oct1);
                }
                StepLum += float3(0.032f, 0.044f, 0.062f) * Albedo;

                float StepTr   = exp(-SigT * Ds);
                float IntW     = (1.0f - StepTr);
                InScatter     += Transmittance * StepLum * IntW;
                Transmittance *= StepTr;
            }
        }
    }

    CloudWorkTarget[uint3(Tid.xy, 0u)] = float4(InScatter * GlassTint, saturate(Transmittance));
}

//------------------------------------------------------------------------------------------------------------------------
//                 3. REALM-AWARE & DEPTH-AWARE BILATERAL SPATIAL + TEMPORAL CLOUD RECONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void ReconstructVolumetricCloudsMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    uint   CenterRealm = uint(Gbuffer[uint3(Tid.xy, 1u)].w + 0.5f);
    float  CenterRevZ  = Gbuffer[uint3(Tid.xy, 0u)].w;
    float4 CenterCloud = CloudWorkTarget[uint3(Tid.xy, 0u)];

    // 3x3 edge-stopping bilateral filter (never bleeds across Portal realm boundaries or depth silhouettes!)
    float4 Accum = float4(0.0f);
    float  WSum  = 0.0f;
    for (int Dy = -1; Dy <= 1; ++Dy)
    {
        int Py = clamp(int(Tid.y) + Dy, 0, int(Push.Counts.y) - 1);
        for (int Dx = -1; Dx <= 1; ++Dx)
        {
            int    Px    = clamp(int(Tid.x) + Dx, 0, int(Push.Counts.x) - 1);
            uint3  Coord = uint3(uint(Px), uint(Py), 0u);
            uint   NRealm = uint(Gbuffer[uint3(Coord.xy, 1u)].w + 0.5f);
            if (NRealm != CenterRealm)
            {
                continue;
            }
            float  NRevZ  = Gbuffer[uint3(Coord.xy, 0u)].w;
            float  DepthW = exp(-abs(NRevZ - CenterRevZ) * 120.0f);
            float  SpatW  = (Dx == 0 && Dy == 0) ? 1.0f : ((Dx == 0 || Dy == 0) ? 0.6f : 0.36f);
            float  W      = SpatW * DepthW;
            Accum += CloudWorkTarget[Coord] * W;
            WSum  += W;
        }
    }
    float4 Filtered = (WSum > 0.0f) ? (Accum / WSum) : CenterCloud;

    // Temporal blend when history is valid (Push.Counts.w != 0 => blend with previous history stored in CloudWorkTarget layer 1)
    if (Push.Counts.w != 0u)
    {
        float4 Prev = CloudWorkTarget[uint3(Tid.xy, 1u)];
        // Neighborhood clamp to prevent ghosting
        float4 MinC = min(CenterCloud, Filtered) * 0.85f;
        float4 MaxC = max(CenterCloud, Filtered) * 1.15f + float4(0.01f);
        Prev        = clamp(Prev, MinC, MaxC);
        Filtered    = lerp(Filtered, Prev, 0.80f);
    }

    // Store reconstructed cloud state in layer 1 (history + transmittance for FlareVisibility!)
    CloudWorkTarget[uint3(Tid.xy, 1u)] = Filtered;

    // Composite clouds onto HdrFrame
    float3 BaseHdr              = SecondaryTarget[uint3(Tid.xy, 0u)].rgb;
    float3 Composited           = BaseHdr * Filtered.w + Filtered.rgb;
    SecondaryTarget[uint3(Tid.xy, 0u)] = float4(Composited, 1.0f);
}
