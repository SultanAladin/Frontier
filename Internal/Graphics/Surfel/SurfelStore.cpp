/*==============================================================================================================================================
                                                             SURFELSTORE.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the surfel field's storage. Initialize claims thirteen device-local buffers and two atlases at a fixed capacity, then writes
//    each one's correct initial bytes on a single one-shot command buffer it submits and waits on: the counter SEEDED from SurfelCounterSeed and the
//    vacancy table seeded to the identity stack (both staged from the host, since vkCmdFillBuffer can only repeat one 32-bit word), everything else
//    zero-filled, and both atlases transitioned out of UNDEFINED and cleared. Raw Vulkan, no VMA, against the shared VulkanHost.
//
// 📝 Why one command buffer for all of it: every clear here is independent, so batching them costs nothing and the single fence wait replaces fifteen.
//    The seeds ride the same submission as the fills, which is what makes "the store is correct when Initialize returns" true without a second sync.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Surfel/SurfelStore.h"

#include <cstdio>
#include <cstring>
#include <numeric>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

void ReportSurfelStore(const char* MessageText)
{
    std::fprintf(stderr, "[SurfelStore] %s\n", MessageText);
}

// First memory type allowed by the requirement bitmask that carries every required property bit — the same helper the retired SurfelPool, the
// InstanceCull submission and the HiZ pyramid each use, kept identical so all four agree on memory selection.
uint32_t SelectMemoryTypeIndex(VkPhysicalDevice      PhysicalDevice,
                               uint32_t              CompatibleTypesBitmask,
                               VkMemoryPropertyFlags RequiredProperties,
                               bool&                 FoundEnabled)
{
    VkPhysicalDeviceMemoryProperties MemoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);
    for (uint32_t IndexIterator = 0; IndexIterator < MemoryProperties.memoryTypeCount; ++IndexIterator)
    {
        const bool TypeCompatible = (CompatibleTypesBitmask & (1u << IndexIterator)) != 0;
        const bool PropertyMatch  = (MemoryProperties.memoryTypes[IndexIterator].propertyFlags & RequiredProperties) == RequiredProperties;
        if (TypeCompatible && PropertyMatch) { FoundEnabled = true; return IndexIterator; }
    }
    FoundEnabled = false;
    return 0;
}

// Claim one buffer of ByteLength with Usage and MemoryProperties, binding a fresh allocation to it. On any failure the slot is left fully empty, so a
// caller can release the whole store unconditionally without tracking which claim failed.
bool AllocateBufferSlot(VulkanHost&           Host,
                        VkDeviceSize          ByteLength,
                        VkBufferUsageFlags    Usage,
                        VkMemoryPropertyFlags MemoryProperties,
                        SurfelBufferSlot&     Slot)
{
    Slot = SurfelBufferSlot{};
    if (ByteLength == 0) ByteLength = 4;   // a zero-size buffer is invalid; one word costs nothing and keeps the guards uniform

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteLength;
    BufferInformation.usage       = Usage;
    BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Host.Device, &BufferInformation, Host.Allocator, &Slot.Buffer) != VK_SUCCESS)
    {
        Slot.Buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Host.Device, Slot.Buffer, &MemoryRequirements);

    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice, MemoryRequirements.memoryTypeBits,
                                                           MemoryProperties, MemoryTypeFound);
    if (!MemoryTypeFound)
    {
        vkDestroyBuffer(Host.Device, Slot.Buffer, Host.Allocator);
        Slot = SurfelBufferSlot{};
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &Slot.Memory) != VK_SUCCESS ||
        vkBindBufferMemory(Host.Device, Slot.Buffer, Slot.Memory, 0) != VK_SUCCESS)
    {
        if (Slot.Memory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Slot.Memory, Host.Allocator);
        vkDestroyBuffer(Host.Device, Slot.Buffer, Host.Allocator);
        Slot = SurfelBufferSlot{};
        return false;
    }

    Slot.ByteLength = ByteLength;
    return true;
}

// The store's ordinary device-local storage buffer: written by compute, cleared/seeded by a transfer, and readable back by a transfer (which is what
// lets the Phase-5 probes copy any of these out for inspection without re-creating them).
bool AllocateStorageSlot(VulkanHost& Host, VkDeviceSize ByteLength, SurfelBufferSlot& Slot)
{
    return AllocateBufferSlot(Host, ByteLength,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Slot);
}

void ReleaseBufferSlot(VulkanHost& Host, SurfelBufferSlot& Slot)
{
    if (Slot.Buffer != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, Slot.Buffer, Host.Allocator);
    if (Slot.Memory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Slot.Memory, Host.Allocator);
    Slot = SurfelBufferSlot{};
}

// Claim one atlas image + view. STORAGE because the integrate writes it, SAMPLED because the shade filters it, TRANSFER_DST for the initial clear.
bool AllocateAtlasSlot(VulkanHost& Host, VkFormat Format, uint32_t Width, uint32_t Height, SurfelAtlasSlot& Slot)
{
    Slot = SurfelAtlasSlot{};

    VkImageCreateInfo ImageInformation = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ImageInformation.imageType     = VK_IMAGE_TYPE_2D;
    ImageInformation.format        = Format;
    ImageInformation.extent        = { Width, Height, 1u };
    ImageInformation.mipLevels     = 1;
    ImageInformation.arrayLayers   = 1;
    ImageInformation.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageInformation.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageInformation.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ImageInformation.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageInformation.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(Host.Device, &ImageInformation, Host.Allocator, &Slot.Image) != VK_SUCCESS)
    {
        Slot.Image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetImageMemoryRequirements(Host.Device, Slot.Image, &MemoryRequirements);

    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice, MemoryRequirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, MemoryTypeFound);
    if (!MemoryTypeFound)
    {
        vkDestroyImage(Host.Device, Slot.Image, Host.Allocator);
        Slot = SurfelAtlasSlot{};
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &Slot.Memory) != VK_SUCCESS ||
        vkBindImageMemory(Host.Device, Slot.Image, Slot.Memory, 0) != VK_SUCCESS)
    {
        if (Slot.Memory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Slot.Memory, Host.Allocator);
        vkDestroyImage(Host.Device, Slot.Image, Host.Allocator);
        Slot = SurfelAtlasSlot{};
        return false;
    }

    VkImageViewCreateInfo ViewInformation = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ViewInformation.image                       = Slot.Image;
    ViewInformation.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    ViewInformation.format                      = Format;
    ViewInformation.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewInformation.subresourceRange.levelCount = 1;
    ViewInformation.subresourceRange.layerCount = 1;
    if (vkCreateImageView(Host.Device, &ViewInformation, Host.Allocator, &Slot.View) != VK_SUCCESS)
    {
        vkFreeMemory(Host.Device, Slot.Memory, Host.Allocator);
        vkDestroyImage(Host.Device, Slot.Image, Host.Allocator);
        Slot = SurfelAtlasSlot{};
        return false;
    }

    Slot.Format = Format;
    Slot.Width  = Width;
    Slot.Height = Height;
    return true;
}

void ReleaseAtlasSlot(VulkanHost& Host, SurfelAtlasSlot& Slot)
{
    if (Slot.View   != VK_NULL_HANDLE) vkDestroyImageView(Host.Device, Slot.View, Host.Allocator);
    if (Slot.Image  != VK_NULL_HANDLE) vkDestroyImage(Host.Device, Slot.Image, Host.Allocator);
    if (Slot.Memory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Slot.Memory, Host.Allocator);
    Slot = SurfelAtlasSlot{};
}

// A host-visible TRANSFER_SRC buffer holding a copy of Bytes, for the two seeds vkCmdFillBuffer cannot express (an ascending sequence, and the
// counter's non-uniform six words). The caller copies from it on a command buffer and releases it after the wait.
bool AllocateSeedStaging(VulkanHost& Host, const void* Bytes, VkDeviceSize ByteLength, SurfelBufferSlot& Slot)
{
    if (!AllocateBufferSlot(Host, ByteLength, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Slot))
        return false;

    void* Mapped = nullptr;
    if (vkMapMemory(Host.Device, Slot.Memory, 0, ByteLength, 0, &Mapped) != VK_SUCCESS)
    {
        ReleaseBufferSlot(Host, Slot);
        return false;
    }
    std::memcpy(Mapped, Bytes, (size_t)ByteLength);
    vkUnmapMemory(Host.Device, Slot.Memory);
    return true;
}

// Transition an atlas UNDEFINED -> GENERAL and clear it to zero. 📝 Both atlases live in GENERAL for the whole run: the integrate writes them as
// storage images and the shade samples them, and GENERAL is the one layout that serves both without a per-frame transition.
void SeedAtlasContents(VkCommandBuffer CommandBuffer, const SurfelAtlasSlot& Slot)
{
    VkImageSubresourceRange Range = {};
    Range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    Range.levelCount = 1;
    Range.layerCount = 1;

    VkImageMemoryBarrier ToTransfer = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    ToTransfer.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    ToTransfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToTransfer.srcAccessMask       = 0;
    ToTransfer.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    ToTransfer.image               = Slot.Image;
    ToTransfer.subresourceRange    = Range;
    ToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToTransfer);

    VkClearColorValue ClearColour = {};
    vkCmdClearColorImage(CommandBuffer, Slot.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearColour, 1, &Range);

    VkImageMemoryBarrier ToGeneral = ToTransfer;
    ToGeneral.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToGeneral.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
    ToGeneral.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    ToGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToGeneral);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSurfelStore(SurfelStore&                 Store,
                           VulkanHost&                  Host,
                           VkCommandPool                CommandPool,
                           const SurfelGridProportions& Proportions)
{
    Store = SurfelStore{};
    Store.Host        = &Host;
    Store.Proportions = Proportions;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportSurfelStore("no device — store not built");
        return false;
    }
    if (CommandPool == VK_NULL_HANDLE)
    {
        ReportSurfelStore("no command pool — the seeds cannot be submitted, so the store would be silently wrong");
        return false;
    }

    Store.Capacity  = SurfelTotalLimit;
    Store.CellCount = ResolveSurfelCellCount(Proportions.CellDimension);

    // --- byte lengths, one per buffer ------------------------------------------------------------------------------------
    const VkDeviceSize RecordBytes    = (VkDeviceSize)Store.Capacity  * sizeof(Surfel);                 // stride 100 B
    const VkDeviceSize LocatorBytes   = (VkDeviceSize)Store.Capacity  * 4u * sizeof(uint32_t);          // uvec4 anchor
    const VkDeviceSize IndexBytes     = (VkDeviceSize)Store.Capacity  * sizeof(uint32_t);
    const VkDeviceSize CellSpanBytes  = (VkDeviceSize)Store.CellCount * sizeof(SurfelCellSpan);
    const VkDeviceSize CellListBytes  = (VkDeviceSize)SurfelCellListCapacity * sizeof(uint32_t);
    const VkDeviceSize ReserveBytes   = (VkDeviceSize)Store.CellCount * sizeof(uint32_t);
    const VkDeviceSize RecycleBytes   = (VkDeviceSize)Store.Capacity  * sizeof(SurfelRecycleRecord);
    const VkDeviceSize OutcomeBytes   = (VkDeviceSize)SurfelRayBudget * sizeof(SurfelRayOutcome);

    if (!AllocateStorageSlot(Host, RecordBytes,          Store.SurfelRecords)  ||
        !AllocateStorageSlot(Host, LocatorBytes,         Store.HitLocator)     ||
        !AllocateStorageSlot(Host, IndexBytes,           Store.LiveIndex)      ||
        !AllocateStorageSlot(Host, IndexBytes,           Store.PendingIndex)   ||
        !AllocateStorageSlot(Host, IndexBytes,           Store.VacancyTable)   ||
        !AllocateStorageSlot(Host, CellSpanBytes,        Store.CellSpan)       ||
        !AllocateStorageSlot(Host, CellListBytes,        Store.CellList)       ||
        !AllocateStorageSlot(Host, ReserveBytes,         Store.Reservation)    ||
        !AllocateStorageSlot(Host, IndexBytes,           Store.ReferenceTally) ||
        !AllocateStorageSlot(Host, RecycleBytes,         Store.RecycleRecords) ||
        !AllocateStorageSlot(Host, OutcomeBytes,         Store.RayOutcomes)    ||
        !AllocateStorageSlot(Host, SurfelCounterBytes,   Store.Counter))
    {
        ReportSurfelStore("device-local buffer allocation failed");
        FinalizeSurfelStore(Store);
        return false;
    }

    // The counter's host-visible mirror. TRANSFER_DST only — the host reads it, nothing on the device does.
    if (!AllocateBufferSlot(Host, SurfelCounterBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Store.CounterReadback))
    {
        ReportSurfelStore("counter readback mirror allocation failed");
        FinalizeSurfelStore(Store);
        return false;
    }

    if (!AllocateAtlasSlot(Host, VK_FORMAT_R32_SFLOAT,    SurfelAtlasWidth, SurfelAtlasHeight, Store.IrradianceAtlas) ||
        !AllocateAtlasSlot(Host, VK_FORMAT_R32G32_SFLOAT, SurfelAtlasWidth, SurfelAtlasHeight, Store.DepthAtlas))
    {
        ReportSurfelStore("atlas allocation failed");
        FinalizeSurfelStore(Store);
        return false;
    }

    // --- stage the two seeds vkCmdFillBuffer cannot express --------------------------------------------------------------
    // 🔴 ② The vacancy table is the identity stack 0,1,2,…,limit-1. Zeroed, it hands slot 0 to every spawn.
    std::vector<uint32_t> VacancySequence(Store.Capacity);
    std::iota(VacancySequence.begin(), VacancySequence.end(), 0u);

    SurfelBufferSlot VacancyStaging = {};
    SurfelBufferSlot CounterStaging = {};
    if (!AllocateSeedStaging(Host, VacancySequence.data(), IndexBytes, VacancyStaging) ||
        !AllocateSeedStaging(Host, SurfelCounterSeed, SurfelCounterBytes, CounterStaging))
    {
        ReportSurfelStore("seed staging allocation failed");
        ReleaseBufferSlot(Host, VacancyStaging);
        ReleaseBufferSlot(Host, CounterStaging);
        FinalizeSurfelStore(Store);
        return false;
    }

    // --- one command buffer: every seed, every zero-fill, both atlas transitions, one wait -------------------------------
    bool Seeded = false;
    VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandAllocate.commandPool        = CommandPool;
    CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CommandAllocate.commandBufferCount = 1;
    VkCommandBuffer SeedCommand = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &SeedCommand) == VK_SUCCESS)
    {
        VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(SeedCommand, &BeginInformation) == VK_SUCCESS)
        {
            // 🔴 ① The counter is SEEDED, not zeroed: FreeSurfel = SurfelTotalLimit. A zero-filled counter reads as "no vacancies" and the field
            //    never spawns a single surfel — no error, no warning, just permanent darkness.
            VkBufferCopy CounterRegion = { 0, 0, SurfelCounterBytes };
            vkCmdCopyBuffer(SeedCommand, CounterStaging.Buffer, Store.Counter.Buffer, 1, &CounterRegion);

            VkBufferCopy VacancyRegion = { 0, 0, IndexBytes };
            vkCmdCopyBuffer(SeedCommand, VacancyStaging.Buffer, Store.VacancyTable.Buffer, 1, &VacancyRegion);

            // ③ Everything else starts at zero. For CellSpan / Reservation / ReferenceTally this is also the per-frame reset the lifecycle repeats;
            //    for the rest it merely guarantees the first frame reads defined bytes rather than whatever the allocation happened to contain.
            vkCmdFillBuffer(SeedCommand, Store.SurfelRecords.Buffer,  0, Store.SurfelRecords.ByteLength,  0u);
            vkCmdFillBuffer(SeedCommand, Store.HitLocator.Buffer,     0, Store.HitLocator.ByteLength,     0u);
            vkCmdFillBuffer(SeedCommand, Store.LiveIndex.Buffer,      0, Store.LiveIndex.ByteLength,      0u);
            vkCmdFillBuffer(SeedCommand, Store.PendingIndex.Buffer,   0, Store.PendingIndex.ByteLength,   0u);
            vkCmdFillBuffer(SeedCommand, Store.CellSpan.Buffer,       0, Store.CellSpan.ByteLength,       0u);
            vkCmdFillBuffer(SeedCommand, Store.CellList.Buffer,       0, Store.CellList.ByteLength,       0u);
            vkCmdFillBuffer(SeedCommand, Store.Reservation.Buffer,    0, Store.Reservation.ByteLength,    0u);
            vkCmdFillBuffer(SeedCommand, Store.ReferenceTally.Buffer, 0, Store.ReferenceTally.ByteLength, 0u);
            vkCmdFillBuffer(SeedCommand, Store.RecycleRecords.Buffer, 0, Store.RecycleRecords.ByteLength, 0u);
            vkCmdFillBuffer(SeedCommand, Store.RayOutcomes.Buffer,    0, Store.RayOutcomes.ByteLength,    0u);

            SeedAtlasContents(SeedCommand, Store.IrradianceAtlas);
            SeedAtlasContents(SeedCommand, Store.DepthAtlas);

            if (vkEndCommandBuffer(SeedCommand) == VK_SUCCESS)
            {
                VkFence Fence = VK_NULL_HANDLE;
                VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
                if (vkCreateFence(Host.Device, &FenceInformation, Host.Allocator, &Fence) == VK_SUCCESS)
                {
                    VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
                    SubmitInformation.commandBufferCount = 1;
                    SubmitInformation.pCommandBuffers    = &SeedCommand;
                    if (vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInformation, Fence) == VK_SUCCESS &&
                        vkWaitForFences(Host.Device, 1, &Fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS)
                    {
                        Seeded = true;
                    }
                    vkDestroyFence(Host.Device, Fence, Host.Allocator);
                }
            }
        }
        vkFreeCommandBuffers(Host.Device, CommandPool, 1, &SeedCommand);
    }

    ReleaseBufferSlot(Host, VacancyStaging);
    ReleaseBufferSlot(Host, CounterStaging);

    if (!Seeded)
    {
        ReportSurfelStore("seed submission failed — releasing rather than leaving an unseeded store live");
        FinalizeSurfelStore(Store);
        return false;
    }

    Store.ReadyCondition = true;
    return true;
}

void FinalizeSurfelStore(SurfelStore& Store)
{
    if (Store.Host == nullptr || Store.Host->Device == VK_NULL_HANDLE)
    {
        Store = SurfelStore{};
        return;
    }
    VulkanHost& Host = *Store.Host;

    ReleaseBufferSlot(Host, Store.SurfelRecords);
    ReleaseBufferSlot(Host, Store.HitLocator);
    ReleaseBufferSlot(Host, Store.LiveIndex);
    ReleaseBufferSlot(Host, Store.PendingIndex);
    ReleaseBufferSlot(Host, Store.VacancyTable);
    ReleaseBufferSlot(Host, Store.CellSpan);
    ReleaseBufferSlot(Host, Store.CellList);
    ReleaseBufferSlot(Host, Store.Reservation);
    ReleaseBufferSlot(Host, Store.ReferenceTally);
    ReleaseBufferSlot(Host, Store.RecycleRecords);
    ReleaseBufferSlot(Host, Store.RayOutcomes);
    ReleaseBufferSlot(Host, Store.Counter);
    ReleaseBufferSlot(Host, Store.CounterReadback);

    ReleaseAtlasSlot(Host, Store.IrradianceAtlas);
    ReleaseAtlasSlot(Host, Store.DepthAtlas);

    Store = SurfelStore{};
}

void RecordSurfelCounterReadback(const SurfelStore& Store, VkCommandBuffer CommandBuffer)
{
    if (!Store.ReadyCondition || CommandBuffer == VK_NULL_HANDLE) return;

    // 📝 The counter is written by compute right up to this point, so the copy must wait on those writes. Without this barrier the mirror can capture
    //    a half-updated counter — which reads as a plausible population and is why a "surfel count fluctuates oddly" report is hard to chase.
    VkBufferMemoryBarrier CounterBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    CounterBarrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    CounterBarrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    CounterBarrier.buffer              = Store.Counter.Buffer;
    CounterBarrier.offset              = 0;
    CounterBarrier.size                = SurfelCounterBytes;
    CounterBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    CounterBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 1, &CounterBarrier, 0, nullptr);

    VkBufferCopy Region = { 0, 0, SurfelCounterBytes };
    vkCmdCopyBuffer(CommandBuffer, Store.Counter.Buffer, Store.CounterReadback.Buffer, 1, &Region);
}

bool RetrieveSurfelPopulation(const SurfelStore& Store, SurfelPopulation& Result)
{
    if (!Store.ReadyCondition || Store.Host == nullptr || Store.CounterReadback.Memory == VK_NULL_HANDLE)
        return false;

    void* Mapped = nullptr;
    if (vkMapMemory(Store.Host->Device, Store.CounterReadback.Memory, 0, SurfelCounterBytes, 0, &Mapped) != VK_SUCCESS)
        return false;

    uint32_t Slots[SurfelCounterSlotCount] = {};
    std::memcpy(Slots, Mapped, SurfelCounterBytes);
    vkUnmapMemory(Store.Host->Device, Store.CounterReadback.Memory);

    // ⚠️ SurfelCounterOffset is spelled in BYTES (0,4,8,…) because upstream indexes a byte-addressed buffer; this is a uint array, so each offset is
    //    divided by 4 to reach the element. Copying an offset in raw would read slot 0 for everything past the first.
    Result.LiveCount     = Slots[(uint32_t)SurfelCounterOffset::ValidSurfel  / 4u];
    Result.PendingCount  = Slots[(uint32_t)SurfelCounterOffset::DirtySurfel  / 4u];
    Result.VacancyCount  = Slots[(uint32_t)SurfelCounterOffset::FreeSurfel   / 4u];
    Result.CellCursor    = Slots[(uint32_t)SurfelCounterOffset::Cell         / 4u];
    Result.RequestedRays = Slots[(uint32_t)SurfelCounterOffset::RequestedRay / 4u];
    Result.MissedRays    = Slots[(uint32_t)SurfelCounterOffset::MissBounce   / 4u];
    return true;
}

VkDeviceSize ResolveSurfelStoreFootprint(const SurfelStore& Store)
{
    // 📝 The atlases are counted from their texel geometry rather than a stored allocation size: the driver may pad an OPTIMAL-tiled image, so this is
    //    the payload footprint, which is the number the budget decision was made against.
    const VkDeviceSize IrradianceBytes = (VkDeviceSize)Store.IrradianceAtlas.Width * Store.IrradianceAtlas.Height * 4u;
    const VkDeviceSize DepthBytes      = (VkDeviceSize)Store.DepthAtlas.Width      * Store.DepthAtlas.Height      * 8u;

    return Store.SurfelRecords.ByteLength + Store.HitLocator.ByteLength    + Store.LiveIndex.ByteLength      +
           Store.PendingIndex.ByteLength  + Store.VacancyTable.ByteLength  + Store.CellSpan.ByteLength       +
           Store.CellList.ByteLength      + Store.Reservation.ByteLength   + Store.ReferenceTally.ByteLength +
           Store.RecycleRecords.ByteLength + Store.RayOutcomes.ByteLength  + Store.Counter.ByteLength        +
           IrradianceBytes + DepthBytes;
}

} // namespace Frontier
