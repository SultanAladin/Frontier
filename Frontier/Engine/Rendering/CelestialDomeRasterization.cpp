/*==========================================================================================================================================
                                           CELESTIAL DOME RASTERIZATION DEFINITION
==========================================================================================================================================*/
// 🧩 Uploads 2048x1024x6 sRGB Lunar Atlas + 32x32 Hipparcos octahedral star index and executes CelestialDome.slang.

#include "Engine/Rendering/CelestialDomeRasterization.h"

#include <array>
#include <cmath>
#include <vector>

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

bool CelestialDomeRasterization::Initialize(VulkanComputeSubstrate& Substrate,
                                            const std::string&      ShaderBinaryDirectory,
                                            const std::string&      LunaSourceJpgPath,
                                            const std::string&      HipparcosCsvPath,
                                            std::string*            OutDiagnostic)
{
    const std::array<VkDescriptorType, 9> Bindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // Ensemble
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // StarCellSpans
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // StarRecords
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // TransLut
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // MultiLut
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // SkyViewLut
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // LunarAtlas (2048x1024x6 sRGB)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // Gbuffer
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // HdrFrame
    }};

    if (!Substrate.LoadComputeProgram(ShaderBinaryDirectory + "/RasterizeCelestialDome.spv",
                                      "RasterizeCelestialDomeMain",
                                      Bindings,
                                      sizeof(PushHeader),
                                      Program_,
                                      OutDiagnostic))
    {
        return false;
    }

    if (!HostDomeModel_.LoadLunarAtlas(LunaSourceJpgPath, 2048u, 1024u))
    {
        // Procedural 512x256 fallback if external path not provided
        (void)HostDomeModel_.LoadLunarAtlas("", 512u, 256u);
    }

    const LunarSixBodyAtlas& Atlas = HostDomeModel_.LunarAtlas();
    if (!Substrate.AllocateSrgb8Array(Atlas.Width(), Atlas.Height(), LunarAtlasLayerCount, LunarAtlasImage_) ||
        !Substrate.UploadSrgb8Array(LunarAtlasImage_, Atlas.Rgba8SrgbBytes()))
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "Failed to upload 6-body sRGB Lunar Atlas to Vulkan 2D-Array image";
        }
        return false;
    }

    if (!HipparcosCsvPath.empty())
    {
        if (!HostDomeModel_.LoadHipparcosCatalogueCsv(HipparcosCsvPath))
        {
            HostDomeModel_.BuildProceduralStarCatalogue(8000u);
        }
    }
    else
    {
        HostDomeModel_.BuildProceduralStarCatalogue(8000u);
    }

    const auto& Cells = HostDomeModel_.StarCells();
    const auto& Stars = HostDomeModel_.BinnedStars();
    IndexedStarCount_ = HostDomeModel_.SourceStarCount();

    std::vector<std::array<uint32_t, 2>> CellSpans(CelestialDomeModel::OctahedralCellCount);
    for (size_t I = 0u; I < Cells.size(); ++I)
    {
        CellSpans[I] = {Cells[I].First, Cells[I].Count};
    }

    std::vector<std::array<float, 4>> PackedStars;
    PackedStars.reserve(std::max<size_t>(2u, Stars.size() * 2u));
    for (size_t I = 0u; I < Stars.size(); ++I)
    {
        const BinnedCatalogueStar& St   = Stars[I];
        const float                Seed = std::fmod(static_cast<float>(I) * 0.6180339887f, 1.0f);
        PackedStars.push_back({St.DirectionUnit.X, St.DirectionUnit.Y, St.DirectionUnit.Z, St.Magnitude});
        PackedStars.push_back({St.ChromaticityRgb.X, St.ChromaticityRgb.Y, St.ChromaticityRgb.Z, Seed});
    }
    if (PackedStars.empty())
    {
        PackedStars.push_back({0.0f, 1.0f, 0.0f, 20.0f});
        PackedStars.push_back({0.0f, 0.0f, 0.0f, 0.0f});
    }

    if (!Substrate.UploadBuffer(StarCellBuffer_, CellSpans.data(), CellSpans.size() * sizeof(uint32_t) * 2u) ||
        !Substrate.UploadBuffer(StarRecordBuffer_, PackedStars.data(), PackedStars.size() * sizeof(float) * 4u))
    {
        return false;
    }

    return true;
}

void CelestialDomeRasterization::Shutdown(VulkanComputeSubstrate& Substrate) noexcept
{
    Substrate.ReleaseBuffer(StarRecordBuffer_);
    Substrate.ReleaseBuffer(StarCellBuffer_);
    Substrate.ReleaseImage(LunarAtlasImage_);
    Substrate.ReleaseProgram(Program_);
    IndexedStarCount_ = 0u;
}

bool CelestialDomeRasterization::Execute(VulkanComputeSubstrate&        Substrate,
                                         const VulkanBufferSlot&        EnsembleBuffer,
                                         const AtmosphereLutSubmission& AtmosphereLuts,
                                         const VulkanImageSlot&         GbufferArray,
                                         VulkanImageSlot&               HdrFrameTarget,
                                         uint32_t                       ViewportWidth,
                                         uint32_t                       ViewportHeight)
{
    const PushHeader Push{{ViewportWidth, ViewportHeight, 0u, 0u}, {0.0f, 0.0f, 0.0f, 0.0f}};
    const std::array<VulkanDescriptorArgument, 9> Args{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleBuffer, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &StarCellBuffer_, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &StarRecordBuffer_, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.TransmittanceArray()},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.MultiScatteringArray()},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.SkyViewArray()},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &LunarAtlasImage_},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &GbufferArray},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &HdrFrameTarget},
    }};
    return Substrate.DispatchCompute(
        Program_, Args, &Push, sizeof(Push), (ViewportWidth + 7u) / 8u, (ViewportHeight + 7u) / 8u, 1u);
}

} // namespace Frontier
