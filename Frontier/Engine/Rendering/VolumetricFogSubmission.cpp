/*==========================================================================================================================================
                                            VOLUMETRIC FOG SUBMISSION DEFINITION
==========================================================================================================================================*/
// 🧩 Dispatches IntegrateVolumetricFogMain across all active realms and cross-portal rays.

#include "Engine/Rendering/VolumetricFogSubmission.h"

#include <array>

namespace Frontier
{
namespace
{
struct PushHeader
{
    uint32_t Counts[4];
    float    Tuning[4];
};
} // namespace

bool VolumetricFogSubmission::Initialize(VulkanComputeSubstrate& Substrate,
                                         const std::string&      ShaderBinaryDirectory,
                                         std::string*            OutDiagnostic)
{
    const std::array<VkDescriptorType, 6> Bindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // Ensemble
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // TransLut
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // MultiLut
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // CloudShadowLut
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // Gbuffer
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // HdrFrame
    }};

    return Substrate.LoadComputeProgram(ShaderBinaryDirectory + "/IntegrateVolumetricFog.spv",
                                        "IntegrateVolumetricFogMain",
                                        Bindings,
                                        sizeof(PushHeader),
                                        Program_,
                                        OutDiagnostic);
}

void VolumetricFogSubmission::Shutdown(VulkanComputeSubstrate& Substrate) noexcept
{
    Substrate.ReleaseProgram(Program_);
}

bool VolumetricFogSubmission::Execute(VulkanComputeSubstrate&        Substrate,
                                      const VulkanBufferSlot&        EnsembleBuffer,
                                      const AtmosphereLutSubmission& AtmosphereLuts,
                                      const VolumetricCloudSequence& CloudSequence,
                                      const VulkanImageSlot&         GbufferArray,
                                      VulkanImageSlot&               HdrFrameTarget,
                                      uint32_t                       ViewportWidth,
                                      uint32_t                       ViewportHeight)
{
    const PushHeader Push{{ViewportWidth, ViewportHeight, 0u, 0u}, {0.0f, 0.0f, 0.0f, 0.0f}};
    const std::array<VulkanDescriptorArgument, 6> Args{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleBuffer, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.TransmittanceArray()},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.MultiScatteringArray()},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &CloudSequence.CloudShadowArray()},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &GbufferArray},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &HdrFrameTarget},
    }};
    return Substrate.DispatchCompute(
        Program_, Args, &Push, sizeof(Push), (ViewportWidth + 7u) / 8u, (ViewportHeight + 7u) / 8u, 1u);
}

} // namespace Frontier
