/*==============================================================================================================================================
                                                         RADIXSORTSUBMISSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the GPU LSD radix sort. Initialize reads the four .comp.spv modules, builds one set layout + pipeline layout + pipeline per
//    stage, then a pool with the six pre-baked sets (tally and scatter twice, once per ping-pong parity) and the device-local key / payload /
//    bin-table / offset-table / block-total / block-base buffers. Upload stages the caller's Morton keys and primitive indices into the primary pair
//    through a host-visible scratch buffer. RecordRadixSort walks the four digits, each as tally → scan 2a → scan 2b → scan 2c → scatter with a
//    compute→compute barrier between every dispatch, alternating the ping-pong parity per pass. Readback copies the sorted head back for the
//    validation gate. Raw Vulkan, no VMA, mirroring the InstanceCullSubmission / HierarchicalDepthPyramid idioms.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Acceleration/RadixSortSubmission.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

void ReportRadixSort(const char* MessageText)
{
    std::fprintf(stderr, "[RadixSort] %s\n", MessageText);
}

// First memory type allowed by the requirement bitmask carrying every required property bit — mirrors the cull / HiZ helper.
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

// Create a buffer of ByteSize with the given usage, backed by memory carrying the required property bits. On any failure both out-handles are null.
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

// Build a set layout of BindingCount consecutive storage-buffer bindings (0..BindingCount-1, all compute stage) plus a pipeline layout carrying a
// push range of PushByteSize. Every one of the four stages has exactly this shape, differing only in the two counts.
bool ConstructStageLayout(VulkanHost&            Host,
                          uint32_t               BindingCount,
                          uint32_t               PushByteSize,
                          VkDescriptorSetLayout& OutSetLayout,
                          VkPipelineLayout&      OutPipelineLayout)
{
    OutSetLayout      = VK_NULL_HANDLE;
    OutPipelineLayout = VK_NULL_HANDLE;

    VkDescriptorSetLayoutBinding Bindings[8] = {};
    for (uint32_t Index = 0; Index < BindingCount; ++Index)
    {
        Bindings[Index].binding         = Index;
        Bindings[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[Index].descriptorCount = 1;
        Bindings[Index].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = BindingCount;
    LayoutInformation.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &OutSetLayout) != VK_SUCCESS)
    {
        OutSetLayout = VK_NULL_HANDLE;
        return false;
    }

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = PushByteSize;

    VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipelineLayoutInformation.setLayoutCount         = 1;
    PipelineLayoutInformation.pSetLayouts            = &OutSetLayout;
    PipelineLayoutInformation.pushConstantRangeCount = 1;
    PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &PipelineLayoutInformation, Host.Allocator, &OutPipelineLayout) != VK_SUCCESS)
    {
        OutPipelineLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Build one compute pipeline from a named .comp.spv in ShaderDirectory. Destroys the module before returning. False with the pipeline null on any
// failure, having reported which shader was at fault (a missing .spv is the common case and is worth naming).
bool ConstructStagePipeline(VulkanHost&        Host,
                           const std::string& ShaderDirectory,
                           const char*        ShaderName,
                           VkPipelineLayout   PipelineLayout,
                           VkPipeline&        OutPipeline)
{
    OutPipeline = VK_NULL_HANDLE;

    const std::string ModulePath = ShaderDirectory + "/" + ShaderName;
    VkShaderModule Module = ConstructShaderModule(Host, RetrieveShaderBytes(ModulePath));
    if (Module == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "[RadixSort] shader module unavailable: %s — sort will not run\n", ModulePath.c_str());
        return false;
    }

    VkPipelineShaderStageCreateInfo StageInformation = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    StageInformation.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    StageInformation.module = Module;
    StageInformation.pName  = "main";

    VkComputePipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    PipelineInformation.stage  = StageInformation;
    PipelineInformation.layout = PipelineLayout;
    const VkResult Outcome = vkCreateComputePipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInformation, Host.Allocator, &OutPipeline);
    vkDestroyShaderModule(Host.Device, Module, Host.Allocator);
    if (Outcome != VK_SUCCESS)
    {
        OutPipeline = VK_NULL_HANDLE;
        std::fprintf(stderr, "[RadixSort] compute pipeline creation failed: %s\n", ShaderName);
        return false;
    }
    return true;
}

// Point a set's consecutive bindings 0..BufferCount-1 at the given buffers, each over its whole range.
void BindStorageBuffers(VulkanHost& Host, VkDescriptorSet Set, const VkBuffer* Buffers, uint32_t BufferCount)
{
    VkDescriptorBufferInfo BufferInfos[8] = {};
    VkWriteDescriptorSet   Writes[8]      = {};
    for (uint32_t Index = 0; Index < BufferCount; ++Index)
    {
        BufferInfos[Index].buffer = Buffers[Index];
        BufferInfos[Index].offset = 0;
        BufferInfos[Index].range  = VK_WHOLE_SIZE;

        Writes[Index].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet          = Set;
        Writes[Index].dstBinding      = Index;
        Writes[Index].descriptorCount = 1;
        Writes[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[Index].pBufferInfo     = &BufferInfos[Index];
    }
    vkUpdateDescriptorSets(Host.Device, BufferCount, Writes, 0, nullptr);
}

// The compute→compute fence every dispatch boundary in the sort needs: the next dispatch reads exactly what the previous one wrote.
void InsertComputeBarrier(VkCommandBuffer CommandBuffer)
{
    VkMemoryBarrier StageBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    StageBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    StageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &StageBarrier, 0, nullptr, 0, nullptr);
}

// Allocate a one-shot command buffer, run Recorder into it, submit on the graphics queue and wait on a fence. Every blocking host-side transfer in
// this file (upload staging, readback) shares it.
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

// Tiles needed to cover Count items at TileSize each.
uint32_t ComputeGroupCount(uint32_t Count, uint32_t TileSize)
{
    return (Count + TileSize - 1u) / TileSize;
}

// Allocate every device-local buffer for MaxKeys. Key and payload buffers carry TRANSFER_SRC | TRANSFER_DST as well as STORAGE, because the upload
// stages into them and the validation readback copies back out of them.
bool ConstructSortBuffers(RadixSortSubmission& Sort, uint32_t MaxKeys)
{
    VulkanHost& Host = *Sort.Host;

    const uint32_t GroupCapacity = ComputeGroupCount(MaxKeys, RadixKeysPerGroup);
    const uint32_t BinTableEntryCount = RadixBinCount * GroupCapacity;
    // The bin table is what the scan walks, so the block count — and every block-indexed buffer — is sized from the TABLE, not from the key count.
    const uint32_t BinTableBlockCount = ComputeGroupCount(BinTableEntryCount, RadixKeysPerGroup);

    const VkDeviceSize KeyBytes      = (VkDeviceSize)MaxKeys * sizeof(uint32_t);
    const VkDeviceSize BinTableBytes = (VkDeviceSize)BinTableEntryCount * sizeof(uint32_t);
    const VkDeviceSize BlockBytes    = (VkDeviceSize)BinTableBlockCount * sizeof(uint32_t);

    const VkBufferUsageFlags KeyUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT  |
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    const VkBufferUsageFlags ScratchUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (!AllocateBackedBuffer(Host, KeyBytes, KeyUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              Sort.PrimaryKeyBuffer, Sort.PrimaryKeyMemory))                  return false;
    if (!AllocateBackedBuffer(Host, KeyBytes, KeyUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              Sort.PrimaryPayloadBuffer, Sort.PrimaryPayloadMemory))          return false;
    if (!AllocateBackedBuffer(Host, KeyBytes, KeyUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              Sort.ScratchKeyBuffer, Sort.ScratchKeyMemory))                  return false;
    if (!AllocateBackedBuffer(Host, KeyBytes, KeyUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              Sort.ScratchPayloadBuffer, Sort.ScratchPayloadMemory))          return false;

    if (!AllocateBackedBuffer(Host, BinTableBytes, ScratchUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              Sort.BinTallyBuffer, Sort.BinTallyMemory))                      return false;
    if (!AllocateBackedBuffer(Host, BinTableBytes, ScratchUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              Sort.BinOffsetBuffer, Sort.BinOffsetMemory))                    return false;
    if (!AllocateBackedBuffer(Host, BlockBytes, ScratchUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              Sort.BlockTotalBuffer, Sort.BlockTotalMemory))                  return false;
    if (!AllocateBackedBuffer(Host, BlockBytes, ScratchUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              Sort.BlockBaseBuffer, Sort.BlockBaseMemory))                    return false;

    Sort.KeyCapacity   = MaxKeys;
    Sort.GroupCapacity = GroupCapacity;
    return true;
}

// Allocate the pool + the six sets and point every binding at its owned buffer. Nothing here is ever rewritten afterwards — see the parity note in
// the header for why the record path must not touch descriptors.
bool ConstructSortDescriptors(RadixSortSubmission& Sort)
{
    VulkanHost& Host = *Sort.Host;

    // 2 tally sets (2 buffers each) + 2 scan sets (3 each) + 1 base-add set (2) + 2 scatter sets (5 each) = 22 storage descriptors over 7 sets.
    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = 22;

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 7;
    PoolInformation.poolSizeCount = 1;
    PoolInformation.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Sort.DescriptorPool) != VK_SUCCESS)
    {
        Sort.DescriptorPool = VK_NULL_HANDLE;
        return false;
    }

    auto AllocateSet = [&](VkDescriptorSetLayout SetLayout, VkDescriptorSet& OutSet) -> bool
    {
        VkDescriptorSetAllocateInfo SetAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        SetAllocation.descriptorPool     = Sort.DescriptorPool;
        SetAllocation.descriptorSetCount = 1;
        SetAllocation.pSetLayouts        = &SetLayout;
        if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, &OutSet) != VK_SUCCESS)
        {
            OutSet = VK_NULL_HANDLE;
            return false;
        }
        return true;
    };

    if (!AllocateSet(Sort.TallySetLayout,   Sort.TallySet[0])) return false;
    if (!AllocateSet(Sort.TallySetLayout,   Sort.TallySet[1])) return false;
    if (!AllocateSet(Sort.ScanSetLayout,    Sort.BlockScanSet)) return false;
    if (!AllocateSet(Sort.ScanSetLayout,    Sort.TotalScanSet)) return false;
    if (!AllocateSet(Sort.BaseAddSetLayout, Sort.BaseAddSet)) return false;
    if (!AllocateSet(Sort.ScatterSetLayout, Sort.ScatterSet[0])) return false;
    if (!AllocateSet(Sort.ScatterSetLayout, Sort.ScatterSet[1])) return false;

    // Tally: { key source, bin table }. Parity 0 reads the primary keys, parity 1 the scratch keys.
    const VkBuffer TallyEvenBuffers[2] = { Sort.PrimaryKeyBuffer, Sort.BinTallyBuffer };
    const VkBuffer TallyOddBuffers[2]  = { Sort.ScratchKeyBuffer, Sort.BinTallyBuffer };
    BindStorageBuffers(Host, Sort.TallySet[0], TallyEvenBuffers, 2);
    BindStorageBuffers(Host, Sort.TallySet[1], TallyOddBuffers,  2);

    // Scan step 2a: bin table -> offset table, publishing one total per block.
    const VkBuffer BlockScanBuffers[3] = { Sort.BinTallyBuffer, Sort.BinOffsetBuffer, Sort.BlockTotalBuffer };
    BindStorageBuffers(Host, Sort.BlockScanSet, BlockScanBuffers, 3);

    // Scan step 2b: block totals -> block bases. Binding 2 is bound but never written (TotalWriteEnabled is 0); it points at BlockTotalBuffer only
    // because a storage binding declared in the shader must be backed by something valid.
    const VkBuffer TotalScanBuffers[3] = { Sort.BlockTotalBuffer, Sort.BlockBaseBuffer, Sort.BlockTotalBuffer };
    BindStorageBuffers(Host, Sort.TotalScanSet, TotalScanBuffers, 3);

    // Scan step 2c: offset table += its block's base.
    const VkBuffer BaseAddBuffers[2] = { Sort.BinOffsetBuffer, Sort.BlockBaseBuffer };
    BindStorageBuffers(Host, Sort.BaseAddSet, BaseAddBuffers, 2);

    // Scatter: { key src, payload src, key dst, payload dst, offset table }. Parity 0 moves primary -> scratch, parity 1 scratch -> primary.
    const VkBuffer ScatterEvenBuffers[5] = { Sort.PrimaryKeyBuffer, Sort.PrimaryPayloadBuffer,
                                            Sort.ScratchKeyBuffer, Sort.ScratchPayloadBuffer, Sort.BinOffsetBuffer };
    const VkBuffer ScatterOddBuffers[5]  = { Sort.ScratchKeyBuffer, Sort.ScratchPayloadBuffer,
                                            Sort.PrimaryKeyBuffer, Sort.PrimaryPayloadBuffer, Sort.BinOffsetBuffer };
    BindStorageBuffers(Host, Sort.ScatterSet[0], ScatterEvenBuffers, 5);
    BindStorageBuffers(Host, Sort.ScatterSet[1], ScatterOddBuffers,  5);
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeRadixSortSubmission(RadixSortSubmission& Sort,
                                   VulkanHost&          Host,
                                   uint32_t             MaxKeys,
                                   const char*          ShaderDirectory)
{
    Sort = RadixSortSubmission{};
    Sort.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportRadixSort("no device — sort not built");
        return false;
    }
    if (MaxKeys == 0)
        MaxKeys = 1;

    // 🔴 Refuse rather than clamp. A clamped capacity would leave the scan's step 2b unable to reach the tail blocks, and the sort would return
    //    plausible-looking but corrupt output instead of failing — the worst possible outcome for the tree build downstream.
    if (MaxKeys > RadixSortKeyCeiling)
    {
        std::fprintf(stderr, "[RadixSort] %u keys exceeds the single-workgroup scan ceiling of %u — refusing to build\n",
                     MaxKeys, RadixSortKeyCeiling);
        return false;
    }

    const std::string Directory = ShaderDirectory;

    if (!ConstructStageLayout(Host, 2, (uint32_t)sizeof(RadixDigitConstants),   Sort.TallySetLayout,   Sort.TallyLayout)   ||
        !ConstructStageLayout(Host, 3, (uint32_t)sizeof(RadixScanConstants),    Sort.ScanSetLayout,    Sort.ScanLayout)    ||
        !ConstructStageLayout(Host, 2, (uint32_t)sizeof(RadixBaseAddConstants), Sort.BaseAddSetLayout, Sort.BaseAddLayout) ||
        !ConstructStageLayout(Host, 5, (uint32_t)sizeof(RadixDigitConstants),   Sort.ScatterSetLayout, Sort.ScatterLayout))
    {
        ReportRadixSort("pipeline layout creation failed");
        FinalizeRadixSortSubmission(Sort);
        return false;
    }

    if (!ConstructStagePipeline(Host, Directory, "RadixBinTally.comp.spv",     Sort.TallyLayout,   Sort.TallyPipeline)   ||
        !ConstructStagePipeline(Host, Directory, "RadixBlockScan.comp.spv",    Sort.ScanLayout,    Sort.ScanPipeline)    ||
        !ConstructStagePipeline(Host, Directory, "RadixBlockBaseAdd.comp.spv", Sort.BaseAddLayout, Sort.BaseAddPipeline) ||
        !ConstructStagePipeline(Host, Directory, "RadixKeyScatter.comp.spv",   Sort.ScatterLayout, Sort.ScatterPipeline))
    {
        FinalizeRadixSortSubmission(Sort);
        return false;
    }

    if (!ConstructSortBuffers(Sort, MaxKeys))
    {
        ReportRadixSort("sort buffer allocation failed");
        FinalizeRadixSortSubmission(Sort);
        return false;
    }
    if (!ConstructSortDescriptors(Sort))
    {
        ReportRadixSort("sort descriptor plumbing creation failed");
        FinalizeRadixSortSubmission(Sort);
        return false;
    }

    Sort.ReadyCondition = true;
    return true;
}

bool UploadRadixSortKeys(RadixSortSubmission& Sort,
                         VkCommandPool        CommandPool,
                         const uint32_t*      Keys,
                         const uint32_t*      Payloads,
                         uint32_t             KeyCount)
{
    if (!Sort.ReadyCondition || Sort.Host == nullptr || Keys == nullptr)
        return false;

    if (KeyCount > Sort.KeyCapacity)
    {
        ReportRadixSort("key count exceeds sort capacity — truncating");
        KeyCount = Sort.KeyCapacity;
    }
    Sort.KeyCount = KeyCount;
    if (KeyCount == 0)
        return true;

    VulkanHost& Host = *Sort.Host;
    const VkDeviceSize PayloadOffset = (VkDeviceSize)KeyCount * sizeof(uint32_t);
    const VkDeviceSize StagingBytes  = PayloadOffset * 2;   // keys then payloads, back to back in one staging buffer

    VkBuffer       StagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    if (!AllocateBackedBuffer(Host, StagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              StagingBuffer, StagingMemory))
    {
        ReportRadixSort("upload staging buffer allocation failed");
        Sort.KeyCount = 0;
        return false;
    }

    void* Mapped = nullptr;
    if (vkMapMemory(Host.Device, StagingMemory, 0, StagingBytes, 0, &Mapped) != VK_SUCCESS)
    {
        vkDestroyBuffer(Host.Device, StagingBuffer, Host.Allocator);
        vkFreeMemory(Host.Device, StagingMemory, Host.Allocator);
        ReportRadixSort("upload staging map failed");
        Sort.KeyCount = 0;
        return false;
    }
    uint32_t* StagingWords = reinterpret_cast<uint32_t*>(Mapped);
    std::memcpy(StagingWords, Keys, (size_t)KeyCount * sizeof(uint32_t));
    if (Payloads != nullptr)
    {
        std::memcpy(StagingWords + KeyCount, Payloads, (size_t)KeyCount * sizeof(uint32_t));
    }
    else
    {
        // No payload supplied: the identity, so the sorted payload is the permutation that produced the sorted keys.
        for (uint32_t Index = 0; Index < KeyCount; ++Index)
            StagingWords[KeyCount + Index] = Index;
    }
    vkUnmapMemory(Host.Device, StagingMemory);

    const bool Copied = ExecuteBlockingTransfer(Host, CommandPool, [&](VkCommandBuffer TransferCommand)
    {
        VkBufferCopy KeyRegion = {};
        KeyRegion.srcOffset = 0;
        KeyRegion.dstOffset = 0;
        KeyRegion.size      = PayloadOffset;
        vkCmdCopyBuffer(TransferCommand, StagingBuffer, Sort.PrimaryKeyBuffer, 1, &KeyRegion);

        VkBufferCopy PayloadRegion = {};
        PayloadRegion.srcOffset = PayloadOffset;
        PayloadRegion.dstOffset = 0;
        PayloadRegion.size      = PayloadOffset;
        vkCmdCopyBuffer(TransferCommand, StagingBuffer, Sort.PrimaryPayloadBuffer, 1, &PayloadRegion);
    });

    vkDestroyBuffer(Host.Device, StagingBuffer, Host.Allocator);
    vkFreeMemory(Host.Device, StagingMemory, Host.Allocator);

    if (!Copied)
    {
        ReportRadixSort("upload copy submission failed");
        Sort.KeyCount = 0;
        return false;
    }
    return true;
}

void RecordRadixSort(RadixSortSubmission& Sort, VkCommandBuffer CommandBuffer)
{
    if (!Sort.ReadyCondition || Sort.Host == nullptr || Sort.KeyCount == 0)
        return;

    const uint32_t KeyCount   = Sort.KeyCount;
    const uint32_t GroupCount = ComputeGroupCount(KeyCount, RadixKeysPerGroup);

    // The scan operates on the bin table, so its extents come from the table, not the key count.
    const uint32_t BinTableEntryCount = RadixBinCount * GroupCount;
    const uint32_t BinTableBlockCount = ComputeGroupCount(BinTableEntryCount, RadixKeysPerGroup);

    // The live-count guard for the ceiling the header documents. Initialize already refused an over-capacity build, so reaching this means the
    // capacity was fine but the live count still overflowed step 2b — bail rather than emit a silently corrupt sort.
    if (BinTableBlockCount > RadixKeysPerGroup)
    {
        ReportRadixSort("live key count overflows the single-workgroup scan — sort skipped");
        return;
    }

    for (uint32_t PassIndex = 0; PassIndex < RadixPassCount; ++PassIndex)
    {
        const uint32_t Parity = PassIndex & 1u;

        RadixDigitConstants DigitConstants = {};
        DigitConstants.KeyCount   = KeyCount;
        DigitConstants.GroupCount = GroupCount;
        DigitConstants.ShiftBits  = PassIndex * 8u;

        // ---- dispatch 1 : tally each tile's digits into the bin-major bin table -----------------------------------------------------------------
        vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Sort.TallyPipeline);
        vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Sort.TallyLayout, 0, 1, &Sort.TallySet[Parity], 0, nullptr);
        vkCmdPushConstants(CommandBuffer, Sort.TallyLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DigitConstants), &DigitConstants);
        vkCmdDispatch(CommandBuffer, GroupCount, 1, 1);
        InsertComputeBarrier(CommandBuffer);

        // ---- dispatch 2a : exclusive-scan each block of the bin table, publishing its total ------------------------------------------------------
        RadixScanConstants BlockScanConstants = {};
        BlockScanConstants.EntryCount        = BinTableEntryCount;
        BlockScanConstants.TotalWriteEnabled = 1u;
        vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Sort.ScanPipeline);
        vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Sort.ScanLayout, 0, 1, &Sort.BlockScanSet, 0, nullptr);
        vkCmdPushConstants(CommandBuffer, Sort.ScanLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BlockScanConstants), &BlockScanConstants);
        vkCmdDispatch(CommandBuffer, BinTableBlockCount, 1, 1);
        InsertComputeBarrier(CommandBuffer);

        // ---- dispatch 2b : scan the block totals into per-block bases, ONE workgroup ------------------------------------------------------------
        // 🔴 One workgroup is the whole point: it is the only way to scan across blocks without inter-workgroup ordering, which Vulkan does not
        //    provide. The guard above is what keeps that legitimate.
        RadixScanConstants TotalScanConstants = {};
        TotalScanConstants.EntryCount        = BinTableBlockCount;
        TotalScanConstants.TotalWriteEnabled = 0u;
        vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Sort.ScanLayout, 0, 1, &Sort.TotalScanSet, 0, nullptr);
        vkCmdPushConstants(CommandBuffer, Sort.ScanLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TotalScanConstants), &TotalScanConstants);
        vkCmdDispatch(CommandBuffer, 1, 1, 1);
        InsertComputeBarrier(CommandBuffer);

        // ---- dispatch 2c : add each block's base back into its slice of the offset table --------------------------------------------------------
        RadixBaseAddConstants BaseAddConstants = {};
        BaseAddConstants.EntryCount = BinTableEntryCount;
        vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Sort.BaseAddPipeline);
        vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Sort.BaseAddLayout, 0, 1, &Sort.BaseAddSet, 0, nullptr);
        vkCmdPushConstants(CommandBuffer, Sort.BaseAddLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BaseAddConstants), &BaseAddConstants);
        vkCmdDispatch(CommandBuffer, BinTableBlockCount, 1, 1);
        InsertComputeBarrier(CommandBuffer);

        // ---- dispatch 3 : scatter keys + payloads to their sorted positions ---------------------------------------------------------------------
        vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Sort.ScatterPipeline);
        vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Sort.ScatterLayout, 0, 1, &Sort.ScatterSet[Parity], 0, nullptr);
        vkCmdPushConstants(CommandBuffer, Sort.ScatterLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DigitConstants), &DigitConstants);
        vkCmdDispatch(CommandBuffer, GroupCount, 1, 1);
        InsertComputeBarrier(CommandBuffer);
    }
}

bool SetRadixSortKeyCount(RadixSortSubmission& Sort, uint32_t KeyCount)
{
    if (!Sort.ReadyCondition)
        return false;

    // 🔴 REFUSED, NOT TRUNCATED. UploadRadixSortKeys truncates because it is also copying the data and a short copy is at least self-consistent.
    //    Here there is no copy to shorten: the buffers were filled by a device pass that wrote one entry per instance, so accepting a clamped count
    //    would sort a prefix and silently drop the tail of a scene that the producer believes it emitted in full.
    if (KeyCount > Sort.KeyCapacity)
    {
        ReportRadixSort("device-side key count exceeds sort capacity — refusing");
        return false;
    }

    Sort.KeyCount = KeyCount;
    return true;
}

void RetrieveRadixSortInputBuffers(const RadixSortSubmission& Sort, VkBuffer& OutKeyBuffer, VkBuffer& OutPayloadBuffer)
{
    // Always the primary pair: pass 0's tally reads at parity 0, which is the primary side regardless of RadixPassCount. The result side varies
    // with parity, which is why RetrieveRadixSortedBuffers derives it and this does not.
    OutKeyBuffer     = Sort.PrimaryKeyBuffer;
    OutPayloadBuffer = Sort.PrimaryPayloadBuffer;
}

void RetrieveRadixSortedBuffers(const RadixSortSubmission& Sort, VkBuffer& OutKeyBuffer, VkBuffer& OutPayloadBuffer)
{
    // Pass P scatters into scratch when P is even and into primary when odd, so after RadixPassCount passes the result sits in primary for an even
    // count and scratch for an odd one. Derived, not assumed — see the header.
    const bool ResultInPrimary = (RadixPassCount % 2u) == 0u;
    OutKeyBuffer     = ResultInPrimary ? Sort.PrimaryKeyBuffer     : Sort.ScratchKeyBuffer;
    OutPayloadBuffer = ResultInPrimary ? Sort.PrimaryPayloadBuffer : Sort.ScratchPayloadBuffer;
}

bool RetrieveRadixSortReadback(RadixSortSubmission& Sort,
                               VkCommandPool        CommandPool,
                               uint32_t             SampleCount,
                               uint32_t*            OutKeys,
                               uint32_t*            OutPayloads)
{
    if (!Sort.ReadyCondition || Sort.Host == nullptr || SampleCount == 0)
        return false;
    if (SampleCount > Sort.KeyCount)
        SampleCount = Sort.KeyCount;
    if (SampleCount == 0)
        return false;

    VulkanHost& Host = *Sort.Host;

    VkBuffer SortedKeyBuffer     = VK_NULL_HANDLE;
    VkBuffer SortedPayloadBuffer = VK_NULL_HANDLE;
    RetrieveRadixSortedBuffers(Sort, SortedKeyBuffer, SortedPayloadBuffer);

    const VkDeviceSize SampleBytes  = (VkDeviceSize)SampleCount * sizeof(uint32_t);
    const VkDeviceSize StagingBytes = SampleBytes * 2;

    VkBuffer       StagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    if (!AllocateBackedBuffer(Host, StagingBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              StagingBuffer, StagingMemory))
    {
        ReportRadixSort("readback staging buffer allocation failed");
        return false;
    }

    const bool Copied = ExecuteBlockingTransfer(Host, CommandPool, [&](VkCommandBuffer TransferCommand)
    {
        VkBufferCopy KeyRegion = {};
        KeyRegion.srcOffset = 0;
        KeyRegion.dstOffset = 0;
        KeyRegion.size      = SampleBytes;
        vkCmdCopyBuffer(TransferCommand, SortedKeyBuffer, StagingBuffer, 1, &KeyRegion);

        VkBufferCopy PayloadRegion = {};
        PayloadRegion.srcOffset = 0;
        PayloadRegion.dstOffset = SampleBytes;
        PayloadRegion.size      = SampleBytes;
        vkCmdCopyBuffer(TransferCommand, SortedPayloadBuffer, StagingBuffer, 1, &PayloadRegion);
    });

    bool Succeeded = false;
    if (Copied)
    {
        void* Mapped = nullptr;
        if (vkMapMemory(Host.Device, StagingMemory, 0, StagingBytes, 0, &Mapped) == VK_SUCCESS)
        {
            const uint32_t* StagingWords = reinterpret_cast<const uint32_t*>(Mapped);
            if (OutKeys     != nullptr) std::memcpy(OutKeys,     StagingWords,               (size_t)SampleBytes);
            if (OutPayloads != nullptr) std::memcpy(OutPayloads, StagingWords + SampleCount, (size_t)SampleBytes);
            vkUnmapMemory(Host.Device, StagingMemory);
            Succeeded = true;
        }
        else
        {
            ReportRadixSort("readback staging map failed");
        }
    }
    else
    {
        ReportRadixSort("readback copy submission failed");
    }

    vkDestroyBuffer(Host.Device, StagingBuffer, Host.Allocator);
    vkFreeMemory(Host.Device, StagingMemory, Host.Allocator);
    return Succeeded;
}

void FinalizeRadixSortSubmission(RadixSortSubmission& Sort)
{
    if (Sort.Host == nullptr || Sort.Host->Device == VK_NULL_HANDLE)
    {
        Sort = RadixSortSubmission{};
        return;
    }
    VkDevice Device = Sort.Host->Device;
    const VkAllocationCallbacks* Allocator = Sort.Host->Allocator;

    if (Sort.TallyPipeline   != VK_NULL_HANDLE) vkDestroyPipeline(Device, Sort.TallyPipeline, Allocator);
    if (Sort.ScanPipeline    != VK_NULL_HANDLE) vkDestroyPipeline(Device, Sort.ScanPipeline, Allocator);
    if (Sort.BaseAddPipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, Sort.BaseAddPipeline, Allocator);
    if (Sort.ScatterPipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, Sort.ScatterPipeline, Allocator);

    if (Sort.TallyLayout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Sort.TallyLayout, Allocator);
    if (Sort.ScanLayout    != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Sort.ScanLayout, Allocator);
    if (Sort.BaseAddLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Sort.BaseAddLayout, Allocator);
    if (Sort.ScatterLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Sort.ScatterLayout, Allocator);

    if (Sort.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Sort.DescriptorPool, Allocator);   // frees every set

    if (Sort.TallySetLayout   != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Sort.TallySetLayout, Allocator);
    if (Sort.ScanSetLayout    != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Sort.ScanSetLayout, Allocator);
    if (Sort.BaseAddSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Sort.BaseAddSetLayout, Allocator);
    if (Sort.ScatterSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Sort.ScatterSetLayout, Allocator);

    if (Sort.PrimaryKeyBuffer     != VK_NULL_HANDLE) vkDestroyBuffer(Device, Sort.PrimaryKeyBuffer, Allocator);
    if (Sort.PrimaryKeyMemory     != VK_NULL_HANDLE) vkFreeMemory(Device, Sort.PrimaryKeyMemory, Allocator);
    if (Sort.PrimaryPayloadBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, Sort.PrimaryPayloadBuffer, Allocator);
    if (Sort.PrimaryPayloadMemory != VK_NULL_HANDLE) vkFreeMemory(Device, Sort.PrimaryPayloadMemory, Allocator);
    if (Sort.ScratchKeyBuffer     != VK_NULL_HANDLE) vkDestroyBuffer(Device, Sort.ScratchKeyBuffer, Allocator);
    if (Sort.ScratchKeyMemory     != VK_NULL_HANDLE) vkFreeMemory(Device, Sort.ScratchKeyMemory, Allocator);
    if (Sort.ScratchPayloadBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, Sort.ScratchPayloadBuffer, Allocator);
    if (Sort.ScratchPayloadMemory != VK_NULL_HANDLE) vkFreeMemory(Device, Sort.ScratchPayloadMemory, Allocator);

    if (Sort.BinTallyBuffer   != VK_NULL_HANDLE) vkDestroyBuffer(Device, Sort.BinTallyBuffer, Allocator);
    if (Sort.BinTallyMemory   != VK_NULL_HANDLE) vkFreeMemory(Device, Sort.BinTallyMemory, Allocator);
    if (Sort.BinOffsetBuffer  != VK_NULL_HANDLE) vkDestroyBuffer(Device, Sort.BinOffsetBuffer, Allocator);
    if (Sort.BinOffsetMemory  != VK_NULL_HANDLE) vkFreeMemory(Device, Sort.BinOffsetMemory, Allocator);
    if (Sort.BlockTotalBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, Sort.BlockTotalBuffer, Allocator);
    if (Sort.BlockTotalMemory != VK_NULL_HANDLE) vkFreeMemory(Device, Sort.BlockTotalMemory, Allocator);
    if (Sort.BlockBaseBuffer  != VK_NULL_HANDLE) vkDestroyBuffer(Device, Sort.BlockBaseBuffer, Allocator);
    if (Sort.BlockBaseMemory  != VK_NULL_HANDLE) vkFreeMemory(Device, Sort.BlockBaseMemory, Allocator);

    Sort = RadixSortSubmission{};
}

} // namespace Frontier
