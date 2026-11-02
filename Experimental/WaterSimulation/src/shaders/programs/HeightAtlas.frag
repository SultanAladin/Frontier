#version 300 es
precision highp float;

#include "WaveSampling.glsl"

uniform float uAtlasDomain;
uniform float uAtlasResolution;

out vec4 FragColor;

// 📝 One shared world-space height atlas feeds spray collision, vessel buoyancy, and the grid overlay
void main()
{
    vec2 Coordinate = gl_FragCoord.xy / float(uAtlasResolution);
    vec2 WorldXz = (Coordinate - 0.5) * uAtlasDomain;

    WaveSample Sample = SampleWaves(WorldXz);
    float Terrain = SampleTerrainHeight(WorldXz);
    float Surface = SeaLevel + Sample.Height;

    FragColor = vec4(Surface, Terrain, Sample.CrestFoam, max(Surface - Terrain, 0.0));
}
