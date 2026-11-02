#version 300 es
precision highp float;

in vec4 a_Splat;   // x, z, impulse strength, point size in pixels

uniform vec2 uDomain;

out float vStrength;

void main()
{
    vec2 Coordinate = a_Splat.xy / uDomain + 0.5;
    gl_Position = vec4(Coordinate * 2.0 - 1.0, 0.0, 1.0);
    gl_PointSize = clamp(a_Splat.w, 1.0, 96.0);
    vStrength = a_Splat.z;
}
