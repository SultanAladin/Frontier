/*==========================================================================================================================================
                                                         CLOUD FIELD
                                     ------------------------------------------------------------------
                 Deterministic value-noise density slab + volumetric raymarch (single-scatter
                      sun with shadow march) — plan §8.1: CloudField/CloudRecords/CloudTrace.
==========================================================================================================================================*/

// 🧩 Cloud field: a horizontal density slab between base/top heights, shaped by coverage and a
//    4-octave value-noise FBM whose lattice hash is integer PCG-style mixing — bit-identical on
//    every platform for a given seed (integer hashes, no transcendentals in the hash path).
//    The marcher integrates Beer-Lambert transmittance and accumulates sun in-scattering with a
//    short secondary march toward the sun (self-shadowing), Henyey-Greenstein phase. A uniform-
//    density override exists so validation can hold the marcher to closed-form Beer-Lambert.

#pragma once

#include "AtmosphereModel.h"   // SpectrumRgb
#include "ClusterNesting.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{

struct CloudParameters
{
    double   BaseHeightM     = 800.0;    // [m]
    double   TopHeightM      = 1800.0;   // [m]
    double   Coverage        = 0.55;     // [-] 0 = clear, 1 = overcast
    double   DensityScale    = 1.0;      // [-] multiplier on shaped density
    double   ExtinctionPerM  = 0.012;    // [1/m] at density 1
    double   PhaseG          = 0.35;     // [-] forward-scatter asymmetry (droplets, mildly forward here)
    uint64_t Seed            = 0xC10D5ull;
    double   WindTimeS       = 0.0;      // [s] field advection clock (offsets the noise lattice)
    double   WindXMps        = 6.0;      // [m/s]
    double   WindYMps        = 2.0;      // [m/s]
    SpectrumRgb AmbientRadiance{}; // [-] optional uniform isotropic incident radiance; zero preserves sun-only model
    double   UniformDensityOverride = -1.0;   // [-] >= 0: ignore noise/coverage (validation + debug views)
};

// Shaped density [0..~1.2] at a world point (Z up, metres). Zero outside the slab.
[[nodiscard]] double CloudDensity(const CloudParameters& P, SpatialVector Point) noexcept;

struct CloudMarchResult
{
    SpectrumRgb Radiance{};         // in-scattered sun radiance along the ray
    double      Transmittance = 1.0;
};

// Marches Origin + t·Direction (unit), t in [0, MaxDistanceM], through the slab.
[[nodiscard]] CloudMarchResult MarchClouds(const CloudParameters& P, SpatialVector Origin, SpatialVector Direction,
                                           SpatialVector SunDirection, const SpectrumRgb& SunRadiance,
                                           double MaxDistanceM = 40000.0, uint32_t Steps = 64,
                                           uint32_t SunSteps = 6) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                      MULTI-VOLUME PERLIN-WORLEY CLOUDS, BEERS-POWDER & ORTHO SHADOW PROJECTION
//------------------------------------------------------------------------------------------------------------------------

enum class CloudVolumeDomain : uint8_t
{
    PlanetarySlab   = 0u,   // Horizontal altitude slab [LowerAltitudeM, UpperAltitudeM]
    BoundedLocalBox = 1u    // Bounded 3D volume [BoxCenterM ± BoxHalfExtentsM] with smooth edge falloff
};

struct CloudVolumeProfile
{
    CloudVolumeDomain Domain                = CloudVolumeDomain::PlanetarySlab;
    CloudParameters   Slab                  = {};
    SpatialVector     BoxCenterM            = {0.0, 0.0, 420.0};
    SpatialVector     BoxHalfExtentsM       = {450.0, 450.0, 180.0};
    double            BoxEdgeFalloffM       = 65.0;
    double            WorleyErosionStrength = 0.42;   // [-] 0 = pure FBM, >0 = Perlin-Worley cellular cauliflower erosion
    double            PowderStrength        = 0.85;   // [-] Beers-Powder dark-edge sugar-powder multiplier
    double            MultiScatterStrength  = 0.55;   // [-] 2-octave Hillaire/Schneider multi-scattering gain
    double            BackscatterPhaseG     = -0.25;  // [-] dual-lobe HG backscatter asymmetry
};

struct CloudSolarBeam
{
    SpatialVector Direction   = {0.0, 0.70710678, 0.70710678};
    SpectrumRgb   Radiance    = {18.5, 18.0, 17.2};
};

struct VolumetricCloudSample
{
    SpectrumRgb Radiance{};
    double      Transmittance     = 1.0;
    double      MeanHitDistanceM  = 0.0;
};

// Evaluates 3D Perlin-Worley + FBM density for a planetary slab or bounded local cloud box.
[[nodiscard]] double EvaluateCloudVolumeDensity(const CloudVolumeProfile& Volume, SpatialVector PointMeters) noexcept;

// Evaluates Beers-Powder multi-scattering energy along optical depth Tau toward a solar source.
[[nodiscard]] double EvaluateBeersPowderEnergy(double OpticalDepth,
                                               double CosAngle,
                                               double ForwardPhaseG,
                                               double BackscatterPhaseG,
                                               double PowderStrength,
                                               double MultiScatterStrength) noexcept;

// Ray-marches arbitrary N global + bounded local cloud volumes illuminated by arbitrary M solar beams.
[[nodiscard]] VolumetricCloudSample MarchCloudVolumes(std::span<const CloudVolumeProfile> Volumes,
                                                      SpatialVector                       OriginMeters,
                                                      SpatialVector                       DirectionUnit,
                                                      std::span<const CloudSolarBeam>     SolarBeams,
                                                      const SpectrumRgb&                  SkyAmbientRadiance,
                                                      double                              MaxDistanceM = 40000.0,
                                                      uint32_t                            Steps        = 64,
                                                      uint32_t                            ShadowSteps  = 6) noexcept;

inline constexpr uint32_t kCloudShadowMapResolution = 256u;

// Orthographic cloud shadow optical-depth & transmittance projection sampled by direct sun, fog, and GI.
class CloudShadowProjection
{
  public:
    bool Bake(std::span<const CloudVolumeProfile> Volumes,
              SpatialVector                       AnchorMeters,
              SpatialVector                       SolarDirectionUnit,
              double                              HalfSpanMeters  = 4096.0,
              uint32_t                            Resolution      = 256,
              uint32_t                            MarchSteps      = 24);

    [[nodiscard]] double        SampleTransmittance(SpatialVector WorldPointMeters) const noexcept;
    [[nodiscard]] double        SampleOpticalDepth(SpatialVector WorldPointMeters) const noexcept;
    [[nodiscard]] bool          Baked() const noexcept { return !OpticalDepthTexels_.empty(); }
    [[nodiscard]] uint32_t      Resolution() const noexcept { return Resolution_; }
    [[nodiscard]] double        HalfSpanMeters() const noexcept { return HalfSpanM_; }
    [[nodiscard]] SpatialVector SolarDirection() const noexcept { return SolarDirection_; }
    [[nodiscard]] const std::vector<float>& OpticalDepthTexels() const noexcept { return OpticalDepthTexels_; }

  private:
    std::vector<float> OpticalDepthTexels_;
    uint32_t           Resolution_      = 0;
    double             HalfSpanM_       = 4096.0;
    SpatialVector      AnchorM_         = {};
    SpatialVector      SolarDirection_  = {0.0, 0.0, 1.0};
    SpatialVector      OrthoAxisU_      = {1.0, 0.0, 0.0};
    SpatialVector      OrthoAxisV_      = {0.0, 1.0, 0.0};
};

} // namespace Frontier
