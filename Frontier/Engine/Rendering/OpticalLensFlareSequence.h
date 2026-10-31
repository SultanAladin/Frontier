/*==========================================================================================================================================
                                              OPTICAL LENS FLARE SEQUENCE
                                     ------------------------------------------------------------------
             1x1-per-source GPU solar disc visibility reduction (reversed-Z depth, portal window
             clipping, volumetric cloud transmittance & dielectric glass tint) + multi-element
             chromatic ghosts, spectral halo ring, streaks, and polygonal aperture diffraction.
==========================================================================================================================================*/
// 🧩 Unit 4 of 5: Optical Lens Flare GPU visibility & aperture composite sequence.

#pragma once

#include "Engine/Rendering/VolumetricCloudSequence.h"
#include "Engine/Rendering/VulkanComputeSubstrate.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

class OpticalLensFlareSequence
{
  public:
    bool Initialize(VulkanComputeSubstrate& Substrate,
                    const std::string&      ShaderBinaryDirectory,
                    std::string*            OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeSubstrate& Substrate) noexcept;

    bool Execute(VulkanComputeSubstrate&        Substrate,
                 const VulkanBufferSlot&        EnsembleBuffer,
                 const VulkanImageSlot&         GbufferArray,
                 const VolumetricCloudSequence& CloudSequence,
                 VulkanImageSlot&               HdrFrameTarget,
                 uint32_t                       FlareSourceCount,
                 uint32_t                       ViewportWidth,
                 uint32_t                       ViewportHeight);

    [[nodiscard]] const VulkanImageSlot& FlareVisibilityImage() const noexcept { return FlareVisibilityImage_; }

    // Reads back GPU-evaluated (VisR, VisG, VisB, ScalarVisibility) for each active flare candidate.
    bool ReadbackVisibilityValues(VulkanComputeSubstrate&               Substrate,
                                  uint32_t                              FlareSourceCount,
                                  std::vector<std::array<float, 4>>&    OutVisibilities);

  private:
    VulkanComputeProgram VisibilityProgram_{};
    VulkanComputeProgram CompositeProgram_{};
    VulkanImageSlot      FlareVisibilityImage_{};
};

} // namespace Frontier
