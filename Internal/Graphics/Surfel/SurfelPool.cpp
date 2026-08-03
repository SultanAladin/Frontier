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
#include <filesystem>
#include <string>
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

// Allocate a host-visible staging buffer of ByteSize as a copy DESTINATION (TRANSFER_DST). Mirrors AllocateStagingWithData
// but does NOT map/fill — the caller copies a device-local buffer INTO it on a command buffer, then maps to read it back.
bool AllocateStagingReadback(VulkanHost& Host, VkDeviceSize ByteSize, VkBuffer& OutBuffer, VkDeviceMemory& OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;
    if (ByteSize == 0) ByteSize = 4;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteSize;
    BufferInformation.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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

//------------------------------------------------------------------------------------------------------------------------
//                                                       DIAGNOSTIC DUMP
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// Copy SourceBuffer[0..ByteSize) into a fresh host-visible readback buffer on a one-shot command buffer, submit, wait, and
// hand back the still-mapped bytes in OutBytes. The staging buffer/memory are freed here (bytes are copied into the vector).
bool CopyDeviceBufferToVector(VulkanHost&           Host,
                              VkCommandPool         CommandPool,
                              VkBuffer              SourceBuffer,
                              VkDeviceSize          ByteSize,
                              std::vector<uint8_t>& OutBytes)
{
    OutBytes.clear();
    if (SourceBuffer == VK_NULL_HANDLE || ByteSize == 0)
        return false;

    VkBuffer       Staging = VK_NULL_HANDLE;
    VkDeviceMemory Memory  = VK_NULL_HANDLE;
    if (!AllocateStagingReadback(Host, ByteSize, Staging, Memory))
        return false;

    bool Copied = false;
    VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandAllocate.commandPool        = CommandPool;
    CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CommandAllocate.commandBufferCount = 1;
    VkCommandBuffer CopyCommand = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &CopyCommand) == VK_SUCCESS)
    {
        VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(CopyCommand, &BeginInformation) == VK_SUCCESS)
        {
            VkBufferCopy Region = { 0, 0, ByteSize };
            vkCmdCopyBuffer(CopyCommand, SourceBuffer, Staging, 1, &Region);
            if (vkEndCommandBuffer(CopyCommand) == VK_SUCCESS)
            {
                VkFence Fence = VK_NULL_HANDLE;
                VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
                if (vkCreateFence(Host.Device, &FenceInformation, Host.Allocator, &Fence) == VK_SUCCESS)
                {
                    VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
                    SubmitInformation.commandBufferCount = 1;
                    SubmitInformation.pCommandBuffers    = &CopyCommand;
                    if (vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInformation, Fence) == VK_SUCCESS &&
                        vkWaitForFences(Host.Device, 1, &Fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS)
                    {
                        void* Mapped = nullptr;
                        if (vkMapMemory(Host.Device, Memory, 0, ByteSize, 0, &Mapped) == VK_SUCCESS)
                        {
                            OutBytes.resize((size_t)ByteSize);
                            std::memcpy(OutBytes.data(), Mapped, (size_t)ByteSize);
                            vkUnmapMemory(Host.Device, Memory);
                            Copied = true;
                        }
                    }
                    vkDestroyFence(Host.Device, Fence, Host.Allocator);
                }
            }
        }
        vkFreeCommandBuffers(Host.Device, CommandPool, 1, &CopyCommand);
    }

    vkDestroyBuffer(Host.Device, Staging, Host.Allocator);
    vkFreeMemory(Host.Device, Memory, Host.Allocator);
    return Copied;
}

// Read one signed int32 back from a single-int atomic buffer (AliveCount / PoolMax). Returns Fallback on any copy failure.
int32_t ReadAtomicInt(VulkanHost& Host, VkCommandPool CommandPool, VkBuffer Buffer, int32_t Fallback)
{
    std::vector<uint8_t> Bytes;
    if (Buffer == VK_NULL_HANDLE || !CopyDeviceBufferToVector(Host, CommandPool, Buffer, sizeof(int32_t), Bytes) || Bytes.size() < sizeof(int32_t))
        return Fallback;
    int32_t Value = Fallback;
    std::memcpy(&Value, Bytes.data(), sizeof(int32_t));
    return Value;
}

} // namespace

