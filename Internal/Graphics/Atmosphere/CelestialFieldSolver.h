/*==============================================================================================================================================
                                                         CELESTIALFIELDSOLVER.H
==============================================================================================================================================*/
// 🧩 Physically based celestial sphere solver: six simultaneous textured moons with 3D relief normals, Hapke/Lommel-Seeliger + Oren-Nayar
//    regolith BRDF, planetshine, and true 3D ray-marched spherical Rayleigh/Mie/detached-haze lunar atmospheres; paired with a J2000 bright
//    star catalogue, cubemap-indexed Pogson deep field, Ballesteros Planckian Rec.709 spectra, optical Airy-Moffat PSF, and Milky Way band.

#pragma once
#ifndef FRONTIER_GRAPHICS_ATMOSPHERE_CELESTIALFIELDSOLVER_H
#define FRONTIER_GRAPHICS_ATMOSPHERE_CELESTIALFIELDSOLVER_H

#include "Graphics/Atmosphere/AtmosphereProfile.h"
#include "Graphics/Atmosphere/AtmosphericScatteringSolver.h"
#include "EngineContext/Math/LinearAlgebra_Float32.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t MaximumLunarBodyCount = 6u;   // [-] - Simultaneous lunar bodies supported in a single scene

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

enum class LunarPresetIdentifier : uint32_t
{
    Luna    = 0u,   // [-] - Silicate mare/highland moon with crater rims & tenuous sunlit sodium/dust exosphere
    Ember   = 1u,   // [-] - Ferric-oxide canyon world with suspended amber dust troposphere & CO2/ice blue upper limb
    Glacier = 2u,   // [-] - Fractured water-ice ocean moon with translucent cyan-sapphire cryovolcanic plume atmosphere
    Sulfur  = 3u,   // [-] - Silicate-sulfur volcanic moon with luminous golden SO2 plume crescent & violet upper fringe
    Shroud  = 4u,   // [-] - Organic methane-haze world with amber troposphere & detached cobalt stratospheric haze layer
    Shard   = 5u,   // [-] - Scarred ancient basin moon with crisp thin nitrogen-methane pale indigo Rayleigh atmosphere
};

//------------------------------------------------------------------------------------------------------------------------
//                                                           STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Physical parameters of a moon's concentric 3D spherical atmosphere [BodyRadiusKm, BodyRadiusKm + TopAltitudeKm].
struct LunarAtmosphereProfile
{
    bool         AtmosphereEnabled        = true;                        // [-]    - Enables 3D spherical lunar atmosphere integration
    float        BodyRadiusKm             = 1737.4f;                     // [km]   - Solid lunar body radius R_m
    float        TopAltitudeKm            = 120.0f;                      // [km]   - Atmosphere top thickness H_a above R_m
    float        RayleighScaleHeightKm    = 18.0f;                       // [km]   - Exponential molecular scale height H_R
    float        MieScaleHeightKm         = 9.0f;                        // [km]   - Exponential aerosol/dust/plume scale height H_M
    LinearColour RayleighScatteringPerKm  { 0.0004f, 0.0009f, 0.0021f }; // [1/km] - Sea-level lunar Rayleigh scattering β_R
    LinearColour MieScatteringPerKm       { 0.0003f, 0.0003f, 0.0003f }; // [1/km] - Sea-level lunar Mie scattering β_M
    LinearColour MieExtinctionPerKm       { 0.0004f, 0.0004f, 0.0004f }; // [1/km] - Sea-level lunar Mie extinction β_M,ext
    float        MieAsymmetry             = 0.72f;                       // [-]    - Cornette-Shanks aerosol forward asymmetry g
    float        DetachedHazeAltitudeKm   = 0.0f;                        // [km]   - Stratospheric photochemical haze peak (0 = none)
    float        DetachedHazeHalfWidthKm  = 24.0f;                       // [km]   - Gaussian radial half-width of detached haze layer
    LinearColour DetachedHazeScatterPerKm { 0.0f, 0.0f, 0.0f };          // [1/km] - Scattering coefficient at detached haze peak
};

