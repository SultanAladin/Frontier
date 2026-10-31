/*==========================================================================================================================================
                                               VOLUMETRIC CLOUD SEQUENCE
                                     ------------------------------------------------------------------
             Orthographic 256x256xR cloud shadow map projection, 3D Perlin-Worley global/local
                 cloud ray-marching, and realm- & depth-aware temporal bilateral reconstruction.
==========================================================================================================================================*/
// 🧩 Unit 3 of 5: Volumetric Clouds & Cloud Shadows GPU compute sequence.

#pragma once

#include "Engine/Rendering/AtmosphereLutSubmission.h"
#include "Engine/Rendering/EnvironmentRealmEnsemble.h"
#include "Engine/Rendering/VulkanComputeSubstrate.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

class VolumetricCloudSequence
{
  public:
    bool Initialize(VulkanComputeSubstrate& Substrate,
                    const std::string&      ShaderBinaryDirectory,
                    std::string*            OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeSubstrate& Substrate) noexcept;

    // Step 1: Projects 256x256xR orthographic optical-depth & transmittance map before G-buffer surface lighting & fog.
    bool ExecuteShadowProjections(VulkanComputeSubstrate&         Substrate,
                                  const VulkanBufferSlot&         EnsembleBuffer,
                                  const AtmosphereLutSubmission&  AtmosphereLuts,
                                  const EnvironmentRealmEnsemble& Ensemble,
                                  const VulkanImageSlot&          DummyGbuffer);

    // Step 2: Ray-marches global slabs + bounded local 3D cloud boxes and applies temporal bilateral reconstruction.
    bool ExecuteTraceAndReconstruct(VulkanComputeSubstrate&        Substrate,
                                    const VulkanBufferSlot&        EnsembleBuffer,
                                    const AtmosphereLutSubmission& AtmosphereLuts,
                                    const VulkanImageSlot&         GbufferArray,
                                    VulkanImageSlot&               HdrFrameTarget,
                                    uint32_t                       ViewportWidth,
                                    uint32_t                       ViewportHeight,
                                    uint32_t                       FrameIndex);

    [[nodiscard]] const VulkanImageSlot& CloudShadowArray() const noexcept { return CloudShadowArray_; }
    [[nodiscard]] const VulkanImageSlot& CloudWorkArray() const noexcept { return CloudWorkArray_; }

    bool ReadbackShadowLayer(VulkanComputeSubstrate& Substrate, uint32_t RealmIndex, std::vector<float>& OutRgba);
    bool ReadbackReconstructedLayer(VulkanComputeSubstrate& Substrate, std::vector<float>& OutRgba);

  private:
    VulkanComputeProgram ShadowProgram_{};
    VulkanComputeProgram TraceProgram_{};
    VulkanComputeProgram ReconstructProgram_{};
    VulkanImageSlot      CloudShadowArray_{};
    VulkanImageSlot      CloudWorkArray_{}; // Layer 0 = raw ray-march; Layer 1 = bilateral + temporal history
    bool                 HasTemporalHistory_ = false;
};

} // namespace Frontier
