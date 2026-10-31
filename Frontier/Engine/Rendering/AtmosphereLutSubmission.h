/*==========================================================================================================================================
                                               ATMOSPHERE LUT SUBMISSION
                                     ------------------------------------------------------------------
             Dirty-gated per-realm RGBA16F 2D-Array compute bakes: Transmittance (256x64xR),
                         Multi-Scattering (32x32xR), and Sky-View (192x108xR).
==========================================================================================================================================*/
// 🧩 Unit 1 of 5: Physical Atmosphere & Twilight GPU LUT submission.

#pragma once

#include "Engine/Rendering/EnvironmentRealmEnsemble.h"
#include "Engine/Rendering/VulkanComputeSubstrate.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

class AtmosphereLutSubmission
{
  public:
    bool Initialize(VulkanComputeSubstrate& Substrate,
                    const std::string&      ShaderBinaryDirectory,
                    std::string*            OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeSubstrate& Substrate) noexcept;

    bool Execute(VulkanComputeSubstrate&         Substrate,
                 const VulkanBufferSlot&         EnsembleBuffer,
                 const EnvironmentRealmEnsemble& Ensemble);

    [[nodiscard]] const VulkanImageSlot& TransmittanceArray() const noexcept { return TransmittanceArray_; }
    [[nodiscard]] const VulkanImageSlot& MultiScatteringArray() const noexcept { return MultiScatteringArray_; }
    [[nodiscard]] const VulkanImageSlot& SkyViewArray() const noexcept { return SkyViewArray_; }

    [[nodiscard]] uint32_t LastAtmosphereBakedCount() const noexcept { return LastAtmosphereBakedCount_; }
    [[nodiscard]] uint32_t LastSkyViewBakedCount() const noexcept { return LastSkyViewBakedCount_; }

    bool ReadbackTransmittanceLayer(VulkanComputeSubstrate& Substrate, uint32_t RealmIndex, std::vector<float>& OutRgba);
    bool ReadbackMultiScatteringLayer(VulkanComputeSubstrate& Substrate, uint32_t RealmIndex, std::vector<float>& OutRgba);
    bool ReadbackSkyViewLayer(VulkanComputeSubstrate& Substrate, uint32_t RealmIndex, std::vector<float>& OutRgba);

  private:
    VulkanComputeProgram  TransmittanceProgram_{};
    VulkanComputeProgram  MultiScatterProgram_{};
    VulkanComputeProgram  SkyViewProgram_{};
    VulkanImageSlot       TransmittanceArray_{};
    VulkanImageSlot       MultiScatteringArray_{};
    VulkanImageSlot       SkyViewArray_{};
    std::vector<uint64_t> BakedAtmosphereRevisions_{};
    std::vector<uint64_t> BakedSkyViewRevisions_{};
    uint32_t              LastAtmosphereBakedCount_ = 0u;
    uint32_t              LastSkyViewBakedCount_    = 0u;
};

} // namespace Frontier
