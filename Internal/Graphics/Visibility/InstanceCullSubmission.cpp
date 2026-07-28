/*==============================================================================================================================================
                                                         INSTANCECULLSUBMISSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the per-instance GPU cull. Initialize reads InstanceCullSubmission.comp.spv, builds a set layout (five storage buffers +
//    one sampled pyramid, all compute stage) + a pipeline layout carrying the InstanceCullConstants push range, the compute pipeline, and the
//    owned buffers: a host-visible mapped RecordBuffer (written once per scene — the record count is tiny, mirroring the raster's instance buffer),
//    and device-local SurvivorBuffer / RetestBuffer / CounterBuffer / ArgumentBuffer (compute writes them, the raster reads them, so they carry
//    STORAGE | INDIRECT | TRANSFER_DST). Upload fits one PartitionCullRecord per instance on the CPU (mesh-local sphere/cone transformed by each
//    Model) and memcpies them into the mapped record buffer. ResetFrame zeroes the counters and reseeds the argument with vkCmdUpdateBuffer before
//    the early pass. RecordPass rebinds the HiZ source when its view changed, pushes the constants, and dispatches one lane per record, then
//    barriers the outputs for the indirect + vertex read the raster performs. Raw Vulkan, no VMA, mirroring the HiZ / raster idioms.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Visibility/InstanceCullSubmission.h"

#include <cmath>
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

void ReportInstanceCull(const char* MessageText)
{
    std::fprintf(stderr, "[InstanceCull] %s\n", MessageText);
}

// First memory type allowed by the requirement bitmask carrying every required property bit — mirrors the HiZ / raster helper.
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

// Build the set layout (bindings 0/2/3/4/5 = storage buffers, binding 6 = combined image sampler; all compute stage) + the pipeline layout with the
// InstanceCullConstants push range. Binding numbers match InstanceCullSubmission.comp (binding 1 is intentionally unused — the shader's SurvivorBuffer
// is at binding 2 to leave room for a future second survivor stream without renumbering). Returns false with every handle null on any failure.
bool ConstructCullPipelineLayout(InstanceCullSubmission& Cull)
{
    VulkanHost& Host = *Cull.Host;

    VkDescriptorSetLayoutBinding Bindings[6] = {};
    const uint32_t StorageBindingIds[5] = { 0u, 2u, 3u, 4u, 5u };
    for (int Index = 0; Index < 5; ++Index)
    {
        Bindings[Index].binding         = StorageBindingIds[Index];
        Bindings[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[Index].descriptorCount = 1;
        Bindings[Index].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    Bindings[5].binding         = 6;
    Bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Bindings[5].descriptorCount = 1;
    Bindings[5].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = 6;
    LayoutInformation.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Cull.SetLayout) != VK_SUCCESS)
    {
        Cull.SetLayout = VK_NULL_HANDLE;
        return false;
    }

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(InstanceCullConstants);

    VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipelineLayoutInformation.setLayoutCount         = 1;
    PipelineLayoutInformation.pSetLayouts            = &Cull.SetLayout;
    PipelineLayoutInformation.pushConstantRangeCount = 1;
    PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &PipelineLayoutInformation, Host.Allocator, &Cull.PipelineLayout) != VK_SUCCESS)
    {
        Cull.PipelineLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Build the compute pipeline from the shader module. Destroys the module before returning. False with the pipeline null on any failure.
bool ConstructCullPipeline(InstanceCullSubmission& Cull, const char* ShaderDirectory)
{
    VulkanHost& Host = *Cull.Host;
    const std::string Directory = ShaderDirectory;
    VkShaderModule Module = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/InstanceCullSubmission.comp.spv"));
    if (Module == VK_NULL_HANDLE)
    {
        ReportInstanceCull("cull shader module unavailable — cull will not run");
        return false;
    }

    VkPipelineShaderStageCreateInfo StageInformation = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    StageInformation.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    StageInformation.module = Module;
    StageInformation.pName  = "main";

    VkComputePipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    PipelineInformation.stage  = StageInformation;
    PipelineInformation.layout = Cull.PipelineLayout;
    const VkResult Outcome = vkCreateComputePipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInformation, Host.Allocator, &Cull.Pipeline);
    vkDestroyShaderModule(Host.Device, Module, Host.Allocator);
    if (Outcome != VK_SUCCESS)
    {
        Cull.Pipeline = VK_NULL_HANDLE;
        ReportInstanceCull("cull compute pipeline creation failed");
        return false;
    }
    return true;
}

// Allocate every per-frame buffer sized to MaxInstances. RecordBuffer is host-visible mapped (written once per scene). Survivor / Retest / Counter /
// Argument are device-local and carry STORAGE | INDIRECT | TRANSFER_DST (the cull writes them, the raster reads the argument indirectly, and the host
// resets them with vkCmdUpdateBuffer). On any failure everything claimed so far is left for the caller's Finalize to release.
bool ConstructCullBuffers(InstanceCullSubmission& Cull, uint32_t MaxInstances)
{
    VulkanHost& Host = *Cull.Host;

    const VkDeviceSize RecordBytes   = (VkDeviceSize)MaxInstances * sizeof(PartitionCullRecord);
    const VkDeviceSize SurvivorBytes = (VkDeviceSize)MaxInstances * sizeof(uint32_t);
    const VkDeviceSize RetestBytes   = (VkDeviceSize)MaxInstances * sizeof(uint32_t);
    const VkDeviceSize CounterBytes  = (VkDeviceSize)2 * sizeof(uint32_t);
    const VkDeviceSize ArgumentBytes = (VkDeviceSize)sizeof(InstanceDrawArgument);

    const VkBufferUsageFlags DeviceStorage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (!AllocateBackedBuffer(Host, RecordBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              Cull.RecordBuffer, Cull.RecordMemory))
        return false;
    if (!AllocateBackedBuffer(Host, SurvivorBytes, DeviceStorage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Cull.SurvivorBuffer, Cull.SurvivorMemory))
        return false;
    if (!AllocateBackedBuffer(Host, RetestBytes, DeviceStorage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Cull.RetestBuffer, Cull.RetestMemory))
        return false;
    if (!AllocateBackedBuffer(Host, CounterBytes, DeviceStorage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Cull.CounterBuffer, Cull.CounterMemory))
        return false;
    if (!AllocateBackedBuffer(Host, ArgumentBytes, DeviceStorage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Cull.ArgumentBuffer, Cull.ArgumentMemory))
        return false;

    Cull.RecordCapacity = MaxInstances;
    return true;
}

// Allocate the descriptor pool + one set and point the five storage bindings at the owned buffers. Binding 6 (the pyramid) is bound at record time
// (it points at an external view). Returns false on any failure.
bool ConstructCullDescriptors(InstanceCullSubmission& Cull)
{
    VulkanHost& Host = *Cull.Host;

    VkDescriptorPoolSize PoolSizes[2] = {};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSizes[0].descriptorCount = 5;
    PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 1;
    PoolInformation.poolSizeCount = 2;
    PoolInformation.pPoolSizes    = PoolSizes;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Cull.DescriptorPool) != VK_SUCCESS)
    {
        Cull.DescriptorPool = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocation.descriptorPool     = Cull.DescriptorPool;
    SetAllocation.descriptorSetCount = 1;
    SetAllocation.pSetLayouts        = &Cull.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, &Cull.CullSet) != VK_SUCCESS)
    {
        Cull.CullSet = VK_NULL_HANDLE;
        return false;
    }

    // Point the five storage bindings at their owned buffers once; only the pyramid rebinds later.
    VkDescriptorBufferInfo RecordInfo   = { Cull.RecordBuffer,   0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo SurvivorInfo = { Cull.SurvivorBuffer, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo CounterInfo  = { Cull.CounterBuffer,  0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo RetestInfo   = { Cull.RetestBuffer,   0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo ArgumentInfo = { Cull.ArgumentBuffer, 0, VK_WHOLE_SIZE };

    VkWriteDescriptorSet Writes[5] = {};
    const uint32_t BindingIds[5]                 = { 0u, 2u, 3u, 4u, 5u };
    const VkDescriptorBufferInfo* BufferInfos[5] = { &RecordInfo, &SurvivorInfo, &CounterInfo, &RetestInfo, &ArgumentInfo };
    for (int Index = 0; Index < 5; ++Index)
    {
        Writes[Index].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet          = Cull.CullSet;
        Writes[Index].dstBinding      = BindingIds[Index];
        Writes[Index].descriptorCount = 1;
        Writes[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[Index].pBufferInfo     = BufferInfos[Index];
    }
    vkUpdateDescriptorSets(Host.Device, 5, Writes, 0, nullptr);
    return true;
}

// Transform a mesh-local bounding sphere by an instance's column-major Model[16]: the centre goes through the full transform, the radius scales by
// the largest column length (the max axis scale, so the fit stays enclosing under non-uniform scale).
void TransformSphereByModel(const float Model[16], const float LocalSphere[4], float& OutCentreX, float& OutCentreY, float& OutCentreZ, float& OutRadius)
{
    const float Cx = LocalSphere[0], Cy = LocalSphere[1], Cz = LocalSphere[2];
    // Column-major: element (col c, row r) at Model[c*4 + r]. World centre = Model * (centre, 1).
    OutCentreX = Model[0] * Cx + Model[4] * Cy + Model[8]  * Cz + Model[12];
    OutCentreY = Model[1] * Cx + Model[5] * Cy + Model[9]  * Cz + Model[13];
    OutCentreZ = Model[2] * Cx + Model[6] * Cy + Model[10] * Cz + Model[14];

    auto ColumnLength = [&](int Column)
    {
        const float X = Model[Column * 4 + 0], Y = Model[Column * 4 + 1], Z = Model[Column * 4 + 2];
        return std::sqrt(X * X + Y * Y + Z * Z);
    };
    const float ScaleX = ColumnLength(0), ScaleY = ColumnLength(1), ScaleZ = ColumnLength(2);
    float MaxScale = ScaleX > ScaleY ? ScaleX : ScaleY;
    if (ScaleZ > MaxScale) MaxScale = ScaleZ;
    OutRadius = LocalSphere[3] * MaxScale;
}

// Rotate a mesh-local normal-cone axis by an instance's Model (rotation-only upper-left, then renormalize). The cosine (half-angle) is scale- and
// rotation-invariant, so it passes through unchanged. A non-coneable record (cosine <= -1) stays non-coneable.
void TransformConeByModel(const float Model[16], const float LocalCone[4], float& OutAxisX, float& OutAxisY, float& OutAxisZ, float& OutCosine)
{
    const float Ax = LocalCone[0], Ay = LocalCone[1], Az = LocalCone[2];
    float Rx = Model[0] * Ax + Model[4] * Ay + Model[8]  * Az;
    float Ry = Model[1] * Ax + Model[5] * Ay + Model[9]  * Az;
    float Rz = Model[2] * Ax + Model[6] * Ay + Model[10] * Az;
    const float Length = std::sqrt(Rx * Rx + Ry * Ry + Rz * Rz);
    if (Length > 1e-6f)
    {
        Rx /= Length; Ry /= Length; Rz /= Length;
    }
    OutAxisX  = Rx;
    OutAxisY  = Ry;
    OutAxisZ  = Rz;
    OutCosine = LocalCone[3];
}

// Stage bytes into a device-local buffer through a host-visible scratch buffer + one-shot copy on the graphics queue, waiting on a fence. Mirrors
// BufferAllocation's StageBytesIntoBuffer. Only used for the initial argument seed; the per-frame reset uses vkCmdUpdateBuffer instead.
bool ClearDeviceBuffer(VulkanHost& Host, VkCommandPool CommandPool, VkBuffer Destination, VkDeviceSize ByteSize, uint32_t FillValue)
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

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeInstanceCullSubmission(InstanceCullSubmission& Cull,
                                      VulkanHost&             Host,
                                      uint32_t                MaxInstances,
                                      const char*             ShaderDirectory)
{
    Cull = InstanceCullSubmission{};
    Cull.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportInstanceCull("no device — cull not built");
        return false;
    }
    if (MaxInstances == 0)
        MaxInstances = 1;

    if (!ConstructCullPipelineLayout(Cull))
    {
        ReportInstanceCull("cull pipeline layout creation failed");
        FinalizeInstanceCullSubmission(Cull);
        return false;
    }
    if (!ConstructCullPipeline(Cull, ShaderDirectory))
    {
        FinalizeInstanceCullSubmission(Cull);
        return false;
    }
    if (!ConstructCullBuffers(Cull, MaxInstances))
    {
        ReportInstanceCull("cull buffer allocation failed");
        FinalizeInstanceCullSubmission(Cull);
        return false;
    }
    if (!ConstructCullDescriptors(Cull))
    {
        ReportInstanceCull("cull descriptor plumbing creation failed");
        FinalizeInstanceCullSubmission(Cull);
        return false;
    }

    Cull.ReadyCondition = true;
    return true;
}

void UploadInstanceCullRecords(InstanceCullSubmission&                  Cull,
                               VkCommandPool                            CommandPool,
                               const std::vector<SuzanneSceneInstance>& Instances,
                               const float                              LocalSphere[4],
                               const float                              LocalCone[4],
                               uint32_t                                 MeshIndexCount)
{
    (void)CommandPool;
    if (!Cull.ReadyCondition || Cull.Host == nullptr || Cull.RecordMemory == VK_NULL_HANDLE)
        return;

    uint32_t Count = (uint32_t)Instances.size();
    if (Count > Cull.RecordCapacity)
    {
        ReportInstanceCull("scene exceeds cull-record capacity — truncating");
        Count = Cull.RecordCapacity;
    }

    // Fit one record per instance on the CPU: the mesh-local sphere/cone transformed into world by each instance's Model.
    std::vector<PartitionCullRecord> Records(Count);
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        PartitionCullRecord& Record = Records[Index];
        TransformSphereByModel(Instances[Index].Model, LocalSphere,
                               Record.SphereX, Record.SphereY, Record.SphereZ, Record.SphereRadius);
        TransformConeByModel(Instances[Index].Model, LocalCone,
                             Record.ConeAxisX, Record.ConeAxisY, Record.ConeAxisZ, Record.ConeCosine);
    }

    void* Mapped = nullptr;
    const VkDeviceSize RecordCapacityBytes = (VkDeviceSize)Cull.RecordCapacity * sizeof(PartitionCullRecord);
    if (vkMapMemory(Cull.Host->Device, Cull.RecordMemory, 0, RecordCapacityBytes, 0, &Mapped) != VK_SUCCESS)
    {
        ReportInstanceCull("cull record buffer map failed");
        Cull.RecordCount = 0;
        return;
    }
    if (Count > 0)
        std::memcpy(Mapped, Records.data(), (size_t)Count * sizeof(PartitionCullRecord));
    vkUnmapMemory(Cull.Host->Device, Cull.RecordMemory);

    Cull.RecordCount    = Count;
    Cull.MeshIndexCount = MeshIndexCount;

    // Seed the argument buffer's static fields once (IndexCount reseeded per frame in ResetFrame anyway; this covers the pre-first-frame state).
    ClearDeviceBuffer(*Cull.Host, CommandPool, Cull.CounterBuffer, (VkDeviceSize)2 * sizeof(uint32_t), 0u);
}

void ResetInstanceCullFrame(InstanceCullSubmission& Cull, VkCommandBuffer CommandBuffer)
{
    if (!Cull.ReadyCondition || Cull.Host == nullptr)
        return;

    // Zero both counters, then reseed the whole argument with instanceCount 0 and the live mesh index count. vkCmdUpdateBuffer is inline (small
    // payloads), so no staging buffer is needed. Both writes are TRANSFER, so the cull's compute read must wait on a transfer->compute barrier.
    const uint32_t ZeroPair[2] = { 0u, 0u };
    vkCmdUpdateBuffer(CommandBuffer, Cull.CounterBuffer, 0, sizeof(ZeroPair), ZeroPair);

    InstanceDrawArgument Argument = {};
    Argument.IndexCount    = Cull.MeshIndexCount;
    Argument.InstanceCount = 0;
    Argument.FirstIndex    = 0;
    Argument.VertexOffset  = 0;
    Argument.FirstInstance = 0;
    vkCmdUpdateBuffer(CommandBuffer, Cull.ArgumentBuffer, 0, sizeof(InstanceDrawArgument), &Argument);

    // Fence the resets before the cull's compute reads / atomics touch the same buffers.
    VkMemoryBarrier ResetBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    ResetBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    ResetBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &ResetBarrier, 0, nullptr, 0, nullptr);
}

void RecordInstanceCullPass(InstanceCullSubmission&         Cull,
                            const HierarchicalDepthPyramid& Pyramid,
                            const InstanceCullConstants&    Constants,
                            VkCommandBuffer                 CommandBuffer)
{
    if (!Cull.ReadyCondition || Cull.Host == nullptr || Cull.RecordCount == 0)
        return;
    if (!Pyramid.ReadyCondition || Pyramid.CurrentLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || Pyramid.LevelCount == 0)
        return;

    VulkanHost& Host = *Cull.Host;

    // Rebind the HiZ source (binding 6) when its top-level sampled view changed (rebuilt on resize). The pyramid's whole chain is one image; the
    // cull samples it with textureLod across levels, so it binds the mip-0 sampled view of the full chain — but the pyramid exposes per-mip views.
    // Bind the level-0 sampled view (a full-chain sampler2D is not available here); textureLod's level argument then selects the mip.
    const VkImageView PyramidView = Pyramid.SampledViews.empty() ? VK_NULL_HANDLE : Pyramid.SampledViews[0];
    if (PyramidView != VK_NULL_HANDLE && PyramidView != Cull.BoundPyramidView)
    {
        VkDescriptorImageInfo PyramidInfo = {};
        PyramidInfo.sampler     = Pyramid.PointSampler;
        PyramidInfo.imageView   = PyramidView;
        PyramidInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet PyramidWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        PyramidWrite.dstSet          = Cull.CullSet;
        PyramidWrite.dstBinding      = 6;
        PyramidWrite.descriptorCount = 1;
        PyramidWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        PyramidWrite.pImageInfo      = &PyramidInfo;
        vkUpdateDescriptorSets(Host.Device, 1, &PyramidWrite, 0, nullptr);
        Cull.BoundPyramidView = PyramidView;
    }

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Cull.Pipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Cull.PipelineLayout, 0, 1, &Cull.CullSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Cull.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(InstanceCullConstants), &Constants);

    const uint32_t Groups = (Constants.RecordCount + InstanceCullWorkgroupEdge - 1) / InstanceCullWorkgroupEdge;
    vkCmdDispatch(CommandBuffer, Groups, 1, 1);

    // Fence the cull's writes before the raster's indirect read (argument buffer) and vertex-stage storage read (survivor list). Also cover the
    // early->late chain: the late pass reads the re-test list + counters the early pass wrote.
    VkMemoryBarrier CullBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    CullBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    CullBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0, 1, &CullBarrier, 0, nullptr, 0, nullptr);
}

void FinalizeInstanceCullSubmission(InstanceCullSubmission& Cull)
{
    if (Cull.Host == nullptr || Cull.Host->Device == VK_NULL_HANDLE)
    {
        Cull = InstanceCullSubmission{};
        return;
    }
    VkDevice Device = Cull.Host->Device;
    const VkAllocationCallbacks* Allocator = Cull.Host->Allocator;

    if (Cull.Pipeline       != VK_NULL_HANDLE) vkDestroyPipeline(Device, Cull.Pipeline, Allocator);
    if (Cull.PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Cull.PipelineLayout, Allocator);
    if (Cull.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Cull.DescriptorPool, Allocator);   // frees CullSet
    if (Cull.SetLayout      != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Cull.SetLayout, Allocator);

    if (Cull.RecordBuffer   != VK_NULL_HANDLE) vkDestroyBuffer(Device, Cull.RecordBuffer, Allocator);
    if (Cull.RecordMemory   != VK_NULL_HANDLE) vkFreeMemory(Device, Cull.RecordMemory, Allocator);
    if (Cull.SurvivorBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, Cull.SurvivorBuffer, Allocator);
    if (Cull.SurvivorMemory != VK_NULL_HANDLE) vkFreeMemory(Device, Cull.SurvivorMemory, Allocator);
    if (Cull.RetestBuffer   != VK_NULL_HANDLE) vkDestroyBuffer(Device, Cull.RetestBuffer, Allocator);
    if (Cull.RetestMemory   != VK_NULL_HANDLE) vkFreeMemory(Device, Cull.RetestMemory, Allocator);
    if (Cull.CounterBuffer  != VK_NULL_HANDLE) vkDestroyBuffer(Device, Cull.CounterBuffer, Allocator);
    if (Cull.CounterMemory  != VK_NULL_HANDLE) vkFreeMemory(Device, Cull.CounterMemory, Allocator);
    if (Cull.ArgumentBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, Cull.ArgumentBuffer, Allocator);
    if (Cull.ArgumentMemory != VK_NULL_HANDLE) vkFreeMemory(Device, Cull.ArgumentMemory, Allocator);

    Cull = InstanceCullSubmission{};
}

} // namespace Frontier
