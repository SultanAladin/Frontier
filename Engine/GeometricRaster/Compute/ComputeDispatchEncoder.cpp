/*============================================================================================================================================
                                                        COMPUTEDISPATCHENCODER.CPP
============================================================================================================================================*/
// 🧩 Implementation of ComputeDispatchEncoder managing .slang SPIR-V compute pipelines, descriptor sets, and UAV barriers

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"

#include <array>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

ComputeDispatchEncoder::~ComputeDispatchEncoder()
{
    TeardownEncoder();
}

bool ComputeDispatchEncoder::InitializeEncoder(VulkanDeviceContext* BoundContext, uint32_t MaxDispatchesPerPool)
{
    TeardownEncoder();
    if (BoundContext == nullptr || !BoundContext->Ready())
    {
        return false;
    }

    Context = BoundContext;
    const std::array<VkDescriptorPoolSize, 2u> PoolSizes {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxDispatchesPerPool * 2u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MaxDispatchesPerPool * 6u },
    }};

    VkDescriptorPoolCreateInfo PoolInfo {};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.maxSets       = MaxDispatchesPerPool;
    PoolInfo.poolSizeCount = static_cast<uint32_t>(PoolSizes.size());
    PoolInfo.pPoolSizes    = PoolSizes.data();

    if (vkCreateDescriptorPool(Context->AccessDevice(), &PoolInfo, nullptr, &DescriptorPool) != VK_SUCCESS)
    {
        Context = nullptr;
        return false;
    }
    return true;
}

void ComputeDispatchEncoder::TeardownEncoder() noexcept
{
    if (Context != nullptr && Context->Ready())
    {
        VkDevice Dev = Context->AccessDevice();
        vkDeviceWaitIdle(Dev);
        for (RegisteredComputeProgram& Prog : Programs)
        {
            if (Prog.Pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(Dev, Prog.Pipeline, nullptr);
            }
            if (Prog.PipelineLayout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(Dev, Prog.PipelineLayout, nullptr);
            }
            if (Prog.SetLayout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(Dev, Prog.SetLayout, nullptr);
            }
            if (Prog.ShaderModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(Dev, Prog.ShaderModule, nullptr);
            }
        }
        Programs.clear();

        if (DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Dev, DescriptorPool, nullptr);
            DescriptorPool = VK_NULL_HANDLE;
        }
    }
    Context = nullptr;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           PROGRAM REGISTRATION & DISPATCH
//------------------------------------------------------------------------------------------------------------------------

