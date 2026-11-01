/*==========================================================================================================================================
                                                SURFACE RADIOSITY ATLAS
                                     ------------------------------------------------------------------
                 Six-axis oriented surface patch capture, multi-bounce radiosity solver, & GPU float4 table packing
==========================================================================================================================================*/
// 🧩 Captures 6-axis (+/-X, +/-Y, +/-Z) diffuse surface cards per opaque instance and solves multi-bounce form-factor
//    radiosity B = R_0 + rho * F * B on both CPU reference and Vulkan 1.3 Slang compute (SolveSurfaceRadiosityMain).

#pragma once

#include "Engine/Geometry/GeometryProcessing/RangeDistanceAtlas.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{

struct SurfaceCardBuildInstance
{
    std::array<float, 3> LowerWorldMeters{0.0f, 0.0f, 0.0f};      // [m]
    std::array<float, 3> UpperWorldMeters{1.0f, 1.0f, 1.0f};      // [m]
    std::array<float, 3> DiffuseReflectance{0.5f, 0.5f, 0.5f};    // [-] - linear albedo in [0, 0.95]
    std::array<float, 3> SelfRadiance{0.0f, 0.0f, 0.0f};          // [W/(m^2*sr)]
};

struct SurfaceCardLuminaireInput
{
    uint32_t             Category       = 0u;                     // [-] - 0=DirectionalSun, 1=Point, 2=RectArea
    std::array<float, 3> PositionOrDir  = {0.0f, 1.0f, 0.0f};     // [m or -]
    std::array<float, 3> RadianceOrFlux = {1.0f, 1.0f, 1.0f};     // [W/(m^2*sr) or W/sr]
    float                RangeOrArea    = 25.0f;                  // [m or m^2]
};

struct SurfacePatchEntry
{
    std::array<float, 3> CenterMeters{0.0f, 0.0f, 0.0f};          // [m]
    float                AreaSquareMeters = 1.0f;                 // [m^2]
    std::array<float, 3> NormalUnit{0.0f, 1.0f, 0.0f};            // [-]
    float                ExtentRadiusMeters = 1.0f;               // [m]
    std::array<float, 3> DiffuseReflectance{0.5f, 0.5f, 0.5f};    // [-]
    std::array<float, 3> SelfRadiance{0.0f, 0.0f, 0.0f};          // [W/(m^2*sr)]
    std::array<float, 3> SolvedRadiosity{0.0f, 0.0f, 0.0f};       // [W/(m^2*sr)]
};

struct RadiositySolveMetrics
{
    uint32_t PatchCount        = 0u;
    uint32_t ExecutedBounces   = 0u;
    float    InitialResidual   = 0.0f;
    float    FinalResidual     = 0.0f;
    float    MeanRadiosityLuma = 0.0f;
};

class SurfaceRadiosityAtlas
{
  public:
    static constexpr uint32_t MaximumPatchCount = 256u;

    SurfaceRadiosityAtlas() = default;

    void BuildPatchesFromInstances(std::span<const SurfaceCardBuildInstance> Instances);

    // Deterministic CPU multi-bounce solver (verifies convergence & initializes fallback radiosity)
    RadiositySolveMetrics SolveCpuMultiBounce(std::span<const SurfaceCardLuminaireInput> Luminaires,
                                              const std::array<float, 3>&                SkyZenithRadiance,
                                              float                                      SkyWeight,
                                              float                                      IndirectGain,
                                              uint32_t                                   BounceCount,
                                              const RangeDistanceAtlas*                  OptionalClipmap = nullptr);

    [[nodiscard]] uint32_t                         PatchCount() const noexcept { return static_cast<uint32_t>(Patches_.size()); }
    [[nodiscard]] std::span<const SurfacePatchEntry> Patches() const noexcept { return Patches_; }

    [[nodiscard]] std::array<float, 3> SampleRadiosity(const std::array<float, 3>& WorldMeters,
                                                       const std::array<float, 3>& NormalUnit) const noexcept;

    // Packs static patch geometry (4 * float4 per patch) and initial/solved radiosity (1 * float4 per patch) for GPU compute
    void PackForGpu(std::vector<std::array<float, 4>>& OutPatchGeometryFloat4,
                    std::vector<std::array<float, 4>>& OutPatchRadiosityFloat4) const;

  private:
    std::vector<SurfacePatchEntry> Patches_{};
};

} // namespace Frontier
