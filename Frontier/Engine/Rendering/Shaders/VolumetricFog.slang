//==========================================================================================================================================
//                                                    VOLUMETRIC FOG SLANG
//==========================================================================================================================================
// Couples analytic closed-form exponential height fog + planet-curved surface-distance aerial perspective +
// bounded 3D noise-modulated froxel volumetric fog boxes scattering CloudShadow-attenuated sun shafts,
// photometric IES/spot luminaires, branching lightning channels, and thermal/fire radiant facets.

#include "SolarSkyEnvironment.slang"

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4> Ensemble;
[[vk::binding(1, 0)]] Sampler2DArray<float4>   TransLut;
[[vk::binding(2, 0)]] Sampler2DArray<float4>   MultiLut;
[[vk::binding(3, 0)]] Sampler2DArray<float4>   CloudShadowLut;
[[vk::binding(4, 0)]] [format("rgba16f")] RWTexture2DArray<float4> Gbuffer;
[[vk::binding(5, 0)]] [format("rgba16f")] RWTexture2DArray<float4> HdrFrame;

float FogHeightDensityAtY(float4 Hf0, float HeightM)
{
    if (Hf0.x <= 0.0f)
    {
        return 0.0f;
    }
    float ExpArg = clamp(-Hf0.y * (HeightM - Hf0.z), -60.0f, 40.0f);
    return Hf0.x * exp(ExpArg);
}

float EvaluateLocalFogBoxExtinction(StructuredBuffer<float4> Ens,
                                    uint                     FB,
                                    float3                   PosM,
                                    float                    TimeSec,
                                    out float                OutAlbedo,
                                    out float                OutPhaseG)
{
    float4 F0 = Ens[FB + 0u]; // CenterXYZ, RefDensity
    float4 F1 = Ens[FB + 1u]; // HalfXYZ, ScatterAlbedo
    float4 F2 = Ens[FB + 2u]; // NoiseAmp, NoiseFreq, PhaseG, EdgeSoftnessM
    OutAlbedo = F1.w;
    OutPhaseG = F2.z;

    if (F0.w <= 0.0f)
    {
        return 0.0f;
    }
    float3 Rel  = PosM - F0.xyz;
    float3 Half = max(F1.xyz, float3(1.0e-3f));
    if (abs(Rel.x) >= Half.x || abs(Rel.y) >= Half.y || abs(Rel.z) >= Half.z)
    {
        return 0.0f;
    }
    float Soft = max(F2.w, 1.0e-3f);
    float Mx   = saturate((Half.x - abs(Rel.x)) / Soft);
    float My   = saturate((Half.y - abs(Rel.y)) / Soft);
    float Mz   = saturate((Half.z - abs(Rel.z)) / Soft);
    float Mask = Mx * Mx * (3.0f - 2.0f * Mx) * My * My * (3.0f - 2.0f * My) * Mz * Mz * (3.0f - 2.0f * Mz);

    float Mod = 1.0f;
    if (F2.x > 0.0f && F2.y > 0.0f)
    {
        float3 Adv = (PosM - float3(1.4f * TimeSec, 0.25f * TimeSec, 0.6f * TimeSec)) * F2.y;
        float  N   = FbmValue3D(Adv, 3, 409u);
        Mod        = max(0.0f, 1.0f + F2.x * (2.0f * N - 1.0f));
    }
    return F0.w * Mask * Mod;
}

