#version 300 es
precision highp float;

in vec4 a_Position;   // world xyz, remaining life
in vec4 a_Spray;      // seed, age, category, impact

uniform vec2  uDomain;
uniform float uPointSize;

out float vStrength;

void main()
{
    vec2 Coordinate = a_Position.xz / uDomain + 0.5;
    gl_Position = vec4(Coordinate * 2.0 - 1.0, 0.0, 1.0);
    gl_PointSize = clamp(uPointSize, 1.0, 28.0);
    vStrength = a_Spray.w;
}
