#version 300 es
precision highp float;

#include "WaveSampling.glsl"

in vec2 a_GridPosition;   // world XZ

uniform mat4  uViewProjection;
uniform float uOverlayLift;

out float vDistance;

void main()
{
    float SurfaceHeight = SeaLevel + SampleWaves(a_GridPosition).Height;
    vec3 Position = vec3(a_GridPosition.x, SurfaceHeight + uOverlayLift, a_GridPosition.y);

    vDistance = length(Position.xz);
    gl_Position = uViewProjection * vec4(Position, 1.0);
}
