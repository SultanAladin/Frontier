/*==============================================================================================================================================
                                                       INSTANCEBOUNDSSUBMISSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the TLAS build's instance-bounds reduce and Morton emit. Initialize reads both .spv files, builds a set layout + pipeline
//    layout + pipeline for each, a pool with two sets, and the six-word ordered-int accumulator. BindInstanceBoundsScene points both sets at the
//    instance array and the arena; BindInstanceMortonTarget points the Morton set at the sort's key/payload buffers. The two record functions seed
//    and dispatch. Raw Vulkan, no VMA, drawing its device helpers from AccelerationDeviceSupport.h rather than carrying a fourth private copy.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Acceleration/InstanceBoundsSubmission.h"
#include "Graphics/Acceleration/AccelerationDeviceSupport.h"

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

void ReportInstanceBounds(const char* MessageText)
{
    std::fprintf(stderr, "[InstanceBounds] %s\n", MessageText);
}

// 📝 Binding counts for the two sets. The reduce reads three inputs and writes the accumulator; the Morton pass reads those same four (the
//    accumulator now as input) and writes two more.
constexpr uint32_t ReduceBindingCount = 4;
constexpr uint32_t MortonBindingCount = 6;

// Build a storage-buffer-only descriptor set layout of BindingCount sequential compute bindings.
bool ConstructStorageSetLayout(VulkanHost& Host, uint32_t BindingCount, VkDescriptorSetLayout& OutLayout)
{
    std::vector<VkDescriptorSetLayoutBinding> Bindings(BindingCount);
    for (uint32_t Index = 0; Index < BindingCount; ++Index)
    {
        Bindings[Index] = {};
        Bindings[Index].binding         = Index;
        Bindings[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[Index].descriptorCount = 1;
        Bindings[Index].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = BindingCount;
    LayoutInformation.pBindings    = Bindings.data();
    return vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &OutLayout) == VK_SUCCESS;
}

// Build a compute pipeline over ModuleName in ShaderDirectory, with SetLayout and an InstanceBoundsConstants push range.
bool ConstructComputePipeline(VulkanHost&            Host,
                              const char*            ShaderDirectory,
                              const char*            ModuleName,
                              VkDescriptorSetLayout  SetLayout,
                              VkPipelineLayout&      OutPipelineLayout,
                              VkPipeline&            OutPipeline)
{
    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(InstanceBoundsConstants);

    VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipelineLayoutInformation.setLayoutCount         = 1;
    PipelineLayoutInformation.pSetLayouts            = &SetLayout;
    PipelineLayoutInformation.pushConstantRangeCount = 1;
    PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &PipelineLayoutInformation, Host.Allocator, &OutPipelineLayout) != VK_SUCCESS)
        return false;

    const std::string ModulePath = std::string(ShaderDirectory) + "/" + ModuleName;
    const std::vector<char> ModuleBytes = RetrieveAccelerationShaderBytes(ModulePath);
    if (ModuleBytes.empty())
        return false;

    VkShaderModule Module = ConstructAccelerationShaderModule(Host, ModuleBytes);
    if (Module == VK_NULL_HANDLE)
        return false;

    VkPipelineShaderStageCreateInfo StageInformation = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    StageInformation.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    StageInformation.module = Module;
    StageInformation.pName  = "main";

    VkComputePipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    PipelineInformation.stage  = StageInformation;
    PipelineInformation.layout = OutPipelineLayout;

    const VkResult PipelineResult = vkCreateComputePipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInformation,
                                                             Host.Allocator, &OutPipeline);
    vkDestroyShaderModule(Host.Device, Module, Host.Allocator);
    if (PipelineResult != VK_SUCCESS)
    {
        OutPipeline = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Write one storage-buffer descriptor into Set at Binding.
void WriteStorageDescriptor(VkDevice               Device,
                            VkDescriptorSet        Set,
                            uint32_t               Binding,
                            VkBuffer               Buffer,
                            VkDeviceSize           ByteSize)
{
    VkDescriptorBufferInfo BufferInformation = {};
    BufferInformation.buffer = Buffer;
    BufferInformation.offset = 0;
    BufferInformation.range  = ByteSize;

    VkWriteDescriptorSet Write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    Write.dstSet          = Set;
    Write.dstBinding      = Binding;
    Write.descriptorCount = 1;
    Write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Write.pBufferInfo     = &BufferInformation;
    vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);
}

// The compute->compute / compute->transfer barrier both record paths end with.
void RecordBufferBarrier(VkCommandBuffer      CommandBuffer,
                         VkBuffer             Buffer,
                         VkAccessFlags        SourceAccess,
                         VkAccessFlags        TargetAccess,
                         VkPipelineStageFlags SourceStage,
                         VkPipelineStageFlags TargetStage)
{
    VkBufferMemoryBarrier Barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    Barrier.srcAccessMask       = SourceAccess;
    Barrier.dstAccessMask       = TargetAccess;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.buffer              = Buffer;
    Barrier.offset              = 0;
    Barrier.size                = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(CommandBuffer, SourceStage, TargetStage, 0, 0, nullptr, 1, &Barrier, 0, nullptr);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeInstanceBoundsSubmission(InstanceBoundsSubmission& Bounds,
                                        VulkanHost&               Host,
                                        const char*               ShaderDirectory)
{
    Bounds = InstanceBoundsSubmission();
    Bounds.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportInstanceBounds("host has no device — refusing to build");
        return false;
    }
    if (ShaderDirectory == nullptr)
    {
        ReportInstanceBounds("no shader directory supplied — refusing to build");
        return false;
    }

    // ─── set layouts ───
    if (!ConstructStorageSetLayout(Host, ReduceBindingCount, Bounds.ReduceSetLayout))
    {
        ReportInstanceBounds("reduce descriptor set layout creation failed");
        FinalizeInstanceBoundsSubmission(Bounds);
        return false;
    }
    if (!ConstructStorageSetLayout(Host, MortonBindingCount, Bounds.MortonSetLayout))
    {
        ReportInstanceBounds("morton descriptor set layout creation failed");
        FinalizeInstanceBoundsSubmission(Bounds);
        return false;
    }

    // ─── pipelines ───
    if (!ConstructComputePipeline(Host, ShaderDirectory, "InstanceBoundsReduce.comp.spv",
                                  Bounds.ReduceSetLayout, Bounds.ReduceLayout, Bounds.ReducePipeline))
    {
        ReportInstanceBounds("InstanceBoundsReduce pipeline creation failed (missing .spv or allocation failure)");
        FinalizeInstanceBoundsSubmission(Bounds);
        return false;
    }
    if (!ConstructComputePipeline(Host, ShaderDirectory, "InstanceMortonCode.comp.spv",
                                  Bounds.MortonSetLayout, Bounds.MortonLayout, Bounds.MortonPipeline))
    {
        ReportInstanceBounds("InstanceMortonCode pipeline creation failed (missing .spv or allocation failure)");
        FinalizeInstanceBoundsSubmission(Bounds);
        return false;
    }

    // ─── descriptor pool + the two sets ───
    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = ReduceBindingCount + MortonBindingCount;

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 2;
    PoolInformation.poolSizeCount = 1;
    PoolInformation.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Bounds.DescriptorPool) != VK_SUCCESS)
    {
        ReportInstanceBounds("descriptor pool creation failed");
        FinalizeInstanceBoundsSubmission(Bounds);
        return false;
    }

    VkDescriptorSetAllocateInfo ReduceAllocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ReduceAllocate.descriptorPool     = Bounds.DescriptorPool;
    ReduceAllocate.descriptorSetCount = 1;
    ReduceAllocate.pSetLayouts        = &Bounds.ReduceSetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &ReduceAllocate, &Bounds.ReduceSet) != VK_SUCCESS)
    {
        ReportInstanceBounds("reduce descriptor set allocation failed");
        FinalizeInstanceBoundsSubmission(Bounds);
        return false;
    }

    VkDescriptorSetAllocateInfo MortonAllocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    MortonAllocate.descriptorPool     = Bounds.DescriptorPool;
    MortonAllocate.descriptorSetCount = 1;
    MortonAllocate.pSetLayouts        = &Bounds.MortonSetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &MortonAllocate, &Bounds.MortonSet) != VK_SUCCESS)
    {
        ReportInstanceBounds("morton descriptor set allocation failed");
        FinalizeInstanceBoundsSubmission(Bounds);
        return false;
    }

    // ─── the accumulator buffer ───
    // TRANSFER_DST for the seeding fills, TRANSFER_SRC for the readback, STORAGE for the shader's atomics.
    const VkDeviceSize BoundsBytes = (VkDeviceSize)InstanceBoundsElementCount * sizeof(int32_t);
    if (!AllocateAccelerationBuffer(Host, BoundsBytes,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    Bounds.BoundsBuffer, Bounds.BoundsMemory))
    {
        ReportInstanceBounds("bounds accumulator allocation failed");
        FinalizeInstanceBoundsSubmission(Bounds);
        return false;
    }

    Bounds.ReadyCondition = true;
    return true;
}

bool BindInstanceBoundsScene(InstanceBoundsSubmission& Bounds,
                             VkBuffer                  InstanceBuffer,
                             VkDeviceSize              InstanceByteSize,
                             VkBuffer                  SliceBuffer,
                             VkDeviceSize              SliceByteSize,
                             VkBuffer                  NodeBuffer,
                             VkDeviceSize              NodeByteSize,
                             uint32_t                  InstanceCount,
                             uint32_t                  SliceCount)
{
    if (!Bounds.ReadyCondition || Bounds.Host == nullptr)
        return false;
    if (InstanceBuffer == VK_NULL_HANDLE || SliceBuffer == VK_NULL_HANDLE || NodeBuffer == VK_NULL_HANDLE)
    {
        ReportInstanceBounds("scene bind given a null buffer");
        return false;
    }

    // 🔴 A slice count larger than the table actually holds lets a shader read past its end while looking up a MeshOrdinal, and on this device an
    //    out-of-bounds storage read returns zeros rather than faulting — so the lookup yields NodeOffset 0 and the instance silently borrows mesh
    //    zero's box. Refusing is the only way this surfaces.
    const VkDeviceSize RequiredSliceBytes = (VkDeviceSize)SliceCount * 32u;   // GeometryArenaSlice is pinned at 32 B by its static_assert
    if (RequiredSliceBytes > SliceByteSize)
    {
        ReportInstanceBounds("slice count exceeds the slice table — refusing to bind");
        return false;
    }

    VkDevice Device = Bounds.Host->Device;

    // Bindings 0..2 are shared by both shaders and carry identical contents, so both sets are written here. Binding 3 differs in access only (the
    // reduce writes the accumulator, the Morton pass reads it), which is a shader-side qualifier, not a descriptor-side one.
    VkDescriptorSet Sets[2] = { Bounds.ReduceSet, Bounds.MortonSet };
    for (uint32_t SetIterator = 0; SetIterator < 2; ++SetIterator)
    {
        WriteStorageDescriptor(Device, Sets[SetIterator], 0, InstanceBuffer, InstanceByteSize);
        WriteStorageDescriptor(Device, Sets[SetIterator], 1, SliceBuffer,    SliceByteSize);
        WriteStorageDescriptor(Device, Sets[SetIterator], 2, NodeBuffer,     NodeByteSize);
        WriteStorageDescriptor(Device, Sets[SetIterator], 3, Bounds.BoundsBuffer,
                               (VkDeviceSize)InstanceBoundsElementCount * sizeof(int32_t));
    }

    Bounds.InstanceCount = InstanceCount;
    Bounds.SliceCount    = SliceCount;
    Bounds.SceneBound    = true;
    return true;
}

bool BindInstanceMortonTarget(InstanceBoundsSubmission& Bounds,
                              VkBuffer                  MortonKeyBuffer,
                              VkDeviceSize              MortonKeyByteSize,
                              VkBuffer                  MortonPayloadBuffer,
                              VkDeviceSize              MortonPayloadByteSize)
{
    if (!Bounds.ReadyCondition || Bounds.Host == nullptr)
        return false;
    if (MortonKeyBuffer == VK_NULL_HANDLE || MortonPayloadBuffer == VK_NULL_HANDLE)
    {
        ReportInstanceBounds("morton target bind given a null buffer");
        return false;
    }

    // 🔴 The Morton pass writes one key and one payload per instance at the instance's own index, so a target shorter than the instance count is a
    //    write past the end. Refused rather than truncated: a truncated emit leaves the tail of the key buffer holding the PREVIOUS frame's codes,
    //    which the sort then orders alongside this frame's as if they were live.
    const VkDeviceSize RequiredBytes = (VkDeviceSize)Bounds.InstanceCount * sizeof(uint32_t);
    if (Bounds.SceneBound && (RequiredBytes > MortonKeyByteSize || RequiredBytes > MortonPayloadByteSize))
    {
        ReportInstanceBounds("morton target smaller than the instance count — refusing to bind");
        return false;
    }

    VkDevice Device = Bounds.Host->Device;
    WriteStorageDescriptor(Device, Bounds.MortonSet, 4, MortonKeyBuffer,     MortonKeyByteSize);
    WriteStorageDescriptor(Device, Bounds.MortonSet, 5, MortonPayloadBuffer, MortonPayloadByteSize);

    // 🔴 RETAINED SO THE RECORD PATH CAN FENCE ITS OWN OUTPUT. These buffers belong to the radix sort, and the sort's tally dispatch reads them in a
    //    command buffer this submission never sees, so neither side can barrier the seam unless one of them remembers the handles. The writer is the
    //    side that must: it is the one that knows the write happened. Without this the tally reads partially-written Morton codes and the whole
    //    downstream tree is a well-formed structure over scrambled input — deterministic-looking, and wrong differently every run.
    Bounds.MortonKeyTarget     = MortonKeyBuffer;
    Bounds.MortonPayloadTarget = MortonPayloadBuffer;
    Bounds.MortonKeyBytes      = MortonKeyByteSize;
    Bounds.MortonPayloadBytes  = MortonPayloadByteSize;

    Bounds.MortonTargetBound = true;
    return true;
}

void RecordInstanceBoundsReduce(InstanceBoundsSubmission& Bounds, VkCommandBuffer CommandBuffer)
{
    if (!Bounds.ReadyCondition || !Bounds.SceneBound || Bounds.InstanceCount == 0 || CommandBuffer == VK_NULL_HANDLE)
        return;

    // ─── seed the accumulators ───────────────────────────────────────────────────────────────────────────────────────────────────────────────
    // 🔴 Two fills, not one: vkCmdFillBuffer writes a single repeated 32-bit pattern and the minima and maxima need OPPOSITE seeds. The minima
    //    occupy words 0..2 and the maxima words 3..5, so each half is one contiguous fill.
    const VkDeviceSize HalfBytes = (VkDeviceSize)(InstanceBoundsElementCount / 2u) * sizeof(int32_t);
    vkCmdFillBuffer(CommandBuffer, Bounds.BoundsBuffer, 0,         HalfBytes, RetrieveOrderedMinimumSeed());
    vkCmdFillBuffer(CommandBuffer, Bounds.BoundsBuffer, HalfBytes, HalfBytes, RetrieveOrderedMaximumSeed());

    // The fills must land before the shader's atomics read them, or the seed races the reduction.
    RecordBufferBarrier(CommandBuffer, Bounds.BoundsBuffer,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // ─── the reduce ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Bounds.ReducePipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Bounds.ReduceLayout, 0, 1, &Bounds.ReduceSet, 0, nullptr);

    InstanceBoundsConstants Constants;
    Constants.InstanceCount = Bounds.InstanceCount;
    Constants.SliceCount    = Bounds.SliceCount;
    vkCmdPushConstants(CommandBuffer, Bounds.ReduceLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants), &Constants);

    const uint32_t GroupCount = ComputeAccelerationGroupCount(Bounds.InstanceCount, InstanceBoundsLanesPerGroup);
    vkCmdDispatch(CommandBuffer, GroupCount, 1, 1);

    // The atomics must complete before anything reads the box — the Morton pass (compute) or a readback copy (transfer).
    RecordBufferBarrier(CommandBuffer, Bounds.BoundsBuffer,
                        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);
}

void RecordInstanceMortonCode(InstanceBoundsSubmission& Bounds, VkCommandBuffer CommandBuffer)
{
    if (!Bounds.ReadyCondition || !Bounds.SceneBound || !Bounds.MortonTargetBound ||
        Bounds.InstanceCount == 0 || CommandBuffer == VK_NULL_HANDLE)
        return;

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Bounds.MortonPipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Bounds.MortonLayout, 0, 1, &Bounds.MortonSet, 0, nullptr);

    InstanceBoundsConstants Constants;
    Constants.InstanceCount = Bounds.InstanceCount;
    Constants.SliceCount    = Bounds.SliceCount;
    vkCmdPushConstants(CommandBuffer, Bounds.MortonLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants), &Constants);

    const uint32_t GroupCount = ComputeAccelerationGroupCount(Bounds.InstanceCount, InstanceBoundsLanesPerGroup);
    vkCmdDispatch(CommandBuffer, GroupCount, 1, 1);

    // 🔴 THIS BARRIER IS THE ONE THING BETWEEN THE EMIT AND THE SORT, AND IT WAS MISSING. The very next dispatch the caller records is the radix
    //    sort's first digit tally, which reads every word this pass just wrote. Two compute dispatches in one command buffer are NOT ordered against
    //    each other by submission order — without an explicit dependency the tally is free to run concurrently and tally codes that are still in
    //    flight. That does not fail loudly: the sort produces a perfectly ordered permutation of whatever it happened to read, the Karras build
    //    produces a perfectly shaped tree over it, and the only symptom is that two runs of an identical scene disagree.
    //
    //    ⚠️ TRANSFER_READ is included deliberately. The keys are also the natural snapshot point for a diagnostic copy, and a barrier that covers
    //       only the compute consumer leaves that copy to be added — with its own barrier — by whoever needs it, which is how this gap opened.
    RecordBufferBarrier(CommandBuffer, Bounds.MortonKeyTarget,
                        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);

    RecordBufferBarrier(CommandBuffer, Bounds.MortonPayloadTarget,
                        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);
}

bool RetrieveInstanceBounds(InstanceBoundsSubmission& Bounds,
                            VkCommandPool             CommandPool,
                            InstanceBounds&           OutBounds)
{
    OutBounds = InstanceBounds();
    if (!Bounds.ReadyCondition || Bounds.Host == nullptr || CommandPool == VK_NULL_HANDLE)
        return false;

    VulkanHost& Host = *Bounds.Host;
    const VkDeviceSize BoundsBytes = (VkDeviceSize)InstanceBoundsElementCount * sizeof(int32_t);

    VkBuffer       StagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    if (!AllocateAccelerationBuffer(Host, BoundsBytes,
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    StagingBuffer, StagingMemory))
    {
        ReportInstanceBounds("readback staging allocation failed");
        return false;
    }

    const bool Copied = ExecuteAccelerationTransfer(Host, CommandPool, [&](VkCommandBuffer TransferCommand)
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
            int32_t Ordered[InstanceBoundsElementCount] = {};
            std::memcpy(Ordered, Mapped, (size_t)BoundsBytes);
            vkUnmapMemory(Host.Device, StagingMemory);

            // 🔴 Emptiness is detected in the ORDERED domain, before the float conversion, because the seeds are exactly the patterns that make an
            //    untouched box read inverted. Testing the raw accumulators keeps the empty case unambiguous.
            const bool Untouched = (Ordered[0] > Ordered[3]) || (Ordered[1] > Ordered[4]) || (Ordered[2] > Ordered[5]);

            OutBounds.MinimumX = OrderedIntegerBitsToFloat(Ordered[0]);
            OutBounds.MinimumY = OrderedIntegerBitsToFloat(Ordered[1]);
            OutBounds.MinimumZ = OrderedIntegerBitsToFloat(Ordered[2]);
            OutBounds.MaximumX = OrderedIntegerBitsToFloat(Ordered[3]);
            OutBounds.MaximumY = OrderedIntegerBitsToFloat(Ordered[4]);
            OutBounds.MaximumZ = OrderedIntegerBitsToFloat(Ordered[5]);
            OutBounds.EmptyCondition = Untouched;
            Succeeded = true;
        }
        else
        {
            ReportInstanceBounds("readback staging map failed");
        }
    }
    else
    {
        ReportInstanceBounds("readback transfer failed");
    }

    vkDestroyBuffer(Host.Device, StagingBuffer, Host.Allocator);
    vkFreeMemory(Host.Device, StagingMemory, Host.Allocator);
    return Succeeded;
}

VkBuffer RetrieveInstanceBoundsBuffer(const InstanceBoundsSubmission& Bounds)
{
    return Bounds.BoundsBuffer;
}

void FinalizeInstanceBoundsSubmission(InstanceBoundsSubmission& Bounds)
{
    if (Bounds.Host == nullptr || Bounds.Host->Device == VK_NULL_HANDLE)
    {
        Bounds = InstanceBoundsSubmission();
        return;
    }

    VulkanHost& Host = *Bounds.Host;

    if (Bounds.BoundsBuffer    != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, Bounds.BoundsBuffer, Host.Allocator);
    if (Bounds.BoundsMemory    != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Bounds.BoundsMemory, Host.Allocator);
    // The sets are freed with the pool; freeing them separately would need FREE_DESCRIPTOR_SET on the pool, which is not set.
    if (Bounds.DescriptorPool  != VK_NULL_HANDLE) vkDestroyDescriptorPool(Host.Device, Bounds.DescriptorPool, Host.Allocator);
    if (Bounds.MortonPipeline  != VK_NULL_HANDLE) vkDestroyPipeline(Host.Device, Bounds.MortonPipeline, Host.Allocator);
    if (Bounds.MortonLayout    != VK_NULL_HANDLE) vkDestroyPipelineLayout(Host.Device, Bounds.MortonLayout, Host.Allocator);
    if (Bounds.MortonSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Host.Device, Bounds.MortonSetLayout, Host.Allocator);
    if (Bounds.ReducePipeline  != VK_NULL_HANDLE) vkDestroyPipeline(Host.Device, Bounds.ReducePipeline, Host.Allocator);
    if (Bounds.ReduceLayout    != VK_NULL_HANDLE) vkDestroyPipelineLayout(Host.Device, Bounds.ReduceLayout, Host.Allocator);
    if (Bounds.ReduceSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Host.Device, Bounds.ReduceSetLayout, Host.Allocator);

    Bounds = InstanceBoundsSubmission();
}

} // namespace Frontier
