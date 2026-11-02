#version 300 es
precision highp float;

#include "Common.glsl"

in vec2 vScreenCoordinate;

uniform sampler2D uSource;
uniform vec2  uTexelSize;
uniform float uThreshold;
uniform float uKnee;

out vec4 FragColor;

void main()
{
    vec2 Offset = uTexelSize;
    vec3 Center = textureLod(uSource, vScreenCoordinate, 0.0).rgb;
    vec3 Left = textureLod(uSource, vScreenCoordinate - vec2(Offset.x, 0.0), 0.0).rgb;
    vec3 Right = textureLod(uSource, vScreenCoordinate + vec2(Offset.x, 0.0), 0.0).rgb;
    vec3 Down = textureLod(uSource, vScreenCoordinate - vec2(0.0, Offset.y), 0.0).rgb;
    vec3 Up = textureLod(uSource, vScreenCoordinate + vec2(0.0, Offset.y), 0.0).rgb;

    vec3 Color = (Center * 2.0 + Left + Right + Down + Up) / 6.0;
    float Brightness = Luminance(Color);
    float Soft = clamp(Brightness - uThreshold + uKnee, 0.0, 2.0 * uKnee);
    Soft = Soft * Soft / (4.0 * uKnee + 0.0001);
    float Contribution = max(Soft, Brightness - uThreshold) / max(Brightness, 0.0001);

    FragColor = vec4(Color * Contribution, 1.0);
}
