/*============================================================================================================================================
                                                         VECTORAUTHORINGCOVERAGERENDERING.H
============================================================================================================================================*/
// 🧩 Reuse retired Vulkan coverage images for interactive authoring without modifying an image still sampled by an earlier submission.

#pragma once
#include "VectorCoverageRasterization.h"
#include <optional>

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
    [[nodiscard]] uint64_t QueryDispatchCount() const noexcept;
private:
    struct CoverageSlot
    {
        VectorCoverageRasterization Rasterization;
        std::optional<VectorCoverageProgram> Program;
        std::optional<VectorSurfaceProgram> SurfaceProgram;
        uint64_t SampledSerial = 0;
        bool Pending = false;
    };
    std::array<CoverageSlot, 3> Slots;
    uint64_t Completed = 0, PendingSerial = 0;
    int Selected = -1;
    bool SampleRequested = false;
};
}
