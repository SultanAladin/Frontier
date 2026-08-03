/*==============================================================================================================================================
                                                      SURFELINTEGRATESUBMISSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the per-surfel integrate. Initialize builds two storage-only set layouts (the 7-binding BVH set 0 + the 7-binding surfel set
//    1), one pipeline layout carrying both + the SurfelIntegrateConstants push range, the single SurfelIntegrate.comp pipeline, and a descriptor pool
//    with the two sets. Refresh points every borrowed binding once (scene static). Record binds both sets, pushes the constants, and dispatches
//    ceil(Capacity/64) — the in-shader age-guard is the bounds guard. Raw Vulkan, no VMA, mirroring SurfelLifecycleSubmission / InstanceCullSubmission.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Surfel/SurfelIntegrateSubmission.h"

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

void ReportIntegrate(const char* MessageText)
{
    std::fprintf(stderr, "[SurfelIntegrate] %s\n", MessageText);
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
        ReportIntegrate("integrate shader module unavailable");
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

// Build a storage-only set layout of N compute bindings starting at binding 0 (the BVH set and the surfel set are both plain storage runs).
VkDescriptorSetLayout ConstructStorageSetLayout(VulkanHost& Host, uint32_t BindingCount)
{
    std::vector<VkDescriptorSetLayoutBinding> Bindings(BindingCount);
    for (uint32_t Index = 0; Index < BindingCount; ++Index)
    {
        Bindings[Index].binding         = Index;
        Bindings[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[Index].descriptorCount = 1;
        Bindings[Index].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = BindingCount;
    LayoutInformation.pBindings    = Bindings.data();
    VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Layout) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return Layout;
}

VkPipelineLayout ConstructPipelineLayout(VulkanHost& Host, const VkDescriptorSetLayout* SetLayouts, uint32_t SetCount, uint32_t PushSize)
{
    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = PushSize;

    VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipelineLayoutInformation.setLayoutCount         = SetCount;
    PipelineLayoutInformation.pSetLayouts            = SetLayouts;
    PipelineLayoutInformation.pushConstantRangeCount = 1;
    PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(Host.Device, &PipelineLayoutInformation, Host.Allocator, &Layout) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return Layout;
}

// Write a single storage-buffer descriptor (VK_WHOLE_SIZE — the arena word arrays are freed after upload, so no host byte count survives; matches the
// trace exe's descriptor write).
void WriteStorageDescriptor(VulkanHost& Host, VkDescriptorSet Set, uint32_t Binding, VkBuffer Buffer)
{
    VkDescriptorBufferInfo BufferInfo = { Buffer, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet Write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    Write.dstSet          = Set;
    Write.dstBinding      = Binding;
    Write.descriptorCount = 1;
    Write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Write.pBufferInfo     = &BufferInfo;
    vkUpdateDescriptorSets(Host.Device, 1, &Write, 0, nullptr);
}

// Re-point a borrowed binding only on a handle change (the Bound* cache). Skips a null buffer so an unloaded resource never leaves a descriptor
// undefined mid-run — it stays whatever it was, and ReadyCondition / the caller's guards keep the record off until every handle is real.
void RepointOnChange(VulkanHost& Host, VkDescriptorSet Set, uint32_t Binding, VkBuffer Buffer, VkBuffer& Cache)
{
    if (Buffer != VK_NULL_HANDLE && Buffer != Cache)
    {
        WriteStorageDescriptor(Host, Set, Binding, Buffer);
        Cache = Buffer;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSurfelIntegrateSubmission(SurfelIntegrateSubmission& Integrate,
                                         VulkanHost&                Host,
                                         const char*                ShaderDirectory)
{
    Integrate = SurfelIntegrateSubmission{};
    Integrate.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportIntegrate("no device — integrate not built");
        return false;
    }

    // --- set layouts: BVH set 0 (7 storage) + surfel set 1 (7 storage) ---
    Integrate.BvhLayout    = ConstructStorageSetLayout(Host, 7);   // Instances, Slices, ArenaNodeWords, ArenaPrimitives, TreeNodeWords, MeshIndices, Vertices
    Integrate.SurfelLayout = ConstructStorageSetLayout(Host, 7);   // Surfels, Moments, Offsets, List, Guiding, SurfelDepth, Touched
    if (Integrate.BvhLayout == VK_NULL_HANDLE || Integrate.SurfelLayout == VK_NULL_HANDLE)
    {
        ReportIntegrate("set layout creation failed");
        FinalizeSurfelIntegrateSubmission(Integrate);
        return false;
    }

    // --- pipeline layout: both sets + the push range ---
    const VkDescriptorSetLayout Sets[2] = { Integrate.BvhLayout, Integrate.SurfelLayout };
    Integrate.PipelineLayout = ConstructPipelineLayout(Host, Sets, 2, sizeof(SurfelIntegrateConstants));
    if (Integrate.PipelineLayout == VK_NULL_HANDLE)
    {
        ReportIntegrate("pipeline layout creation failed");
        FinalizeSurfelIntegrateSubmission(Integrate);
        return false;
    }

    // --- pipeline ---
    const std::string Directory = ShaderDirectory;
    Integrate.Pipeline = ConstructComputePipeline(Host, Integrate.PipelineLayout, Directory, "SurfelIntegrate.comp.spv");
    if (Integrate.Pipeline == VK_NULL_HANDLE)
    {
        ReportIntegrate("integrate pipeline failed to build");
        FinalizeSurfelIntegrateSubmission(Integrate);
        return false;
    }

    // --- descriptor pool + two sets (14 storage descriptors total) ---
    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = 7 + 7;

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 2;
    PoolInformation.poolSizeCount = 1;
    PoolInformation.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Integrate.DescriptorPool) != VK_SUCCESS)
    {
        ReportIntegrate("descriptor pool creation failed");
        FinalizeSurfelIntegrateSubmission(Integrate);
        return false;
    }

    const VkDescriptorSetLayout AllocLayouts[2] = { Integrate.BvhLayout, Integrate.SurfelLayout };
    VkDescriptorSet AllocSets[2] = {};
    VkDescriptorSetAllocateInfo SetAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocation.descriptorPool     = Integrate.DescriptorPool;
    SetAllocation.descriptorSetCount = 2;
    SetAllocation.pSetLayouts        = AllocLayouts;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, AllocSets) != VK_SUCCESS)
    {
        ReportIntegrate("descriptor set allocation failed");
        FinalizeSurfelIntegrateSubmission(Integrate);
        return false;
    }
    Integrate.BvhSet    = AllocSets[0];
    Integrate.SurfelSet = AllocSets[1];

    Integrate.ReadyCondition = true;
    return true;
}

void RefreshSurfelIntegrateBindings(SurfelIntegrateSubmission& Integrate,
                                    const SurfelPool&          Pool,
                                    const SurfelGridSlotting&  Slotting,
                                    VkBuffer                   InstanceBuffer,
                                    VkBuffer                   SliceBuffer,
                                    VkBuffer                   ArenaNodeBuffer,
                                    VkBuffer                   ArenaPrimitiveBuffer,
                                    VkBuffer                   TreeNodeBuffer,
                                    VkBuffer                   IndexBuffer,
                                    VkBuffer                   VertexBuffer)
{
    if (!Integrate.ReadyCondition || Integrate.Host == nullptr)
        return;
    VulkanHost& Host = *Integrate.Host;

    // --- set 0: the BVH (b0 Instances, b1 Slices, b2 ArenaNodeWords, b3 ArenaPrimitives, b4 TreeNodeWords, b5 MeshIndices, b6 Vertices) ---
    RepointOnChange(Host, Integrate.BvhSet, 0u, InstanceBuffer,       Integrate.BoundInstances);
    RepointOnChange(Host, Integrate.BvhSet, 1u, SliceBuffer,          Integrate.BoundSlices);
    RepointOnChange(Host, Integrate.BvhSet, 2u, ArenaNodeBuffer,      Integrate.BoundArenaNodeWords);
    RepointOnChange(Host, Integrate.BvhSet, 3u, ArenaPrimitiveBuffer, Integrate.BoundArenaPrimitives);
    RepointOnChange(Host, Integrate.BvhSet, 4u, TreeNodeBuffer,       Integrate.BoundTreeNodeWords);
    RepointOnChange(Host, Integrate.BvhSet, 5u, IndexBuffer,          Integrate.BoundMeshIndices);
    RepointOnChange(Host, Integrate.BvhSet, 6u, VertexBuffer,         Integrate.BoundVertices);

    // --- set 1: the surfel state (b0 Surfels, b1 Moments, b2 Offsets, b3 List, b4 Guiding, b5 SurfelDepth, b6 Touched) ---
    RepointOnChange(Host, Integrate.SurfelSet, 0u, Pool.SurfelBuffer,      Integrate.BoundSurfels);
    RepointOnChange(Host, Integrate.SurfelSet, 1u, Pool.MomentsBuffer,     Integrate.BoundMoments);
    RepointOnChange(Host, Integrate.SurfelSet, 2u, Slotting.OffsetsBuffer, Integrate.BoundOffsets);
    RepointOnChange(Host, Integrate.SurfelSet, 3u, Slotting.ListBuffer,    Integrate.BoundList);
    RepointOnChange(Host, Integrate.SurfelSet, 4u, Pool.GuidingBuffer,     Integrate.BoundGuiding);
    RepointOnChange(Host, Integrate.SurfelSet, 5u, Pool.SurfelDepthBuffer, Integrate.BoundSurfelDepth);
    RepointOnChange(Host, Integrate.SurfelSet, 6u, Pool.TouchedBuffer,     Integrate.BoundTouched);
}

SurfelIntegrateConstants AssembleSurfelIntegrateConstants(const SurfelPool& Pool,
                                                          const float       CameraPosition[3],
                                                          const float       GridOrigin[3],
                                                          const float       SunDirection[3],
                                                          const float       SunColour[3],
                                                          const float       SkyGround[3],
                                                          const float       SkyZenith[3],
                                                          float             SkyIntensity,
                                                          uint32_t          Frame,
                                                          uint32_t          InstanceCount,
                                                          uint32_t          SliceCount)
{
    SurfelIntegrateConstants Constants = {};

    for (int Index = 0; Index < 3; ++Index)
    {
        Constants.CameraPosition[Index] = CameraPosition[Index];
        Constants.GridOrigin[Index]     = GridOrigin[Index];
        Constants.SunDirection[Index]   = SunDirection[Index];
        Constants.SunColour[Index]      = SunColour[Index];
        Constants.SkyGround[Index]      = SkyGround[Index];
        Constants.SkyZenith[Index]      = SkyZenith[Index];
    }
    Constants.SkyZenith[3] = SkyIntensity;   // sky intensity rides SkyZenith.w

    // The occlusion tunables (ported 1:1) stay at their struct defaults (1.2, 0.2, 0.25, 0.15).

    // 🔴 fact 4: the shader indexes Moments[index + offset] over the 5-vec4 struct array, so it wants the ELEMENT count parity*Capacity — NOT the byte
    //    offset SurfelMomentsReadOffset/WriteOffset return. Read half = parity*cap, write half = (1-parity)*cap.
    Constants.ReadOffsetElements  = Pool.MomentsParity * Pool.Capacity;
    Constants.WriteOffsetElements = (1u - Pool.MomentsParity) * Pool.Capacity;
    Constants.Capacity            = Pool.Capacity;
    Constants.Frame               = Frame;

    Constants.BaseSampleCount = 32;
    Constants.InstanceCount   = InstanceCount;
    Constants.SliceCount      = SliceCount;
    Constants.AlbedoBoost     = 1.0f;
    Constants.GiFromDirect    = 1.0f;
    Constants.GiFromIndirect  = 1.0f;

    return Constants;
}

void RecordSurfelIntegrate(SurfelIntegrateSubmission&      Integrate,
                           const SurfelPool&               Pool,
                           const SurfelIntegrateConstants& Constants,
                           VkCommandBuffer                 CommandBuffer)
{
    if (!Integrate.ReadyCondition)
        return;
    if (!Pool.ReadyCondition || Pool.SurfelBuffer == VK_NULL_HANDLE || Pool.Capacity == 0)
        return;

    const VkDescriptorSet Sets[2] = { Integrate.BvhSet, Integrate.SurfelSet };
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Integrate.PipelineLayout, 0, 2, Sets, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Integrate.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SurfelIntegrateConstants), &Constants);
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Integrate.Pipeline);
    const uint32_t Groups = (Pool.Capacity + SurfelIntegrateWorkgroupEdge - 1) / SurfelIntegrateWorkgroupEdge;
    vkCmdDispatch(CommandBuffer, Groups, 1, 1);
    // The caller inserts B3 (write-visibility) + SwapSurfelMoments after this record.
}

void FinalizeSurfelIntegrateSubmission(SurfelIntegrateSubmission& Integrate)
{
    if (Integrate.Host == nullptr || Integrate.Host->Device == VK_NULL_HANDLE)
    {
        Integrate = SurfelIntegrateSubmission{};
        return;
    }
    VkDevice Device = Integrate.Host->Device;
    const VkAllocationCallbacks* Allocator = Integrate.Host->Allocator;

    if (Integrate.Pipeline       != VK_NULL_HANDLE) vkDestroyPipeline(Device, Integrate.Pipeline, Allocator);
    if (Integrate.PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Integrate.PipelineLayout, Allocator);
    if (Integrate.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Integrate.DescriptorPool, Allocator);   // frees both sets

    if (Integrate.BvhLayout    != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Integrate.BvhLayout, Allocator);
    if (Integrate.SurfelLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Integrate.SurfelLayout, Allocator);

    Integrate = SurfelIntegrateSubmission{};
}

} // namespace Frontier
