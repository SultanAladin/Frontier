/*==========================================================================================================================================
                                            VOLUMETRIC CLOUD SEQUENCE DEFINITION
==========================================================================================================================================*/
// 🧩 Orthographic 256x256xR CloudShadowArray + 3D Perlin-Worley ray-march + temporal bilateral reconstruction.

#include "Engine/Rendering/VolumetricCloudSequence.h"

#include <algorithm>
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

bool VolumetricCloudSequence::Initialize(VulkanComputeSubstrate& Substrate,
                                         const std::string&      ShaderBinaryDirectory,
                                         std::string*            OutDiagnostic)
{
    const std::array<VkDescriptorType, 5> Bindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    }};

    if (!Substrate.LoadComputeProgram(ShaderBinaryDirectory + "/ProjectCloudShadow.spv",
                                      "ProjectCloudShadowMain",
                                      Bindings,
                                      sizeof(PushHeader),
                                      ShadowProgram_,
                                      OutDiagnostic) ||
        !Substrate.LoadComputeProgram(ShaderBinaryDirectory + "/TraceVolumetricClouds.spv",
                                      "TraceVolumetricCloudsMain",
                                      Bindings,
                                      sizeof(PushHeader),
                                      TraceProgram_,
                                      OutDiagnostic) ||
        !Substrate.LoadComputeProgram(ShaderBinaryDirectory + "/ReconstructVolumetricClouds.spv",
                                      "ReconstructVolumetricCloudsMain",
                                      Bindings,
                                      sizeof(PushHeader),
                                      ReconstructProgram_,
                                      OutDiagnostic))
    {
        return false;
    }

    return Substrate.AllocateRgba16fArray(kCloudShadowMapResolution, kCloudShadowMapResolution, 1u, CloudShadowArray_) &&
           Substrate.AllocateRgba16fArray(8u, 8u, 2u, CloudWorkArray_);
}

void VolumetricCloudSequence::Shutdown(VulkanComputeSubstrate& Substrate) noexcept
{
    Substrate.ReleaseImage(CloudWorkArray_);
    Substrate.ReleaseImage(CloudShadowArray_);
    Substrate.ReleaseProgram(ReconstructProgram_);
    Substrate.ReleaseProgram(TraceProgram_);
    Substrate.ReleaseProgram(ShadowProgram_);
    HasTemporalHistory_ = false;
}

bool VolumetricCloudSequence::ExecuteShadowProjections(VulkanComputeSubstrate&         Substrate,
                                                       const VulkanBufferSlot&         EnsembleBuffer,
                                                       const AtmosphereLutSubmission&  AtmosphereLuts,
                                                       const EnvironmentRealmEnsemble& Ensemble,
                                                       const VulkanImageSlot&          DummyGbuffer)
{
    const uint32_t RealmCount = std::max<uint32_t>(1u, static_cast<uint32_t>(Ensemble.Realms.size()));
    if (CloudShadowArray_.ArrayLayers < RealmCount)
    {
        if (!Substrate.AllocateRgba16fArray(kCloudShadowMapResolution, kCloudShadowMapResolution, RealmCount, CloudShadowArray_))
        {
            return false;
        }
    }

    for (size_t RIdx = 0u; RIdx < Ensemble.Realms.size(); ++RIdx)
    {
        const PushHeader Push{{kCloudShadowMapResolution, kCloudShadowMapResolution, static_cast<uint32_t>(RIdx), 0u},
                              {0.0f, 0.0f, 0.0f, 0.0f}};
        const std::array<VulkanDescriptorArgument, 5> Args{{
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleBuffer, nullptr},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.TransmittanceArray()},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &DummyGbuffer},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &CloudShadowArray_},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &CloudWorkArray_},
        }};
        if (!Substrate.DispatchCompute(ShadowProgram_,
                                       Args,
                                       &Push,
                                       sizeof(Push),
                                       (kCloudShadowMapResolution + 7u) / 8u,
                                       (kCloudShadowMapResolution + 7u) / 8u,
                                       1u))
        {
            return false;
        }
    }
    return true;
}

bool VolumetricCloudSequence::ExecuteTraceAndReconstruct(VulkanComputeSubstrate&        Substrate,
                                                         const VulkanBufferSlot&        EnsembleBuffer,
                                                         const AtmosphereLutSubmission& AtmosphereLuts,
                                                         const VulkanImageSlot&         GbufferArray,
                                                         VulkanImageSlot&               HdrFrameTarget,
                                                         uint32_t                       ViewportWidth,
                                                         uint32_t                       ViewportHeight,
                                                         uint32_t                       FrameIndex)
{
    if (CloudWorkArray_.Width != ViewportWidth || CloudWorkArray_.Height != ViewportHeight)
    {
        if (!Substrate.AllocateRgba16fArray(ViewportWidth, ViewportHeight, 2u, CloudWorkArray_) ||
            !Substrate.ClearRgba16fArray(CloudWorkArray_, {0.0f, 0.0f, 0.0f, 1.0f}))
        {
            return false;
        }
        HasTemporalHistory_ = false;
    }

    const PushHeader TracePush{{ViewportWidth, ViewportHeight, FrameIndex, 0u}, {0.0f, 0.0f, 0.0f, 0.0f}};
    const std::array<VulkanDescriptorArgument, 5> Args{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleBuffer, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.TransmittanceArray()},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &GbufferArray},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &CloudWorkArray_},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &HdrFrameTarget},
    }};
    if (!Substrate.DispatchCompute(TraceProgram_,
                                   Args,
                                   &TracePush,
                                   sizeof(TracePush),
                                   (ViewportWidth + 7u) / 8u,
                                   (ViewportHeight + 7u) / 8u,
                                   1u))
    {
        return false;
    }

    const uint32_t   UseHistory = (HasTemporalHistory_ && FrameIndex > 0u) ? 1u : 0u;
    const PushHeader ReconPush{{ViewportWidth, ViewportHeight, FrameIndex, UseHistory}, {0.0f, 0.0f, 0.0f, 0.0f}};
    if (!Substrate.DispatchCompute(ReconstructProgram_,
                                   Args,
                                   &ReconPush,
                                   sizeof(ReconPush),
                                   (ViewportWidth + 7u) / 8u,
                                   (ViewportHeight + 7u) / 8u,
                                   1u))
    {
        return false;
    }

    HasTemporalHistory_ = true;
    return true;
}

bool VolumetricCloudSequence::ReadbackShadowLayer(VulkanComputeSubstrate& Substrate,
                                                  uint32_t                RealmIndex,
                                                  std::vector<float>&     OutRgba)
{
    return Substrate.DownloadRgba16fLayer(CloudShadowArray_, RealmIndex, OutRgba);
}

bool VolumetricCloudSequence::ReadbackReconstructedLayer(VulkanComputeSubstrate& Substrate, std::vector<float>& OutRgba)
{
    return Substrate.DownloadRgba16fLayer(CloudWorkArray_, 1u, OutRgba);
}

} // namespace Frontier
