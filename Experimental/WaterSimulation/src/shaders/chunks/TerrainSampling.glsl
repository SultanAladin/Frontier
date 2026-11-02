// 🧩 Baked terrain atlas lookups: (height, normal xyz) sampled by the surface, solvers, and spray
#include "Common.glsl"

uniform sampler2D uTerrainAtlas;
uniform float uTerrainDomain;

vec4 SampleTerrainEntry(vec2 WorldXz)
{
    vec2 Coordinate = WorldXz / uTerrainDomain + 0.5;
    return textureLod(uTerrainAtlas, clamp(Coordinate, vec2(0.0006), vec2(0.9994)), 0.0);
}

float SampleTerrainHeight(vec2 WorldXz)
{
    return SampleTerrainEntry(WorldXz).r;
}

vec3 SampleTerrainNormal(vec2 WorldXz)
{
    vec4 Entry = SampleTerrainEntry(WorldXz);
    return normalize(Entry.gba);
}

float SampleWaterDepth(vec2 WorldXz)
{
    return SeaLevel - SampleTerrainHeight(WorldXz);
}

// Wave amplitude fades in shoaling water so crests do not punch through the beach
float ShoalingAttenuation(vec2 WorldXz)
{
    return mix(0.16, 1.0, smoothstep(0.0, 9.0, max(SampleWaterDepth(WorldXz), 0.0)));
}