bool DumpSurfelStateToDisk(const SurfelPool& Pool,
                           VkBuffer          TileAllocBuffer,
                           VkBuffer          TileCandidateBuffer,
                           uint32_t          TileCount,
                           VkCommandPool     CommandPool,
                           const float       CameraEye[3],
                           uint32_t          FrameIndex,
                           uint32_t          DumpSequence,
                           const char*       OutputDirectory)
{
    if (Pool.Host == nullptr || Pool.Host->Device == VK_NULL_HANDLE || !Pool.ReadyCondition)
    {
        ReportSurfelPool("dump skipped — pool not ready");
        return false;
    }
    VulkanHost& Host = *Pool.Host;
    const char* Directory = (OutputDirectory && OutputDirectory[0]) ? OutputDirectory : "SurfelDumps";

    // Create the output directory if it is missing (a fresh checkout has no dump folder). Best-effort — a create failure just
    // means the fopen below fails and we report it; std::filesystem swallows its own error via the non-throwing overload.
    {
        std::error_code DirError;
        std::filesystem::create_directories(Directory, DirError);
    }

    // --- 1. copy the whole surfel record buffer back (stride 32 B; Capacity records) --------------------------------------
    const VkDeviceSize SurfelBytes = (VkDeviceSize)Pool.Capacity * SurfelStrideFloats * sizeof(float);
    std::vector<uint8_t> SurfelBytesHost;
    if (!CopyDeviceBufferToVector(Host, CommandPool, Pool.SurfelBuffer, SurfelBytes, SurfelBytesHost))
    {
        ReportSurfelPool("dump failed — surfel readback copy failed");
        return false;
    }
    const SurfelRecord* Records = reinterpret_cast<const SurfelRecord*>(SurfelBytesHost.data());
    const uint32_t      RecordCount = (uint32_t)(SurfelBytesHost.size() / sizeof(SurfelRecord));

    // Bounds hint from the atomics (best-effort; the age filter below is the authority, so a failed read just widens the scan).
    const int32_t AliveCount = ReadAtomicInt(Host, CommandPool, Pool.AliveCountBuffer, -1);
    const int32_t PoolMax    = ReadAtomicInt(Host, CommandPool, Pool.PoolMaxBuffer,    -1);
    const uint32_t ScanLimit = (PoolMax > 0 && (uint32_t)PoolMax < RecordCount) ? (uint32_t)PoolMax + 1u : RecordCount;

    // --- 2. copy this-frame spawn-request buffers back (optional) ---------------------------------------------------------
    std::vector<uint8_t> TileAllocHost;      // TileCount x int32 (1 == requested)
    std::vector<uint8_t> TileCandidateHost;  // TileCount x 2 x vec4 (32 B / tile): [0]=pos.xyz + frame in .w, [1]=normal.xyz
    bool HaveSpawns = false;
    if (TileAllocBuffer != VK_NULL_HANDLE && TileCandidateBuffer != VK_NULL_HANDLE && TileCount > 0)
    {
        const VkDeviceSize AllocBytes     = (VkDeviceSize)TileCount * sizeof(int32_t);
        const VkDeviceSize CandidateBytes = (VkDeviceSize)TileCount * 2 * 4 * sizeof(float);
        HaveSpawns = CopyDeviceBufferToVector(Host, CommandPool, TileAllocBuffer,     AllocBytes,     TileAllocHost) &&
                     CopyDeviceBufferToVector(Host, CommandPool, TileCandidateBuffer, CandidateBytes, TileCandidateHost);
        if (!HaveSpawns)
            ReportSurfelPool("dump note — spawn-request readback failed; writing live surfels only");
    }
    const int32_t* TileAlloc     = HaveSpawns ? reinterpret_cast<const int32_t*>(TileAllocHost.data())     : nullptr;
    const float*   TileCandidate = HaveSpawns ? reinterpret_cast<const float*>(TileCandidateHost.data())   : nullptr;

    // --- 3. build the file paths ------------------------------------------------------------------------------------------
    // Each L press passes a distinct DumpSequence so successive presses ACCUMULATE numbered snapshots instead of clobbering
    // the last dump. FrameIndex alone is not a safe key (a paused / near-identical frame repeats it), so the monotonic press
    // counter owns the filename. A 4-digit zero-padded suffix sorts naturally in a file listing and in the viewer's picker.
    char Suffix[16];
    std::snprintf(Suffix, sizeof(Suffix), "-%04u", DumpSequence);
    const std::string LivePath  = std::string(Directory) + "/surfel-live"  + Suffix + ".csv";
    const std::string SpawnPath = std::string(Directory) + "/surfel-spawns" + Suffix + ".csv";
    const std::string JsonPath  = std::string(Directory) + "/surfel-dump"  + Suffix + ".json";

    const float Eye[3] = { CameraEye ? CameraEye[0] : 0.0f, CameraEye ? CameraEye[1] : 0.0f, CameraEye ? CameraEye[2] : 0.0f };

    // --- 4. surfel-live.csv: one row per LIVE surfel (Age < SurfelTtl && Age != SurfelLifeRecycle) ------------------------
    uint32_t LiveWritten = 0;
    if (FILE* LiveFile = std::fopen(LivePath.c_str(), "wb"))
    {
        std::fprintf(LiveFile, "# surfel-live  frame=%u  cameraEye=%.4f,%.4f,%.4f  capacity=%u  aliveCount=%d  poolMax=%d  ttl=%d\n",
                     FrameIndex, Eye[0], Eye[1], Eye[2], Pool.Capacity, AliveCount, PoolMax, (int)SurfelTtl);
        std::fprintf(LiveFile, "x,y,z,nx,ny,nz,age\n");
        for (uint32_t Index = 0; Index < ScanLimit && Index < RecordCount; ++Index)
        {
            const SurfelRecord& R = Records[Index];
            if (R.Age >= SurfelTtl || R.Age == SurfelLifeRecycle || R.Age == SurfelLifeRecycled)
                continue;
            std::fprintf(LiveFile, "%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%d\n",
                         R.PositionX, R.PositionY, R.PositionZ, R.NormalX, R.NormalY, R.NormalZ, R.Age);
            ++LiveWritten;
        }
        std::fclose(LiveFile);
    }
    else
    {
        ReportSurfelPool("dump failed — could not open surfel-live.csv for writing");
        return false;
    }

    // --- 5. surfel-spawns.csv: one row per tile with TileAlloc==1 ---------------------------------------------------------
    uint32_t SpawnWritten = 0;
    if (FILE* SpawnFile = std::fopen(SpawnPath.c_str(), "wb"))
    {
        std::fprintf(SpawnFile, "# surfel-spawns  frame=%u  cameraEye=%.4f,%.4f,%.4f  tileCount=%u\n",
                     FrameIndex, Eye[0], Eye[1], Eye[2], TileCount);
        std::fprintf(SpawnFile, "tile,x,y,z,nx,ny,nz,frame\n");
        if (HaveSpawns)
        {
            for (uint32_t Tile = 0; Tile < TileCount; ++Tile)
            {
                if (TileAlloc[Tile] != 1)
                    continue;
                const float* Pos    = &TileCandidate[Tile * 8 + 0];   // vec4 [0]: xyz pos, w = frame stamp
                const float* Normal = &TileCandidate[Tile * 8 + 4];   // vec4 [1]: xyz normal
                std::fprintf(SpawnFile, "%u,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%d\n",
                             Tile, Pos[0], Pos[1], Pos[2], Normal[0], Normal[1], Normal[2], (int)Pos[3]);
                ++SpawnWritten;
            }
        }
        std::fclose(SpawnFile);
    }

    // --- 6. surfel-dump.json: camera + counts + the two arrays (one file the HTML viewer fetches) -------------------------
    if (FILE* JsonFile = std::fopen(JsonPath.c_str(), "wb"))
    {
        std::fprintf(JsonFile, "{\n");
        std::fprintf(JsonFile, "  \"frame\": %u,\n", FrameIndex);
        std::fprintf(JsonFile, "  \"cameraEye\": [%.5f, %.5f, %.5f],\n", Eye[0], Eye[1], Eye[2]);
        std::fprintf(JsonFile, "  \"capacity\": %u,\n", Pool.Capacity);
        std::fprintf(JsonFile, "  \"aliveCount\": %d,\n", AliveCount);
        std::fprintf(JsonFile, "  \"liveCount\": %u,\n", LiveWritten);
        std::fprintf(JsonFile, "  \"spawnCount\": %u,\n", SpawnWritten);
        // live: flat [x,y,z,nx,ny,nz,age, ...]
        std::fprintf(JsonFile, "  \"live\": [");
        {
            uint32_t Emitted = 0;
            for (uint32_t Index = 0; Index < ScanLimit && Index < RecordCount; ++Index)
            {
                const SurfelRecord& R = Records[Index];
                if (R.Age >= SurfelTtl || R.Age == SurfelLifeRecycle || R.Age == SurfelLifeRecycled)
                    continue;
                std::fprintf(JsonFile, "%s%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d",
                             (Emitted == 0 ? "" : ","),
                             R.PositionX, R.PositionY, R.PositionZ, R.NormalX, R.NormalY, R.NormalZ, R.Age);
                ++Emitted;
            }
        }
        std::fprintf(JsonFile, "],\n");
        // spawns: flat [x,y,z,nx,ny,nz,frame, ...]
        std::fprintf(JsonFile, "  \"spawns\": [");
        if (HaveSpawns)
        {
            uint32_t Emitted = 0;
            for (uint32_t Tile = 0; Tile < TileCount; ++Tile)
            {
                if (TileAlloc[Tile] != 1)
                    continue;
                const float* Pos    = &TileCandidate[Tile * 8 + 0];
                const float* Normal = &TileCandidate[Tile * 8 + 4];
                std::fprintf(JsonFile, "%s%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d",
                             (Emitted == 0 ? "" : ","),
                             Pos[0], Pos[1], Pos[2], Normal[0], Normal[1], Normal[2], (int)Pos[3]);
                ++Emitted;
            }
        }
        std::fprintf(JsonFile, "]\n}\n");
        std::fclose(JsonFile);
    }

    std::fprintf(stdout, "[surfel] dumped %u live, %u spawn requests -> %s , %s , %s\n",
                 LiveWritten, SpawnWritten, LivePath.c_str(), SpawnPath.c_str(), JsonPath.c_str());
    std::fflush(stdout);
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

    Pool = SurfelPool{};
}

} // namespace Frontier
