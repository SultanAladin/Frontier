/*==========================================================================================================================================
                                           OPTICAL LENS FLARE SEQUENCE DEFINITION
==========================================================================================================================================*/
// 🧩 GPU 1x1-per-source solar disc visibility integration + multi-ghost chromatic aperture diffraction composite.

#include "Engine/Rendering/OpticalLensFlareSequence.h"

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

bool OpticalLensFlareSequence::Initialize(VulkanComputeSubstrate& Substrate,
                                          const std::string&      ShaderBinaryDirectory,
                                          std::string*            OutDiagnostic)
{
    const std::array<VkDescriptorType, 5> Bindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // Ensemble
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  // Gbuffer
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  // CloudWorkArray (Layer 1.w = Cloud transmittance)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  // FlareVisibility (N x 1 x 1 RGBA16F)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  // HdrFrame
    }};

    if (!Substrate.LoadComputeProgram(ShaderBinaryDirectory + "/EvaluateFlareVisibility.spv",
                                      "EvaluateFlareVisibilityMain",
                                      Bindings,
                                      sizeof(PushHeader),
                                      VisibilityProgram_,
                                      OutDiagnostic) ||
        !Substrate.LoadComputeProgram(ShaderBinaryDirectory + "/CompositeFlareAperture.spv",
                                      "CompositeFlareApertureMain",
                                      Bindings,
                                      sizeof(PushHeader),
                                      CompositeProgram_,
                                      OutDiagnostic))
    {
        return false;
    }

    return Substrate.AllocateRgba16fArray(16u, 1u, 1u, FlareVisibilityImage_);
}

void OpticalLensFlareSequence::Shutdown(VulkanComputeSubstrate& Substrate) noexcept
{
    Substrate.ReleaseImage(FlareVisibilityImage_);
    Substrate.ReleaseProgram(CompositeProgram_);
    Substrate.ReleaseProgram(VisibilityProgram_);
}

bool OpticalLensFlareSequence::Execute(VulkanComputeSubstrate&        Substrate,
                                       const VulkanBufferSlot&        EnsembleBuffer,
                                       const VulkanImageSlot&         GbufferArray,
                                       const VolumetricCloudSequence& CloudSequence,
                                       VulkanImageSlot&               HdrFrameTarget,
                                       uint32_t                       FlareSourceCount,
                                       uint32_t                       ViewportWidth,
                                       uint32_t                       ViewportHeight)
{
    if (FlareSourceCount == 0u)
    {
        return true;
    }
    if (FlareVisibilityImage_.Width < FlareSourceCount)
    {
        if (!Substrate.AllocateRgba16fArray(std::max(16u, FlareSourceCount), 1u, 1u, FlareVisibilityImage_))
        {
            return false;
        }
    }

    const PushHeader Push{{ViewportWidth, ViewportHeight, FlareSourceCount, 0u}, {0.0f, 0.0f, 0.0f, 0.0f}};
    const std::array<VulkanDescriptorArgument, 5> Args{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleBuffer, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &GbufferArray},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &CloudSequence.CloudWorkArray()},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &FlareVisibilityImage_},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &HdrFrameTarget},
    }};

    if (!Substrate.DispatchCompute(VisibilityProgram_, Args, &Push, sizeof(Push), FlareSourceCount, 1u, 1u))
    {
        return false;
    }

    return Substrate.DispatchCompute(
        CompositeProgram_, Args, &Push, sizeof(Push), (ViewportWidth + 7u) / 8u, (ViewportHeight + 7u) / 8u, 1u);
}

bool OpticalLensFlareSequence::ReadbackVisibilityValues(VulkanComputeSubstrate&            Substrate,
                                                        uint32_t                           FlareSourceCount,
                                                        std::vector<std::array<float, 4>>& OutVisibilities)
{
    OutVisibilities.clear();
    if (FlareSourceCount == 0u)
    {
        return true;
    }
    std::vector<float> Rgba;
    if (!Substrate.DownloadRgba16fLayer(FlareVisibilityImage_, 0u, Rgba))
    {
        return false;
    }
    const uint32_t Count = std::min<uint32_t>(FlareSourceCount, FlareVisibilityImage_.Width);
    OutVisibilities.resize(Count);
    for (uint32_t I = 0u; I < Count; ++I)
    {
        OutVisibilities[I] = {Rgba[I * 4u + 0u], Rgba[I * 4u + 1u], Rgba[I * 4u + 2u], Rgba[I * 4u + 3u]};
    }
    return true;
}

} // namespace Frontier
