#version 450 core

// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//                                                     SKY DOME (per-frame) 🧩
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// The per-frame sky background. Reconstructs the world view ray from the camera's inverse view-projection (the same InverseViewProjection the
// ground grid derives from), looks up pre-baked sky-view radiance for that direction, draws the analytic sun disc through the transmittance LUT,
// then exposure-tonemaps to display. Records first in the frame so the grid and (later) geometry draw over it. Fills the whole framebuffer.

#include "AtmosphereCommon.glsl"
#include "TransmittanceLookup.glsl"

layout(binding = 4) uniform sampler2D SkyViewLUT;   // 256×128 RGBA16F baked radiance (U azimuth REPEAT)

layout(push_constant) uniform SkyPush
{
    mat4  InverseViewProjection;   // clip → world
    vec4  CameraPosition;          // world eye xyz; w unused
    float SunAngularRadius;        // [rad] half-angle of the solar disc
    float SunGlareStrength;        // [-]   radiant-sun glare veil: 0 muted sun .. 1 radiant sun (not an exposure — see the 🔴 note below)
    float SunIntensity;            // [-]   brightness of the disc itself
    float DomeEnabled;             // 1 draw sky, 0 leave cleared
} Push;

layout(location = 0) in  vec2 InUv;
layout(location = 0) out vec4 OutColour;

// 📝 Direction → limb-centred sky-view uv (mirror of AtmosphericScatteringSolver.cpp::SampleSkyViewRadiance and SkyView.frag's rows):
//    the upper bank [0.5, 1] spans zenith → the viewer's geometric limb, the lower bank [0, 0.5) spans limb → nadir; each bank is sampled
//    strictly inside its own texel range so bilinear filtering never blurs sky across the planet limb.
const float SkyViewHalfRows = 64.0;   // Atmosphere::SkyViewExtentY / 2

float MapUnitToTexelSafe(float UnitCoord, float TexelCount)
{
    return 0.5 / TexelCount + clamp(UnitCoord, 0.0, 1.0) * (1.0 - 1.0 / TexelCount);
}

float ResolveViewerRadiusKm()
{
    float Stored    = Atmosphere.OzoneAbsorptionPeak.w;
    float Altitude  = (Stored > 0.0) ? Stored : 0.200;
    float Thickness = max(1.0, Atmosphere.TopRadius - Atmosphere.BottomRadius);
    return Atmosphere.BottomRadius + clamp(Altitude, 0.005, Thickness - 0.05);
}

vec2 DirectionToSkyViewUv(vec3 Direction, float ViewerRadiusKm)
{
    float LimbAngle  = PI - asin(clamp(Atmosphere.BottomRadius / max(ViewerRadiusKm, Atmosphere.BottomRadius + 1.0e-4), 0.0, 1.0));
    float PolarAngle = acos(clamp(Direction.y, -1.0, 1.0));
    float U          = atan(Direction.z, Direction.x) / (2.0 * PI) + 0.5;
    if (PolarAngle <= LimbAngle)
    {
        float QuadraticParameter = sqrt(max(0.0, 1.0 - PolarAngle / max(1.0e-4, LimbAngle)));
        return vec2(U, 0.5 + 0.5 * MapUnitToTexelSafe(QuadraticParameter, SkyViewHalfRows));
    }
    float QuadraticParameter = sqrt(max(0.0, (PolarAngle - LimbAngle) / max(1.0e-4, PI - LimbAngle)));
    return vec2(U, 0.5 * MapUnitToTexelSafe(1.0 - QuadraticParameter, SkyViewHalfRows));
}

// 📝 Abramowitz-Stegun 7.1.26 complementary error function (|ε| < 1.5e-7), for the seeing-limited photospheric limb.
float EvaluateComplementaryError(float Parameter)
{
    float Magnitude = abs(Parameter);
    float Ratio     = 1.0 / (1.0 + 0.3275911 * Magnitude);
    float Series    = Ratio * (0.254829592 + Ratio * (-0.284496736 + Ratio * (1.421413741 + Ratio * (-1.453152027 + Ratio * 1.061405429))));
    float Tail      = Series * exp(-Magnitude * Magnitude);
    return (Parameter >= 0.0) ? Tail : 2.0 - Tail;
}

// 📝 Neckel & Labs (1994) photospheric limb darkening at 650 / 550 / 450 nm (mirror of EvaluateNeckelLabsLimbDarkening).
vec3 EvaluateNeckelLabsLimbDarkening(float MuPhotosphere)
{
    float Mu  = clamp(MuPhotosphere, 0.0, 1.0);
    vec3  Ld  = vec3(0.34685, 0.23698, 0.11903)
              + vec3(1.37539, 1.37916, 1.24089) * Mu
              + vec3(-2.04425, -1.83226, -1.19977) * (Mu * Mu)
              + vec3(2.70493, 2.34540, 1.33936) * (Mu * Mu * Mu)
              + vec3(-1.94290, -1.65319, -0.98690) * (Mu * Mu * Mu * Mu)
              + vec3(0.55999, 0.52391, 0.48739) * (Mu * Mu * Mu * Mu * Mu);
    return clamp(Ld, 0.0, 1.05);
}

