/*==============================================================================================================================================
                                                      RADIOMETRICSKYEVALUATION.H
==============================================================================================================================================*/
// 🧩 Physical Hillaire-2020 / Bruneton spherical atmospheric radiative transfer: planet-shadowed transmittance LUT, 64-direction isotropic
//    multiple-scattering LUT, horizon-centered sky-view LUT, Neckel-Labs limb-darkened solar photosphere with continuous coronal/aureole
//    falloff, ForceField-advected cirrus/aerosol sky strata, and full orbital outer-space planetary limb + surface evaluation.

#pragma once
#ifndef FRONTIER_GRAPHICS_ATMOSPHERE_RADIOMETRICSKYEVALUATION_H
#define FRONTIER_GRAPHICS_ATMOSPHERE_RADIOMETRICSKYEVALUATION_H

#include "EngineContext/Math/LinearAlgebra_Float32.h"
#include "Graphics/Atmosphere/AtmosphereProfile.h"
#include "Simulation/ForceField/AtmosphericForceField.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Unbounded linear RGB spectral radiance / transmittance triplet.
struct SpectralColor
{
    float Red   = 0.0f;   // [-] - Linear red channel (~650 nm)
    float Green = 0.0f;   // [-] - Linear green channel (~550 nm)
    float Blue  = 0.0f;   // [-] - Linear blue channel (~450 nm)
};

// 📝 Solar photosphere rendering character. The disk itself is always the Neckel-Labs limb-darkened photosphere; this selects how the
//    continuous circumsolar envelope around it is distributed, which is what a viewer reads as "the kind of sun".
enum class SolarDiscCharacter : uint8_t
{
    SolarPhotosphere   = 0u,   // [-] - Balanced physical sun: photosphere + Thomson K-corona + aerosol aureole (default)
    GoldenHaze         = 1u,   // [-] - Low-sun / hazy air: broad warm aureole, the disk stays soft and slightly bloated
    DiamondCorona      = 2u,   // [-] - High, crisp air: tight steep corona, a hard diamond-sharp disk with a compact glare
    OvercastDiffuse    = 3u,   // [-] - Thin overcast: no hard disk, an extended diffuse luminous patch (forward-scattered)
};

// 📝 Physical planetary and atmospheric parameters driving both LUT bakes and direct spherical raymarching.
struct RadiometricSkyConfiguration
{
    float         BottomRadiusKilometres      = 6360.0f;                         // [km]  - Planetary solid/ocean radius
    float         TopRadiusKilometres         = 6460.0f;                         // [km]  - Outer exobase boundary
    SpectralColor RayleighScatteringPerKm     { 0.005802f, 0.013558f, 0.033100f }; // [1/km] - Molecular Rayleigh scattering at sea level
    float         RayleighScaleHeightKm       = 8.0f;                            // [km]  - Rayleigh exponential scale height
    float         MieScatteringPerKm          = 0.003996f;                       // [1/km] - Aerosol Mie scattering at sea level
    float         MieExtinctionPerKm          = 0.004440f;                       // [1/km] - Aerosol Mie total extinction at sea level
    float         MieScaleHeightKm            = 1.2f;                            // [km]  - Aerosol exponential scale height
    float         MieAsymmetry                = 0.80f;                           // [-1..1] - Cornette-Shanks forward anisotropy
    SpectralColor OzoneAbsorptionPerKm        { 0.000650f, 0.001881f, 0.000085f }; // [1/km] - Chappuis ozone peak absorption
    float         OzonePeakAltitudeKm         = 25.0f;                           // [km]  - Stratospheric ozone layer center
    float         OzoneHalfWidthKm            = 15.0f;                           // [km]  - Ozone tent half-width (10..40 km)
    float         MultipleScatteringGain      = 1.0f;                            // [0..2] - Isotropic multiple-scattering series weight

    Vector3f      SolarDirectionScene         { 0.50f, 0.50f, 0.70710678f };     // [-]   - Unit surface->sun vector in Z-up scene coordinates
    SpectralColor ExtraterrestrialSolarFlux   { 12.8f, 12.35f, 12.10f };         // [-]   - Calibrated extraterrestrial solar illuminance
    float         SolarAngularRadiusRadians   = 0.00465f;                        // [rad] - Apparent solar photosphere half-angle (~0.266 deg)
    float         SolarDiscRadianceMultiplier = 1.0f;                            // [x]   - Relative photosphere radiance scale
    SolarDiscCharacter SolarCharacter         = SolarDiscCharacter::SolarPhotosphere; // [-] - Solar envelope character

