/*==============================================================================================================================================
                                                       SURFELRADIANCESUBMISSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the single-dispatch radiance trace. Initialize builds two set layouts — the store's twelve bindings (ordinals mirroring
//    Shaders/SurfelStoreBindings.glsl) and the scene's seven (ordinals mirroring SurfelRadianceTrace.comp's set 1) — a pipeline layout carrying the
//    88-byte push range, one descriptor pool holding both sets, and the one compute pipeline. Record binds both sets, pushes once and dispatches.
//    Raw Vulkan, no VMA, mirroring the SurfelLifecycleSubmission / SurfelStore idiom.
//
// 🔴 THE TWO SET LAYOUTS ARE BUILT SEPARATELY EVEN THOUGH BOTH ARE ALL-STORAGE-BUFFER RUNS. They differ in size (12 against 7) and a pipeline layout
//    names its sets positionally, so set 0 must be the twelve-binding layout and set 1 the seven-binding one — passing the same layout twice, or
//    them in the other order, links cleanly and binds the store's records where the shader expects instances. ⚠️ The failure is not a validation
//    error on every driver; it is a field of nonsense.
//
// 📝 The descriptor pool sizes ONE storage-buffer pool entry covering both sets (12 + 7 = 19 descriptors, maxSets 2). Vulkan pools count descriptors
//    by type across the whole pool, not per set, so a single VkDescriptorPoolSize with the summed count is the correct spelling — not two entries of
//    the same type.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Surfel/SurfelRadianceSubmission.h"

#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 Must match `layout(local_size_x = 64)` in SurfelRadianceTrace.comp. The dispatch covers the whole ray budget; the shader early-outs against the
//    counter (see the header's note on why the claim is not knowable here).
constexpr uint32_t RadianceGroupWidth = 64u;

// 🔴 SET 0'S BINDING ORDINALS, IN THE ORDER Shaders/SurfelStoreBindings.glsl DECLARES THEM — the same table SurfelLifecycleSubmission.cpp carries,
//    repeated rather than shared because the two units must be able to disagree loudly at review rather than quietly at runtime if either shader's
//    set ever diverges. A mismatch does NOT fail validation: both sides declare a storage buffer at every index, so it simply hands the pass the
//    wrong buffer. ⚠️ Adding a binding means editing the .glsl and BOTH .cpp tables in one edit.
enum RadianceStoreBinding : uint32_t
{
    BindingSurfelRecords  =  0u,
    BindingLiveIndex      =  1u,
    BindingPendingIndex   =  2u,
    BindingVacancyTable   =  3u,
    BindingCellSpan       =  4u,
    BindingCellList       =  5u,
    BindingReservation    =  6u,
    BindingReferenceTally =  7u,
    BindingRecycleRecords =  8u,
    BindingRayOutcomes    =  9u,
    BindingCounter        = 10u,
    BindingHitLocator     = 11u,
};

// 🔴 SET 1'S BINDING ORDINALS, IN THE ORDER SurfelRadianceTrace.comp DECLARES THEM (its set-1 block, and TwoLevelTraceProbe.comp's first seven at
//    set 0 — the same shape, one set along). The trace walks the arena through 1..3 and the tree through 4; swapping a node stream for a primitive
//    stream produces a walk that terminates and returns hits, which is why this list is spelled out rather than inferred from the struct's order.
enum RadianceSceneBinding : uint32_t
{
    BindingInstances       = 0u,
    BindingSlices          = 1u,
    BindingArenaNodes      = 2u,
    BindingArenaPrimitives = 3u,
    BindingTreeNodes       = 4u,
    BindingMeshIndices     = 5u,
    BindingVertices        = 6u,
};

constexpr uint32_t RadianceStoreBindingCount = 12u;
constexpr uint32_t RadianceSceneBindingCount =  7u;

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ReportRadiance(const char* MessageText)
{
    std::fprintf(stderr, "[SurfelRadianceSubmission] %s\n", MessageText);
}

std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
{
    std::vector<char> Bytes;
    FILE* Handle = std::fopen(FilePath.c_str(), "rb");
    if (Handle == nullptr)
        return Bytes;
    std::fseek(Handle, 0, SEEK_END);
    const long Size = std::ftell(Handle);
    std::fseek(Handle, 0, SEEK_SET);
    if (Size > 0)
    {
        Bytes.resize((size_t)Size);
        if (std::fread(Bytes.data(), 1, (size_t)Size, Handle) != (size_t)Size)
            Bytes.clear();
    }
    std::fclose(Handle);
    return Bytes;
}

VkPipeline ConstructComputePipeline(VulkanHost& Host, VkPipelineLayout Layout, const std::string& Directory, const char* FileName)
{
    const std::vector<char> Bytes = RetrieveShaderBytes(Directory + "/" + FileName);
    if (Bytes.empty() || (Bytes.size() % 4) != 0)
    {
        ReportRadiance("shader module unavailable or not word-aligned");
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo ModuleInformation = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ModuleInformation.codeSize = Bytes.size();
    ModuleInformation.pCode    = reinterpret_cast<const uint32_t*>(Bytes.data());
    VkShaderModule Module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(Host.Device, &ModuleInformation, Host.Allocator, &Module) != VK_SUCCESS)
        return VK_NULL_HANDLE;

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

// One all-storage-buffer, all-compute set layout of BindingCount consecutive bindings starting at zero. Both of this unit's sets have that shape,
// so the two differ only in their count.
bool ConstructStorageSetLayout(VulkanHost& Host, uint32_t BindingCount, VkDescriptorSetLayout& Result)
{
    std::vector<VkDescriptorSetLayoutBinding> Bindings((size_t)BindingCount);
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
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Result) != VK_SUCCESS)
    {
        Result = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Both set layouts and the pipeline layout that names them positionally, plus the push range.
bool ConstructRadianceLayout(SurfelRadianceSubmission& Submission)
{
    VulkanHost& Host = *Submission.Host;

    if (!ConstructStorageSetLayout(Host, RadianceStoreBindingCount, Submission.StoreSetLayout))
        return false;
    if (!ConstructStorageSetLayout(Host, RadianceSceneBindingCount, Submission.SceneSetLayout))
        return false;

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(SurfelRadianceConstants);

    // 🔴 THE ORDER OF THIS ARRAY IS THE SET NUMBERING. Index 0 is the shader's `set = 0` and index 1 its `set = 1`; the store layout must come first.
    const VkDescriptorSetLayout OrderedLayouts[2] = { Submission.StoreSetLayout, Submission.SceneSetLayout };

    VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipelineLayoutInformation.setLayoutCount         = 2;
    PipelineLayoutInformation.pSetLayouts            = OrderedLayouts;
    PipelineLayoutInformation.pushConstantRangeCount = 1;
    PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &PipelineLayoutInformation, Host.Allocator, &Submission.PipelineLayout) != VK_SUCCESS)
    {
        Submission.PipelineLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// One pool holding both sets. 📝 The pool's descriptor count is the SUM across both sets (12 + 7), because a pool budgets by type over the whole
// pool rather than per set.
bool ConstructRadianceDescriptors(SurfelRadianceSubmission& Submission)
{
    VulkanHost& Host = *Submission.Host;

    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = RadianceStoreBindingCount + RadianceSceneBindingCount;

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 2;
    PoolInformation.poolSizeCount = 1;
    PoolInformation.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Submission.DescriptorPool) != VK_SUCCESS)
    {
        Submission.DescriptorPool = VK_NULL_HANDLE;
        return false;
    }

    // Allocated in one call so the two sets come back in the same order as the layouts handed in — which is also the order they are bound in.
    const VkDescriptorSetLayout OrderedLayouts[2] = { Submission.StoreSetLayout, Submission.SceneSetLayout };
    VkDescriptorSet             AllocatedSets[2]  = { VK_NULL_HANDLE, VK_NULL_HANDLE };

    VkDescriptorSetAllocateInfo SetAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocation.descriptorPool     = Submission.DescriptorPool;
    SetAllocation.descriptorSetCount = 2;
    SetAllocation.pSetLayouts        = OrderedLayouts;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, AllocatedSets) != VK_SUCCESS)
    {
        Submission.StoreSet = VK_NULL_HANDLE;
        Submission.SceneSet = VK_NULL_HANDLE;
        return false;
    }

    Submission.StoreSet = AllocatedSets[0];
    Submission.SceneSet = AllocatedSets[1];
    return true;
}

// Write a run of consecutive storage-buffer bindings from an ordered handle array. 🔴 Index i is written to binding i — the array's order IS the
// binding order — and every binding in the set is written in ONE call, because a set with even one binding left unwritten is undefined to dispatch
// against.
bool WriteStorageDescriptors(VkDevice Device, VkDescriptorSet Set, const VkBuffer* OrderedBuffers, uint32_t BindingCount)
{
    std::vector<VkDescriptorBufferInfo> BufferSpans((size_t)BindingCount);
    std::vector<VkWriteDescriptorSet>   Writes((size_t)BindingCount);

    for (uint32_t Index = 0; Index < BindingCount; ++Index)
    {
        if (OrderedBuffers[Index] == VK_NULL_HANDLE)
        {
            ReportRadiance("a null buffer was handed in; descriptors not written");
            return false;
        }

        BufferSpans[Index] = {};
        BufferSpans[Index].buffer = OrderedBuffers[Index];
        BufferSpans[Index].offset = 0;
        BufferSpans[Index].range  = VK_WHOLE_SIZE;

        Writes[Index] = {};
        Writes[Index].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet          = Set;
        Writes[Index].dstBinding      = Index;
        Writes[Index].descriptorCount = 1;
        Writes[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[Index].pBufferInfo     = &BufferSpans[Index];
    }

    vkUpdateDescriptorSets(Device, BindingCount, Writes.data(), 0, nullptr);
    return true;
}

// Point set 0 at the store. The array's order mirrors the RadianceStoreBinding table above, which mirrors SurfelStoreBindings.glsl.
bool WriteRadianceStoreDescriptors(SurfelRadianceSubmission& Submission, const SurfelStore& Store)
{
    const VkBuffer OrderedBuffers[RadianceStoreBindingCount] =
    {
        Store.SurfelRecords.Buffer,      // 0
        Store.LiveIndex.Buffer,          // 1
        Store.PendingIndex.Buffer,       // 2
        Store.VacancyTable.Buffer,       // 3
        Store.CellSpan.Buffer,           // 4
        Store.CellList.Buffer,           // 5
        Store.Reservation.Buffer,        // 6
        Store.ReferenceTally.Buffer,     // 7
        Store.RecycleRecords.Buffer,     // 8
        Store.RayOutcomes.Buffer,        // 9
        Store.Counter.Buffer,            // 10
        Store.HitLocator.Buffer,         // 11
    };

    if (!WriteStorageDescriptors(Submission.Host->Device, Submission.StoreSet, OrderedBuffers, RadianceStoreBindingCount))
        return false;

    Submission.BoundRecordBuffer = Store.SurfelRecords.Buffer;
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSurfelRadianceSubmission(SurfelRadianceSubmission& Submission,
                                        VulkanHost&               Host,
                                        const SurfelStore&        Store,
                                        const char*               ShaderDirectory)
{
    Submission = SurfelRadianceSubmission{};
    Submission.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportRadiance("no device — trace not built");
        Submission = SurfelRadianceSubmission{};
        return false;
    }
    if (!Store.ReadyCondition)
    {
        ReportRadiance("store not ready — trace not built");
        Submission = SurfelRadianceSubmission{};
        return false;
    }
    if (ShaderDirectory == nullptr)
    {
        ReportRadiance("no shader directory — trace not built");
        Submission = SurfelRadianceSubmission{};
        return false;
    }

    if (!ConstructRadianceLayout(Submission))
    {
        ReportRadiance("set / pipeline layout creation failed");
        FinalizeSurfelRadianceSubmission(Submission);
        return false;
    }
    if (!ConstructRadianceDescriptors(Submission))
    {
        ReportRadiance("descriptor pool / set creation failed");
        FinalizeSurfelRadianceSubmission(Submission);
        return false;
    }
    if (!WriteRadianceStoreDescriptors(Submission, Store))
    {
        ReportRadiance("store descriptor write failed");
        FinalizeSurfelRadianceSubmission(Submission);
        return false;
    }

    // ⚠️ Set 1 is deliberately NOT written here. The acceleration units publish their buffers after the renderer builds its pipelines, so demanding
    //    them at this point would order the two the wrong way round; RefreshSurfelRadianceSceneBinding fills it and raises SceneCondition, and the
    //    record is a no-op until that has happened once.

    const std::string Directory = ShaderDirectory;
    Submission.TracePipeline = ConstructComputePipeline(Host, Submission.PipelineLayout, Directory, "SurfelRadianceTrace.comp.spv");
    if (Submission.TracePipeline == VK_NULL_HANDLE)
    {
        ReportRadiance("the trace pipeline failed to build");
        FinalizeSurfelRadianceSubmission(Submission);
        return false;
    }

    // 📝 The budget, not the store's surfel capacity: the dispatch covers the RAY pool (SurfelRayBudget entries), one invocation per ray, and the
    //    outcome buffer is what bounds it. Taken from the buffer's own length so a store built at a different budget cannot disagree with the header.
    Submission.RayBudget = (uint32_t)(Store.RayOutcomes.ByteLength / sizeof(SurfelRayOutcome));
    if (Submission.RayBudget == 0u)
    {
        ReportRadiance("the store's ray-outcome pool is empty — trace not built");
        FinalizeSurfelRadianceSubmission(Submission);
        return false;
    }

    Submission.ReadyCondition = true;
    return true;
}

bool RefreshSurfelRadianceSceneBinding(SurfelRadianceSubmission& Submission, const SurfelSceneBinding& Binding)
{
    if (Submission.Host == nullptr || Submission.SceneSet == VK_NULL_HANDLE)
        return false;

    // The array's order mirrors the RadianceSceneBinding table, which mirrors SurfelRadianceTrace.comp's set 1.
    const VkBuffer OrderedBuffers[RadianceSceneBindingCount] =
    {
        Binding.Instances,        // 0
        Binding.Slices,           // 1
        Binding.ArenaNodes,       // 2
        Binding.ArenaPrimitives,  // 3
        Binding.TreeNodes,        // 4
        Binding.MeshIndices,      // 5
        Binding.Vertices,         // 6
    };

    if (!WriteStorageDescriptors(Submission.Host->Device, Submission.SceneSet, OrderedBuffers, RadianceSceneBindingCount))
        return false;

    Submission.BoundVertexBuffer = Binding.Vertices;
    Submission.SceneCondition    = true;
    return true;
}

void RecordSurfelRadianceTrace(SurfelRadianceSubmission&      Submission,
                               const SurfelStore&             Store,
                               const SurfelRadianceConstants& Constants,
                               VkCommandBuffer                CommandBuffer)
{
    if (!Submission.ReadyCondition || Submission.Host == nullptr || CommandBuffer == VK_NULL_HANDLE)
        return;
    if (!Submission.SceneCondition)
        return;   // the scene has never published; there is nothing to trace against
    if (!Store.ReadyCondition)
        return;

    // A rebuilt store hands out fresh handles; re-point set 0 rather than dispatching against freed memory. In the steady state this never fires,
    // because the store allocates once and stays resident.
    if (Store.SurfelRecords.Buffer != Submission.BoundRecordBuffer)
    {
        if (!WriteRadianceStoreDescriptors(Submission, Store))
            return;
        Submission.RayBudget = (uint32_t)(Store.RayOutcomes.ByteLength / sizeof(SurfelRayOutcome));
        if (Submission.RayBudget == 0u)
            return;
    }

    // 🔴 BOTH SETS ARE BOUND IN ONE CALL, AT FIRST SET 0. The shader reads the store through set 0 and the scene through set 1, and a pipeline layout
    //    names its sets positionally — binding them in two calls would work equally, but one call makes the pairing impossible to get half-right.
    const VkDescriptorSet OrderedSets[2] = { Submission.StoreSet, Submission.SceneSet };
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Submission.PipelineLayout, 0, 2, OrderedSets, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Submission.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SurfelRadianceConstants), &Constants);

    // ⚠️ THE CONSTANTS ARE PUSHED AS THE CALLER GAVE THEM, INCLUDING InstanceCount AND SliceCount. Unlike the lifecycle's CellCount — which this unit
    //    can correct from the store it owns a view of — the scene's counts describe buffers this unit only borrows and never sized, so there is
    //    nothing here to check them against. The caller that wrote set 1 is the same caller that knows the counts; they must move together.

    // 🔴 THE DISPATCH COVERS THE WHOLE RAY BUDGET, NOT THE FRAME'S CLAIM. See the header. The shader's first act is to read the counter and return
    //    for every index past it, so the excess groups cost one buffer read each.
    const uint32_t RayGroups = (Submission.RayBudget + RadianceGroupWidth - 1u) / RadianceGroupWidth;
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Submission.TracePipeline);
    vkCmdDispatch(CommandBuffer, RayGroups, 1, 1);

    // ⚠️ No trailing barrier. The integrate reads the outcomes this wrote, and the fence that makes them visible belongs to it — the consumer knows
    //    its own destination stage, this unit does not. Same contract as RecordSurfelLifecycle.
}

void FinalizeSurfelRadianceSubmission(SurfelRadianceSubmission& Submission)
{
    if (Submission.Host == nullptr || Submission.Host->Device == VK_NULL_HANDLE)
    {
        Submission = SurfelRadianceSubmission{};
        return;
    }

    VkDevice                     Device    = Submission.Host->Device;
    const VkAllocationCallbacks* Allocator = Submission.Host->Allocator;

    if (Submission.TracePipeline  != VK_NULL_HANDLE) vkDestroyPipeline(Device, Submission.TracePipeline, Allocator);

    if (Submission.PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Submission.PipelineLayout, Allocator);
    if (Submission.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Submission.DescriptorPool, Allocator);   // frees both sets
    if (Submission.SceneSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Submission.SceneSetLayout, Allocator);
    if (Submission.StoreSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Submission.StoreSetLayout, Allocator);

    Submission = SurfelRadianceSubmission{};
}

} // namespace Frontier
