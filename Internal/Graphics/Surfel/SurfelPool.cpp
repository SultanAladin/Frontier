/*==============================================================================================================================================
                                                             SURFELPOOL.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the surfel pool's storage. Initialize allocates every device-local buffer at a fixed capacity and clears each to its correct
//    initial state on a one-shot command buffer it submits and waits on: the four F7 buffers (Moments / Touched / Guiding / SurfelDepth) to zero, the
//    free-list to the identity stack 0..cap-1 (staged from the CPU, since vkCmdFillBuffer can only write one repeated 32-bit value and the stack is
//    ascending), and the three atomics to zero. Ages inside the surfel records are deliberately NOT written — Prepare owns them (F21). Raw Vulkan, no
//    VMA, mirroring InstanceCullSubmission's AllocateBackedBuffer / ClearDeviceBuffer idioms verbatim so the pool sits in the same register as the
//    rest of the compute submissions.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Surfel/SurfelPool.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

void ReportSurfelPool(const char* MessageText)
{
    std::fprintf(stderr, "[SurfelPool] %s\n", MessageText);
}

// First memory type allowed by the requirement bitmask carrying every required property bit — identical to the InstanceCull / HiZ helper.
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

// Create a device-local storage buffer of ByteSize carrying STORAGE | TRANSFER_DST | TRANSFER_SRC (every pool buffer is written by the GPU and
// cleared/seeded by a transfer; TRANSFER_SRC lets the validation gate copy the buffer back to a host-visible staging buffer for readback). On any
// failure both out-handles are null.
bool AllocateStorageBuffer(VulkanHost& Host, VkDeviceSize ByteSize, VkBuffer& OutBuffer, VkDeviceMemory& OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;
    if (ByteSize == 0) ByteSize = 4;   // never create a zero-size buffer

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteSize;
    BufferInformation.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Host.Device, &BufferInformation, Host.Allocator, &OutBuffer) != VK_SUCCESS)
    {
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Host.Device, OutBuffer, &MemoryRequirements);

    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice, MemoryRequirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, MemoryTypeFound);
    if (!MemoryTypeFound)
    {
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &OutMemory) != VK_SUCCESS ||
        vkBindBufferMemory(Host.Device, OutBuffer, OutMemory, 0) != VK_SUCCESS)
    {
        if (OutMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Allocate a temporary host-visible staging buffer, memcpy Bytes into it, and return it mapped-and-filled. The caller copies from it and frees it.
bool AllocateStagingWithData(VulkanHost& Host, const void* Bytes, VkDeviceSize ByteSize, VkBuffer& OutBuffer, VkDeviceMemory& OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteSize;
    BufferInformation.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Host.Device, &BufferInformation, Host.Allocator, &OutBuffer) != VK_SUCCESS)
    {
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Host.Device, OutBuffer, &MemoryRequirements);
    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice, MemoryRequirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, MemoryTypeFound);
    if (!MemoryTypeFound)
    {
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &OutMemory) != VK_SUCCESS ||
        vkBindBufferMemory(Host.Device, OutBuffer, OutMemory, 0) != VK_SUCCESS)
    {
        if (OutMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        return false;
    }

    void* Mapped = nullptr;
    if (vkMapMemory(Host.Device, OutMemory, 0, ByteSize, 0, &Mapped) != VK_SUCCESS)
    {
        vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        return false;
    }
    std::memcpy(Mapped, Bytes, (size_t)ByteSize);
    vkUnmapMemory(Host.Device, OutMemory);
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSurfelPool(SurfelPool&   Pool,
                          VulkanHost&   Host,
                          VkCommandPool CommandPool,
                          uint32_t      Capacity)
{
    Pool = SurfelPool{};
    Pool.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportSurfelPool("no device — pool not built");
        return false;
    }
    if (Capacity == 0) Capacity = 1;
    Pool.Capacity = Capacity;

    // --- byte sizes, one per buffer -------------------------------------------------------------------------------------
    const VkDeviceSize SurfelBytes  = (VkDeviceSize)Capacity * SurfelStrideFloats  * sizeof(float);   // 32 B / surfel
    const VkDeviceSize PoolBytes    = (VkDeviceSize)Capacity * sizeof(int32_t);
    const VkDeviceSize AtomicBytes  = (VkDeviceSize)sizeof(int32_t);
    const VkDeviceSize MomentsBytes = (VkDeviceSize)Capacity * SurfelMomentsFloats * 2 * sizeof(float);
    const VkDeviceSize TouchedBytes = (VkDeviceSize)Capacity * sizeof(int32_t);
    const VkDeviceSize GuidingBytes = (VkDeviceSize)Capacity * SurfelGuidingFloats * sizeof(float);
    const VkDeviceSize DepthBytes   = (VkDeviceSize)Capacity * SurfelDepthFloats   * sizeof(float);

    if (!AllocateStorageBuffer(Host, SurfelBytes,  Pool.SurfelBuffer,     Pool.SurfelMemory)     ||
        !AllocateStorageBuffer(Host, PoolBytes,    Pool.PoolBuffer,       Pool.PoolMemory)       ||
        !AllocateStorageBuffer(Host, AtomicBytes,  Pool.AliveCountBuffer, Pool.AliveCountMemory) ||
        !AllocateStorageBuffer(Host, AtomicBytes,  Pool.PoolAllocBuffer,  Pool.PoolAllocMemory)  ||
        !AllocateStorageBuffer(Host, AtomicBytes,  Pool.PoolMaxBuffer,    Pool.PoolMaxMemory)    ||
        !AllocateStorageBuffer(Host, MomentsBytes, Pool.MomentsBuffer,    Pool.MomentsMemory)    ||
        !AllocateStorageBuffer(Host, TouchedBytes, Pool.TouchedBuffer,    Pool.TouchedMemory)    ||
        !AllocateStorageBuffer(Host, GuidingBytes, Pool.GuidingBuffer,    Pool.GuidingMemory)    ||
        !AllocateStorageBuffer(Host, DepthBytes,   Pool.SurfelDepthBuffer,Pool.SurfelDepthMemory))
    {
        ReportSurfelPool("pool buffer allocation failed");
        FinalizeSurfelPool(Pool);
        return false;
    }

    // 🩺 DIAGNOSTIC-ONLY (task #37): a 12-byte host-visible readback mirror of the three atomics. Non-fatal — a failure just leaves the diagnostic off.
    {
        const VkDeviceSize ReadbackBytes = 3u * sizeof(int32_t);
        VkBufferCreateInfo ReadbackInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        ReadbackInformation.size        = ReadbackBytes;
        ReadbackInformation.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        ReadbackInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Host.Device, &ReadbackInformation, Host.Allocator, &Pool.AtomicReadbackBuffer) == VK_SUCCESS)
        {
            VkMemoryRequirements ReadbackRequirements = {};
            vkGetBufferMemoryRequirements(Host.Device, Pool.AtomicReadbackBuffer, &ReadbackRequirements);
            bool ReadbackTypeFound = false;
            const uint32_t ReadbackTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice, ReadbackRequirements.memoryTypeBits,
                                                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ReadbackTypeFound);
            if (ReadbackTypeFound)
            {
                VkMemoryAllocateInfo ReadbackAllocate = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
                ReadbackAllocate.allocationSize  = ReadbackRequirements.size;
                ReadbackAllocate.memoryTypeIndex = ReadbackTypeIndex;
                if (vkAllocateMemory(Host.Device, &ReadbackAllocate, Host.Allocator, &Pool.AtomicReadbackMemory) == VK_SUCCESS &&
                    vkBindBufferMemory(Host.Device, Pool.AtomicReadbackBuffer, Pool.AtomicReadbackMemory, 0) == VK_SUCCESS)
                {
                    Pool.AtomicReadbackReady = true;
                }
            }
        }
        if (!Pool.AtomicReadbackReady)
        {
            if (Pool.AtomicReadbackMemory != VK_NULL_HANDLE) { vkFreeMemory(Host.Device, Pool.AtomicReadbackMemory, Host.Allocator); Pool.AtomicReadbackMemory = VK_NULL_HANDLE; }
            if (Pool.AtomicReadbackBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(Host.Device, Pool.AtomicReadbackBuffer, Host.Allocator); Pool.AtomicReadbackBuffer = VK_NULL_HANDLE; }
        }
    }

    // --- stage the identity free-list 0..cap-1 (vkCmdFillBuffer cannot write an ascending sequence) ---------------------
    std::vector<int32_t> FreeList(Capacity);
    for (uint32_t Index = 0; Index < Capacity; ++Index)
        FreeList[Index] = (int32_t)Index;
    VkBuffer       StagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    if (!AllocateStagingWithData(Host, FreeList.data(), PoolBytes, StagingBuffer, StagingMemory))
    {
        ReportSurfelPool("free-list staging allocation failed");
        FinalizeSurfelPool(Pool);
        return false;
    }

    // --- one command buffer: seed the free-list, zero the four F7 buffers + the three atomics, wait -----------------------
    bool Cleared = false;
    VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandAllocate.commandPool        = CommandPool;
    CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CommandAllocate.commandBufferCount = 1;
    VkCommandBuffer ClearCommand = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &ClearCommand) == VK_SUCCESS)
    {
        VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(ClearCommand, &BeginInformation) == VK_SUCCESS)
        {
            // Free-list: identity stack from staging. Everything else the GPU sets up front is a repeated-zero, so vkCmdFillBuffer suffices.
            VkBufferCopy Region = { 0, 0, PoolBytes };
            vkCmdCopyBuffer(ClearCommand, StagingBuffer, Pool.PoolBuffer, 1, &Region);

            // 🔴 F7: the FOUR zero-fill buffers. WebGPU zero-inits these implicitly; Vulkan must be told. Leaving any one carries garbage into Phase 2.
            vkCmdFillBuffer(ClearCommand, Pool.MomentsBuffer,     0, MomentsBytes, 0u);
            vkCmdFillBuffer(ClearCommand, Pool.TouchedBuffer,     0, TouchedBytes, 0u);
            vkCmdFillBuffer(ClearCommand, Pool.GuidingBuffer,     0, GuidingBytes, 0u);
            vkCmdFillBuffer(ClearCommand, Pool.SurfelDepthBuffer, 0, DepthBytes,   0u);

            // The three atomics start at 0 (no live surfels, stack pointer at base, no high-water slot). NOT the ages — Prepare owns those (F21).
            vkCmdFillBuffer(ClearCommand, Pool.AliveCountBuffer, 0, AtomicBytes, 0u);
            vkCmdFillBuffer(ClearCommand, Pool.PoolAllocBuffer,  0, AtomicBytes, 0u);
            vkCmdFillBuffer(ClearCommand, Pool.PoolMaxBuffer,    0, AtomicBytes, 0u);

            if (vkEndCommandBuffer(ClearCommand) == VK_SUCCESS)
            {
                VkFence Fence = VK_NULL_HANDLE;
                VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
                if (vkCreateFence(Host.Device, &FenceInformation, Host.Allocator, &Fence) == VK_SUCCESS)
                {
                    VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
                    SubmitInformation.commandBufferCount = 1;
                    SubmitInformation.pCommandBuffers    = &ClearCommand;
                    if (vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInformation, Fence) == VK_SUCCESS &&
                        vkWaitForFences(Host.Device, 1, &Fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS)
                    {
                        Cleared = true;
                    }
                    vkDestroyFence(Host.Device, Fence, Host.Allocator);
                }
            }
        }
        vkFreeCommandBuffers(Host.Device, CommandPool, 1, &ClearCommand);
    }

    vkDestroyBuffer(Host.Device, StagingBuffer, Host.Allocator);
    vkFreeMemory(Host.Device, StagingMemory, Host.Allocator);

    if (!Cleared)
    {
        ReportSurfelPool("pool clear submission failed");
        FinalizeSurfelPool(Pool);
        return false;
    }

    Pool.MomentsParity  = 0;
    Pool.ReadyCondition = true;
    return true;
}

VkDeviceSize SurfelMomentsReadOffset(const SurfelPool& Pool)
{
    return (VkDeviceSize)Pool.MomentsParity * Pool.Capacity * SurfelMomentsFloats * sizeof(float);
}

VkDeviceSize SurfelMomentsWriteOffset(const SurfelPool& Pool)
{
    return (VkDeviceSize)(1u - Pool.MomentsParity) * Pool.Capacity * SurfelMomentsFloats * sizeof(float);
}

void SwapSurfelMoments(SurfelPool& Pool)
{
    Pool.MomentsParity = 1u - Pool.MomentsParity;
}

void RecordSurfelAtomicReadback(const SurfelPool& Pool, VkCommandBuffer CommandBuffer)
{
    if (!Pool.AtomicReadbackReady || CommandBuffer == VK_NULL_HANDLE) return;

    // Copy each single-int atomic into its slot of the 3-int host-visible mirror. The caller already fenced the atomics' COMPUTE writes; make those
    // writes visible to the transfer read (COMPUTE -> TRANSFER, SHADER_WRITE -> TRANSFER_READ) so the mirror never latches a torn value.
    VkMemoryBarrier ToTransfer = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    ToTransfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 1, &ToTransfer, 0, nullptr, 0, nullptr);

    VkBufferCopy Alive = { 0, 0 * sizeof(int32_t), sizeof(int32_t) };
    VkBufferCopy Alloc = { 0, 1 * sizeof(int32_t), sizeof(int32_t) };
    VkBufferCopy Max   = { 0, 2 * sizeof(int32_t), sizeof(int32_t) };
    vkCmdCopyBuffer(CommandBuffer, Pool.AliveCountBuffer, Pool.AtomicReadbackBuffer, 1, &Alive);
    vkCmdCopyBuffer(CommandBuffer, Pool.PoolAllocBuffer,  Pool.AtomicReadbackBuffer, 1, &Alloc);
    vkCmdCopyBuffer(CommandBuffer, Pool.PoolMaxBuffer,    Pool.AtomicReadbackBuffer, 1, &Max);
}

bool ReadSurfelAtomicReadback(const SurfelPool& Pool, int32_t& AliveOut, int32_t& AllocPtrOut, int32_t& MaxSlotOut)
{
    AliveOut = AllocPtrOut = MaxSlotOut = 0;
    if (!Pool.AtomicReadbackReady || Pool.Host == nullptr || Pool.Host->Device == VK_NULL_HANDLE) return false;

    void* Mapped = nullptr;
    if (vkMapMemory(Pool.Host->Device, Pool.AtomicReadbackMemory, 0, 3u * sizeof(int32_t), 0, &Mapped) != VK_SUCCESS) return false;
    const int32_t* Values = reinterpret_cast<const int32_t*>(Mapped);
    AliveOut    = Values[0];
    AllocPtrOut = Values[1];
    MaxSlotOut  = Values[2];
    vkUnmapMemory(Pool.Host->Device, Pool.AtomicReadbackMemory);
    return true;
}

void FinalizeSurfelPool(SurfelPool& Pool)
{
    if (Pool.Host == nullptr || Pool.Host->Device == VK_NULL_HANDLE)
    {
        Pool = SurfelPool{};
        return;
    }
    VkDevice Device = Pool.Host->Device;
    const VkAllocationCallbacks* Allocator = Pool.Host->Allocator;

    VkBuffer       Buffers[9] = { Pool.SurfelBuffer, Pool.PoolBuffer, Pool.AliveCountBuffer, Pool.PoolAllocBuffer, Pool.PoolMaxBuffer,
                                  Pool.MomentsBuffer, Pool.TouchedBuffer, Pool.GuidingBuffer, Pool.SurfelDepthBuffer };
    VkDeviceMemory Memories[9] = { Pool.SurfelMemory, Pool.PoolMemory, Pool.AliveCountMemory, Pool.PoolAllocMemory, Pool.PoolMaxMemory,
                                   Pool.MomentsMemory, Pool.TouchedMemory, Pool.GuidingMemory, Pool.SurfelDepthMemory };
    for (int Index = 0; Index < 9; ++Index)
    {
        if (Buffers[Index]  != VK_NULL_HANDLE) vkDestroyBuffer(Device, Buffers[Index], Allocator);
        if (Memories[Index] != VK_NULL_HANDLE) vkFreeMemory(Device, Memories[Index], Allocator);
    }

    // 🩺 DIAGNOSTIC-ONLY (task #37) readback staging.
    if (Pool.AtomicReadbackBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, Pool.AtomicReadbackBuffer, Allocator);
    if (Pool.AtomicReadbackMemory != VK_NULL_HANDLE) vkFreeMemory(Device, Pool.AtomicReadbackMemory, Allocator);

    Pool = SurfelPool{};
}

} // namespace Frontier
