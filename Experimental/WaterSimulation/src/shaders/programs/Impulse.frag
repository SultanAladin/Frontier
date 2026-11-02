#version 300 es
precision highp float;

in float vStrength;

out vec4 FragColor;

void main()
{
    vec2 Offset = gl_PointCoord * 2.0 - 1.0;
    float Falloff = clamp(1.0 - dot(Offset, Offset), 0.0, 1.0);
    FragColor = vec4(0.0, Falloff * Falloff * vStrength, 0.0, 0.0);
}
