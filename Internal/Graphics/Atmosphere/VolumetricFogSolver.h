/*==============================================================================================================================================
                                                         VOLUMETRICFOGSOLVER.H
==============================================================================================================================================*/
// 🧩 Atmospheric height fog + local volumetric fog banks driven by the SAME AtmosphericFlowField that moves the clouds: analytic exponential
//    height falloff (exact closed-form segment and sun-ward optical depth), ellipsoidal fog volumes with advected fBm edges, dual-phase bounded-age
//    RK4 advection (wind, curl eddies and every force-field operator push, swirl, lift, clear or thicken the fog), Beer-Lambert energy-conserving
//    near->far integration, dual-lobe Henyey-Greenstein phase, SunTransmittanceMarch self-shadow, and a two-stream diffuse multiple-scatter term.

#pragma once
#ifndef FRONTIER_GRAPHICS_ATMOSPHERE_VOLUMETRICFOGSOLVER_H
#define FRONTIER_GRAPHICS_ATMOSPHERE_VOLUMETRICFOGSOLVER_H

#include "Graphics/Atmosphere/AtmosphereProfile.h"
#include "Graphics/Atmosphere/AtmosphericFlowField.h"
#include "Graphics/Atmosphere/AtmosphericScatteringSolver.h"
#include "EngineContext/Math/LinearAlgebra_Float32.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t MaximumFogVolumeCount = 4u;   // [-] - Simultaneous local fog banks packed into the std140 UBO

//------------------------------------------------------------------------------------------------------------------------
//                                                           STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Global atmospheric height fog: σ_t(h) = GroundExtinction · exp(-max(0, h - FloorAltitude) / ScaleHeight). Flat below the base, so a base above
//    the viewer gives a uniform medium (the Beer-Lambert gate). Koschmieder visibility ≈ 3.912 / σ_t: 0.6 / km ≈ 6.5 km haze, 8 / km ≈ 500 m fog.
struct HeightFalloffProfile
{
    bool  HeightFogEnabled      = true;   // [-]    - Evaluates the global height fog when true
    float FloorAltitudeKm        = 0.0f;   // [km]   - Altitude below which the fog is uniform
    float GroundExtinctionPerKm = 0.6f;   // [1/km] - σ_t at and below the base altitude
    float ScaleHeightKm         = 0.12f;  // [km]   - e-folding thickness above the base
    float FlowVariation         = 0.45f;  // [-]    - Advected fBm thickness modulation in [0, 1] (0 = exact analytic profile)
    float VariationScaleKm      = 0.9f;   // [km]   - Horizontal size of the advected thickness patches
};

// 📝 One local fog bank: a soft ellipsoid (flow world metres, +Z up) with fBm-distorted edges. Its density field is evaluated at the backward-advected
//    parcel origin, so wind bends it, curl eddies and cyclones swirl it, updrafts lift it, and outflows hollow it — the bank stays anchored overall
//    (bounded-age advection) while the flow visibly moves through it.
struct FogVolumeDescriptor
{
    bool     ActivationEnabled  = false;                          // [-]    - Participates when true
    Vector3f WorldCentreMetres  { 0.0f, 0.0f, 40.0f };            // [m]    - Ellipsoid centre (+Z up)
    Vector3f HalfExtentMetres   { 1500.0f, 1500.0f, 140.0f };     // [m]    - Ellipsoid half-axes
    float    ExtinctionPerKm    = 6.0f;                           // [1/km] - σ_t in the bank core
    float    EdgeSoftness       = 0.45f;                          // [-]    - Fraction of the radius over which the bank fades out
    float    NoiseStrength      = 0.6f;                           // [-]    - fBm edge distortion + interior clumping in [0, 1]
    float    NoiseScaleMetres   = 320.0f;                         // [m]    - Clump size of the bank texture
};

// 📝 Complete volumetric fog configuration (both fog layers, optics, quality).
struct VolumetricFogConfiguration
{
    bool                 FogEnabled                     = true;   // [-]    - Master enable
    HeightFalloffProfile Height                         {};       // [-]    - Global atmospheric height fog
    FogVolumeDescriptor  Volumes[MaximumFogVolumeCount] {};       // [-]    - Local fog banks
    uint32_t             VolumeCount                    = 0u;     // [-]    - Populated slot count in [0..MaximumFogVolumeCount]
    float                SingleScatterAlbedo            = 0.97f;  // [-]    - ω_0 of the fog droplets / haze
    float                ScatteringDistribution         = 0.62f;  // [-]    - Forward HG lobe g (the sun-ward glow)
    float                BackwardDistribution           = -0.20f; // [-]    - Backward HG lobe g
    float                ForwardLobeWeight              = 0.85f;  // [-]    - Forward lobe blend in [0, 1]
    float                AmbientStrength                = 1.0f;   // [-]    - Sky ambient gain
    float                FlowCouplingScale              = 1.0f;   // [-]    - Gain of the flow condensation (lift thickens, outflow clears)
    float                AdvectionPeriodSeconds         = 90.0f;  // [s]    - Bounded parcel age: longer winds the fog further around eddies
    uint32_t             PrimaryStepCount               = 48u;    // [-]    - Exponentially distributed primary segments
    uint32_t             SunTransmittanceTapCount       = 6u;     // [-]    - SunTransmittanceMarch taps through the local banks
    float                MaximumTraceDistanceKm         = 30.0f;  // [km]   - Primary trace distance cap
};

