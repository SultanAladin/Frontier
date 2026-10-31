/*==========================================================================================================================================
                                               VOLUMETRIC CLOUDS DEFINITION
==========================================================================================================================================*/
// 🧩 Orthographic 256x256xR CloudShadowArray + 3D procedural ray-march + temporal bilateral reconstruction.

#include "Engine/PlanetaryAtmosphere/VolumetricClouds.h"

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

bool VolumetricClouds::Initialize(VulkanComputeExchange& Exchange,
                                  const std::string&     ShaderBinaryDirectory,
                                  std::string*           OutDiagnostic)
{
    const std::array<VkDescriptorType, 5> Bindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    }};

    if (!Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/ProjectCloudShadow.spv",
                                     "ProjectCloudShadowMain",
                                     Bindings,
                                     sizeof(PushHeader),
                                     ShadowProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/TraceVolumetricClouds.spv",
                                     "TraceVolumetricCloudsMain",
                                     Bindings,
                                     sizeof(PushHeader),
                                     TraceProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/ReconstructVolumetricClouds.spv",
                                     "ReconstructVolumetricCloudsMain",
                                     Bindings,
                                     sizeof(PushHeader),
                                     ReconstructProgram_,
                                     OutDiagnostic))
    {
        return false;
    }

    return Exchange.AllocateRgba16fArray(kCloudShadowMapResolution, kCloudShadowMapResolution, 1u, CloudShadowArray_) &&
           Exchange.AllocateRgba16fArray(8u, 8u, 2u, CloudWorkArray_);
}

void VolumetricClouds::Shutdown(VulkanComputeExchange& Exchange) noexcept
{
    Exchange.ReleaseImage(CloudWorkArray_);
    Exchange.ReleaseImage(CloudShadowArray_);
    Exchange.ReleaseProgram(ReconstructProgram_);
    Exchange.ReleaseProgram(TraceProgram_);
    Exchange.ReleaseProgram(ShadowProgram_);
    HasTemporalHistory_ = false;
}

bool VolumetricClouds::ExecuteShadowProjections(VulkanComputeExchange&      Exchange,
                                                const DeviceMemorySpan&     EnsembleSpan,
                                                const AtmosphereProjection& AtmosphereLuts,
                                                uint32_t                    RealmCount,
                                                const DeviceImageArraySlot& PlaceholderVisibility)
{
    const uint32_t LayerCount = std::max<uint32_t>(1u, RealmCount);
    if (CloudShadowArray_.ArrayLayers < LayerCount)
    {
        if (!Exchange.AllocateRgba16fArray(kCloudShadowMapResolution, kCloudShadowMapResolution, LayerCount, CloudShadowArray_))
        {
            return false;
        }
    }

    for (uint32_t RealmIdx = 0u; RealmIdx < RealmCount; ++RealmIdx)
    {
        const PushHeader Push{{kCloudShadowMapResolution, kCloudShadowMapResolution, RealmIdx, 0u},
                              {0.0f, 0.0f, 0.0f, 0.0f}};
        const std::array<ComputeDescriptorArgument, 5> Args{{
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleSpan, nullptr},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.TransmittanceArray()},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &PlaceholderVisibility},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &CloudShadowArray_},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &CloudWorkArray_},
        }};
        if (!Exchange.DispatchCompute(ShadowProgram_,
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

bool VolumetricClouds::ExecuteTraceAndReconstruct(VulkanComputeExchange&      Exchange,
                                                  const DeviceMemorySpan&     EnsembleSpan,
                                                  const AtmosphereProjection& AtmosphereLuts,
                                                  const DeviceImageArraySlot& VisibilityImage,
                                                  DeviceImageArraySlot&       HdrFrameTarget,
                                                  uint32_t                    ViewportWidth,
                                                  uint32_t                    ViewportHeight,
                                                  uint32_t                    FrameIndex)
{
    if (CloudWorkArray_.Width != ViewportWidth || CloudWorkArray_.Height != ViewportHeight)
    {
        if (!Exchange.AllocateRgba16fArray(ViewportWidth, ViewportHeight, 2u, CloudWorkArray_) ||
            !Exchange.ClearRgba16fArray(CloudWorkArray_, {0.0f, 0.0f, 0.0f, 1.0f}))
        {
            return false;
        }
        HasTemporalHistory_ = false;
    }

    const PushHeader TracePush{{ViewportWidth, ViewportHeight, FrameIndex, 0u}, {0.0f, 0.0f, 0.0f, 0.0f}};
    const std::array<ComputeDescriptorArgument, 5> Args{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleSpan, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.TransmittanceArray()},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &VisibilityImage},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &CloudWorkArray_},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &HdrFrameTarget},
    }};
    if (!Exchange.DispatchCompute(TraceProgram_,
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
    if (!Exchange.DispatchCompute(ReconstructProgram_,
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

bool VolumetricClouds::ReadbackShadowLayer(VulkanComputeExchange& Exchange,
                                           uint32_t               RealmIndex,
                                           std::vector<float>&    OutRgba)
{
    return Exchange.DownloadRgba16fLayer(CloudShadowArray_, RealmIndex, OutRgba);
}

bool VolumetricClouds::ReadbackReconstructedLayer(VulkanComputeExchange& Exchange, std::vector<float>& OutRgba)
{
    return Exchange.DownloadRgba16fLayer(CloudWorkArray_, 1u, OutRgba);
}

} // namespace Frontier