// 📝 Solar disc + K/F corona + aerosol aureole through the per-ray line transmittance (mirror of EvaluateSolarRadiometricDisc).
vec3 EvaluateSolarDisc(vec3 Direction, vec3 SunDir, float ViewerRadiusKm, float AngularRadius, float DiscIntensity)
{
    if (DiscIntensity <= 0.0)
        return vec3(0.0);
    float HalfRadians = max(0.0010, AngularRadius);
    float Separation  = 2.0 * asin(clamp(0.5 * length(Direction - SunDir), 0.0, 1.0));   // stable small-angle chord identity
    if (Separation > HalfRadians * 48.0)
        return vec3(0.0);

    float GroundDiscriminant = ViewerRadiusKm * ViewerRadiusKm * (Direction.y * Direction.y - 1.0) + Atmosphere.BottomRadius * Atmosphere.BottomRadius;
    if (Direction.y < 0.0 && GroundDiscriminant >= 0.0)
        return vec3(0.0);
    vec3 LineTransmittance = SampleTransmittanceToSpace(ViewerRadiusKm, Direction.y);
    if (LineTransmittance.r + LineTransmittance.g + LineTransmittance.b <= 1.0e-6)
        return vec3(0.0);

    float NormalizedRadius = Separation / HalfRadians;
    vec3  Photosphere      = vec3(0.0);
    if (NormalizedRadius < 1.32)
    {
        float ClampedRadius = min(1.0, NormalizedRadius);
        float EdgeErfc      = 0.5 * EvaluateComplementaryError((NormalizedRadius - 0.997) / 0.012);
        Photosphere = EvaluateNeckelLabsLimbDarkening(sqrt(max(0.0, 1.0 - ClampedRadius * ClampedRadius))) * (EdgeErfc * DiscIntensity);
    }
    float InnerTerm     = 1.0 / pow(1.0 + (NormalizedRadius / 1.18) * (NormalizedRadius / 1.18), 1.85);
    float MiddleTerm    = 1.0 / pow(1.0 + (NormalizedRadius / 3.40) * (NormalizedRadius / 3.40), 1.38);
    float OuterTerm     = 1.0 / pow(1.0 + (NormalizedRadius / 11.5) * (NormalizedRadius / 11.5), 1.22);
    float ScatterFactor = clamp(0.55 + 0.85 * (1.0 - LineTransmittance.b), 0.55, 1.35);
    float Corona        = DiscIntensity * (0.042 * InnerTerm + 0.0085 * ScatterFactor * MiddleTerm + 0.0014 * ScatterFactor * OuterTerm);
    float WindowFade    = clamp(1.0 - NormalizedRadius / 48.0, 0.0, 1.0);
    vec3  CoronaColour  = Corona * WindowFade * WindowFade * vec3(1.04, 0.99, 0.91);
    return (Photosphere + CoronaColour) * LineTransmittance * Atmosphere.SolarIlluminance.rgb;
}

// Radiant-sun glare veil — mirrors EvaluateSolarGlareRadiance / Atmosphere::EvaluateSolarGlareSpread (C++). Radiance per unit solar
// illuminance; carries the transmittance toward the sun itself, so it spills across the horizon and vanishes once the sun has set.
const float SolarGlareReachRadians   = 30.0 * 0.01745329251994329577;
const float SolarGlareBloomWeight     = 1.50;
const float SolarGlareAureoleWeight  = 0.25;
const float SolarGlareAureoleDegrees = 6.0;

float EvaluateSolarGlareSpread(float SeparationRadians, float DiscRadiusRadians)
{
    if (SeparationRadians >= SolarGlareReachRadians)
        return 0.0;
    float SeparationDegrees = SeparationRadians * 57.2957795130823208768;
    float DiscDegrees       = DiscRadiusRadians * 57.2957795130823208768;
    float Effective         = sqrt(SeparationDegrees * SeparationDegrees + DiscDegrees * DiscDegrees);
    float Window            = 1.0 - SeparationRadians / SolarGlareReachRadians;
    float Bloom             = SolarGlareBloomWeight / ((Effective + 0.02) * (Effective + 0.02));
    float AureoleRatio      = Effective / SolarGlareAureoleDegrees;
    float Aureole           = SolarGlareAureoleWeight / (1.0 + AureoleRatio * AureoleRatio);
    return (Bloom + Aureole) * Window * Window;
}