uint32_t ComputeDispatchEncoder::RegisterProgram(
    const std::filesystem::path&      SpirvPath,
    std::span<const VkDescriptorType> BindingTypes,
    const char*                       EntryPointName)
{
    if (Context == nullptr || !Context->Ready())
    {
        return UINT32_MAX;
    }

    VkDevice Dev = Context->AccessDevice();
    RegisteredComputeProgram Prog {};
    Prog.ShaderModule = Context->LoadShaderModule(SpirvPath);
    if (Prog.ShaderModule == VK_NULL_HANDLE)
    {
        return UINT32_MAX;
    }

    std::vector<VkDescriptorSetLayoutBinding> LayoutBindings(BindingTypes.size());
    for (std::size_t Idx = 0u; Idx < BindingTypes.size(); ++Idx)
    {
        LayoutBindings[Idx].binding            = static_cast<uint32_t>(Idx);
        LayoutBindings[Idx].descriptorType     = BindingTypes[Idx];
        LayoutBindings[Idx].descriptorCount    = 1u;
        LayoutBindings[Idx].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
        LayoutBindings[Idx].pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo SetLayoutInfo {};
    SetLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetLayoutInfo.bindingCount = static_cast<uint32_t>(LayoutBindings.size());
    SetLayoutInfo.pBindings    = LayoutBindings.data();
    if (vkCreateDescriptorSetLayout(Dev, &SetLayoutInfo, nullptr, &Prog.SetLayout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Dev, Prog.ShaderModule, nullptr);
        return UINT32_MAX;
    }

    VkPipelineLayoutCreateInfo PipeLayoutInfo {};
    PipeLayoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PipeLayoutInfo.setLayoutCount = 1u;
    PipeLayoutInfo.pSetLayouts    = &Prog.SetLayout;
    if (vkCreatePipelineLayout(Dev, &PipeLayoutInfo, nullptr, &Prog.PipelineLayout) != VK_SUCCESS)
    {
        vkDestroyDescriptorSetLayout(Dev, Prog.SetLayout, nullptr);
        vkDestroyShaderModule(Dev, Prog.ShaderModule, nullptr);
        return UINT32_MAX;
    }

    VkComputePipelineCreateInfo PipeCreate {};
    PipeCreate.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    PipeCreate.stage.sType        = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    PipeCreate.stage.stage        = VK_SHADER_STAGE_COMPUTE_BIT;
    PipeCreate.stage.module       = Prog.ShaderModule;
    PipeCreate.stage.pName        = EntryPointName;
    PipeCreate.layout             = Prog.PipelineLayout;

    if (vkCreateComputePipelines(Dev, VK_NULL_HANDLE, 1u, &PipeCreate, nullptr, &Prog.Pipeline) != VK_SUCCESS)
    {
        vkDestroyPipelineLayout(Dev, Prog.PipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(Dev, Prog.SetLayout, nullptr);
        vkDestroyShaderModule(Dev, Prog.ShaderModule, nullptr);
        return UINT32_MAX;
    }

    Prog.BindingCount = static_cast<uint32_t>(BindingTypes.size());
    const uint32_t AssignedIndex = static_cast<uint32_t>(Programs.size());
    Programs.push_back(Prog);
    return AssignedIndex;
}

void ComputeDispatchEncoder::ResetTransientDescriptors() noexcept
{
    if (Context != nullptr && DescriptorPool != VK_NULL_HANDLE)
    {
        vkResetDescriptorPool(Context->AccessDevice(), DescriptorPool, 0u);
    }
}

bool ComputeDispatchEncoder::EncodeComputeDispatch(
    VkCommandBuffer                           CmdBuffer,
    uint32_t                                  ProgramHandle,
    std::span<const ComputeBindingDescriptor> Bindings,
    uint32_t                                  GroupCountX,
    uint32_t                                  GroupCountY,
    uint32_t                                  GroupCountZ)
{
    if (Context == nullptr || ProgramHandle >= Programs.size() || GroupCountX == 0u)
    {
        return false;
    }

    VkDevice Dev = Context->AccessDevice();
    const RegisteredComputeProgram& Prog = Programs[ProgramHandle];

    VkDescriptorSetAllocateInfo AllocInfo {};
    AllocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    AllocInfo.descriptorPool     = DescriptorPool;
    AllocInfo.descriptorSetCount = 1u;
    AllocInfo.pSetLayouts        = &Prog.SetLayout;

    VkDescriptorSet DescSet = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(Dev, &AllocInfo, &DescSet) != VK_SUCCESS)
    {
        vkResetDescriptorPool(Dev, DescriptorPool, 0u);
        if (vkAllocateDescriptorSets(Dev, &AllocInfo, &DescSet) != VK_SUCCESS)
        {
            return false;
        }
    }

    std::vector<VkDescriptorBufferInfo> BufInfos(Bindings.size());
    std::vector<VkWriteDescriptorSet>   Writes(Bindings.size());
    for (std::size_t Idx = 0u; Idx < Bindings.size(); ++Idx)
    {
        BufInfos[Idx].buffer = Bindings[Idx].BufferHandle;
        BufInfos[Idx].offset = 0u;
        BufInfos[Idx].range  = Bindings[Idx].ByteRange;

        Writes[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Idx].dstSet          = DescSet;
        Writes[Idx].dstBinding      = Bindings[Idx].BindingIndex;
        Writes[Idx].descriptorCount = 1u;
        Writes[Idx].descriptorType  = Bindings[Idx].DescriptorKind;
        Writes[Idx].pBufferInfo     = &BufInfos[Idx];
    }

    vkUpdateDescriptorSets(Dev, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);

    vkCmdBindPipeline(CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Prog.Pipeline);
    vkCmdBindDescriptorSets(CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Prog.PipelineLayout, 0u, 1u, &DescSet, 0u, nullptr);
    vkCmdDispatch(CmdBuffer, GroupCountX, GroupCountY, GroupCountZ);

    VkMemoryBarrier ComputeToShaderBarrier {};
    ComputeToShaderBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    ComputeToShaderBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ComputeToShaderBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(
        CmdBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u,
        1u,
        &ComputeToShaderBarrier,
        0u,
        nullptr,
        0u,
        nullptr);

    return true;
}

DeviceBufferResource ComputeDispatchEncoder::AllocateHostWritableBuffer(
    uint64_t           ByteSize,
    VkBufferUsageFlags UsageFlags) const
{
    if (Context == nullptr)
    {
        return {};
    }
    return Context->AllocateBuffer(
        ByteSize,
        UsageFlags | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
}

} // namespace Frontier::GeometricRaster
