/*==============================================================================================================================================
                                                        VOLUMEBOUNDSSUBMISSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the LBVH build's scene-bounds reduce. Initialize reads VolumeBoundsReduce.comp.spv, builds the set layout + pipeline layout +
//    pipeline, a pool with one set, and the six-word ordered-int accumulator buffer. BindVolumeBoundsGeometry points the set at the caller's vertex
//    and index streams. RecordVolumeBoundsReduce seeds the accumulators with two fills, barriers, and dispatches one workgroup per 256 triangles.
//    RetrieveVolumeBounds copies the accumulators back and resolves them out of the ordered-int domain. Raw Vulkan, no VMA, mirroring the
//    RadixSortSubmission / InstanceCullSubmission idioms.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Acceleration/VolumeBoundsSubmission.h"

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

void ReportVolumeBounds(const char* MessageText)
{
    std::fprintf(stderr, "[VolumeBounds] %s\n", MessageText);
}

// First memory type allowed by the requirement bitmask carrying every required property bit — mirrors the sort / cull / HiZ helper.
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

// Run a one-shot recorded command buffer to completion on the graphics queue.
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

// ─── ORDERED-INT BITS ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// 🔴 THE HOST COPY MUST STAY IDENTICAL TO THE SHADER COPY IN VolumeBoundsReduce.comp. The host writes the seeds and reads the result back; the
//    shader does the atomics in between. If the two transforms disagree the seed does not act as an identity and the box comes back wrong —
//    silently, and only for scenes touching the half of the number line where they diverge. The pair is proven equivalent, monotonic across the
//    sign boundary, and seed-correct by _ClaudeScratch/build/VolumeBoundsOrderedIntProbe.cpp.
int32_t FloatBitsToOrderedInt(float Value)
{
    int32_t Bits;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    return (Bits < 0) ? (Bits ^ 0x7FFFFFFF) : Bits;
}

float OrderedIntBitsToFloat(int32_t Bits)
{
    const int32_t Restored = (Bits < 0) ? (Bits ^ 0x7FFFFFFF) : Bits;
    float Value;
    std::memcpy(&Value, &Restored, sizeof(Value));
    return Value;
}

// The seeds, as the 32-bit patterns vkCmdFillBuffer writes. Minima start at +inf and maxima at -inf so the first real value wins every accumulator.
uint32_t RetrieveMinimumSeedPattern()
{
    const int32_t Ordered = FloatBitsToOrderedInt(INFINITY);
    uint32_t Pattern;
    std::memcpy(&Pattern, &Ordered, sizeof(Pattern));
    return Pattern;
}

uint32_t RetrieveMaximumSeedPattern()
{
    const int32_t Ordered = FloatBitsToOrderedInt(-INFINITY);
    uint32_t Pattern;
    std::memcpy(&Pattern, &Ordered, sizeof(Pattern));
    return Pattern;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeVolumeBoundsSubmission(VolumeBoundsSubmission& Bounds,
                                      VulkanHost&             Host,
                                      const char*             ShaderDirectory)
{
    Bounds = VolumeBoundsSubmission();
    Bounds.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportVolumeBounds("host has no device — refusing to build");
        return false;
    }
    if (ShaderDirectory == nullptr)
    {
        ReportVolumeBounds("no shader directory supplied — refusing to build");
        return false;
    }

    // ─── set layout : { vertices, indices, bounds accumulator } + the push range ───
    VkDescriptorSetLayoutBinding Bindings[3] = {};
    for (uint32_t Index = 0; Index < 3; ++Index)
    {
        Bindings[Index].binding         = Index;
        Bindings[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[Index].descriptorCount = 1;
        Bindings[Index].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = 3;
    LayoutInformation.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Bounds.ReduceSetLayout) != VK_SUCCESS)
    {
        ReportVolumeBounds("descriptor set layout creation failed");
        FinalizeVolumeBoundsSubmission(Bounds);
        return false;
    }

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(VolumeBoundsConstants);

    VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipelineLayoutInformation.setLayoutCount         = 1;
    PipelineLayoutInformation.pSetLayouts            = &Bounds.ReduceSetLayout;
    PipelineLayoutInformation.pushConstantRangeCount = 1;
    PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &PipelineLayoutInformation, Host.Allocator, &Bounds.ReduceLayout) != VK_SUCCESS)
    {
        ReportVolumeBounds("pipeline layout creation failed");
        FinalizeVolumeBoundsSubmission(Bounds);
        return false;
    }

    // ─── pipeline ───
    const std::string ModulePath = std::string(ShaderDirectory) + "/VolumeBoundsReduce.comp.spv";
    const std::vector<char> ModuleBytes = RetrieveShaderBytes(ModulePath);
    if (ModuleBytes.empty())
    {
        ReportVolumeBounds("VolumeBoundsReduce.comp.spv missing or unreadable");
        FinalizeVolumeBoundsSubmission(Bounds);
        return false;
    }

    VkShaderModule Module = ConstructShaderModule(Host, ModuleBytes);
    if (Module == VK_NULL_HANDLE)
    {
        ReportVolumeBounds("VolumeBoundsReduce shader module creation failed");
        FinalizeVolumeBoundsSubmission(Bounds);
        return false;
    }

    VkPipelineShaderStageCreateInfo StageInformation = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    StageInformation.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    StageInformation.module = Module;
    StageInformation.pName  = "main";

    VkComputePipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    PipelineInformation.stage  = StageInformation;
    PipelineInformation.layout = Bounds.ReduceLayout;

    const VkResult PipelineResult = vkCreateComputePipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInformation,
                                                             Host.Allocator, &Bounds.ReducePipeline);
    vkDestroyShaderModule(Host.Device, Module, Host.Allocator);
    if (PipelineResult != VK_SUCCESS)
    {
        Bounds.ReducePipeline = VK_NULL_HANDLE;
        ReportVolumeBounds("compute pipeline creation failed");
        FinalizeVolumeBoundsSubmission(Bounds);
        return false;
    }

    // ─── descriptor pool + the single set ───
    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = 3;

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 1;
    PoolInformation.poolSizeCount = 1;
    PoolInformation.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Bounds.DescriptorPool) != VK_SUCCESS)
    {
        ReportVolumeBounds("descriptor pool creation failed");
        FinalizeVolumeBoundsSubmission(Bounds);
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocate.descriptorPool     = Bounds.DescriptorPool;
    SetAllocate.descriptorSetCount = 1;
    SetAllocate.pSetLayouts        = &Bounds.ReduceSetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocate, &Bounds.ReduceSet) != VK_SUCCESS)
    {
        ReportVolumeBounds("descriptor set allocation failed");
        FinalizeVolumeBoundsSubmission(Bounds);
        return false;
    }

    // ─── the accumulator buffer ───
    // TRANSFER_DST for the seeding fills, TRANSFER_SRC for the readback, STORAGE for the shader's atomics.
    const VkDeviceSize BoundsBytes = (VkDeviceSize)VolumeBoundsElementCount * sizeof(int32_t);
    if (!AllocateBackedBuffer(Host, BoundsBytes,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              Bounds.BoundsBuffer, Bounds.BoundsMemory))
    {
        ReportVolumeBounds("bounds accumulator allocation failed");
        FinalizeVolumeBoundsSubmission(Bounds);
        return false;
    }

    Bounds.ReadyCondition = true;
    return true;
}

bool BindVolumeBoundsGeometry(VolumeBoundsSubmission& Bounds,
                              VkBuffer                VertexBuffer,
                              VkDeviceSize            VertexByteSize,
                              VkBuffer                IndexBuffer,
                              VkDeviceSize            IndexByteSize,
                              uint32_t                TriangleCount)
{
    if (!Bounds.ReadyCondition || Bounds.Host == nullptr)
        return false;
    if (VertexBuffer == VK_NULL_HANDLE || IndexBuffer == VK_NULL_HANDLE)
    {
        ReportVolumeBounds("geometry bind given a null buffer");
        return false;
    }

    // 🔴 The shader indexes Indices[TriangleIndex * 3 + 2], so the index stream must actually hold three indices per triangle. A caller passing a
    //    triangle count larger than the stream supports reads out of bounds — which on this device returns zeros rather than faulting, quietly
    //    folding a spurious centroid at vertex 0 into the box. Refusing is the only way this surfaces.
    const VkDeviceSize RequiredIndexBytes = (VkDeviceSize)TriangleCount * 3u * sizeof(uint32_t);
    if (RequiredIndexBytes > IndexByteSize)
    {
        ReportVolumeBounds("triangle count exceeds the index stream — refusing to bind");
        return false;
    }

    VkDescriptorBufferInfo BufferInformation[3] = {};
    BufferInformation[0].buffer = VertexBuffer;
    BufferInformation[0].offset = 0;
    BufferInformation[0].range  = VertexByteSize;
    BufferInformation[1].buffer = IndexBuffer;
    BufferInformation[1].offset = 0;
    BufferInformation[1].range  = IndexByteSize;
    BufferInformation[2].buffer = Bounds.BoundsBuffer;
    BufferInformation[2].offset = 0;
    BufferInformation[2].range  = (VkDeviceSize)VolumeBoundsElementCount * sizeof(int32_t);

    VkWriteDescriptorSet Writes[3] = {};
    for (uint32_t Index = 0; Index < 3; ++Index)
    {
        Writes[Index].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet          = Bounds.ReduceSet;
        Writes[Index].dstBinding      = Index;
        Writes[Index].descriptorCount = 1;
        Writes[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[Index].pBufferInfo     = &BufferInformation[Index];
    }
    vkUpdateDescriptorSets(Bounds.Host->Device, 3, Writes, 0, nullptr);

    Bounds.TriangleCount = TriangleCount;
    Bounds.GeometryBound = true;
    return true;
}

void RecordVolumeBoundsReduce(VolumeBoundsSubmission& Bounds, VkCommandBuffer CommandBuffer)
{
    if (!Bounds.ReadyCondition || !Bounds.GeometryBound || Bounds.TriangleCount == 0 || CommandBuffer == VK_NULL_HANDLE)
        return;

    // ─── seed the accumulators ───────────────────────────────────────────────────────────────────────────────────────────────────────────────
    // 🔴 Two fills, not one, because vkCmdFillBuffer writes a single repeated 32-bit pattern and the minima and maxima need OPPOSITE seeds. The
    //    minima occupy words 0..2 and the maxima words 3..5, so each half is one contiguous fill.
    const VkDeviceSize HalfBytes = (VkDeviceSize)(VolumeBoundsElementCount / 2u) * sizeof(int32_t);
    vkCmdFillBuffer(CommandBuffer, Bounds.BoundsBuffer, 0,         HalfBytes, RetrieveMinimumSeedPattern());
    vkCmdFillBuffer(CommandBuffer, Bounds.BoundsBuffer, HalfBytes, HalfBytes, RetrieveMaximumSeedPattern());

    // The fills must land before the shader's atomics read them, or the seed races the reduction and the box keeps whatever survived.
    VkBufferMemoryBarrier SeedBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    SeedBarrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    SeedBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    SeedBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    SeedBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    SeedBarrier.buffer              = Bounds.BoundsBuffer;
    SeedBarrier.offset              = 0;
    SeedBarrier.size                = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 1, &SeedBarrier, 0, nullptr);

    // ─── the reduce ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Bounds.ReducePipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Bounds.ReduceLayout,
                            0, 1, &Bounds.ReduceSet, 0, nullptr);

    VolumeBoundsConstants Constants;
    Constants.TriangleCount = Bounds.TriangleCount;
    vkCmdPushConstants(CommandBuffer, Bounds.ReduceLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants), &Constants);

    const uint32_t GroupCount = ComputeGroupCount(Bounds.TriangleCount, VolumeBoundsLanesPerGroup);
    vkCmdDispatch(CommandBuffer, GroupCount, 1, 1);

    // The atomics must complete before anything reads the box — a readback copy (transfer) or the Morton pass (compute).
    VkBufferMemoryBarrier ResultBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    ResultBarrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    ResultBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    ResultBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ResultBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ResultBarrier.buffer              = Bounds.BoundsBuffer;
    ResultBarrier.offset              = 0;
    ResultBarrier.size                = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 1, &ResultBarrier, 0, nullptr);
}

bool RetrieveVolumeBounds(VolumeBoundsSubmission& Bounds,
                          VkCommandPool           CommandPool,
                          VolumeBounds&           OutBounds)
{
    OutBounds = VolumeBounds();
    if (!Bounds.ReadyCondition || Bounds.Host == nullptr || CommandPool == VK_NULL_HANDLE)
        return false;

    VulkanHost& Host = *Bounds.Host;
    const VkDeviceSize BoundsBytes = (VkDeviceSize)VolumeBoundsElementCount * sizeof(int32_t);

    VkBuffer       StagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    if (!AllocateBackedBuffer(Host, BoundsBytes,
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              StagingBuffer, StagingMemory))
    {
        ReportVolumeBounds("readback staging allocation failed");
        return false;
    }

    const bool Copied = ExecuteBlockingTransfer(Host, CommandPool, [&](VkCommandBuffer TransferCommand)
    {
        VkBufferCopy Region = {};
        Region.srcOffset = 0;
        Region.dstOffset = 0;
        Region.size      = BoundsBytes;
        vkCmdCopyBuffer(TransferCommand, Bounds.BoundsBuffer, StagingBuffer, 1, &Region);
    });

    bool Succeeded = false;
    if (Copied)
    {
        void* Mapped = nullptr;
        if (vkMapMemory(Host.Device, StagingMemory, 0, BoundsBytes, 0, &Mapped) == VK_SUCCESS && Mapped != nullptr)
        {
            int32_t Ordered[VolumeBoundsElementCount] = {};
            std::memcpy(Ordered, Mapped, (size_t)BoundsBytes);
            vkUnmapMemory(Host.Device, StagingMemory);

            // 🔴 Emptiness is detected in the ORDERED domain, before the float conversion, because the seeds are exactly the patterns that make an
            //    untouched box read inverted. Resolving first and comparing floats would work too, but +inf > -inf comparisons invite a caller to
            //    "helpfully" normalise them away; testing the raw accumulators keeps the empty case unambiguous.
            const bool Untouched = (Ordered[0] > Ordered[3]) || (Ordered[1] > Ordered[4]) || (Ordered[2] > Ordered[5]);

            OutBounds.MinimumX = OrderedIntBitsToFloat(Ordered[0]);
            OutBounds.MinimumY = OrderedIntBitsToFloat(Ordered[1]);
            OutBounds.MinimumZ = OrderedIntBitsToFloat(Ordered[2]);
            OutBounds.MaximumX = OrderedIntBitsToFloat(Ordered[3]);
            OutBounds.MaximumY = OrderedIntBitsToFloat(Ordered[4]);
            OutBounds.MaximumZ = OrderedIntBitsToFloat(Ordered[5]);
            OutBounds.EmptyCondition = Untouched;
            Succeeded = true;
        }
        else
        {
            ReportVolumeBounds("readback staging map failed");
        }
    }
    else
    {
        ReportVolumeBounds("readback transfer failed");
    }

    vkDestroyBuffer(Host.Device, StagingBuffer, Host.Allocator);
    vkFreeMemory(Host.Device, StagingMemory, Host.Allocator);
    return Succeeded;
}

VkBuffer RetrieveVolumeBoundsBuffer(const VolumeBoundsSubmission& Bounds)
{
    return Bounds.BoundsBuffer;
}

void FinalizeVolumeBoundsSubmission(VolumeBoundsSubmission& Bounds)
{
    if (Bounds.Host == nullptr || Bounds.Host->Device == VK_NULL_HANDLE)
    {
        Bounds = VolumeBoundsSubmission();
        return;
    }

    VulkanHost& Host = *Bounds.Host;

    if (Bounds.BoundsBuffer   != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, Bounds.BoundsBuffer, Host.Allocator);
    if (Bounds.BoundsMemory   != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Bounds.BoundsMemory, Host.Allocator);
    // The set is freed with the pool; freeing it separately would need FREE_DESCRIPTOR_SET on the pool, which is not set.
    if (Bounds.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Host.Device, Bounds.DescriptorPool, Host.Allocator);
    if (Bounds.ReducePipeline != VK_NULL_HANDLE) vkDestroyPipeline(Host.Device, Bounds.ReducePipeline, Host.Allocator);
    if (Bounds.ReduceLayout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(Host.Device, Bounds.ReduceLayout, Host.Allocator);
    if (Bounds.ReduceSetLayout!= VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Host.Device, Bounds.ReduceSetLayout, Host.Allocator);

    Bounds = VolumeBoundsSubmission();
}

} // namespace Frontier
