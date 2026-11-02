#version 300 es
precision highp float;

#include "Common.glsl"

uniform sampler2D uFieldA;
uniform sampler2D uFieldB;
uniform float uAmplitudeScale;
uniform float uChoppiness;
uniform int   uResolution;

out vec4 FragColor;

void main()
{
    vec2 Coordinate = gl_FragCoord.xy / float(uResolution);
    vec4 EntryA = textureLod(uFieldA, Coordinate, 0.0);
    vec4 EntryB = textureLod(uFieldB, Coordinate, 0.0);

    // 📝 Tessendorf sums over wave vectors directly; the butterfly output is already that sum
    float Normalization = uAmplitudeScale;
    float Height = EntryA.x;
    vec2 Horizontal = vec2(EntryA.z, EntryB.x) * uChoppiness;

    FragColor = vec4(Horizontal.x * Normalization, Height * Normalization, Horizontal.y * Normalization, 0.0);
}
