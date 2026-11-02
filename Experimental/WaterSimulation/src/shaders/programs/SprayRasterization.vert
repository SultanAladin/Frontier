#version 300 es
precision highp float;

#include "Common.glsl"

in vec4 a_Position;   // world xyz, remaining life
in vec4 a_Velocity;   // velocity xyz, droplet size
in vec4 a_Spray;      // seed, age, category, impact

uniform mat4  uViewProjection;
uniform float uPixelScale;

out float vAlpha;
out float vCategory;

void main()
{
    vec4 Clip = uViewProjection * vec4(a_Position.xyz, 1.0);
    gl_Position = Clip;
    gl_PointSize = clamp(a_Velocity.w * uPixelScale / max(Clip.w, 0.15), 1.0, 96.0);

    vAlpha = Saturate(a_Position.w * 1.5) * (a_Spray.z > 0.5 ? 0.45 : 1.0);
    vCategory = a_Spray.z;
}
