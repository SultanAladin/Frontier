/*==============================================================================================================================================
                                                            ATMOSPHEREPROFILE.H
==============================================================================================================================================*/
// 🧩 The physical atmosphere description shared by the CPU pass and every sky shader. Holds the Rayleigh / Mie / ozone scattering bases, the
//    two-layer density profiles, the planetary radii, and the current solar vector — all in one std140-packed block (AtmosphereUniformBlock)
//    that mirrors the GLSL `AtmosphereBlock` uniform byte-for-byte. Earth-like defaults come straight from the Bruneton 2008 constants
//    (validated against the archived reference). Nothing here talks to Vulkan; it is pure data the pass uploads once and re-uploads only when
//    the sun moves. Every vec4 rides as a plain float[4] (matching GroundGridConstants), so the std140 mirror needs no external vector type.

#pragma once
#ifndef FRONTIER_GRAPHICS_ATMOSPHERE_ATMOSPHEREPROFILE_H
#define FRONTIER_GRAPHICS_ATMOSPHERE_ATMOSPHEREPROFILE_H

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The std140 GPU mirror. This is what lands in the uniform buffer and what every sky shader declares as `AtmosphereBlock`. All vec3s are
//    promoted to vec4 (rgb + pad) and each density layer rides as a vec4 (Width, ExponentialTerm, ExponentialScale, LinearTerm), so the C++
//    layout matches std140 with no hidden padding. 16 vec4s (256 bytes) + 4 trailing floats (16 bytes) = 272 bytes. SolarDirection is the
//    unit vector FROM the surface TOWARD the sun. Density at altitude h (below Width) is exp(-h/ExponentialTerm)*ExponentialScale + LinearTerm*h.
struct AtmosphereUniformBlock
{
    float RayleighScatteringBase[4];   // [1/m] rgb: Rayleigh scattering coefficient; a: pad
    float MieScatteringBase[4];        // [1/m] rgb: Mie scattering coefficient; a: pad
    float MieExtinctionBase[4];        // [1/m] rgb: Mie extinction (scatter + absorb); a: pad
    float OzoneAbsorptionBase[4];      // [1/m] rgb: Ozone absorption coefficient; a: pad

    float RayleighProfileLayer0[4];    // (Width, ExponentialTerm, ExponentialScale, LinearTerm)
    float RayleighProfileLayer1[4];    // second Rayleigh layer (earth default: inactive scale)
    float MieProfileLayer0[4];         // Mie layer 0
    float MieProfileLayer1[4];         // Mie layer 1
    float OzoneProfileLayer0[4];       // ozone tent up-slope layer
    float OzoneProfileLayer1[4];       // ozone tent down-slope layer

    float SolarDirection[4];           // [-]  rgb: unit surface→sun vector; a: pad
    float SolarIlluminance[4];         // [-]  rgb: extraterrestrial solar flux (relative); a: pad

    float BottomRadius;                // [km] planetary surface radius (Earth 6360)
    float TopRadius;                   // [km] atmosphere outer boundary (Earth 6460)
    float MieAsymmetry;                // [-1..1] Cornette-Shanks forward-scatter g
    float MultipleScatteringFactor;    // [0..1] isotropic multi-scatter intensity
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    EARTH-LIKE DEFAULTS (Bruneton 2008)
//------------------------------------------------------------------------------------------------------------------------

namespace Atmosphere
{
    inline void AssignFloat4(float Target[4], float X, float Y, float Z, float W)
    {
        Target[0] = X; Target[1] = Y; Target[2] = Z; Target[3] = W;
    }

    // 📝 The relative solar flux that puts the sky at the tone curve's intended operating point. The scattering integral is evaluated
    //    PER UNIT illuminance — the transmittance and multi-scatter tables are dimensionless ratios — so this single factor is the
    //    renderer's absolute-radiance calibration for the sky, applied at exactly two sites: SkyView.frag's final multiply (the LUT
    //    bake) and SkyDome.frag's sun disc. Scaling here therefore moves the baked sky and the disc together and cannot desynchronize
    //    them, which a per-consumer multiplier could.
    //
    // 🔴 THE SKY'S CALIBRATION LIVES HERE BECAUSE THE RESOLVE'S EXPOSURE CANNOT CARRY IT. RadianceResolve's Exposure scales the WHOLE
    //    radiance target, so raising it to brighten the sky brightens every lit surface with it. The sky and the surface shade are on
    //    independent scales today (the shade's key light is its own hardcoded LightIntensity), so the sky needs its own knob.
    //
    // ⚠️ SOLVED AGAINST THE PREVIOUS IMAGE, NOT PICKED. Until P5.9b the dome applied `Exposure = 8.0` and its own ACES curve ending in
    //    clamp(0,1); removing that curve (correctly — it clipped the HDR at source and double-mapped) took the 8x with it and left the
    //    sky running ~0.47 RMS dark in display units, which read as a dimmed sky while object colours stayed correct. 8.0 is NOT the
    //    replacement: the multiply now lands ahead of Khronos PBR Neutral + the hardware sRGB OETF instead of ACES + pow(1/2.2), a
    //    different transform. Least-squares over the visible mid-range gives 12.35 (neutral) and 13.29 (Rayleigh-weighted blue);
    //    12.8 splits them at ~0.044 RMS, an order of magnitude closer than the uncalibrated value.
    constexpr float SolarIlluminanceCalibration = 12.8f;   // [-] - relative flux; see the solve above before retuning

