//==========================================================================================================================================
//                                                    ATMOSPHERE LUT SLANG
//==========================================================================================================================================
// Per-realm RGBA16F 2D-Array compute bakes: Transmittance (256x64), Multi-Scattering (32x32), and Sky-View (192x108).

#include "SolarSkyEnvironment.slang"

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4>   Ensemble;
[[vk::binding(1, 0)]] Sampler2DArray<float4>     SampledTransLut;
[[vk::binding(2, 0)]] Sampler2DArray<float4>     SampledMultiLut;
[[vk::binding(3, 0)]] [format("rgba16f")] RWTexture2DArray<float4> OutputLut;

void TransmittanceUvToRadiusMu(float BottomR, float TopR, float2 Uv, out float OutR, out float OutMu)
{
    float H    = sqrt(max(TopR * TopR - BottomR * BottomR, 1.0e-5f));
    float Rho  = H * saturate(Uv.y);
    OutR       = sqrt(Rho * Rho + BottomR * BottomR);
    float DMin = TopR - OutR;
    float DMax = Rho + H;
    float D    = DMin + saturate(Uv.x) * (DMax - DMin);
    OutMu      = (D <= 1.0e-5f) ? 1.0f : clamp((H * H - Rho * Rho - D * D) / (2.0f * OutR * D), -1.0f, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                      1. TRANSMITTANCE LUT (256 x 64 per Realm)
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void BakeTransmittanceLutMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }
    uint   RealmId = Push.Counts.z;
    uint   RBase   = RealmSlotBase(Ensemble, RealmId);
    float4 R0      = Ensemble[RBase + 0u];
    float  BottomR = R0.x;
    float  TopR    = R0.y;

    float2 Uv = (float2(Tid.xy) + 0.5f) / float2(Push.Counts.xy);
    float  RadKm, Mu;
    TransmittanceUvToRadiusMu(BottomR, TopR, Uv, RadKm, Mu);

    float3 Origin = float3(0.0f, RadKm, 0.0f);
    float3 Dir    = float3(sqrt(max(0.0f, 1.0f - Mu * Mu)), Mu, 0.0f);

    float ExitLen = DistanceToSphereExit(Origin, Dir, TopR);
    if (ExitLen <= 0.0f)
    {
        OutputLut[uint3(Tid.xy, RealmId)] = float4(1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }

    const int kSteps = 40;
    float     Ds     = ExitLen / float(kSteps);
    float3    Tau    = float3(0.0f);
    for (int I = 0; I < kSteps; ++I)
    {
        float3 P     = Origin + Dir * ((float(I) + 0.5f) * Ds);
        float  AltKm = max(0.0f, length(P) - BottomR);
        float3 SigRay, SigT;
        float  SigMie;
        EvaluateMediumCoefficientsGpu(Ensemble, RBase, AltKm, SigRay, SigMie, SigT);
        Tau += SigT * Ds;
    }

    OutputLut[uint3(Tid.xy, RealmId)] = float4(exp(-Tau), 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                    2. MULTI-SCATTERING LUT (32 x 32 per Realm)
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void BakeMultiScatterLutMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }
    uint   RealmId = Push.Counts.z;
    uint   RBase   = RealmSlotBase(Ensemble, RealmId);
    float4 R0      = Ensemble[RBase + 0u];
    float  BottomR = R0.x;
    float  TopR    = R0.y;
    float  GroundA = R0.w;

    float2 Uv      = (float2(Tid.xy) + 0.5f) / float2(Push.Counts.xy);
    float  CosSun  = clamp(Uv.x * 2.0f - 1.0f, -1.0f, 1.0f);
    float  RadKm   = BottomR + saturate(Uv.y) * max(TopR - BottomR - 1.0e-3f, 1.0e-3f);
    float3 Origin  = float3(0.0f, RadKm, 0.0f);
    float3 SunDir  = float3(sqrt(max(0.0f, 1.0f - CosSun * CosSun)), CosSun, 0.0f);

    const int kDirCount   = 16;
    const int kMarchSteps = 16;
    float     SolidWeight = 4.0f * kPi / float(kDirCount);
    float3    L2ndSum     = float3(0.0f);
    float3    FmsSum      = float3(0.0f);

    for (int DIdx = 0; DIdx < kDirCount; ++DIdx)
    {
        float CosTheta = 1.0f - 2.0f * (float(DIdx) + 0.5f) / float(kDirCount);
        float SinTheta = sqrt(max(0.0f, 1.0f - CosTheta * CosTheta));
        float Phi      = 2.39996323f * float(DIdx);
        float3 RayDir  = float3(SinTheta * cos(Phi), CosTheta, SinTheta * sin(Phi));

        float MaxT     = DistanceToSphereExit(Origin, RayDir, TopR);
        float GroundT;
        bool  HitGnd   = HitsPlanetGround(Origin, RayDir, BottomR, GroundT);
        if (HitGnd)
        {
            MaxT = min(MaxT, GroundT);
        }
        float  Ds    = MaxT / float(kMarchSteps);
        float3 TPath = float3(1.0f);
        float3 LDir  = float3(0.0f);
        float3 FDir  = float3(0.0f);

        for (int S = 0; S < kMarchSteps; ++S)
        {
            float3 P      = Origin + RayDir * ((float(S) + 0.5f) * Ds);
            float  PRad   = length(P);
            float3 PUp    = P / max(PRad, 1.0e-4f);
            float  AltKm  = max(0.0f, PRad - BottomR);

            float3 SigRay, SigT;
            float  SigMie;
            EvaluateMediumCoefficientsGpu(Ensemble, RBase, AltKm, SigRay, SigMie, SigT);
            float3 SigScat = SigRay + float3(SigMie);
            float3 StepTr  = exp(-SigT * Ds);
            float3 ScatInt = SigScat * (1.0f - StepTr) / max(SigT, float3(1.0e-6f));

            float  GndBlock;
            bool   Blocked = HitsPlanetGround(P, SunDir, BottomR, GndBlock);
            float3 TSun    = Blocked ? float3(0.0f)
                                     : SampleTransmittanceLutArray(SampledTransLut, RealmId, BottomR, TopR, PRad, dot(PUp, SunDir));

            LDir  += TPath * ScatInt * TSun * kInv4Pi;
            FDir  += TPath * ScatInt * kInv4Pi;
            TPath *= StepTr;
        }

        if (HitGnd)
        {
            float3 GPos    = Origin + RayDir * GroundT;
            float3 GUp     = SafeNormalize(GPos);
            float  CosGSun = max(0.0f, dot(GUp, SunDir));
            if (CosGSun > 0.0f)
            {
                float3 TSunG = SampleTransmittanceLutArray(SampledTransLut, RealmId, BottomR, TopR, BottomR + 1.0e-3f, CosGSun);
                LDir += TPath * TSunG * (CosGSun * GroundA / kPi);
            }
        }

        L2ndSum += LDir * SolidWeight;
        FmsSum  += FDir * SolidWeight;
    }

    float3 Psi = L2ndSum / max(1.0f - FmsSum, float3(0.05f));
    OutputLut[uint3(Tid.xy, RealmId)] = float4(max(Psi, float3(0.0f)), 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                      3. SKY-VIEW LUT (192 x 108 per Realm)
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void BakeSkyViewLutMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }
    uint   RealmId  = Push.Counts.z;
    uint   RBase    = RealmSlotBase(Ensemble, RealmId);
    float4 R0       = Ensemble[RBase + 0u];
    float4 R8       = Ensemble[RBase + 8u];
    float  BottomR  = R0.x;
    float  ObsR     = BottomR + max(R0.z, 1.0e-3f);

    uint   SunFirst = uint(R8.x + 0.5f);
    uint   SunCount = uint(R8.y + 0.5f);
    float3 PrimSun  = (SunCount > 0u) ? Ensemble[SunSlotBase(Ensemble, SunFirst)].xyz : float3(0.0f, 1.0f, 0.0f);

    float2 Uv       = (float2(Tid.xy) + 0.5f) / float2(Push.Counts.xy);
    float  Azimuth  = Uv.x * kPi;
    float  SinLimb  = clamp(BottomR / max(ObsR, BottomR + 1.0e-4f), 0.0f, 1.0f);
    float  LimbElev = -asin(SinLimb);

    float ViewElev = 0.0f;
    if (Uv.y < 0.5f)
    {
        float K  = 1.0f - 2.0f * Uv.y;
        ViewElev = LimbElev - K * K * (LimbElev + 0.5f * kPi);
    }
    else
    {
        float K  = 2.0f * Uv.y - 1.0f;
        ViewElev = LimbElev + K * K * (0.5f * kPi - LimbElev);
    }

    // Align azimuth 0 with primary sun horizontal projection
    float2 SunHoriz = float2(PrimSun.x, PrimSun.z);
    float  Ls       = length(SunHoriz);
    float2 BasisF   = (Ls > 1.0e-5f) ? (SunHoriz / Ls) : float2(0.0f, 1.0f);
    float2 BasisR   = float2(BasisF.y, -BasisF.x);

    float  CosEl    = cos(ViewElev);
    float2 HorizDir = BasisF * cos(Azimuth) + BasisR * sin(Azimuth);
    float3 ViewDir  = SafeNormalize(float3(HorizDir.x * CosEl, sin(ViewElev), HorizDir.y * CosEl));

    float3 SkyRgb = EvaluateCoupledSolarSky6Pt(Ensemble, SampledTransLut, SampledMultiLut, RealmId, ViewDir, 0.0f);
    OutputLut[uint3(Tid.xy, RealmId)] = float4(SkyRgb, 1.0f);
}
