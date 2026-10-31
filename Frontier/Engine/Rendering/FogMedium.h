/*==========================================================================================================================================
                                                         FOG MEDIUM
                                     ------------------------------------------------------------------
                 Exponential height fog with an analytic ray integral (plan §8.1: FogMedium).
==========================================================================================================================================*/

// 🧩 Fog medium: density ρ(h) = ρ0 · exp(−max(h − hBase, 0)/H). The transmittance integral along a
//    straight ray through an exponential profile has a closed form, implemented exactly (with the
//    horizontal-ray limit) so no marching error ever enters the fog term. Colour application is the
//    standard lerp toward the fog (in-scatter) colour by 1 − T.

#pragma once

#include "AtmosphereModel.h"   // SpectrumRgb
#include "CloudField.h"        // CloudShadowProjection
#include "ClusterNesting.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace Frontier
{

struct FogParameters
{
    double      DensityAtBase = 0.02;    // [1/m] extinction at/below base height
    double      BaseHeightM   = 0.0;     // [m] full density below this height
    double      FalloffHeightM = 25.0;   // [m] exponential scale height above the base
    SpectrumRgb Colour{0.55, 0.62, 0.72};
};

// Transmittance along Origin + t·Direction (Direction unit), t in [0, DistanceM]. Z is up.
[[nodiscard]] double FogTransmittance(const FogParameters& P, SpatialVector Origin, SpatialVector Direction,
                                      double DistanceM) noexcept;

// L_in attenuated + fog colour gathered: L·T + C·(1 − T).
[[nodiscard]] SpectrumRgb ApplyFog(const FogParameters& P, SpectrumRgb Radiance, double Transmittance) noexcept;

struct FogRadianceIntegral
{
    SpectrumRgb Radiance{};
    double Transmittance = 1.0;
};

// Single scattering with midpoint incident light and exact exponential optical depth per segment.
// Incident supplies phase-weighted, shadowed radiance at a world position; no screen-space ray effect.
[[nodiscard]] FogRadianceIntegral IntegrateFogRadiance(
    const FogParameters& Medium,
    SpatialVector Origin,
    SpatialVector Direction,
    double DistanceM,
    uint32_t Steps,
    double ScatteringAlbedo,
    const std::function<SpectrumRgb(SpatialVector)>& Incident);

//------------------------------------------------------------------------------------------------------------------------
//              COUPLED HEIGHT FOG, AERIAL PERSPECTIVE & BOUNDED 3D NOISE-MODULATED VOLUMETRIC FOG
//------------------------------------------------------------------------------------------------------------------------

struct LocalFogVolumeProfile
{
    SpatialVector BoxCenterM            = {0.0, 0.0, 12.0};
    SpatialVector BoxHalfExtentsM       = {45.0, 45.0, 18.0};
    double        BoxEdgeFalloffM       = 6.0;
    double        ExtinctionPerM        = 0.035;   // [1/m] peak extinction inside bounded volume
    double        HeightFalloffM        = 18.0;    // [m] vertical exponential decay inside box
    double        ScatteringAlbedo      = 0.90;    // [-] single-scattering albedo
    double        PhaseAsymmetryG       = 0.62;    // [-] Henyey-Greenstein forward scattering
    SpectrumRgb   ScatteringTint        = {0.92, 0.96, 1.0};
    double        NoiseModulation       = 0.58;    // [0..1] 3D turbulent FBM density modulation
    double        NoiseScaleM           = 14.0;    // [m] spatial scale of 3D noise
    SpatialVector WindVelocityMps       = {1.8, 0.6, 0.25};
    double        WindTimeS             = 0.0;
    uint64_t      Seed                  = 0xF06B0ull;
};

struct PhotometricFogLuminaire
{
    SpatialVector          PositionM        = {0.0, 0.0, 4.0};
    SpatialVector          AxisUnit         = {0.0, 0.0, -1.0};
    SpectrumRgb            PeakIntensity    = {45.0, 38.0, 28.0};   // [W/sr]
    double                 RangeMeters      = 65.0;
    double                 InnerConeCos     = 0.92;
    double                 OuterConeCos     = 0.45;
    // 8-ring polar IES candela profile from cos(theta)=1 (on-axis) to cos(theta)=-1 (back)
    std::array<float, 8>   IesPolarWeights  = {1.0f, 0.94f, 0.78f, 0.52f, 0.22f, 0.06f, 0.01f, 0.0f};
};

struct LightningFogChannel
{
    SpatialVector SegmentStartM = {0.0, 0.0, 120.0};
    SpatialVector SegmentEndM   = {0.0, 0.0, 0.0};
    SpectrumRgb   LineRadiance  = {120.0, 155.0, 240.0};
    double        CoreRadiusM   = 1.5;
    double        ReachMeters   = 450.0;
};

struct RadiantFogFacet
{
    SpatialVector CornerA       = {-1.0, 0.0, 0.0};
    SpatialVector CornerB       = {1.0, 0.0, 0.0};
    SpatialVector CornerC       = {0.0, 0.0, 2.0};
    SpectrumRgb   SurfaceRadiance = {28.0, 11.0, 2.5};   // Fire / thermal / area source radiance
};

struct UnifiedVolumetricFogSpecification
{
    FogParameters                          HeightFog              = {};
    double                                 HeightFogAlbedo        = 0.88;
    double                                 HeightFogPhaseG        = 0.60;
    SpectrumRgb                            SkyAmbientRadiance     = {0.018, 0.024, 0.036};
    bool                                   EnableAerialPerspective = true;
    double                                 ObserverAltitudeKm     = 0.05;
    AtmosphereParameters                   Atmosphere             = {};
    const TransmittanceLut*                Transmittance          = nullptr;
    const MultiScatteringLut*              MultiScatter           = nullptr;
    const CloudShadowProjection*           CloudShadow            = nullptr;
    std::span<const AtmosphereSolarSource> SolarSources           = {};
    std::span<const LocalFogVolumeProfile> LocalVolumes           = {};
    std::span<const PhotometricFogLuminaire> Luminaires           = {};
    std::span<const LightningFogChannel>   LightningChannels      = {};
    std::span<const RadiantFogFacet>       RadiantFacets          = {};
    double                                 MaxFogMarchDistanceM   = 600.0;
    uint32_t                               FroxelSteps            = 32;
};

// Evaluates 3D turbulent density extinction [1/m] of a bounded local fog volume at PointMeters.
[[nodiscard]] double EvaluateLocalFogExtinction(const LocalFogVolumeProfile& Volume, SpatialVector PointMeters) noexcept;

// Evaluates coupled analytic height fog + surface-distance aerial perspective + bounded 3D volumetric fog.
[[nodiscard]] FogRadianceIntegral EvaluateUnifiedVolumetricFog(
    const UnifiedVolumetricFogSpecification& Specification,
    SpatialVector                            OriginMeters,
    SpatialVector                            DirectionUnit,
    double                                   SurfaceDistanceMeters,
    const std::function<bool(SpatialVector, SpatialVector, double)>& IsOccluded = {}) noexcept;

} // namespace Frontier
