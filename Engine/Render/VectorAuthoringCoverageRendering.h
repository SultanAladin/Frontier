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
    int SelectProgram(const VectorCoverageProgram &Program);
    int SelectSurfaceProgram(const VectorSurfaceProgram &Program);
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
        // 📝 One retained rasterization per bake extent (128/256/512): reselecting a pattern reuses its
        // extent instead of destroying pipelines on the UI thread, and keeps the last valid image live.
        std::array<std::unique_ptr<VectorCoverageRasterization>, 3> Extents;
        size_t Active = 3;
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
    void RefitSlot(size_t Index, size_t Segments);
    VectorCoverageRasterization &ActiveRasterization(size_t Index);
    [[nodiscard]] const VectorCoverageRasterization *QueryActiveRasterization(size_t Slot) const;
};
}