// 📝 Complete physical descriptor for one moon positioned on the celestial sphere (atmosphere-local +Y up axes).
struct LunarBodyDescriptor
{
    LunarPresetIdentifier  Preset                = LunarPresetIdentifier::Luna; // [-]   - Surface map & geological identity
    bool                   VisibilityEnabled     = false;                       // [-]   - Rendered when true
    Vector3f               DirectionLocal        { -0.35f, 0.42f, 0.84f };      // [-]   - Unit direction toward moon centre (+Y up)
    Vector3f               IlluminationDirection { 0.0f, 0.0f, 0.0f };          // [-]   - Optional override sun axis (0 = use Profile sun)
    float                  AngularRadiusRadians  = 0.022f;                      // [rad] - Apparent solid-body angular radius
    float                  SpinLongitudeRadians  = 0.0f;                        // [rad] - Selenographic prime-meridian rotation
    float                  AxialTiltRadians      = 0.08f;                       // [rad] - Spin-axis obliquity
    float                  GeometricAlbedo       = 0.16f;                       // [-]   - Bond/geometric reflectance scale
    float                  AlbedoContrast        = 1.12f;                       // [-]   - Mare-to-highland reflectance dynamic range
    LinearColour           RegolithTint          { 1.0f, 1.0f, 1.0f };          // [-]   - Broadband mineralogical multiplier
    float                  RegolithRoughness     = 0.68f;                       // [rad] - Oren-Nayar micro-facet slope standard deviation
    float                  LommelSeeligerWeight  = 0.72f;                       // [-]   - Porous regolith Lommel-Seeliger blend in [0, 1]
    float                  OppositionSurgeAmp    = 0.55f;                       // [-]   - Hapke shadow-hiding surge B_0
    float                  OppositionSurgeWidth  = 0.085f;                      // [-]   - Hapke angular compaction parameter h_s
    float                  ReliefStrength        = 1.35f;                       // [-]   - Terminator crater/canyon/lineae normal scale
    float                  IceSpecularStrength   = 0.0f;                        // [-]   - Cryogenic water-ice Fresnel highlight (Glacier)
    float                  PlanetshineIntensity  = 0.022f;                      // [-]   - Reflected blue planetshine on night hemisphere
    float                  SolarFluxScale        = 1.0f;                        // [-]   - Incident solar flux calibration multiplier
    LunarAtmosphereProfile Atmosphere            {};                            // [-]   - Physical 3D volumetric lunar atmosphere
};

// 📝 Optical starfield + Milky Way configuration.
struct StarfieldConfiguration
{
    bool     StarsEnabled             = true;    // [-]   - Evaluates J2000 catalogue + Pogson deep field when true
    bool     MilkyWayEnabled          = true;    // [-]   - Evaluates galactic bulge, spiral star clouds, and dust rift
    bool     NebulaeEnabled           = true;    // [-]   - Evaluates M42, M45 reflection, M31, and galactic H-II knots
    float    CelestialRotationRadians = 0.45f;   // [rad] - Sidereal hour angle rotation around celestial pole
    float    SiteLatitudeRadians      = 0.48f;   // [rad] - Latitude tilting celestial pole relative to local vertical
    float    RadianceScale            = 1.0f;    // [-]   - Photometric starfield exposure calibration
    float    MilkyWayRadianceScale    = 1.0f;    // [-]   - Galactic plane surface-brightness calibration
    float    ScintillationStrength    = 0.35f;   // [-]   - Atmospheric Kolmogorov twinkling strength (forced 0 in space)
    float    PixelAngularSizeRadians  = 0.0011f; // [rad] - Sensor pixel pitch setting diffraction-limited optical PSF central lobe
};

// 📝 Decoded linear-Rec.709 equirectangular lunar reflectance map, normalised to unit mean luminance (absolute scale = GeometricAlbedo);
//    its luminance doubles as the relief height channel for terminator normals.
struct DecodedLunarSurfaceMap
{
    uint32_t           Width  = 0u;             // [px]  - Equirectangular longitude texel count (e.g. 2048)
    uint32_t           Height = 0u;             // [px]  - Equirectangular latitude texel count (e.g. 1024)
    std::vector<float> LinearRgb;               // [-]   - Width * Height * 3 relative linear Rec.709 reflectance values
    bool               LoadedFromAsset = false; // [-]   - True when decoded from Documentation/Assets/CelestialMaps
};

// 📝 Loaded cache of all six lunar surface maps shared across renders and validation checks.
struct LunarSurfaceAtlas
{
    DecodedLunarSurfaceMap Maps[MaximumLunarBodyCount] {};   // [-] - Indexed by LunarPresetIdentifier
    uint32_t               DecodedAssetCount           = 0u; // [-] - Count of authentic maps loaded from disk
};

// 📝 Full multi-moon + starfield scene specification.
struct CelestialSceneConfiguration
{
    LunarBodyDescriptor    Moons[MaximumLunarBodyCount] {};   // [-] - Up to six active moons in one unified sky/space frame
    uint32_t               MoonCount                    = 0u; // [-] - Populated moon slots
    StarfieldConfiguration Stars                        {};   // [-] - Optical J2000 starfield and Milky Way configuration
};

// 📝 Result of evaluating all moons (solid discs + off-limb 3D volumetric atmospheres) along one ray.
struct LunarRayResult
{
    LinearColour Radiance             {};                   // [-] - Scattered lunar atmosphere + attenuated regolith radiance
    LinearColour TransmittanceToStars { 1.0f, 1.0f, 1.0f }; // [-] - Remaining transmittance to background stars (0 on solid moon)
    bool         SolidDiscHit         = false;              // [-] - True when ray intersected a moon's solid surface (r <= R_m)
    bool         OffLimbAtmosphereHit = false;              // [-] - True when ray traversed only off-limb lunar atmosphere (r > R_m)
    float        NormalizedImpact     = 1.0e6f;             // [-] - Closest ray impact parameter b / R_m to nearest active moon
};

