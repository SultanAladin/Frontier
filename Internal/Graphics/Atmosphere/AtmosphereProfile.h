/*==============================================================================================================================================
                                                            ATMOSPHEREPROFILE.H
==============================================================================================================================================*/
// 🧩 Physical Bruneton-2008 / Hillaire-2020 atmosphere profile, std140 uniform mirror, Khronos PBR Neutral resolve mirror, piecewise sRGB
//    transfer, and Owen-scrambled (0,2) Sobol sequence shared by the Vulkan sky pipeline and the standalone C++ radiometric solvers.

#pragma once
#ifndef FRONTIER_GRAPHICS_ATMOSPHERE_ATMOSPHEREPROFILE_H
#define FRONTIER_GRAPHICS_ATMOSPHERE_ATMOSPHEREPROFILE_H

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "EngineContext/Math/LinearAlgebra_Float32.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

namespace Atmosphere
{
    constexpr float PiConstant       = 3.14159265358979323846f; // [rad] - Half-turn constant π
    constexpr float TauConstant      = 6.28318530717958647692f; // [rad] - Full-turn constant 2π
    constexpr float DegreesToRadians = 0.01745329251994329577f; // [rad/°] - Angular degree scale
    constexpr float RadiansToDegrees = 57.2957795130823208768f; // [°/rad] - Inverse degree scale

    constexpr uint32_t TransmittanceExtentX = 256u; // [px]  - Transmittance LUT width (d mapping)
    constexpr uint32_t TransmittanceExtentY = 64u;  // [px]  - Transmittance LUT height (ρ mapping)
    constexpr uint32_t MultiScatterExtentX  = 32u;  // [px]  - Multi-scatter LUT width (μ_s)
    constexpr uint32_t MultiScatterExtentY  = 32u;  // [px]  - Multi-scatter LUT height (altitude)
    constexpr uint32_t SkyViewExtentX       = 256u; // [px]  - Sky-view LUT width (azimuth 0..2π)
    constexpr uint32_t SkyViewExtentY       = 128u; // [px]  - Sky-view LUT height (limb-centered)

    // 📝 Absolute solar flux calibration paired with RadianceResolve.frag (Khronos PBR Neutral + hardware sRGB OETF).
    //    Transmittance and multi-scatter LUTs remain dimensionless ratios per unit solar flux; scaling here moves sky,
    //    clouds, orbital limb, and solar disc in exact radiometric lockstep.
    constexpr float SolarIlluminanceCalibration = 12.8f;               // [-]   - Extraterrestrial relative solar flux
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Unbounded linear Rec.709 tristimulus radiance / reflectance triplet used across atmosphere, cloud, and celestial solvers.
struct LinearColour
{
    float Red   = 0.0f; // [-] - Linear Rec.709 red channel (650 nm reference)
    float Green = 0.0f; // [-] - Linear Rec.709 green channel (550 nm reference)
    float Blue  = 0.0f; // [-] - Linear Rec.709 blue channel (450 nm reference)
};

// 📝 Exact 208-byte std140 GPU mirror of GLSL `AtmosphereBlock`. Unused `.w` lanes carry physical planetary surface reflectance
//    (cool oceanic Earth default — eliminating lower-hemisphere brown/beige bias), viewer altitude above BottomRadius [km]
//    (supporting seamless sea-level through deep-orbital space rendering), aerosol flow coupling, and sequence elapsed seconds.
//    For pure linear density layers (ExponentialScale == 0, such as the stratospheric Chappuis ozone tent), Layer.y stores the
//    signed constant offset (-2/3 below 25 km, +8/3 above 25 km) so ozone rises over 10..25 km and falls cleanly over 25..40 km.
struct AtmosphereUniformBlock
{
    float RayleighScatteringSeaLevel[4]; // [1/m] - rgb: Rayleigh scattering β_R; w: planetary surface reflectance R
    float MieScatteringSeaLevel[4];      // [1/m] - rgb: Mie scattering β_M,s;    w: planetary surface reflectance G
    float MieExtinctionSeaLevel[4];      // [1/m] - rgb: Mie extinction β_M,e;    w: planetary surface reflectance B
    float OzoneAbsorptionPeak[4];        // [1/m] - rgb: Chappuis ozone β_O3,a;   w: viewer altitude above surface [km]

