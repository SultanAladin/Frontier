#version 450 core

// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//                                                     SKY-VIEW LUT BAKE 🧩
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// Bakes the sky-view radiance table (256×128 RGBA16F), mirroring AtmosphericScatteringSolver.cpp::SynthesizeSkyViewTable so the CPU reference
// harness stays the ground truth for the GPU path:
//   - limb-centred quadratic mapping: rows [64, 128) span zenith → geometric limb θ_limb = π − asin(R_b / r) of the viewer, rows [0, 64) span
//     limb → nadir, so the steep horizon gradient gets the texels and the planet limb never blurs across a row (exact at orbital altitude)
//   - viewer altitude from AtmosphereBlock.OzoneAbsorptionPeak.w (0 → 0.2 km default), clamped inside the atmosphere shell
//   - power-law (exponent 1.25) march with the analytic S·(1 − e^{−σΔs})/σ step integral, Ψ_ms scaled by MultipleScatteringFactor
//   - ~22 V mag/arcsec² natural night background (OI airglow + zodiacal + unresolved stars) with limb brightening, behind the transmittance
//   - lower hemisphere: aerial perspective to the ocean, Lambert water body under sun + sky irradiance, Schlick Fresnel sky reflection read
//     from the same mirrored upper row the CPU uses (integrated inline here), and the GGX wind-ruffled sun glitter path
// Re-baked only when the sun moves. X = azimuth (wraps, sampled with U REPEAT), Y = limb-centred zenith.

#include "AtmosphereCommon.glsl"
#include "TransmittanceLookup.glsl"

layout(binding = 3) uniform sampler2D MultiScatterLUT;   // 32×32 RGBA16F, X = sun-zenith cos, Y = altitude

layout(location = 0) in  vec2 InUv;
layout(location = 0) out vec4 OutRadiance;

const float SkyViewHalfRows = 64.0;   // Atmosphere::SkyViewExtentY / 2

// 📝 Texel-centre sub-coordinate → unit parameter (mirror of MapSubTexelToUnit) so the first / last rows land exactly on 0 / 1.
float MapSubTexelToUnit(float SubCoord, float TexelCount)
{
    return clamp((SubCoord - 0.5 / TexelCount) * (TexelCount / max(1.0, TexelCount - 1.0)), 0.0, 1.0);
}

float ResolveViewerRadiusKm()
{
    float Stored    = Atmosphere.OzoneAbsorptionPeak.w;
    float Altitude  = (Stored > 0.0) ? Stored : 0.200;
    float Thickness = max(1.0, Atmosphere.TopRadius - Atmosphere.BottomRadius);
    return Atmosphere.BottomRadius + clamp(Altitude, 0.005, Thickness - 0.05);
}

float ResolveLimbPolarAngle(float ViewerRadiusKm)
{
    return PI - asin(clamp(Atmosphere.BottomRadius / max(ViewerRadiusKm, Atmosphere.BottomRadius + 1.0e-4), 0.0, 1.0));
}

// 📝 Upper-bank polar angle for half-row index UpperRow (mirror of the CPU first pass, nudged inside the sky cone at the limb row).
float ResolveUpperPolarAngle(float UpperRow, float LimbAngle)
{
    float QuadraticParameter = MapSubTexelToUnit((UpperRow + 0.5) / SkyViewHalfRows, SkyViewHalfRows);
    return max(0.0, LimbAngle * (1.0 - QuadraticParameter * QuadraticParameter) - 1.5e-4);
}

vec3 SampleMultiScatter(float RadiusFromCentre, float SunZenithCos)
{
    float Altitude = RadiusFromCentre - Atmosphere.BottomRadius;
    float U        = 0.5 * (SunZenithCos + 1.0);
    float V        = clamp(Altitude / max(Atmosphere.TopRadius - Atmosphere.BottomRadius, 1e-4), 0.0, 1.0);
    return texture(MultiScatterLUT, vec2(U, V)).rgb;
}

// 📝 Sun transmittance with ground occlusion (the CPU SampleTransmittanceToSpace returns 0 when the sun ray hits the planet).
vec3 SampleSunTransmittance(float RadiusFromCentre, float SunZenithCos)
{
    if (RadiusFromCentre >= Atmosphere.TopRadius)
        return vec3(1.0);
    float Discriminant = RadiusFromCentre * RadiusFromCentre * (SunZenithCos * SunZenithCos - 1.0) + Atmosphere.BottomRadius * Atmosphere.BottomRadius;
    if (SunZenithCos < 0.0 && Discriminant >= 0.0)
        return vec3(0.0);
    return SampleTransmittanceToSpace(RadiusFromCentre, SunZenithCos);
}

