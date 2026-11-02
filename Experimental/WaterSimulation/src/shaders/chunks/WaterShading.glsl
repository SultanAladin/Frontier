// 🧩 Water BRDF terms, foam texturing, micro ripple slopes, and crest subsurface scattering
#include "Common.glsl"
#include "Noise.glsl"

uniform float uSimulationTime;
uniform float uFoamIntensity;
uniform float uMicroDetail;

float DistributionGgx(float NormalDotHalf, float Roughness)
{
    float Alpha = Roughness * Roughness;
    float AlphaSquared = Alpha * Alpha;
    float Denominator = NormalDotHalf * NormalDotHalf * (AlphaSquared - 1.0) + 1.0;
    return AlphaSquared / max(Pi * Denominator * Denominator, 0.0001);
}

float GeometrySchlickGgx(float NormalDotDirection, float Roughness)
{
    float K = (Roughness + 1.0) * (Roughness + 1.0) / 8.0;
    return NormalDotDirection / max(NormalDotDirection * (1.0 - K) + K, 0.0001);
}

vec3 FresnelSchlick(float CosTheta, vec3 Reflectance)
{
    return Reflectance + (1.0 - Reflectance) * pow(1.0 - Saturate(CosTheta), 5.0);
}

vec3 SunSpecular(vec3 Normal, vec3 ViewDirection, vec3 SunDirection, vec3 SunRadiance, float Roughness, float F0)
{
    vec3 HalfVector = normalize(ViewDirection + SunDirection);
    float NormalDotHalf = Saturate(dot(Normal, HalfVector));
    float NormalDotView = Saturate(dot(Normal, ViewDirection));
    float NormalDotLight = Saturate(dot(Normal, SunDirection));
    float ViewDotHalf = Saturate(dot(ViewDirection, HalfVector));

    float Distribution = DistributionGgx(NormalDotHalf, Roughness);
    float Geometry = GeometrySchlickGgx(NormalDotView, Roughness) * GeometrySchlickGgx(NormalDotLight, Roughness);
    vec3 Fresnel = FresnelSchlick(ViewDotHalf, vec3(F0));

    return SunRadiance * (Distribution * Geometry * Fresnel / max(4.0 * NormalDotView * NormalDotLight, 0.0008));
}

// 💡 Two gradient tiers of value noise give sub-texel capillary detail without extra simulation cost
vec2 MicroRippleSlope(vec2 WorldXz, float Strength)
{
    vec2 Primary = WorldXz * 1.35 + vec2(uSimulationTime * 0.09, uSimulationTime * -0.055);
    vec2 Secondary = WorldXz * 3.10 + vec2(uSimulationTime * -0.17, uSimulationTime * 0.12);

    vec2 PrimaryGradient = FractalGradient(Primary, 3, 0.9);
    vec2 SecondaryGradient = FractalGradient(Secondary, 2, 0.9);

    return PrimaryGradient * 0.85 + SecondaryGradient * 0.42 * Strength;
}

float FoamPattern(vec2 WorldXz, float Coverage)
{
    vec2 Drifted = SwirlCoordinate(WorldXz * 0.62 + vec2(uSimulationTime * 0.014, uSimulationTime * -0.022),
                                   uSimulationTime, 0.55);
    vec2 Lattice = FoamLattice(Drifted);
    float Bubbles = smoothstep(0.62, 0.06, Lattice.x);
    float Fibres = FractalNoise(Drifted * 2.6 + Lattice.y * 9.0, 3);
    float Pattern = Bubbles * (0.35 + 0.9 * Fibres);

    return Saturate(Pattern * (0.55 + Coverage * 2.1) + Coverage * 0.55);
}

vec3 CrestScattering(vec3 ViewDirection, vec3 SunDirection, vec3 Normal, float CrestHeight, float Strength)
{
    float BackLight = Saturate(dot(ViewDirection, -SunDirection));
    float Crest = Saturate(CrestHeight * 2.2 + 0.12);
    float Wrap = pow(BackLight, 2.4) * 0.7 + 0.3;
    float Facing = Saturate(dot(Normal, SunDirection) * 0.5 + 0.5);

    return vec3(0.09, 0.52, 0.44) * Wrap * Crest * Facing * Strength;
}

vec3 WaterAbsorption(float PathLength, vec3 AbsorptionCoefficient)
{
    return exp(-AbsorptionCoefficient * max(PathLength, 0.0));
}

vec3 WaterScatteringTint(float Clarity)
{
    vec3 MurkyTint = vec3(0.055, 0.185, 0.160);
    vec3 ClearTint = vec3(0.012, 0.085, 0.135);
    return mix(MurkyTint, ClearTint, Saturate(Clarity));
}
