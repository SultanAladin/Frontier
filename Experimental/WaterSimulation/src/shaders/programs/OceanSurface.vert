#version 300 es
precision highp float;

#include "WaveSampling.glsl"

in vec2 a_GridPosition;   // normalized radial grid coordinate, multiplied by the grid radius

uniform mat4  uViewProjection;
uniform vec3  uCameraPosition;
uniform vec2  uGridCenter;
uniform float uGridRadius;

out vec3  vWorldPosition;
out vec3  vNormal;
out vec2  vSlope;
out float vCrestFoam;
out float vWaveHeight;
out vec2  vWorldXz;
out float vDepth;
out float vViewDistance;

// 🧩 Camera-locked radial surface grid; large-scale displacement comes from the active model
void main()
{
    vec2 WorldXz = a_GridPosition * uGridRadius + uGridCenter;
    WaveSample Sample = SampleWaves(WorldXz);
    float Terrain = SampleTerrainHeight(WorldXz);

    vec3 Position = vec3(WorldXz.x + Sample.Displacement.x,
                         SeaLevel + Sample.Height,
                         WorldXz.y + Sample.Displacement.z);

    // 📝 Below dry sand the surface is tucked away, so the shoreline is a clean intersection curve
    if (Terrain > SeaLevel + 0.02)
    {
        Position.y = Terrain - 0.9;
    }

    vWorldPosition = Position;
    vWorldXz = WorldXz;
    vSlope = Sample.Slope;
    vNormal = DecodeSlopeNormal(Sample.Slope);
    vCrestFoam = Sample.CrestFoam;
    vWaveHeight = Sample.Height;
    vDepth = max(SeaLevel - Terrain, 0.0);
    vViewDistance = length(Position - uCameraPosition);

    gl_Position = uViewProjection * vec4(Position, 1.0);
}
