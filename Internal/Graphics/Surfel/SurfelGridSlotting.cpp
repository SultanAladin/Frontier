/*==============================================================================================================================================
                                                         SURFELGRIDSLOTTING.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the per-frame grid build. Initialize allocates the Offsets + List SSBOs (List cleared to -1 on a one-shot command buffer, since
//    a fresh list must read "empty" and the slot pass only overwrites the entries the scan allocates), builds a four-binding set layout + push range, the
//    clear/count/slot pipelines, one descriptor set, and the owned SurfelPrefixSum. Record binds the borrowed pool buffers, then dispatches clear ->
//    count -> the prefix scan -> slot with a compute->compute storage barrier between each stage: the whole chain RMWs the Offsets buffer, so every stage
//    must see the previous stage's writes complete (F3). Raw Vulkan, no VMA, mirroring InstanceCullSubmission.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Surfel/SurfelGridSlotting.h"

#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

void ReportSurfelSlotting(const char* MessageText)
{
    std::fprintf(stderr, "[SurfelGridSlotting] %s\n", MessageText);
}

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

bool AllocateStorageBuffer(VulkanHost& Host, VkDeviceSize ByteSize, VkBuffer& OutBuffer, VkDeviceMemory& OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;
    if (ByteSize == 0) ByteSize = 4;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteSize;
    // STORAGE (the grid passes) | TRANSFER_DST (the per-frame clear + the one-shot List seed) | TRANSFER_SRC (the validation gate reads Offsets/List back).
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

std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
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

VkShaderModule ConstructShaderModule(const VulkanHost& Host, const std::vector<char>& Bytes)
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

VkPipeline ConstructComputePipeline(VulkanHost& Host, VkPipelineLayout Layout, const std::string& Directory, const char* FileName)
{
    VkShaderModule Module = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/" + FileName));
    if (Module == VK_NULL_HANDLE)
    {
        ReportSurfelSlotting("slotting shader module unavailable");
        return VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo StageInformation = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    StageInformation.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    StageInformation.module = Module;
    StageInformation.pName  = "main";

    VkComputePipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    PipelineInformation.stage  = StageInformation;
    PipelineInformation.layout = Layout;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    const VkResult Outcome = vkCreateComputePipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInformation, Host.Allocator, &Pipeline);
    vkDestroyShaderModule(Host.Device, Module, Host.Allocator);
    if (Outcome != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return Pipeline;
}

// Build the four-binding set layout (Offsets @0, Surfel @1, PoolMax @2, List @3; all compute storage) + pipeline layout with the SurfelSlottingConstants push range.
bool ConstructSlottingLayout(SurfelGridSlotting& Slotting)
{
    VulkanHost& Host = *Slotting.Host;

    VkDescriptorSetLayoutBinding Bindings[4] = {};
    for (int Index = 0; Index < 4; ++Index)
    {
        Bindings[Index].binding         = (uint32_t)Index;
        Bindings[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[Index].descriptorCount = 1;
        Bindings[Index].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = 4;
    LayoutInformation.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Slotting.SetLayout) != VK_SUCCESS)
    {
        Slotting.SetLayout = VK_NULL_HANDLE;
        return false;
    }

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(SurfelSlottingConstants);

    VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipelineLayoutInformation.setLayoutCount         = 1;
    PipelineLayoutInformation.pSetLayouts            = &Slotting.SetLayout;
    PipelineLayoutInformation.pushConstantRangeCount = 1;
    PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &PipelineLayoutInformation, Host.Allocator, &Slotting.PipelineLayout) != VK_SUCCESS)
    {
        Slotting.PipelineLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Allocate the descriptor pool + one set and point the two OWNED bindings (Offsets @0, List @3). The two borrowed pool bindings (@1, @2) are bound at record time.
bool ConstructSlottingDescriptors(SurfelGridSlotting& Slotting)
{
    VulkanHost& Host = *Slotting.Host;

    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = 4;

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 1;
    PoolInformation.poolSizeCount = 1;
    PoolInformation.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Slotting.DescriptorPool) != VK_SUCCESS)
    {
        Slotting.DescriptorPool = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocation.descriptorPool     = Slotting.DescriptorPool;
    SetAllocation.descriptorSetCount = 1;
    SetAllocation.pSetLayouts        = &Slotting.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, &Slotting.SlottingSet) != VK_SUCCESS)
    {
        Slotting.SlottingSet = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorBufferInfo OffsetsInfo = { Slotting.OffsetsBuffer, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo ListInfo    = { Slotting.ListBuffer,    0, VK_WHOLE_SIZE };

    VkWriteDescriptorSet Writes[2] = {};
    const uint32_t BindingIds[2]                 = { 0u, 3u };
    const VkDescriptorBufferInfo* BufferInfos[2] = { &OffsetsInfo, &ListInfo };
    for (int Index = 0; Index < 2; ++Index)
    {
        Writes[Index].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet          = Slotting.SlottingSet;
        Writes[Index].dstBinding      = BindingIds[Index];
        Writes[Index].descriptorCount = 1;
        Writes[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[Index].pBufferInfo     = BufferInfos[Index];
    }
    vkUpdateDescriptorSets(Host.Device, 2, Writes, 0, nullptr);
    return true;
}

// Fill a device-local buffer with a repeated 32-bit value on a one-shot command buffer, waiting on a fence. Used once to seed List to -1.
bool FillDeviceBuffer(VulkanHost& Host, VkCommandPool CommandPool, VkBuffer Destination, VkDeviceSize ByteSize, uint32_t FillValue)
{
    VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandAllocate.commandPool        = CommandPool;
    CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CommandAllocate.commandBufferCount = 1;
    VkCommandBuffer FillCommand = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &FillCommand) != VK_SUCCESS)
        return false;

    bool Succeeded = false;
    VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(FillCommand, &BeginInformation) == VK_SUCCESS)
    {
        vkCmdFillBuffer(FillCommand, Destination, 0, ByteSize, FillValue);
        if (vkEndCommandBuffer(FillCommand) == VK_SUCCESS)
        {
            VkFence Fence = VK_NULL_HANDLE;
            VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            if (vkCreateFence(Host.Device, &FenceInformation, Host.Allocator, &Fence) == VK_SUCCESS)
            {
                VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
                SubmitInformation.commandBufferCount = 1;
                SubmitInformation.pCommandBuffers    = &FillCommand;
                if (vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInformation, Fence) == VK_SUCCESS &&
                    vkWaitForFences(Host.Device, 1, &Fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS)
                {
                    Succeeded = true;
                }
                vkDestroyFence(Host.Device, Fence, Host.Allocator);
            }
        }
    }
    vkFreeCommandBuffers(Host.Device, CommandPool, 1, &FillCommand);
    return Succeeded;
}

// A compute->compute all-storage barrier between two grid stages (F3): the previous stage's writes are visible to the next stage's reads/writes.
void SlottingStorageBarrier(VkCommandBuffer CommandBuffer)
{
    VkMemoryBarrier Barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &Barrier, 0, nullptr, 0, nullptr);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSurfelGridSlotting(SurfelGridSlotting& Slotting,
                                  VulkanHost&         Host,
                                  VkCommandPool       CommandPool,
                                  const char*         ShaderDirectory)
{
    Slotting = SurfelGridSlotting{};
    Slotting.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportSurfelSlotting("no device — slotting not built");
        return false;
    }

    const VkDeviceSize OffsetsBytes = (VkDeviceSize)SurfelGridOffsetsCount * sizeof(int32_t);
    const VkDeviceSize ListBytes    = (VkDeviceSize)SurfelGridListCount    * sizeof(int32_t);
    if (!AllocateStorageBuffer(Host, OffsetsBytes, Slotting.OffsetsBuffer, Slotting.OffsetsMemory) ||
        !AllocateStorageBuffer(Host, ListBytes,    Slotting.ListBuffer,    Slotting.ListMemory))
    {
        ReportSurfelSlotting("grid buffer allocation failed");
        FinalizeSurfelGridSlotting(Slotting);
        return false;
    }

    // Seed the List to -1 once (an empty list reads -1; the slot pass overwrites only the counted entries). Offsets is cleared per-frame by the clear pass.
    if (!FillDeviceBuffer(Host, CommandPool, Slotting.ListBuffer, ListBytes, 0xFFFFFFFFu))
    {
        ReportSurfelSlotting("initial list clear failed");
        FinalizeSurfelGridSlotting(Slotting);
        return false;
    }

    if (!ConstructSlottingLayout(Slotting))
    {
        ReportSurfelSlotting("slotting pipeline layout creation failed");
        FinalizeSurfelGridSlotting(Slotting);
        return false;
    }
    if (!ConstructSlottingDescriptors(Slotting))
    {
        ReportSurfelSlotting("slotting descriptor plumbing creation failed");
        FinalizeSurfelGridSlotting(Slotting);
        return false;
    }

    const std::string Directory = ShaderDirectory;
    Slotting.ClearPipeline = ConstructComputePipeline(Host, Slotting.PipelineLayout, Directory, "SurfelGridClear.comp.spv");
    Slotting.CountPipeline = ConstructComputePipeline(Host, Slotting.PipelineLayout, Directory, "SurfelGridCount.comp.spv");
    Slotting.SlotPipeline  = ConstructComputePipeline(Host, Slotting.PipelineLayout, Directory, "SurfelGridSlot.comp.spv");
    if (Slotting.ClearPipeline == VK_NULL_HANDLE || Slotting.CountPipeline == VK_NULL_HANDLE || Slotting.SlotPipeline == VK_NULL_HANDLE)
    {
        ReportSurfelSlotting("one or more slotting pipelines failed to build");
        FinalizeSurfelGridSlotting(Slotting);
        return false;
    }

    // The owned scan operates on the Offsets header (SurfelGridOffsetsCount entries).
    if (!InitializeSurfelPrefixSum(Slotting.Prefix, Host, SurfelGridOffsetsCount, ShaderDirectory))
    {
        ReportSurfelSlotting("owned prefix sum init failed");
        FinalizeSurfelGridSlotting(Slotting);
        return false;
    }

    Slotting.ReadyCondition = true;
    return true;
}

void RecordSurfelGridSlotting(SurfelGridSlotting&            Slotting,
                              const SurfelPool&               Pool,
                              const SurfelSlottingConstants&  Constants,
                              VkCommandBuffer                 CommandBuffer)
{
    if (!Slotting.ReadyCondition || Slotting.Host == nullptr)
        return;
    if (!Pool.ReadyCondition || Pool.SurfelBuffer == VK_NULL_HANDLE || Pool.PoolMaxBuffer == VK_NULL_HANDLE)
        return;

    // Re-point the two borrowed pool bindings (@1 Surfel, @2 PoolMax) when the pool's handles changed.
    if (Pool.SurfelBuffer != Slotting.BoundSurfelBuffer || Pool.PoolMaxBuffer != Slotting.BoundPoolMaxBuffer)
    {
        VkDescriptorBufferInfo SurfelInfo  = { Pool.SurfelBuffer,  0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo PoolMaxInfo = { Pool.PoolMaxBuffer, 0, VK_WHOLE_SIZE };

        VkWriteDescriptorSet Writes[2] = {};
        const uint32_t BindingIds[2]                 = { 1u, 2u };
        const VkDescriptorBufferInfo* BufferInfos[2] = { &SurfelInfo, &PoolMaxInfo };
        for (int Index = 0; Index < 2; ++Index)
        {
            Writes[Index].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            Writes[Index].dstSet          = Slotting.SlottingSet;
            Writes[Index].dstBinding      = BindingIds[Index];
            Writes[Index].descriptorCount = 1;
            Writes[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            Writes[Index].pBufferInfo     = BufferInfos[Index];
        }
        vkUpdateDescriptorSets(Slotting.Host->Device, 2, Writes, 0, nullptr);
        Slotting.BoundSurfelBuffer  = Pool.SurfelBuffer;
        Slotting.BoundPoolMaxBuffer = Pool.PoolMaxBuffer;
    }

    SurfelSlottingConstants Push = Constants;
    Push.ListCount = (int32_t)SurfelGridListCount;

    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Slotting.PipelineLayout, 0, 1, &Slotting.SlottingSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Slotting.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SurfelSlottingConstants), &Push);

    // Stage 1 — Clear: zero the Offsets header.
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Slotting.ClearPipeline);
    const uint32_t ClearGroups = (SurfelGridOffsetsCount + SurfelClearWorkgroupEdge - 1) / SurfelClearWorkgroupEdge;
    vkCmdDispatch(CommandBuffer, ClearGroups, 1, 1);
    SlottingStorageBarrier(CommandBuffer);

    // Stage 2 — Count: atomicAdd per intersecting cell. One lane per pool slot.
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Slotting.CountPipeline);
    const uint32_t SlotGroups = (Pool.Capacity + SurfelSlottingWorkgroupEdge - 1) / SurfelSlottingWorkgroupEdge;
    vkCmdDispatch(CommandBuffer, SlotGroups, 1, 1);
    SlottingStorageBarrier(CommandBuffer);

    // Stage 3 — Scan: segmented prefix sum turns per-cell counts into END offsets (owns its own three inter-pass barriers).
    RecordSurfelPrefixSum(Slotting.Prefix, Slotting.OffsetsBuffer, SurfelGridOffsetsCount, CommandBuffer);
    SlottingStorageBarrier(CommandBuffer);   // the scan's last pass (Merge) wrote Offsets; the slot pass must see it complete

    // Stage 4 — Slot: atomicAdd(-1) claims a slice slot, writes List[writeIdx] = surfelIndex.
    // 🔴 The scan bound its OWN pipeline layout (a different push range + set), which by Vulkan's pipeline-layout-compatibility rules DISTURBS the
    //    slotting set + push constants bound above. Rebind both before dispatching the slot pass, or it runs against the prefix sum's layout — set 0
    //    incompatible, push range [0,8) vs the [0,48) this pipeline needs — and reads garbage offsets.
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Slotting.PipelineLayout, 0, 1, &Slotting.SlottingSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Slotting.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SurfelSlottingConstants), &Push);
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Slotting.SlotPipeline);
    vkCmdDispatch(CommandBuffer, SlotGroups, 1, 1);
    // The caller barriers Offsets/List before a downstream read (the trace gather, Phase 2) — that fence belongs to the consumer.
}

void FinalizeSurfelGridSlotting(SurfelGridSlotting& Slotting)
{
    if (Slotting.Host == nullptr || Slotting.Host->Device == VK_NULL_HANDLE)
    {
        Slotting = SurfelGridSlotting{};
        return;
    }
    VkDevice Device = Slotting.Host->Device;
    const VkAllocationCallbacks* Allocator = Slotting.Host->Allocator;

    FinalizeSurfelPrefixSum(Slotting.Prefix);

    if (Slotting.ClearPipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, Slotting.ClearPipeline, Allocator);
    if (Slotting.CountPipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, Slotting.CountPipeline, Allocator);
    if (Slotting.SlotPipeline  != VK_NULL_HANDLE) vkDestroyPipeline(Device, Slotting.SlotPipeline, Allocator);

    if (Slotting.PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Slotting.PipelineLayout, Allocator);
    if (Slotting.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Slotting.DescriptorPool, Allocator);   // frees SlottingSet
    if (Slotting.SetLayout      != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Slotting.SetLayout, Allocator);

    if (Slotting.OffsetsBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, Slotting.OffsetsBuffer, Allocator);
    if (Slotting.OffsetsMemory != VK_NULL_HANDLE) vkFreeMemory(Device, Slotting.OffsetsMemory, Allocator);
    if (Slotting.ListBuffer    != VK_NULL_HANDLE) vkDestroyBuffer(Device, Slotting.ListBuffer, Allocator);
    if (Slotting.ListMemory    != VK_NULL_HANDLE) vkFreeMemory(Device, Slotting.ListMemory, Allocator);

    Slotting = SurfelGridSlotting{};
}

} // namespace Frontier
