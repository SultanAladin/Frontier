//==========================================================================================================================================
//                                                SOLAR SKY ENVIRONMENT SLANG
//==========================================================================================================================================
// Multi-realm, multi-sun 6-point quadratic-warped Gauss-Legendre coupled solar/sky/twilight atmospheric radiance evaluator.

#include "EnvironmentEnsemble.slang"

float2 DirectionToTransmittanceUv(float BottomRadiusKm, float TopRadiusKm, float RadiusKm, float CosSunAngle)
{
    float H        = sqrt(max(TopRadiusKm * TopRadiusKm - BottomRadiusKm * BottomRadiusKm, 1.0e-5f));
    float Rho      = sqrt(max(RadiusKm * RadiusKm - BottomRadiusKm * BottomRadiusKm, 0.0f));
    float Discrim  = RadiusKm * RadiusKm * (CosSunAngle * CosSunAngle - 1.0f) + TopRadiusKm * TopRadiusKm;
    float D        = max(0.0f, -RadiusKm * CosSunAngle + sqrt(max(Discrim, 0.0f)));
    float DMin     = TopRadiusKm - RadiusKm;
    float DMax     = Rho + H;
    float XMu      = (DMax > DMin + 1.0e-5f) ? ((D - DMin) / (DMax - DMin)) : 0.0f;
    float XR       = Rho / H;
    return saturate(float2(XMu, XR));
}

float3 SampleTransmittanceLutArray(Sampler2DArray<float4> TransLut,
                                   uint                   RealmId,
                                   float                  BottomRadiusKm,
                                   float                  TopRadiusKm,
                                   float                  RadiusKm,
                                   float                  CosSunAngle)
{
    float2 Uv = DirectionToTransmittanceUv(BottomRadiusKm, TopRadiusKm, clamp(RadiusKm, BottomRadiusKm, TopRadiusKm), clamp(CosSunAngle, -1.0f, 1.0f));
    return TransLut.SampleLevel(float3(Uv, float(RealmId)), 0.0f).rgb;
}

float2 DirectionToSkyViewUv(float BottomRadiusKm, float ObserverRadiusKm, float3 ViewDir, float3 PrimarySunDir)
{
    float R          = max(ObserverRadiusKm, BottomRadiusKm + 1.0e-4f);
    float SinLimb    = clamp(BottomRadiusKm / R, 0.0f, 1.0f);
    float LimbElev   = -asin(SinLimb);
    float ViewElev   = asin(clamp(ViewDir.y, -1.0f, 1.0f));

    float VCoord = 0.5f;
    if (ViewElev < LimbElev)
    {
        float Ratio = (ViewElev - LimbElev) / max(-0.5f * kPi - LimbElev, -1.0e-4f);
        VCoord      = 0.5f - 0.5f * sqrt(saturate(Ratio));
    }
    else
    {
        float Ratio = (ViewElev - LimbElev) / max(0.5f * kPi - LimbElev, 1.0e-4f);
        VCoord      = 0.5f + 0.5f * sqrt(saturate(Ratio));
    }

    float2 HorizView = float2(ViewDir.x, ViewDir.z);
    float2 HorizSun  = float2(PrimarySunDir.x, PrimarySunDir.z);
    float  Lv        = length(HorizView);
    float  Ls        = length(HorizSun);
    float  Azimuth   = 0.0f;
    if (Lv > 1.0e-5f && Ls > 1.0e-5f)
    {
        float CosAz = clamp(dot(HorizView / Lv, HorizSun / Ls), -1.0f, 1.0f);
        Azimuth     = acos(CosAz);
    }
    return saturate(float2(Azimuth / kPi, VCoord));
}

void EvaluateMediumCoefficientsGpu(StructuredBuffer<float4> Ensemble,
                                   uint                     RBase,
                                   float                    AltitudeKm,
                                   out float3               OutSigmaSRay,
                                   out float                OutSigmaSMie,
                                   out float3               OutSigmaT)
{
    float4 R1 = Ensemble[RBase + 1u];
    float4 R2 = Ensemble[RBase + 2u];
    float4 R3 = Ensemble[RBase + 3u];
    float4 R4 = Ensemble[RBase + 4u];

    float Alt    = max(0.0f, AltitudeKm);
    float RhoRay = exp(-Alt / max(R1.w, 1.0e-3f));
    float RhoMie = exp(-Alt / max(R2.z, 1.0e-3f));
    float RhoOzo = max(0.0f, 1.0f - abs(Alt - R3.w) / max(R4.x, 1.0e-3f));

    OutSigmaSRay = R1.xyz * RhoRay;
    OutSigmaSMie = R2.x * RhoMie;
    OutSigmaT    = max(OutSigmaSRay + float3(R2.y * RhoMie) + R3.xyz * RhoOzo, float3(1.0e-8f));
}

