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

// 📝 First memory type allowed by the requirement bitmask carrying every required property bit — mirrors VisibilityDepth's selector.
uint32_t SelectMemoryTypeIndex(VkPhysicalDevice      PhysicalDevice,
                               uint32_t              CompatibleTypesBitmask,
                               VkMemoryPropertyFlags RequiredProperties,
                               bool&                 FoundEnabled)
{
    VkPhysicalDeviceMemoryProperties MemoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);
    for (uint32_t IndexIterator = 0; IndexIterator < MemoryProperties.memoryTypeCount; ++IndexIterator)
    {
        const bool TypeCompatible = (CompatibleTypesBitmask & (1u << IndexIterator)) != 0;
        const bool PropertyMatch  = (MemoryProperties.memoryTypes[IndexIterator].propertyFlags & RequiredProperties) == RequiredProperties;
        if (TypeCompatible && PropertyMatch) { FoundEnabled = true; return IndexIterator; }
    }
    FoundEnabled = false;
    return 0;
}

// Build the 1x1 D32 placeholder image + device-local memory + depth view so the depth set can be WRITTEN at init, before any real scene depth
// exists. SAMPLED so the descriptor is legal (never actually read — DepthTestEnabled stays 0 while it is bound). The contents are left undefined;
// that is fine because the shader never samples it. On any failure every out handle is left null and the caller falls back to leaving the set
// unwritten (which reintroduces 08114, so init treats that as a hard fault).
bool ConstructPlaceholderDepth(const VulkanHost& Host,
                               VkImage&          OutImage,
                               VkDeviceMemory&   OutMemory,
                               VkImageView&      OutView)
{
    OutImage  = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;
    OutView   = VK_NULL_HANDLE;

    VkImageCreateInfo ImageInformation = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ImageInformation.imageType     = VK_IMAGE_TYPE_2D;
    ImageInformation.format        = VK_FORMAT_D32_SFLOAT;
    ImageInformation.extent        = { 1, 1, 1 };
    ImageInformation.mipLevels     = 1;
    ImageInformation.arrayLayers   = 1;
    ImageInformation.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageInformation.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageInformation.usage         = VK_IMAGE_USAGE_SAMPLED_BIT;
    ImageInformation.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageInformation.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(Host.Device, &ImageInformation, Host.Allocator, &OutImage) != VK_SUCCESS)
    {
        OutImage = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetImageMemoryRequirements(Host.Device, OutImage, &MemoryRequirements);

    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice,
                                                           MemoryRequirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                           MemoryTypeFound);
    if (!MemoryTypeFound)
    {
        vkDestroyImage(Host.Device, OutImage, Host.Allocator);
        OutImage = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &OutMemory) != VK_SUCCESS ||
        vkBindImageMemory(Host.Device, OutImage, OutMemory, 0) != VK_SUCCESS)
    {
        if (OutMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
        vkDestroyImage(Host.Device, OutImage, Host.Allocator);
        OutImage  = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        return false;
    }

    VkImageViewCreateInfo ViewInformation = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ViewInformation.image                       = OutImage;
    ViewInformation.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    ViewInformation.format                      = VK_FORMAT_D32_SFLOAT;
    ViewInformation.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    ViewInformation.subresourceRange.levelCount = 1;
    ViewInformation.subresourceRange.layerCount = 1;
    if (vkCreateImageView(Host.Device, &ViewInformation, Host.Allocator, &OutView) != VK_SUCCESS)
    {
        vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
        vkDestroyImage(Host.Device, OutImage, Host.Allocator);
        OutImage  = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        OutView   = VK_NULL_HANDLE;
        return false;
    }

    // 🔴 Move the placeholder from UNDEFINED to SHADER_READ_ONLY_OPTIMAL — the layout the descriptor is written with. The fragment shader guards
    //    its texelFetch behind DepthTestEnabled, which is 0 while the placeholder is bound, but the validator's descriptor-access check
    //    (VUID-vkCmdDraw-None-09600) treats a statically-referenced sampler as accessed and demands its image already be in the written layout —
    //    a runtime-uniform branch does not prove the access dead. So a one-time transition on a transient pool + fence is required, not optional.
    //    Failure here is fatal for the same reason the image is: an untransitioned placeholder trips 09600 on the first draw.
    VkCommandPool TransitionPool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo PoolCreate = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    PoolCreate.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    PoolCreate.queueFamilyIndex = Host.GraphicsQueueFamily;
    bool TransitionOk = vkCreateCommandPool(Host.Device, &PoolCreate, Host.Allocator, &TransitionPool) == VK_SUCCESS;

    VkCommandBuffer TransitionCommand = VK_NULL_HANDLE;
    if (TransitionOk)
    {
        VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        CommandAllocate.commandPool        = TransitionPool;
        CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        CommandAllocate.commandBufferCount = 1;
        TransitionOk = vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &TransitionCommand) == VK_SUCCESS;
    }
    if (TransitionOk)
    {
        VkCommandBufferBeginInfo Begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(TransitionCommand, &Begin);

        VkImageMemoryBarrier ToReadOnly = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        ToReadOnly.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
        ToReadOnly.newLayout                   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ToReadOnly.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        ToReadOnly.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        ToReadOnly.image                       = OutImage;
        ToReadOnly.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        ToReadOnly.subresourceRange.levelCount = 1;
        ToReadOnly.subresourceRange.layerCount = 1;
        ToReadOnly.srcAccessMask               = 0;
        ToReadOnly.dstAccessMask               = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(TransitionCommand,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &ToReadOnly);
        vkEndCommandBuffer(TransitionCommand);

        VkSubmitInfo Submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        Submit.commandBufferCount = 1;
        Submit.pCommandBuffers    = &TransitionCommand;
        TransitionOk = vkQueueSubmit(Host.GraphicsQueue, 1, &Submit, VK_NULL_HANDLE) == VK_SUCCESS;
        if (TransitionOk)
            vkQueueWaitIdle(Host.GraphicsQueue);   // init-time only: safe to stall until the one-shot transition retires
    }
    if (TransitionPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(Host.Device, TransitionPool, Host.Allocator);   // frees TransitionCommand with it

    if (!TransitionOk)
    {
        vkDestroyImageView(Host.Device, OutView, Host.Allocator);
        vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
        vkDestroyImage(Host.Device, OutImage, Host.Allocator);
        OutImage  = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        OutView   = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Point DepthSet binding 0 at DepthView with the SHADER_READ_ONLY layout the shader expects. Shared by the init placeholder write and Refresh.
void WriteDepthDescriptor(const VulkanHost& Host, VkDescriptorSet Set, VkSampler Sampler, VkImageView DepthView)
{
    VkDescriptorImageInfo DepthInfo = {};
    DepthInfo.sampler     = Sampler;
    DepthInfo.imageView   = DepthView;
    DepthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet Write = {};
    Write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Write.dstSet          = Set;
    Write.dstBinding      = 0;
    Write.descriptorCount = 1;
    Write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Write.pImageInfo      = &DepthInfo;

    vkUpdateDescriptorSets(Host.Device, 1, &Write, 0, nullptr);
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

    // 🔴 Seat binding 0 with an owned 1x1 placeholder NOW, so the set is not merely bound but WRITTEN on the very first draw. The fragment shader
    //    statically references set-0 binding-0, so a bound-but-unwritten descriptor trips VUID-vkCmdDraw-None-08114 during the startup frames before
    //    the real scene depth exists (the DepthTestEnabled runtime guard does NOT satisfy the validator — it checks static shader use). Refresh later
    //    rewrites the set to the real depth view; until then this keeps binding 0 valid. If it fails to build we treat it as fatal — leaving the set
    //    unwritten would reintroduce exactly the fault this exists to prevent.
    if (!ConstructPlaceholderDepth(Host, Pass.PlaceholderImage, Pass.PlaceholderMemory, Pass.PlaceholderView))
    {
        ISSUE_FAULT("ground-grid", "placeholder depth image creation failed — set 0 binding 0 would be unwritten");
        return false;
    }
    WriteDepthDescriptor(Host, Pass.DepthSet, Pass.PointSampler, Pass.PlaceholderView);
    Pass.BoundDepthView = Pass.PlaceholderView;

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
        return;   // idempotent: the set already points at this view, so a per-frame call costs one compare (placeholder != real, so this fires once)

    WriteDepthDescriptor(Host, Pass.DepthSet, Pass.PointSampler, Depth.DepthView);
    Pass.BoundDepthView = Depth.DepthView;
}

void RecordGroundGridPass(const GroundGridPass&      Pass,
                          VkCommandBuffer            CommandBuffer,
                          VkExtent2D                 Extent,
                          const GroundGridConstants& Constants)
{
    if (!Pass.ReadyCondition)
        return;

    // 🔴 The depth test is forced OFF until the set points at a REAL scene-depth view (Refresh not yet called, or the depth target failed to
    //    build). Binding 0 is always a WRITTEN descriptor now — at startup it holds the 1x1 placeholder — so the readable test must exclude the
    //    placeholder explicitly: sampling it would occlude the grid against a 1x1 undefined-contents image. The symptom of getting this wrong is a
    //    MISSING grid, which reads as "the grid pass is broken" rather than "depth not wired yet". The local copy keeps the caller's constants
    //    untouched, so the caller's own DepthTestEnabled intent is never silently edited.
    const bool DepthReadable = Pass.DepthSet != VK_NULL_HANDLE &&
                               Pass.BoundDepthView != VK_NULL_HANDLE &&
                               Pass.BoundDepthView != Pass.PlaceholderView;

    GroundGridConstants Effective = Constants;
    if (!DepthReadable)
        Effective.DepthTestEnabled = 0.0f;

    // 🔴 Set 0 is bound whenever the set HANDLE is valid — NOT only when a real depth view is readable. The fragment shader statically declares
    //    sampler2D SceneDepthImage at set=0,binding=0, so the pipeline "statically uses" set 0; Vulkan then requires a set bound at that slot on
    //    every draw regardless of whether the branch guarding the texelFetch runs (VUID-vkCmdDraw-None-08600). Beyond merely BOUND, binding 0 must be
    //    WRITTEN — a bound-but-unwritten descriptor trips VUID-vkCmdDraw-None-08114, which also keys off static shader use and ignores the runtime
    //    DepthTestEnabled guard. Init writes the 1x1 placeholder into binding 0 so the descriptor is always valid; Refresh swaps to the real view. The
    //    placeholder is never sampled (DepthReadable excludes it, forcing DepthTestEnabled to 0), so its undefined contents never reach the output.
    if (Pass.DepthSet != VK_NULL_HANDLE)
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
    // The owned placeholder depth: view, then image, then its allocation. Safe on a partial build (any handle may be null).
    if (Pass.PlaceholderView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(Host.Device, Pass.PlaceholderView, Host.Allocator);
        Pass.PlaceholderView = VK_NULL_HANDLE;
    }
    if (Pass.PlaceholderImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(Host.Device, Pass.PlaceholderImage, Host.Allocator);
        Pass.PlaceholderImage = VK_NULL_HANDLE;
    }
    if (Pass.PlaceholderMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Host.Device, Pass.PlaceholderMemory, Host.Allocator);
        Pass.PlaceholderMemory = VK_NULL_HANDLE;
    }
    Pass.BoundDepthView = VK_NULL_HANDLE;
    Pass.ReadyCondition = false;
}

} // namespace Frontier
