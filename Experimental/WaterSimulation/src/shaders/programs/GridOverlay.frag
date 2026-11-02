#version 300 es
precision highp float;

#include "Common.glsl"

in float vDistance;

uniform vec3  uOverlayColor;
uniform float uOverlayOpacity;

out vec4 FragColor;

void main()
{
    float Fade = 1.0 - smoothstep(120.0, 360.0, vDistance);
    FragColor = vec4(uOverlayColor, uOverlayOpacity * Fade);
}
