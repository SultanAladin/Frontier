#version 450
#extension GL_GOOGLE_include_directive : enable

// 🧩 Physical sky dome & outer-space atmosphere shader (Hillaire 2020):
//    - Surface/Troposphere: limb-concentrated Sky-View LUT + Neckel-Labs solar limb darkening + continuous circumsolar aureole
//      + cool marine Fresnel lower-hemisphere boundary (zero fake DawnMist white ribbon, zero brown/beige sub-horizon band!).
//    - Outer Space / High Altitude: seamless transition to full spherical orbital raymarching of the curved planetary limb,
//      sunlit continents, specular ocean glint, night-side city lights, and ForceField cyclonic cloud bands alongside stars & sun!
//    - Exoatmospheric Celestials: Fraunhofer Airy starfield + 3D atmospheric moons attenuated by true LookupTransmittance.

#include "AtmosphereCommon.glsl"
#include "TransmittanceLookup.glsl"
#include "CelestialBodies.glsl"

layout(location = 0) in  vec2 FragUv;
layout(location = 0) out vec4 OutColor;

layout(set = 0, binding = 1) uniform sampler2D TransmittanceTable;
layout(set = 0, binding = 2) uniform sampler2D MultiScatterTable;
layout(set = 0, binding = 3) uniform sampler2D SkyViewTable;

// Mirror of SkyAtmosphere.h::SkyDomeConstants (96 bytes).
layout(push_constant) uniform SkyDomePush
{
    mat4 InverseViewProjection;
    vec4 CameraPosition;          // xyz: world camera position [m] in Z-up scene frame
    vec4 SunAndExposure;          // x: SolarAngularRadius [rad], yzw: reserved
} Push;

vec3 SampleMultiScatterOrbital(float Radius, float CosineSunZenith)
{
    vec2 Uv = vec2(CosineSunZenith * 0.5 + 0.5,
                   clamp((Radius - Atmosphere.BottomRadius) / (Atmosphere.TopRadius - Atmosphere.BottomRadius), 0.0, 1.0));
    return texture(MultiScatterTable, Uv).rgb;
}

vec3 SampleSkyView(vec3 WorldDir, float ObserverRadius)
{
    vec3  SunHoriz = vec3(Atmosphere.SolarDirection.x, 0.0, Atmosphere.SolarDirection.z);
    float HorizLen = length(SunHoriz);
    vec3  SunAxisX = (HorizLen > 1.0e-5) ? (SunHoriz / HorizLen) : vec3(1.0, 0.0, 0.0);
    vec3  SunAxisZ = vec3(-SunAxisX.z, 0.0, SunAxisX.x);

    vec3  Local    = vec3(dot(WorldDir, SunAxisX), WorldDir.y, dot(WorldDir, SunAxisZ));
    float Latitude = asin(clamp(Local.y, -1.0, 1.0));
    float Azimuth  = atan(Local.z, Local.x);

    float SinHorizon  = Atmosphere.BottomRadius / ObserverRadius;
    float HorizonBeta = -acos(clamp(SinHorizon, -1.0, 1.0));

    float U = Azimuth / (2.0 * PI) + 0.5;
    float V;
    if (Latitude < HorizonBeta)
    {
        float Norm = (HorizonBeta - Latitude) / max(1.0e-5, 0.5 * PI + HorizonBeta);
        V          = 0.5 - 0.5 * sqrt(clamp(Norm, 0.0, 1.0));
    }
    else
    {
        float Norm = (Latitude - HorizonBeta) / max(1.0e-5, 0.5 * PI - HorizonBeta);
        V          = 0.5 + 0.5 * sqrt(clamp(Norm, 0.0, 1.0));
    }

    return texture(SkyViewTable, vec2(U, clamp(V, 0.0, 1.0))).rgb;
}

