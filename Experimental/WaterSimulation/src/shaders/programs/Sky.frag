#version 300 es
precision highp float;

#include "Common.glsl"
#include "SkyRadiance.glsl"

in vec2 vScreenCoordinate;

uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;

out vec4 FragColor;

void main()
{
    vec4 FarPoint = uInverseViewProjection * vec4(vScreenCoordinate * 2.0 - 1.0, 1.0, 1.0);
    vec3 Direction = normalize(FarPoint.xyz / FarPoint.w - uCameraPosition);

    vec3 Color = SkyRadiance(Direction, 5);
    FragColor = vec4(Color, 1.0);
}
