/*==========================================================================================================================================
                                                 SOLAR SKY ENVIRONMENT
                                     ------------------------------------------------------------------
                 Multi-planet, multi-sun coupled atmospheric sky, 6-point Gauss-Legendre quadrature,
                           limb-darkened solar disc NEE, and cloud-slab transmittance.
==========================================================================================================================================*/
// 🧩 Coupled Bruneton/Hillaire atmospheric sky, limb-darkened multi-sun disc radiance, and cloud-slab transmittance.

#pragma once

#include "AtmosphereModel.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{

inline constexpr std::uint32_t SolarSkyPayloadFloats = 20u;

struct SolarSkySourceProfile
{
    SpatialVector SolarDirection     = {0.32, 0.86, 0.39799497};   // [-] unit vector toward solar disc centre
    SpectrumRgb   SolarIrradiance    = {18.5, 18.0, 17.2};         // [W/m^2] top-of-atmosphere solar beam
    double        SolarAngularRadius = 0.00465421;                 // [rad] apparent solar half-angle (~0.2667 deg)
    double        LimbDarkeningU1    = 0.397;                      // [-] linear limb-darkening coefficient
    double        LimbDarkeningU2    = 0.196;                      // [-] quadratic limb-darkening coefficient
};

struct SolarSkySpecification
{
    AtmosphereParameters               Atmosphere           = {};
    SpatialVector                      LocalUpDirection     = {0.0, 0.0, 1.0};   // [-] local planet-up unit vector
    double                             ObserverAltitudeKm   = 0.05;              // [km] observer height above surface
    double                             MultipleScatterScale = 1.0;               // [-] second-order scattering multiplier
    double                             CloudBottomKm        = 1.6;               // [km] lower altitude of cloud slab
    double                             CloudTopKm           = 2.4;               // [km] upper altitude of cloud slab
    double                             CloudExtinctionKm    = 0.0;               // [1/km] peak slab optical extinction
    double                             CloudCoverage        = 0.0;               // [-] spatial cloud coverage [0, 1]
    std::vector<SolarSkySourceProfile> SolarSources         = {SolarSkySourceProfile{}};
};

//------------------------------------------------------------------------------------------------------------------------
//                                           PAYLOAD COMPILATION AND VALIDATION
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::array<float, SolarSkyPayloadFloats> CompileSolarSkyPayload(
    const SolarSkySpecification& Specification,
    std::size_t                  SourceIndex = 0u);

void ValidateSolarSkyPayload(std::span<const float> Payload);

//------------------------------------------------------------------------------------------------------------------------
//                                    FP64 ORACLE & 6-POINT GAUSS-LEGENDRE QUADRATURE
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] double EvaluateSolarSkyCloudTransmittance(const SolarSkySpecification& Specification,
                                                        SpatialVector                OriginMeters,
                                                        SpatialVector                Direction) noexcept;

// High-precision FP64 reference continuum sky radiance (default 128..256 steps) summed across all active solar sources.
[[nodiscard]] SpectrumRgb EvaluateSolarSkyOracleContinuum(const SolarSkySpecification& Specification,
                                                          SpatialVector                OriginMeters,
                                                          SpatialVector                Direction,
                                                          std::uint32_t                Steps = 128u) noexcept;

// Fast 6-segment / quadratic-warped 6-point Gauss-Legendre sky continuum matching Shaders/SolarSkyEnvironment.slang.
[[nodiscard]] SpectrumRgb EvaluateSolarSkyGaussLegendre6Pt(const SolarSkySpecification& Specification,
                                                           SpatialVector                OriginMeters,
                                                           SpatialVector                Direction) noexcept;

// Limb-darkened solar disc radiance summed across all active solar sources in the specification.
[[nodiscard]] SpectrumRgb EvaluateSolarSkyDiscRadiance(const SolarSkySpecification& Specification,
                                                       SpatialVector                OriginMeters,
                                                       SpatialVector                Direction,
                                                       double*                      OutPrimarySolidAnglePdf = nullptr,
                                                       std::uint32_t                Steps = 48u) noexcept;

[[nodiscard]] SpectrumRgb EvaluateSolarSkyTotalRadiance(const SolarSkySpecification& Specification,
                                                        SpatialVector                OriginMeters,
                                                        SpatialVector                Direction) noexcept;

// Coupled solar disc direct irradiance NEE quadrature on a receiver surface normal across all active solar sources.
[[nodiscard]] SpectrumRgb IntegrateSolarSkyDiscIrradiance(const SolarSkySpecification& Specification,
                                                          SpatialVector                OriginMeters,
                                                          SpatialVector                SurfaceNormal,
                                                          std::uint32_t                RadialRings    = 32u,
                                                          std::uint32_t                AzimuthSectors = 64u) noexcept;

} // namespace Frontier
