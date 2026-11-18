/*==============================================================================================================================================
                                                ATMOSPHERICFOGEVALUATION.H
==============================================================================================================================================*/
// 🧩 Physical participating-media fog: an exponential-height aerosol layer integrated in closed form along the view ray (Beer-Lambert,
//    no step count) plus a set of ellipsoidal local fog volumes raymarched with the same multiple-scattering ladder the cloud deck uses.
//    Both are advected by the same AtmosphericWindField / ForceField and the same baked Lagrangian offset volume that moves the clouds,
//    so ground fog, valley mist and smoke plumes drift in lockstep with the sky and the cloud deck instead of sliding against them.

#pragma once
#ifndef FRONTIER_GRAPHICS_ATMOSPHERE_ATMOSPHERICFOGEVALUATION_H
#define FRONTIER_GRAPHICS_ATMOSPHERE_ATMOSPHERICFOGEVALUATION_H

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

enum class FogLayerCategory : uint8_t
{
    RadiationMist   = 0u,   // [-] - Radiatively cooled nocturnal boundary-layer mist (thin, sharply confined to the surface)
    AdvectionFog    = 1u,   // [-] - Warm moist air advected over a cold surface (deep, uniform, the classic sea fog)
    ValleyStratum   = 2u,   // [-] - Cold-air drainage pooled in terrain lows (flat-topped, bounded above by an inversion)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Exponential height fog layer: density rho(z) = Rho0 * exp(-(z - BaseAltitude) / ScaleHeight), the standard atmospheric
//    aerosol stratification. The vertical integral is solved analytically, so the layer costs one exponential per channel.
struct HeightFogConfiguration
{
    bool           LayerEnabled          = true;                       // [-]   - Master gate for this layer
    FogLayerCategory Category            = FogLayerCategory::AdvectionFog; // [-] - Selects the physical stratification profile
    float          BaseAltitudeMetres    = 8.0f;                       // [m]   - Datum altitude the profile is anchored to
    float          ExtinctionPerKilometre = 0.42f;                     // [1/km]- Extinction coefficient at the datum
    float          ScaleHeightMetres     = 620.0f;                     // [m]   - e-folding height of the aerosol column
    float          InversionAltitudeMetres = 1400.0f;                  // [m]   - Sharp upper bound for ValleyStratum (0 = none)
    float          InversionReliefMetres = 0.0f;                       // [m]   - Wind-advected vertical relief of the inversion cap
    float          SingleScatteringAlbedo = 0.985f;                    // [0..1]- Droplet/aerosol scattering albedo
    float          ForwardLobeAnisotropy = 0.68f;                      // [-1..1]- Henyey-Greenstein g for the forward Mie lobe
    float          BackLobeAnisotropy    = -0.28f;                     // [-1..0]- Secondary back-scatter lobe
    float          ForwardLobeMix        = 0.72f;                      // [0..1] - Weight toward the forward lobe
    float          DensityNoiseStrength  = 0.55f;                      // [0..1] - ForceField-advected patchiness of the layer
};

// 📝 One localized fog body (valley pool, smoke plume, ground-hugging bank) as an ellipsoid advected by the ForceField.
struct LocalFogVolume
{
    bool     VolumeEnabled        = true;                              // [-] - Participates when true
    Vector3f CenterMetres         { 0.0f, 1800.0f, 220.0f };           // [m] - Lagrangian world center (Z-up)
    Vector3f HalfExtentsMetres    { 2600.0f, 1900.0f, 520.0f };        // [m] - Ellipsoidal envelope dimensions
    float    ExtinctionPerKilometre = 6.5f;                            // [1/km]- Peak extinction at the ellipsoid centre
    float    EdgeSoftness         = 0.55f;                             // [0..1]- 0 = hard ellipsoid shell, 1 = very diffuse margin
    float    AnisotropyGain       = 1.0f;                              // [x]   - Local forward-scatter boost (steam vs. haze)
    float    DensityNoiseStrength = 0.60f;                             // [0..1]- Wind-advected billow that breaks the ellipsoid shell
    bool     AdvectedByForceField = true;                              // [-] - Lets the flow carry the body downstream
};

// 📝 Complete fog specification coupling the stratified layer and the local volumes to the shared ForceField.
struct AtmosphericFogConfiguration
{
    bool                      FogEnabled            = true;            // [-]   - Master gate for all fog
    HeightFogConfiguration    HeightLayer            {};                // [-]   - Exponential height layer
    std::vector<LocalFogVolume> LocalVolumes         {};                // [-]   - Discrete fog bodies
    bool                      WindAdvectionEnabled  = true;            // [-]   - Couples every layer to the AtmosphericWindField
    float                     AdvectionSpanSeconds  = 240.0f;          // [s]   - Lagrangian lookback window for the fog bodies
    uint32_t                  VolumeMarchStepCount  = 28u;             // [-]   - Samples through the local-volume span
    float                     MaximumTraceDistanceMetres = 24000.0f;   // [m]   - Far cutoff for local fog bodies
    float                     AmbientInscatterGain  = 0.55f;           // [x]   - Sky-dome fill light on the fog
};

// 📝 Premultiplied fog colour plus the transmittance that survives it (composite as background * T + Inscattered).
struct FogRayResult
{
    SpectralColor InscatteredRadiance { 0.0f, 0.0f, 0.0f };   // [-] - Fog radiance toward the camera
    float         Transmittance       = 1.0f;                 // [0..1] - Fraction of the background still visible
    float         HeightOpticalDepth  = 0.0f;                 // [-] - Vertical-layer optical depth (for audit gating)
    float         VolumeOpticalDepth  = 0.0f;                 // [-] - Local-volume optical depth (for audit gating)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Extinction coefficient [1/km] of the stratified layer at AltitudeMetres (public so the audit can profile the column).
[[nodiscard]] float EvaluateHeightFogExtinctionPerKilometre(const HeightFogConfiguration& Layer,
                                                           float                         AltitudeMetres) noexcept;

// Resolve a localized fog body's CURRENT center after ForceField advection (backward characteristic at SequenceSeconds).
[[nodiscard]] Vector3f ResolveAdvectedFogCenter(const AtmosphericFogConfiguration& Configuration,
                                                const LocalFogVolume&              Volume,
                                                const AtmosphericWindField*        OptionalWindField,
                                                float                              SequenceSeconds) noexcept;

// Integrate every fog layer along one view ray, lit by the same sky/solar radiance the atmosphere pass uses.
[[nodiscard]] FogRayResult EvaluateAtmosphericFogRay(const AtmosphericFogConfiguration& Configuration,
                                                     const RadiometricSkyTables&        SkyTables,
                                                     const RadiometricSkyConfiguration& SkyConfiguration,
                                                     const Vector3f&                    CameraSceneMetres,
                                                     const Vector3f&                    RayDirectionScene,
                                                     const AtmosphericWindField*        OptionalWindField            = nullptr,
                                                     const AdvectionOffsetVolume*       PreparedWindOffsetVolume     = nullptr,
                                                     uint32_t                           PixelCoordX                  = 0u,
                                                     uint32_t                           PixelCoordY                  = 0u,
                                                     uint32_t                           SubSampleIndex               = 0u) noexcept;

} // namespace Frontier

#endif
