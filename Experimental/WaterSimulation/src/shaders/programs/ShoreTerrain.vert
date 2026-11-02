#version 300 es
precision highp float;

#include "TerrainSampling.glsl"

in vec2 a_GridPosition;   // normalized [-0.5, 0.5] terrain domain coordinate

uniform mat4  uViewProjection;
uniform float uTerrainExtent;

out vec3  vWorldPosition;
out vec3  vNormal;
out vec2  vWorldXz;
out float vDepth;

// 🧩 Global sand/seabed grid; the same analytic profile drives the solvers and the shoreline foam
void main()
{
    vec2 WorldXz = a_GridPosition * uTerrainExtent;
    float Height = SampleTerrainHeight(WorldXz);

    vWorldPosition = vec3(WorldXz.x, Height, WorldXz.y);
    vWorldXz = WorldXz;
    vNormal = SampleTerrainNormal(WorldXz);
    vDepth = SeaLevel - Height;

    gl_Position = uViewProjection * vec4(vWorldPosition, 1.0);
}
