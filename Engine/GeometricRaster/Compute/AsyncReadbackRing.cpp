/*============================================================================================================================================
                                                           ASYNCREADBACKRING.CPP
============================================================================================================================================*/
// 🧩 Implementation of AsyncReadbackRing encoding zero-stall GPU buffer/image transfers into 64B-aligned host-cached staging slots

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Compute/AsyncReadbackRing.h"

#include <cstring>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

AsyncReadbackRing::~AsyncReadbackRing()
{
    TeardownRing();
}

bool AsyncReadbackRing::InitializeRing(
    VulkanDeviceContext* BoundContext,
    uint64_t             SlotCapacityBytes)
{
    TeardownRing();
    if (BoundContext == nullptr || !BoundContext->Ready() || SlotCapacityBytes == 0ull)
    {
        return false;
    }

    Context = BoundContext;
    for (StagingSlot& Slot : Slots)
    {
        Slot.Buffer = Context->AllocateBuffer(
            SlotCapacityBytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        Slot.WriteCursor = 0ull;
        Slot.LastSignal  = 0ull;

        if (Slot.Buffer.BufferHandle == VK_NULL_HANDLE || Slot.Buffer.MappedPtr == nullptr)
        {
            TeardownRing();
            return false;
        }
    }

    ActiveSlot = 0u;
    return true;
}

void AsyncReadbackRing::TeardownRing() noexcept
{
    if (Context != nullptr)
    {
        for (StagingSlot& Slot : Slots)
        {
            Context->ReleaseBuffer(Slot.Buffer);
            Slot.WriteCursor = 0ull;
            Slot.LastSignal  = 0ull;
        }
    }
    Context = nullptr;
}

void AsyncReadbackRing::AdvanceFrameSlot() noexcept
{
    ActiveSlot = (ActiveSlot + 1u) % RingSlotCount;
    StagingSlot& Next = Slots[ActiveSlot];
    if (Context != nullptr && Next.LastSignal != 0ull)
    {
        (void)Context->AwaitTimelineValue(Next.LastSignal);
    }
    Next.WriteCursor = 0ull;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                TRANSFER ENCODING & READBACK
//------------------------------------------------------------------------------------------------------------------------

ReadbackTicket AsyncReadbackRing::EncodeBufferReadback(
    VkCommandBuffer SourceCmdBuffer,
    VkBuffer        SourceDeviceBuffer,
    uint64_t        SourceOffsetBytes,
    uint64_t        CopyByteCount)
{
    ReadbackTicket Ticket {};
    if (Context == nullptr || CopyByteCount == 0ull)
    {
        return Ticket;
    }

    StagingSlot& Slot = Slots[ActiveSlot];
    const uint64_t AlignedOffset = (Slot.WriteCursor + 63ull) & ~63ull;
    if (AlignedOffset + CopyByteCount > Slot.Buffer.ByteCapacity)
    {
        AdvanceFrameSlot();
    }

    StagingSlot& TargetSlot    = Slots[ActiveSlot];
    const uint64_t DestOffset  = (TargetSlot.WriteCursor + 63ull) & ~63ull;
    if (DestOffset + CopyByteCount > TargetSlot.Buffer.ByteCapacity)
    {
        return Ticket;
    }

    VkMemoryBarrier ComputeToTransfer {};
    ComputeToTransfer.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    ComputeToTransfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ComputeToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(
        SourceCmdBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u,
        1u,
        &ComputeToTransfer,
        0u,
        nullptr,
        0u,
        nullptr);

    VkBufferCopy CopyRegion {};
    CopyRegion.srcOffset = SourceOffsetBytes;
    CopyRegion.dstOffset = DestOffset;
    CopyRegion.size      = CopyByteCount;
    vkCmdCopyBuffer(SourceCmdBuffer, SourceDeviceBuffer, TargetSlot.Buffer.BufferHandle, 1u, &CopyRegion);

    VkMemoryBarrier HostBarrier {};
    HostBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    HostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    HostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(
        SourceCmdBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0u,
        1u,
        &HostBarrier,
        0u,
        nullptr,
        0u,
        nullptr);

    TargetSlot.WriteCursor = DestOffset + CopyByteCount;
    Ticket.SlotIndex       = ActiveSlot;
    Ticket.ByteOffset      = DestOffset;
    Ticket.ByteLength      = CopyByteCount;
    return Ticket;
}

ReadbackTicket AsyncReadbackRing::EncodeImageRegionReadback(
    VkCommandBuffer SourceCmdBuffer,
    VkImage         SourceImage,
    VkImageLayout   CurrentImageLayout,
    int32_t         OffsetX,
    int32_t         OffsetY,
    uint32_t        ExtentWidth,
    uint32_t        ExtentHeight,
    uint32_t        BytesPerPixel)
{
    ReadbackTicket Ticket {};
    const uint64_t TotalBytes = static_cast<uint64_t>(ExtentWidth) * static_cast<uint64_t>(ExtentHeight) * BytesPerPixel;
    if (Context == nullptr || TotalBytes == 0ull)
    {
        return Ticket;
    }

    StagingSlot& Slot = Slots[ActiveSlot];
    if (((Slot.WriteCursor + 63ull) & ~63ull) + TotalBytes > Slot.Buffer.ByteCapacity)
    {
        AdvanceFrameSlot();
    }

    StagingSlot&   TargetSlot = Slots[ActiveSlot];
    const uint64_t DestOffset = (TargetSlot.WriteCursor + 63ull) & ~63ull;
    if (DestOffset + TotalBytes > TargetSlot.Buffer.ByteCapacity)
    {
        return Ticket;
    }

    VkImageMemoryBarrier ToTransfer {};
    ToTransfer.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToTransfer.oldLayout                       = CurrentImageLayout;
    ToTransfer.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ToTransfer.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    ToTransfer.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    ToTransfer.image                           = SourceImage;
    ToTransfer.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ToTransfer.subresourceRange.baseMipLevel   = 0u;
    ToTransfer.subresourceRange.levelCount     = 1u;
    ToTransfer.subresourceRange.baseArrayLayer = 0u;
    ToTransfer.subresourceRange.layerCount     = 1u;
    ToTransfer.srcAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    ToTransfer.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(
        SourceCmdBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u,
        0u,
        nullptr,
        0u,
        nullptr,
        1u,
        &ToTransfer);

    VkBufferImageCopy CopyRegion {};
    CopyRegion.bufferOffset                    = DestOffset;
    CopyRegion.bufferRowLength                 = 0u;
    CopyRegion.bufferImageHeight               = 0u;
    CopyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.mipLevel       = 0u;
    CopyRegion.imageSubresource.baseArrayLayer = 0u;
    CopyRegion.imageSubresource.layerCount     = 1u;
    CopyRegion.imageOffset                     = { OffsetX, OffsetY, 0 };
    CopyRegion.imageExtent                     = { ExtentWidth, ExtentHeight, 1u };

    vkCmdCopyImageToBuffer(
        SourceCmdBuffer,
        SourceImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        TargetSlot.Buffer.BufferHandle,
        1u,
        &CopyRegion);

    VkMemoryBarrier HostBarrier {};
    HostBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    HostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    HostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(
        SourceCmdBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0u,
        1u,
        &HostBarrier,
        0u,
        nullptr,
        0u,
        nullptr);

    TargetSlot.WriteCursor = DestOffset + TotalBytes;
    Ticket.SlotIndex       = ActiveSlot;
    Ticket.ByteOffset      = DestOffset;
    Ticket.ByteLength      = TotalBytes;
    return Ticket;
}

void AsyncReadbackRing::BindTicketTimelineSignal(ReadbackTicket& Ticket, uint64_t TimelineSignal) noexcept
{
    Ticket.TimelineSignal = TimelineSignal;
    if (Ticket.SlotIndex < RingSlotCount)
    {
        Slots[Ticket.SlotIndex].LastSignal = TimelineSignal;
    }
}

bool AsyncReadbackRing::QueryTicketReady(const ReadbackTicket& Ticket) const noexcept
{
    return Context != nullptr && Ticket.Valid() && Context->QueryTimelineReached(Ticket.TimelineSignal);
}

bool AsyncReadbackRing::AwaitAndCopyBytes(const ReadbackTicket& Ticket, std::span<uint8_t> DestinationBytes) const noexcept
{
    if (Context == nullptr || !Ticket.Valid() || DestinationBytes.size() < Ticket.ByteLength || Ticket.SlotIndex >= RingSlotCount)
    {
        return false;
    }
    if (!Context->AwaitTimelineValue(Ticket.TimelineSignal))
    {
        return false;
    }

    const StagingSlot& Slot = Slots[Ticket.SlotIndex];
    if (Slot.Buffer.MappedPtr == nullptr || Ticket.ByteOffset + Ticket.ByteLength > Slot.Buffer.ByteCapacity)
    {
        return false;
    }

    const auto* SourceBytes = static_cast<const uint8_t*>(Slot.Buffer.MappedPtr) + Ticket.ByteOffset;
    std::memcpy(DestinationBytes.data(), SourceBytes, static_cast<std::size_t>(Ticket.ByteLength));
    return true;
}

} // namespace Frontier::GeometricRaster
