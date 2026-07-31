/*==============================================================================================================================================
                                                      RADIANCERESOLVEINSCRIPTION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the radiance resolve. Initialize reads the two SPIR-V modules (the fullscreen-triangle vertex stage is
//    VisibilityInscription's, reused verbatim), builds a one-binding descriptor set layout + pool + set, a linear sampler, a pipeline layout
//    carrying that set and the ResolveConstants push range, and a graphics pipeline configured for dynamic rendering against the SWAPCHAIN colour
//    format. Refresh points the set at the borrowed radiance view. Record binds and draws the three-vertex triangle inside the caller's open
//    swapchain scope.
//
//    The structure deliberately mirrors SurfaceShadeInscription.cpp — same helper shapes, same failure-unwind pattern, same idempotent-Refresh rule.
//    What differs is the binding count (one, not five), no owned buffer, and one load-bearing inversion: blending is DISABLED here. The shade
//    composites onto what is already in the scope; the resolve REPLACES every pixel, because it is the first thing in a scope whose prior contents
//    are undefined.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Render/Radiance/RadianceResolveInscription.h"
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

// The descriptor binding, named so the layout / pool / write code below cannot drift out of step with the shader's declaration.
constexpr uint32_t BindingRadianceBuffer = 0;
constexpr uint32_t BindingCount          = 1;

// Read a whole SPIR-V file into a byte buffer. Empty on failure (missing / unreadable), which the caller treats as "skip".
std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
{
    std::vector<char> Bytes;
    FILE* Handle = std::fopen(FilePath.c_str(), "rb");
    if (Handle == nullptr)
    {
        ISSUE_CAUTION("radiance-resolve", "shader module not found: %s", FilePath.c_str());
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

bool InitializeRadianceResolveInscription(RadianceResolveInscription& Resolve,
                                          VulkanHost&                 Host,
                                          VkFormat                    SwapchainFormat,
                                          const char*                 ShaderDirectory)
{
    Resolve = RadianceResolveInscription{};
    Resolve.Host = &Host;
    if (!Host.DynamicRenderingEnabled || Host.Device == VK_NULL_HANDLE)
        return false;

    // -- Shader modules. The fullscreen-triangle vertex stage is VisibilityInscription's, reused as-is. ---------------------
    const std::string Directory      = ShaderDirectory;
    VkShaderModule    VertexModule   = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/VisibilityInscription.vert.spv"));
    VkShaderModule    FragmentModule = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/RadianceResolve.frag.spv"));
    if (VertexModule == VK_NULL_HANDLE || FragmentModule == VK_NULL_HANDLE)
    {
        if (VertexModule   != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        if (FragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        ISSUE_CAUTION("radiance-resolve", "pipeline not built — shader modules unavailable, the scene will not reach the screen");
        return false;
    }

    // A single unwind path for every failure after the modules exist, so no branch below can leak them.
    auto ReleaseModules = [&]()
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
    };

    // -- Descriptor set layout: b0 = the radiance target, sampled (fragment stage) -----------------------------------------
    VkDescriptorSetLayoutBinding Bindings[BindingCount] = {};
    Bindings[0].binding         = BindingRadianceBuffer;
    Bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Bindings[0].descriptorCount = 1;
    Bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo SetLayoutInfo = {};
    SetLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetLayoutInfo.bindingCount = BindingCount;
    SetLayoutInfo.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &SetLayoutInfo, Host.Allocator, &Resolve.SetLayout) != VK_SUCCESS)
    {
        ReleaseModules();
        ISSUE_FAULT("radiance-resolve", "descriptor set layout creation failed");
        return false;
    }

    // -- Descriptor pool + set (one sampler) -------------------------------------------------------------------------------
    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo PoolInfo = {};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.maxSets       = 1;
    PoolInfo.poolSizeCount = 1;
    PoolInfo.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInfo, Host.Allocator, &Resolve.DescriptorPool) != VK_SUCCESS)
    {
        ReleaseModules();
        FinalizeRadianceResolveInscription(Resolve);
        ISSUE_FAULT("radiance-resolve", "descriptor pool creation failed");
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocate = {};
    SetAllocate.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetAllocate.descriptorPool     = Resolve.DescriptorPool;
    SetAllocate.descriptorSetCount = 1;
    SetAllocate.pSetLayouts        = &Resolve.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocate, &Resolve.ResolveSet) != VK_SUCCESS)
    {
        ReleaseModules();
        FinalizeRadianceResolveInscription(Resolve);
        ISSUE_FAULT("radiance-resolve", "descriptor set allocation failed");
        return false;
    }

    // -- Linear sampler (clamp). Inert today — the shader uses texelFetch — but correct for any future scaled resolve. -----
    VkSamplerCreateInfo SamplerInfo = {};
    SamplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SamplerInfo.magFilter    = VK_FILTER_LINEAR;
    SamplerInfo.minFilter    = VK_FILTER_LINEAR;
    SamplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(Host.Device, &SamplerInfo, Host.Allocator, &Resolve.LinearSampler) != VK_SUCCESS)
    {
        ReleaseModules();
        FinalizeRadianceResolveInscription(Resolve);
        ISSUE_FAULT("radiance-resolve", "linear sampler creation failed");
        return false;
    }

    // -- Pipeline layout: the one set + the ResolveConstants push range (fragment stage) -----------------------------------
    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(RadianceResolveConstants);

    VkPipelineLayoutCreateInfo LayoutInfo = {};
    LayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutInfo.setLayoutCount         = 1;
    LayoutInfo.pSetLayouts            = &Resolve.SetLayout;
    LayoutInfo.pushConstantRangeCount = 1;
    LayoutInfo.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &LayoutInfo, Host.Allocator, &Resolve.PipelineLayout) != VK_SUCCESS)
    {
        ReleaseModules();
        FinalizeRadianceResolveInscription(Resolve);
        ISSUE_FAULT("radiance-resolve", "pipeline layout creation failed");
        return false;
    }

    // -- Shader stages ------------------------------------------------------------------------------------------------------
    VkPipelineShaderStageCreateInfo Stages[2] = {};
    Stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = VertexModule;
    Stages[0].pName  = "main";
    Stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = FragmentModule;
    Stages[1].pName  = "main";

    // -- Fixed-function state: no vertex input, triangle list, dynamic viewport/scissor, NO blend, no depth ------------------
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

    // ⚠️ blendEnable FALSE, unlike every other unit in this family. The resolve is the FIRST draw in the swapchain scope and covers every pixel,
    //    so there is nothing underneath to composite with — the scope's prior contents are undefined. Blending here would read that undefined
    //    destination. All the actual compositing already happened upstream, in linear, inside the radiance target.
    VkPipelineColorBlendAttachmentState BlendAttachment = {};
    BlendAttachment.blendEnable    = VK_FALSE;
    BlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
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

    // -- Dynamic-rendering attachment format. The SWAPCHAIN format: this pipeline WRITES the swapchain and READS the radiance target through the
    //    descriptor set, so the two formats appear in different places and must not be confused. -------------------------------------------------
    VkPipelineRenderingCreateInfoKHR RenderingInfo = {};
    RenderingInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    RenderingInfo.colorAttachmentCount    = 1;
    RenderingInfo.pColorAttachmentFormats = &SwapchainFormat;

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
    PipelineInfo.layout              = Resolve.PipelineLayout;

    VkResult Outcome = vkCreateGraphicsPipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInfo, Host.Allocator, &Resolve.Pipeline);

    ReleaseModules();

    if (Outcome != VK_SUCCESS)
    {
        FinalizeRadianceResolveInscription(Resolve);
        ISSUE_FAULT("radiance-resolve", "graphics pipeline creation failed (VkResult %d)", (int)Outcome);
        return false;
    }

    Resolve.ReadyCondition = true;
    ISSUE_NOTICE("radiance-resolve", "radiance resolve ready");
    return true;
}

void RefreshRadianceResolveInscription(RadianceResolveInscription& Resolve, const RadianceTarget& Target)
{
    if (!Resolve.ReadyCondition || Resolve.ResolveSet == VK_NULL_HANDLE)
        return;
    if (!Target.ReadyCondition || Target.ColourView == VK_NULL_HANDLE)
        return;

    // Idempotent, for the same reason SurfaceShadeInscription's Refresh is: this set is bound every frame, so rewriting it when nothing changed
    // would trip the "descriptor in use by a pending command buffer" rule for no benefit. Only bring-up and resize actually differ.
    if (Resolve.BoundRadianceView == Target.ColourView)
        return;

    VkDescriptorImageInfo ImageInfo = {};
    ImageInfo.sampler     = Resolve.LinearSampler;
    ImageInfo.imageView   = Target.ColourView;
    ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet Write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    Write.dstSet          = Resolve.ResolveSet;
    Write.dstBinding      = BindingRadianceBuffer;
    Write.descriptorCount = 1;
    Write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Write.pImageInfo      = &ImageInfo;
    vkUpdateDescriptorSets(Resolve.Host->Device, 1, &Write, 0, nullptr);

    Resolve.BoundRadianceView = Target.ColourView;
}

void RecordRadianceResolveInscription(const RadianceResolveInscription& Resolve,
                                      VkExtent2D                        Extent,
                                      const RadianceResolveConstants&   Constants,
                                      VkCommandBuffer                   CommandBuffer)
{
    if (!Resolve.ReadyCondition || Resolve.ResolveSet == VK_NULL_HANDLE)
        return;
    // Nothing has been bound yet (Refresh has not run, or ran before the target existed) — recording now would read an undefined descriptor.
    if (Resolve.BoundRadianceView == VK_NULL_HANDLE)
        return;

    VkViewport ViewportRegion = {};
    ViewportRegion.width    = (float)Extent.width;
    ViewportRegion.height   = (float)Extent.height;
    ViewportRegion.minDepth = 0.0f;
    ViewportRegion.maxDepth = 1.0f;
    vkCmdSetViewport(CommandBuffer, 0, 1, &ViewportRegion);

    VkRect2D Scissor = {};
    Scissor.extent = Extent;
    vkCmdSetScissor(CommandBuffer, 0, 1, &Scissor);

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Resolve.Pipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Resolve.PipelineLayout,
                            0, 1, &Resolve.ResolveSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Resolve.PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(RadianceResolveConstants), &Constants);
    vkCmdDraw(CommandBuffer, 3, 1, 0, 0);
}

void FinalizeRadianceResolveInscription(RadianceResolveInscription& Resolve)
{
    if (Resolve.Host == nullptr || Resolve.Host->Device == VK_NULL_HANDLE)
    {
        Resolve = RadianceResolveInscription{};
        return;
    }
    VkDevice                     Device    = Resolve.Host->Device;
    const VkAllocationCallbacks* Allocator = Resolve.Host->Allocator;

    if (Resolve.Pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(Device, Resolve.Pipeline, Allocator);
    if (Resolve.PipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(Device, Resolve.PipelineLayout, Allocator);
    if (Resolve.LinearSampler != VK_NULL_HANDLE)
        vkDestroySampler(Device, Resolve.LinearSampler, Allocator);
    if (Resolve.DescriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(Device, Resolve.DescriptorPool, Allocator);   // frees ResolveSet
    if (Resolve.SetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(Device, Resolve.SetLayout, Allocator);

    Resolve = RadianceResolveInscription{};
}

} // namespace Frontier