float PointSegmentDistSq(float3 P, float3 A, float3 B)
{
    float3 Ab = B - A;
    float  L2 = dot(Ab, Ab);
    if (L2 <= 1.0e-12f)
    {
        float3 D = P - A;
        return dot(D, D);
    }
    float  T = saturate(dot(P - A, Ab) / L2);
    float3 C = A + Ab * T;
    float3 D = P - C;
    return dot(D, D);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateVolumetricFogMain(uint3 Tid : SV_DispatchThreadID)
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
    bool   IsSky     = (HitDistM < 0.0f);

    uint   RBase     = RealmSlotBase(Ensemble, RealmId);
    float4 R0        = Ensemble[RBase + 0u];
    float4 Hf0       = Ensemble[RBase + 6u]; // Density, Falloff, RefHeightM, MaxOpacity
    float4 Hf1       = Ensemble[RBase + 7u]; // StartDistM, PhaseG, Albedo, EnableAerialPerspective
    float4 R8        = Ensemble[RBase + 8u];
    float4 R10       = Ensemble[RBase + 10u];
    float4 R11       = Ensemble[RBase + 11u];
    float4 R12       = Ensemble[RBase + 12u];
    float  SpanM     = max(Ensemble[RBase + 13u].z, 1.0f);
    float  TimeSec   = Ensemble[3].w;

    uint SunFirst    = uint(R8.x + 0.5f);
    uint SunCount    = uint(R8.y + 0.5f);
    uint FogBoxTable = uint(Ensemble[6].y + 0.5f);
    uint FogBoxFirst = uint(R10.x + 0.5f);
    uint FogBoxCount = uint(R10.y + 0.5f);
    uint LumTable    = uint(Ensemble[6].z + 0.5f);
    uint LumFirst    = uint(R10.z + 0.5f);
    uint LumCount    = uint(R10.w + 0.5f);
    uint BoltTable   = uint(Ensemble[6].w + 0.5f);
    uint BoltFirst   = uint(R11.x + 0.5f);
    uint BoltCount   = uint(R11.y + 0.5f);
    uint SphTable    = uint(Ensemble[7].y + 0.5f);
    uint SphFirst    = uint(R11.z + 0.5f);
    uint SphCount    = uint(R11.w + 0.5f);
    uint FacetTable  = uint(Ensemble[7].x + 0.5f);
    uint FacetFirst  = uint(R12.z + 0.5f);
    uint FacetCount  = uint(R12.w + 0.5f);

    float3 BaseHdr = HdrFrame[uint3(Tid.xy, 0u)].rgb;

    // 1. Planet-curved surface-distance aerial perspective (on opaque surfaces)
    if (!IsSky && Hf1.w > 0.5f && HitDistM > 5.0f)
    {
        float  DistKm  = HitDistM * 1.0e-3f;
        float3 ApIn    = EvaluateCoupledSolarSky6Pt(Ensemble, TransLut, MultiLut, RealmId, RayDir, DistKm);
        float3 SigRay, SigT;
        float  SigMie;
        EvaluateMediumCoefficientsGpu(Ensemble, RBase, max(0.0f, R0.z), SigRay, SigMie, SigT);
        float3 ApTrans = exp(-SigT * DistKm);
        BaseHdr        = BaseHdr * ApTrans + ApIn * GlassTint;
    }

    float MaxMarchM = IsSky ? 64.0f : min(HitDistM, 120.0f);
    if (Hf0.x <= 0.0f && FogBoxCount == 0u)
    {
        HdrFrame[uint3(Tid.xy, 0u)] = float4(BaseHdr, 1.0f);
        return;
    }

    // 2. Quadratic froxel volumetric fog integration (height fog + local 3D fog boxes + sun shafts + IES + lightning + fire)
    const int kSlices = 36;
    float     Trans   = 1.0f;
    float3    InScat  = float3(0.0f);

    for (int I = 0; I < kSlices; ++I)
    {
        float U0  = float(I) / float(kSlices);
        float U1  = float(I + 1) / float(kSlices);
        float T0  = MaxMarchM * (U0 * U0);
        float T1  = MaxMarchM * (U1 * U1);
        float Ds  = T1 - T0;
        if (Ds <= 1.0e-5f)
        {
            continue;
        }
        float  TMid = 0.5f * (T0 + T1);
        float3 PosM = RayOrigin + RayDir * TMid;

        float  SigH = (TMid >= Hf1.x) ? FogHeightDensityAtY(Hf0, PosM.y) : 0.0f;
        float  SigT = SigH;
        float  SigS = SigH * Hf1.z;
        float  PhG  = SigS * Hf1.y;

        for (uint F = 0u; F < FogBoxCount; ++F)
        {
            float BAlb, BG;
            float BSig = EvaluateLocalFogBoxExtinction(Ensemble, FogBoxTable + (FogBoxFirst + F) * kFogBoxStride, PosM, TimeSec, BAlb, BG);
            if (BSig > 0.0f)
            {
                SigT += BSig;
                SigS += BSig * BAlb;
                PhG  += BSig * BAlb * BG;
            }
        }
        if (SigT <= 1.0e-7f)
        {
            continue;
        }
        float MeanG = (SigS > 1.0e-7f) ? (PhG / SigS) : 0.0f;

        float3 Lum = float3(0.012f, 0.016f, 0.024f); // Ambient diffuse sky scatter in fog

        // Solar god-rays modulated by CloudShadowArray + sphere shadow rays!
        for (uint S = 0u; S < SunCount; ++S)
        {
            uint   SB     = SunSlotBase(Ensemble, SunFirst + S);
            float3 SunDir = Ensemble[SB + 0u].xyz;
            float3 SunIrr = Ensemble[SB + 1u].xyz;
            if (SunDir.y <= -0.05f)
            {
                continue;
            }
            // Check sphere shadow for crisp crepuscular rays
            bool Blocked = false;
            for (uint Sp = 0u; Sp < SphCount; ++Sp)
            {
                float4 S0 = Ensemble[SphTable + (SphFirst + Sp) * kSphereStride];
                float3 Oc = PosM - S0.xyz;
                float  B  = dot(Oc, SunDir);
                float  Det = B * B - (dot(Oc, Oc) - S0.w * S0.w);
                if (Det > 0.0f && (-B - sqrt(Det)) > 0.0f)
                {
                    Blocked = true;
                    break;
                }
            }
            if (Blocked)
            {
                continue;
            }
            float2 ShUv    = clamp(0.5f + 0.5f * (PosM.xz / SpanM), 0.0f, 1.0f);
            float  CloudTr = CloudShadowLut.SampleLevel(float3(ShUv, float(RealmId)), 0.0f).g;
            float3 AtmoTr  = SampleTransmittanceLutArray(TransLut, RealmId, R0.x, R0.y, R0.x + max(R0.z, 1.0e-3f), SunDir.y);
            float  Phase   = HenyeyGreensteinVal(dot(RayDir, SunDir), MeanG);
            Lum           += SunIrr * AtmoTr * (CloudTr * Phase);
        }

        // Clustered IES / spot luminaires
        for (uint L = 0u; L < LumCount; ++L)
        {
            uint   LB   = LumTable + (LumFirst + L) * kLuminaireStride;
            float4 L0   = Ensemble[LB + 0u];
            float4 L1   = Ensemble[LB + 1u];
            float4 L2   = Ensemble[LB + 2u];
            float3 ToP  = PosM - L0.xyz;
            float  D2   = dot(ToP, ToP);
            if (D2 < L2.w * L2.w)
            {
                float  Dist  = sqrt(max(D2, 1.0e-3f));
                float3 LDir  = ToP / Dist;
                float  CosC  = dot(LDir, L1.xyz);
                if (CosC >= L1.w)
                {
                    float Cone = smoothstep(L1.w, min(1.0f, L1.w + 0.22f), CosC);
                    float Win  = pow(saturate(1.0f - D2 / max(L2.w * L2.w, 1.0e-2f)), 2.0f);
                    float Ph   = HenyeyGreensteinVal(dot(RayDir, -LDir), MeanG);
                    Lum       += L2.xyz * (L0.w * Cone * Win / (D2 + 0.25f) * Ph);
                }
            }
        }

        // Branching lightning bolt channels
        for (uint B = 0u; B < BoltCount; ++B)
        {
            uint   BB    = BoltTable + (BoltFirst + B) * kLightningStride;
            float4 B0    = Ensemble[BB + 0u];
            float4 B1    = Ensemble[BB + 1u];
            float4 B2    = Ensemble[BB + 2u];
            float  Dist  = sqrt(PointSegmentDistSq(PosM, B0.xyz, B1.xyz));
            float  Atten = exp(-B2.w * max(0.0f, Dist - B1.w)) / (1.0f + 0.08f * Dist * Dist);
            Lum         += B2.xyz * (B0.w * Atten * kInv4Pi);
        }

        // Thermal fire / radiant area facets
        for (uint Fc = 0u; Fc < FacetCount; ++Fc)
        {
            uint   FB   = FacetTable + (FacetFirst + Fc) * kFacetStride;
            float4 F0   = Ensemble[FB + 0u];
            float4 F1   = Ensemble[FB + 1u];
            float4 F2   = Ensemble[FB + 2u];
            float3 ToP  = PosM - F0.xyz;
            float  D2   = dot(ToP, ToP);
            if (D2 < F1.w * F1.w)
            {
                float3 Dir  = ToP * rsqrt(max(D2, 1.0e-3f));
                float  CosE = (F2.w > 0.5f) ? abs(dot(F1.xyz, Dir)) : max(0.0f, dot(F1.xyz, Dir));
                float  Win  = saturate(1.0f - sqrt(D2) / max(F1.w, 1.0e-2f));
                Lum        += F2.xyz * (F0.w * CosE * Win * Win / (D2 + 0.5f) * kInv4Pi);
            }
        }

        float StepTr = exp(-SigT * Ds);
        float IntW   = (SigS / SigT) * (1.0f - StepTr);
        InScat      += Trans * Lum * IntW;
        Trans       *= StepTr;
    }

    // Analytic closed-form exponential height fog tail beyond MaxMarchM up to distant surface!
    if (!IsSky && Hf0.x > 0.0f && HitDistM > MaxMarchM)
    {
        float3 StartP = RayOrigin + RayDir * MaxMarchM;
        float  RemM   = HitDistM - MaxMarchM;
        float  Dh     = RayDir.y * RemM;
        float  DStart = FogHeightDensityAtY(Hf0, StartP.y);
        float  TauRem = (abs(Hf0.y * Dh) < 1.0e-3f)
                            ? (DStart * RemM * (1.0f - 0.5f * Hf0.y * Dh))
                            : (DStart * RemM * (1.0f - exp(-Hf0.y * Dh)) / (Hf0.y * Dh));
        float  TailTr = max(1.0f - Hf0.w, exp(-max(0.0f, TauRem)));
        InScat       += Trans * (1.0f - TailTr) * float3(0.035f, 0.045f, 0.060f) * Hf1.z;
        Trans        *= TailTr;
    }

    Trans = max(Trans, 1.0f - saturate(Hf0.w));
    HdrFrame[uint3(Tid.xy, 0u)] = float4(BaseHdr * Trans + InScat * GlassTint, 1.0f);
}
