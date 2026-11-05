/*==========================================================================================================================================
                                                            PHOTOMETRY
                                     ------------------------------------------------------------------
                    IESNA LM-63 Type C photometry for the clustered lights: parse the files,
                 reconstruct them at the atlas grid, and pack the atlas the shaders read.
==========================================================================================================================================*/

// 🧩 IESNA LM-63 Type C photometry: the portable (CPU) form of the demo's Photometry.js, 1:1.
//
// 📝 Provenance. The parser and the first Type C symmetry rules are the Frontier lighting rewrite's
//    (streamlinkinbox/Frontier, branch arena/734ca2e5-frontier, commit f2aa20a6). Four of the seven
//    shipped profiles (Roadway, Area, RoofFlood, RoofSpot) are REAL manufacturer / test-lab files,
//    verbatim, with their source, commit and licence in EngineContent/Photometry/PROVENANCE.json and
//    EngineContent/Photometry/README.md. The three automotive beams (LowBeam, HighBeam, FogBeam) are
//    still AUTHORED demonstration curves (their headers say so): no measured vehicle headlamp file
//    under a permissive licence turned up. Real files forced three extensions of the parser, all
//    LM-63 conventions: a 90°-270° grid (symmetric about the 90-270 plane), an asymmetric grid that
//    stops before 360° (periodic, the last plane blends back to the first), and a vertical range that
//    ends at 90° (a downlight: no light beyond its last measured angle).
//
// 📝 Convention. Type C: the polar angle is measured FROM the fixture's optical axis (0° on the axis,
//    180° straight behind), C0 is "up" around the axis and C90 is to its right. Values are
//    peak-normalised, and the light record's intensity and colour supply the scale, so a profile
//    reshapes a light without relighting it.
//
// 📝 Packing. 7 profiles × 37 azimuths (10°) × 91 polar angles (2°) = 23 569 texels, one byte each
//    (the strongest texel of each profile = 255), 23 584 bytes after padding to the vec4u the uniform
//    record needs: small enough to travel inside LightClusterRecords, with the lights and their cell
//    masks, in one binding.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

inline constexpr uint32_t PhotometryAzimuthCount = 37;   // [-] azimuth samples, 10° apart
inline constexpr uint32_t PhotometryPolarCount = 91;     // [-] polar samples, 2° apart
inline constexpr uint32_t PhotometryProfileCount = 7;    // [-] shipped profiles; id 0 is isotropic

// 📝 Profile ids: 1..7 in PhotometryProfileFiles order; 0 means "no profile" (isotropic).
enum class PhotometryProfileId : uint32_t
{
    Isotropic = 0,
    Roadway = 1,
    Area = 2,
    LowBeam = 3,
    HighBeam = 4,
    FogBeam = 5,
    RoofFlood = 6,
    RoofSpot = 7
};

inline constexpr std::array<std::string_view, PhotometryProfileCount> PhotometryProfileFiles = {{
    "Roadway", "Area", "LowBeam", "HighBeam", "FogBeam", "RoofFlood", "RoofSpot"
}};

inline constexpr uint32_t PhotometryTexelsPerProfile = PhotometryAzimuthCount * PhotometryPolarCount;   // 3367
inline constexpr uint32_t PhotometryTexelCount = PhotometryProfileCount * PhotometryTexelsPerProfile;  // 23569
inline constexpr uint32_t PhotometryVectorCount = (PhotometryTexelCount + 15u) / 16u;                  // vec4u in the record
inline constexpr uint32_t PhotometryBytes = PhotometryVectorCount * 16u;                               // 23584

// 📝 One parsed LM-63 photometric grid: the vertical (polar) angles, the horizontal (C-plane) angles,
//    the candela values (already multiplied by the file's multiplier) and the grid's peak.
struct IesPhotometricProfile
{
    std::vector<double> Vertical;    // [deg] increasing from 0, ≤ 180
    std::vector<double> Horizontal;  // [deg] C-planes, one of the LM-63 layouts
    std::vector<double> Candela;     // [cd] Horizontal.size() × Vertical.size(), row-major by plane
    double Peak = 0.0;               // [cd] the strongest candela value in the grid
};

// 📝 Parses one LM-63 Type C photometric file. Returns nullopt (with Diagnostic filled) for anything
//    the reference parser refuses: a missing or non-NONE TILT, an incomplete header, a grid that does
//    not start at zero or does not increase, a candela value that is negative or not finite, or a
//    grid with no light in it at all.
[[nodiscard]] std::optional<IesPhotometricProfile> ParseIes(std::string_view Text, std::string& Diagnostic);

// 📝 The profile's relative intensity (peak = 1) towards (azimuth°, polar°). LM-63 Type C symmetry:
//    a single plane is rotational, 0-90 mirrors both axes, 0-180 mirrors about C180, 90-270 mirrors
//    about the 90-270 plane (φ → 180° − φ), and a grid that reaches 360° is used as it stands. A grid
//    whose last plane is short of 360° is periodic: between that plane and 360° the intensity blends
//    from the last plane back to the first. Beyond the last measured polar angle there is no light (a
//    downlight measured to 90° emits nothing above the horizontal).
[[nodiscard]] double SampleIes(const IesPhotometricProfile& Profile, double AzimuthDegrees, double PolarDegrees);

// 📝 The packed atlas from the profile texts, in PhotometryProfileFiles order: texel (profile p,
//    azimuth a, polar v) lives at ((p-1)·37 + a)·91 + v, a byte, little-endian inside the u32 the
//    shader loads. Texts is indexed by profile file name.
//    Each profile is scaled so that the strongest TEXEL is 255. The file's own peak can fall between
//    grid points (the FREGAT roadway luminaire peaks at 69° and 166.8°, off the 2° × 10° grid, so its
//    strongest texel is 96 % of that peak): the shader reads texel values as "relative to the
//    profile's peak", so the grid's maximum is the peak it can see.
[[nodiscard]] std::vector<uint8_t> ConstructPhotometricAtlas(
    const std::vector<std::string>& Texts,
    std::string& Diagnostic);

// 📝 Reads the seven shipped profiles out of Directory (<Name>.ies) and packs the atlas. The directory
//    defaults to the engine content path so the renderer never needs a caller-supplied root.
[[nodiscard]] std::vector<uint8_t> LoadPhotometricAtlas(std::string_view Directory, std::string& Diagnostic);
[[nodiscard]] std::vector<uint8_t> LoadPhotometricAtlas(std::string_view Directory = "EngineContent/Photometry");

// 📝 What a decoded atlas texel means, for tests: the bilinear lookup the shader performs, on the CPU.
[[nodiscard]] double SamplePhotometricAtlas(const std::vector<uint8_t>& Atlas,
                                            uint32_t ProfileId,
                                            double AzimuthDegrees,
                                            double PolarDegrees);

} // namespace Frontier
