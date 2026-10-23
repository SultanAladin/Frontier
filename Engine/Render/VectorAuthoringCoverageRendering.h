/*============================================================================================================================================
                                                         VECTORAUTHORINGCOVERAGERENDERING.H
============================================================================================================================================*/
// 🧩 Reuse retired Vulkan coverage images for interactive authoring without modifying an image still sampled by an earlier submission.

#pragma once
#include "VectorCoverageRasterization.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Frontier
{
class VectorAuthoringCoverageRendering
{
public:
    void Initialize(const VulkanRasterAccess &Access, RasterShader Shader);
    void Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderArchive);
    // 📝 Call during main-viewport UI construction; its chosen image is produced before that UI submission samples it.
    // 📝 DisplayExtent is an optional device-pixel hint (e.g. the live canvas quad at its current
    // zoom): when the authored content is unchanged but the hint now needs a different pooled tier,
    // the slot re-bakes at that tier so the shown image stays vector-crisp instead of being a
    // raster stretched past its baked resolution. Zero keeps the prior budget-only behaviour.
    int SelectProgram(const VectorCoverageProgram &Program, uint32_t DisplayExtent = 0);
    int SelectSurfaceProgram(const VectorSurfaceProgram &Program, uint32_t DisplayExtent = 0);
    void ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial);
    void ConfirmSubmission();
    void Finalize() noexcept;
    [[nodiscard]] VkImage QueryImage(size_t Slot) const;
    [[nodiscard]] VkImageView QueryImageView(size_t Slot) const;
    // 📝 Active bake edge extent of a slot (0 before its first fit); hosts retain one descriptor per slot/extent.
    [[nodiscard]] uint32_t QuerySlotExtent(size_t Slot) const;
    // 📝 True once the selected program's image has been recorded and its sampling submission retired.
    [[nodiscard]] bool QuerySelectedReady() const noexcept;
    [[nodiscard]] uint64_t QueryDispatchCount() const noexcept;
private:
    struct CoverageSlot
    {
        // 📝 One retained rasterization per bake extent (128/256/512/1024): reselecting a pattern reuses its
        // extent instead of destroying pipelines on the UI thread, and keeps the last valid image live.
        std::array<std::unique_ptr<VectorCoverageRasterization>, 4> Extents;
        size_t Active = 4;
        std::optional<VectorCoverageProgram> Program;
        std::optional<VectorSurfaceProgram> SurfaceProgram;
        uint64_t SampledSerial = 0;
        bool Pending = false;
    };
    std::array<CoverageSlot, 3> Slots;
    VulkanRasterAccess RetainedAccess{};
    std::vector<uint32_t> RetainedShaderWords;
    std::string RetainedShaderEntry;
    uint64_t Completed = 0, PendingSerial = 0;
    int Selected = -1;
    bool SampleRequested = false;
    // 📝 Returns true when the slot's active pooled extent changed (a fresh dispatch is required
    // even if the authored content itself did not, e.g. a zoom-driven DisplayExtent change).
    bool RefitSlot(size_t Index, size_t Segments, uint32_t DisplayExtent = 0);
    VectorCoverageRasterization &ActiveRasterization(size_t Index);
    [[nodiscard]] const VectorCoverageRasterization *QueryActiveRasterization(size_t Slot) const;
};
}
