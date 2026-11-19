/*==============================================================================================================================================
                                                        VOLUMETRICCLOUDSOLVER.H
==============================================================================================================================================*/
// 🧩 Curved spherical volumetric cloud stratum + upper cirrus veil: signed-distance thermal envelopes (germ-grain lattice of domes, bubble
//    columns, anvilled storm towers, rolls) grown by a Quílez bubble cascade and sculpted by Nubis-style Worley value erosion, five physical
//    genera (Cumulus, Stratocumulus, Stratus, Cumulonimbus, Cirrus), RK4 AtmosphericFlowField advection, Owen-Sobol jittered ray marching,
//    Wrenninge multiple scattering over the Jendersie–d'Eon approximate Mie phase (δ-M diffraction peak), Beer-Powder, solar cone, analytic
//    step integral, Hillaire aerial perspective, cloud shadow transmittance, crepuscular-ray (godray) inscatter, and joint bilateral denoising.

#pragma once
#ifndef FRONTIER_GRAPHICS_ATMOSPHERE_VOLUMETRICCLOUDSOLVER_H
#define FRONTIER_GRAPHICS_ATMOSPHERE_VOLUMETRICCLOUDSOLVER_H

#include "Graphics/Atmosphere/AtmosphereProfile.h"
#include "Graphics/Atmosphere/AtmosphericFlowField.h"
#include "Graphics/Atmosphere/AtmosphericScatteringSolver.h"
#include "EngineContext/Math/LinearAlgebra_Float32.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

