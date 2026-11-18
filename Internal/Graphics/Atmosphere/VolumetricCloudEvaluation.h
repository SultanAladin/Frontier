/*==============================================================================================================================================
                                                     VOLUMETRICCLOUDEVALUATION.H
==============================================================================================================================================*/
// 🧩 Nubis-class physical volumetric cloudscape solver: 3D Perlin-Worley shape + domain-rotated Worley detail erosion, Owen-scrambled (0,2)
//    Sobol ray jitter, AtmosphericWindField backward characteristic advection, 6-tap sun-cone self-shadowing, 4-octave multiple-scattering
//    dual-lobe Henyey-Greenstein + Beer-Powder radiative transfer, and spherical atmospheric aerial-perspective coupling.

#pragma once
#ifndef FRONTIER_GRAPHICS_ATMOSPHERE_VOLUMETRICCLOUDEVALUATION_H
#define FRONTIER_GRAPHICS_ATMOSPHERE_VOLUMETRICCLOUDEVALUATION_H

#include "EngineContext/Math/LinearAlgebra_Float32.h"
#include "Graphics/Atmosphere/RadiometricSkyEvaluation.h"
#include "Simulation/ForceField/AtmosphericForceField.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class CloudFormationPreset : uint8_t
{
    FairWeatherCumulus   = 0u,   // [-] - Puffy convective cumulus humilis / mediocris deck
    StratocumulusSheet   = 1u,   // [-] - Broken marine stratocumulus rolls
    ToweringCumulonimbus = 2u,   // [-] - Deep convective storm towers with glaciated anvil tops
    CyclonicSpiralDeck   = 3u,   // [-] - Mesoscale spiral cloud bands sculpted by CyclonicCirculation ForceFields
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Optional localized cloud volume (e.g., isolated cumulus cell or mesoscale storm cluster advected by ForceFields).
struct LocalizedCloudCell
{
    bool     ActivationEnabled    = true;                          // [-] - Participates when true
    Vector3f CenterMetres         { 0.0f, 6500.0f, 2400.0f };      // [m] - Initial Lagrangian world center (Z-up)
    Vector3f HalfExtentsMetres    { 3800.0f, 3800.0f, 1350.0f };   // [m] - Ellipsoidal envelope dimensions
    float    PeakDensityMultiplier = 1.35f;                        // [x] - Local liquid-water content gain
};

// 📝 Complete physical and numerical specification for volumetric cloud raymarching.
struct VolumetricCloudConfiguration
{
    bool                 CloudsEnabled               = true;                                      // [-]   - Master volumetric cloud gate
    CloudFormationPreset FormationPreset             = CloudFormationPreset::FairWeatherCumulus;  // [-]   - Selects vertical altitude gradient
    float                LowerAltitudeMetres         = 1450.0f;                                   // [m]   - Cloud condensation level above surface
    float                StratumThicknessMetres      = 2650.0f;                                   // [m]   - Vertical cloud deck span
    float                MacroCoverage               = 0.52f;                                     // [0..1] - Weather-map horizontal cloud fraction
    float                PrecipitationBias           = 0.20f;                                     // [0..1] - Increases optical thickness & storm anvil
    float                ExtinctionPerMetre          = 0.042f;                                    // [1/m] - Peak cloud droplet extinction coefficient
    float                CondensateDensityGain       = 2.60f;                                     // [x]   - Normalised mass -> droplet density gain
    float                SingleScatteringAlbedo      = 0.985f;                                    // [0..1] - Droplet scattering albedo (~0.99 for clean water)
    float                DetailErosionStrength       = 0.36f;                                     // [0..1] - 3D Worley wispy edge carving weight
    float                ForwardLobeAnisotropy       = 0.80f;                                     // [0..0.95] - Primary Mie forward HG lobe g0
    float                BackLobeAnisotropy          = -0.42f;                                    // [-0.9..0] - Secondary glory/backscatter HG lobe g1
    float                ForwardLobeMix              = 0.70f;                                     // [0..1] - Blend weight toward g0
    float                PowderEffectStrength        = 0.58f;                                     // [0..1] - Schneider Beer-Powder dark-edge relief
    float                AdvectionSpanSeconds        = 180.0f;                                    // [s]   - Backward streamline integration window
    bool                 CumulativeAdvectionEnabled  = true;                                      // [-]   - Uses SequenceSeconds directly for continuous drift
    uint32_t             PrimaryMarchStepCount       = 96u;                                       // [-]   - View-ray integration sample count
    uint32_t             SunConeStepCount            = 6u;                                        // [-]   - Cone-shadow samples toward SolarDirection
    float                MaximumTraceDistanceMetres  = 52000.0f;                                  // [m]   - Far cutoff for low-angle cloud rays
    float                TargetMarchStepMetres       = 62.0f;                                     // [m]   - Adaptive step target near the cloud
    std::vector<LocalizedCloudCell> LocalizedCells   {};                                          // [-]   - Optional hero cells advected by wind/forces
};

// 📝 Premultiplied-alpha volumetric cloud integration output ready for aerial-perspective compositing over sky + celestials.
struct VolumetricCloudRayResult
{
    SpectralColor InscatteredRadiance         { 0.0f, 0.0f, 0.0f };   // [-] - Accumulated cloud + aerial-perspective radiance
    float         Transmittance               = 1.0f;                 // [0..1] - Remaining background visibility through cloud deck
    float         WeightedHitDistanceMetres   = 0.0f;                 // [m] - Optical-depth-weighted mean cloud distance
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bit-exact Laine-Karras Owen-scrambled base-2 Sobol sequence in [0, 1) eliminating LCG/IGN screen-space striping.
[[nodiscard]] float EvaluateOwenSobolUnit(uint32_t SampleIndex, uint32_t DimensionIndex, uint32_t PixelScrambleSeed) noexcept;

// Evaluate the shared rotated-gradient FBM shape basis in [0, 1] (the fog layers use it so their patchiness matches the cloud deck).
[[nodiscard]] float EvaluateCloudShapeNoise(const Vector3f& Coord, uint32_t Octaves, uint32_t Salt) noexcept;

// Evaluate volumetric cloud density [0..1] at WorldPositionMetres (Z-up) advected by OptionalWindField at SequenceSeconds.
[[nodiscard]] float EvaluateCloudDensityField(const VolumetricCloudConfiguration& CloudConfiguration,
                                              const Vector3f&                     WorldPositionMetres,
                                              float                               SequenceSeconds,
                                              const AtmosphericWindField*         OptionalWindField = nullptr) noexcept;

// Bake the coarse Lagrangian wind-offset volume covering the volumetric cloud stratum for the current integration.
void PrepareCloudAdvectionOffsetVolume(AdvectionOffsetVolume&              OffsetVolume,
                                       const VolumetricCloudConfiguration& CloudConfiguration,
                                       const RadiometricSkyConfiguration&  SkyConfiguration,
                                       const AtmosphericWindField*         OptionalWindField) noexcept;

// Raymarch the spherical volumetric cloud stratum along RayDirectionScene and couple with Hillaire sky transmittance + aerial perspective.
// Pass the baked OffsetVolume to make every march / shadow-cone density sample a trilinear fetch (nullptr falls back to
// exact per-sample streamline integration).
[[nodiscard]] VolumetricCloudRayResult EvaluateVolumetricCloudRay(const VolumetricCloudConfiguration& CloudConfiguration,
                                                                  const RadiometricSkyTables&         SkyTables,
                                                                  const RadiometricSkyConfiguration&  SkyConfiguration,
                                                                  const Vector3f&                     CameraSceneMetres,
                                                                  const Vector3f&                     RayDirectionScene,
                                                                  const AtmosphericWindField*         OptionalWindField          = nullptr,
                                                                  uint32_t                            PixelCoordX                = 0u,
                                                                  uint32_t                            PixelCoordY                = 0u,
                                                                  uint32_t                            SubSampleIndex             = 0u,
                                                                  const AdvectionOffsetVolume*        PreparedCloudOffsetVolume  = nullptr) noexcept;

} // namespace Frontier

#endif