    float RayleighProfileLayer0[4]; // [-]   - (WidthKm, ScaleHeightKmOrConst, ExpScale, LinearSlopePerKm)
    float RayleighProfileLayer1[4]; // [-]   - Upper Rayleigh profile layer
    float MieProfileLayer0[4];      // [-]   - Lower Mie aerosol profile layer
    float MieProfileLayer1[4];      // [-]   - Upper Mie aerosol profile layer
    float OzoneProfileLayer0[4];    // [-]   - Stratospheric ozone lower ramp (10..25 km: slope +1/15, offset -2/3)
    float OzoneProfileLayer1[4];    // [-]   - Stratospheric ozone upper ramp (25..40 km: slope -1/15, offset +8/3)

    float SolarDirection[4];   // [-]   - rgb: unit atmosphere-local (+Y up) sun vector; w: flow-veil gain
    float SolarIlluminance[4]; // [-]   - rgb: extraterrestrial solar flux; w: sequence elapsed seconds [s]

    float BottomRadius;             // [km]  - Planetary solid/ocean radius (Earth 6360 km)
    float TopRadius;                // [km]  - Atmosphere outer Kármán boundary (Earth 6460 km)
    float MieAsymmetry;             // [-]   - Cornette-Shanks forward-scatter parameter g in [-0.99, +0.99]
    float MultipleScatteringFactor; // [-]   - Hillaire infinite-bounce Ψ_ms contribution scale [0..2]
};

static_assert(sizeof(AtmosphereUniformBlock) == 208u, "AtmosphereUniformBlock must remain 208 bytes for std140 parity.");

//------------------------------------------------------------------------------------------------------------------------
//                                                  COLOUR ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline LinearColour AddColour(const LinearColour& Left, const LinearColour& Right) noexcept
{
    return LinearColour{ Left.Red + Right.Red, Left.Green + Right.Green, Left.Blue + Right.Blue };
}

[[nodiscard]] inline LinearColour ScaleColour(const LinearColour& Source, float Factor) noexcept
{
    return LinearColour{ Source.Red * Factor, Source.Green * Factor, Source.Blue * Factor };
}

[[nodiscard]] inline LinearColour MultiplyColour(const LinearColour& Left, const LinearColour& Right) noexcept
{
    return LinearColour{ Left.Red * Right.Red, Left.Green * Right.Green, Left.Blue * Right.Blue };
}

[[nodiscard]] inline LinearColour InterpolateColour(const LinearColour& Start, const LinearColour& Finish, float Parameter) noexcept
{
    return LinearColour{
        Start.Red   + (Finish.Red   - Start.Red)   * Parameter,
        Start.Green + (Finish.Green - Start.Green) * Parameter,
        Start.Blue  + (Finish.Blue  - Start.Blue)  * Parameter };
}

[[nodiscard]] inline float EvaluateRelativeLuminance(const LinearColour& Source) noexcept
{
    return 0.2126f * Source.Red + 0.7152f * Source.Green + 0.0722f * Source.Blue;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      TONE MAPPING & DISPLAY ENCODING (RESOLVE PARITY)
//------------------------------------------------------------------------------------------------------------------------

// 📝 Exact C++ mirror of Internal/Graphics/Render/Radiance/Shaders/RadianceResolve.frag::TonemapPbrNeutral (Khronos PBR Neutral).
[[nodiscard]] inline LinearColour EvaluatePbrNeutralTonemap(const LinearColour& InputRadiance, float ExposureScale = 1.0f) noexcept
{
    constexpr float CompressionStart = 0.8f - 0.04f;
    constexpr float DesaturationRate = 0.15f;

    const float Gain  = std::max(0.0f, ExposureScale);
    float       Red   = std::max(0.0f, InputRadiance.Red   * Gain);
    float       Green = std::max(0.0f, InputRadiance.Green * Gain);
    float       Blue  = std::max(0.0f, InputRadiance.Blue  * Gain);

    const float MinimumChannel = std::min(Red, std::min(Green, Blue));
    const float ToeOffset      = (MinimumChannel < 0.08f)
                               ? (MinimumChannel - 6.25f * MinimumChannel * MinimumChannel)
                               : 0.04f;
    Red   -= ToeOffset;
    Green -= ToeOffset;
    Blue  -= ToeOffset;

    const float Peak = std::max(Red, std::max(Green, Blue));
    if (Peak < CompressionStart)
    {
        return LinearColour{ std::max(0.0f, Red), std::max(0.0f, Green), std::max(0.0f, Blue) };
    }

    const float Headroom       = 1.0f - CompressionStart;
    const float CompressedPeak = 1.0f - (Headroom * Headroom) / (Peak + Headroom - CompressionStart);
    const float Ratio          = CompressedPeak / std::max(Peak, 1.0e-8f);
    Red   *= Ratio;
    Green *= Ratio;
    Blue  *= Ratio;

    const float DesatWeight = 1.0f - 1.0f / (DesaturationRate * (Peak - CompressedPeak) + 1.0f);
    return LinearColour{
        Red   + (CompressedPeak - Red)   * DesatWeight,
        Green + (CompressedPeak - Green) * DesatWeight,
        Blue  + (CompressedPeak - Blue)  * DesatWeight };
}

// 📝 Exact IEC 61966-2-1 piecewise sRGB OETF mirroring hardware VK_FORMAT_*_SRGB presentation output.
[[nodiscard]] inline float EncodeLinearChannelToSrgb(float LinearChannel) noexcept
{
    const float Clamped = std::clamp(LinearChannel, 0.0f, 1.0f);
    return (Clamped <= 0.0031308f)
         ? (12.92f * Clamped)
         : (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
}

[[nodiscard]] inline uint8_t EncodeLinearToSrgbByte(float LinearChannel, float DitherOffset = 0.0f) noexcept
{
    const float Encoded = EncodeLinearChannelToSrgb(LinearChannel) * 255.0f + DitherOffset;
    return static_cast<uint8_t>(std::clamp(std::floor(Encoded + 0.5f), 0.0f, 255.0f));
}

[[nodiscard]] inline float DecodeSrgbByteToLinear(uint8_t EncodedByte) noexcept
{
    const float Normalized = static_cast<float>(EncodedByte) * (1.0f / 255.0f);
    return (Normalized <= 0.04045f)
         ? (Normalized / 12.92f)
         : std::pow((Normalized + 0.055f) / 1.055f, 2.4f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                        OWEN-SCRAMBLED (0, 2) SOBOL QUADRATURE
//------------------------------------------------------------------------------------------------------------------------

namespace Atmosphere
{
    [[nodiscard]] inline uint32_t ReverseBits32(uint32_t Word) noexcept
    {
        Word = ((Word & 0x55555555u) << 1u)  | ((Word & 0xAAAAAAAAu) >> 1u);
        Word = ((Word & 0x33333333u) << 2u)  | ((Word & 0xCCCCCCCCu) >> 2u);
        Word = ((Word & 0x0F0F0F0Fu) << 4u)  | ((Word & 0xF0F0F0F0u) >> 4u);
        Word = ((Word & 0x00FF00FFu) << 8u)  | ((Word & 0xFF00FF00u) >> 8u);
        return (Word << 16u) | (Word >> 16u);
    }

    // 📝 Laine-Karras (2011) nested uniform Owen permutation in reverse-bit domain (preserves (0,2)-net stratification).
    [[nodiscard]] inline uint32_t PermuteLaineKarras(uint32_t Word, uint32_t Seed) noexcept
    {
        Word += Seed;
        Word ^= Word * 0x6C50B47Cu;
        Word ^= Word * 0xB82F1E52u;
        Word ^= Word * 0xC7AFE638u;
        Word ^= Word * 0x8D22F6E6u;
        return Word;
    }

    [[nodiscard]] inline uint32_t ScrambleOwenWord(uint32_t Unscrambled, uint32_t Seed) noexcept
    {
        return ReverseBits32(PermuteLaineKarras(ReverseBits32(Unscrambled), Seed));
    }

    [[nodiscard]] inline uint32_t HashPixelSeed(uint32_t PixelX, uint32_t PixelY, uint32_t SequenceOrdinal) noexcept
    {
        uint32_t Mixed = PixelX * 0x9E3779B1u ^ PixelY * 0x85EBCA77u ^ SequenceOrdinal * 0xC2B2AE3Du;
        Mixed ^= Mixed >> 16u;
        Mixed *= 0x7FEB352Du;
        Mixed ^= Mixed >> 15u;
        Mixed *= 0x846CA68Bu;
        Mixed ^= Mixed >> 16u;
        return Mixed;
    }

    // 📝 Evaluates stratified 2D Owen-scrambled (0,2) Sobol sample in [0, 1)^2 — eliminates screen-correlated IGN diagonal striping.
    inline void EvaluateOwenSobolPair(
        uint32_t SampleIndex,
        uint32_t PixelSeed,
        float&   OutFirst,
        float&   OutSecond) noexcept
    {
        const uint32_t DimZero   = ReverseBits32(SampleIndex);
        uint32_t       DimOne    = 0u;
        uint32_t       Generator = 0x80000000u;
        uint32_t       Cursor    = SampleIndex;
        while (Cursor != 0u)
        {
            if ((Cursor & 1u) != 0u)
                DimOne ^= Generator;
            Cursor    >>= 1u;
            Generator ^= (Generator >> 1u);
        }

        const uint32_t ScrambledZero = ScrambleOwenWord(DimZero, PixelSeed ^ 0xA511E9B3u);
        const uint32_t ScrambledOne  = ScrambleOwenWord(DimOne,  PixelSeed ^ 0x63D83595u);
        OutFirst  = static_cast<float>(ScrambledZero >> 8u) * (1.0f / 16777216.0f);
        OutSecond = static_cast<float>(ScrambledOne  >> 8u) * (1.0f / 16777216.0f);
    }

//------------------------------------------------------------------------------------------------------------------------
//                                    DOMAIN-ROTATED 3D LATTICE NOISE (SHARED BY ALL SOLVERS)
//------------------------------------------------------------------------------------------------------------------------

    // 📝 Integer lattice hash in [0, 1), quintic C2 value noise, and orthonormally domain-rotated fBm shared by the planet,
    //    sky-veil, cloud, Milky Way, and lunar procedural-fallback evaluators (single definition, no per-solver duplicates).
    [[nodiscard]] inline float HashLatticeCell(int32_t CellX, int32_t CellY, int32_t CellZ, uint32_t Salt) noexcept
    {
        uint32_t Mixed = static_cast<uint32_t>(CellX) * 0x9E3779B1u
                       ^ static_cast<uint32_t>(CellY) * 0x85EBCA77u
                       ^ static_cast<uint32_t>(CellZ) * 0xC2B2AE3Du
                       ^ Salt * 0x27D4EB2Fu;
        Mixed ^= Mixed >> 16u;
        Mixed *= 0x7FEB352Du;
        Mixed ^= Mixed >> 15u;
        Mixed *= 0x846CA68Bu;
        Mixed ^= Mixed >> 16u;
        return static_cast<float>(Mixed >> 8u) * (1.0f / 16777216.0f);
    }

    [[nodiscard]] inline float EvaluateQuinticNoise3D(const Vector3f& Coord, uint32_t Salt) noexcept
    {
        const float   Fx = std::floor(Coord.XCoord);
        const float   Fy = std::floor(Coord.YCoord);
        const float   Fz = std::floor(Coord.ZCoord);
        const int32_t Ix = static_cast<int32_t>(Fx);
        const int32_t Iy = static_cast<int32_t>(Fy);
        const int32_t Iz = static_cast<int32_t>(Fz);

        const auto  Quintic = [](float T) noexcept -> float { return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f); };
        const float Ux      = Quintic(Coord.XCoord - Fx);
        const float Uy      = Quintic(Coord.YCoord - Fy);
        const float Uz      = Quintic(Coord.ZCoord - Fz);
        const auto  Mix     = [](float A, float B, float T) noexcept -> float { return A + (B - A) * T; };

        const float C00 = Mix(HashLatticeCell(Ix, Iy,     Iz,     Salt), HashLatticeCell(Ix + 1, Iy,     Iz,     Salt), Ux);
        const float C10 = Mix(HashLatticeCell(Ix, Iy + 1, Iz,     Salt), HashLatticeCell(Ix + 1, Iy + 1, Iz,     Salt), Ux);
        const float C01 = Mix(HashLatticeCell(Ix, Iy,     Iz + 1, Salt), HashLatticeCell(Ix + 1, Iy,     Iz + 1, Salt), Ux);
        const float C11 = Mix(HashLatticeCell(Ix, Iy + 1, Iz + 1, Salt), HashLatticeCell(Ix + 1, Iy + 1, Iz + 1, Salt), Ux);
        return Mix(Mix(C00, C10, Uy), Mix(C01, C11, Uy), Uz);
    }

    [[nodiscard]] inline float EvaluateRotatedFbm3D(Vector3f Coord, uint32_t OctaveCount, uint32_t Salt) noexcept
    {
        float Total        = 0.0f;
        float Amplitude    = 0.5f;
        float AmplitudeSum = 0.0f;
        for (uint32_t Octave = 0u; Octave < OctaveCount; ++Octave)
        {
            Total        += Amplitude * EvaluateQuinticNoise3D(Coord, Salt + Octave * 37u);
            AmplitudeSum += Amplitude;

            // Orthonormal rotation matrix eliminates axis-aligned grid artifacts across the unit sphere.
            const Vector3f Rotated{
                 0.80f * Coord.XCoord - 0.48f * Coord.YCoord + 0.36f * Coord.ZCoord,
                 0.48f * Coord.XCoord + 0.86f * Coord.YCoord + 0.16f * Coord.ZCoord,
                -0.36f * Coord.XCoord + 0.04f * Coord.YCoord + 0.93f * Coord.ZCoord };
            Coord     = AddVector(ScaleVector(Rotated, 2.03f), Vector3f{ 13.7f, -7.1f, 5.3f });
            Amplitude *= 0.5f;
        }
        return (AmplitudeSum > 0.0f) ? (Total / AmplitudeSum) : 0.0f;
    }

//------------------------------------------------------------------------------------------------------------------------
//                                              EARTH PROFILE CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

    inline void EncodeFloat4(float Target[4], float First, float Second, float Third, float Fourth) noexcept
    {
        Target[0] = First;
        Target[1] = Second;
        Target[2] = Third;
        Target[3] = Fourth;
    }

    // 📝 Construct calibrated Earth atmosphere (Bruneton 2008 + Hillaire 2020 exact Chappuis ozone tent 10..25..40 km).
    //    Lower-hemisphere planetary reflectance defaults to clean oceanic Rayleigh slate (0.045, 0.088, 0.165) — zero brown/beige.
    [[nodiscard]] inline AtmosphereUniformBlock ConstructEarthProfile() noexcept
    {
        AtmosphereUniformBlock Profile{};

        EncodeFloat4(Profile.RayleighScatteringSeaLevel, 5.802e-6f, 13.558e-6f, 33.100e-6f, 0.004f);
        EncodeFloat4(Profile.MieScatteringSeaLevel,      3.996e-6f,  3.996e-6f,  3.996e-6f, 0.012f);
        EncodeFloat4(Profile.MieExtinctionSeaLevel,      4.440e-6f,  4.440e-6f,  4.440e-6f, 0.032f);
        EncodeFloat4(Profile.OzoneAbsorptionPeak,    0.650e-6f,  1.881e-6f,  0.085e-6f, 0.200f);

        // ① Rayleigh molecular scattering: 8.0 km exponential scale height.
        EncodeFloat4(Profile.RayleighProfileLayer0, 0.0f, 8.0f, 1.0f, 0.0f);
        EncodeFloat4(Profile.RayleighProfileLayer1, 0.0f, 8.0f, 0.0f, 0.0f);

        // ② Mie continental-maritime aerosol scattering: 1.2 km exponential scale height.
        EncodeFloat4(Profile.MieProfileLayer0,      0.0f, 1.2f, 1.0f, 0.0f);
        EncodeFloat4(Profile.MieProfileLayer1,      0.0f, 1.2f, 0.0f, 0.0f);

        // ③ Chappuis stratospheric ozone tent peaking at 25 km with 15 km half-width (active 10..40 km, ∫ρ dh = 15 km).
        EncodeFloat4(Profile.OzoneProfileLayer0, 25.0f, -2.0f / 3.0f, 0.0f,  1.0f / 15.0f);
        EncodeFloat4(Profile.OzoneProfileLayer1,  0.0f,  8.0f / 3.0f, 0.0f, -1.0f / 15.0f);

        EncodeFloat4(Profile.SolarDirection, 0.70710678f, 0.70710678f, 0.0f, 1.0f);
        EncodeFloat4(Profile.SolarIlluminance,
                     SolarIlluminanceCalibration,
                     SolarIlluminanceCalibration,
                     SolarIlluminanceCalibration,
                     0.0f);

        Profile.BottomRadius             = 6360.0f;
        Profile.TopRadius                = 6460.0f;
        Profile.MieAsymmetry             = 0.80f;
        Profile.MultipleScatteringFactor = 1.0f;

        return Profile;
    }

    // 📝 Assign unit solar vector in atmosphere-local (+Y up) coordinates from elevation [-π/2..+π/2] and azimuth [0..2π).
    inline void ConfigureSolarDirection(
        AtmosphereUniformBlock& Profile,
        float                   ElevationRadians,
        float                   AzimuthRadians) noexcept
    {
        const float CosineElevation = std::cos(ElevationRadians);
        const float SineElevation   = std::sin(ElevationRadians);
        Profile.SolarDirection[0] = CosineElevation * std::cos(AzimuthRadians);
        Profile.SolarDirection[1] = SineElevation;
        Profile.SolarDirection[2] = CosineElevation * std::sin(AzimuthRadians);
    }

    inline void ConfigureGroundReflectance(
        AtmosphereUniformBlock& Profile,
        const LinearColour&     Reflectance) noexcept
    {
        Profile.RayleighScatteringSeaLevel[3] = std::clamp(Reflectance.Red,   0.0f, 1.0f);
        Profile.MieScatteringSeaLevel[3]      = std::clamp(Reflectance.Green, 0.0f, 1.0f);
        Profile.MieExtinctionSeaLevel[3]      = std::clamp(Reflectance.Blue,  0.0f, 1.0f);
    }

    [[nodiscard]] inline LinearColour RetrieveGroundReflectance(const AtmosphereUniformBlock& Profile) noexcept
    {
        return LinearColour{
            std::clamp(Profile.RayleighScatteringSeaLevel[3], 0.0f, 1.0f),
            std::clamp(Profile.MieScatteringSeaLevel[3],      0.0f, 1.0f),
            std::clamp(Profile.MieExtinctionSeaLevel[3],      0.0f, 1.0f) };
    }

    inline void ConfigureViewerAltitudeKm(AtmosphereUniformBlock& Profile, float AltitudeKm) noexcept
    {
        Profile.OzoneAbsorptionPeak[3] = std::max(0.005f, AltitudeKm);
    }

    [[nodiscard]] inline float RetrieveViewerAltitudeKm(const AtmosphereUniformBlock& Profile) noexcept
    {
        const float Stored = Profile.OzoneAbsorptionPeak[3];
        return (Stored > 0.0f) ? Stored : 0.200f;
    }

    inline void ConfigureSequenceSeconds(
        AtmosphereUniformBlock& Profile,
        float                   SequenceSeconds,
        float                   FlowCoupling = 1.0f) noexcept
    {
        Profile.SolarIlluminance[3] = SequenceSeconds;
        Profile.SolarDirection[3]   = std::max(0.0f, FlowCoupling);
    }

    // 📝 Convert stored atmosphere-local (+Y up) solar vector into scene world coordinates (+Z up).
    inline void ResolveSolarDirectionWorldSpace(
        const AtmosphereUniformBlock& Profile,
        float&                        OutX,
        float&                        OutY,
        float&                        OutZ) noexcept
    {
        OutX = Profile.SolarDirection[0];
        OutY = Profile.SolarDirection[2];
        OutZ = Profile.SolarDirection[1];
    }

    // 📝 Radiant-sun glare. A muted sun (thin haze, the eye adapted to the disc) shows the photosphere plus its faint corona and
    //    aureole only. A radiant sun (clean air, the eye adapted to the sky) floods the optics: forward-scattered light inside the
    //    eye or lens spreads a bright core bloom plus a broad warm fade around the disc. Shape:
    //      G(θ) = CoreWeight / (θ_e + 0.02°)² + AureoleWeight / (1 + (θ_e / 6°)²),   θ_e = √(θ² + r_disc²)
    //    (the inverse-square core is the dominant Spencer, Shirley, Zimmerman & Greenberg 1995 photopic PSF wing; the Lorentzian is
    //    the clean-air small-angle aerosol aureole), windowed by (1 - θ / 30°)² so it vanishes without a step. G is radiance per
    //    unit solar illuminance: ~1.6 at 1°, ~0.36 at 3° (≈ 4× blue-sky radiance), ~0.08 at 10° (≈ the sky itself). It is independent of the disc
    //    intensity so the bloom stays the same width whatever brightness the disc is authored at. Strength 0 = muted, 1 = radiant.
    constexpr float SolarGlareReachRadians   = 30.0f * DegreesToRadians; // [rad] - Window edge of the glare halo
    constexpr float SolarGlareBloomWeight     = 1.50f;                    // [°²]  - Inverse-square core weight
    constexpr float SolarGlareAureoleWeight  = 0.25f;                    // [-]   - Lorentzian aureole weight
    constexpr float SolarGlareAureoleDegrees = 6.0f;                     // [°]   - Lorentzian aureole half-width

    [[nodiscard]] inline float EvaluateSolarGlareSpread(float SeparationRadians, float DiscRadiusRadians) noexcept
    {
        if (SeparationRadians >= SolarGlareReachRadians)
            return 0.0f;
        const float SeparationDegrees = SeparationRadians * RadiansToDegrees;
        const float DiscDegrees       = DiscRadiusRadians * RadiansToDegrees;
        const float Effective         = std::sqrt(SeparationDegrees * SeparationDegrees + DiscDegrees * DiscDegrees);
        const float Window            = 1.0f - SeparationRadians / SolarGlareReachRadians;
        const float Bloom             = SolarGlareBloomWeight / ((Effective + 0.02f) * (Effective + 0.02f));
        const float AureoleRatio      = Effective / SolarGlareAureoleDegrees;
        const float Aureole           = SolarGlareAureoleWeight / (1.0f + AureoleRatio * AureoleRatio);
        return (Bloom + Aureole) * Window * Window;
    }
}

} // namespace Frontier

#endif
