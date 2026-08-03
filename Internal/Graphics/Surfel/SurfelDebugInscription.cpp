/*==============================================================================================================================================
                                                         SURFELDEBUGINSCRIPTION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the surfel debug splat. Initialize reads the two SPIR-V modules, builds a two-binding storage set layout (b0 surfel records,
//    b1 the slotting Offsets header), a pipeline layout carrying that set plus the push block, and an alpha-blended POINT-LIST graphics pipeline
//    configured for dynamic rendering into the radiance colour format (no depth attachment, no depth test — the splat is a pure overlay). Refresh
//    points the set at the borrowed pool + slotting buffers. Record binds both, pushes the per-frame constants, and draws Capacity points with no
//    vertex buffer (the vertex shader reads the pool by gl_VertexIndex). Finalize destroys all of it.
//
// 📝 Cloned from GroundGridPass.cpp — the same fullscreen-overlay compositing idiom — with three differences: the topology is POINT_LIST (each surfel
//    is one point sprite, not a fullscreen triangle), the set is two STORAGE buffers rather than a sampled image, and the draw count is the pool
//    capacity rather than 3. Point-sprite gl_PointSize / gl_PointCoord need no device feature on desktop Vulkan (unlike wide LINES), so no feature gate.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Surfel/SurfelDebugInscription.h"
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

// Read a whole SPIR-V file into a byte buffer. Empty on failure (missing / unreadable), which the caller treats as "skip the splat".
std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
{
    std::vector<char> Bytes;
    FILE* Handle = std::fopen(FilePath.c_str(), "rb");
    if (Handle == nullptr)
    {
        ISSUE_CAUTION("surfel-debug", "shader module not found: %s", FilePath.c_str());
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

bool InitializeSurfelDebugInscription(SurfelDebugInscription& Debug,
                                      VulkanHost&             Host,
                                      VkFormat                ColourFormat,
                                      const char*             ShaderDirectory)
{
    Debug.Host           = &Host;
    Debug.ReadyCondition = false;
    if (!Host.DynamicRenderingEnabled || Host.Device == VK_NULL_HANDLE)
        return false;

    // -- Shader modules -------------------------------------------------------------------------------------------------
    const std::string Directory       = ShaderDirectory;
    VkShaderModule    VertexModule     = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/SurfelDebugSplat.vert.spv"));
    VkShaderModule    FragmentModule   = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/SurfelDebugSplat.frag.spv"));
    if (VertexModule == VK_NULL_HANDLE || FragmentModule == VK_NULL_HANDLE)
    {
        if (VertexModule   != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        if (FragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        ISSUE_CAUTION("surfel-debug", "pipeline not built — shader modules unavailable, debug splat will not draw");
        return false;
    }

    auto ReleaseModules = [&]()
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
    };

    // -- Depth sampler (owned): nearest + clamp for the fragment depth-reject; the frag texelFetches, but a valid sampler is still required by the combined
    //    image-sampler descriptor. Nearest/clamp is the safe choice for a depth texture read at integer pixel coordinates.
    VkSamplerCreateInfo SamplerInfo = {};
    SamplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SamplerInfo.magFilter    = VK_FILTER_NEAREST;
    SamplerInfo.minFilter    = VK_FILTER_NEAREST;
    SamplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.maxLod       = 0.0f;
    if (vkCreateSampler(Host.Device, &SamplerInfo, Host.Allocator, &Debug.DepthSampler) != VK_SUCCESS)
    {
        ReleaseModules();
        ISSUE_FAULT("surfel-debug", "depth sampler creation failed");
        return false;
    }

    // -- Descriptor set layout: set 0 b0 = surfel records, b1 = slotting Offsets header, b2 = pool Moments (irradiance modes), b3 = scene depth sampler --
    VkDescriptorSetLayoutBinding Bindings[4] = {};
    Bindings[0].binding         = 0;
    Bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[0].descriptorCount = 1;
    Bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    Bindings[1].binding         = 1;
    Bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[1].descriptorCount = 1;
    Bindings[1].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    Bindings[2].binding         = 2;
    Bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[2].descriptorCount = 1;
    Bindings[2].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;                              // the irradiance modes read it in the vertex stage
    Bindings[3].binding         = 3;
    Bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Bindings[3].descriptorCount = 1;
    Bindings[3].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;                            // the depth-reject samples it in the fragment stage

    VkDescriptorSetLayoutCreateInfo SetLayoutInfo = {};
    SetLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetLayoutInfo.bindingCount = 4;
    SetLayoutInfo.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &SetLayoutInfo, Host.Allocator, &Debug.SetLayout) != VK_SUCCESS)
    {
        vkDestroySampler(Host.Device, Debug.DepthSampler, Host.Allocator);
        Debug.DepthSampler = VK_NULL_HANDLE;
        ReleaseModules();
        ISSUE_FAULT("surfel-debug", "descriptor set layout creation failed");
        return false;
    }

    VkDescriptorPoolSize PoolSizes[2] = {};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSizes[0].descriptorCount = 3;   // b0 surfels, b1 offsets, b2 moments
    PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSizes[1].descriptorCount = 1;   // b3 scene depth

    VkDescriptorPoolCreateInfo PoolInfo = {};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.maxSets       = 1;
    PoolInfo.poolSizeCount = 2;
    PoolInfo.pPoolSizes    = PoolSizes;
    if (vkCreateDescriptorPool(Host.Device, &PoolInfo, Host.Allocator, &Debug.DescriptorPool) != VK_SUCCESS)
    {
        vkDestroySampler(Host.Device, Debug.DepthSampler, Host.Allocator);
        Debug.DepthSampler = VK_NULL_HANDLE;
        ReleaseModules();
        ISSUE_FAULT("surfel-debug", "descriptor pool creation failed");
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocation = {};
    SetAllocation.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetAllocation.descriptorPool     = Debug.DescriptorPool;
    SetAllocation.descriptorSetCount = 1;
    SetAllocation.pSetLayouts        = &Debug.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, &Debug.SplatSet) != VK_SUCCESS)
    {
        vkDestroySampler(Host.Device, Debug.DepthSampler, Host.Allocator);
        Debug.DepthSampler = VK_NULL_HANDLE;
        ReleaseModules();
        ISSUE_FAULT("surfel-debug", "descriptor set allocation failed");
        return false;
    }

    // -- Pipeline layout: the push block plus the storage set -------------------------------------------------------------
    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(SurfelDebugConstants);

    VkPipelineLayoutCreateInfo LayoutInfo = {};
    LayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutInfo.setLayoutCount         = 1;
    LayoutInfo.pSetLayouts            = &Debug.SetLayout;
    LayoutInfo.pushConstantRangeCount = 1;
    LayoutInfo.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &LayoutInfo, Host.Allocator, &Debug.PipelineLayout) != VK_SUCCESS)
    {
        vkDestroySampler(Host.Device, Debug.DepthSampler, Host.Allocator);
        Debug.DepthSampler = VK_NULL_HANDLE;
        ReleaseModules();
        ISSUE_FAULT("surfel-debug", "pipeline layout creation failed");
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

    // -- Fixed-function state: no vertex input, POINT LIST, dynamic viewport/scissor, alpha blend, no depth --------------
    VkPipelineVertexInputStateCreateInfo VertexInput = {};
    VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo InputAssembly = {};
    InputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;   // 🔴 one point sprite per surfel — the whole splat is point sprites

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
    PipelineInfo.layout              = Debug.PipelineLayout;

    VkResult Outcome = vkCreateGraphicsPipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInfo, Host.Allocator, &Debug.Pipeline);

    vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
    vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);

    if (Outcome != VK_SUCCESS)
    {
        vkDestroySampler(Host.Device, Debug.DepthSampler, Host.Allocator);
        Debug.DepthSampler = VK_NULL_HANDLE;
        ISSUE_FAULT("surfel-debug", "graphics pipeline creation failed (VkResult %d)", (int)Outcome);
        return false;
    }

    Debug.ReadyCondition = true;
    ISSUE_NOTICE("surfel-debug", "surfel debug splat ready");
    return true;
}

void RefreshSurfelDebugInscription(SurfelDebugInscription&   Debug,
                                   const SurfelPool&         Pool,
                                   const SurfelGridSlotting& Slotting,
                                   VkImageView               SceneDepthView)
{
    if (!Debug.ReadyCondition || Debug.SplatSet == VK_NULL_HANDLE)
        return;
    if (!Pool.ReadyCondition || Pool.SurfelBuffer == VK_NULL_HANDLE || Pool.MomentsBuffer == VK_NULL_HANDLE)
        return;
    if (!Slotting.ReadyCondition || Slotting.OffsetsBuffer == VK_NULL_HANDLE)
        return;
    if (SceneDepthView == VK_NULL_HANDLE)
        return;   // the depth-reject needs a live scene-depth view; without it the draw would sample an undefined image

    // Latch the capacity every call (cheap) so the draw count tracks the pool even if the descriptor did not change this frame.
    Debug.Capacity = Pool.Capacity;

    if (Debug.BoundSurfelBuffer  == Pool.SurfelBuffer     && Debug.BoundOffsetsBuffer == Slotting.OffsetsBuffer &&
        Debug.BoundMomentsBuffer == Pool.MomentsBuffer    && Debug.BoundDepthView     == SceneDepthView)
        return;   // idempotent: all four already point where they should, so a per-frame call costs four compares

    VkDescriptorBufferInfo SurfelInfo = {};
    SurfelInfo.buffer = Pool.SurfelBuffer;
    SurfelInfo.offset = 0;
    SurfelInfo.range  = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo OffsetsInfo = {};
    OffsetsInfo.buffer = Slotting.OffsetsBuffer;
    OffsetsInfo.offset = 0;
    OffsetsInfo.range  = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo MomentsInfo = {};
    MomentsInfo.buffer = Pool.MomentsBuffer;
    MomentsInfo.offset = 0;
    MomentsInfo.range  = VK_WHOLE_SIZE;

    // The depth image is in SHADER_READ_ONLY at radiance time (TransitionVisibilityDepthForSampling ran in the preamble), so this layout is correct.
    VkDescriptorImageInfo DepthInfo = {};
    DepthInfo.sampler     = Debug.DepthSampler;
    DepthInfo.imageView   = SceneDepthView;
    DepthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet Writes[4] = {};
    Writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[0].dstSet          = Debug.SplatSet;
    Writes[0].dstBinding      = 0;
    Writes[0].descriptorCount = 1;
    Writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[0].pBufferInfo     = &SurfelInfo;
    Writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[1].dstSet          = Debug.SplatSet;
    Writes[1].dstBinding      = 1;
    Writes[1].descriptorCount = 1;
    Writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[1].pBufferInfo     = &OffsetsInfo;
    Writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[2].dstSet          = Debug.SplatSet;
    Writes[2].dstBinding      = 2;
    Writes[2].descriptorCount = 1;
    Writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[2].pBufferInfo     = &MomentsInfo;
    Writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[3].dstSet          = Debug.SplatSet;
    Writes[3].dstBinding      = 3;
    Writes[3].descriptorCount = 1;
    Writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Writes[3].pImageInfo      = &DepthInfo;

    vkUpdateDescriptorSets(Debug.Host->Device, 4, Writes, 0, nullptr);
    Debug.BoundSurfelBuffer  = Pool.SurfelBuffer;
    Debug.BoundOffsetsBuffer = Slotting.OffsetsBuffer;
    Debug.BoundMomentsBuffer = Pool.MomentsBuffer;
    Debug.BoundDepthView     = SceneDepthView;
    Debug.BindingsReady      = true;
}

void RecordSurfelDebugInscription(const SurfelDebugInscription& Debug,
                                  VkExtent2D                    Extent,
                                  const SurfelDebugConstants&   Constants,
                                  VkCommandBuffer               CommandBuffer)
{
    if (!Debug.ReadyCondition)
        return;
    // 🔴 Mode 0 draws NOTHING — the debug view is off by default and costs a single compare when off.
    if (Constants.DebugMode == SurfelDebugModeOff)
        return;
    // The set must be pointed at ALL FOUR real resources (surfels, offsets, moments, depth), or the draw would read undefined memory / sample an
    // undefined depth image. BindingsReady is set only once Refresh wrote b0-b3 against live handles; skip the draw until then.
    if (Debug.SplatSet == VK_NULL_HANDLE || !Debug.BindingsReady || Debug.Capacity == 0)
        return;

    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Debug.PipelineLayout,
                            0, 1, &Debug.SplatSet, 0, nullptr);

    VkViewport ViewportRegion = {};
    ViewportRegion.width    = (float)Extent.width;
    ViewportRegion.height   = (float)Extent.height;
    ViewportRegion.minDepth = 0.0f;
    ViewportRegion.maxDepth = 1.0f;
    vkCmdSetViewport(CommandBuffer, 0, 1, &ViewportRegion);

    VkRect2D Scissor = {};
    Scissor.extent = Extent;
    vkCmdSetScissor(CommandBuffer, 0, 1, &Scissor);

    // The push-block Capacity must equal the draw count so the vertex stage's own bounds guard matches the vertices issued.
    SurfelDebugConstants Effective = Constants;
    Effective.Capacity = Debug.Capacity;

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Debug.Pipeline);
    vkCmdPushConstants(CommandBuffer, Debug.PipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(SurfelDebugConstants), &Effective);
    vkCmdDraw(CommandBuffer, Debug.Capacity, 1, 0, 0);
}

void FinalizeSurfelDebugInscription(SurfelDebugInscription& Debug)
{
    if (Debug.Host == nullptr)
        return;
    VkDevice Device = Debug.Host->Device;

    if (Debug.Pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Device, Debug.Pipeline, Debug.Host->Allocator);
        Debug.Pipeline = VK_NULL_HANDLE;
    }
    if (Debug.PipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(Device, Debug.PipelineLayout, Debug.Host->Allocator);
        Debug.PipelineLayout = VK_NULL_HANDLE;
    }
    // The pool owns SplatSet, so destroying it frees the set — no explicit vkFreeDescriptorSets.
    if (Debug.DescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(Device, Debug.DescriptorPool, Debug.Host->Allocator);
        Debug.DescriptorPool = VK_NULL_HANDLE;
        Debug.SplatSet       = VK_NULL_HANDLE;
    }
    if (Debug.SetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(Device, Debug.SetLayout, Debug.Host->Allocator);
        Debug.SetLayout = VK_NULL_HANDLE;
    }
    if (Debug.DepthSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(Device, Debug.DepthSampler, Debug.Host->Allocator);
        Debug.DepthSampler = VK_NULL_HANDLE;
    }
    Debug.BoundSurfelBuffer  = VK_NULL_HANDLE;
    Debug.BoundOffsetsBuffer = VK_NULL_HANDLE;
    Debug.BoundMomentsBuffer = VK_NULL_HANDLE;
    Debug.BoundDepthView     = VK_NULL_HANDLE;
    Debug.Capacity           = 0;
    Debug.ReadyCondition     = false;
    Debug.BindingsReady      = false;
}

} // namespace Frontier
