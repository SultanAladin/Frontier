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
    [[nodiscard]] MaterialPreviewConfiguration QueryPreviewConfiguration() const;
    // 📝 Display-size preview: the square target follows the viewport pixel edge (256..2048) so the cover
    // fit stops upscaling. Replacement retires at the submission boundary; hosts must rebind the preview
    // view when QueryPreviewView changes.
    void ConfigurePreviewExtent(uint32_t Extent);
    [[nodiscard]] uint32_t QueryPreviewExtent() const noexcept;
    void ConfigureSelection(const std::filesystem::path &RelativePath, const CompiledSurfaceMaterial &Material);
    void ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial);
    void ConfirmSubmission() noexcept;
    void InvalidateThumbnails() noexcept;
    void InvalidateThumbnail(const std::filesystem::path &RelativePath) noexcept;
    [[nodiscard]] bool AuthenticateThumbnail(const std::filesystem::path &RelativePath, uint64_t Generation) const;
    [[nodiscard]] uint64_t QueryBakeCount() const noexcept;
    [[nodiscard]] VkImage QueryPreviewImage() const noexcept;
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