enum class CloudGenusIdentifier : uint32_t
{
    Cumulus       = 0u,   // [-] - Fair-weather convective cells: flat condensation floor, cauliflower billowed crowns
    Stratocumulus = 1u,   // [-] - Shallow rolled cellular deck with open/closed mesoscale cells
    Stratus       = 2u,   // [-] - Uniform featureless grey sheet with soft undulating underside
    Cumulonimbus  = 3u,   // [-] - Deep convective tower reaching the tropopause with a spreading anvil
    Cirrus        = 4u,   // [-] - High-altitude fibrous ice-crystal filaments (veil-only genus)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                           STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Physical description of the curved volumetric cloud stratum (altitudes above BottomRadius).
struct CloudStratumProfile
{
    CloudGenusIdentifier Genus               = CloudGenusIdentifier::Cumulus; // [-]    - Morphological genus
    float                CoverageFraction    = 0.45f;                         // [-]    - Sky fraction covered in [0, 1]
    float                FloorAltitudeKm     = 1.4f;                          // [km]   - Condensation level (cloud base)
    float                CeilingAltitudeKm   = 3.6f;                          // [km]   - Stratum top
    float                ExtinctionPerKm     = 42.0f;                         // [1/km] - σ_t at unit density (liquid water ~0.04 1/m)
    float                SingleScatterAlbedo = 0.995f;                        // [-]    - ω_0 of cloud droplets
    float                ShapeScaleKm        = 4.2f;                          // [km]   - Perlin-Worley base cell size
    float                DetailScaleKm       = 0.55f;                         // [km]   - Worley erosion billow size
    float                ErosionStrength     = 0.42f;                         // [-]    - Edge erosion depth in [0, 1]
    float                WeatherScaleKm      = 18.0f;                         // [km]   - Mesoscale coverage cluster size
    float                AnvilSpread         = 0.0f;                          // [-]    - Upper anvil coverage boost (Cumulonimbus)
};

// 📝 Upper-troposphere ice-crystal cirrus veil (thin curved sheet).
struct CirrusVeilProfile
{
    bool  VeilEnabled      = true;  // [-]    - Evaluates the cirrus sheet when true
    float CoverageFraction = 0.35f; // [-]    - Filament coverage in [0, 1]
    float AltitudeKm       = 9.2f;  // [km]   - Sheet mid-altitude
    float OpticalDepthPeak = 0.55f; // [-]    - Vertical optical depth at full filament density
    float FilamentScaleKm  = 6.5f;  // [km]   - Along-wind fibre length scale
    float StreakAnisotropy = 5.0f;  // [-]    - Along-wind to cross-wind stretch ratio
    float SheetThicknessKm = 1.4f;  // [km]   - Ice slab thickness setting the exact grazing chord length
};

// 📝 Complete volumetric cloud configuration (quality, lighting model, and both cloud layers).
struct VolumetricCloudConfiguration
{
    bool                CloudsEnabled          = true;   // [-]   - Master enable
    CloudStratumProfile Stratum                {};       // [-]   - Primary curved volumetric stratum
    CirrusVeilProfile   Cirrus                 {};       // [-]   - Upper cirrus sheet
    uint32_t            PrimaryStepLimit       = 384u;   // [-]   - Maximum primary ray-march samples (coarse probes + fine steps)
    uint32_t            SolarConeTapCount      = 6u;     // [-]   - Cone-distributed solar shadow taps
    uint32_t            ScatterOctaveCount     = 4u;     // [-]   - Wrenninge multiple-scattering octaves
    float               DropletDiameterMicrometres = 16.0f; // [um] - Effective droplet diameter of the approximate Mie phase (5..50)
    float               PowderStrength         = 0.65f;  // [-]   - Beer-Powder in-scatter darkening strength
    float               AmbientStrength        = 1.0f;   // [-]   - Sky ambient irradiance gain
    float               MaximumTraceDistanceKm = 90.0f;  // [km]  - Primary trace distance cap (skyline LOD)
    float               AerialPerspectiveScale = 1.0f;   // [-]   - Hillaire aerial perspective strength
};

// 📝 Jendersie–d'Eon (2023) approximate Mie phase: (1 - w) HG(g_peak) + w Draine(g_D, α), fitted to water droplets of diameter d.
struct ApproximateMieParameters
{
    float PeakAsymmetry   = 0.995f; // [-] - g of the narrow HG diffraction lobe
    float DraineAsymmetry = 0.59f;  // [-] - g of the Draine body lobe
    float DraineAlpha     = 27.0f;  // [-] - Draine α (0 = HG, 1 = Cornette-Shanks)
    float DraineWeight    = 0.50f;  // [-] - Blend weight w of the Draine lobe
};

// 📝 Crepuscular rays (godrays): sunlight scattered by the air between the viewer and the cloud floor, gated by the cloud shadow at every
//    point of the view ray. Shafts are the part of the sky's air in-scatter that the cloud shadow removes, plus an optional hazy boundary
//    layer (aerosol σ_e e^{-h/H}) that makes the lit shafts brighter than the clean-air sky.
struct CrepuscularRayConfiguration
{
    bool     ShaftsEnabled        = false; // [-]    - Evaluates the godray layer when true
    float    HazeExtinctionPerKm  = 0.08f; // [1/km] - Boundary-layer aerosol extinction at the surface (0 = clean Earth profile only)
    float    HazeScaleHeightKm    = 1.6f;  // [km]   - Aerosol scale height
    float    HazeAsymmetry        = 0.72f; // [-]    - Henyey-Greenstein g of the haze particles (continental aerosol 0.6..0.75)
    float    HazeAlbedo           = 0.92f; // [-]    - Single-scatter albedo of the haze
    float    MaximumDistanceKm    = 40.0f; // [km]   - View-ray march limit
    uint32_t MarchStepCount       = 28u;   // [-]    - View-ray samples (each one marches the sunward cloud column)
};

// 📝 std140 GPU mirror of CrepuscularRayConfiguration (32 B).
struct CrepuscularRayUniformBlock
{
    float HazePack[4];  // [1/km,km,-] - x: haze extinction; y: scale height; z: HG g; w: single-scatter albedo
    float MarchPack[4]; // [km,-]      - x: maximum distance; y: step count; z: enabled (0 / 1); w: reserved
};

static_assert(sizeof(CrepuscularRayUniformBlock) == 32u, "CrepuscularRayUniformBlock must remain 32 bytes for std140 parity.");

// 📝 Result of integrating the godray layer along one view ray. Composite: Haze + T_haze (Cloud + T_cloud max(0, Backdrop - Deficit)).
struct CrepuscularRayResult
{
    LinearColour ShadowedDeficit   {};                   // [-] - Clean-air in-scatter removed by the cloud shadow (subtract from the sky)
    LinearColour HazeRadiance      {};                   // [-] - Sun + sky light scattered by the haze layer (shadow-gated)
    LinearColour HazeTransmittance { 1.0f, 1.0f, 1.0f }; // [-] - Haze transmittance to the cloud floor / far limit
};

// 📝 Result of integrating the cloud layers along one view ray.
struct CloudRayResult
{
    LinearColour Radiance           {};                   // [-]  - In-scattered cloud radiance incl. aerial perspective
    LinearColour Transmittance      { 1.0f, 1.0f, 1.0f }; // [-]  - Remaining transmittance to the sky/celestial backdrop
    float        WeightedDistanceKm = 0.0f;               // [km] - Transmittance-weighted mean cloud distance (denoise guide)
};

// 📝 Exact std140 GPU uniform mirror for Shaders/VolumetricCloudField.glsl (112 bytes).
struct CloudFieldUniformBlock
{
    float StratumGeometryPack[4]; // [km,-]   - x: floor altitude; y: ceiling altitude; z: coverage; w: genus index
    float StratumNoisePack[4];    // [km,-]   - x: shape scale; y: detail scale; z: erosion; w: weather scale
    float StratumOpticsPack[4];   // [1/km,-] - x: σ_t; y: ω_0; z: anvil spread; w: powder strength
    float PhasePack[4];           // [-]      - x: peak g; y: Draine g; z: Draine alpha; w: Draine weight
    float CirrusPack[4];          // [km,-]   - x: altitude; y: coverage; z: optical depth; w: filament scale (0 = disabled)
    float QualityPack[4];         // [-,km]   - x: step limit; y: cone taps; z: trace distance; w: sequence seconds
    float LightingPack[4];        // [um,-]   - x: droplet diameter; y: diffraction peak fraction (delta-M); z: octave count; w: ambient gain
};

static_assert(sizeof(CloudFieldUniformBlock) == 112u, "CloudFieldUniformBlock must remain 112 bytes for std140 parity.");

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build a calibrated cloud configuration for one genus at CoverageFraction (Cirrus selects a thin stratum + dense veil).
[[nodiscard]] VolumetricCloudConfiguration BuildCloudGenusPreset(CloudGenusIdentifier Genus, float CoverageFraction) noexcept;

// Resolve the Jendersie–d'Eon approximate Mie phase parameters for an effective droplet diameter [um] (clamped to the 5..50 fit range).
[[nodiscard]] ApproximateMieParameters ResolveApproximateMieParameters(float DropletDiameterMicrometres) noexcept;

// Evaluate the approximate Mie phase [1/sr] at CosineAngle; EccentricityScale multiplies both asymmetries (multiple-scatter octaves).
[[nodiscard]] float EvaluateApproximateMiePhase(const ApproximateMieParameters& Phase, float CosineAngle, float EccentricityScale = 1.0f) noexcept;

// Fraction of scattering inside the diffraction peak, removed from sunlight-path extinction by delta-M scaling.
[[nodiscard]] float ResolveDiffractionPeakFraction(const ApproximateMieParameters& Phase) noexcept;

// Evaluate 3D Worley F1 cellular noise in [0, 1] (1 at feature points, 0 far away) at Coord (unit cell spacing).
[[nodiscard]] float EvaluateWorleyNoise3D(const Vector3f& Coord, uint32_t Salt) noexcept;

// Evaluate normalized stratum cloud density at an atmosphere-local planet-centred position [km] (+Y up). DetailEnabled=false: two bubble octaves, no erosion.
[[nodiscard]] float EvaluateCloudDensity(
    const VolumetricCloudConfiguration& CloudConfiguration,
    const AtmosphereUniformBlock&       Profile,
    const AtmosphericFlowField*         OptionalFlowField,
    const Vector3f&                     PlanetPositionKm,
    float                               SequenceSeconds,
    bool                                DetailEnabled) noexcept;

// Evaluate the empty-space probe bound of the stratum density (thermal envelope lifted by the maximum bubble-cascade growth): zero
// wherever no fidelity level can produce cloud.
[[nodiscard]] float EvaluateCloudDensityBound(
    const VolumetricCloudConfiguration& CloudConfiguration,
    const AtmosphereUniformBlock&       Profile,
    const AtmosphericFlowField*         OptionalFlowField,
    const Vector3f&                     PlanetPositionKm,
    float                               SequenceSeconds) noexcept;

// Integrate stratum + cirrus along LocalDirection from the profile viewer altitude (Owen-Sobol jitter seeded by pixel + sample ordinal).
[[nodiscard]] CloudRayResult IntegrateVolumetricClouds(
    const VolumetricCloudConfiguration& CloudConfiguration,
    const AtmosphericLookupTables&      Tables,
    const AtmosphereUniformBlock&       Profile,
    const AtmosphericFlowField*         OptionalFlowField,
    const Vector3f&                     LocalDirection,
    float                               SequenceSeconds,
    uint32_t                            PixelX,
    uint32_t                            PixelY,
    uint32_t                            SampleOrdinal) noexcept;

// Evaluate sun-ward cloud shadow transmittance (stratum + cirrus) above a planet-centred point [km].
[[nodiscard]] float EvaluateCloudShadowTransmittance(
    const VolumetricCloudConfiguration& CloudConfiguration,
    const AtmosphereUniformBlock&       Profile,
    const AtmosphericFlowField*         OptionalFlowField,
    const Vector3f&                     PlanetPositionKm,
    float                               SequenceSeconds) noexcept;

// Integrate crepuscular rays (cloud-shadowed air in-scatter deficit + hazy boundary-layer shafts) along a viewer-local direction.
[[nodiscard]] CrepuscularRayResult IntegrateCrepuscularRays(
    const CrepuscularRayConfiguration&  ShaftConfiguration,
    const VolumetricCloudConfiguration& CloudConfiguration,
    const AtmosphericLookupTables&      Tables,
    const AtmosphereUniformBlock&       Profile,
    const AtmosphericFlowField*         OptionalFlowField,
    const Vector3f&                     LocalDirection,
    float                               SequenceSeconds,
    float                               StartJitter) noexcept;

// Pack CrepuscularRayConfiguration into the std140 GPU block.
void EncodeCrepuscularRayUniformBlock(const CrepuscularRayConfiguration& ShaftConfiguration, CrepuscularRayUniformBlock& OutUniforms) noexcept;

// Edge-preserving 5x5 joint bilateral filter over cloud radiance + transmittance guided by distance and transmittance (in place).
void DenoiseCloudLayerBilateral(
    uint32_t                   Width,
    uint32_t                   Height,
    std::vector<LinearColour>& InOutRadiance,
    std::vector<LinearColour>& InOutTransmittance,
    const std::vector<float>&  DistanceGuideKm);

// Pack CPU VolumetricCloudConfiguration into the std140 CloudFieldUniformBlock for Vulkan UBO upload.
void EncodeCloudFieldUniformBlock(
    const VolumetricCloudConfiguration& CloudConfiguration,
    float                               SequenceSeconds,
    CloudFieldUniformBlock&             OutUniforms) noexcept;

} // namespace Frontier

#endif
