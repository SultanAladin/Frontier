/*==============================================================================================================================================
                                                        INSTANCETREESUBMISSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the TLAS build's radix-tree construction and bounds refit. Initialize reads both .spv files, builds a set layout + pipeline
//    layout + pipeline for each, a pool with two sets, and the node / parent / counter buffers sized for an instance capacity.
//    BindInstanceTreeSorted points the build pass at the sort's result; BindInstanceTreeScene points the refit at the instance array and arena. The
//    two record functions seed and dispatch. Raw Vulkan, no VMA, drawing its device helpers from AccelerationDeviceSupport.h.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Acceleration/InstanceTreeSubmission.h"
#include "Graphics/Acceleration/AccelerationDeviceSupport.h"
#include "Graphics/Acceleration/GeometryTreeBuild.h"

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

void ReportInstanceTree(const char* MessageText)
{
    std::fprintf(stderr, "[InstanceTree] %s\n", MessageText);
}

// 📝 Binding counts for the two sets. The build reads the sorted pair and writes nodes + parents; the refit reads the scene, the payloads and the
//    parents, and read-modify-writes the nodes and the counters.
constexpr uint32_t BuildBindingCount = 4;
constexpr uint32_t RefitBindingCount = 7;

// 🔴 A tree over N leaves has exactly N-1 internal nodes, so the node array is 2N-1 entries with internal nodes first. Derived in one place rather
//    than open-coded at every allocation and dispatch, because an off-by-one here is a buffer overrun the shader cannot detect.
uint32_t TotalNodeCountForLeaves(uint32_t LeafCount)
{
    return (LeafCount == 0u) ? 0u : (2u * LeafCount - 1u);
}

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

// Build a compute pipeline over ModuleName in ShaderDirectory, with SetLayout and a push range of PushByteSize.
bool ConstructComputePipeline(VulkanHost&            Host,
                              const char*            ShaderDirectory,
                              const char*            ModuleName,
                              VkDescriptorSetLayout  SetLayout,
                              uint32_t               PushByteSize,
                              VkPipelineLayout&      OutPipelineLayout,
                              VkPipeline&            OutPipeline)
{
    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = PushByteSize;

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
void WriteStorageDescriptor(VkDevice        Device,
                            VkDescriptorSet Set,
                            uint32_t        Binding,
                            VkBuffer        Buffer,
                            VkDeviceSize    ByteSize)
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

// The compute->compute / transfer->compute barrier both record paths use.
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

bool InitializeInstanceTreeSubmission(InstanceTreeSubmission& Tree,
                                      VulkanHost&             Host,
                                      uint32_t                MaxInstances,
                                      const char*             ShaderDirectory)
{
    Tree = InstanceTreeSubmission();
    Tree.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportInstanceTree("host has no device — refusing to build");
        return false;
    }
    if (ShaderDirectory == nullptr)
    {
        ReportInstanceTree("no shader directory supplied — refusing to build");
        return false;
    }
    if (MaxInstances == 0)
    {
        ReportInstanceTree("zero instance capacity — refusing to build");
        return false;
    }

    // ─── set layouts ───
    if (!ConstructStorageSetLayout(Host, BuildBindingCount, Tree.BuildSetLayout))
    {
        ReportInstanceTree("build descriptor set layout creation failed");
        FinalizeInstanceTreeSubmission(Tree);
        return false;
    }
    if (!ConstructStorageSetLayout(Host, RefitBindingCount, Tree.RefitSetLayout))
    {
        ReportInstanceTree("refit descriptor set layout creation failed");
        FinalizeInstanceTreeSubmission(Tree);
        return false;
    }

    // ─── pipelines ───
    if (!ConstructComputePipeline(Host, ShaderDirectory, "InstanceTreeBuild.comp.spv",
                                  Tree.BuildSetLayout, (uint32_t)sizeof(InstanceTreeConstants),
                                  Tree.BuildLayout, Tree.BuildPipeline))
    {
        ReportInstanceTree("InstanceTreeBuild pipeline creation failed (missing .spv or allocation failure)");
        FinalizeInstanceTreeSubmission(Tree);
        return false;
    }
    if (!ConstructComputePipeline(Host, ShaderDirectory, "InstanceTreeRefit.comp.spv",
                                  Tree.RefitSetLayout, (uint32_t)sizeof(InstanceTreeRefitConstants),
                                  Tree.RefitLayout, Tree.RefitPipeline))
    {
        ReportInstanceTree("InstanceTreeRefit pipeline creation failed (missing .spv or allocation failure)");
        FinalizeInstanceTreeSubmission(Tree);
        return false;
    }

    // ─── descriptor pool + the two sets ───
    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = BuildBindingCount + RefitBindingCount;

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 2;
    PoolInformation.poolSizeCount = 1;
    PoolInformation.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Tree.DescriptorPool) != VK_SUCCESS)
    {
        ReportInstanceTree("descriptor pool creation failed");
        FinalizeInstanceTreeSubmission(Tree);
        return false;
    }

    VkDescriptorSetAllocateInfo BuildAllocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    BuildAllocate.descriptorPool     = Tree.DescriptorPool;
    BuildAllocate.descriptorSetCount = 1;
    BuildAllocate.pSetLayouts        = &Tree.BuildSetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &BuildAllocate, &Tree.BuildSet) != VK_SUCCESS)
    {
        ReportInstanceTree("build descriptor set allocation failed");
        FinalizeInstanceTreeSubmission(Tree);
        return false;
    }

    VkDescriptorSetAllocateInfo RefitAllocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    RefitAllocate.descriptorPool     = Tree.DescriptorPool;
    RefitAllocate.descriptorSetCount = 1;
    RefitAllocate.pSetLayouts        = &Tree.RefitSetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &RefitAllocate, &Tree.RefitSet) != VK_SUCCESS)
    {
        ReportInstanceTree("refit descriptor set allocation failed");
        FinalizeInstanceTreeSubmission(Tree);
        return false;
    }

    // ─── the tree buffers ───
    // 📝 Sized once for the capacity and reused every frame; a per-frame rebuild must not allocate. TRANSFER_DST carries the parent and counter
    //    seeding fills, TRANSFER_SRC the gate's readback.
    const uint32_t     TotalNodes  = TotalNodeCountForLeaves(MaxInstances);
    const VkDeviceSize NodeBytes   = (VkDeviceSize)TotalNodes * InstanceTreeWordsPerNode * sizeof(uint32_t);
    const VkDeviceSize ParentBytes = (VkDeviceSize)TotalNodes * sizeof(uint32_t);
    const VkDeviceSize CounterBytes= (VkDeviceSize)MaxInstances * sizeof(uint32_t);   // one per internal node; MaxInstances-1 rounded up to leaves

    const VkBufferUsageFlags TreeUsage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (!AllocateAccelerationBuffer(Host, NodeBytes, TreeUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    Tree.NodeBuffer, Tree.NodeMemory))
    {
        ReportInstanceTree("tree node buffer allocation failed");
        FinalizeInstanceTreeSubmission(Tree);
        return false;
    }
    if (!AllocateAccelerationBuffer(Host, ParentBytes, TreeUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    Tree.ParentBuffer, Tree.ParentMemory))
    {
        ReportInstanceTree("tree parent buffer allocation failed");
        FinalizeInstanceTreeSubmission(Tree);
        return false;
    }
    if (!AllocateAccelerationBuffer(Host, CounterBytes, TreeUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    Tree.CounterBuffer, Tree.CounterMemory))
    {
        ReportInstanceTree("tree counter buffer allocation failed");
        FinalizeInstanceTreeSubmission(Tree);
        return false;
    }

    Tree.InstanceCapacity = MaxInstances;
    Tree.ReadyCondition   = true;
    return true;
}

bool BindInstanceTreeSorted(InstanceTreeSubmission& Tree,
                            VkBuffer                SortedKeyBuffer,
                            VkDeviceSize            SortedKeyByteSize,
                            VkBuffer                SortedPayloadBuffer,
                            VkDeviceSize            SortedPayloadByteSize,
                            uint32_t                InstanceCount)
{
    if (!Tree.ReadyCondition || Tree.Host == nullptr)
        return false;
    if (SortedKeyBuffer == VK_NULL_HANDLE || SortedPayloadBuffer == VK_NULL_HANDLE)
    {
        ReportInstanceTree("sorted bind given a null buffer");
        return false;
    }

    // 🔴 Refused rather than clamped. A capacity overrun writes nodes past the end of a buffer sized at Initialize, and on this device that is
    //    either a silent stomp on whatever follows or a device loss — never a clean truncation the caller could reason about.
    if (InstanceCount > Tree.InstanceCapacity)
    {
        ReportInstanceTree("instance count exceeds the tree capacity — refusing to bind");
        return false;
    }

    const VkDeviceSize RequiredBytes = (VkDeviceSize)InstanceCount * sizeof(uint32_t);
    if (RequiredBytes > SortedKeyByteSize || RequiredBytes > SortedPayloadByteSize)
    {
        ReportInstanceTree("sorted key/payload buffer smaller than the instance count — refusing to bind");
        return false;
    }

    VkDevice Device = Tree.Host->Device;

    const uint32_t     TotalNodes  = TotalNodeCountForLeaves(InstanceCount);
    const VkDeviceSize NodeBytes   = (VkDeviceSize)TotalNodes * InstanceTreeWordsPerNode * sizeof(uint32_t);
    const VkDeviceSize ParentBytes = (VkDeviceSize)TotalNodes * sizeof(uint32_t);

    WriteStorageDescriptor(Device, Tree.BuildSet, 0, SortedKeyBuffer,     SortedKeyByteSize);
    WriteStorageDescriptor(Device, Tree.BuildSet, 1, SortedPayloadBuffer, SortedPayloadByteSize);
    WriteStorageDescriptor(Device, Tree.BuildSet, 2, Tree.NodeBuffer,     NodeBytes);
    WriteStorageDescriptor(Device, Tree.BuildSet, 3, Tree.ParentBuffer,   ParentBytes);

    Tree.InstanceCount = InstanceCount;
    Tree.SortedBound   = true;
    return true;
}

bool BindInstanceTreeScene(InstanceTreeSubmission& Tree,
                           VkBuffer                InstanceBuffer,
                           VkDeviceSize            InstanceByteSize,
                           VkBuffer                SliceBuffer,
                           VkDeviceSize            SliceByteSize,
                           VkBuffer                ArenaNodeBuffer,
                           VkDeviceSize            ArenaNodeByteSize,
                           VkBuffer                SortedPayloadBuffer,
                           VkDeviceSize            SortedPayloadByteSize,
                           uint32_t                SliceCount)
{
    if (!Tree.ReadyCondition || Tree.Host == nullptr)
        return false;
    if (InstanceBuffer == VK_NULL_HANDLE || SliceBuffer == VK_NULL_HANDLE ||
        ArenaNodeBuffer == VK_NULL_HANDLE || SortedPayloadBuffer == VK_NULL_HANDLE)
    {
        ReportInstanceTree("scene bind given a null buffer");
        return false;
    }

    // 🔴 Same refusal the bounds pass makes, for the same reason: an out-of-bounds storage read returns zeros on this device rather than faulting,
    //    so an oversized slice count silently makes every stray instance borrow mesh zero's box.
    const VkDeviceSize RequiredSliceBytes = (VkDeviceSize)SliceCount * 32u;   // GeometryArenaSlice is pinned at 32 B
    if (RequiredSliceBytes > SliceByteSize)
    {
        ReportInstanceTree("slice count exceeds the slice table — refusing to bind");
        return false;
    }

    VkDevice Device = Tree.Host->Device;

    const uint32_t     TotalNodes   = TotalNodeCountForLeaves(Tree.InstanceCount);
    const VkDeviceSize NodeBytes    = (VkDeviceSize)TotalNodes * InstanceTreeWordsPerNode * sizeof(uint32_t);
    const VkDeviceSize ParentBytes  = (VkDeviceSize)TotalNodes * sizeof(uint32_t);
    const VkDeviceSize CounterBytes = (VkDeviceSize)Tree.InstanceCapacity * sizeof(uint32_t);

    WriteStorageDescriptor(Device, Tree.RefitSet, 0, InstanceBuffer,      InstanceByteSize);
    WriteStorageDescriptor(Device, Tree.RefitSet, 1, SliceBuffer,         SliceByteSize);
    WriteStorageDescriptor(Device, Tree.RefitSet, 2, ArenaNodeBuffer,     ArenaNodeByteSize);
    WriteStorageDescriptor(Device, Tree.RefitSet, 3, SortedPayloadBuffer, SortedPayloadByteSize);
    WriteStorageDescriptor(Device, Tree.RefitSet, 4, Tree.NodeBuffer,     NodeBytes);
    WriteStorageDescriptor(Device, Tree.RefitSet, 5, Tree.ParentBuffer,   ParentBytes);
    WriteStorageDescriptor(Device, Tree.RefitSet, 6, Tree.CounterBuffer,  CounterBytes);

    Tree.SliceCount = SliceCount;
    Tree.SceneBound = true;
    return true;
}

void RecordInstanceTreeBuild(InstanceTreeSubmission& Tree, VkCommandBuffer CommandBuffer)
{
    if (!Tree.ReadyCondition || !Tree.SortedBound || CommandBuffer == VK_NULL_HANDLE)
        return;

    // 📝 A tree over fewer than two leaves has no internal nodes: the lone leaf IS the tree, and the refit writes it. Nothing to build here.
    if (Tree.InstanceCount < 2u)
        return;

    const uint32_t     TotalNodes  = TotalNodeCountForLeaves(Tree.InstanceCount);
    const VkDeviceSize ParentBytes = (VkDeviceSize)TotalNodes * sizeof(uint32_t);

    // ─── seed the parent table ───────────────────────────────────────────────────────────────────────────────────────────────────────────────
    // 🔴 The root is the node NOBODY claims, so it is identified by still holding this sentinel after the build. Skipping the seed leaves the
    //    previous frame's parents in place, and the refit's climb then follows a stale path — through nodes that exist, to a root that is not the
    //    root, terminating cleanly on a tree it has only partly bounded.
    vkCmdFillBuffer(CommandBuffer, Tree.ParentBuffer, 0, ParentBytes, GeometryTreeNoParent);

    RecordBufferBarrier(CommandBuffer, Tree.ParentBuffer,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // ─── the build ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Tree.BuildPipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Tree.BuildLayout, 0, 1, &Tree.BuildSet, 0, nullptr);

    InstanceTreeConstants Constants;
    Constants.InstanceCount = Tree.InstanceCount;
    vkCmdPushConstants(CommandBuffer, Tree.BuildLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants), &Constants);

    // One lane per INTERNAL node, of which there are exactly InstanceCount - 1.
    const uint32_t GroupCount = ComputeAccelerationGroupCount(Tree.InstanceCount - 1u, InstanceTreeLanesPerGroup);
    vkCmdDispatch(CommandBuffer, GroupCount, 1, 1);

    // The shape must be complete before the refit reads children or climbs parents.
    RecordBufferBarrier(CommandBuffer, Tree.NodeBuffer,
                        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    RecordBufferBarrier(CommandBuffer, Tree.ParentBuffer,
                        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void RecordInstanceTreeRefit(InstanceTreeSubmission& Tree, VkCommandBuffer CommandBuffer)
{
    if (!Tree.ReadyCondition || !Tree.SortedBound || !Tree.SceneBound ||
        Tree.InstanceCount == 0 || CommandBuffer == VK_NULL_HANDLE)
        return;

    // ─── zero the arrival counters ───────────────────────────────────────────────────────────────────────────────────────────────────────────
    // 🔴 Every rebuild, without exception — see the file header. A counter left at its previous value makes the first lane to reach a node believe
    //    it is the second, so it climbs on a box whose sibling subtree has not landed.
    const VkDeviceSize CounterBytes = (VkDeviceSize)Tree.InstanceCapacity * sizeof(uint32_t);
    vkCmdFillBuffer(CommandBuffer, Tree.CounterBuffer, 0, CounterBytes, 0u);

    RecordBufferBarrier(CommandBuffer, Tree.CounterBuffer,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // ─── the refit ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Tree.RefitPipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Tree.RefitLayout, 0, 1, &Tree.RefitSet, 0, nullptr);

    InstanceTreeRefitConstants Constants;
    Constants.InstanceCount    = Tree.InstanceCount;
    Constants.SliceCount       = Tree.SliceCount;
    Constants.NoParentSentinel = GeometryTreeNoParent;
    vkCmdPushConstants(CommandBuffer, Tree.RefitLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants), &Constants);

    // One lane per LEAF.
    const uint32_t GroupCount = ComputeAccelerationGroupCount(Tree.InstanceCount, InstanceTreeLanesPerGroup);
    vkCmdDispatch(CommandBuffer, GroupCount, 1, 1);

    // The boxes must land before anything descends the tree — the trace (compute) or a readback copy (transfer).
    RecordBufferBarrier(CommandBuffer, Tree.NodeBuffer,
                        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);
}

void RetrieveInstanceTreeBuffers(const InstanceTreeSubmission& Tree, VkBuffer& OutNodeBuffer, VkBuffer& OutParentBuffer)
{
    OutNodeBuffer   = Tree.NodeBuffer;
    OutParentBuffer = Tree.ParentBuffer;
}

bool RetrieveInstanceTreeReadback(InstanceTreeSubmission& Tree,
                                  VkCommandPool           CommandPool,
                                  uint32_t                InstanceCount,
                                  uint32_t*               OutNodeWords,
                                  uint32_t*               OutParents)
{
    if (!Tree.ReadyCondition || Tree.Host == nullptr || CommandPool == VK_NULL_HANDLE)
        return false;
    if (InstanceCount == 0 || InstanceCount > Tree.InstanceCapacity)
        return false;

    VulkanHost& Host = *Tree.Host;

    const uint32_t     TotalNodes  = TotalNodeCountForLeaves(InstanceCount);
    const VkDeviceSize NodeBytes   = (VkDeviceSize)TotalNodes * InstanceTreeWordsPerNode * sizeof(uint32_t);
    const VkDeviceSize ParentBytes = (VkDeviceSize)TotalNodes * sizeof(uint32_t);
    const VkDeviceSize TotalBytes  = NodeBytes + ParentBytes;

    VkBuffer       StagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    if (!AllocateAccelerationBuffer(Host, TotalBytes,
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    StagingBuffer, StagingMemory))
    {
        ReportInstanceTree("readback staging allocation failed");
        return false;
    }

    // Both buffers into one staging allocation, nodes first then parents, so the readback is a single blocking submit.
    const bool Copied = ExecuteAccelerationTransfer(Host, CommandPool, [&](VkCommandBuffer TransferCommand)
    {
        VkBufferCopy NodeRegion = {};
        NodeRegion.srcOffset = 0;
        NodeRegion.dstOffset = 0;
        NodeRegion.size      = NodeBytes;
        vkCmdCopyBuffer(TransferCommand, Tree.NodeBuffer, StagingBuffer, 1, &NodeRegion);

        VkBufferCopy ParentRegion = {};
        ParentRegion.srcOffset = 0;
        ParentRegion.dstOffset = NodeBytes;
        ParentRegion.size      = ParentBytes;
        vkCmdCopyBuffer(TransferCommand, Tree.ParentBuffer, StagingBuffer, 1, &ParentRegion);
    });

    bool Succeeded = false;
    if (Copied)
    {
        void* Mapped = nullptr;
        if (vkMapMemory(Host.Device, StagingMemory, 0, TotalBytes, 0, &Mapped) == VK_SUCCESS && Mapped != nullptr)
        {
            const unsigned char* Bytes = static_cast<const unsigned char*>(Mapped);
            if (OutNodeWords != nullptr)
                std::memcpy(OutNodeWords, Bytes, (size_t)NodeBytes);
            if (OutParents != nullptr)
                std::memcpy(OutParents, Bytes + (size_t)NodeBytes, (size_t)ParentBytes);
            vkUnmapMemory(Host.Device, StagingMemory);
            Succeeded = true;
        }
        else
        {
            ReportInstanceTree("readback staging map failed");
        }
    }
    else
    {
        ReportInstanceTree("readback transfer failed");
    }

    vkDestroyBuffer(Host.Device, StagingBuffer, Host.Allocator);
    vkFreeMemory(Host.Device, StagingMemory, Host.Allocator);
    return Succeeded;
}

void FinalizeInstanceTreeSubmission(InstanceTreeSubmission& Tree)
{
    if (Tree.Host == nullptr || Tree.Host->Device == VK_NULL_HANDLE)
    {
        Tree = InstanceTreeSubmission();
        return;
    }

    VulkanHost& Host = *Tree.Host;

    if (Tree.CounterBuffer  != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, Tree.CounterBuffer, Host.Allocator);
    if (Tree.CounterMemory  != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Tree.CounterMemory, Host.Allocator);
    if (Tree.ParentBuffer   != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, Tree.ParentBuffer, Host.Allocator);
    if (Tree.ParentMemory   != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Tree.ParentMemory, Host.Allocator);
    if (Tree.NodeBuffer     != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, Tree.NodeBuffer, Host.Allocator);
    if (Tree.NodeMemory     != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Tree.NodeMemory, Host.Allocator);
    // The sets are freed with the pool; freeing them separately would need FREE_DESCRIPTOR_SET on the pool, which is not set.
    if (Tree.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Host.Device, Tree.DescriptorPool, Host.Allocator);
    if (Tree.RefitPipeline  != VK_NULL_HANDLE) vkDestroyPipeline(Host.Device, Tree.RefitPipeline, Host.Allocator);
    if (Tree.RefitLayout    != VK_NULL_HANDLE) vkDestroyPipelineLayout(Host.Device, Tree.RefitLayout, Host.Allocator);
    if (Tree.RefitSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Host.Device, Tree.RefitSetLayout, Host.Allocator);
    if (Tree.BuildPipeline  != VK_NULL_HANDLE) vkDestroyPipeline(Host.Device, Tree.BuildPipeline, Host.Allocator);
    if (Tree.BuildLayout    != VK_NULL_HANDLE) vkDestroyPipelineLayout(Host.Device, Tree.BuildLayout, Host.Allocator);
    if (Tree.BuildSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Host.Device, Tree.BuildSetLayout, Host.Allocator);

    Tree = InstanceTreeSubmission();
}

} // namespace Frontier
