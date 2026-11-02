#version 300 es
precision highp float;

#include "Common.glsl"

in vec2 vScreenCoordinate;

uniform sampler2D uSource;
uniform vec2 uDirection;   // texel-sized step along one axis

out vec4 FragColor;

void main()
{
    vec3 Sum = textureLod(uSource, vScreenCoordinate, 0.0).rgb * 0.227027;
    Sum += textureLod(uSource, vScreenCoordinate + uDirection * 1.3846153846, 0.0).rgb * 0.3162162162;
    Sum += textureLod(uSource, vScreenCoordinate - uDirection * 1.3846153846, 0.0).rgb * 0.3162162162;
    Sum += textureLod(uSource, vScreenCoordinate + uDirection * 3.2307692308, 0.0).rgb * 0.0702702703;
    Sum += textureLod(uSource, vScreenCoordinate - uDirection * 3.2307692308, 0.0).rgb * 0.0702702703;

    FragColor = vec4(Sum, 1.0);
}
