#version 300 es
precision highp float;

#include "Common.glsl"
#include "Noise.glsl"

in vec2 vScreenCoordinate;

uniform sampler2D uSceneColor;
uniform sampler2D uBloomNear;
uniform sampler2D uBloomFar;
uniform vec2  uTexelSize;
uniform float uExposure;
uniform float uBloomStrength;
uniform float uAntialiasStrength;
uniform float uVignette;
uniform int   uBloomEnabled;
uniform int   uRawView;

out vec4 FragColor;

vec3 AcesTonemap(vec3 Color)
{
    const float A = 2.51;
    const float B = 0.03;
    const float C = 2.43;
    const float D = 0.59;
    const float E = 0.14;

    return Saturate3((Color * (A * Color + B)) / (Color * (C * Color + D) + E));
}

vec3 Composite(vec2 Coordinate)
{
    vec3 Scene = textureLod(uSceneColor, Coordinate, 0.0).rgb;

    if (uBloomEnabled == 1)
    {
        vec3 NearBloom = textureLod(uBloomNear, Coordinate, 0.0).rgb;
        vec3 FarBloom = textureLod(uBloomFar, Coordinate, 0.0).rgb;
        Scene += (NearBloom * 0.65 + FarBloom * 0.55) * uBloomStrength;
    }

    return Scene;
}

void main()
{
    vec3 Hdr;

    if (uAntialiasStrength > 0.001)
    {
        // 💡 Five-tap rotating-grid resolve: cheap edge softening without a separate FXAA pass
        vec2 Offset = uTexelSize * uAntialiasStrength;
        Hdr = Composite(vScreenCoordinate) * 0.4;
        Hdr += Composite(vScreenCoordinate + vec2(Offset.x, Offset.y) * 0.75) * 0.15;
        Hdr += Composite(vScreenCoordinate + vec2(-Offset.x, Offset.y) * 0.75) * 0.15;
        Hdr += Composite(vScreenCoordinate + vec2(Offset.x, -Offset.y) * 0.75) * 0.15;
        Hdr += Composite(vScreenCoordinate + vec2(-Offset.x, -Offset.y) * 0.75) * 0.15;
    }
    else
    {
        Hdr = Composite(vScreenCoordinate);
    }

    if (uRawView == 1)
    {
        FragColor = vec4(Saturate3(Hdr), 1.0);
        return;
    }

    vec3 Color = AcesTonemap(Hdr * uExposure);
    Color = pow(Color, vec3(1.0 / 2.2));

    float VignetteShape = 1.0 - dot(vScreenCoordinate - 0.5, vScreenCoordinate - 0.5) * uVignette * 1.6;
    Color *= Saturate(VignetteShape);

    float Dither = HashScalar(gl_FragCoord.xy + vec2(uVignette * 17.0)) - 0.5;
    Color += Dither * 0.0025;

    FragColor = vec4(Color, 1.0);
}
