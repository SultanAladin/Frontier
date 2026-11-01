/*==========================================================================================================================================
                                                SURFACE CARD RADIOSITY
                                     ------------------------------------------------------------------
             GPU Vulkan compute & CPU reference 6-axis RGB multi-bounce surface-card radiosity solver
             evaluating direct SDF-shadowed solar/sky/local exitance and Jacobi form-factor bounces
             B^(k+1) = E + g * ρ * F * B^(k) for GTX-first ReSTIR GI secondary-hit shading.
==========================================================================================================================================*/
// 🧩 GPU & CPU 6-axis surface-card multi-bounce radiosity solver and world-bounds patch generator.

#pragma once

#include "Engine/DeviceExchange/VulkanComputeExchange.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

struct SurfaceCardPatch
{
    std::array<float, 3> WorldCenter        = {0.0f, 0.0f, 0.0f}; // [m]
    float                SurfaceAreaM2      = 1.0f;               // [m^2]
    std::array<float, 3> WorldNormal        = {0.0f, 1.0f, 0.0f};
    float                SkyVisibility      = 1.0f;               // [0..1]
    std::array<float, 3> DiffuseReflectance = {0.5f, 0.5f, 0.5f}; // ρ in [0..0.95]^3
    float                InfluenceRadiusM   = 4.0f;               // [m]
    std::array<float, 3> SelfEmission       = {0.0f, 0.0f, 0.0f}; // [W / (sr * m^2)]
};

struct SurfaceCardLightingParameters
{
    float                IndirectBounceGain    = 1.0f;
    float                SkyVisibilityWeight   = 1.0f;
    uint32_t             BounceIterations      = 3u;
    bool                 EnableVisibilityCones = true;
    std::array<float, 3> SunDirectionUnit      = {0.42f, 0.78f, 0.46f};
    float                SunIlluminanceLux     = 4.5f;
    std::array<float, 3> SunRadianceTint       = {1.0f, 0.96f, 0.90f};
    float                SkyAmbientLuminance   = 0.35f;
    std::array<float, 3> SkyZenithRadiance     = {0.26f, 0.44f, 0.82f};
    std::array<float, 3> SkyHorizonRadiance    = {0.62f, 0.68f, 0.78f};
};

struct SurfaceCardSolveReport
{
    uint32_t CardCount             = 0u;
    uint32_t CompletedBounces      = 0u;
    double   GpuMilliseconds       = 0.0;
    double   CpuMilliseconds       = 0.0;
    float    MaxBounceResidual     = 0.0f;
    bool     GpuTimestampAvailable = false;
};

class SurfaceCardRadiositySolver
{
  public:
    bool Initialize(VulkanComputeExchange& Device, const std::string& ShaderSpvDirectory, std::string* OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeExchange& Device) noexcept;

    // Appends up to 6 axis-oriented patches (+X,-X,+Y,-Y,+Z,-Z) around an object world AABB.
    static void AppendSixAxisObjectCards(std::array<float, 3>             BoundsMin,
                                         std::array<float, 3>             BoundsMax,
                                         std::array<float, 3>             DiffuseReflectance,
                                         std::array<float, 3>             SelfEmission,
                                         uint32_t                         CardsPerObject,
                                         std::vector<SurfaceCardPatch>&   InOutPatches);

    // Appends subdivided upward ground radiosity patches so bounce light bleeds onto vehicle underbody & arches.
    static void AppendGroundRadiosityGrid(std::array<float, 2>           MinXZ,
                                          std::array<float, 2>           MaxXZ,
                                          float                          GroundY,
                                          uint32_t                       SubdivisionsPerAxis,
                                          std::array<float, 3>           GroundReflectance,
                                          std::vector<SurfaceCardPatch>& InOutPatches);

    bool UploadPatches(VulkanComputeExchange& Device, std::span<const SurfaceCardPatch> Patches);

    // Executes GPU direct irradiance injection + N Jacobi multi-bounce iterations on Vulkan compute.
    bool SolveGpuRadiosity(VulkanComputeExchange&               Device,
                           const DeviceMemorySpan&              DistanceVolumeSpan,
                           const DeviceMemorySpan&              LuminairesSpan,
                           uint32_t                             LuminaireCount,
                           const SurfaceCardLightingParameters& Parameters,
                           SurfaceCardSolveReport*              OutReport = nullptr);

    // Downloads solved per-card RGB exitance [W / (sr * m^2)] for verification & telemetry.
    bool DownloadSolvedExitance(const VulkanComputeExchange& Device, std::vector<std::array<float, 4>>& OutCardExitance) const;

    // Exact CPU Jacobi radiosity reference solver (used by CTest parity & convergence validation).
    static void SolveCpuReferenceRadiosity(std::span<const SurfaceCardPatch>    Patches,
                                           const SurfaceCardLightingParameters& Parameters,
                                           std::vector<std::array<float, 3>>&   OutDirectExitance,
                                           std::vector<std::array<float, 3>>&   OutSolvedExitance,
                                           std::vector<float>*                  OutBounceResiduals = nullptr);

    [[nodiscard]] const DeviceMemorySpan& DescriptorSpan() const noexcept { return DescriptorSpan_; }
    [[nodiscard]] const DeviceMemorySpan& SolvedExitanceSpan() const noexcept
    {
        return ActivePingIndex_ == 0u ? PingSpanA_ : PingSpanB_;
    }
    [[nodiscard]] ComputeDescriptorArgument SolvedExitanceArgument() const noexcept
    {
        return ComputeDescriptorArgument::Span(ActivePingIndex_ == 0u ? &PingSpanA_ : &PingSpanB_);
    }
    [[nodiscard]] uint32_t CardCount() const noexcept { return ActiveCardCount_; }
    [[nodiscard]] double   TotalCompileMilliseconds() const noexcept
    {
        return InjectProgram_.CompileMilliseconds + IterateProgram_.CompileMilliseconds;
    }

  private:
    ComputeProgramSlot InjectProgram_{};
    ComputeProgramSlot IterateProgram_{};
    DeviceMemorySpan   DescriptorSpan_{};
    DeviceMemorySpan   DirectExitanceSpan_{};
    DeviceMemorySpan   PingSpanA_{};
    DeviceMemorySpan   PingSpanB_{};
    uint32_t           ActiveCardCount_ = 0u;
    uint32_t           ActivePingIndex_ = 0u;
};

} // namespace Frontier