// 📝 Single + multiple scattering along [0, PathLengthKm] from (0, StartRadius, 0) (mirror of IntegrateSkyRayRadiometric).
vec3 IntegrateSkyRay(float StartRadius, vec3 ViewDir, vec3 SunDir, float PathLengthKm, int StepCount, out vec3 Throughput)
{
    Throughput = vec3(1.0);
    if (PathLengthKm <= 1.0e-5)
        return vec3(0.0);

    float CosineScatter = dot(ViewDir, SunDir);
    float RayleighPh    = EvaluateRayleighPhase(CosineScatter);
    float MiePh         = EvaluateCornetteShanksPhase(CosineScatter, Atmosphere.MieAsymmetry);

    vec3  Accumulated      = vec3(0.0);
    float PreviousDistance = 0.0;
    for (int Step = 0; Step < StepCount; ++Step)
    {
        float NextDistance = PathLengthKm * pow(float(Step + 1) / float(StepCount), 1.25);
        float DeltaKm      = max(1.0e-5, NextDistance - PreviousDistance);
        float Distance     = PreviousDistance + 0.5 * DeltaKm;
        PreviousDistance   = NextDistance;

        vec3  SamplePos = vec3(0.0, StartRadius, 0.0) + ViewDir * Distance;
        float SampleRad = max(Atmosphere.BottomRadius, length(SamplePos));
        float SampleAlt = SampleRad - Atmosphere.BottomRadius;

        vec3 Scattering;
        vec3 Extinction;
        EvaluateScatterExtinction(SampleAlt, Scattering, Extinction);
        vec3 StepTransmittance = exp(-Extinction * DeltaKm * 1000.0);

        float RayleighDensity = EvaluateProfileDensity(Atmosphere.RayleighProfileLayer0, Atmosphere.RayleighProfileLayer1, SampleAlt);
        float MieDensity      = EvaluateProfileDensity(Atmosphere.MieProfileLayer0,      Atmosphere.MieProfileLayer1,      SampleAlt);
        vec3  RayleighScatter = Atmosphere.RayleighScatteringSeaLevel.rgb * RayleighDensity;
        vec3  MieScatter      = Atmosphere.MieScatteringSeaLevel.rgb      * MieDensity;

        float SampleSunCos = dot(SamplePos / SampleRad, SunDir);
        vec3  SunTransmit  = SampleSunTransmittance(SampleRad, SampleSunCos);
        vec3  MultiScatter = SampleMultiScatter(SampleRad, SampleSunCos) * Atmosphere.MultipleScatteringFactor;

        vec3 InScatter = (RayleighScatter * RayleighPh + MieScatter * MiePh) * SunTransmit + (RayleighScatter + MieScatter) * MultiScatter;
        Accumulated += Throughput * InScatter * (1.0 - StepTransmittance) / max(Extinction, vec3(1.0e-9));
        Throughput  *= StepTransmittance;
    }
    return Accumulated;
}

// 📝 Calibrated upper-hemisphere radiance for one polar angle: scattered sunlight + limb-brightened night background behind the path.
vec3 EvaluateUpperSky(float StartRadius, float PolarAngle, float Azimuth, vec3 SunDir)
{
    // Night background ≈ E_V0 · 10^(−0.4 (22 − (−26.74))) / 2.3504e-11 sr in solar-calibrated units, green-dominant (OI 557.7 nm).
    const float NightBackgroundLuminance = 12.8 * pow(10.0, -0.4 * (22.0 + 26.74)) / 2.3504e-11;   // 12.8 = SolarIlluminanceCalibration
    const vec3  NightBackground          = vec3(0.90, 1.04, 0.88) * NightBackgroundLuminance;

    float CosinePolar   = cos(PolarAngle);
    float SinePolar     = sin(PolarAngle);
    vec3  ViewDir       = vec3(SinePolar * cos(Azimuth), CosinePolar, SinePolar * sin(Azimuth));
    float DistanceToTop = max(0.0, IntersectSphere(StartRadius, CosinePolar, Atmosphere.TopRadius));
    float AirglowLimb   = 1.0 + 0.85 * exp(-max(0.0, CosinePolar) * 5.0);

    vec3 Throughput;
    vec3 Radiance = IntegrateSkyRay(StartRadius, ViewDir, SunDir, DistanceToTop, 48, Throughput) * Atmosphere.SolarIlluminance.rgb;
    return Radiance + NightBackground * Throughput * AirglowLimb;
}

