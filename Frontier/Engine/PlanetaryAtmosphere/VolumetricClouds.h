/*==========================================================================================================================================
                                                   VOLUMETRIC CLOUDS
                                     ------------------------------------------------------------------
             Orthographic 256x256xR cloud shadow map projection, non-tiled 3D procedural global/local
                 cloud ray-marching, and realm- & depth-aware temporal bilateral reconstruction.
==========================================================================================================================================*/
// 🧩 Volumetric clouds & cloud shadows GPU compute sequence.

#pragma once

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/PlanetaryAtmosphere/SkyProjection.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

inline constexpr uint32_t kCloudShadowMapResolution = 256u;

class VolumetricClouds
{
  public:
    bool Initialize(VulkanComputeExchange& Exchange,
                    const std::string&     ShaderBinaryDirectory,
                    std::string*           OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeExchange& Exchange) noexcept;

    // Step 1: Projects 256x256xR orthographic optical-depth & transmittance map before surface lighting & fog.
    bool ExecuteShadowProjections(VulkanComputeExchange&      Exchange,
                                  const DeviceMemorySpan&     EnsembleSpan,
                                  const SkyProjection&        SkyLuts,
                                  uint32_t                    RealmCount,
                                  const DeviceImageArraySlot& PlaceholderVisibility);

    // Step 2: Ray-marches global slabs + bounded local 3D cloud boxes and applies temporal bilateral reconstruction.
    bool ExecuteTraceAndReconstruct(VulkanComputeExchange&      Exchange,
                                    const DeviceMemorySpan&     EnsembleSpan,
                                    const SkyProjection&        SkyLuts,
                                    const DeviceImageArraySlot& VisibilityImage,
                                    DeviceImageArraySlot&       HdrFrameTarget,
                                    uint32_t                    ViewportWidth,
                                    uint32_t                    ViewportHeight,
                                    uint32_t                    FrameIndex);

    [[nodiscard]] const DeviceImageArraySlot& CloudShadowArray() const noexcept { return CloudShadowArray_; }
    [[nodiscard]] const DeviceImageArraySlot& CloudWorkArray() const noexcept { return CloudWorkArray_; }

    bool ReadbackShadowLayer(VulkanComputeExchange& Exchange, uint32_t RealmIndex, std::vector<float>& OutRgba);
    bool ReadbackReconstructedLayer(VulkanComputeExchange& Exchange, std::vector<float>& OutRgba);

  private:
    ComputeProgramSlot   ShadowProgram_{};
    ComputeProgramSlot   TraceProgram_{};
    ComputeProgramSlot   ReconstructProgram_{};
    DeviceImageArraySlot CloudShadowArray_{};
    DeviceImageArraySlot CloudWorkArray_{}; // Layer 0 = raw ray-march; Layer 1 = bilateral + temporal history
    bool                 HasTemporalHistory_ = false;
};

} // namespace Frontier
