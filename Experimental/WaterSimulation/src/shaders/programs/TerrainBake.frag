#version 300 es
precision highp float;

#include "TerrainShape.glsl"

uniform float uTerrainDomain;
uniform float uBakeResolution;

out vec4 FragColor;

// 🧩 One-time bake of the analytic shoreline into (height, normal xyz) for cheap runtime lookups
void main()
{
    vec2 Coordinate = gl_FragCoord.xy / uBakeResolution;
    vec2 WorldXz = (Coordinate - 0.5) * uTerrainDomain;

    float Height = TerrainHeight(WorldXz);
    vec3 Normal = TerrainNormal(WorldXz);

    FragColor = vec4(Height, Normal.x, Normal.y, Normal.z);
}