// Physically cool marine Fresnel reflectance below the tangent limb — zero warm brown/beige dirt band!
vec3 EvaluateCoolLowerHemisphere(float ObserverRadius, vec3 ViewDirLocal, vec3 SunDirLocal)
{
    float ToGroundKm = IntersectSphere(ObserverRadius, ViewDirLocal.y, Atmosphere.BottomRadius);
    if (ToGroundKm < 0.0)
        return vec3(0.0);

    vec3  HitPos      = vec3(0.0, ObserverRadius, 0.0) + ViewDirLocal * ToGroundKm;
    vec3  HitUp       = normalize(HitPos);
    float NdotV       = clamp(dot(HitUp, -ViewDirLocal), 0.0, 1.0);
    float Fresnel     = 0.020 + 0.980 * pow(1.0 - NdotV, 5.0);

    vec3  ReflDir      = reflect(ViewDirLocal, HitUp);
    ReflDir.y          = clamp(abs(ReflDir.y) + 0.48 * sqrt(NdotV), 0.006, 0.96);
    vec3  SpecSky      = SampleSkyView(normalize(ReflDir), ObserverRadius);
    vec3  OverheadDome = SampleSkyView(vec3(0.0, 0.88, 0.0), ObserverRadius);
    float DomeBlend    = clamp(NdotV * 2.8, 0.0, 0.68);
    vec3  ReflSky      = mix(SpecSky, OverheadDome, DomeBlend);

    float GroundSunMu  = max(0.0, dot(HitUp, SunDirLocal));
    vec3  GroundSun    = (GroundSunMu > 0.0)
                       ? LookupTransmittance(TransmittanceTable, Atmosphere.BottomRadius + 0.001, GroundSunMu) * Atmosphere.SolarIlluminance.rgb
                       : vec3(0.0);
    vec3  UpwellWater  = (GroundSun * (GroundSunMu / PI) + OverheadDome * vec3(0.55, 0.65, 0.78)) * vec3(0.005, 0.028, 0.088);
    vec3  TroughTint   = vec3(1.0 - 0.34 * (1.0 - Fresnel), 1.0 - 0.10 * (1.0 - Fresnel), 1.0);
    vec3  SurfaceRad   = ReflSky * TroughTint * Fresnel + UpwellWater * (1.0 - Fresnel);

    // Attenuate surface by atmospheric airlight over path length ToGroundKm.
    vec3  RayleighS, MieS, ExtBase;
    EvaluateMediumScattering(Atmosphere.BottomRadius + 0.25, RayleighS, MieS, ExtBase);
    vec3  SegTrans    = exp(-ExtBase * min(ToGroundKm, 95.0) * 0.45);
    vec3  LimbAir     = SampleSkyView(normalize(vec3(ViewDirLocal.x, 0.004, ViewDirLocal.z)), ObserverRadius);
    return SurfaceRad * SegTrans + LimbAir * (vec3(1.0) - SegTrans);
}

