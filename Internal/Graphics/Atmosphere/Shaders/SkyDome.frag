#version 450 core

// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//                                                     SKY DOME (per-frame) 🧩
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// The per-frame sky background. Reconstructs the world view ray from the camera's inverse view-projection (the same InverseViewProjection the
// ground grid derives from), looks up pre-baked sky-view radiance for that direction, draws the analytic sun disc through the transmittance LUT,
// then exposure-tonemaps to display. Records first in the frame so the grid and (later) geometry draw over it. Fills the whole framebuffer.

#include "AtmosphereCommon.glsl"
#include "TransmittanceLookup.glsl"

layout(binding = 4) uniform sampler2D SkyViewLUT;   // 192×108 RGBA16F baked radiance

layout(push_constant) uniform SkyPush
{
    mat4  InverseViewProjection;   // clip → world
    vec4  CameraPosition;          // world eye xyz; w unused
    float SunAngularRadius;        // [rad] half-angle of the solar disc
    float Exposure;                // [-]   linear exposure multiplier before tonemap
    float SunIntensity;            // [-]   brightness of the disc itself
    float DomeEnabled;             // 1 draw sky, 0 leave cleared
} Push;

layout(location = 0) in  vec2 InUv;
layout(location = 0) out vec4 OutColour;

// 📝 sky-view LUT sampling mirror of SkyView.frag's SkyViewUvToDirection (direction → uv).
vec2 DirectionToSkyViewUv(vec3 Direction)
{
    float ViewZenithCos = clamp(Direction.y, -1.0, 1.0);
    float Zenith  = acos(ViewZenithCos);
    float Azimuth = atan(Direction.z, Direction.x);

    float U = Azimuth / (2.0 * PI) + 0.5;

    float V;
    if (Zenith > (PI * 0.5))
    {
        float T = sqrt((Zenith - PI * 0.5) / (PI * 0.5));
        V = 0.5 * (1.0 - T);
    }
    else
    {
        float T = sqrt((PI * 0.5 - Zenith) / (PI * 0.5));
        V = 0.5 * (1.0 + T);
    }
    return vec2(U, V);
}

vec3 Tonemap(vec3 Colour)
{
    // ACES-ish filmic curve, then gamma to sRGB.
    Colour *= Push.Exposure;
    vec3 Mapped = (Colour * (2.51 * Colour + 0.03)) / (Colour * (2.43 * Colour + 0.59) + 0.14);
    Mapped = clamp(Mapped, 0.0, 1.0);
    return pow(Mapped, vec3(1.0 / 2.2));
}

void main()
{
    if (Push.DomeEnabled < 0.5)
        discard;

    // Reconstruct the world-space view ray. 🔴 Precision: unproject ONLY the near clip point (its homogeneous w is ~1, so the
    // divide keeps full float precision) and take the direction from the EXACT eye — never subtract two far/near unprojections.
    // The far-clip unproject FarClip.xyz/FarClip.w divides two near-equal large floats (FarClip.w scales with the far plane),
    // so the quotient keeps only a few significant bits and SNAPS between representable values as the camera rotates — a
    // per-frame /\/\ shimmer even though the camera moves perfectly smoothly (proven by the input + camera trace). The grid
    // shared this exact reconstruction and shimmered identically; both are fixed the same way (see GroundGrid.vert).
    vec2 Ndc = InUv * 2.0 - 1.0;
    vec4 NearClip  = Push.InverseViewProjection * vec4(Ndc, 0.0, 1.0);
    vec3 NearWorld = NearClip.xyz / NearClip.w;
    vec3 ViewDir   = normalize(NearWorld - Push.CameraPosition.xyz);

    // World is Z-up; atmosphere-local up is +Y. Remap (x, y, z_world) → (x, z_world, y) so z_up becomes local +Y.
    vec3 SkyDir = normalize(vec3(ViewDir.x, ViewDir.z, ViewDir.y));

    vec3 SunDir = normalize(Atmosphere.SolarDirection.xyz);
    vec3 SkyRadiance = texture(SkyViewLUT, DirectionToSkyViewUv(SkyDir)).rgb;

    // Analytic sun disc: inside the angular radius, add the direct solar radiance attenuated by transmittance-to-space.
    float CosToSun = dot(SkyDir, SunDir);
    float CosDiscEdge = cos(Push.SunAngularRadius);
    if (CosToSun > CosDiscEdge)
    {
        float StartRadius = Atmosphere.BottomRadius + 0.5;
        vec3  SunTransmit = SampleTransmittanceToSpace(StartRadius, SunDir.y);
        float Limb = smoothstep(CosDiscEdge, mix(CosDiscEdge, 1.0, 0.5), CosToSun);
        SkyRadiance += SunTransmit * Push.SunIntensity * Limb * Atmosphere.SolarIlluminance.rgb;
    }

    OutColour = vec4(Tonemap(SkyRadiance), 1.0);
}