void main()
{
    vec3  SunDir      = normalize(Atmosphere.SolarDirection.xyz);
    vec3  SolarFlux   = Atmosphere.SolarIlluminance.rgb;
    float StartRadius = ResolveViewerRadiusKm();
    float LimbAngle   = ResolveLimbPolarAngle(StartRadius);
    float Azimuth     = (InUv.x * 2.0 - 1.0) * PI;
    float Row         = min(floor(InUv.y * 2.0 * SkyViewHalfRows), 2.0 * SkyViewHalfRows - 1.0);

    if (Row >= SkyViewHalfRows)
    {
        OutRadiance = vec4(max(EvaluateUpperSky(StartRadius, ResolveUpperPolarAngle(Row - SkyViewHalfRows, LimbAngle), Azimuth, SunDir), vec3(0.0)), 1.0);
        return;
    }

    // Lower planetary hemisphere: aerial perspective to the ocean + Fresnel-blended water body / reflected sky + sun glitter.
    float MirrorOffset       = (SkyViewHalfRows - 1.0) - Row;
    float QuadraticParameter = MapSubTexelToUnit((MirrorOffset + 0.5) / SkyViewHalfRows, SkyViewHalfRows);
    float PolarAngle         = min(PI, LimbAngle + (PI - LimbAngle) * QuadraticParameter * QuadraticParameter + 2.0e-4);
    float CosinePolar        = cos(PolarAngle);
    float SinePolar          = sin(PolarAngle);
    float DistanceGround     = max(0.005, IntersectSphere(StartRadius, CosinePolar, Atmosphere.BottomRadius));
    vec3  ViewDir            = vec3(SinePolar * cos(Azimuth), CosinePolar, SinePolar * sin(Azimuth));

    vec3 Throughput;
    vec3 AerialRadiance = IntegrateSkyRay(StartRadius, ViewDir, SunDir, DistanceGround, 32, Throughput) * SolarFlux;

    vec3  SurfaceNormal    = normalize(vec3(0.0, StartRadius, 0.0) + ViewDir * DistanceGround);
    float SurfaceSunCosine = dot(SurfaceNormal, SunDir);
    float ClampedSunCosine = max(0.0, SurfaceSunCosine);
    float NormalViewCosine = clamp(-dot(ViewDir, SurfaceNormal), 0.0, 1.0);

    vec3 SunAtSurface    = (ClampedSunCosine > 0.0) ? SampleSunTransmittance(Atmosphere.BottomRadius + 0.005, ClampedSunCosine) : vec3(0.0);
    vec3 SkyAtSurface    = SampleMultiScatter(Atmosphere.BottomRadius + 0.005, SurfaceSunCosine) * (PI * Atmosphere.MultipleScatteringFactor);
    vec3 GroundAlbedo    = clamp(vec3(Atmosphere.RayleighScatteringSeaLevel.w, Atmosphere.MieScatteringSeaLevel.w, Atmosphere.MieExtinctionSeaLevel.w), 0.0, 1.0);
    vec3 TotalIrradiance = (SunAtSurface * ClampedSunCosine + SkyAtSurface) * SolarFlux;
    vec3 WaterBody       = GroundAlbedo * TotalIrradiance / PI;

    float FresnelComplement = 1.0 - NormalViewCosine;
    float FresnelSquared    = FresnelComplement * FresnelComplement;
    float FresnelFactor     = 0.02 + 0.98 * (FresnelSquared * FresnelSquared * FresnelComplement);

    // Same mirrored upper row the CPU bake reads (ReflectedRow = HalfHeight + min(HalfHeight − 1, ⌊0.85 · MirrorOffset⌋)), integrated inline.
    float ReflectedRow = min(SkyViewHalfRows - 1.0, floor(MirrorOffset * 0.85));
    vec3  ReflectedSky = EvaluateUpperSky(StartRadius, ResolveUpperPolarAngle(ReflectedRow, LimbAngle), Azimuth, SunDir) * vec3(0.78, 0.84, 0.92);

    vec3 SolarGlint = vec3(0.0);
    if (SunDir.y > -0.015 && ClampedSunCosine > 0.0)
    {
        const float RoughnessSquared    = 0.016;   // wind-ruffled sea slope variance σ²
        vec3        HalfVector          = normalize(SunDir - ViewDir);
        float       NormalHalfwayCosine = max(0.0, dot(SurfaceNormal, HalfVector));
        float       Denominator         = NormalHalfwayCosine * NormalHalfwayCosine * (RoughnessSquared - 1.0) + 1.0;
        float       GgxDistance         = RoughnessSquared / max(1.0e-5, PI * Denominator * Denominator);
        float       GlintGain           = min(6.0, GgxDistance * FresnelFactor * 0.16) * clamp((SunDir.y + 0.015) * 18.0, 0.0, 1.0);
        SolarGlint = SunAtSurface * SolarFlux * GlintGain;
    }

    vec3 SurfaceOut = WaterBody * (1.0 - FresnelFactor) + ReflectedSky * FresnelFactor + SolarGlint;
    OutRadiance = vec4(max(AerialRadiance + Throughput * SurfaceOut, vec3(0.0)), 1.0);
}
