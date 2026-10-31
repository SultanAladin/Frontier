/*==========================================================================================================================================
                                               VOLUMETRIC FOG SUBMISSION
                                     ------------------------------------------------------------------
             Analytic closed-form exponential height fog + planet-curved aerial perspective +
             bounded 3D noise-modulated froxel volumetric fog scattering solar shafts, IES, lightning & fire.
==========================================================================================================================================*/
// 🧩 Unit 5 of 5: Height, Aerial & Local Volumetric Fog GPU compute submission.

#pragma once

#include "Engine/Rendering/AtmosphereLutSubmission.h"
#include "Engine/Rendering/VolumetricCloudSequence.h"
#include "Engine/Rendering/VulkanComputeSubstrate.h"

#include <cstdint>
#include <string>

namespace Frontier
{

class VolumetricFogSubmission
{
  public:
    bool Initialize(VulkanComputeSubstrate& Substrate,
                    const std::string&      ShaderBinaryDirectory,
                    std::string*            OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeSubstrate& Substrate) noexcept;

    bool Execute(VulkanComputeSubstrate&        Substrate,
                 const VulkanBufferSlot&        EnsembleBuffer,
                 const AtmosphereLutSubmission& AtmosphereLuts,
                 const VolumetricCloudSequence& CloudSequence,
                 const VulkanImageSlot&         GbufferArray,
                 VulkanImageSlot&               HdrFrameTarget,
                 uint32_t                       ViewportWidth,
                 uint32_t                       ViewportHeight);

  private:
    VulkanComputeProgram Program_{};
};

} // namespace Frontier
