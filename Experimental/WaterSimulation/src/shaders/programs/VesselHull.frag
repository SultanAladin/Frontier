#version 300 es
precision highp float;

#include "Common.glsl"
#include "SkyRadiance.glsl"
#include "WaterShading.glsl"

in vec3 vWorldPosition;
in vec3 vNormal;
in vec3 vColor;

uniform vec3 uCameraPosition;
uniform int  uReflectionPass;

out vec4 FragColor;

// 🧩 Painted hull, teak deck, and a waterline stripe with sky ambient and sun specular
void main()
{
    if (uReflectionPass == 1 && vWorldPosition.y < SeaLevel + 0.02)
    {
        discard;
    }

    vec3 Normal = normalize(vNormal);
    vec3 ViewDirection = normalize(uCameraPosition - vWorldPosition);
    vec3 SunColor = uSunRadiance;

    vec3 Ambient = SkyRadiance(vec3(0.0, 1.0, 0.0), 2) * 0.55;
    vec3 SkyBounce = SkyRadiance(reflect(-ViewDirection, Normal), 2) * 0.25;

    float NormalDotLight = Saturate(dot(Normal, uSunDirection));
    vec3 Albedo = vColor * (Ambient + SunColor * NormalDotLight * 0.85);
    Albedo += SkyBounce * vColor;

    // Glossy gel-coat response
    Albedo += SunSpecular(Normal, ViewDirection, uSunDirection, SunColor, 0.22, 0.05) * 1.4;

    float FresnelTerm = 0.04 + 0.96 * pow(1.0 - Saturate(dot(Normal, ViewDirection)), 5.0);
    Albedo = mix(Albedo, SkyRadiance(reflect(-ViewDirection, Normal), 2), FresnelTerm * 0.25);

    FragColor = vec4(Albedo, 1.0);
}
