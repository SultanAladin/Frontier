// 🧩 Shared physical functions and the std140 AtmosphereBlock uniform mirrored against AtmosphereProfile.h::AtmosphereUniformBlock.
//    Included by Transmittance.comp, MultiScatter.comp, SkyView.frag, SkyDome.frag, and VolumetricCloudMarch.comp via GL_GOOGLE_include_directive.
//    Every distance is in kilometres inside these functions; scattering bases arrive in 1/m from the profile and scale by 1000 at the sample site.

#ifndef FRONTIER_ATMOSPHERE_COMMON_GLSL
#define FRONTIER_ATMOSPHERE_COMMON_GLSL

const float PI = 3.14159265358979323846;

//------------------------------------------------------------------------------------------------------------------------
//                                  ATMOSPHERE UNIFORM BLOCK (std140, 208 bytes)
//------------------------------------------------------------------------------------------------------------------------

layout(std140, set = 0, binding = 0) uniform AtmosphereBlock
{
    vec4  RayleighScatteringBase;   // [1/m] rgb; a: pad
    vec4  MieScatteringBase;        // [1/m] rgb; a: pad
    vec4  MieExtinctionBase;        // [1/m] rgb; a: pad
    vec4  OzoneAbsorptionBase;      // [1/m] rgb; a: pad

    vec4  RayleighProfileLayer0;    // (Width, ExponentialTerm, ExponentialScale, LinearTerm)
    vec4  RayleighProfileLayer1;
    vec4  MieProfileLayer0;
    vec4  MieProfileLayer1;
    vec4  OzoneProfileLayer0;
    vec4  OzoneProfileLayer1;

    vec4  SolarDirection;           // rgb: unit surface->sun (Y-up); a: observer altitude above BottomRadius [km]
    vec4  SolarIlluminance;         // rgb: extraterrestrial flux;    a: sequence elapsed seconds [s]

    float BottomRadius;             // [km]
    float TopRadius;                // [km]
    float MieAsymmetry;             // [-1..1]
    float MultipleScatteringFactor; // [0..1]
} Atmosphere;

float ObserverAltitudeKilometres()
{
    return max(0.002, Atmosphere.SolarDirection.w);
}

