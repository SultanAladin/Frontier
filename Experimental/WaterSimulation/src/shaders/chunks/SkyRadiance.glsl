// 🧩 Analytic sky radiance: altitude gradient, sun disc, Mie glow, and drifting fbm cloud deck
#include "Noise.glsl"

uniform vec3  uSunDirection;
uniform vec3  uSunRadiance;
uniform float uSkyTime;
uniform float uCloudCover;
uniform float uSkyHaze;

vec3 SkyZenithColor()
{
    vec3 CoolZenith = vec3(0.045, 0.115, 0.245);
    vec3 WarmZenith = vec3(0.105, 0.225, 0.480);
    return mix(CoolZenith, WarmZenith, uSkyHaze);
}

vec3 SkyHorizonColor()
{
    vec3 CoolHorizon = vec3(0.52, 0.60, 0.70);
    vec3 WarmHorizon = vec3(0.86, 0.68, 0.46);
    return mix(CoolHorizon, WarmHorizon, uSkyHaze);
}

vec3 CloudRadiance(vec3 Direction, float CloudDensity)
{
    float SunDot = max(dot(Direction, uSunDirection), 0.0);
    vec3 LitFace = mix(vec3(0.42, 0.46, 0.54), uSunRadiance * 0.55, pow(SunDot, 2.0));
    vec3 ShadowFace = vec3(0.16, 0.19, 0.26);

    return mix(ShadowFace, LitFace, CloudDensity) * (0.55 + 0.55 * CloudDensity);
}

vec3 SkyRadiance(vec3 Direction, int CloudOctaves)
{
    vec3 Unit = normalize(Direction);
    float Altitude = Saturate(Unit.y);
    vec3 Zenith = SkyZenithColor();
    vec3 Horizon = SkyHorizonColor();

    float Gradient = pow(Altitude, 0.42);
    vec3 Color = mix(Horizon, Zenith, Gradient);

    float SunDot = max(dot(Unit, uSunDirection), 0.0);

    // Mie forward scattering around the sun plus the horizon warm band
    Color += uSunRadiance * 0.085 * pow(SunDot, 6.0);
    Color += uSunRadiance * 0.42 * pow(SunDot, 240.0);
    Color += uSunRadiance * 0.30 * pow(SunDot, 3.0) * exp(-Altitude * 4.5) * (1.0 - uSkyHaze * 0.4);
    Color += uSunRadiance * 42.0 * pow(SunDot, 2600.0);

    if (CloudOctaves > 0 && Unit.y > 0.012)
    {
        vec2 CloudPlane = Unit.xz / Unit.y * 0.55;
        vec2 Drifted = CloudPlane * 0.85 + vec2(uSkyTime * 0.012, uSkyTime * 0.006);
        float Base = FractalNoise(Drifted, CloudOctaves);
        float Detail = FractalNoise(Drifted * 2.7 + 13.1, min(CloudOctaves, 4));
        float Density = Saturate((Base * 0.72 + Detail * 0.28 - (1.0 - uCloudCover) * 0.62) * 2.35);
        Density *= smoothstep(0.012, 0.16, Unit.y);

        vec3 CloudColor = CloudRadiance(Unit, Density);
        Color = mix(Color, CloudColor, Density * 0.92);
    }

    return Color;
}

vec3 SkyRadianceSimple(vec3 Direction)
{
    return SkyRadiance(Direction, 0);
}

// 📝 Sun disc radiance used by the water specular term and the terrain sheen
vec3 SunDiscRadiance()
{
    return uSunRadiance * 26.0;
}
