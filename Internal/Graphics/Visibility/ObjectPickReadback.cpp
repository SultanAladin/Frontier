/*==============================================================================================================================================
                                                           OBJECTPICKREADBACK.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the one-pixel identity copy-back. Initialize allocates a small ring of 4-byte host-visible + coherent staging buffers and
//    maps each one persistently (a 4-byte map is free to hold open, and re-mapping every frame would be pure overhead). Record transitions nothing
//    itself — the caller hands it an image already in TRANSFER_SRC_OPTIMAL — and issues one vkCmdCopyImageToBuffer of a 1x1 region at the cursor.
//    Resolve reads the OLDEST slot, which is the one the substrate's fence has certainly retired. Raw Vulkan, no VMA, mirroring the
//    VisibilityImage / VisibilityRasterization idioms.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Visibility/ObjectPickReadback.h"

// 📝 The header declares nothing unless FRONTIER_POLYGON_AUTHORING is set, so the whole translation unit collapses to empty with it
//    off. The include above stays outside the guard so the flag is picked up from it if a build defines it there.
#ifdef FRONTIER_POLYGON_AUTHORING

#include "Graphics/RenderExtension/Diagnostics/DiagnosticArchive.h"

#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// First memory type allowed by the requirement bitmask carrying every required property bit. Mirrors the identical helper in
// VisibilityRasterization.cpp — kept local rather than shared, as the visibility bricks each carry their own copy by house idiom.
uint32_t SelectPickMemoryTypeIndex(VkPhysicalDevice      PhysicalDevice,
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

// Allocate one 4-byte host-visible + coherent TRANSFER_DST slot and map it. Both handles null + MappedIdentity null on failure.
bool ConstructPickSlot(VulkanHost& Host, PickReadbackSlot& Slot)
{
    Slot.StagingBuffer  = VK_NULL_HANDLE;
    Slot.StagingMemory  = VK_NULL_HANDLE;
    Slot.MappedIdentity = nullptr;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = sizeof(uint32_t);
    BufferInformation.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Host.Device, &BufferInformation, Host.Allocator, &Slot.StagingBuffer) != VK_SUCCESS)
    {
        Slot.StagingBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Host.Device, Slot.StagingBuffer, &MemoryRequirements);

    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectPickMemoryTypeIndex(Host.PhysicalDevice,
                                                              MemoryRequirements.memoryTypeBits,
                                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                              MemoryTypeFound);
    if (!MemoryTypeFound)
    {
        vkDestroyBuffer(Host.Device, Slot.StagingBuffer, Host.Allocator);
        Slot.StagingBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo AllocationInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocationInformation.allocationSize  = MemoryRequirements.size;
    AllocationInformation.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocationInformation, Host.Allocator, &Slot.StagingMemory) != VK_SUCCESS)
    {
        vkDestroyBuffer(Host.Device, Slot.StagingBuffer, Host.Allocator);
        Slot.StagingBuffer = VK_NULL_HANDLE;
        Slot.StagingMemory = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindBufferMemory(Host.Device, Slot.StagingBuffer, Slot.StagingMemory, 0) != VK_SUCCESS)
    {
        vkFreeMemory(Host.Device, Slot.StagingMemory, Host.Allocator);
        vkDestroyBuffer(Host.Device, Slot.StagingBuffer, Host.Allocator);
        Slot.StagingBuffer = VK_NULL_HANDLE;
        Slot.StagingMemory = VK_NULL_HANDLE;
        return false;
    }

    void* MappedAddress = nullptr;
    if (vkMapMemory(Host.Device, Slot.StagingMemory, 0, sizeof(uint32_t), 0, &MappedAddress) != VK_SUCCESS)
    {
        vkFreeMemory(Host.Device, Slot.StagingMemory, Host.Allocator);
        vkDestroyBuffer(Host.Device, Slot.StagingBuffer, Host.Allocator);
        Slot.StagingBuffer = VK_NULL_HANDLE;
        Slot.StagingMemory = VK_NULL_HANDLE;
        return false;
    }

    Slot.MappedIdentity = static_cast<uint32_t*>(MappedAddress);
    // 📝 Seed the slot with the empty sentinel so a read before any copy lands reports "nothing", not stale heap bytes.
    *Slot.MappedIdentity = VisibilityEmptySentinel;
    Slot.PendingCondition = false;
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeObjectPickReadback(ObjectPickReadback& Readback, VulkanHost& Host)
{
    FinalizeObjectPickReadback(Readback);
    Readback.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ISSUE_CAUTION("ObjectPick", "no logical device - pick readback unavailable");
        return false;
    }

    for (uint32_t SlotIterator = 0; SlotIterator < PickReadbackSlots; ++SlotIterator)
    {
        if (!ConstructPickSlot(Host, Readback.Slots[SlotIterator]))
        {
            ISSUE_CAUTION("ObjectPick", "staging slot %u allocation failed - pick readback unavailable", SlotIterator);
            FinalizeObjectPickReadback(Readback);
            return false;
        }
    }

    Readback.WriteCursor       = 0;
    Readback.ResolvedPartition = NoSelectionSentinel;
    Readback.ReadyCondition    = true;
    ISSUE_NOTICE("ObjectPick", "readback ring live (%u slots, 4 B each)", PickReadbackSlots);
    return true;
}

void TransitionVisibilityImageForPickCopy(VisibilityImage& Image, VkCommandBuffer CommandBuffer)
{
    if (!Image.ReadyCondition || Image.IdImage == VK_NULL_HANDLE)
        return;

    VkImageMemoryBarrier Barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    Barrier.oldLayout                       = Image.CurrentLayout;
    Barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image                           = Image.IdImage;
    Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    Barrier.subresourceRange.baseMipLevel   = 0;
    Barrier.subresourceRange.levelCount     = 1;
    Barrier.subresourceRange.baseArrayLayer = 0;
    Barrier.subresourceRange.layerCount     = 1;

    // 📝 The source access mask must match where the image is coming FROM. The raster leaves it in COLOR_ATTACHMENT_OPTIMAL; the resolve path may
    //    already have moved it to SHADER_READ_ONLY_OPTIMAL. Both are handled so the pick copy can run either before or after the resolve transition.
    VkPipelineStageFlags SourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (Image.CurrentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        Barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        SourceStage           = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        Barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    Barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(CommandBuffer,
                         SourceStage,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &Barrier);

    Image.CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}

void RecordObjectPickCopy(ObjectPickReadback& Readback,
                          VisibilityImage&    Image,
                          int32_t             CursorX,
                          int32_t             CursorY,
                          VkCommandBuffer     CommandBuffer)
{
    if (!Readback.ReadyCondition || !Image.ReadyCondition || Image.IdImage == VK_NULL_HANDLE)
        return;

    // ⚠️ A copy region outside the image is undefined behaviour, not a clamped read — the cursor legitimately leaves the window, so this guard is
    //    load-bearing rather than defensive. The slot is left untouched (still holding the previous frame's id) when the cursor is outside.
    if (CursorX < 0 || CursorY < 0 || CursorX >= (int32_t)Image.Width || CursorY >= (int32_t)Image.Height)
        return;

    PickReadbackSlot& Slot = Readback.Slots[Readback.WriteCursor];

    VkBufferImageCopy CopyRegion = {};
    CopyRegion.bufferOffset                    = 0;
    CopyRegion.bufferRowLength                 = 0;   // tightly packed: one texel
    CopyRegion.bufferImageHeight               = 0;
    CopyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.mipLevel       = 0;
    CopyRegion.imageSubresource.baseArrayLayer = 0;
    CopyRegion.imageSubresource.layerCount     = 1;
    CopyRegion.imageOffset                     = { CursorX, CursorY, 0 };
    CopyRegion.imageExtent                     = { 1, 1, 1 };

    vkCmdCopyImageToBuffer(CommandBuffer,
                           Image.IdImage,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           Slot.StagingBuffer,
                           1,
                           &CopyRegion);

    Slot.RecordedCursorX  = CursorX;
    Slot.RecordedCursorY  = CursorY;
    Slot.PendingCondition = true;

    Readback.WriteCursor = (Readback.WriteCursor + 1) % PickReadbackSlots;
}

uint32_t ResolveObjectPickIdentity(ObjectPickReadback& Readback)
{
    if (!Readback.ReadyCondition)
        return NoSelectionSentinel;

    // 📝 The slot about to be overwritten is the OLDEST — PickReadbackSlots frames back, so the substrate has waited its fence and the copy has
    //    certainly landed. Reading here, before RecordObjectPickCopy advances the cursor, is what makes the whole path stall-free.
    PickReadbackSlot& Slot = Readback.Slots[Readback.WriteCursor];
    if (!Slot.PendingCondition || Slot.MappedIdentity == nullptr)
        return NoSelectionSentinel;

    const uint32_t Identity = *Slot.MappedIdentity;

    // ⚠️ Reject the empty word on the WHOLE value before unpacking. Shifting the all-ones sentinel would yield partition 0xFFF, a reachable
    //    ordinal — so a click on the sky would otherwise "select" the 4095th object.
    if (Identity == VisibilityEmptySentinel)
    {
        Readback.ResolvedPartition = NoSelectionSentinel;
        Readback.ResolvedPrimitive = NoSelectionSentinel;
        return NoSelectionSentinel;
    }

    Readback.ResolvedPartition = Identity >> PickPrimitiveBits;
    // 📝 The primitive half was always in the copied word — previously masked off and thrown away. The component modes need it, and surfacing it costs
    //    nothing: no extra copy, no extra latency, no change to what this function returns (still the partition, so every existing caller is unaffected).
    Readback.ResolvedPrimitive = Identity & ((1u << PickPrimitiveBits) - 1u);
    return Readback.ResolvedPartition;
}

void FinalizeObjectPickReadback(ObjectPickReadback& Readback)
{
    if (Readback.Host != nullptr && Readback.Host->Device != VK_NULL_HANDLE)
    {
        for (uint32_t SlotIterator = 0; SlotIterator < PickReadbackSlots; ++SlotIterator)
        {
            PickReadbackSlot& Slot = Readback.Slots[SlotIterator];
            if (Slot.StagingMemory != VK_NULL_HANDLE && Slot.MappedIdentity != nullptr)
                vkUnmapMemory(Readback.Host->Device, Slot.StagingMemory);
            if (Slot.StagingBuffer != VK_NULL_HANDLE)
                vkDestroyBuffer(Readback.Host->Device, Slot.StagingBuffer, Readback.Host->Allocator);
            if (Slot.StagingMemory != VK_NULL_HANDLE)
                vkFreeMemory(Readback.Host->Device, Slot.StagingMemory, Readback.Host->Allocator);
        }
    }

    for (uint32_t SlotIterator = 0; SlotIterator < PickReadbackSlots; ++SlotIterator)
        Readback.Slots[SlotIterator] = PickReadbackSlot();

    Readback.Host              = nullptr;
    Readback.WriteCursor       = 0;
    Readback.ResolvedPartition = NoSelectionSentinel;
    Readback.ReadyCondition    = false;
}

} // namespace Frontier

#endif // FRONTIER_POLYGON_AUTHORING
