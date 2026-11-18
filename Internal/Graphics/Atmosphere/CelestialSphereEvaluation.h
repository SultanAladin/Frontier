/*==============================================================================================================================================
                                                     CELESTIALSPHEREEVALUATION.H
==============================================================================================================================================*/
// 🧩 Astronomical celestial sphere evaluation: 120+ J2000 Hipparcos bright navigation stars, Galactic-plane-weighted Pogson stellar population,
//    Milky Way dust-extinguished star clouds, sub-pixel Fraunhofer Airy / Moffat optical PSF, and up to 6 simultaneous 3D moons with 2K cratered
//    Hapke/Lommel-Seeliger regolith and genuine 3D spherical Rayleigh+Mie lunar atmosphere radiative transfer.

#pragma once
#ifndef FRONTIER_GRAPHICS_ATMOSPHERE_CELESTIALSPHEREEVALUATION_H
#define FRONTIER_GRAPHICS_ATMOSPHERE_CELESTIALSPHEREEVALUATION_H

#include "EngineContext/Math/LinearAlgebra_Float32.h"
#include "Graphics/Atmosphere/RadiometricSkyEvaluation.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class LunarSurfaceCategory : uint8_t
{
    LunaRegolith  = 0u,   // [-] - Feldspathic highlands + basaltic maria (Earth's Moon)
    EmberBasalt   = 1u,   // [-] - Volcanic flood basalt + warm sulfur-dioxide aerosol haze
    GlacierCryo   = 2u,   // [-] - Fractured water-ice crust + nitrogen-methane Rayleigh envelope
    SulfurMantle  = 3u,   // [-] - Silicate-sulfur volcanic plains + golden plume envelope
    ShroudSlate   = 4u,   // [-] - Carbonaceous slate crust + deep cobalt-cyan stratospheric haze
    ShardSilicate = 5u,   // [-] - Pale cryovolcanic mineral crust + seafoam-teal nitrogen envelope
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Physical 3D spherical atmosphere parameters surrounding a single moon (integrated via Beer-Lambert + Rayleigh/Mie raymarch).
struct LunarAtmosphereProfile
{
    bool          EnvelopeEnabled              = true;                        // [-] - Enables 3D spherical shell ray-sphere integration
    float         TopAltitudeFraction          = 0.14f;                       // [-] - Outer atmosphere thickness (R_top - R_m) / R_m
    float         RayleighScaleHeightFraction  = 0.028f;                      // [-] - Molecular exponential scale height h_R / R_m
    float         MieScaleHeightFraction       = 0.011f;                      // [-] - Aerosol exponential scale height h_M / R_m
    SpectralColor RayleighOpticalDepthScale    { 0.18f, 0.44f, 1.05f };       // [-] - Dimensionless beta_R * R_m at surface
    float         MieScatteringDepthScale      = 0.12f;                       // [-] - Dimensionless beta_M,s * R_m at surface
    float         MieExtinctionDepthScale      = 0.14f;                       // [-] - Dimensionless beta_M,e * R_m at surface
    float         MieAsymmetry                 = 0.76f;                       // [-] - Cornette-Shanks aerosol forward-scatter g
    float         MultipleScatterApproximation = 0.18f;                       // [-] - Secondary isotropic twilight wrap inside shell
};

// 📝 Complete 3D geometric, surface, and atmospheric specification for one moon instance in the scene.
struct LunarBodyDescriptor
{
    bool                   VisibilityEnabled           = false;                            // [-]   - Participates when true
    LunarSurfaceCategory   SurfaceCategory             = LunarSurfaceCategory::LunaRegolith; // [-] - Selects 2K surface tile + mineral tint
    bool                   CustomBearingEnabled        = false;                            // [-]   - Uses CustomBearingScene directly when true
    Vector3f               CustomBearingScene          { 0.0f, 1.0f, 0.35f };              // [-]   - Unit observer->moon bearing in Z-up scene basis
    float                  AzimuthRadians              = 1.05f;                            // [rad] - Compass azimuth around +Z
    float                  ElevationRadians            = 0.42f;                            // [rad] - Altitude angle above XY plane
    float                  AngularRadiusRadians        = 0.0046f;                          // [rad] - Apparent solid radius (~0.26 deg for Luna)
    float                  DistanceKilometres          = 384400.0f;                        // [km]  - Depth-sort distance from observer
    float                  SurfaceRollRadians          = 0.0f;                             // [rad] - Tangent-plane roll angle
    float                  SurfacePitchRadians         = 0.0f;                             // [rad] - Selenographic latitude tilt
    float                  LongitudeOffsetRadians      = 0.0f;                             // [rad] - Prime-meridian longitude rotation
    float                  GeometricAlbedoScale        = 1.0f;                             // [x]   - Multiplier on physical regolith reflectance
    float                  CraterReliefStrength        = 1.0f;                             // [x]   - 3D normal-mapped terminator crater rim relief
    float                  EarthshineStrength          = 1.0f;                             // [x]   - Planet-reflected fill on unlit hemisphere
    bool                   CustomIlluminationEnabled   = false;                            // [-]   - Overrides scene sun vector for synodic phase showcase
    Vector3f               CustomSolarDirectionScene   { 0.7071f, -0.50f, 0.50f };         // [-]   - Optional moon->sun unit vector
    LunarAtmosphereProfile AtmosphereEnvelope          {};                                 // [-]   - Physical 3D spherical lunar atmosphere
};

// 📝 One J2000 Hipparcos / Yale Bright Star Catalogue entry.
struct BrightStarEntry
{
    const char* Designation         = "";      // [-]   - Astronomical star name
    float       RightAscensionHours = 0.0f;    // [h]   - J2000 equatorial right ascension [0..24)
    float       DeclinationDegrees  = 0.0f;    // [deg] - J2000 equatorial declination [-90..+90]
    float       ApparentMagnitude   = 2.0f;    // [mag] - Johnson V-band apparent visual magnitude
    float       ColorIndexBv        = 0.0f;    // [mag] - (B-V) color index (-0.33 hot O -> +1.85 cool M)
};

// 📝 Optical and astronomical parameters governing the nocturnal / outer-space starfield and Milky Way band.
struct StarfieldConfiguration
{
    bool  StarfieldEnabled             = true;      // [-]   - Master gate for stars + Milky Way
    bool  BrightCatalogueEnabled       = true;      // [-]   - Evaluates the J2000 bright navigation star table
    bool  GalacticStructureEnabled     = true;      // [-]   - Galactic latitude star concentration + dust-extinguished Milky Way
    bool  AtmosphericSeeingEnabled     = true;      // [-]   - Adds Moffat seeing skirt + airmass Kolmogorov scintillation inside atmosphere
    float LimitingMagnitude            = 7.1f;      // [mag] - Faintest resolved star magnitude
    float RadiometricFluxScale         = 1.0f;      // [x]   - Overall stellar photon flux calibration
    float ObserverLatitudeDegrees      = -26.19f;   // [deg] - Geographic latitude orienting the celestial sphere
    float LocalSiderealHours           = 5.75f;     // [h]   - Sidereal clock orienting J2000 right ascension
    float ApertureDiameterMillimetres  = 34.0f;     // [mm]  - Optical entrance pupil setting Fraunhofer Airy diffraction scale
    float SeeingFullWidthArcseconds    = 1.35f;     // ["]   - Atmospheric turbulence seeing FWHM at overhead airmass 1
};

// 📝 Output of extraterrestrial celestial evaluation before multiplying by Earth's atmospheric ViewTransmittance.
struct CelestialRayEvaluation
{
    SpectralColor ExtraterrestrialRadiance { 0.0f, 0.0f, 0.0f };   // [-] - Combined stars + Milky Way + moons (pre-Earth extinction)
    SpectralColor SolarOcclusionWeight     { 1.0f, 1.0f, 1.0f };   // [-] - Transmittance of moons in front of the distant sun (eclipse support)
    bool          SolidMoonHitCondition    = false;                // [-] - True when ray hit at least one solid lunar sphere
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Construct physically calibrated default LunarAtmosphereProfile for a given LunarSurfaceCategory.
[[nodiscard]] LunarAtmosphereProfile ConstructPresetLunarAtmosphere(LunarSurfaceCategory Category) noexcept;

// Convert stellar (B-V) index into effective temperature via Ballesteros (2012) and Planckian linear sRGB chromaticity.
[[nodiscard]] SpectralColor EvaluatePlanckianStarColor(float ColorIndexBv) noexcept;

// Retrieve the built-in J2000 bright navigation star table (120+ stars across Northern + Southern hemispheres).
[[nodiscard]] const std::vector<BrightStarEntry>& RetrieveBrightStarCatalogue();

// Resolve the Z-up scene unit direction toward a moon descriptor.
[[nodiscard]] Vector3f ResolveLunarBearingScene(const LunarBodyDescriptor& Moon) noexcept;

// Evaluate extraterrestrial stars, Milky Way dust lanes, and all visible 3D moons (with 3D spherical lunar atmospheres) along RayDirectionScene.
[[nodiscard]] CelestialRayEvaluation EvaluateCelestialSphereRay(const Vector3f&                         RayDirectionScene,
                                                                const RadiometricSkyConfiguration&      SkyConfiguration,
                                                                const std::vector<LunarBodyDescriptor>& Moons,
                                                                const StarfieldConfiguration&           StarConfiguration,
                                                                float                                   PixelAngularPitchRadians,
                                                                bool                                    VacuumSpaceCondition) noexcept;

} // namespace Frontier

#endif
