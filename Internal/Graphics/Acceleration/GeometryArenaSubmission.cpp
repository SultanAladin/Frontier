/*==============================================================================================================================================
                                                       GEOMETRYARENASUBMISSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the bottom-level tree arena. Initialize attaches a host and clears the accumulators. AppendGeometryTreeToArena copies one
//    built tree's node words, primitive order and (when dynamic) parent table onto the end of the host accumulation, recording the slice that
//    describes where they landed. UploadGeometryArena allocates the device buffers once, stages every accumulation across in a single blocking
//    transfer, and releases the host copies. Raw Vulkan, no VMA, mirroring the VolumeBoundsSubmission idiom — the memory-selection, allocation and
//    blocking-transfer helpers below are lifted verbatim from that file rather than rewritten.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Acceleration/GeometryArenaSubmission.h"

#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

void ReportGeometryArena(const char* MessageText)
{
    std::fprintf(stderr, "[GeometryArena] %s\n", MessageText);
}

// First memory type allowed by the requirement bitmask carrying every required property bit — verbatim from VolumeBoundsSubmission.cpp:34.
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

// Create a buffer of ByteSize with the given usage, backed by memory carrying the required property bits — verbatim from
// VolumeBoundsSubmission.cpp:52. On any failure both out-handles are null.
bool AllocateBackedBuffer(VulkanHost&           Host,
                          VkDeviceSize          ByteSize,
                          VkBufferUsageFlags    Usage,
                          VkMemoryPropertyFlags MemoryProperties,
                          VkBuffer&             OutBuffer,
                          VkDeviceMemory&       OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteSize;
    BufferInformation.usage       = Usage;
    BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Host.Device, &BufferInformation, Host.Allocator, &OutBuffer) != VK_SUCCESS)
    {
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Host.Device, OutBuffer, &MemoryRequirements);

    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice, MemoryRequirements.memoryTypeBits, MemoryProperties, MemoryTypeFound);
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

// Run a one-shot recorded command buffer to completion on the graphics queue — verbatim from VolumeBoundsSubmission.cpp:134.
template <typename RecorderType>
bool ExecuteBlockingTransfer(VulkanHost& Host, VkCommandPool CommandPool, RecorderType Recorder)
{
    VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandAllocate.commandPool        = CommandPool;
    CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CommandAllocate.commandBufferCount = 1;
    VkCommandBuffer TransferCommand = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &TransferCommand) != VK_SUCCESS)
        return false;

    bool Succeeded = false;
    VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(TransferCommand, &BeginInformation) == VK_SUCCESS)
    {
        Recorder(TransferCommand);
        if (vkEndCommandBuffer(TransferCommand) == VK_SUCCESS)
        {
            VkFence Fence = VK_NULL_HANDLE;
            VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            if (vkCreateFence(Host.Device, &FenceInformation, Host.Allocator, &Fence) == VK_SUCCESS)
            {
                VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
                SubmitInformation.commandBufferCount = 1;
                SubmitInformation.pCommandBuffers    = &TransferCommand;
                if (vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInformation, Fence) == VK_SUCCESS &&
                    vkWaitForFences(Host.Device, 1, &Fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS)
                {
                    Succeeded = true;
                }
                vkDestroyFence(Host.Device, Fence, Host.Allocator);
            }
        }
    }
    vkFreeCommandBuffers(Host.Device, CommandPool, 1, &TransferCommand);
    return Succeeded;
}

// Stage one host array into a fresh device-local buffer through a HOST_VISIBLE intermediate. Returns false with both out-handles null on any
// failure. Empty input is a success that produces no buffer, which is how the parent table stays unallocated for a fully static scene.
bool UploadWordArray(VulkanHost&                  Host,
                     VkCommandPool                CommandPool,
                     const std::vector<uint32_t>& Words,
                     VkBuffer&                    OutBuffer,
                     VkDeviceMemory&              OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;
    if (Words.empty())
        return true;

    const VkDeviceSize ByteSize = (VkDeviceSize)Words.size() * sizeof(uint32_t);

    VkBuffer       StagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    if (!AllocateBackedBuffer(Host, ByteSize,
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              StagingBuffer, StagingMemory))
    {
        ReportGeometryArena("upload staging allocation failed");
        return false;
    }

    bool Succeeded = false;
    void* Mapped = nullptr;
    if (vkMapMemory(Host.Device, StagingMemory, 0, ByteSize, 0, &Mapped) == VK_SUCCESS && Mapped != nullptr)
    {
        std::memcpy(Mapped, Words.data(), (size_t)ByteSize);
        vkUnmapMemory(Host.Device, StagingMemory);

        if (AllocateBackedBuffer(Host, ByteSize,
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 OutBuffer, OutMemory))
        {
            Succeeded = ExecuteBlockingTransfer(Host, CommandPool, [&](VkCommandBuffer TransferCommand)
            {
                VkBufferCopy Region = {};
                Region.srcOffset = 0;
                Region.dstOffset = 0;
                Region.size      = ByteSize;
                vkCmdCopyBuffer(TransferCommand, StagingBuffer, OutBuffer, 1, &Region);
            });

            if (!Succeeded)
            {
                ReportGeometryArena("upload transfer failed");
                vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
                vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
                OutBuffer = VK_NULL_HANDLE;
                OutMemory = VK_NULL_HANDLE;
            }
        }
        else
        {
            ReportGeometryArena("device-local arena allocation failed");
        }
    }
    else
    {
        ReportGeometryArena("upload staging map failed");
    }

    vkDestroyBuffer(Host.Device, StagingBuffer, Host.Allocator);
    vkFreeMemory(Host.Device, StagingMemory, Host.Allocator);
    return Succeeded;
}

// Release a buffer/memory pair and null both handles. Tolerates either being null already.
void ReleaseBackedBuffer(VulkanHost& Host, VkBuffer& Buffer, VkDeviceMemory& Memory)
{
    if (Buffer != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, Buffer, Host.Allocator);
    if (Memory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Memory, Host.Allocator);
    Buffer = VK_NULL_HANDLE;
    Memory = VK_NULL_HANDLE;
}

// Reinterpret the slice table as a flat word array so it stages through the same path as the other three. The struct is all uint32_t and pinned at
// 32 B by the header's static_assert, so this is a layout-exact view, not a serialisation.
std::vector<uint32_t> FlattenSliceTable(const std::vector<GeometryArenaSlice>& Slices)
{
    const size_t WordsPerSlice = sizeof(GeometryArenaSlice) / sizeof(uint32_t);
    std::vector<uint32_t> Flattened(Slices.size() * WordsPerSlice);
    if (!Slices.empty())
        std::memcpy(Flattened.data(), Slices.data(), Slices.size() * sizeof(GeometryArenaSlice));
    return Flattened;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeGeometryArenaSubmission(GeometryArenaSubmission& Arena, VulkanHost& Host)
{
    Arena = GeometryArenaSubmission();

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportGeometryArena("initialize called with no device");
        return false;
    }

    Arena.Host           = &Host;
    Arena.ReadyCondition = true;
    return true;
}

bool AppendGeometryTreeToArena(GeometryArenaSubmission& Arena,
                               const GeometryTree&      Tree,
                               uint32_t                 VertexOffset,
                               uint32_t                 IndexOffset,
                               uint32_t&                OutMeshOrdinal)
{
    OutMeshOrdinal = 0;

    if (!Arena.ReadyCondition)
        return false;

    // 🔴 See the header: a post-upload append would leave the device sized without this mesh while the host slice claims it is there, so the trace
    //    would walk past the end of the node buffer. Refused rather than half-honoured.
    if (Arena.UploadedCondition)
    {
        ReportGeometryArena("append after upload — the arena is sealed; refusing");
        return false;
    }

    if (!Tree.ReadyCondition || Tree.NodeCount == 0 || Tree.NodeWords.empty())
    {
        ReportGeometryArena("append given a tree that is not ready");
        return false;
    }

    // The node blob must be exactly NodeCount whole nodes. A partial trailing node means the builder and this arena disagree about the stride, and
    // every slice appended after it would be offset — so it is caught here rather than surfacing as a wrong root box three meshes later.
    if (Tree.NodeWords.size() != (size_t)Tree.NodeCount * GeometryTreeWordsPerNode)
    {
        ReportGeometryArena("append given a node blob whose word count disagrees with NodeCount");
        return false;
    }

    // 📝 A populated parent table must carry exactly one entry per node; that is what makes the refit's child->parent walk total. An empty table is
    //    the ordinary static case and is not an error.
    const bool DynamicTree = !Tree.ParentTable.empty();
    if (DynamicTree && Tree.ParentTable.size() != (size_t)Tree.NodeCount)
    {
        ReportGeometryArena("append given a parent table that is neither empty nor one entry per node");
        return false;
    }

    GeometryArenaSlice Slice;
    Slice.NodeOffset      = (uint32_t)Arena.PendingNodeWords.size();
    Slice.NodeCount       = Tree.NodeCount;
    Slice.PrimitiveOffset = (uint32_t)Arena.PendingPrimitiveOrder.size();
    Slice.PrimitiveCount  = (uint32_t)Tree.PrimitiveOrder.size();
    Slice.VertexOffset    = VertexOffset;
    Slice.IndexOffset     = IndexOffset;
    Slice.ParentOffset    = DynamicTree ? (uint32_t)Arena.PendingParentTable.size() : GeometryTreeNoParent;
    Slice.Padding         = 0;

    Arena.PendingNodeWords.insert(Arena.PendingNodeWords.end(), Tree.NodeWords.begin(), Tree.NodeWords.end());
    Arena.PendingPrimitiveOrder.insert(Arena.PendingPrimitiveOrder.end(), Tree.PrimitiveOrder.begin(), Tree.PrimitiveOrder.end());
    if (DynamicTree)
        Arena.PendingParentTable.insert(Arena.PendingParentTable.end(), Tree.ParentTable.begin(), Tree.ParentTable.end());

    OutMeshOrdinal = (uint32_t)Arena.Slices.size();
    Arena.Slices.push_back(Slice);
    return true;
}

bool UploadGeometryArena(GeometryArenaSubmission& Arena, VkCommandPool CommandPool)
{
    if (!Arena.ReadyCondition || Arena.Host == nullptr || CommandPool == VK_NULL_HANDLE)
        return false;

    if (Arena.UploadedCondition)
    {
        ReportGeometryArena("upload called twice — the arena is already sealed");
        return false;
    }

    if (Arena.Slices.empty() || Arena.PendingNodeWords.empty())
    {
        ReportGeometryArena("upload called with nothing appended");
        return false;
    }

    VulkanHost& Host = *Arena.Host;
    const std::vector<uint32_t> SliceWords = FlattenSliceTable(Arena.Slices);

    const bool Uploaded = UploadWordArray(Host, CommandPool, Arena.PendingNodeWords,      Arena.NodeBuffer,      Arena.NodeMemory)      &&
                          UploadWordArray(Host, CommandPool, Arena.PendingPrimitiveOrder, Arena.PrimitiveBuffer, Arena.PrimitiveMemory) &&
                          UploadWordArray(Host, CommandPool, SliceWords,                  Arena.SliceBuffer,     Arena.SliceMemory)     &&
                          UploadWordArray(Host, CommandPool, Arena.PendingParentTable,    Arena.ParentBuffer,    Arena.ParentMemory);

    if (!Uploaded)
    {
        ReleaseBackedBuffer(Host, Arena.NodeBuffer,      Arena.NodeMemory);
        ReleaseBackedBuffer(Host, Arena.PrimitiveBuffer, Arena.PrimitiveMemory);
        ReleaseBackedBuffer(Host, Arena.SliceBuffer,     Arena.SliceMemory);
        ReleaseBackedBuffer(Host, Arena.ParentBuffer,    Arena.ParentMemory);
        return false;
    }

    // 📝 The host accumulations are released, not kept: they are a full second copy of the arena and nothing reads them after the upload. Slices
    //    stays, because RetrieveGeometryArenaSlice serves it without a readback.
    Arena.PendingNodeWords      = std::vector<uint32_t>();
    Arena.PendingPrimitiveOrder = std::vector<uint32_t>();
    Arena.PendingParentTable    = std::vector<uint32_t>();

    Arena.UploadedCondition = true;
    return true;
}

void RetrieveGeometryArenaBuffers(const GeometryArenaSubmission& Arena,
                                  VkBuffer&                      OutNodeBuffer,
                                  VkBuffer&                      OutPrimitiveBuffer,
                                  VkBuffer&                      OutSliceBuffer,
                                  VkBuffer&                      OutParentBuffer)
{
    OutNodeBuffer      = Arena.NodeBuffer;
    OutPrimitiveBuffer = Arena.PrimitiveBuffer;
    OutSliceBuffer     = Arena.SliceBuffer;
    OutParentBuffer    = Arena.ParentBuffer;
}

GeometryArenaSlice RetrieveGeometryArenaSlice(const GeometryArenaSubmission& Arena, uint32_t MeshOrdinal)
{
    if (MeshOrdinal >= Arena.Slices.size())
        return GeometryArenaSlice();
    return Arena.Slices[MeshOrdinal];
}

void FinalizeGeometryArenaSubmission(GeometryArenaSubmission& Arena)
{
    if (Arena.Host == nullptr || Arena.Host->Device == VK_NULL_HANDLE)
    {
        Arena = GeometryArenaSubmission();
        return;
    }

    VulkanHost& Host = *Arena.Host;
    ReleaseBackedBuffer(Host, Arena.NodeBuffer,      Arena.NodeMemory);
    ReleaseBackedBuffer(Host, Arena.PrimitiveBuffer, Arena.PrimitiveMemory);
    ReleaseBackedBuffer(Host, Arena.SliceBuffer,     Arena.SliceMemory);
    ReleaseBackedBuffer(Host, Arena.ParentBuffer,    Arena.ParentMemory);

    Arena = GeometryArenaSubmission();
}

} // namespace Frontier