vec3 EvaluateSolarGlare(vec3 Direction, vec3 SunDir, float ViewerRadiusKm, float AngularRadius, float GlareStrength)
{
    if (GlareStrength <= 0.0)
        return vec3(0.0);
    float HalfRadians = max(0.0010, AngularRadius);
    float Separation  = 2.0 * asin(clamp(0.5 * length(Direction - SunDir), 0.0, 1.0));
    float Spread      = EvaluateSolarGlareSpread(Separation, HalfRadians);
    if (Spread <= 0.0)
        return vec3(0.0);
    float AxisDiscriminant = ViewerRadiusKm * ViewerRadiusKm * (SunDir.y * SunDir.y - 1.0) + Atmosphere.BottomRadius * Atmosphere.BottomRadius;
    if (SunDir.y < 0.0 && AxisDiscriminant >= 0.0)
        return vec3(0.0);
    vec3 AxisTransmittance = SampleTransmittanceToSpace(ViewerRadiusKm, SunDir.y);
    return GlareStrength * Spread * vec3(1.02, 1.0, 0.96) * AxisTransmittance * Atmosphere.SolarIlluminance.rgb;
}

// 🔴 THERE IS NO TONEMAP IN THIS SHADER, AND NONE MAY BE ADDED. The dome writes LINEAR SKY RADIANCE, unbounded above 1.0, into
//    the linear HDR radiance target; `RadianceResolve.frag` is the single site that applies exposure and the tone curve.
//    The ACES-ish curve that used to live here was removed in the P5.9b completion pass. It was doing two kinds of damage:
//      • DOUBLE MAP — it ran against the radiance target, which the resolve then mapped again, and its curve DIFFERED from the
//        shade pass's Khronos PBR Neutral, so sky and geometry rolled off differently and could never match at the horizon.
//        (This file's own comment claimed that defect was already "fixed in P5.9b" while the curve was still executing.)
//      • HDR CLIPPED AT SOURCE — it ended in clamp(Mapped, 0.0, 1.0), so every sky value above 1.0 was destroyed BEFORE reaching
//        the float target. The sun disc and bright horizon carry radiance far above 1.0; clamping here threw away exactly the
//        headroom the HDR target exists to hold, and no downstream exposure could recover it.
// ⚠️ Exposure is applied at the resolve, not here. The push slot that used to be the inert ExposureUnused hole now carries
//    SunGlareStrength; it scales only the glare veil and must never be used as an exposure multiplier.
// ⚠️ Removing the curve also removed the 8x multiply that used to precede it, which left the sky measurably dark against every
//    earlier image. That calibration was NOT restored here; it belongs in the profile's SolarIlluminance, which scales the baked
//    sky-view LUT and this disc together. Re-adding a multiply in this shader would brighten the disc away from the sky behind it.

void main()
{
    if (Push.DomeEnabled < 0.5)
        discard;

    // Reconstruct the world-space view ray. 🔴 Precision: unproject ONLY the near clip point (its homogeneous w is ~1, so the
    // divide keeps full float precision) and take the direction from the EXACT eye — never subtract two far/near unprojections.
    // The far-clip unproject FarClip.xyz/FarClip.w divides two near-equal large floats (FarClip.w scales with the far plane),
    // so the quotient keeps only a few significant bits and SNAPS between representable values as the camera rotates — a
    // per-frame /\/\ shimmer even though the camera moves perfectly smoothly (proven by the input + camera trace). The grid
    // shared this exact reconstruction and shimmered identically; both are fixed the same way (see GroundGrid.vert).
    vec2 Ndc = InUv * 2.0 - 1.0;
    vec4 NearClip  = Push.InverseViewProjection * vec4(Ndc, 0.0, 1.0);
    vec3 NearWorld = NearClip.xyz / NearClip.w;
    vec3 ViewDir   = normalize(NearWorld - Push.CameraPosition.xyz);

    // World is Z-up; atmosphere-local up is +Y. Remap (x, y, z_world) → (x, z_world, y) so z_up becomes local +Y.
    vec3 SkyDir = normalize(vec3(ViewDir.x, ViewDir.z, ViewDir.y));

    vec3  SunDir         = normalize(Atmosphere.SolarDirection.xyz);
    float ViewerRadiusKm = ResolveViewerRadiusKm();
    vec3  SkyRadiance    = texture(SkyViewLUT, DirectionToSkyViewUv(SkyDir, ViewerRadiusKm)).rgb;

    // Analytic solar disc (limb-darkened photosphere, seeing-limited erfc limb, corona + aureole), clipped by the curved planet limb.
    SkyRadiance += EvaluateSolarDisc(SkyDir, SunDir, ViewerRadiusKm, Push.SunAngularRadius, Push.SunIntensity);
    SkyRadiance += EvaluateSolarGlare(SkyDir, SunDir, ViewerRadiusKm, Push.SunAngularRadius, Push.SunGlareStrength);

    // Linear sky radiance out, unbounded above 1.0 (the sun disc is far brighter than 1.0 and must stay that way). The resolve owns
    // exposure and the tone curve. See the 🔴 note where the curve used to live.
    OutColour = vec4(SkyRadiance, 1.0);
}
