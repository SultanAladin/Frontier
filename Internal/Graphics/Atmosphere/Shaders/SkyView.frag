#version 450 core

// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//                                                     SKY-VIEW LUT BAKE 🧩
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// Bakes the sky-view radiance table (192×108 RGBA16F): for each view direction over the sphere, raymarches single scattering with a real
// transmittance-LUT lookup toward the sun AND adds the baked multi-scatter Ψ term. This is the pass that replaces the archive's fake
// `exp(-Altitude*0.1)` — it samples the LUTs and marches in correct km-space. Re-baked only when the sun moves. X = azimuth, Y = view zenith.

#include "AtmosphereCommon.glsl"
#include "TransmittanceLookup.glsl"

layout(binding = 3) uniform sampler2D MultiScatterLUT;   // 32×32 RGBA16F, X = sun-zenith cos, Y = altitude

layout(location = 0) in  vec2 InUv;
layout(location = 0) out vec4 OutRadiance;

// 📝 LUT uv → view direction. Y uses a sqrt bias so the horizon (where the gradient is steep) gets more texels.
vec3 SkyViewUvToDirection(vec2 Uv, out float ViewZenithCos)
{
    float Azimuth = (Uv.x * 2.0 - 1.0) * PI;

    // Uv.y in [0,0.5) = below horizon-ish → down hemisphere; [0.5,1] = up. Bias toward horizon at 0.5.
    float V = Uv.y;
    float Zenith;
    if (V < 0.5)
    {
        float T = 1.0 - 2.0 * V;      // 0 at horizon → 1 at nadir
        Zenith = (PI * 0.5) + (T * T) * (PI * 0.5);
    }
    else
    {
        float T = 2.0 * V - 1.0;      // 0 at horizon → 1 at zenith
        Zenith = (PI * 0.5) - (T * T) * (PI * 0.5);
    }
    ViewZenithCos = cos(Zenith);
    float SinZenith = sin(Zenith);
    return vec3(SinZenith * cos(Azimuth), ViewZenithCos, SinZenith * sin(Azimuth));
}

vec3 SampleMultiScatter(float RadiusFromCentre, float SunZenithCos)
{
    float Altitude = RadiusFromCentre - Atmosphere.BottomRadius;
    float U = 0.5 * (SunZenithCos + 1.0);
    float Vv = clamp(Altitude / max(Atmosphere.TopRadius - Atmosphere.BottomRadius, 1e-4), 0.0, 1.0);
    return texture(MultiScatterLUT, vec2(U, Vv)).rgb;
}

void main()
{
    float ViewZenithCos;
    vec3  ViewDir = SkyViewUvToDirection(InUv, ViewZenithCos);
    vec3  SunDir  = normalize(Atmosphere.SolarDirection.xyz);

    // Camera sits just above the ground surface, local up = +Y.
    float StartRadius = Atmosphere.BottomRadius + 0.5;   // 0.5 km observer altitude
    vec3  StartPos    = vec3(0.0, StartRadius, 0.0);

    float PathToTop    = IntersectSphere(StartRadius, ViewZenithCos, Atmosphere.TopRadius);
    float PathToGround = IntersectSphere(StartRadius, ViewZenithCos, Atmosphere.BottomRadius);
    float PathLength   = (PathToGround > 0.0) ? PathToGround : max(PathToTop, 0.0);

    vec3 Radiance   = vec3(0.0);
    vec3 Throughput = vec3(1.0);

    if (PathLength > 0.0)
    {
        const int StepCount = 32;
        float StepSize = PathLength / float(StepCount);

        float CosSunView = dot(ViewDir, SunDir);
        float RayleighPh = EvaluateRayleighPhase(CosSunView);
        float MiePh      = EvaluateCornetteShanksPhase(CosSunView, Atmosphere.MieAsymmetry);

        for (int Step = 0; Step < StepCount; Step++)
        {
            float Distance  = (float(Step) + 0.5) * StepSize;
            vec3  SamplePos = StartPos + ViewDir * Distance;
            float SampleRad = length(SamplePos);
            float SampleAlt = SampleRad - Atmosphere.BottomRadius;

            vec3 Scattering, Extinction;
            EvaluateScatterExtinction(SampleAlt, Scattering, Extinction);
            vec3 StepTransmittance = exp(-Extinction * StepSize * 1000.0);

            // Split scattering into Rayleigh vs Mie for correct per-species phase weighting.
            float RayleighDensity = EvaluateProfileDensity(Atmosphere.RayleighProfileLayer0, Atmosphere.RayleighProfileLayer1, SampleAlt);
            float MieDensity      = EvaluateProfileDensity(Atmosphere.MieProfileLayer0,      Atmosphere.MieProfileLayer1,      SampleAlt);
            vec3  RayleighScatter = Atmosphere.RayleighScatteringBase.rgb * RayleighDensity;
            vec3  MieScatter      = Atmosphere.MieScatteringBase.rgb      * MieDensity;

            float SampleSunCos = dot(normalize(SamplePos), SunDir);
            vec3  SunTransmit  = SampleTransmittanceToSpace(SampleRad, SampleSunCos);
            vec3  MultiScatter = SampleMultiScatter(SampleRad, SampleSunCos);

            // Single scatter (phase-weighted, shadowed by transmittance to sun) + isotropic multi-scatter.
            vec3 InScatter = (RayleighScatter * RayleighPh + MieScatter * MiePh) * SunTransmit
                           + (RayleighScatter + MieScatter) * MultiScatter;

            // Analytic S·T step integral for reduced banding.
            vec3 IntegralS = (InScatter - InScatter * StepTransmittance) / max(Extinction, vec3(1e-6));
            Radiance   += Throughput * IntegralS;
            Throughput *= StepTransmittance;
        }
    }

    Radiance *= Atmosphere.SolarIlluminance.rgb;
    OutRadiance = vec4(Radiance, 1.0);
}
