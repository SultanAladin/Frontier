/*============================================================================================================================================
                                                          BUFFERALLOCATION.CPP
============================================================================================================================================*/
// 🧩 Device-local vertex/index upload for a display polygon. The flow is the textbook staged transfer: create a device-local
//    destination buffer (the draw path reads it), create a host-visible staging buffer, memcpy the stream bytes into staging,
//    then record a one-shot vkCmdCopyBuffer on a transient command buffer, submit it on the graphics queue, and wait a fence
//    so the copy has landed before the staging buffer is torn down. Vertices and indices are staged independently through the
//    same helper so each owns a tightly-sized allocation. Raw Vulkan + a manual memory-type search against the shared VulkanHost.

#include "Graphics/Render/Resources/BufferAllocation.h"

#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void ReportBuffer(const char* MessageText)
    {
        std::fprintf(stderr, "[BufferAllocation] %s\n", MessageText);
    }

    // 📝 First memory type that is both allowed by the buffer's requirement bitmask and carries every required property bit.
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

    // 📝 Create a buffer of ByteSize with the given usage, then back it with memory of the required property flags. On any
    //    failure both out-handles are left null and false is returned, so a caller can release unconditionally.
    bool AllocateBackedBuffer(VkPhysicalDevice      PhysicalDevice,
                              VkDevice              Device,
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
        if (vkCreateBuffer(Device, &BufferInformation, nullptr, &OutBuffer) != VK_SUCCESS)
        {
            OutBuffer = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryRequirements MemoryRequirements = {};
        vkGetBufferMemoryRequirements(Device, OutBuffer, &MemoryRequirements);

        bool MemoryTypeFound = false;
        const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(PhysicalDevice,
                                                               MemoryRequirements.memoryTypeBits,
                                                               MemoryProperties,
                                                               MemoryTypeFound);
        if (!MemoryTypeFound)
        {
            vkDestroyBuffer(Device, OutBuffer, nullptr);
            OutBuffer = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        AllocateInformation.allocationSize  = MemoryRequirements.size;
        AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
        if (vkAllocateMemory(Device, &AllocateInformation, nullptr, &OutMemory) != VK_SUCCESS)
        {
            vkDestroyBuffer(Device, OutBuffer, nullptr);
            OutBuffer = VK_NULL_HANDLE;
            OutMemory = VK_NULL_HANDLE;
            return false;
        }
        if (vkBindBufferMemory(Device, OutBuffer, OutMemory, 0) != VK_SUCCESS)
        {
            vkFreeMemory(Device, OutMemory, nullptr);
            vkDestroyBuffer(Device, OutBuffer, nullptr);
            OutBuffer = VK_NULL_HANDLE;
            OutMemory = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    // 📝 Stage SourceBytes (ByteSize bytes) into an already-created device-local destination buffer: a host-visible scratch
    //    buffer is filled, one transient command buffer records a copy, and the graphics queue runs it under a fence this
    //    function waits on. The scratch buffer + command buffer are always torn down before returning. The destination's
    //    memory is never mapped (it is device-local), so the copy is the only way its bytes arrive.
    bool StageBytesIntoBuffer(VulkanHost&     Host,
                              VkCommandPool   CommandPool,
                              VkBuffer        DestinationBuffer,
                              const void*     SourceBytes,
                              VkDeviceSize    ByteSize)
    {
        VkDevice LogicalDevice = Host.Device;

        // ─── Host-visible staging buffer, filled by a mapped memcpy ───
        VkBuffer       StagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
        if (!AllocateBackedBuffer(Host.PhysicalDevice,
                                  LogicalDevice,
                                  ByteSize,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  StagingBuffer,
                                  StagingMemory))
        {
            ReportBuffer("failed to allocate staging buffer");
            return false;
        }

        void* MappedPointer = nullptr;
        if (vkMapMemory(LogicalDevice, StagingMemory, 0, ByteSize, 0, &MappedPointer) != VK_SUCCESS)
        {
            ReportBuffer("failed to map staging memory");
            vkDestroyBuffer(LogicalDevice, StagingBuffer, nullptr);
            vkFreeMemory(LogicalDevice, StagingMemory, nullptr);
            return false;
        }
        std::memcpy(MappedPointer, SourceBytes, (size_t)ByteSize);
        vkUnmapMemory(LogicalDevice, StagingMemory);

        // ─── One transient command buffer records the copy ───
        bool TransferSucceeded = false;
        VkCommandBuffer CopyCommand = VK_NULL_HANDLE;

        VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        CommandAllocate.commandPool        = CommandPool;
        CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        CommandAllocate.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(LogicalDevice, &CommandAllocate, &CopyCommand) != VK_SUCCESS)
        {
            ReportBuffer("failed to allocate transfer command buffer");
            vkDestroyBuffer(LogicalDevice, StagingBuffer, nullptr);
            vkFreeMemory(LogicalDevice, StagingMemory, nullptr);
            return false;
        }

        VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(CopyCommand, &BeginInformation) == VK_SUCCESS)
        {
            VkBufferCopy CopyRegion = {};
            CopyRegion.srcOffset = 0;
            CopyRegion.dstOffset = 0;
            CopyRegion.size      = ByteSize;
            vkCmdCopyBuffer(CopyCommand, StagingBuffer, DestinationBuffer, 1, &CopyRegion);

            if (vkEndCommandBuffer(CopyCommand) == VK_SUCCESS)
            {
                // ─── Submit under a fence and wait until the copy has fully landed ───
                VkFence TransferFence = VK_NULL_HANDLE;
                VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
                if (vkCreateFence(LogicalDevice, &FenceInformation, nullptr, &TransferFence) == VK_SUCCESS)
                {
                    VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
                    SubmitInformation.commandBufferCount = 1;
                    SubmitInformation.pCommandBuffers    = &CopyCommand;
                    if (vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInformation, TransferFence) == VK_SUCCESS &&
                        vkWaitForFences(LogicalDevice, 1, &TransferFence, VK_TRUE, UINT64_MAX) == VK_SUCCESS)
                    {
                        TransferSucceeded = true;
                    }
                    else
                    {
                        ReportBuffer("transfer submit / fence wait failed");
                    }
                    vkDestroyFence(LogicalDevice, TransferFence, nullptr);
                }
                else
                {
                    ReportBuffer("failed to create transfer fence");
                }
            }
        }
        else
        {
            ReportBuffer("failed to begin transfer command buffer");
        }

        vkFreeCommandBuffers(LogicalDevice, CommandPool, 1, &CopyCommand);
        vkDestroyBuffer(LogicalDevice, StagingBuffer, nullptr);
        vkFreeMemory(LogicalDevice, StagingMemory, nullptr);
        return TransferSucceeded;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ConstructPolygonBufferAllocation(VulkanHost&               Host,
                                      VkCommandPool             CommandPool,
                                      const RenderVertexStream& Stream,
                                      PolygonBufferAllocation&  Result)
{
    // 📝 Always start from the empty value so a failed build, an empty stream, and a fresh value are indistinguishable.
    ReleasePolygonBufferAllocation(Host, Result);

    if (Host.Device == VK_NULL_HANDLE || Host.GraphicsQueue == VK_NULL_HANDLE || CommandPool == VK_NULL_HANDLE)
    {
        ReportBuffer("device / command pool not provisioned");
        return false;
    }

    // 📝 An empty polygon is a success that draws nothing — leave Result empty rather than claim zero-byte buffers.
    if (Stream.Vertices.empty() || Stream.Indices.empty())
        return true;

    const VkDeviceSize VertexByteSize = (VkDeviceSize)Stream.Vertices.size() * sizeof(RenderVertex);
    const VkDeviceSize IndexByteSize  = (VkDeviceSize)Stream.Indices.size()  * sizeof(uint32_t);

    PolygonBufferAllocation Claimed = {};

    if (!AllocateBackedBuffer(Host.PhysicalDevice,
                              Host.Device,
                              VertexByteSize,
                              // STORAGE lets the software micro-raster (SoftwareRasterization.comp) read this geometry as an SSBO, the same
                              // device-local buffer the hardware raster binds as a vertex buffer — one upload, both raster paths.
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              Claimed.VertexBuffer,
                              Claimed.VertexMemory))
    {
        ReportBuffer("failed to allocate device-local vertex buffer");
        ReleasePolygonBufferAllocation(Host, Claimed);
        return false;
    }

    if (!AllocateBackedBuffer(Host.PhysicalDevice,
                              Host.Device,
                              IndexByteSize,
                              // STORAGE lets the software micro-raster fetch triangle indices as an SSBO alongside the vertex buffer above.
                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              Claimed.IndexBuffer,
                              Claimed.IndexMemory))
    {
        ReportBuffer("failed to allocate device-local index buffer");
        ReleasePolygonBufferAllocation(Host, Claimed);
        return false;
    }

    if (!StageBytesIntoBuffer(Host, CommandPool, Claimed.VertexBuffer, Stream.Vertices.data(), VertexByteSize))
    {
        ReleasePolygonBufferAllocation(Host, Claimed);
        return false;
    }
    if (!StageBytesIntoBuffer(Host, CommandPool, Claimed.IndexBuffer, Stream.Indices.data(), IndexByteSize))
    {
        ReleasePolygonBufferAllocation(Host, Claimed);
        return false;
    }

    Claimed.VertexCount        = (uint32_t)Stream.Vertices.size();
    Claimed.IndexCount         = (uint32_t)Stream.Indices.size();
    Claimed.VertexByteCapacity = VertexByteSize;
    Claimed.IndexByteCapacity  = IndexByteSize;

    Result = Claimed;
    return true;
}

void ReleasePolygonBufferAllocation(VulkanHost& Host, PolygonBufferAllocation& Allocation)
{
    VkDevice LogicalDevice = Host.Device;
    if (LogicalDevice != VK_NULL_HANDLE)
    {
        if (Allocation.VertexBuffer) vkDestroyBuffer(LogicalDevice, Allocation.VertexBuffer, nullptr);
        if (Allocation.VertexMemory) vkFreeMemory   (LogicalDevice, Allocation.VertexMemory, nullptr);
        if (Allocation.IndexBuffer)  vkDestroyBuffer(LogicalDevice, Allocation.IndexBuffer,  nullptr);
        if (Allocation.IndexMemory)  vkFreeMemory   (LogicalDevice, Allocation.IndexMemory,  nullptr);
    }
    Allocation = {};
}

} // namespace Frontier
