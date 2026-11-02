#version 300 es
precision highp float;

#include "Common.glsl"

uniform sampler2D uDisplacement;
uniform int   uResolution;
uniform float uTexelWorld;

out vec4 FragColor;

void main()
{
    vec2 Coordinate = gl_FragCoord.xy / float(uResolution);
    vec2 Offset = vec2(1.0 / float(uResolution), 0.0);
    vec2 OffsetVertical = vec2(0.0, 1.0 / float(uResolution));

    vec4 Left = textureLod(uDisplacement, Coordinate - Offset, 0.0);
    vec4 Right = textureLod(uDisplacement, Coordinate + Offset, 0.0);
    vec4 Down = textureLod(uDisplacement, Coordinate - OffsetVertical, 0.0);
    vec4 Up = textureLod(uDisplacement, Coordinate + OffsetVertical, 0.0);
    vec4 Center = textureLod(uDisplacement, Coordinate, 0.0);

    float SlopeX = (Right.y - Left.y) / (2.0 * uTexelWorld);
    float SlopeZ = (Up.y - Down.y) / (2.0 * uTexelWorld);

    float DerivativeXX = (Right.x - Left.x) / (2.0 * uTexelWorld);
    float DerivativeZZ = (Up.z - Down.z) / (2.0 * uTexelWorld);
    float DerivativeXZ = (Up.x - Down.x) / (2.0 * uTexelWorld);
    float DerivativeZX = (Right.z - Left.z) / (2.0 * uTexelWorld);

    float Jacobian = (1.0 + DerivativeXX) * (1.0 + DerivativeZZ) - DerivativeXZ * DerivativeZX;
    float Foam = 1.0 - smoothstep(0.02, 0.68, Jacobian);

    FragColor = vec4(SlopeX, SlopeZ, Foam, Center.y);
}
