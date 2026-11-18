/*==============================================================================================================================================
                                                       ATMOSPHERICSCATTERINGSOLVER.H
==============================================================================================================================================*/
// 🧩 Exact Bruneton-2008 / Hillaire-2020 spherical atmosphere radiative-transfer solver: limb-aware transmittance LUT, 64-direction
//    infinite-order multi-scatter LUT, geometric-limb-centered sky-view LUT (with Fresnel oceanic lower hemisphere), Neckel-Labs 1994
//    wavelength-dependent solar photosphere limb darkening + smooth K/F corona, force-field upper-sky veil, and full orbital planet view.

#pragma once
#ifndef FRONTIER_GRAPHICS_ATMOSPHERE_ATMOSPHERICSCATTERINGSOLVER_H
#define FRONTIER_GRAPHICS_ATMOSPHERE_ATMOSPHERICSCATTERINGSOLVER_H

#include "Graphics/Atmosphere/AtmosphereProfile.h"
#include "Graphics/Atmosphere/AtmosphericFlowField.h"
#include "EngineContext/Math/LinearAlgebra_Float32.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                           STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Precomputed dimensionless transmittance (256x64), isotropic infinite-bounce multi-scatter Ψ_ms (32x32), and calibrated
//    limb-centered sky-view radiance (256x128) tables — 1:1 CPU mathematical twin of Transmittance.comp / MultiScatter.comp / SkyView.frag.
struct AtmosphericLookupTables
{
    std::vector<float> TransmittanceRgb;              // [-] - TransmittanceExtentX * TransmittanceExtentY * 3 floats
    std::vector<float> MultiScatterRgb;               // [-] - MultiScatterExtentX  * MultiScatterExtentY  * 3 floats
    std::vector<float> SkyViewRgb;                    // [-] - SkyViewExtentX       * SkyViewExtentY       * 3 floats
    float              BakedViewerRadiusKm   = 0.0f;  // [km] - Viewer planetary radius the SkyView LUT was baked at
    bool               FoundationTablesReady = false; // [-] - True once Transmittance + MultiScatter are baked
    bool               SkyViewReady          = false; // [-] - True once SkyView is baked for current solar vector
};