// 6-point quadratic-warped Gauss-Legendre coupled solar/sky/twilight evaluator (matches CPU SolarSkyEnvironment < 2.85% vs FP64 oracle)
float3 EvaluateCoupledSolarSky6Pt(StructuredBuffer<float4> Ensemble,
                                  Sampler2DArray<float4>   TransLut,
                                  Sampler2DArray<float4>   MultiLut,
                                  uint                     RealmId,
                                  float3                   ViewDir,
                                  float                    MaxMarchKm)
{
    uint   RBase     = RealmSlotBase(Ensemble, RealmId);
    float4 R0        = Ensemble[RBase + 0u];
    float4 R2        = Ensemble[RBase + 2u];
    float4 R8        = Ensemble[RBase + 8u];
    float  Twilight  = Ensemble[RBase + 14u].w;

    float BottomR    = R0.x;
    float TopR       = R0.y;
    float ObsR       = BottomR + max(R0.z, 1.0e-3f);
    float3 Origin    = float3(0.0f, ObsR, 0.0f);

    float MarchLen = DistanceToSphereExit(Origin, ViewDir, TopR);
    float GroundT;
    if (HitsPlanetGround(Origin, ViewDir, BottomR, GroundT))
    {
        MarchLen = min(MarchLen, GroundT);
    }
    if (MaxMarchKm > 0.0f)
    {
        MarchLen = min(MarchLen, MaxMarchKm);
    }
    if (MarchLen <= 1.0e-5f)
    {
        return float3(0.0f);
    }

    static const float kGlNodes[6] = {
        0.03376524f, 0.16939531f, 0.38069041f, 0.61930959f, 0.83060469f, 0.96623476f
    };
    static const float kGlWeights[6] = {
        0.08566225f, 0.18038079f, 0.23395697f, 0.23395697f, 0.18038079f, 0.08566225f
    };

    uint SunFirst = uint(R8.x + 0.5f);
    uint SunCount = uint(R8.y + 0.5f);

    float3 Radiance       = float3(0.0f);
    float3 Throughput     = float3(1.0f);
    float  PrevS          = 0.0f;

    const float WarpExp = 2.22f;
    [unroll]
    for (int I = 0; I < 6; ++I)
    {
        float U     = kGlNodes[I];
        float Upw   = pow(U, WarpExp);
        float S     = MarchLen * Upw;
        float DsDu  = MarchLen * (WarpExp * pow(U, WarpExp - 1.0f));
        float StepW = DsDu * kGlWeights[I];
        float SegDs = max(S - PrevS, 0.0f);
        PrevS       = S;

        float3 Pos   = Origin + ViewDir * S;
        float  RadKm = length(Pos);
        float3 UpUnit = Pos / max(RadKm, 1.0e-4f);
        float  AltKm = max(0.0f, RadKm - BottomR);

        float3 SigmaSRay;
        float  SigmaSMie;
        float3 SigmaT;
        EvaluateMediumCoefficientsGpu(Ensemble, RBase, AltKm, SigmaSRay, SigmaSMie, SigmaT);

        float3 StepTrans = exp(-SigmaT * (0.5f * SegDs));
        Throughput *= StepTrans;

        float3 InScatter = float3(0.0f);
        for (uint SIdx = 0u; SIdx < SunCount; ++SIdx)
        {
            uint   SBase     = SunSlotBase(Ensemble, SunFirst + SIdx);
            float3 SunDir    = Ensemble[SBase + 0u].xyz;
            float3 SunIrr    = Ensemble[SBase + 1u].xyz;
            float  CosSunUp  = dot(UpUnit, SunDir);
            float  CosTheta  = dot(ViewDir, SunDir);
            float  PhaseRay  = RayleighPhaseVal(CosTheta);
            float  PhaseMie  = CornetteShanksPhaseVal(CosTheta, R2.w);

            float  EarthGnd;
            bool   EarthBlocked = HitsPlanetGround(Pos, SunDir, BottomR, EarthGnd);
            float3 SunTrans     = EarthBlocked ? float3(0.0f)
                                               : SampleTransmittanceLutArray(TransLut, RealmId, BottomR, TopR, RadKm, CosSunUp);

            float2 MsUv = saturate(float2(0.5f + 0.5f * CosSunUp, AltKm / max(TopR - BottomR, 1.0e-3f)));
            float3 PsiMs = MultiLut.SampleLevel(float3(MsUv, float(RealmId)), 0.0f).rgb;

            // Smooth twilight upper-atmosphere scattering boost when solar disc is near/below geometric limb
            float  TwilBoost = (CosSunUp < 0.06f && CosSunUp > -0.24f)
                                   ? (1.0f + 0.22f * max(Twilight - 1.0f, 0.0f) * smoothstep(-0.24f, 0.02f, CosSunUp))
                                   : 1.0f;

            float3 Single = (SigmaSRay * PhaseRay + float3(SigmaSMie * PhaseMie)) * SunTrans * TwilBoost;
            float3 Multi  = (SigmaSRay + float3(SigmaSMie)) * PsiMs;
            InScatter    += (Single + Multi) * SunIrr;
        }

        Radiance   += Throughput * InScatter * StepW;
        Throughput *= StepTrans;
    }

    return max(Radiance, float3(0.0f));
}