    // 📝 Assemble the Earth-like default profile. Scattering bases are in 1/m at ~550 nm reference wavelengths; radii and
    //    scale heights are in km (the shaders raymarch in km-space). SolarDirection defaults to 45° elevation so a fresh
    //    boot shows a clear daytime sky with the sun above the horizon.
    inline AtmosphereUniformBlock ConstructEarthProfile()
    {
        AtmosphereUniformBlock Profile{};

        AssignFloat4(Profile.RayleighScatteringBase, 5.802e-6f, 13.558e-6f, 33.100e-6f, 0.0f);
        AssignFloat4(Profile.MieScatteringBase,      3.996e-6f, 3.996e-6f,  3.996e-6f,  0.0f);
        AssignFloat4(Profile.MieExtinctionBase,      4.440e-6f, 4.440e-6f,  4.440e-6f,  0.0f);
        AssignFloat4(Profile.OzoneAbsorptionBase,    0.650e-6f, 1.881e-6f,  0.085e-6f,  0.0f);

        // Rayleigh: single 8 km exponential layer. Mie: single 1.2 km exponential layer.
        AssignFloat4(Profile.RayleighProfileLayer0, 0.0f, 8.0f, 1.0f, 0.0f);
        AssignFloat4(Profile.RayleighProfileLayer1, 0.0f, 8.0f, 0.0f, 0.0f);
        AssignFloat4(Profile.MieProfileLayer0,      0.0f, 1.2f, 1.0f, 0.0f);
        AssignFloat4(Profile.MieProfileLayer1,      0.0f, 1.2f, 0.0f, 0.0f);

        // Ozone: symmetric tent centred at 25 km, 15 km half-width (linear up then linear down).
        AssignFloat4(Profile.OzoneProfileLayer0, 25.0f, 0.0f, 0.0f,  1.0f / 15.0f);
        AssignFloat4(Profile.OzoneProfileLayer1,  0.0f, 0.0f, 0.0f, -1.0f / 15.0f);

        AssignFloat4(Profile.SolarDirection,   0.70710678f, 0.70710678f, 0.0f, 0.0f); // 45° elevation
        AssignFloat4(Profile.SolarIlluminance, SolarIlluminanceCalibration, SolarIlluminanceCalibration, SolarIlluminanceCalibration, 0.0f);

        Profile.BottomRadius              = 6360.0f;
        Profile.TopRadius                 = 6460.0f;
        Profile.MieAsymmetry              = 0.8f;
        Profile.MultipleScatteringFactor  = 1.0f;

        return Profile;
    }

    // 📝 Rewrite SolarDirection from elevation + azimuth (both radians). Elevation 0 = horizon, +π/2 = zenith.
    //    Up is +Y in atmosphere-local space, so the sun rides the X/Z plane and lifts along Y.
    inline void AssignSolarDirection(AtmosphereUniformBlock& Profile, float ElevationRadians, float AzimuthRadians)
    {
        float CosElevation = std::cos(ElevationRadians);
        float SinElevation = std::sin(ElevationRadians);
        AssignFloat4(Profile.SolarDirection,
                     CosElevation * std::cos(AzimuthRadians),
                     SinElevation,
                     CosElevation * std::sin(AzimuthRadians),
                     0.0f);
    }

    // 📝 SolarDirection in the SCENE's frame: unit surface→sun with Z as up. Everything outside the atmosphere unit (the shadow clipmap's light basis,
    //    the deferred shade's key light) works in Z-up world space, so this is the one conversion point rather than a swizzle repeated per consumer.
    //
    // 🔴 THE AXES ARE NOT INTERCHANGEABLE AND THE MISTAKE IS SILENT. Atmosphere-local space is Y-up (SkyDome.frag remaps world (x,y,z) → (x,z,y) for
    //    exactly this reason), so the stored vector's .y is ELEVATION while a Z-up consumer reads .z as elevation. Feeding the raw vector to a Z-up
    //    consumer therefore puts the sun on the HORIZON at the default 45° profile — (0.707, 0.707, 0) has zero Z — while the sky keeps drawing it
    //    correctly at altitude. Nothing errors: shadows simply stretch to infinity from a sun the sky disagrees with, and the two never line up.
    //
    // ⚠️ The inverse of SkyDome.frag's remap, and deliberately written as its mirror: local (x, y_up, z) → world (x, z, y_up).
    inline void ResolveSolarDirectionSceneFrame(const AtmosphereUniformBlock& Profile,
                                                float&                        OutX,
                                                float&                        OutY,
                                                float&                        OutZ)
    {
        OutX = Profile.SolarDirection[0];
        OutY = Profile.SolarDirection[2];
        OutZ = Profile.SolarDirection[1];
    }
}

} // namespace Frontier

#endif