    float         ObserverAltitudeMetres      = 18.0f;                           // [m]   - Camera altitude above BottomRadiusKilometres
    float         HighAltitudeCirrusCoverage  = 0.32f;                           // [0..1] - Upper-tropospheric ForceField-advected cirrus veil
    float         TroposphericHazeStrength    = 0.25f;                           // [0..2] - Boundary-layer ForceField-advected Mie streamers
    float         AdvectionSpanSeconds        = 180.0f;                          // [s]   - Lagrangian streamline lookback window
    float         SequenceSeconds             = 0.0f;                            // [s]   - Simulation clock for ForceField + orbital drift
    float         PlanetaryCloudCoverage      = 0.52f;                           // [0..1] - Outer-space planetary cloud deck coverage
    bool          OrbitalPlanetSurfaceEnabled = false;                           // [-]   - Shades continents/ocean/clouds on planet sphere hits
};

// 📝 Precomputed Hillaire-2020 lookup tables shared across pixels for fast transmittance and isotropic multiple scattering.
struct RadiometricSkyTables
{
    static constexpr uint32_t TransmittanceWidth  = 256u;
    static constexpr uint32_t TransmittanceHeight = 64u;
    static constexpr uint32_t MultiScatterWidth   = 32u;
    static constexpr uint32_t MultiScatterHeight  = 32u;
    static constexpr uint32_t SkyViewWidth        = 384u;
    static constexpr uint32_t SkyViewHeight       = 192u;

    std::vector<SpectralColor> TransmittanceTexels {};   // [-] - 256x64 horizon-parameterized T(r, mu)
    std::vector<SpectralColor> MultiScatterTexels  {};   // [-] - 32x32 isotropic series Psi_ms(r, mu_s)
    std::vector<SpectralColor> SkyViewTexels       {};   // [-] - 384x192 limb-centered clear-sky radiance LUT
    bool                       TablesReadyCondition = false;
};

// 📝 Per-integration baked 2D Lagrangian aerosol/cirrus advection offset field for the sky strata (Nubis-style wind-offset texture).
//    Baked once per integration on a coarse polar grid: U from the boundary-layer Mie layer (0.12..2.8 km) and V from the upper-tropospheric
//    ice-cirrus stratum (8.4..11.6 km), each stroking the exact characteristic through the AtmosphericWindField.
struct SkyAdvectionOffsetField
{
    static constexpr uint32_t CellCount = 128u;   // [-] - Polar samples around the full azimuth circle

    bool     ReadyCondition     = false;   // [-] - True once the offset field is baked
    float    OuterRadiusKm      = 640.0f;  // [km] - Radial extent of the baked annulus (covers the visible atmosphere)
    float    LayerSplitKm       = 8.0f;    // [km] - Altitude separating the haze lookup from the cirrus lookup
    float    LowLagX[CellCount]   {};      // [m] - Boundary-layer medium Lagrangian X offset per azimuth cell
    float    LowLagY[CellCount]   {};      // [m] - Boundary-layer medium Lagrangian Y offset per azimuth cell
    float    HighLagX[CellCount]  {};      // [m] - Cirrus medium Lagrangian X offset per azimuth cell
    float    HighLagY[CellCount]  {};      // [m] - Cirrus medium Lagrangian Y offset per azimuth cell
};