// 📝 Compact std140 GPU uniform mirror for Shaders/CelestialField.glsl (608 bytes).
struct LunarUniformEntry
{
    float DirectionRadius[4];             // [-,rad] - xyz: local direction (+Y up); w: angular radius
    float AlbedoRoughness[4];             // [-]     - xyz: regolith tint * albedo; w: roughness
    float PhaseSpinPack[4];               // [-,rad] - x: Lommel weight; y: opposition amp; z: spin lon; w: preset index
    float AtmosphereGeometryPack[4];      // [km,-]  - x: body R_m; y: top H_a; z: H_R; w: H_M
    float AtmosphereRayleighAsymmetry[4]; // [1/km,-]- xyz: Rayleigh β_R; w: Mie asymmetry g
    float AtmosphereMieHazePack[4];       // [1/km,km]- xyz: Mie β_M; w: detached haze altitude km
};

struct CelestialUniformBlock
{
    float             StarControlPack[4];           // [rad,-] - x: sidereal rot; y: site lat; z: star scale; w: Milky Way scale
    float             OpticsCountPack[4];           // [rad,-] - x: pixel angle; y: scintillation; z: sequence sec; w: active moon count
    LunarUniformEntry Moons[MaximumLunarBodyCount]; // [-]     - 6 x 96 B = 576 B packed moon + volumetric atmosphere profiles
};

static_assert(sizeof(CelestialUniformBlock) == 608u, "CelestialUniformBlock must remain 608 bytes for std140 parity.");

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build a physically calibrated LunarBodyDescriptor for one of the six canonical moon presets.
[[nodiscard]] LunarBodyDescriptor BuildLunarPreset(
    LunarPresetIdentifier Preset,
    const Vector3f&       DirectionLocal,
    float                 AngularRadiusRadians = 0.024f) noexcept;

// Load all six equirectangular lunar maps from Documentation/Assets/CelestialMaps (with procedural fallback if unavailable).
void LoadLunarSurfaceAtlas(LunarSurfaceAtlas& OutAtlas, const std::string& OptionalSearchRoot = std::string());

// Convert Johnson B-V colour index [-0.4, +2.0] to linear Rec.709 chromaticity via Ballesteros (2012) + Planck's law.
[[nodiscard]] LinearColour ConvertColourIndexToLinearRec709(float ColourIndexBV) noexcept;

// Evaluate a single moon's solid surface + true 3D spherical volumetric atmosphere along LocalDirection (+Y up).
[[nodiscard]] LunarRayResult EvaluateSingleLunarBody(
    const LunarBodyDescriptor&     Moon,
    const LunarSurfaceAtlas*       OptionalAtlas,
    const Vector3f&                LocalDirection,
    const Vector3f&                SolarDirectionLocal,
    const LinearColour&            SolarIlluminance,
    uint32_t                       AtmosphereStepCount = 28u) noexcept;

// Evaluate all active moons in Scene (back-to-front by angular size) along LocalDirection (+Y up).
[[nodiscard]] LunarRayResult EvaluateLunarSystem(
    const CelestialSceneConfiguration& Scene,
    const LunarSurfaceAtlas*           OptionalAtlas,
    const Vector3f&                    LocalDirection,
    const Vector3f&                    SolarDirectionLocal,
    const LinearColour&                SolarIlluminance) noexcept;

// Evaluate J2000 bright star catalogue + 14k+ Pogson deep starfield + Milky Way & dark dust rift along LocalDirection (+Y up).
[[nodiscard]] LinearColour EvaluateStarfieldGalaxy(
    const StarfieldConfiguration&  StarConfiguration,
    const Vector3f&                LocalDirection,
    float                          SequenceSeconds,
    bool                           VacuumSpaceCondition) noexcept;

// Evaluate complete celestial backdrop (moons + optical stars + Milky Way) with exact terrestrial Beer-Lambert extinction.
[[nodiscard]] LinearColour EvaluateCelestialBackdrop(
    const CelestialSceneConfiguration& Scene,
    const LunarSurfaceAtlas*           OptionalAtlas,
    const AtmosphericLookupTables&     Tables,
    const AtmosphereUniformBlock&      Profile,
    const Vector3f&                    LocalDirection,
    float                              SequenceSeconds,
    bool                               VacuumSpaceCondition) noexcept;

// Pack CPU CelestialSceneConfiguration into std140 CelestialUniformBlock for Vulkan UBO upload.
void EncodeCelestialUniformBlock(
    const CelestialSceneConfiguration& Scene,
    float                              SequenceSeconds,
    CelestialUniformBlock&             OutUniforms) noexcept;

} // namespace Frontier

#endif
