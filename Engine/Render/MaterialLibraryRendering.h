/*==========================================================================================================================================
                                                       MATERIALLIBRARYRENDERING.H
==========================================================================================================================================*/
// 🧩 Share bounded GPU thumbnail residency, material baking and a separate interactive preview between native applications.

#pragma once
#include "MaterialThumbnailAtlas.h"
#include "MaterialPreviewConfiguration.h"
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier
{
struct MaterialThumbnailSelection
{
    uint64_t Generation = 0;
    ThumbnailUvRectangle Uv;
    bool Ready = false;
};

class MaterialLibraryRendering
{
  public:
    MaterialLibraryRendering();
    ~MaterialLibraryRendering();
    MaterialLibraryRendering(const MaterialLibraryRendering &) = delete;
    MaterialLibraryRendering &operator=(const MaterialLibraryRendering &) = delete;
    void Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderDirectory);
    void RequestLibrary(const std::filesystem::path &Directory, const std::vector<std::filesystem::path> &Visible);
    void ConfigurePreview(const MaterialPreviewConfiguration &Configuration);
    // 📝 Resize the interactive preview target and re-derive the projection aspect from it, so the subject fills the
    //    panel instead of being drawn into a fixed square and clipped. Programs and geometry are not reallocated.
    void ConfigurePreviewExtent(uint32_t Width, uint32_t Height);
    [[nodiscard]] MaterialPreviewConfiguration QueryPreviewConfiguration() const;
    void ConfigureSelection(const std::filesystem::path &RelativePath, const CompiledSurfaceMaterial &Material);
    void ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial);
    void ConfirmSubmission() noexcept;
    void InvalidateThumbnails() noexcept;
    [[nodiscard]] bool AuthenticateThumbnail(const std::filesystem::path &RelativePath, uint64_t Generation) const;
    [[nodiscard]] uint64_t QueryBakeCount() const noexcept;
    [[nodiscard]] VkImage QueryPreviewImage() const noexcept;
    // 📐 The live preview target's size, which follows the panel rectangle. A caller reading the image back must use
    //    this rather than assuming a square, or the pixels are reinterpreted at the wrong stride.
    [[nodiscard]] VkExtent2D QueryPreviewExtent() const noexcept;
    [[nodiscard]] VkImage QueryAtlasImage() const noexcept;
    [[nodiscard]] uint32_t QueryAtlasExtent() const noexcept;
    [[nodiscard]] std::string QueryThumbnailDiagnostic(const std::filesystem::path &RelativePath) const;
    [[nodiscard]] MaterialThumbnailSelection QueryThumbnail(const std::filesystem::path &RelativePath) const;
    [[nodiscard]] VkImageView QueryAtlasView() const noexcept;
    [[nodiscard]] VkImageView QueryPreviewView() const noexcept;
    [[nodiscard]] bool QueryPreviewReady() const noexcept;
    [[nodiscard]] const std::string &QueryDiagnostic() const noexcept;

  private:
    struct RenderingAllocation;
    std::unique_ptr<RenderingAllocation> Allocation;
};
} // namespace Frontier
