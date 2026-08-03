/*==============================================================================================================================================
                                                       ACCELERATIONDEVICESUPPORT.H
==============================================================================================================================================*/
// 🧩 The small device-side helpers every submission in Acceleration/ needs before it can do anything interesting: pick a memory type, allocate a
//    backed buffer, read a .spv off disk, wrap it in a module, run a one-shot blocking transfer, size a dispatch, and move floats in and out of
//    the ordered-int domain the atomic min/max path requires. Nothing here is specific to a tree, a sort or a bounds reduce.
//
//    📝 These bodies were written for VolumeBoundsSubmission and independently re-typed into RadixSortSubmission and GeometryArenaSubmission,
//       which is three copies of the same eight functions. This header is where the fourth copy would have gone. Header-only and inline rather
//       than a .cpp, because they are small, they are called at initialize/upload time rather than per frame, and a header keeps the include
//       graph flat — Graphics/Build.bat globs *.cpp recursively, so a new .cpp would compile fine, but there is nothing here worth the
//       translation unit. The existing three copies are deliberately NOT ripped out in the same edit that introduces this: that is a mechanical
//       change across three working files, and folding it into a feature commit makes the feature's diff unreadable.
//
//    🔴 THE ORDERED-INT PAIR AT THE BOTTOM MUST STAY IDENTICAL TO EVERY SHADER COPY OF IT. The host writes the accumulator seeds and reads the
//       result back; the shader does the atomics in between. If the two transforms disagree the seed stops acting as an identity and the box comes
//       back wrong — silently, and only for scenes touching the half of the number line where they diverge. There is no include mechanism spanning
//       C++ and GLSL here, so the shader copies are transcriptions and this comment is the only thing binding them together.

#pragma once
#ifndef FRONTIER_GRAPHICS_ACCELERATION_ACCELERATIONDEVICESUPPORT_H
#define FRONTIER_GRAPHICS_ACCELERATION_ACCELERATIONDEVICESUPPORT_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       MEMORY + BUFFERS
//------------------------------------------------------------------------------------------------------------------------

// First memory type allowed by the requirement bitmask carrying every required property bit.
inline uint32_t SelectAccelerationMemoryType(VkPhysicalDevice      PhysicalDevice,
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

// Create a buffer of ByteSize with the given usage, backed by memory carrying the required property bits. On any failure both out-handles are null.
inline bool AllocateAccelerationBuffer(VulkanHost&           Host,
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
    const uint32_t MemoryTypeIndex = SelectAccelerationMemoryType(Host.PhysicalDevice, MemoryRequirements.memoryTypeBits,
                                                                  MemoryProperties, MemoryTypeFound);
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

//------------------------------------------------------------------------------------------------------------------------
//                                                          SHADER MODULES
//------------------------------------------------------------------------------------------------------------------------

inline std::vector<char> RetrieveAccelerationShaderBytes(const std::string& FilePath)
{
    std::vector<char> Bytes;
    FILE* Handle = std::fopen(FilePath.c_str(), "rb");
    if (Handle == nullptr)
        return Bytes;
    std::fseek(Handle, 0, SEEK_END);
    long Size = std::ftell(Handle);
    std::fseek(Handle, 0, SEEK_SET);
    if (Size > 0)
    {
        Bytes.resize((size_t)Size);
        size_t Read = std::fread(Bytes.data(), 1, (size_t)Size, Handle);
        if (Read != (size_t)Size)
            Bytes.clear();
    }
    std::fclose(Handle);
    return Bytes;
}

inline VkShaderModule ConstructAccelerationShaderModule(const VulkanHost& Host, const std::vector<char>& Bytes)
{
    if (Bytes.empty() || (Bytes.size() % 4) != 0)
        return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ModuleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ModuleInfo.codeSize = Bytes.size();
    ModuleInfo.pCode    = reinterpret_cast<const uint32_t*>(Bytes.data());
    VkShaderModule Module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(Host.Device, &ModuleInfo, Host.Allocator, &Module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return Module;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        DISPATCH + TRANSFER
//------------------------------------------------------------------------------------------------------------------------

// Run a one-shot recorded command buffer to completion on the graphics queue.
template <typename RecorderType>
bool ExecuteAccelerationTransfer(VulkanHost& Host, VkCommandPool CommandPool, RecorderType Recorder)
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

// Tiles needed to cover Count items at TileSize each.
inline uint32_t ComputeAccelerationGroupCount(uint32_t Count, uint32_t TileSize)
{
    return (Count + TileSize - 1u) / TileSize;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          ORDERED-INT BITS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 IDENTICAL BODIES MUST APPEAR IN EVERY SHADER THAT ATOMICALLY MIN/MAXES A FLOAT. A float's raw bits sort correctly as signed ints ONLY while
//    the float is non-negative; negatives are sign-magnitude and run backwards. XOR-ing every bit below the sign of a negative value reverses that
//    half and splices the two into one monotonic integer line. Getting it wrong does not fail loudly — the box is right for a scene sitting wholly
//    in the positive octant and wrong the moment anything crosses an axis. Proven equivalent, monotonic across the sign boundary, and seed-correct
//    by _ClaudeScratch/build/VolumeBoundsOrderedIntProbe.cpp.
inline int32_t FloatBitsToOrderedInteger(float Value)
{
    int32_t Bits;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    return (Bits < 0) ? (Bits ^ 0x7FFFFFFF) : Bits;
}

inline float OrderedIntegerBitsToFloat(int32_t Bits)
{
    const int32_t Restored = (Bits < 0) ? (Bits ^ 0x7FFFFFFF) : Bits;
    float Value;
    std::memcpy(&Value, &Restored, sizeof(Value));
    return Value;
}

// The seeds, as the 32-bit patterns vkCmdFillBuffer writes. Minima start at +inf and maxima at -inf so the first real value wins every accumulator.
inline uint32_t RetrieveOrderedMinimumSeed()
{
    const int32_t Ordered = FloatBitsToOrderedInteger(INFINITY);
    uint32_t Pattern;
    std::memcpy(&Pattern, &Ordered, sizeof(Pattern));
    return Pattern;
}

inline uint32_t RetrieveOrderedMaximumSeed()
{
    const int32_t Ordered = FloatBitsToOrderedInteger(-INFINITY);
    uint32_t Pattern;
    std::memcpy(&Pattern, &Ordered, sizeof(Pattern));
    return Pattern;
}

} // namespace Frontier

#endif
