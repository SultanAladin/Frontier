/*==============================================================================================================================================
                                                           SURFELPREFIXSUM.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the segmented inclusive prefix sum. Initialize builds one two-binding set layout (Offsets @0 borrowed, SegmentSums @1 owned),
//    a pipeline layout carrying the SurfelPrefixConstants push range, four compute pipelines (one per SurfelPrefix*.comp), a descriptor pool + one set,
//    and the device-local SegmentSums scratch buffer sized for the max segment count. Record binds the caller's Offsets buffer at @0 (re-pointing the
//    descriptor when the handle changed) and issues the four dispatches in order with a compute->compute storage barrier between each: the local scan,
//    the segment-total gather, the SERIAL cross-segment prefix (one workgroup, F4), and the merge. Raw Vulkan, no VMA, mirroring InstanceCullSubmission.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Surfel/SurfelPrefixSum.h"

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

void ReportSurfelPrefix(const char* MessageText)
{
    std::fprintf(stderr, "[SurfelPrefixSum] %s\n", MessageText);
}

// First memory type allowed by the requirement bitmask carrying every required property bit — identical to the InstanceCull helper.
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

// Create a device-local storage buffer carrying STORAGE | TRANSFER_DST. On any failure both out-handles are null.
bool AllocateStorageBuffer(VulkanHost& Host, VkDeviceSize ByteSize, VkBuffer& OutBuffer, VkDeviceMemory& OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;
    if (ByteSize == 0) ByteSize = 4;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteSize;
    BufferInformation.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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

// Build one compute pipeline from Directory/FileName.spv against the shared pipeline layout. Destroys the module before returning. VK_NULL_HANDLE on any failure.
VkPipeline ConstructComputePipeline(VulkanHost& Host, VkPipelineLayout Layout, const std::string& Directory, const char* FileName)
{
    VkShaderModule Module = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/" + FileName));
    if (Module == VK_NULL_HANDLE)
    {
        ReportSurfelPrefix("prefix shader module unavailable");
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

// Build the two-binding set layout (Offsets @0, SegmentSums @1, both compute storage) + pipeline layout carrying the SurfelPrefixConstants push range.
bool ConstructPrefixLayout(SurfelPrefixSum& Prefix)
{
    VulkanHost& Host = *Prefix.Host;

    VkDescriptorSetLayoutBinding Bindings[2] = {};
    for (int Index = 0; Index < 2; ++Index)
    {
        Bindings[Index].binding         = (uint32_t)Index;
        Bindings[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[Index].descriptorCount = 1;
        Bindings[Index].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = 2;
    LayoutInformation.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Prefix.SetLayout) != VK_SUCCESS)
    {
        Prefix.SetLayout = VK_NULL_HANDLE;
        return false;
    }

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(SurfelPrefixConstants);

    VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipelineLayoutInformation.setLayoutCount         = 1;
    PipelineLayoutInformation.pSetLayouts            = &Prefix.SetLayout;
    PipelineLayoutInformation.pushConstantRangeCount = 1;
    PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &PipelineLayoutInformation, Host.Allocator, &Prefix.PipelineLayout) != VK_SUCCESS)
    {
        Prefix.PipelineLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Allocate the descriptor pool + one set and point binding 1 (SegmentSums) at the owned scratch buffer. Binding 0 (Offsets) is bound at record time.
bool ConstructPrefixDescriptors(SurfelPrefixSum& Prefix)
{
    VulkanHost& Host = *Prefix.Host;

    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = 2;

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 1;
    PoolInformation.poolSizeCount = 1;
    PoolInformation.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Prefix.DescriptorPool) != VK_SUCCESS)
    {
        Prefix.DescriptorPool = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocation.descriptorPool     = Prefix.DescriptorPool;
    SetAllocation.descriptorSetCount = 1;
    SetAllocation.pSetLayouts        = &Prefix.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, &Prefix.PrefixSet) != VK_SUCCESS)
    {
        Prefix.PrefixSet = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorBufferInfo SegmentInfo = { Prefix.SegmentSumsBuffer, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet SegmentWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    SegmentWrite.dstSet          = Prefix.PrefixSet;
    SegmentWrite.dstBinding      = 1;
    SegmentWrite.descriptorCount = 1;
    SegmentWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    SegmentWrite.pBufferInfo     = &SegmentInfo;
    vkUpdateDescriptorSets(Host.Device, 1, &SegmentWrite, 0, nullptr);
    return true;
}

// A compute->compute all-storage barrier: the previous pass's writes must be visible to the next pass's reads/writes on the SAME buffers. This is the
// mandatory inter-pass fence (F3) — WebGPU inserts it implicitly between compute nodes.
void ScanStorageBarrier(VkCommandBuffer CommandBuffer)
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

bool InitializeSurfelPrefixSum(SurfelPrefixSum& Prefix,
                               VulkanHost&      Host,
                               uint32_t         MaxElementCount,
                               const char*      ShaderDirectory)
{
    Prefix = SurfelPrefixSum{};
    Prefix.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportSurfelPrefix("no device — prefix sum not built");
        return false;
    }
    if (MaxElementCount == 0) MaxElementCount = 1;
    Prefix.MaxSegments = SurfelScanSegmentCount(MaxElementCount);

    // SegmentSums scratch: one int per segment.
    const VkDeviceSize SegmentBytes = (VkDeviceSize)Prefix.MaxSegments * sizeof(int32_t);
    if (!AllocateStorageBuffer(Host, SegmentBytes, Prefix.SegmentSumsBuffer, Prefix.SegmentSumsMemory))
    {
        ReportSurfelPrefix("segment-sums buffer allocation failed");
        FinalizeSurfelPrefixSum(Prefix);
        return false;
    }

    if (!ConstructPrefixLayout(Prefix))
    {
        ReportSurfelPrefix("prefix pipeline layout creation failed");
        FinalizeSurfelPrefixSum(Prefix);
        return false;
    }
    if (!ConstructPrefixDescriptors(Prefix))
    {
        ReportSurfelPrefix("prefix descriptor plumbing creation failed");
        FinalizeSurfelPrefixSum(Prefix);
        return false;
    }

    const std::string Directory = ShaderDirectory;
    Prefix.ScanSegPipeline    = ConstructComputePipeline(Host, Prefix.PipelineLayout, Directory, "SurfelPrefixScanSegment.comp.spv");
    Prefix.CollectSegPipeline = ConstructComputePipeline(Host, Prefix.PipelineLayout, Directory, "SurfelPrefixCollectSegment.comp.spv");
    Prefix.SegPrefixPipeline  = ConstructComputePipeline(Host, Prefix.PipelineLayout, Directory, "SurfelPrefixSegmentReduce.comp.spv");
    Prefix.MergePipeline      = ConstructComputePipeline(Host, Prefix.PipelineLayout, Directory, "SurfelPrefixMerge.comp.spv");
    if (Prefix.ScanSegPipeline    == VK_NULL_HANDLE || Prefix.CollectSegPipeline == VK_NULL_HANDLE ||
        Prefix.SegPrefixPipeline  == VK_NULL_HANDLE || Prefix.MergePipeline      == VK_NULL_HANDLE)
    {
        ReportSurfelPrefix("one or more prefix pipelines failed to build");
        FinalizeSurfelPrefixSum(Prefix);
        return false;
    }

    Prefix.ReadyCondition = true;
    return true;
}

void RecordSurfelPrefixSum(SurfelPrefixSum& Prefix,
                           VkBuffer         OffsetsBuffer,
                           uint32_t         ElementCount,
                           VkCommandBuffer  CommandBuffer)
{
    if (!Prefix.ReadyCondition || Prefix.Host == nullptr || OffsetsBuffer == VK_NULL_HANDLE || ElementCount == 0)
        return;

    const uint32_t SegmentCount = SurfelScanSegmentCount(ElementCount);

    // Re-point binding 0 at the borrowed Offsets buffer when the handle changed (the slotting owns it and may recreate it).
    if (OffsetsBuffer != Prefix.BoundOffsetsBuffer)
    {
        VkDescriptorBufferInfo OffsetsInfo = { OffsetsBuffer, 0, VK_WHOLE_SIZE };
        VkWriteDescriptorSet OffsetsWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        OffsetsWrite.dstSet          = Prefix.PrefixSet;
        OffsetsWrite.dstBinding      = 0;
        OffsetsWrite.descriptorCount = 1;
        OffsetsWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        OffsetsWrite.pBufferInfo     = &OffsetsInfo;
        vkUpdateDescriptorSets(Prefix.Host->Device, 1, &OffsetsWrite, 0, nullptr);
        Prefix.BoundOffsetsBuffer = OffsetsBuffer;
    }

    SurfelPrefixConstants Constants = {};
    Constants.ElementCount = (int32_t)ElementCount;
    Constants.SegmentCount = (int32_t)SegmentCount;

    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Prefix.PipelineLayout, 0, 1, &Prefix.PrefixSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Prefix.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SurfelPrefixConstants), &Constants);

    // Pass 1 — ScanSeg: one workgroup per segment, each scanning its own 1024-wide slice locally.
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Prefix.ScanSegPipeline);
    vkCmdDispatch(CommandBuffer, SegmentCount, 1, 1);
    ScanStorageBarrier(CommandBuffer);

    // Pass 2 — CollectSeg: gather each segment's total into SegmentSums. One lane per segment.
    // 🔴 Group by SurfelCollectWorkgroupEdge (64 = CollectSeg's local_size_x), NOT the merge edge (256). Under-launching leaves high segments' SegmentSums
    //    stale, inflating the prefix tail on every re-run — invisible below 64 segments (4096 elems) but corrupting the full 262145-element grid (257 segs).
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Prefix.CollectSegPipeline);
    const uint32_t CollectGroups = (SegmentCount + SurfelCollectWorkgroupEdge - 1) / SurfelCollectWorkgroupEdge;
    vkCmdDispatch(CommandBuffer, CollectGroups, 1, 1);
    ScanStorageBarrier(CommandBuffer);

    // Pass 3 — SegPrefix: serial single-thread inclusive prefix of SegmentSums (F4). One workgroup; only lane 0 works.
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Prefix.SegPrefixPipeline);
    vkCmdDispatch(CommandBuffer, 1, 1, 1);
    ScanStorageBarrier(CommandBuffer);

    // Pass 4 — Merge: add the prior-segment prefix to every element, lifting the local scans to a global inclusive prefix.
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Prefix.MergePipeline);
    const uint32_t MergeGroups = (ElementCount + SurfelMergeWorkgroupEdge - 1) / SurfelMergeWorkgroupEdge;
    vkCmdDispatch(CommandBuffer, MergeGroups, 1, 1);
    // The caller barriers Offsets before the slot pass reads it — that fence belongs to the slotting, not here.
}

void FinalizeSurfelPrefixSum(SurfelPrefixSum& Prefix)
{
    if (Prefix.Host == nullptr || Prefix.Host->Device == VK_NULL_HANDLE)
    {
        Prefix = SurfelPrefixSum{};
        return;
    }
    VkDevice Device = Prefix.Host->Device;
    const VkAllocationCallbacks* Allocator = Prefix.Host->Allocator;

    if (Prefix.ScanSegPipeline    != VK_NULL_HANDLE) vkDestroyPipeline(Device, Prefix.ScanSegPipeline, Allocator);
    if (Prefix.CollectSegPipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, Prefix.CollectSegPipeline, Allocator);
    if (Prefix.SegPrefixPipeline  != VK_NULL_HANDLE) vkDestroyPipeline(Device, Prefix.SegPrefixPipeline, Allocator);
    if (Prefix.MergePipeline      != VK_NULL_HANDLE) vkDestroyPipeline(Device, Prefix.MergePipeline, Allocator);

    if (Prefix.PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Prefix.PipelineLayout, Allocator);
    if (Prefix.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Prefix.DescriptorPool, Allocator);   // frees PrefixSet
    if (Prefix.SetLayout      != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Prefix.SetLayout, Allocator);

    if (Prefix.SegmentSumsBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, Prefix.SegmentSumsBuffer, Allocator);
    if (Prefix.SegmentSumsMemory != VK_NULL_HANDLE) vkFreeMemory(Device, Prefix.SegmentSumsMemory, Allocator);

    Prefix = SurfelPrefixSum{};
}

} // namespace Frontier