// 📝 Result of marching one view ray through the curved planetary atmosphere (and planetary globe when in orbit).
struct SkyRaymarchResult
{
    SpectralColor InscatteredRadiance          { 0.0f, 0.0f, 0.0f };   // [-]  - Total foreground atmospheric + planetary surface radiance
    SpectralColor ViewTransmittance            { 1.0f, 1.0f, 1.0f };   // [-]  - Remaining transmittance to background space (0 if planet hit)
    float         IntegratedMieOpticalDepth    = 0.0f;                 // [-]  - Line-of-sight aerosol optical depth driving circumsolar aureole
    float         PlanetContactDistanceKm      = -1.0f;                // [km] - Positive distance to BottomRadius when ray hits planet
    bool          PlanetSurfaceHitCondition    = false;                // [-]  - True when solid/ocean planetary sphere occludes background space
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    INLINE ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline SpectralColor AddSpectral(const SpectralColor& Left, const SpectralColor& Right) noexcept
{
    return SpectralColor{ Left.Red + Right.Red, Left.Green + Right.Green, Left.Blue + Right.Blue };
}

[[nodiscard]] inline SpectralColor MultiplySpectral(const SpectralColor& Left, const SpectralColor& Right) noexcept
{
    return SpectralColor{ Left.Red * Right.Red, Left.Green * Right.Green, Left.Blue * Right.Blue };
}

[[nodiscard]] inline SpectralColor ScaleSpectral(const SpectralColor& Color, float Factor) noexcept
{
    return SpectralColor{ Color.Red * Factor, Color.Green * Factor, Color.Blue * Factor };
}

[[nodiscard]] inline float EvaluateLuminance(const SpectralColor& Color) noexcept
{
    return 0.2126f * Color.Red + 0.7152f * Color.Green + 0.0722f * Color.Blue;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Translate an engine AtmosphereUniformBlock (std140 GPU profile) into a RadiometricSkyConfiguration.
[[nodiscard]] RadiometricSkyConfiguration ConstructSkyConfigurationFromProfile(const AtmosphereUniformBlock& Profile) noexcept;

// Bake the 256x64 Transmittance LUT, 32x32 64-direction Multi-Scatter LUT, and 384x192 limb-centered Sky-View LUT.
void BakeRadiometricSkyTables(RadiometricSkyTables&              Tables,
                              const RadiometricSkyConfiguration& Configuration);

// Sample planet-shadowed transmittance T(r, mu) from RadiusKilometres toward space along VerticalCosine; returns 0 inside Earth shadow.
[[nodiscard]] SpectralColor EvaluateTransmittanceToSpace(const RadiometricSkyTables&        Tables,
                                                         const RadiometricSkyConfiguration& Configuration,
                                                         float                              RadiusKilometres,
                                                         float                              VerticalCosine) noexcept;

// Sample the prebaked 384x192 limb-centered Sky-View LUT along a Z-up scene direction.
[[nodiscard]] SpectralColor SampleSkyViewTable(const RadiometricSkyTables& Tables,
                                               const RadiometricSkyConfiguration& Configuration,
                                               const Vector3f& RayDirectionScene) noexcept;

// Evaluate exact spherical Rayleigh + Mie + Ozone + Multi-Scatter radiative transfer along a ray, including ForceField-advected
// cirrus/aerosol sky motion, cool ocean Fresnel lower-hemisphere perspective, and full outer-space planetary globe rendering.
[[nodiscard]] SkyRaymarchResult EvaluateSphericalAtmosphereRay(const RadiometricSkyTables&        Tables,
                                                               const RadiometricSkyConfiguration& Configuration,
                                                               const Vector3f&                    CameraSceneMetres,
                                                               const Vector3f&                    RayDirectionScene,
                                                               const AtmosphericWindField*        OptionalWindField        = nullptr,
                                                               const SkyAdvectionOffsetField*     OptionalAdvectionField  = nullptr) noexcept;

// Bake the coarse azimuthal Lagrangian offset field for the ForceField-advected haze + cirrus strata (call once per integration).
void BakeSkyAdvectionOffsetField(SkyAdvectionOffsetField&           Field,
                                 const RadiometricSkyConfiguration& Configuration,
                                 const AtmosphericWindField*        OptionalWindField) noexcept;

// Evaluate the Neckel & Labs (1994) wavelength-limb-darkened solar photosphere plus continuous small-angle Mie diffraction /
// Thomson K-corona falloff, attenuated by atmospheric ViewTransmittance along the exact pixel ray.
[[nodiscard]] SpectralColor EvaluateSolarPhotosphereCorona(const RadiometricSkyConfiguration& Configuration,
                                                           const SkyRaymarchResult&           AtmosphereAlongRay,
                                                           const Vector3f&                    RayDirectionScene,
                                                           float                              PixelAngularPitchRadians) noexcept;

// Apply Khronos PBR Neutral tone mapping (matching RadianceResolve.frag 1:1) after linear exposure scaling.
[[nodiscard]] SpectralColor ApplyPbrNeutralTonemap(const SpectralColor& LinearRadiance, float ExposureScale = 1.0f) noexcept;

} // namespace Frontier

#endif
