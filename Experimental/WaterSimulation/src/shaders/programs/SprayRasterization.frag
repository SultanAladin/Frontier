#version 300 es
precision highp float;

#include "Common.glsl"

in float vAlpha;
in float vCategory;

uniform sampler2D uSceneDepth;
uniform vec2  uViewport;
uniform float uNearPlane;
uniform float uFarPlane;
uniform vec3  uSprayTint;

out vec4 FragColor;

void main()
{
    vec2 Offset = gl_PointCoord * 2.0 - 1.0;
    float Radius = dot(Offset, Offset);

    if (Radius > 1.0)
    {
        discard;
    }

    float Body = Saturate(1.0 - Radius);
    float Alpha = pow(Body, 1.7) * vAlpha;

    // 📝 Soft intersection against the opaque depth keeps droplets from slicing the sand
    float SceneDepthValue = textureLod(uSceneDepth, gl_FragCoord.xy / uViewport, 0.0).r;
    float SceneDistance = LinearizeDepth(SceneDepthValue, uNearPlane, uFarPlane);
    float DropletDistance = LinearizeDepth(gl_FragCoord.z, uNearPlane, uFarPlane);
    Alpha *= Saturate((SceneDistance - DropletDistance) * 1.8);

    FragColor = vec4(uSprayTint * (1.0 + vCategory * 0.35), Alpha * 0.9);
}
