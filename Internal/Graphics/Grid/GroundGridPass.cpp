/*==============================================================================================================================================
                                                             GROUNDGRIDPASS.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the analytic ground-grid pass. Initialize reads the two SPIR-V modules, builds the depth-sampler set layout + pool +
//    sampler, a pipeline layout carrying that set plus the push block, and an alpha-blended fullscreen-triangle graphics pipeline configured for
//    dynamic rendering (VkPipelineRenderingCreateInfoKHR carries the swapchain colour format — no VkRenderPass object). Refresh points the set at
//    the renderer-owned scene depth. Record binds both, pushes the per-frame constants, and issues a three-vertex draw with no vertex buffer (the
//    vertex shader synthesizes the triangle from gl_VertexIndex). Finalize destroys all of it.
//
// 📝 The pass reads depth but still writes NONE and owns no depth attachment: the occlusion test is a manual compare in the fragment shader
//    against the depth the visibility raster already produced. That keeps the grid a pure colour composite (so it needs no depth attachment in
//    the swapchain scope, which has none) while still respecting the geometry standing in front of it.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Grid/GroundGridPass.h"
#include "Graphics/RenderExtension/Diagnostics/DiagnosticArchive.h"

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

// Read a whole SPIR-V file into a byte buffer. Empty on failure (missing / unreadable), which the caller treats as "skip grid".
std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
{
    std::vector<char> Bytes;
    FILE* Handle = std::fopen(FilePath.c_str(), "rb");
    if (Handle == nullptr)
    {
        ISSUE_CAUTION("ground-grid", "shader module not found: %s", FilePath.c_str());
        return Bytes;
    }
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

// Wrap a SPIR-V byte buffer in a VkShaderModule. VK_NULL_HANDLE on failure.
VkShaderModule ConstructShaderModule(const VulkanHost& Host, const std::vector<char>& Bytes)
{
    if (Bytes.empty() || (Bytes.size() % 4) != 0)
        return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo ModuleInfo = {};
    ModuleInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ModuleInfo.codeSize = Bytes.size();
    ModuleInfo.pCode    = reinterpret_cast<const uint32_t*>(Bytes.data());

    VkShaderModule Module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(Host.Device, &ModuleInfo, Host.Allocator, &Module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return Module;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeGroundGridPass(GroundGridPass& Pass,
                              const VulkanHost& Host,
                              VkFormat          ColourFormat,
                              const char*       ShaderDirectory)
{
    Pass.ReadyCondition = false;
    if (!Host.DynamicRenderingEnabled || Host.Device == VK_NULL_HANDLE)
        return false;

    // -- Shader modules -------------------------------------------------------------------------------------------------
    const std::string Directory   = ShaderDirectory;
    VkShaderModule    VertexModule   = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/AnalyticGroundPlane.vert.spv"));
    VkShaderModule    FragmentModule = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/AnalyticGroundPlane.frag.spv"));
    if (VertexModule == VK_NULL_HANDLE || FragmentModule == VK_NULL_HANDLE)
    {
        if (VertexModule   != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        if (FragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        ISSUE_CAUTION("ground-grid", "pipeline not built — shader modules unavailable, grid will not draw");
        return false;
    }

    // Unwind helper so every failure path below releases the modules exactly once.
    auto ReleaseModules = [&]()
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
    };

    // -- Descriptor set layout: set 0 b0 = the scene depth the occlusion test samples --------------------------------------
    VkDescriptorSetLayoutBinding DepthBinding = {};
    DepthBinding.binding         = 0;
    DepthBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    DepthBinding.descriptorCount = 1;
    DepthBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo SetLayoutInfo = {};
    SetLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetLayoutInfo.bindingCount = 1;
    SetLayoutInfo.pBindings    = &DepthBinding;
    if (vkCreateDescriptorSetLayout(Host.Device, &SetLayoutInfo, Host.Allocator, &Pass.SetLayout) != VK_SUCCESS)
    {
        ReleaseModules();
        ISSUE_FAULT("ground-grid", "descriptor set layout creation failed");
        return false;
    }

    // 📝 NEAREST + CLAMP_TO_EDGE, and the filtering is INERT by design: the shader texelFetches, which ignores sampler filtering
    //    entirely. It is specified as nearest anyway so that the descriptor never disagrees with how the shader reads it — a linear
    //    depth sampler would silently become wrong the moment someone switched to texture(), because averaging two depths produces a
    //    distance where no surface exists.
    VkSamplerCreateInfo SamplerInfo = {};
    SamplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SamplerInfo.magFilter    = VK_FILTER_NEAREST;
    SamplerInfo.minFilter    = VK_FILTER_NEAREST;
    SamplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.maxLod       = 0.0f;
    if (vkCreateSampler(Host.Device, &SamplerInfo, Host.Allocator, &Pass.PointSampler) != VK_SUCCESS)
    {
        ReleaseModules();
        ISSUE_FAULT("ground-grid", "depth sampler creation failed");
        return false;
    }

    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo PoolInfo = {};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.maxSets       = 1;
    PoolInfo.poolSizeCount = 1;
    PoolInfo.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInfo, Host.Allocator, &Pass.DescriptorPool) != VK_SUCCESS)
    {
        ReleaseModules();
        ISSUE_FAULT("ground-grid", "descriptor pool creation failed");
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocation = {};
    SetAllocation.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetAllocation.descriptorPool     = Pass.DescriptorPool;
    SetAllocation.descriptorSetCount = 1;
    SetAllocation.pSetLayouts        = &Pass.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, &Pass.DepthSet) != VK_SUCCESS)
    {
        ReleaseModules();
        ISSUE_FAULT("ground-grid", "descriptor set allocation failed");
        return false;
    }

    // -- Pipeline layout: the push-constant block plus the depth set ------------------------------------------------------
    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(GroundGridConstants);

    VkPipelineLayoutCreateInfo LayoutInfo = {};
    LayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutInfo.setLayoutCount         = 1;
    LayoutInfo.pSetLayouts            = &Pass.SetLayout;
    LayoutInfo.pushConstantRangeCount = 1;
    LayoutInfo.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &LayoutInfo, Host.Allocator, &Pass.PipelineLayout) != VK_SUCCESS)
    {
        ReleaseModules();
        ISSUE_FAULT("ground-grid", "pipeline layout creation failed");
        return false;
    }

    // -- Shader stages --------------------------------------------------------------------------------------------------
    VkPipelineShaderStageCreateInfo Stages[2] = {};
    Stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = VertexModule;
    Stages[0].pName  = "main";
    Stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = FragmentModule;
    Stages[1].pName  = "main";

    // -- Fixed-function state: no vertex input, triangle list, dynamic viewport/scissor, alpha blend, no depth ----------
    VkPipelineVertexInputStateCreateInfo VertexInput = {};
    VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo InputAssembly = {};
    InputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo Viewport = {};
    Viewport.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    Viewport.viewportCount = 1;
    Viewport.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo Rasterization = {};
    Rasterization.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    Rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    Rasterization.cullMode    = VK_CULL_MODE_NONE;
    Rasterization.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Rasterization.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample = {};
    Multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState BlendAttachment = {};
    BlendAttachment.blendEnable         = VK_TRUE;
    BlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    BlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    BlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
    BlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    BlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    BlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
    BlendAttachment.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo ColorBlend = {};
    ColorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ColorBlend.attachmentCount = 1;
    ColorBlend.pAttachments    = &BlendAttachment;

    VkDynamicState DynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo Dynamic = {};
    Dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    Dynamic.dynamicStateCount = 2;
    Dynamic.pDynamicStates    = DynamicStates;

    // -- Dynamic-rendering attachment format (replaces a VkRenderPass) --------------------------------------------------
    VkPipelineRenderingCreateInfoKHR RenderingInfo = {};
    RenderingInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    RenderingInfo.colorAttachmentCount    = 1;
    RenderingInfo.pColorAttachmentFormats = &ColourFormat;

    VkGraphicsPipelineCreateInfo PipelineInfo = {};
    PipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    PipelineInfo.pNext               = &RenderingInfo;
    PipelineInfo.stageCount          = 2;
    PipelineInfo.pStages             = Stages;
    PipelineInfo.pVertexInputState   = &VertexInput;
    PipelineInfo.pInputAssemblyState = &InputAssembly;
    PipelineInfo.pViewportState      = &Viewport;
    PipelineInfo.pRasterizationState = &Rasterization;
    PipelineInfo.pMultisampleState   = &Multisample;
    PipelineInfo.pColorBlendState    = &ColorBlend;
    PipelineInfo.pDynamicState       = &Dynamic;
    PipelineInfo.layout              = Pass.PipelineLayout;

    VkResult Outcome = vkCreateGraphicsPipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInfo, Host.Allocator, &Pass.Pipeline);

    vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
    vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);

    if (Outcome != VK_SUCCESS)
    {
        ISSUE_FAULT("ground-grid", "graphics pipeline creation failed (VkResult %d)", (int)Outcome);
        return false;
    }

    Pass.ReadyCondition = true;
    ISSUE_NOTICE("ground-grid", "grid pass ready");
    return true;
}

void RefreshGroundGridPass(GroundGridPass& Pass, const VulkanHost& Host, const VisibilityDepth& Depth)
{
    if (!Pass.ReadyCondition || Pass.DepthSet == VK_NULL_HANDLE)
        return;
    if (!Depth.ReadyCondition || Depth.DepthView == VK_NULL_HANDLE)
        return;
    if (Pass.BoundDepthView == Depth.DepthView)
        return;   // idempotent: the set already points at this view, so a per-frame call costs one compare

    VkDescriptorImageInfo DepthInfo = {};
    DepthInfo.sampler     = Pass.PointSampler;
    DepthInfo.imageView   = Depth.DepthView;
    DepthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet Write = {};
    Write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Write.dstSet          = Pass.DepthSet;
    Write.dstBinding      = 0;
    Write.descriptorCount = 1;
    Write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Write.pImageInfo      = &DepthInfo;

    vkUpdateDescriptorSets(Host.Device, 1, &Write, 0, nullptr);
    Pass.BoundDepthView = Depth.DepthView;
}

void RecordGroundGridPass(const GroundGridPass&      Pass,
                          VkCommandBuffer            CommandBuffer,
                          VkExtent2D                 Extent,
                          const GroundGridConstants& Constants)
{
    if (!Pass.ReadyCondition)
        return;

    // 🔴 The depth test is forced OFF when the set was never pointed at a real view (Refresh not yet called, or the depth target
    //    failed to build). Without this the shader would sample an unwritten descriptor and occlude the grid by garbage — and the
    //    symptom would be a MISSING grid, which reads as "the grid pass is broken" rather than "the depth wiring is missing". The
    //    local copy also keeps the caller's constants untouched, so the caller's own DepthTestEnabled intent is never silently edited.
    const bool DepthReadable = Pass.DepthSet != VK_NULL_HANDLE && Pass.BoundDepthView != VK_NULL_HANDLE;

    GroundGridConstants Effective = Constants;
    if (!DepthReadable)
        Effective.DepthTestEnabled = 0.0f;

    // ⚠️ The set is bound whenever it is valid, even when DepthTestEnabled is 0: the fragment shader holds a static reference to the
    //    sampler, and a driver may treat that as accessed regardless of the branch guarding it, so an unbound set 0 risks a
    //    validation error on a draw that never reads depth.
    if (DepthReadable)
        vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pass.PipelineLayout,
                                0, 1, &Pass.DepthSet, 0, nullptr);

    VkViewport ViewportRegion = {};
    ViewportRegion.width    = (float)Extent.width;
    ViewportRegion.height   = (float)Extent.height;
    ViewportRegion.minDepth = 0.0f;
    ViewportRegion.maxDepth = 1.0f;
    vkCmdSetViewport(CommandBuffer, 0, 1, &ViewportRegion);

    VkRect2D Scissor = {};
    Scissor.extent = Extent;
    vkCmdSetScissor(CommandBuffer, 0, 1, &Scissor);

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pass.Pipeline);
    vkCmdPushConstants(CommandBuffer, Pass.PipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(GroundGridConstants), &Effective);
    vkCmdDraw(CommandBuffer, 3, 1, 0, 0);
}

void FinalizeGroundGridPass(GroundGridPass& Pass, const VulkanHost& Host)
{
    if (Pass.Pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Host.Device, Pass.Pipeline, Host.Allocator);
        Pass.Pipeline = VK_NULL_HANDLE;
    }
    if (Pass.PipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(Host.Device, Pass.PipelineLayout, Host.Allocator);
        Pass.PipelineLayout = VK_NULL_HANDLE;
    }
    // The pool owns DepthSet, so destroying it frees the set — no explicit vkFreeDescriptorSets.
    if (Pass.DescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(Host.Device, Pass.DescriptorPool, Host.Allocator);
        Pass.DescriptorPool = VK_NULL_HANDLE;
        Pass.DepthSet       = VK_NULL_HANDLE;
    }
    if (Pass.SetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(Host.Device, Pass.SetLayout, Host.Allocator);
        Pass.SetLayout = VK_NULL_HANDLE;
    }
    if (Pass.PointSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(Host.Device, Pass.PointSampler, Host.Allocator);
        Pass.PointSampler = VK_NULL_HANDLE;
    }
    Pass.BoundDepthView = VK_NULL_HANDLE;
    Pass.ReadyCondition = false;
}

} // namespace Frontier
