/*==============================================================================================================================================
                                                      SELECTIONOUTLINEINSCRIPTION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the object-selection outline composite. Initialize reads the two SPIR-V modules, builds a one-binding set layout (combined
//    image sampler, fragment stage) + a pipeline layout carrying the OutlineConstants push range, a point sampler, and a graphics pipeline configured
//    for dynamic rendering against the swapchain colour format (no vertex input, alpha-over blend, no depth). Refresh re-points the set at the
//    borrowed visibility image's view. Record binds and draws the fullscreen triangle. Structurally a sibling of VisibilityInscription.cpp — same
//    idiom, own pipeline, so the two composites can be toggled independently.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Visibility/SelectionOutlineInscription.h"

// 📝 The header declares nothing unless FRONTIER_POLYGON_AUTHORING is set, so the whole translation unit collapses to empty with it
//    off. The include above stays outside the guard so the flag is picked up from it if a build defines it there.
#ifdef FRONTIER_POLYGON_AUTHORING

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

// Read a whole SPIR-V file into a byte buffer. Empty on failure (missing / unreadable).
std::vector<char> RetrieveOutlineShaderBytes(const std::string& FilePath)
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

// Wrap a SPIR-V byte buffer in a VkShaderModule. VK_NULL_HANDLE on failure.
VkShaderModule ConstructOutlineShaderModule(const VulkanHost& Host, const std::vector<char>& Bytes)
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

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSelectionOutlineInscription(SelectionOutlineInscription& Inscription,
                                           VulkanHost&                  Host,
                                           VkFormat                     ColourFormat,
                                           const char*                  ShaderDirectory)
{
    Inscription = SelectionOutlineInscription{};
    Inscription.Host = &Host;
    if (!Host.DynamicRenderingEnabled || Host.Device == VK_NULL_HANDLE)
    {
        ISSUE_CAUTION("selection-outline", "dynamic rendering unavailable - outline will not draw");
        return false;
    }

    // -- Shader modules -------------------------------------------------------------------------------------------------
    const std::string Directory      = ShaderDirectory;
    VkShaderModule    VertexModule   = ConstructOutlineShaderModule(Host, RetrieveOutlineShaderBytes(Directory + "/SelectionOutline.vert.spv"));
    VkShaderModule    FragmentModule = ConstructOutlineShaderModule(Host, RetrieveOutlineShaderBytes(Directory + "/SelectionOutline.frag.spv"));
    if (VertexModule == VK_NULL_HANDLE || FragmentModule == VK_NULL_HANDLE)
    {
        if (VertexModule   != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        if (FragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        ISSUE_CAUTION("selection-outline", "pipeline not built - shader modules unavailable, outline will not draw");
        return false;
    }

    // -- Descriptor set layout: binding 0 = id image, binding 1 = scene depth (both fragment-stage combined image samplers) --
    VkDescriptorSetLayoutBinding Bindings[2] = {};
    Bindings[0].binding         = 0;
    Bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Bindings[0].descriptorCount = 1;
    Bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[1].binding         = 1;
    Bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Bindings[1].descriptorCount = 1;
    Bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo SetLayoutInfo = {};
    SetLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetLayoutInfo.bindingCount = 2;
    SetLayoutInfo.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &SetLayoutInfo, Host.Allocator, &Inscription.SetLayout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        ISSUE_FAULT("selection-outline", "descriptor set layout creation failed");
        return false;
    }

    // -- Descriptor pool + set (TWO combined image samplers in one set: id + depth) ---------------------------------------
    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSize.descriptorCount = 2;

    VkDescriptorPoolCreateInfo PoolInfo = {};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.maxSets       = 1;
    PoolInfo.poolSizeCount = 1;
    PoolInfo.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInfo, Host.Allocator, &Inscription.DescriptorPool) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        FinalizeSelectionOutlineInscription(Inscription);
        ISSUE_FAULT("selection-outline", "descriptor pool creation failed");
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocateInfo = {};
    SetAllocateInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetAllocateInfo.descriptorPool     = Inscription.DescriptorPool;
    SetAllocateInfo.descriptorSetCount = 1;
    SetAllocateInfo.pSetLayouts        = &Inscription.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocateInfo, &Inscription.ImageSet) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        FinalizeSelectionOutlineInscription(Inscription);
        ISSUE_FAULT("selection-outline", "descriptor set allocation failed");
        return false;
    }

    // -- Point sampler (nearest / clamp): the packed id must not be filtered ---------------------------------------------
    // ⚠️ A LINEAR filter here would average two unrelated partition ordinals into a third, meaningless one — producing phantom borders in the
    //    interior of objects. NEAREST is a correctness requirement, not a quality choice.
    VkSamplerCreateInfo SamplerInfo = {};
    SamplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SamplerInfo.magFilter    = VK_FILTER_NEAREST;
    SamplerInfo.minFilter    = VK_FILTER_NEAREST;
    SamplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(Host.Device, &SamplerInfo, Host.Allocator, &Inscription.PointSampler) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        FinalizeSelectionOutlineInscription(Inscription);
        ISSUE_FAULT("selection-outline", "point sampler creation failed");
        return false;
    }

    // -- Pipeline layout: the one sampler set + the OutlineConstants push range (fragment stage) -------------------------
    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(SelectionOutlineConstants);

    VkPipelineLayoutCreateInfo LayoutInfo = {};
    LayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutInfo.setLayoutCount         = 1;
    LayoutInfo.pSetLayouts            = &Inscription.SetLayout;
    LayoutInfo.pushConstantRangeCount = 1;
    LayoutInfo.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &LayoutInfo, Host.Allocator, &Inscription.PipelineLayout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        FinalizeSelectionOutlineInscription(Inscription);
        ISSUE_FAULT("selection-outline", "pipeline layout creation failed");
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

    // -- Fixed-function state: no vertex input, triangle list, dynamic viewport/scissor, alpha-over blend, no depth ------
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

    // 📝 Alpha-over blend so the hover ring's partial alpha reads as a tint over the shaded frame rather than replacing it. The fragment stage
    //    discards every non-ring pixel, so the blend only ever runs on the ring itself.
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
    PipelineInfo.layout              = Inscription.PipelineLayout;

    VkResult Outcome = vkCreateGraphicsPipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInfo, Host.Allocator, &Inscription.Pipeline);

    vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
    vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);

    if (Outcome != VK_SUCCESS)
    {
        FinalizeSelectionOutlineInscription(Inscription);
        ISSUE_FAULT("selection-outline", "graphics pipeline creation failed (VkResult %d)", (int)Outcome);
        return false;
    }

    Inscription.ReadyCondition = true;
    ISSUE_NOTICE("selection-outline", "outline inscription ready");
    return true;
}

void RefreshSelectionOutlineInscription(SelectionOutlineInscription& Inscription,
                                        const VisibilityImage&       Image,
                                        const VisibilityDepth&       Depth)
{
    if (!Inscription.ReadyCondition || Inscription.ImageSet == VK_NULL_HANDLE)
        return;
    if (!Image.ReadyCondition || Image.IdView   == VK_NULL_HANDLE)
        return;
    // 📝 Depth is mandatory (the border test cannot classify an edge without it), so a not-ready depth target leaves BOTH bindings alone. Record then
    //    self-skips on BoundIdView being null rather than sampling binding 1 as undefined.
    if (!Depth.ReadyCondition || Depth.DepthView == VK_NULL_HANDLE)
        return;
    // 📝 Cheap to call every frame: the write is skipped unless a view handle actually changed (a resize rebuilt them).
    if (Inscription.BoundIdView == Image.IdView && Inscription.BoundDepthView == Depth.DepthView)
        return;

    VkDescriptorImageInfo IdInfo = {};
    IdInfo.sampler     = Inscription.PointSampler;
    IdInfo.imageView   = Image.IdView;
    IdInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo DepthInfo = {};
    DepthInfo.sampler     = Inscription.PointSampler;
    DepthInfo.imageView   = Depth.DepthView;
    // ⚠️ SHADER_READ_ONLY_OPTIMAL, *not* DEPTH_STENCIL_READ_ONLY_OPTIMAL. The intuitive choice for a depth aspect is the latter, and it is wrong here:
    //    the preamble's TransitionVisibilityDepthForSampling moves this image to SHADER_READ_ONLY_OPTIMAL for the HiZ reduce, and nothing moves it back
    //    before the colour scope. The descriptor must name the layout the image is ACTUALLY in, or every draw trips VUID-vkCmdDraw-imageLayout-00344
    //    and the sampled depth cannot be trusted. Read VisibilityDepth.cpp before changing this, not intuition.
    DepthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet Writes[2] = {};
    Writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[0].dstSet          = Inscription.ImageSet;
    Writes[0].dstBinding      = 0;
    Writes[0].descriptorCount = 1;
    Writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Writes[0].pImageInfo      = &IdInfo;
    Writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[1].dstSet          = Inscription.ImageSet;
    Writes[1].dstBinding      = 1;
    Writes[1].descriptorCount = 1;
    Writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Writes[1].pImageInfo      = &DepthInfo;

    vkUpdateDescriptorSets(Inscription.Host->Device, 2, Writes, 0, nullptr);
    Inscription.BoundIdView    = Image.IdView;
    Inscription.BoundDepthView = Depth.DepthView;
    ISSUE_TRACE("selection-outline", "descriptors re-pointed at id + depth views");
}

void RecordSelectionOutlineInscription(const SelectionOutlineInscription& Inscription,
                                       VkExtent2D                         Extent,
                                       const SelectionOutlineConstants&   Constants,
                                       VkCommandBuffer                    CommandBuffer)
{
    if (!Inscription.ReadyCondition || Inscription.Pipeline == VK_NULL_HANDLE || Inscription.ImageSet == VK_NULL_HANDLE)
        return;
    if (Inscription.BoundIdView == VK_NULL_HANDLE || Inscription.BoundDepthView == VK_NULL_HANDLE)
        return;   // 📝 never refreshed against live images — sampling an unbound descriptor is undefined
    // 📝 Nothing selected AND nothing hovered: every pixel would discard, so skip the draw entirely rather than burn a fullscreen pass.
    if (Constants.SelectedPartition == NoSelectionSentinel && Constants.HoveredPartition == NoSelectionSentinel)
        return;

    VkViewport ViewportRegion = {};
    ViewportRegion.x        = 0.0f;
    ViewportRegion.y        = 0.0f;
    ViewportRegion.width    = (float)Extent.width;
    ViewportRegion.height   = (float)Extent.height;
    ViewportRegion.minDepth = 0.0f;
    ViewportRegion.maxDepth = 1.0f;

    VkRect2D ScissorRegion = {};
    ScissorRegion.offset = { 0, 0 };
    ScissorRegion.extent = Extent;

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Inscription.Pipeline);
    vkCmdSetViewport(CommandBuffer, 0, 1, &ViewportRegion);
    vkCmdSetScissor(CommandBuffer, 0, 1, &ScissorRegion);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Inscription.PipelineLayout, 0, 1, &Inscription.ImageSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Inscription.PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SelectionOutlineConstants), &Constants);
    vkCmdDraw(CommandBuffer, 3, 1, 0, 0);
}

void FinalizeSelectionOutlineInscription(SelectionOutlineInscription& Inscription)
{
    if (Inscription.Host != nullptr && Inscription.Host->Device != VK_NULL_HANDLE)
    {
        VkDevice               Device    = Inscription.Host->Device;
        const VkAllocationCallbacks* Allocator = Inscription.Host->Allocator;

        if (Inscription.Pipeline       != VK_NULL_HANDLE) vkDestroyPipeline(Device, Inscription.Pipeline, Allocator);
        if (Inscription.PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Inscription.PipelineLayout, Allocator);
        if (Inscription.PointSampler   != VK_NULL_HANDLE) vkDestroySampler(Device, Inscription.PointSampler, Allocator);
        // 📝 The set is freed with the pool; no separate vkFreeDescriptorSets.
        if (Inscription.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Inscription.DescriptorPool, Allocator);
        if (Inscription.SetLayout      != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Inscription.SetLayout, Allocator);
    }

    Inscription = SelectionOutlineInscription{};
}

} // namespace Frontier

#endif // FRONTIER_POLYGON_AUTHORING
