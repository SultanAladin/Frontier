#version 450
#extension GL_GOOGLE_include_directive : enable

// 🧩 Per-sun-position sky-view LUT (Hillaire 2020 §4.3) evaluated at the true observer altitude (SolarDirection.w [km]) with
//    analytical step integration and limb-concentrated quadratic latitude parameterization.

#include "AtmosphereCommon.glsl"
#include "TransmittanceLookup.glsl"

layout(location = 0) in  vec2 FragUv;
layout(location = 0) out vec4 OutLuminance;

layout(set = 0, binding = 1) uniform sampler2D TransmittanceTable;
layout(set = 0, binding = 2) uniform sampler2D MultiScatterTable;

const int SKY_VIEW_STEPS = 40;

vec3 SampleMultiScatter(float Radius, float CosineSunZenith)
{
    vec2 Uv;
    Uv.x = CosineSunZenith * 0.5 + 0.5;
    Uv.y = clamp((Radius - Atmosphere.BottomRadius)
               / (Atmosphere.TopRadius - Atmosphere.BottomRadius), 0.0, 1.0);
    return texture(MultiScatterTable, Uv).rgb;
}

void main()
{
    float Azimuth = (FragUv.x - 0.5) * 2.0 * PI;

    // Observer altitude tracks the true camera height inside the atmosphere [BottomRadius + 1 m .. TopRadius - 10 m].
    float ObserverRadius = clamp(Atmosphere.BottomRadius + ObserverAltitudeKilometres(),
                                 Atmosphere.BottomRadius + 0.001,
                                 Atmosphere.TopRadius - 0.010);

    float SinHorizon  = Atmosphere.BottomRadius / ObserverRadius;
    float HorizonBeta = -acos(clamp(SinHorizon, -1.0, 1.0));

    float Latitude;
    if (FragUv.y < 0.5)
    {
        float Coord = 1.0 - 2.0 * FragUv.y;
        Latitude    = HorizonBeta - Coord * Coord * (0.5 * PI + HorizonBeta);
    }
    else
    {
        float Coord = 2.0 * FragUv.y - 1.0;
        Latitude    = HorizonBeta + Coord * Coord * (0.5 * PI - HorizonBeta);
    }

    float CosLat   = cos(Latitude);
    vec3  ViewRay  = vec3(CosLat * cos(Azimuth), sin(Latitude), CosLat * sin(Azimuth));

    vec3  SunHoriz = vec3(Atmosphere.SolarDirection.x, 0.0, Atmosphere.SolarDirection.z);
    float HorizLen = length(SunHoriz);
    vec3  SunAxisX = (HorizLen > 1.0e-5) ? (SunHoriz / HorizLen) : vec3(1.0, 0.0, 0.0);
    vec3  SunAxisZ = vec3(-SunAxisX.z, 0.0, SunAxisX.x);
    vec3  WorldDir = SunAxisX * ViewRay.x + vec3(0.0, 1.0, 0.0) * ViewRay.y + SunAxisZ * ViewRay.z;

    float CosineZenith = WorldDir.y;
    float PathLength   = DistanceToNearestBoundary(ObserverRadius, CosineZenith);
    float StepLength   = PathLength / float(SKY_VIEW_STEPS);

    vec3  SunDir       = normalize(Atmosphere.SolarDirection.xyz);
    float CosineSun    = dot(WorldDir, SunDir);
    float RayPhase     = RayleighPhase(CosineSun);
    float MiePhase     = CornetteShanksPhase(CosineSun, Atmosphere.MieAsymmetry);

    vec3 Throughput  = vec3(1.0);
    vec3 InScattered = vec3(0.0);

    for (int Step = 0; Step < SKY_VIEW_STEPS; ++Step)
    {
        float T         = (float(Step) + 0.5) * StepLength;
        vec3  SamplePos = vec3(0.0, ObserverRadius, 0.0) + WorldDir * T;
        float SampleR   = length(SamplePos);
        vec3  SampleUp  = SamplePos / SampleR;
        float SampleMuS = dot(SampleUp, SunDir);

        vec3 RayleighS, MieS, Extinction;
        EvaluateMediumScattering(SampleR, RayleighS, MieS, Extinction);

        vec3 StepTrans    = exp(-Extinction * StepLength);
        vec3 SafeExt      = max(Extinction, vec3(1.0e-7));
        vec3 StepIntegral = (vec3(1.0) - StepTrans) / SafeExt;

        vec3 SunTrans     = RayIntersectsGround(SampleR, SampleMuS)
                          ? vec3(0.0)
                          : LookupTransmittance(TransmittanceTable, SampleR, SampleMuS);

        vec3 MultiScat    = SampleMultiScatter(SampleR, SampleMuS) * Atmosphere.MultipleScatteringFactor;
        vec3 TotalScat    = RayleighS + MieS;
        vec3 Scattering   = (RayleighS * RayPhase + MieS * MiePhase) * SunTrans + TotalScat * MultiScat;

        InScattered += Throughput * Scattering * StepIntegral;
        Throughput  *= StepTrans;
    }

    OutLuminance = vec4(InScattered * Atmosphere.SolarIlluminance.rgb, 1.0);
}
