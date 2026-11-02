#version 300 es
precision highp float;

in float vStrength;

uniform float uFoamWeight;
uniform float uVelocityWeight;

out vec4 FragColor;

void main()
{
    vec2 Offset = gl_PointCoord * 2.0 - 1.0;
    float Falloff = clamp(1.0 - dot(Offset, Offset), 0.0, 1.0);
    float Strength = Falloff * Falloff * vStrength;
    FragColor = vec4(Strength * uFoamWeight, Strength * uVelocityWeight, 0.0, 0.0);
}
