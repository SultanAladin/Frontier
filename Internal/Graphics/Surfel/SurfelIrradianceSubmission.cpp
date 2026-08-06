/*==============================================================================================================================================
                                                     SURFELIRRADIANCESUBMISSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of Phase 7's two dispatches. Initialize builds three set layouts — the store's twelve bindings (shared by both pipeline layouts),
//    the integrate's two atlas images, and the spawn's one sampled image plus six geometry streams — two pipeline layouts, one descriptor pool holding
//    all four sets, and both compute pipelines. Record binds a pair of sets, pushes once and dispatches. Raw Vulkan, no VMA, mirroring the
//    SurfelRadianceSubmission / SurfelStore idiom.
//
// 🔴 THE STORE LAYOUT IS ONE OBJECT BOUND INTO TWO PIPELINE LAYOUTS, BUT ITS TWO DESCRIPTOR SETS ARE SEPARATE. Sharing the layout is free and correct —
//    the two passes declare identical set-0 blocks. Sharing the SET would be too, today, but each pass would then be re-pointed by the other's rebuild
//    path and the two would have to agree forever about which buffer generation they are bound to. Two sets cost 12 descriptors and remove that coupling.
//
// 📝 The pool budgets by TYPE across the whole pool, not per set: 30 storage buffers (12 + 12 + 6), 2 storage images, 1 combined sampler, maxSets 4.
//    ⚠️ Three pool sizes are required here where the radiance unit needed one — a pool that omits a type an allocated layout declares fails the
//    allocation outright, which is at least a loud failure rather than a quiet one.
//
// 📝 Neither push block needs a runtime maxPushConstantsSize check: both are ≤ 128 bytes, which Vulkan GUARANTEES on every device. The header's
//    static_asserts are the whole guarantee, and the spawn block sits at exactly 128 with nothing to spare.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Surfel/SurfelIrradianceSubmission.h"

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

// 📝 Must match `layout(local_size_x = 32)` in SurfelRadianceIntegrate.comp — one invocation per surfel SLOT, not per ray.
constexpr uint32_t IntegrateGroupWidth = 32u;

// 📝 Must match `layout(local_size_x = 16, local_size_y = 16)` in SurfelSpawnRasterization.comp. 🔴 The tile is also the unit the shader elects a single
//    spawn pixel within, through shared memory — so this is not a tunable occupancy figure. Changing it changes how densely the field spawns.
constexpr uint32_t SpawnGroupWidth  = 16u;
constexpr uint32_t SpawnGroupHeight = 16u;

// 🔴 SET 0'S BINDING ORDINALS, IN THE ORDER Shaders/SurfelStoreBindings.glsl DECLARES THEM — the same table SurfelRadianceSubmission.cpp and
//    SurfelLifecycleSubmission.cpp carry, repeated rather than shared for the reason stated there: a divergence must be loud at review, because it is
//    silent at runtime. Every binding is a storage buffer on both sides, so a wrong ordinal hands the pass the wrong buffer and validation says nothing.
//    ⚠️ Adding a binding means editing the .glsl and ALL THREE .cpp tables in one edit.
enum IrradianceStoreBinding : uint32_t
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

// 🔴 THE INTEGRATE'S SET 1, IN SurfelRadianceIntegrate.comp's DECLARATION ORDER. Both are storage images in VK_IMAGE_LAYOUT_GENERAL. ⚠️ Swapping them
//    binds an R32 view where the shader writes two channels; the second channel is dropped and the depth moments read back as pure variance-free
//    visibility, which looks like shadows simply not arriving rather than like a binding error.
enum IrradianceAtlasBinding : uint32_t
{
    BindingGuidingAtlas = 0u,
    BindingDepthAtlas   = 1u,
};

// 🔴 THE SPAWN'S SET 1, IN SurfelSpawnRasterization.comp's DECLARATION ORDER. Binding 0 is the only non-buffer in it; 1..6 are the head streams then the
//    floor streams, and the two triples are NOT interchangeable — a floor pixel's instance ordinal is rebased into the floor table, so crossing them
//    reconstructs a real triangle from the wrong mesh.
enum IrradianceSurfaceBinding : uint32_t
{
    BindingVisibility     = 0u,
    BindingVertices       = 1u,
    BindingMeshIndices    = 2u,
    BindingInstances      = 3u,
    BindingFloorVertices  = 4u,
    BindingFloorIndices   = 5u,
    BindingFloorInstances = 6u,
};

constexpr uint32_t IrradianceStoreBindingCount   = 12u;
constexpr uint32_t IrradianceAtlasBindingCount   =  2u;
constexpr uint32_t IrradianceSurfaceBufferCount  =  6u;   // bindings 1..6; binding 0 is the sampled image and is written separately

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ReportIrradiance(const char* MessageText)
{
    std::fprintf(stderr, "[SurfelIrradianceSubmission] %s\n", MessageText);
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
        ReportIrradiance("shader module unavailable or not word-aligned");
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

// One all-compute set layout of BindingCount consecutive bindings starting at zero, every one of DescriptorType. The store set and the atlas set both
// have that uniform shape; the spawn's surface set does not and is built by hand below.
bool ConstructUniformSetLayout(VulkanHost& Host, uint32_t BindingCount, VkDescriptorType DescriptorType, VkDescriptorSetLayout& Result)
{
    std::vector<VkDescriptorSetLayoutBinding> Bindings((size_t)BindingCount);
    for (uint32_t Index = 0; Index < BindingCount; ++Index)
    {
        Bindings[Index] = {};
        Bindings[Index].binding         = Index;
        Bindings[Index].descriptorType  = DescriptorType;
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

// The spawn's set 1: a combined image sampler at binding 0 and six storage buffers after it. Spelled out rather than generated because the mixed shape
// IS the contract — see the IrradianceSurfaceBinding table.
bool ConstructSurfaceSetLayout(VulkanHost& Host, VkDescriptorSetLayout& Result)
{
    VkDescriptorSetLayoutBinding Bindings[1u + IrradianceSurfaceBufferCount] = {};

    Bindings[BindingVisibility].binding         = BindingVisibility;
    Bindings[BindingVisibility].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Bindings[BindingVisibility].descriptorCount = 1;
    Bindings[BindingVisibility].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    for (uint32_t Index = 1u; Index <= IrradianceSurfaceBufferCount; ++Index)
    {
        Bindings[Index].binding         = Index;
        Bindings[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[Index].descriptorCount = 1;
        Bindings[Index].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = 1u + IrradianceSurfaceBufferCount;
    LayoutInformation.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Result) != VK_SUCCESS)
    {
        Result = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// All three set layouts and both pipeline layouts. 🔴 Each pipeline layout names its sets POSITIONALLY: index 0 is the shader's `set = 0`, so the store
// layout leads in both and only index 1 differs between them.
bool ConstructIrradianceLayout(SurfelIrradianceSubmission& Submission)
{
    VulkanHost& Host = *Submission.Host;

    if (!ConstructUniformSetLayout(Host, IrradianceStoreBindingCount, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Submission.StoreSetLayout))
        return false;
    if (!ConstructUniformSetLayout(Host, IrradianceAtlasBindingCount, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, Submission.AtlasSetLayout))
        return false;
    if (!ConstructSurfaceSetLayout(Host, Submission.SurfaceSetLayout))
        return false;

    VkPushConstantRange IntegrateRange = {};
    IntegrateRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    IntegrateRange.offset     = 0;
    IntegrateRange.size       = sizeof(SurfelIrradianceConstants);

    const VkDescriptorSetLayout IntegrateLayouts[2] = { Submission.StoreSetLayout, Submission.AtlasSetLayout };

    VkPipelineLayoutCreateInfo IntegrateInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    IntegrateInformation.setLayoutCount         = 2;
    IntegrateInformation.pSetLayouts            = IntegrateLayouts;
    IntegrateInformation.pushConstantRangeCount = 1;
    IntegrateInformation.pPushConstantRanges    = &IntegrateRange;
    if (vkCreatePipelineLayout(Host.Device, &IntegrateInformation, Host.Allocator, &Submission.IntegrateLayout) != VK_SUCCESS)
    {
        Submission.IntegrateLayout = VK_NULL_HANDLE;
        return false;
    }

    VkPushConstantRange SpawnRange = {};
    SpawnRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    SpawnRange.offset     = 0;
    SpawnRange.size       = sizeof(SurfelSpawnConstants);

    const VkDescriptorSetLayout SpawnLayouts[2] = { Submission.StoreSetLayout, Submission.SurfaceSetLayout };

    VkPipelineLayoutCreateInfo SpawnInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    SpawnInformation.setLayoutCount         = 2;
    SpawnInformation.pSetLayouts            = SpawnLayouts;
    SpawnInformation.pushConstantRangeCount = 1;
    SpawnInformation.pPushConstantRanges    = &SpawnRange;
    if (vkCreatePipelineLayout(Host.Device, &SpawnInformation, Host.Allocator, &Submission.SpawnLayout) != VK_SUCCESS)
    {
        Submission.SpawnLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// One pool holding all four sets. 📝 Counts are summed by TYPE across every set the pool will hand out — the store layout is allocated TWICE, so its
// twelve buffers are counted twice.
bool ConstructIrradianceDescriptors(SurfelIrradianceSubmission& Submission)
{
    VulkanHost& Host = *Submission.Host;

    VkDescriptorPoolSize PoolSizes[3] = {};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSizes[0].descriptorCount = IrradianceStoreBindingCount * 2u + IrradianceSurfaceBufferCount;
    PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    PoolSizes[1].descriptorCount = IrradianceAtlasBindingCount;
    PoolSizes[2].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSizes[2].descriptorCount = 1;

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 4;
    PoolInformation.poolSizeCount = 3;
    PoolInformation.pPoolSizes    = PoolSizes;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Submission.DescriptorPool) != VK_SUCCESS)
    {
        Submission.DescriptorPool = VK_NULL_HANDLE;
        return false;
    }

    // Allocated in one call so the four sets come back in the same order as the layouts handed in.
    const VkDescriptorSetLayout OrderedLayouts[4] =
    {
        Submission.StoreSetLayout,     // -> IntegrateStoreSet
        Submission.StoreSetLayout,     // -> SpawnStoreSet
        Submission.AtlasSetLayout,     // -> AtlasSet
        Submission.SurfaceSetLayout,   // -> SurfaceSet
    };
    VkDescriptorSet AllocatedSets[4] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };

    VkDescriptorSetAllocateInfo SetAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocation.descriptorPool     = Submission.DescriptorPool;
    SetAllocation.descriptorSetCount = 4;
    SetAllocation.pSetLayouts        = OrderedLayouts;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, AllocatedSets) != VK_SUCCESS)
        return false;

    Submission.IntegrateStoreSet = AllocatedSets[0];
    Submission.SpawnStoreSet     = AllocatedSets[1];
    Submission.AtlasSet          = AllocatedSets[2];
    Submission.SurfaceSet        = AllocatedSets[3];
    return true;
}

// Write a run of consecutive storage-buffer bindings from an ordered handle array, beginning at FirstBinding. 🔴 Element i lands on binding
// FirstBinding + i — the array's order IS the binding order.
bool WriteStorageDescriptors(VkDevice Device, VkDescriptorSet Set, uint32_t FirstBinding, const VkBuffer* OrderedBuffers, uint32_t BindingCount)
{
    std::vector<VkDescriptorBufferInfo> BufferSpans((size_t)BindingCount);
    std::vector<VkWriteDescriptorSet>   Writes((size_t)BindingCount);

    for (uint32_t Index = 0; Index < BindingCount; ++Index)
    {
        if (OrderedBuffers[Index] == VK_NULL_HANDLE)
        {
            ReportIrradiance("a null buffer was handed in; descriptors not written");
            return false;
        }

        BufferSpans[Index] = {};
        BufferSpans[Index].buffer = OrderedBuffers[Index];
        BufferSpans[Index].offset = 0;
        BufferSpans[Index].range  = VK_WHOLE_SIZE;

        Writes[Index] = {};
        Writes[Index].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet          = Set;
        Writes[Index].dstBinding      = FirstBinding + Index;
        Writes[Index].descriptorCount = 1;
        Writes[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[Index].pBufferInfo     = &BufferSpans[Index];
    }

    vkUpdateDescriptorSets(Device, BindingCount, Writes.data(), 0, nullptr);
    return true;
}

// Point one store set at the store. The array's order mirrors the IrradianceStoreBinding table, which mirrors SurfelStoreBindings.glsl.
bool WriteIrradianceStoreDescriptors(SurfelIrradianceSubmission& Submission, const SurfelStore& Store, VkDescriptorSet Set)
{
    const VkBuffer OrderedBuffers[IrradianceStoreBindingCount] =
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

    return WriteStorageDescriptors(Submission.Host->Device, Set, 0u, OrderedBuffers, IrradianceStoreBindingCount);
}

// Point the integrate's set 1 at both atlases. ⚠️ VK_IMAGE_LAYOUT_GENERAL is asserted here as the layout the images will be IN at dispatch, not
// requested — a descriptor write does not transition anything. InitializeSurfelStore leaves them there; see the header's note on who must put them back.
bool WriteIrradianceAtlasDescriptors(SurfelIrradianceSubmission& Submission, const SurfelStore& Store)
{
    if (Store.IrradianceAtlas.View == VK_NULL_HANDLE || Store.DepthAtlas.View == VK_NULL_HANDLE)
    {
        ReportIrradiance("the store's atlas views are absent; descriptors not written");
        return false;
    }

    VkDescriptorImageInfo AtlasViews[IrradianceAtlasBindingCount] = {};
    AtlasViews[BindingGuidingAtlas].imageView   = Store.IrradianceAtlas.View;
    AtlasViews[BindingGuidingAtlas].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    AtlasViews[BindingDepthAtlas].imageView     = Store.DepthAtlas.View;
    AtlasViews[BindingDepthAtlas].imageLayout   = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet Writes[IrradianceAtlasBindingCount] = {};
    for (uint32_t Index = 0; Index < IrradianceAtlasBindingCount; ++Index)
    {
        Writes[Index].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet          = Submission.AtlasSet;
        Writes[Index].dstBinding      = Index;
        Writes[Index].descriptorCount = 1;
        Writes[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        Writes[Index].pImageInfo      = &AtlasViews[Index];
    }

    vkUpdateDescriptorSets(Submission.Host->Device, IrradianceAtlasBindingCount, Writes, 0, nullptr);
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSurfelIrradianceSubmission(SurfelIrradianceSubmission& Submission,
                                          VulkanHost&                 Host,
                                          const SurfelStore&          Store,
                                          const char*                 ShaderDirectory)
{
    Submission = SurfelIrradianceSubmission{};
    Submission.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportIrradiance("no device — irradiance passes not built");
        Submission = SurfelIrradianceSubmission{};
        return false;
    }
    if (!Store.ReadyCondition)
    {
        ReportIrradiance("store not ready — irradiance passes not built");
        Submission = SurfelIrradianceSubmission{};
        return false;
    }
    if (ShaderDirectory == nullptr)
    {
        ReportIrradiance("no shader directory — irradiance passes not built");
        Submission = SurfelIrradianceSubmission{};
        return false;
    }

    if (!ConstructIrradianceLayout(Submission))
    {
        ReportIrradiance("set / pipeline layout creation failed");
        FinalizeSurfelIrradianceSubmission(Submission);
        return false;
    }
    if (!ConstructIrradianceDescriptors(Submission))
    {
        ReportIrradiance("descriptor pool / set creation failed");
        FinalizeSurfelIrradianceSubmission(Submission);
        return false;
    }
    if (!WriteIrradianceStoreDescriptors(Submission, Store, Submission.IntegrateStoreSet) ||
        !WriteIrradianceStoreDescriptors(Submission, Store, Submission.SpawnStoreSet))
    {
        ReportIrradiance("store descriptor write failed");
        FinalizeSurfelIrradianceSubmission(Submission);
        return false;
    }
    if (!WriteIrradianceAtlasDescriptors(Submission, Store))
    {
        ReportIrradiance("atlas descriptor write failed");
        FinalizeSurfelIrradianceSubmission(Submission);
        return false;
    }

    Submission.BoundRecordBuffer = Store.SurfelRecords.Buffer;

    // ⚠️ The spawn's set 1 is deliberately NOT written here. The visibility buffer is republished on every swapchain resize and the floor streams appear
    //    only when a floor document loads, so demanding them at this point would order the renderer's construction the wrong way round;
    //    RefreshSurfelSpawnSurfaceBinding fills it and raises SpawnCondition, and RecordSurfelSpawn is a no-op until that has happened once.

    const std::string Directory = ShaderDirectory;
    Submission.IntegratePipeline = ConstructComputePipeline(Host, Submission.IntegrateLayout, Directory, "SurfelRadianceIntegrate.comp.spv");
    if (Submission.IntegratePipeline == VK_NULL_HANDLE)
    {
        ReportIrradiance("the integrate pipeline failed to build");
        FinalizeSurfelIrradianceSubmission(Submission);
        return false;
    }

    Submission.SpawnPipeline = ConstructComputePipeline(Host, Submission.SpawnLayout, Directory, "SurfelSpawnRasterization.comp.spv");
    if (Submission.SpawnPipeline == VK_NULL_HANDLE)
    {
        ReportIrradiance("the spawn pipeline failed to build");
        FinalizeSurfelIrradianceSubmission(Submission);
        return false;
    }

    Submission.ReadyCondition = true;
    return true;
}

bool RefreshSurfelSpawnSurfaceBinding(SurfelIrradianceSubmission& Submission, const SurfelSpawnSurfaceBinding& Binding)
{
    if (Submission.Host == nullptr || Submission.SurfaceSet == VK_NULL_HANDLE)
        return false;
    if (Binding.VisibilityView == VK_NULL_HANDLE || Binding.VisibilitySampler == VK_NULL_HANDLE)
    {
        ReportIrradiance("the visibility view or sampler is absent; surface set not written");
        return false;
    }

    // The array's order mirrors the IrradianceSurfaceBinding table from binding 1 on. ⚠️ When no floor document is loaded the caller aliases the three
    // floor handles onto the head handles — a partially-written set is undefined to dispatch against, so a null here is refused rather than skipped.
    const VkBuffer OrderedBuffers[IrradianceSurfaceBufferCount] =
    {
        Binding.Vertices,         // 1
        Binding.MeshIndices,      // 2
        Binding.Instances,        // 3
        Binding.FloorVertices,    // 4
        Binding.FloorIndices,     // 5
        Binding.FloorInstances,   // 6
    };

    if (!WriteStorageDescriptors(Submission.Host->Device, Submission.SurfaceSet, BindingVertices, OrderedBuffers, IrradianceSurfaceBufferCount))
        return false;

    // 📝 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: the spawn only ever reads the identity buffer, and the raster that wrote it leaves it there for the
    //    shade to sample. The spawn taking it in the same layout means no extra transition is owed on its account.
    VkDescriptorImageInfo VisibilityView = {};
    VisibilityView.sampler     = Binding.VisibilitySampler;
    VisibilityView.imageView   = Binding.VisibilityView;
    VisibilityView.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet VisibilityWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    VisibilityWrite.dstSet          = Submission.SurfaceSet;
    VisibilityWrite.dstBinding      = BindingVisibility;
    VisibilityWrite.descriptorCount = 1;
    VisibilityWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    VisibilityWrite.pImageInfo      = &VisibilityView;
    vkUpdateDescriptorSets(Submission.Host->Device, 1, &VisibilityWrite, 0, nullptr);

    Submission.SpawnCondition = true;
    return true;
}

void RecordSurfelIrradianceIntegrate(SurfelIrradianceSubmission&      Submission,
                                     const SurfelStore&               Store,
                                     const SurfelIrradianceConstants& Constants,
                                     VkCommandBuffer                  CommandBuffer)
{
    if (!Submission.ReadyCondition || Submission.Host == nullptr || CommandBuffer == VK_NULL_HANDLE)
        return;
    if (!Store.ReadyCondition || Store.Capacity == 0u)
        return;

    // A rebuilt store hands out fresh handles; re-point BOTH store sets rather than dispatching against freed memory. In the steady state this never
    // fires, because the store allocates once and stays resident. ⚠️ The atlas set is re-pointed too — a rebuilt store rebuilds its images as well.
    if (Store.SurfelRecords.Buffer != Submission.BoundRecordBuffer)
    {
        if (!WriteIrradianceStoreDescriptors(Submission, Store, Submission.IntegrateStoreSet) ||
            !WriteIrradianceStoreDescriptors(Submission, Store, Submission.SpawnStoreSet)     ||
            !WriteIrradianceAtlasDescriptors(Submission, Store))
            return;
        Submission.BoundRecordBuffer = Store.SurfelRecords.Buffer;
    }

    // 🔴 BOTH SETS ARE BOUND IN ONE CALL, AT FIRST SET 0 — the store at 0 and the atlases at 1, the order the pipeline layout names them.
    const VkDescriptorSet OrderedSets[2] = { Submission.IntegrateStoreSet, Submission.AtlasSet };
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Submission.IntegrateLayout, 0, 2, OrderedSets, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Submission.IntegrateLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SurfelIrradianceConstants), &Constants);

    // 🔴 THE DISPATCH COVERS THE WHOLE SURFEL CAPACITY, NOT THE FRAME'S LIVE COUNT. See the header: the count lives on the device and sizing from it
    //    host-side means a stall. The shader's first act is to read the counter and return for every index past it, so the excess groups cost one
    //    buffer read each. 📝 Taken from the store's own capacity so a store built at a different limit cannot disagree with SurfelTypes.h.
    const uint32_t SurfelGroups = (Store.Capacity + IntegrateGroupWidth - 1u) / IntegrateGroupWidth;
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Submission.IntegratePipeline);
    vkCmdDispatch(CommandBuffer, SurfelGroups, 1, 1);

    // ⚠️ No trailing barrier. The spawn reads the radiance this wrote in order to seed a new surfel from it, and the fence that makes it visible belongs
    //    to the caller that records both — the consumer knows its own destination stage, this unit does not. Same contract as RecordSurfelRadianceTrace.
}

void RecordSurfelSpawn(SurfelIrradianceSubmission& Submission,
                       const SurfelStore&          Store,
                       const SurfelSpawnConstants& Constants,
                       VkCommandBuffer             CommandBuffer)
{
    if (!Submission.ReadyCondition || Submission.Host == nullptr || CommandBuffer == VK_NULL_HANDLE)
        return;
    if (!Submission.SpawnCondition)
        return;   // the visible surface has never published; there is nothing to spawn against
    if (!Store.ReadyCondition)
        return;

    // 📝 Unpacked from the same word the shader unpacks, rather than carried alongside it — one representation, so a caller that filled the field by hand
    //    is wrong in both places at once instead of dispatching a correct group count against a mis-scaled coverage test.
    const uint32_t ResolutionX = Constants.ResolutionPacked & 0xFFFFu;
    const uint32_t ResolutionY = Constants.ResolutionPacked >> 16u;
    if (ResolutionX == 0u || ResolutionY == 0u)
        return;   // a swapchain that has not published yet degrades to "no spawns this frame"

    // ⚠️ Only the spawn's own store set is re-pointed here. A store rebuilt between the two records would be a caller error — they belong to one frame —
    //    so the integrate's check above is the one that owns the rebuild, and this one exists for a caller that records the spawn alone.
    if (Store.SurfelRecords.Buffer != Submission.BoundRecordBuffer)
    {
        if (!WriteIrradianceStoreDescriptors(Submission, Store, Submission.IntegrateStoreSet) ||
            !WriteIrradianceStoreDescriptors(Submission, Store, Submission.SpawnStoreSet)     ||
            !WriteIrradianceAtlasDescriptors(Submission, Store))
            return;
        Submission.BoundRecordBuffer = Store.SurfelRecords.Buffer;
    }

    const VkDescriptorSet OrderedSets[2] = { Submission.SpawnStoreSet, Submission.SurfaceSet };
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Submission.SpawnLayout, 0, 2, OrderedSets, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Submission.SpawnLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SurfelSpawnConstants), &Constants);

    // 🔴 ONE GROUP PER SCREEN TILE, AND THE TILE IS THE ELECTION UNIT — the shader picks a single spawn pixel per group through shared memory, so the
    //    group count is the number of spawn candidates this frame, not an occupancy choice. A partial edge tile is covered by the shader's own bounds
    //    test rather than by a smaller dispatch.
    const uint32_t TileColumns = (ResolutionX + SpawnGroupWidth  - 1u) / SpawnGroupWidth;
    const uint32_t TileRows    = (ResolutionY + SpawnGroupHeight - 1u) / SpawnGroupHeight;
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Submission.SpawnPipeline);
    vkCmdDispatch(CommandBuffer, TileColumns, TileRows, 1);

    // ⚠️ No trailing barrier — the census that picks these new surfels up next frame owns it.
}

void FinalizeSurfelIrradianceSubmission(SurfelIrradianceSubmission& Submission)
{
    if (Submission.Host == nullptr || Submission.Host->Device == VK_NULL_HANDLE)
    {
        Submission = SurfelIrradianceSubmission{};
        return;
    }

    VkDevice                     Device    = Submission.Host->Device;
    const VkAllocationCallbacks* Allocator = Submission.Host->Allocator;

    if (Submission.SpawnPipeline     != VK_NULL_HANDLE) vkDestroyPipeline(Device, Submission.SpawnPipeline, Allocator);
    if (Submission.IntegratePipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, Submission.IntegratePipeline, Allocator);

    if (Submission.SpawnLayout       != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Submission.SpawnLayout, Allocator);
    if (Submission.IntegrateLayout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Submission.IntegrateLayout, Allocator);

    if (Submission.DescriptorPool    != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Submission.DescriptorPool, Allocator);   // frees all four sets

    if (Submission.SurfaceSetLayout  != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Submission.SurfaceSetLayout, Allocator);
    if (Submission.AtlasSetLayout    != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Submission.AtlasSetLayout, Allocator);
    if (Submission.StoreSetLayout    != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Submission.StoreSetLayout, Allocator);

    Submission = SurfelIrradianceSubmission{};
}

} // namespace Frontier
