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
    float ExposureUnused;          // [-]   inert hole — the resolve owns exposure. Never read here (see the 🔴 note below)
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

// 🔴 THERE IS NO TONEMAP IN THIS SHADER, AND NONE MAY BE ADDED. The dome writes LINEAR SKY RADIANCE, unbounded above 1.0, into
//    the linear HDR radiance target; `RadianceResolve.frag` is the single site that applies exposure and the tone curve.
//    The ACES-ish curve that used to live here was removed in the P5.9b completion pass. It was doing two kinds of damage:
//      • DOUBLE MAP — it ran against the radiance target, which the resolve then mapped again, and its curve DIFFERED from the
//        shade pass's Khronos PBR Neutral, so sky and geometry rolled off differently and could never match at the horizon.
//        (This file's own comment claimed that defect was already "fixed in P5.9b" while the curve was still executing.)
//      • HDR CLIPPED AT SOURCE — it ended in clamp(Mapped, 0.0, 1.0), so every sky value above 1.0 was destroyed BEFORE reaching
//        the float target. The sun disc and bright horizon carry radiance far above 1.0; clamping here threw away exactly the
//        headroom the HDR target exists to hold, and no downstream exposure could recover it.
// ⚠️ Exposure is applied at the resolve, not here. Push.ExposureUnused is an inert hole kept for layout compatibility and must stay
//    unread in this shader — applying it twice would scale the sky against the geometry.
// ⚠️ Removing the curve also removed the 8x multiply that used to precede it, which left the sky measurably dark against every
//    earlier image. That calibration was NOT restored here; it belongs in the profile's SolarIlluminance, which scales the baked
//    sky-view LUT and this disc together. Re-adding a multiply in this shader would brighten the disc away from the sky behind it.

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

    // Linear sky radiance out, unbounded above 1.0 (the sun disc is far brighter than 1.0 and must stay that way). The resolve owns
    // exposure and the tone curve. See the 🔴 note where the curve used to live.
    OutColour = vec4(SkyRadiance, 1.0);
}