float AtmosphereSequenceSeconds()
{
    return Atmosphere.SolarIlluminance.w;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PHASE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Rayleigh phase function (isotropic molecular dipole scattering, King depolarization factor included).
float RayleighPhase(float CosineAngle)
{
    return (3.0 / (16.0 * PI)) * (1.0 + CosineAngle * CosineAngle);
}

// Cornette-Shanks phase function (physical improvement over Henyey-Greenstein for aerosol Mie scattering).
float CornetteShanksPhase(float CosineAngle, float Asymmetry)
{
    float G2        = Asymmetry * Asymmetry;
    float Numerator = 3.0 * (1.0 - G2) * (1.0 + CosineAngle * CosineAngle);
    float Base      = max(1.0e-4, 1.0 + G2 - 2.0 * Asymmetry * CosineAngle);
    float Denom     = (8.0 * PI) * (2.0 + G2) * pow(Base, 1.5);
    return Numerator / Denom;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   DENSITY PROFILES
//------------------------------------------------------------------------------------------------------------------------

float SampleProfileLayer(vec4 Layer, float Altitude)
{
    float ExpArgument = (Layer.y != 0.0) ? clamp(-Altitude / Layer.y, -80.0, 0.0) : 0.0;
    float Exponential = (Layer.z != 0.0) ? exp(ExpArgument) * Layer.z : 0.0;
    float Linear      = Layer.w * Altitude;
    return clamp(Exponential + Linear, 0.0, 1.0);
}

float SampleTwoLayerProfile(vec4 Layer0, vec4 Layer1, float Altitude)
{
    if (Layer0.x > 0.0)
    {
        return (Altitude < Layer0.x)
            ? SampleProfileLayer(Layer0, Altitude)
            : SampleProfileLayer(Layer1, Altitude - Layer0.x);
    }
    return SampleProfileLayer(Layer0, Altitude);
}

// Evaluate per-km scattering and total extinction at radius Radius [km] from planet centre.
void EvaluateMediumScattering(float Radius,
                              out vec3 RayleighScattering,
                              out vec3 MieScattering,
                              out vec3 TotalExtinction)
{
    float Altitude = max(0.0, Radius - Atmosphere.BottomRadius);

    float RayleighDensity = SampleTwoLayerProfile(Atmosphere.RayleighProfileLayer0, Atmosphere.RayleighProfileLayer1, Altitude);
    float MieDensity      = SampleTwoLayerProfile(Atmosphere.MieProfileLayer0,      Atmosphere.MieProfileLayer1,      Altitude);

    float OzoneDensity;
    if (Altitude < Atmosphere.OzoneProfileLayer0.x)
        OzoneDensity = clamp(Atmosphere.OzoneProfileLayer0.w * Altitude + (-10.0 / 15.0), 0.0, 1.0);
    else
        OzoneDensity = clamp(Atmosphere.OzoneProfileLayer1.w * Altitude + (40.0 / 15.0), 0.0, 1.0);

    RayleighScattering  = Atmosphere.RayleighScatteringBase.rgb * 1000.0 * RayleighDensity;
    MieScattering       = Atmosphere.MieScatteringBase.rgb      * 1000.0 * MieDensity;
    vec3 MieExtinction  = Atmosphere.MieExtinctionBase.rgb      * 1000.0 * MieDensity;
    vec3 OzoneAbsorb    = Atmosphere.OzoneAbsorptionBase.rgb    * 1000.0 * OzoneDensity;

    TotalExtinction     = RayleighScattering + MieExtinction + OzoneAbsorb;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SPHERE INTERSECTION
//------------------------------------------------------------------------------------------------------------------------

// Ray starts at (0, Radius, 0) with direction unit vector whose Y component is CosineZenith.
// Returns nearest positive intersection distance with sphere of SphereRadius (centred at origin), or -1 if none.
float IntersectSphere(float Radius, float CosineZenith, float SphereRadius)
{
    float Discriminant = Radius * Radius * (CosineZenith * CosineZenith - 1.0) + SphereRadius * SphereRadius;
    if (Discriminant < 0.0)
        return -1.0;

    float Root = sqrt(Discriminant);
    float Near = -Radius * CosineZenith - Root;
    float Far  = -Radius * CosineZenith + Root;
    return (Near >= 0.0) ? Near : ((Far >= 0.0) ? Far : -1.0);
}

// General ray Origin + t * Direction against sphere of SphereRadius centred at (0,0,0). Returns vec2(Near, Far), or (-1,-1) on miss.
vec2 IntersectSphereGeneral(vec3 Origin, vec3 Direction, float SphereRadius)
{
    float ProjB = dot(Origin, Direction);
    float TermC = dot(Origin, Origin) - SphereRadius * SphereRadius;
    float Disc  = ProjB * ProjB - TermC;
    if (Disc < 0.0)
        return vec2(-1.0);
    float Root = sqrt(Disc);
    return vec2(-ProjB - Root, -ProjB + Root);
}

// Distance from (0, Radius, 0) along CosineZenith to the nearest atmosphere boundary (ground if hit, else top).
float DistanceToNearestBoundary(float Radius, float CosineZenith)
{
    float ToGround = IntersectSphere(Radius, CosineZenith, Atmosphere.BottomRadius);
    float ToTop    = IntersectSphere(Radius, CosineZenith, Atmosphere.TopRadius);
    if (ToGround >= 0.0)
        return ToGround;
    return max(0.0, ToTop);
}

bool RayIntersectsGround(float Radius, float CosineZenith)
{
    return (CosineZenith < 0.0)
        && ((Radius * Radius * (CosineZenith * CosineZenith - 1.0)
             + Atmosphere.BottomRadius * Atmosphere.BottomRadius) >= 0.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           SOLAR PHOTOSPHERE & LIMB DARKENING
//------------------------------------------------------------------------------------------------------------------------

// Neckel & Labs (1994) wavelength-dependent Eddington-Barbier solar limb darkening for 650 / 550 / 450 nm.
vec3 EvaluateSolarLimbDarkening(float CosineCenterToLimb)
{
    float Mu = clamp(CosineCenterToLimb, 0.0, 1.0);
    return vec3(
        0.342 + 0.874 * pow(Mu, 0.72) - 0.216 * Mu * Mu,
        0.236 + 0.962 * pow(Mu, 0.80) - 0.198 * Mu * Mu,
        0.124 + 1.058 * pow(Mu, 0.90) - 0.182 * Mu * Mu
    );
}

// Smooth physical circumsolar K-corona + forward aerosol diffraction aureole (continuous outside SolarAngularRadius, zero step seam).
vec3 EvaluateCircumsolarAureole(float CosineSun, float SolarAngularRadius)
{
    float ThetaRad  = acos(clamp(CosineSun, -1.0, 1.0));
    float ExcessDeg = max(0.0, (ThetaRad - SolarAngularRadius) * (180.0 / PI));
    float Inner     = 0.26 / (1.0 + pow(ExcessDeg / 0.16, 1.90));
    float Outer     = 0.022 / (1.0 + pow(ExcessDeg / 1.15, 1.65));
    return Atmosphere.SolarIlluminance.rgb * (Inner * vec3(1.00, 0.96, 0.90) + Outer * vec3(1.00, 0.92, 0.82));
}

#endif
