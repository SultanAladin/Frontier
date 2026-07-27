// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//                                                       ATMOSPHERE COMMON 🧩
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// Shared Hillaire 2020 atmosphere math: the std140 AtmosphereBlock (mirrors C++ AtmosphereUniformBlock), the two-layer density profiles, the
// Rayleigh + Cornette-Shanks phase functions, ray-sphere boundary intersection, and altitude advance. Every sky shader #includes this so the
// scattering physics stays identical across the transmittance bake, the multi-scatter bake, the sky-view bake, and the per-frame sky dome.

const float PI = 3.14159265358979323846;

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// ATMOSPHERE UNIFORM BLOCK (std140, mirrors AtmosphereProfile.h::AtmosphereUniformBlock, 272 bytes)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

layout(std140, binding = 0) uniform AtmosphereBlock
{
    vec4  RayleighScatteringBase;    // rgb [1/m]; a pad
    vec4  MieScatteringBase;         // rgb [1/m]; a pad
    vec4  MieExtinctionBase;         // rgb [1/m]; a pad
    vec4  OzoneAbsorptionBase;       // rgb [1/m]; a pad

    vec4  RayleighProfileLayer0;     // (Width, ExponentialTerm, ExponentialScale, LinearTerm)
    vec4  RayleighProfileLayer1;
    vec4  MieProfileLayer0;
    vec4  MieProfileLayer1;
    vec4  OzoneProfileLayer0;
    vec4  OzoneProfileLayer1;

    vec4  SolarDirection;            // rgb unit surface→sun; a pad
    vec4  SolarIlluminance;          // rgb relative flux; a pad

    float BottomRadius;              // [km]
    float TopRadius;                 // [km]
    float MieAsymmetry;              // [-1..1]
    float MultipleScatteringFactor;  // [0..1]
} Atmosphere;

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// DENSITY PROFILES  (h in km; layer vec4 = Width, ExponentialTerm, ExponentialScale, LinearTerm)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

float EvaluateLayerDensity(vec4 Layer, float Altitude)
{
    float Density = exp(-Altitude / max(Layer.y, 1e-4)) * Layer.z + Layer.w * Altitude;
    return clamp(Density, 0.0, 1.0);
}

float EvaluateProfileDensity(vec4 Layer0, vec4 Layer1, float Altitude)
{
    // Width == 0 on Layer0 means "Layer0 applies everywhere"; otherwise split at Width.
    if (Layer0.x <= 0.0)
        return EvaluateLayerDensity(Layer0, Altitude);
    return (Altitude < Layer0.x) ? EvaluateLayerDensity(Layer0, Altitude)
                                 : EvaluateLayerDensity(Layer1, Altitude);
}

// 📝 Total scattering (Rayleigh + Mie) and total extinction (Rayleigh + Mie extinction + ozone) at an altitude.
void EvaluateScatterExtinction(float Altitude, out vec3 Scattering, out vec3 Extinction)
{
    float RayleighDensity = EvaluateProfileDensity(Atmosphere.RayleighProfileLayer0, Atmosphere.RayleighProfileLayer1, Altitude);
    float MieDensity      = EvaluateProfileDensity(Atmosphere.MieProfileLayer0,      Atmosphere.MieProfileLayer1,      Altitude);
    float OzoneDensity    = EvaluateProfileDensity(Atmosphere.OzoneProfileLayer0,    Atmosphere.OzoneProfileLayer1,    Altitude);

    vec3 RayleighScatter = Atmosphere.RayleighScatteringBase.rgb * RayleighDensity;
    vec3 MieScatter      = Atmosphere.MieScatteringBase.rgb      * MieDensity;

    Scattering = RayleighScatter + MieScatter;
    Extinction = RayleighScatter
               + Atmosphere.MieExtinctionBase.rgb   * MieDensity
               + Atmosphere.OzoneAbsorptionBase.rgb * OzoneDensity;
}

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// PHASE FUNCTIONS
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

float EvaluateRayleighPhase(float CosineAngle)
{
    return (3.0 / (16.0 * PI)) * (1.0 + CosineAngle * CosineAngle);
}

float EvaluateCornetteShanksPhase(float CosineAngle, float Asymmetry)
{
    float AsymmetrySq = Asymmetry * Asymmetry;
    float Numerator   = 3.0 * (1.0 - AsymmetrySq) * (1.0 + CosineAngle * CosineAngle);
    float Denominator = 8.0 * PI * (2.0 + AsymmetrySq) *
                        pow(max(1.0 + AsymmetrySq - 2.0 * Asymmetry * CosineAngle, 1e-4), 1.5);
    return Numerator / Denominator;
}

// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// GEOMETRY  (all lengths in km; the planet centre is the origin, r = distance from centre)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

// 📝 Nearest positive distance from a point at radius r (with view-zenith cosine μ) to a sphere of the given radius.
//    Returns -1 when the ray misses. Used for both the atmosphere-top boundary and ground-intersection tests.
float IntersectSphere(float RadiusFromCentre, float ViewZenithCosine, float SphereRadius)
{
    float B    = RadiusFromCentre * ViewZenithCosine;
    float Disc = B * B - (RadiusFromCentre * RadiusFromCentre - SphereRadius * SphereRadius);
    if (Disc < 0.0) return -1.0;
    Disc = sqrt(Disc);
    float Near = -B - Disc;
    float Far  = -B + Disc;
    if (Far < 0.0) return -1.0;
    return (Near < 0.0) ? Far : Near;
}

// 📝 Radius from centre after advancing Distance km along a ray from radius r with view-zenith cosine μ.
float AdvanceRadius(float RadiusFromCentre, float ViewZenithCosine, float Distance)
{
    return sqrt(max(RadiusFromCentre * RadiusFromCentre
                    + Distance * Distance
                    + 2.0 * RadiusFromCentre * ViewZenithCosine * Distance, 0.0));
}
