#version 300 es
precision highp float;

#include "Common.glsl"
#include "Noise.glsl"
#include "TerrainSampling.glsl"
#include "SkyRadiance.glsl"
#include "WaterShading.glsl"

in vec3  vWorldPosition;
in vec3  vNormal;
in vec2  vWorldXz;
in float vDepth;

uniform vec3  uCameraPosition;
uniform int   uClipBelowWater;
uniform vec3  uAbsorption;
uniform float uWaterClarity;
uniform int   uSubsurfaceEnabled;

out vec4 FragColor;

// 🧩 Sand, wet beach band, algae shelf, rock faces, and underwater caustics
void main()
{
    if (uClipBelowWater == 1 && vWorldPosition.y < SeaLevel + 0.02)
    {
        discard;
    }

    vec3 Normal = normalize(vNormal);
    vec3 ViewDirection = normalize(uCameraPosition - vWorldPosition);
    vec3 SunColor = uSunRadiance;

    float Grain = FractalNoise(vWorldXz * 3.4, 3);
    float Patch = FractalNoise(vWorldXz * 0.31, 4);
    float Depth = vDepth;

    vec3 DrySand = vec3(0.80, 0.72, 0.56) * (0.84 + 0.32 * Grain);
    vec3 WetSand = vec3(0.40, 0.35, 0.28) * (0.88 + 0.24 * Grain);
    float Wetness = 1.0 - smoothstep(0.0, 1.35, Depth);
    vec3 Albedo = mix(DrySand, WetSand, Saturate(Wetness));

    Albedo = mix(Albedo, vec3(0.19, 0.235, 0.175) * (0.7 + 0.7 * Patch), smoothstep(1.8, 9.0, Depth));

    float Steepness = 1.0 - normalize(vNormal).y;
    vec3 RockColor = vec3(0.215, 0.205, 0.195) * (0.72 + 0.55 * Grain);
    Albedo = mix(Albedo, RockColor, smoothstep(0.32, 0.72, Steepness));

    vec3 Ambient = SkyRadiance(vec3(0.0, 1.0, 0.0), 2) * 0.42;
    float NormalDotLight = Saturate(dot(Normal, uSunDirection));
    vec3 Color = Albedo * (Ambient + SunColor * NormalDotLight);

    // Wet sand keeps a glossy sheen; dry sand stays diffuse
    float Sheen = mix(0.22, 0.85, Saturate(Wetness));
    Color += SunSpecular(Normal, ViewDirection, uSunDirection, SunColor, mix(0.62, 0.14, Saturate(Wetness)), 0.028) * Sheen;

    // Submerged sand is filtered through the water column
    if (Depth > 0.0 && uSubsurfaceEnabled == 1)
    {
        float CausticField = FractalNoise(vWorldXz * 0.85 + vec2(uSimulationTime * 0.13, uSimulationTime * 0.09), 3);
        float Caustics = pow(Saturate(CausticField * 1.7 - 0.35), 5.0) * 6.0;
        Color += Albedo * SunColor * Caustics * Saturate(1.0 - Depth / 9.0) * 0.55 * Saturate(dot(Normal, uSunDirection));
        Color *= exp(-uAbsorption * Depth * mix(1.6, 0.5, uWaterClarity));
    }

    FragColor = vec4(Color, 1.0);
}
