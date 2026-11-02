#version 300 es
precision highp float;

#include "Common.glsl"
#include "Noise.glsl"
#include "SkyRadiance.glsl"
#include "WaterShading.glsl"

in vec3  vWorldPosition;
in vec3  vNormal;
in vec2  vSlope;
in float vCrestFoam;
in float vWaveHeight;
in vec2  vWorldXz;
in float vDepth;
in float vViewDistance;

uniform vec3  uCameraPosition;
uniform mat4  uReflectionViewProjection;
uniform sampler2D uReflectionTexture;
uniform sampler2D uSceneColor;
uniform sampler2D uSceneDepth;
uniform sampler2D uFoamField;
uniform float uDomain;
uniform float uNearPlane;
uniform float uFarPlane;
uniform vec2  uViewport;
uniform int   uReflectionEnabled;
uniform int   uRefractionEnabled;
uniform int   uSubsurfaceEnabled;
uniform int   uFoamEnabled;
uniform int   uFieldViewMode;
uniform float uWaterClarity;
uniform vec3  uAbsorption;
uniform float uShoreFoamWidth;
uniform float uWaveHeight;

out vec4 FragColor;

// 🧩 Physically based water shading: screen-space refraction, mirror reflection, GGX glitter, foam, scattering
void main()
{
    vec3 ViewVector = uCameraPosition - vWorldPosition;
    float ViewLength = length(ViewVector);
    vec3 ViewDirection = ViewVector / max(ViewLength, 0.0001);

    vec2 MicroSlope = MicroRippleSlope(vWorldXz, uMicroDetail) * mix(0.3, 1.0, Saturate(vDepth / 6.0));
    vec2 TotalSlope = vSlope + MicroSlope;
    vec3 Normal = DecodeSlopeNormal(TotalSlope);

    vec3 AmbientSky = SkyRadiance(vec3(0.0, 1.0, 0.0), 2);
    vec3 SunColor = uSunRadiance;

    // --- Foam coverage from the global foam grid, crest breaking, and the shoreline band ---
    float FoamCoverage = 0.0;

    if (uFoamEnabled == 1)
    {
        vec2 FoamCoordinate = vWorldXz / uDomain + 0.5;
        float GridFoam = 0.0;

        if (FoamCoordinate.x >= 0.0 && FoamCoordinate.x <= 1.0 && FoamCoordinate.y >= 0.0 && FoamCoordinate.y <= 1.0)
        {
            GridFoam = textureLod(uFoamField, FoamCoordinate, 0.0).r;
        }

        float CrestFoam = vCrestFoam * uFoamIntensity;
        float ShoreBand = 1.0 - smoothstep(0.0, max(uShoreFoamWidth, 0.05), vDepth);
        float ShoreFoam = ShoreBand * uFoamIntensity * 0.85;
        FoamCoverage = Saturate(GridFoam * (0.85 + uFoamIntensity * 0.6) + CrestFoam + ShoreFoam);
    }

    float FoamMask = FoamCoverage > 0.002 ? FoamPattern(vWorldXz, FoamCoverage) * Saturate(FoamCoverage * 1.35) : 0.0;

    // --- Refraction of the opaque scene behind the surface ---
    vec2 ScreenCoordinate = gl_FragCoord.xy / uViewport;
    float SurfaceDistance = LinearizeDepth(gl_FragCoord.z, uNearPlane, uFarPlane);
    vec3 RefractedColor = vec3(0.0);
    float OpticalThickness = 0.0;

    if (uRefractionEnabled == 1)
    {
        vec2 Distortion = TotalSlope * (0.055 / (1.0 + ViewLength * 0.035));
        vec2 RefractedCoordinate = clamp(ScreenCoordinate + Distortion, vec2(0.002), vec2(0.998));
        float SceneDistance = LinearizeDepth(textureLod(uSceneDepth, RefractedCoordinate, 0.0).r, uNearPlane, uFarPlane);

        if (SceneDistance < SurfaceDistance)
        {
            RefractedCoordinate = ScreenCoordinate;
            SceneDistance = LinearizeDepth(textureLod(uSceneDepth, ScreenCoordinate, 0.0).r, uNearPlane, uFarPlane);
        }

        OpticalThickness = max(SceneDistance - SurfaceDistance, 0.0);
        RefractedColor = textureLod(uSceneColor, RefractedCoordinate, 0.0).rgb;
    }
    else
    {
        OpticalThickness = vDepth * 1.4;
    }

    // --- Reflection: analytic sky blended with the mirrored scene render ---
    vec3 ReflectionVector = reflect(-ViewDirection, Normal);
    vec3 ReflectionColor = SkyRadiance(ReflectionVector, 5);

    if (uReflectionEnabled == 1)
    {
        vec4 MirrorClip = uReflectionViewProjection * vec4(vWorldPosition, 1.0);

        if (MirrorClip.w > 0.0)
        {
            vec2 MirrorCoordinate = MirrorClip.xy / MirrorClip.w * 0.5 + 0.5;

            if (MirrorCoordinate.x >= 0.0 && MirrorCoordinate.x <= 1.0
                && MirrorCoordinate.y >= 0.0 && MirrorCoordinate.y <= 1.0)
            {
                vec2 BlurredCoordinate = clamp(MirrorCoordinate + TotalSlope * 0.05, vec2(0.002), vec2(0.998));
                vec3 MirrorColor = textureLod(uReflectionTexture, BlurredCoordinate, 0.0).rgb;
                ReflectionColor = mix(ReflectionColor, MirrorColor, Saturate(0.92 - length(TotalSlope) * 0.3));
            }
        }
    }

    // --- Volumetric absorption and in-scattering for the water body ---
    float ClarityDepth = min(OpticalThickness, 26.0);
    vec3 Transmittance = WaterAbsorption(ClarityDepth, uAbsorption * mix(1.9, 0.55, uWaterClarity));
    vec3 ScatteringTint = WaterScatteringTint(uWaterClarity);
    vec3 WaterBody = RefractedColor * Transmittance
                     + ScatteringTint * (1.0 - Transmittance) * (AmbientSky * 0.55 + SunColor * 0.16);

    float FresnelTerm = 0.02 + 0.98 * pow(1.0 - Saturate(dot(Normal, ViewDirection)), 5.0);
    vec3 Color = mix(WaterBody, ReflectionColor, FresnelTerm);

    // --- Sun glitter ---
    float Roughness = mix(0.035, 0.17, Saturate(length(TotalSlope) * 0.55));
    Color += SunSpecular(Normal, ViewDirection, uSunDirection, SunColor * 1.7, Roughness, 0.02);

    // --- Crest scattering: light bleeding through thin wave tops ---
    if (uSubsurfaceEnabled == 1)
    {
        float CrestRatio = Saturate(vWaveHeight / max(uWaveHeight, 0.25) * 0.5 + 0.25);
        Color += CrestScattering(ViewDirection, uSunDirection, Normal, CrestRatio, 0.85) * mix(0.35, 1.0, Saturate(vDepth / 3.0));
    }

    // --- Foam shading ---
    if (FoamMask > 0.002)
    {
        vec3 FoamAlbedo = vec3(0.93, 0.97, 1.0);
        vec3 FoamLighting = AmbientSky * 0.65 + SunColor * Saturate(dot(Normal, uSunDirection)) * 0.45;
        vec3 FoamColor = FoamAlbedo * FoamLighting + SunColor * 0.06;
        Color = mix(Color, FoamColor, Saturate(FoamMask));
    }

    // --- Aerial perspective into the horizon haze ---
    vec3 HazeColor = SkyRadiance(normalize(vec3(ViewDirection.x, 0.035, ViewDirection.z)), 3);
    float Haze = 1.0 - exp(-vViewDistance * 0.00115);
    Color = mix(Color, HazeColor, Saturate(Haze));

    if (uFieldViewMode == 1)
    {
        Color = vec3(Saturate(vWaveHeight * 0.45 + 0.5));
    }
    else if (uFieldViewMode == 2)
    {
        Color = vec3(FoamMask * 0.9, FoamMask * 0.95, FoamMask);
    }
    else if (uFieldViewMode == 3)
    {
        Color = Normal * 0.5 + 0.5;
    }
    else if (uFieldViewMode == 4)
    {
        Color = vec3(Saturate(vDepth / 12.0));
    }

    FragColor = vec4(Color, 1.0);
}