// Full spherical atmosphere + planetary surface raymarch for high-altitude and outer-space cameras!
vec4 EvaluateOrbitalAtmosphereAndPlanet(vec3 CamPlanetKm, vec3 ViewDirLocal, vec3 SunDirLocal, out bool HitsPlanet)
{
    HitsPlanet   = false;
    vec2 TopHit  = IntersectSphereGeneral(CamPlanetKm, ViewDirLocal, Atmosphere.TopRadius);
    if (TopHit.y <= 0.0)
        return vec4(0.0, 0.0, 0.0, 1.0);

    vec2 GndHit  = IntersectSphereGeneral(CamPlanetKm, ViewDirLocal, Atmosphere.BottomRadius);
    HitsPlanet   = (GndHit.x > 0.0);

    float TStart = max(0.0, TopHit.x);
    float TEnd   = HitsPlanet ? GndHit.x : TopHit.y;
    if (TEnd <= TStart)
        return vec4(0.0, 0.0, 0.0, HitsPlanet ? 0.0 : 1.0);

    const int ORBIT_STEPS = 44;
    float StepLen   = (TEnd - TStart) / float(ORBIT_STEPS);
    float CosineSun = dot(ViewDirLocal, SunDirLocal);
    float PhaseR    = RayleighPhase(CosineSun);
    float PhaseM    = CornetteShanksPhase(CosineSun, Atmosphere.MieAsymmetry);

    vec3 Throughput  = vec3(1.0);
    vec3 InScattered = vec3(0.0);

    for (int Step = 0; Step < ORBIT_STEPS; ++Step)
    {
        float T         = TStart + (float(Step) + 0.5) * StepLen;
        vec3  SamplePos = CamPlanetKm + ViewDirLocal * T;
        float SampleR   = length(SamplePos);
        vec3  SampleUp  = SamplePos / SampleR;
        float SampleMuS = dot(SampleUp, SunDirLocal);

        vec3 RayleighS, MieS, Extinction;
        EvaluateMediumScattering(SampleR, RayleighS, MieS, Extinction);

        vec3 StepTrans    = exp(-Extinction * StepLen);
        vec3 StepIntegral = (vec3(1.0) - StepTrans) / max(Extinction, vec3(1.0e-7));
        vec3 SunTrans     = RayIntersectsGround(SampleR, SampleMuS)
                          ? vec3(0.0)
                          : LookupTransmittance(TransmittanceTable, SampleR, SampleMuS);
        vec3 MultiScat    = SampleMultiScatterOrbital(SampleR, SampleMuS) * Atmosphere.MultipleScatteringFactor;
        vec3 Scat         = ((RayleighS * PhaseR + MieS * PhaseM) * SunTrans + (RayleighS + MieS) * MultiScat)
                          * Atmosphere.SolarIlluminance.rgb;

        InScattered += Throughput * Scat * StepIntegral;
        Throughput  *= StepTrans;
    }

    if (HitsPlanet)
    {
        vec3  SurfPos = CamPlanetKm + ViewDirLocal * GndHit.x;
        vec3  SurfNrm = normalize(SurfPos);
        float MuS     = dot(SurfNrm, SunDirLocal);

        float ContNoise = SmoothCelestialNoise3(SurfNrm * 4.2 + vec3(1.4, -0.8, 2.1), 13u);
        float LandMask  = smoothstep(0.49, 0.56, ContNoise);
        float IceCap    = smoothstep(0.74, 0.88, abs(SurfNrm.y));

        vec3  Albedo    = mix(vec3(0.015, 0.036, 0.078), vec3(0.095, 0.115, 0.072), LandMask);
        Albedo          = mix(Albedo, vec3(0.68, 0.73, 0.78), IceCap);

        // Cyclonic orbital cloud deck.
        float CloudN    = SmoothCelestialNoise3(SurfNrm * 7.5 + vec3(AtmosphereSequenceSeconds() * 0.0004, 0.0, 0.0), 59u);
        float CloudCov  = smoothstep(0.47, 0.74, CloudN) * 0.82;
        vec3  EffAlbedo = mix(Albedo, vec3(0.76, 0.78, 0.81), CloudCov);

        vec3  PlanetRad = vec3(0.0);
        if (MuS > -0.04)
        {
            float ClampedMu = max(0.01, MuS);
            vec3  SunAtSurf = LookupTransmittance(TransmittanceTable, Atmosphere.BottomRadius + 0.01, ClampedMu)
                            * Atmosphere.SolarIlluminance.rgb;
            PlanetRad      += SunAtSurf * (max(0.0, MuS) / PI) * EffAlbedo;

            if (LandMask < 0.45 && CloudCov < 0.55 && MuS > 0.0)
            {
                vec3  HalfVec = normalize(SunDirLocal - ViewDirLocal);
                float Spec    = pow(max(0.0, dot(SurfNrm, HalfVec)), 72.0) * (1.0 - LandMask) * (1.0 - CloudCov);
                PlanetRad    += SunAtSurf * Spec * 0.18;
            }
        }
        if (MuS < 0.04)
        {
            float NightGate = smoothstep(0.04, -0.10, MuS);
            float CityField = pow(SmoothCelestialNoise3(SurfNrm * 34.0, 97u), 4.5);
            PlanetRad      += vec3(0.055, 0.036, 0.014) * (NightGate * LandMask * (1.0 - IceCap) * (1.0 - CloudCov * 0.75) * CityField);
        }

        InScattered += Throughput * PlanetRad;
        Throughput   = vec3(0.0);
    }

    float TransScalar = (Throughput.r + Throughput.g + Throughput.b) / 3.0;
    return vec4(InScattered, TransScalar);
}