// 📝 Per-sequence lighting shared by every fog sample (built once by PrepareFogLighting).
struct FogLightingContext
{
    LinearColour SunIlluminance  {};                   // [-] - Solar illuminance after the atmosphere at the viewer altitude
    LinearColour AmbientRadiance {};                   // [-] - Mean sky + ground radiance over the full sphere
    Vector3f     SunDirection    { 0.0f, 1.0f, 0.0f }; // [-] - Unit atmosphere-local solar vector (+Y up)
};

// 📝 Result of integrating the fog along one view ray.
struct FogRayResult
{
    LinearColour Radiance      {};                   // [-] - In-scattered fog radiance
    LinearColour Transmittance { 1.0f, 1.0f, 1.0f }; // [-] - Remaining transmittance to whatever lies behind the fog
};

// 📝 Exact std140 GPU uniform mirror for Shaders/VolumetricFogField.glsl (272 bytes).
struct FogVolumeUniformEntry
{
    float CentreSoftness[4];   // [m,-]    - xyz: world centre (+Z up); w: edge softness
    float ExtentExtinction[4]; // [m,1/km] - xyz: half-axes; w: core σ_t
    float NoisePack[4];        // [-,m]    - x: noise strength; y: noise scale; z: active (1/0); w: unused
};

struct FogFieldUniformBlock
{
    float                 HeightPack[4];   // [km,1/km,km,-] - x: floor altitude; y: ground σ_t; z: scale height; w: flow variation
    float                 OpticsPack[4];   // [km,-]         - x: variation scale; y: flow coupling; z: ω_0; w: ambient strength
    float                 PhasePack[4];    // [-]            - x: forward g; y: backward g; z: forward weight; w: primary step count
    float                 QualityPack[4];  // [-,km,s,-]     - x: sun taps; y: trace distance; z: sequence seconds; w: active volume count
    float                 FlowPack[4];     // [s,-]          - x: advection period; yzw: unused
    FogVolumeUniformEntry Volumes[MaximumFogVolumeCount]; // [-] - 4 x 48 B packed fog banks
};

static_assert(sizeof(FogFieldUniformBlock) == 272u, "FogFieldUniformBlock must remain 272 bytes for std140 parity.");

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Append one local fog bank; returns false when every slot is taken.
bool RegisterFogVolume(VolumetricFogConfiguration& Configuration, const FogVolumeDescriptor& Descriptor) noexcept;

// Exact ∫ σ_t dl [-] of the height fog over a straight segment whose altitude runs linearly from StartAltitudeKm to EndAltitudeKm.
[[nodiscard]] float IntegrateHeightFogSegment(
    const HeightFalloffProfile& Height,
    float                       StartAltitudeKm,
    float                       EndAltitudeKm,
    float                       LengthKm) noexcept;

// Exact height-fog optical depth [-] from AltitudeKm to space along a direction of up-cosine UpCosine (flat-layer closed form).
[[nodiscard]] float EvaluateHeightFogSunOpticalDepth(
    const HeightFalloffProfile& Height,
    float                       AltitudeKm,
    float                       UpCosine) noexcept;

// Dual-phase bounded-age backward advection of a flow-world point [m] (+Z up) with parcel ages ≤ PeriodSeconds: returns the 3D displacement to
// the parcel origin [m] and the flow condensation coupling (lift / convergence > 0 thickens, outflow / subsidence < 0 clears). Zero displacement
// without a flow field.
void ResolveFogAdvection(
    const AtmosphericFlowField* OptionalFlowField,
    const Vector3f&             WorldMetres,
    float                       SequenceSeconds,
    float                       PeriodSeconds,
    Vector3f&                   OutDisplacementMetres,
    float&                      OutCondensation) noexcept;

// Evaluate the total fog extinction σ_t [1/km] (height fog + every bank) at a flow-world point [m] (+Z up).
[[nodiscard]] float EvaluateFogDensity(
    const VolumetricFogConfiguration& Configuration,
    const AtmosphericFlowField*       OptionalFlowField,
    const Vector3f&                   WorldMetres,
    float                             SequenceSeconds) noexcept;

// Build the per-sequence sun + ambient lighting of the fog from the baked atmosphere tables (profile sun, viewer altitude).
[[nodiscard]] FogLightingContext PrepareFogLighting(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile) noexcept;

// Integrate the fog along LocalDirection from the profile viewer altitude, up to the ground, the trace cap or SceneDistanceKm (if > 0).
[[nodiscard]] FogRayResult IntegrateVolumetricFog(
    const VolumetricFogConfiguration& Configuration,
    const FogLightingContext&         Lighting,
    const AtmosphereUniformBlock&     Profile,
    const AtmosphericFlowField*       OptionalFlowField,
    const Vector3f&                   LocalDirection,
    float                             SequenceSeconds,
    float                             SceneDistanceKm,
    uint32_t                          PixelX,
    uint32_t                          PixelY,
    uint32_t                          SampleOrdinal) noexcept;

// Pack CPU VolumetricFogConfiguration into the std140 FogFieldUniformBlock for Vulkan UBO upload.
void EncodeFogFieldUniformBlock(
    const VolumetricFogConfiguration& Configuration,
    float                             SequenceSeconds,
    FogFieldUniformBlock&             OutUniforms) noexcept;

} // namespace Frontier

#endif