// 📝 Complete result of orbital / high-altitude spherical atmosphere + planet surface integration along one view ray.
struct OrbitalAtmosphereResult
{
    LinearColour CombinedRadiance        {};                   // [-] - Combined planetary surface + atmospheric radiance (linear HDR)
    LinearColour AtmosphereTransmittance { 1.0f, 1.0f, 1.0f }; // [-] - Remaining transmittance to background celestial space (0 if hit planet)
    bool         PlanetSurfaceHit        = false;              // [-] - True when the view ray intersected the solid/ocean planet sphere
    float        MinimumRayAltitudeKm    = 0.0f;               // [km] - Closest approach altitude above BottomRadius along the ray
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Convert a scene world direction (+Z up) into atmosphere-local coordinates (+Y up), mirroring SkyDome.frag.
[[nodiscard]] inline Vector3f ConvertWorldToAtmosphereAxis(const Vector3f& WorldAxis) noexcept
{
    return NormalizeVector(Vector3f{ WorldAxis.XCoord, WorldAxis.ZCoord, WorldAxis.YCoord });
}

// Convert an atmosphere-local direction (+Y up) back into scene world coordinates (+Z up).
[[nodiscard]] inline Vector3f ConvertAtmosphereToWorldAxis(const Vector3f& LocalAxis) noexcept
{
    return NormalizeVector(Vector3f{ LocalAxis.XCoord, LocalAxis.ZCoord, LocalAxis.YCoord });
}

// Test whether a ray originating at planetary radius RadiusKm with local polar cosine PolarCosine intersects BottomRadiusKm.
[[nodiscard]] bool QueryGroundSphereIntersection(
    float RadiusKm,
    float PolarCosine,
    float BottomRadiusKm) noexcept;

// Evaluate Rayleigh scattering, Mie scattering, and combined Rayleigh + Mie + Chappuis-ozone extinction [1/m] at AltitudeKm.
void EvaluateMediumCoefficients(
    const AtmosphereUniformBlock& Profile,
    float                         AltitudeKm,
    LinearColour&                 OutRayleighScatter,
    LinearColour&                 OutMieScatter,
    LinearColour&                 OutTotalExtinction) noexcept;

// Evaluate Rayleigh molecular phase P_R(cos θ) and Cornette-Shanks aerosol phase P_M(cos θ, g).
[[nodiscard]] float EvaluateRayleighPhaseFunction(float CosineAngle) noexcept;
[[nodiscard]] float EvaluateCornetteShanksPhaseFunction(float CosineAngle, float Asymmetry) noexcept;

// Bake sun-independent Transmittance (256x64) and infinite-order MultiScatter Ψ_ms (32x32) tables from Profile.
void SynthesizeAtmosphereFoundationTables(
    const AtmosphereUniformBlock& Profile,
    AtmosphericLookupTables&      OutTables);

// Bake sun- and altitude-dependent limb-centered SkyView (256x128) radiance table from Profile and BaseTables.
void SynthesizeSkyViewTable(
    const AtmosphereUniformBlock& Profile,
    AtmosphericLookupTables&      InOutTables);

// Sample transmittance T(r, μ) from planetary radius RadiusKm to outer space along polar cosine PolarCosine (0 if occluded by planet).
[[nodiscard]] LinearColour SampleTransmittanceToSpace(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    float                          RadiusKm,
    float                          PolarCosine) noexcept;

// Sample transmittance T(p0 -> p1) across a finite segment of DistanceKm inside the atmosphere (for clouds & aerial perspective).
[[nodiscard]] LinearColour SampleSegmentTransmittance(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    float                          StartRadiusKm,
    float                          PolarCosine,
    float                          DistanceKm) noexcept;

// Sample infinite-order isotropic multiple scattering Ψ_ms(r, μ_s) from the 32x32 table.
[[nodiscard]] LinearColour SampleMultiScatterFactor(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    float                          RadiusKm,
    float                          SolarCosine) noexcept;

// Sample baked SkyView radiance for a unit atmosphere-local direction (+Y up), seam-free across azimuth and geometric limb.
[[nodiscard]] LinearColour SampleSkyViewRadiance(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    const Vector3f&                LocalDirection) noexcept;

// Integrate exact aerial-perspective in-scattered radiance and transmittance from StartPositionKm over SegmentDistanceKm.
[[nodiscard]] LinearColour IntegrateAtmosphericSegment(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    const Vector3f&                StartPositionKm,
    const Vector3f&                LocalDirection,
    float                          SegmentDistanceKm,
    uint32_t                       StepCount,
    LinearColour&                  OutTransmittance) noexcept;

// Evaluate Neckel & Labs (1994) wavelength-dependent limb-darkened solar photosphere + smooth monotonic K/F corona & Mie aureole.
[[nodiscard]] LinearColour EvaluateSolarRadiometricDisc(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    const Vector3f&                LocalDirection,
    float                          SolarAngularRadiusRadians,
    float                          SolarDiscIntensity,
    float                          ViewerAltitudeKm,
    bool                           VacuumSpaceCondition = false) noexcept;

// Evaluate the radiant-sun glare veil (optical bloom + clean-air aureole, Atmosphere::EvaluateSolarGlareSpread). Strength 0 = muted
// sun (returns zero), 1 = radiant. Composite it OVER clouds and terrain, scaled by the sun's own visibility (cloud transmittance
// toward the sun) — glare is scattered inside the optics, so per-pixel occluders do not cut it.
[[nodiscard]] LinearColour EvaluateSolarGlareRadiance(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    const Vector3f&                LocalDirection,
    float                          SolarAngularRadiusRadians,
    float                          SolarGlareStrength,
    float                          ViewerAltitudeKm,
    bool                           VacuumSpaceCondition = false) noexcept;

// Evaluate subtle physical upper-troposphere / stratospheric aerosol & airglow streams advected by AtmosphericFlowField.
[[nodiscard]] LinearColour EvaluateFlowAdvectedSkyVeil(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    const AtmosphericFlowField&    FlowField,
    const Vector3f&                LocalDirection,
    float                          SequenceSeconds,
    LinearColour&                  InOutTransmittance) noexcept;

// Evaluate full 3D curved planetary atmosphere, sunlit biomes, Cox-Munk ocean glint, force-field cloud strata, terminator, and night lights.
[[nodiscard]] OrbitalAtmosphereResult EvaluateOrbitalPlanetRadiance(
    const AtmosphericLookupTables& Tables,
    const AtmosphereUniformBlock&  Profile,
    const Vector3f&                CameraPlanetPositionKm,
    const Vector3f&                LocalDirection,
    const AtmosphericFlowField*    OptionalFlowField,
    float                          CloudCoverage,
    float                          SequenceSeconds,
    uint32_t                       AtmosphereStepCount = 48u) noexcept;

} // namespace Frontier

#endif
