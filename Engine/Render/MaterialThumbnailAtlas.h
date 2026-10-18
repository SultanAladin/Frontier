/*==========================================================================================================================================
                                                       MATERIALTHUMBNAILATLAS.H
==========================================================================================================================================*/
// 🧩 One device-local sampled image receives GPU-rendered material tiles with replicated gutters and no pixel readback.

#pragma once
#include "SurfaceRasterization.h"

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     GPU THUMBNAIL ATLAS
//------------------------------------------------------------------------------------------------------------------------

struct ThumbnailUvRectangle
{
    float MinimumU = 0, MinimumV = 0, MaximumU = 0, MaximumV = 0;
};

// ⚠️ The caller serializes one graphics queue, records at most one tile per submission, confirms accepted submission
// and retires all uses before destruction. An aborted recording must be discarded with the atlas.
class MaterialThumbnailAtlas
{
  public:
    MaterialThumbnailAtlas();
    ~MaterialThumbnailAtlas();
    MaterialThumbnailAtlas(const MaterialThumbnailAtlas &) = delete;
    MaterialThumbnailAtlas &operator=(const MaterialThumbnailAtlas &) = delete;
    void Initialize(const VulkanRasterAccess &Access, uint32_t TileExtent = 128, uint32_t Columns = 8);
    void ConstructCopy(VkCommandBuffer Commands, SurfaceRasterization &Source, uint32_t Slot);
    void ConfirmSubmission() noexcept;
    void Finalize() noexcept;
    [[nodiscard]] VkImage QueryImage() const noexcept;
    [[nodiscard]] VkImageView QueryImageView() const noexcept;
    [[nodiscard]] uint32_t QueryExtent() const noexcept;
    [[nodiscard]] uint32_t QueryCapacity() const noexcept;
    [[nodiscard]] ThumbnailUvRectangle QueryUv(uint32_t Slot) const;

  private:
    struct AtlasAllocation;
    std::unique_ptr<AtlasAllocation> Allocation;
};
} // namespace Frontier