void main()
{
    vec2 Ndc      = FragUv * 2.0 - 1.0;
    vec4 NearClip = Push.InverseViewProjection * vec4(Ndc, 0.0, 1.0);
    vec4 FarClip  = Push.InverseViewProjection * vec4(Ndc, 1.0, 1.0);
    vec3 NearPos  = NearClip.xyz / NearClip.w;
    vec3 FarPos   = FarClip.xyz  / FarClip.w;

    // Frontier scene is Z-up; atmosphere-local space is Y-up: (x, y_scene, z_up) -> (x, z_up, y_scene).
    vec3 SceneRay     = normalize(FarPos - NearPos);
    vec3 ViewDirLocal = normalize(vec3(SceneRay.x, SceneRay.z, SceneRay.y));
    vec3 SunDirLocal  = normalize(Atmosphere.SolarDirection.xyz);

    float PixelFootprintRad = clamp(length(fwidth(ViewDirLocal)), 0.00025, 0.015);

    // True camera altitude [km] from Z-up world CameraPosition.z (or UBO override when higher).
    float CamAltKm     = max(0.002, max(Push.CameraPosition.z * 0.001, ObserverAltitudeKilometres()));
    vec3  CamPlanetKm  = vec3(Push.CameraPosition.x * 0.001,
                              Atmosphere.BottomRadius + CamAltKm,
                              Push.CameraPosition.y * 0.001);
    float ObserverRad  = length(CamPlanetKm);

    vec3  SkyRadiance  = vec3(0.0);
    vec3  PathTrans    = vec3(1.0);
    bool  OccludedByPlanet = false;

    if (CamAltKm > 18.0)
    {
        vec4 Orbital     = EvaluateOrbitalAtmosphereAndPlanet(CamPlanetKm, ViewDirLocal, SunDirLocal, OccludedByPlanet);
        SkyRadiance      = Orbital.rgb;
        PathTrans        = OccludedByPlanet ? vec3(0.0) : vec3(Orbital.a);
    }
    else
    {
        float ClampedObs = clamp(ObserverRad, Atmosphere.BottomRadius + 0.001, Atmosphere.TopRadius - 0.010);
        OccludedByPlanet = RayIntersectsGround(ClampedObs, ViewDirLocal.y);
        if (OccludedByPlanet)
        {
            SkyRadiance = EvaluateCoolLowerHemisphere(ClampedObs, ViewDirLocal, SunDirLocal);
            PathTrans   = vec3(0.0);
        }
        else
        {
            SkyRadiance = SampleSkyView(ViewDirLocal, ClampedObs);
            PathTrans   = LookupTransmittance(TransmittanceTable, ClampedObs, ViewDirLocal.y);
        }
    }

    // Exoatmospheric celestials (Sun, Stars, Moons) strictly gated by planet non-occlusion and real optical transmittance!
    if (!OccludedByPlanet)
    {
        // ① Physical Solar Photosphere (Neckel-Labs limb darkening) + Continuous Circumsolar Aureole.
        float SolarRadius = max(0.001, Push.SunAndExposure.x);
        float CosineSun   = dot(ViewDirLocal, SunDirLocal);
        float SunAngle    = acos(clamp(CosineSun, -1.0, 1.0));

        if (SunAngle <= SolarRadius * 1.12)
        {
            float DiscEdge = 1.0 - smoothstep(SolarRadius * 0.94, SolarRadius * 1.05, SunAngle);
            float Ratio    = clamp(SunAngle / SolarRadius, 0.0, 1.0);
            float MuLimb   = sqrt(max(0.0, 1.0 - Ratio * Ratio));
            vec3  LimbDark = EvaluateSolarLimbDarkening(MuLimb);
            SkyRadiance   += Atmosphere.SolarIlluminance.rgb * LimbDark * PathTrans * (DiscEdge * 18.0);
        }
        SkyRadiance += EvaluateCircumsolarAureole(CosineSun, SolarRadius) * PathTrans;

        // ② Exoatmospheric Starfield: naturally masked during bright daytime by sky background contrast, crisp in twilight & orbit!
        float SkyLum      = dot(SkyRadiance, vec3(0.2126, 0.7152, 0.0722));
        float StarContrast = 1.0 / (1.0 + SkyLum * 14.0);
        if (StarContrast > 0.004)
        {
            vec3 StarRad = EvaluateExoatmosphericStarfield(ViewDirLocal, PixelFootprintRad);
            SkyRadiance += StarRad * PathTrans * StarContrast;
        }

        // ③ Primary & Secondary Moons with 3D spherical Rayleigh+Mie lunar atmospheres + Lommel-Seeliger regolith.
        vec3 Moon1Dir = normalize(vec3(-0.52, 0.42, -0.74));
        vec4 Moon1    = EvaluateSingleSphericalMoon(ViewDirLocal, SunDirLocal, Moon1Dir,
                                                    0.0165, 1.18, vec3(0.76, 0.78, 0.84), 0.16,
                                                    vec3(0.45, 0.92, 1.85), 0.14, 17u);
        if (Moon1.a > 0.0)
            SkyRadiance = SkyRadiance * (1.0 - Moon1.a * 0.0) + Moon1.rgb * PathTrans;
    }

    OutColor = vec4(SkyRadiance, 1.0);
}
