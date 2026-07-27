/*==============================================================================================================================================
                                                            SKYATMOSPHERE.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the Hillaire-2020 sky pass. Initialize builds the three LUT images (transmittance / multi-scatter / sky-view), a shared
//    clamp-linear sampler, the profile UBO (persistent-mapped), four descriptor sets, and four pipelines (two compute bakes, the sky-view
//    graphics bake, and the per-frame dome graphics pipeline for the swapchain format). BakeSkyAtmosphereConstants submits a one-shot that
//    fills the two sun-independent LUTs. Record re-bakes the sky-view on a one-shot when the sun moved, then draws the dome into the caller's
//    open dynamic-rendering scope. Finalize tears everything down. Raw Vulkan throughout (VulkanHost.Allocator is the default host allocator).

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Atmosphere/SkyAtmosphere.h"
#include "Graphics/RenderExtension/Diagnostics/DiagnosticArchive.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// LUT dimensions (see PLAN-SkyAtmosphere.md).
const uint32_t TransmittanceWidth  = 256;
const uint32_t TransmittanceHeight = 64;
const uint32_t MultiScatterWidth   = 32;
const uint32_t MultiScatterHeight  = 32;
const uint32_t SkyViewWidth        = 192;
const uint32_t SkyViewHeight       = 108;
const VkFormat LutFormat           = VK_FORMAT_R16G16B16A16_SFLOAT;

// Read a whole SPIR-V file into a byte buffer. Empty on failure (missing / unreadable) → caller skips the sky.
std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
{
    std::vector<char> Bytes;
    FILE* Handle = std::fopen(FilePath.c_str(), "rb");
    if (Handle == nullptr)
    {
        ISSUE_CAUTION("sky-atmosphere", "shader module not found: %s", FilePath.c_str());
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

// Pick a memory type index matching the requirement bits + property flags. Returns UINT32_MAX on failure.
uint32_t ResolveMemoryType(const VulkanHost& Host, uint32_t TypeBits, VkMemoryPropertyFlags Properties)
{
    VkPhysicalDeviceMemoryProperties MemoryProps;
    vkGetPhysicalDeviceMemoryProperties(Host.PhysicalDevice, &MemoryProps);
    for (uint32_t Index = 0; Index < MemoryProps.memoryTypeCount; Index++)
    {
        if ((TypeBits & (1u << Index)) &&
            (MemoryProps.memoryTypes[Index].propertyFlags & Properties) == Properties)
            return Index;
    }
    return UINT32_MAX;
}

// Allocate a 2D image usable as both a storage/colour target and a sampled texture, plus its view. Returns false on failure.
bool ConstructLutImage(const VulkanHost& Host,
                       uint32_t          Width,
                       uint32_t          Height,
                       VkImageUsageFlags Usage,
                       VkImage&          Image,
                       VkDeviceMemory&   Memory,
                       VkImageView&      View)
{
    VkImageCreateInfo ImageInfo = {};
    ImageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageInfo.imageType     = VK_IMAGE_TYPE_2D;
    ImageInfo.format        = LutFormat;
    ImageInfo.extent        = { Width, Height, 1 };
    ImageInfo.mipLevels     = 1;
    ImageInfo.arrayLayers   = 1;
    ImageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageInfo.usage         = Usage | VK_IMAGE_USAGE_SAMPLED_BIT;
    ImageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(Host.Device, &ImageInfo, Host.Allocator, &Image) != VK_SUCCESS)
        return false;

    VkMemoryRequirements Requirements;
    vkGetImageMemoryRequirements(Host.Device, Image, &Requirements);

    VkMemoryAllocateInfo AllocInfo = {};
    AllocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocInfo.allocationSize  = Requirements.size;
    AllocInfo.memoryTypeIndex = ResolveMemoryType(Host, Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (AllocInfo.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(Host.Device, &AllocInfo, Host.Allocator, &Memory) != VK_SUCCESS)
        return false;
    vkBindImageMemory(Host.Device, Image, Memory, 0);

    VkImageViewCreateInfo ViewInfo = {};
    ViewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewInfo.image    = Image;
    ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ViewInfo.format   = LutFormat;
    ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewInfo.subresourceRange.levelCount = 1;
    ViewInfo.subresourceRange.layerCount = 1;
    return vkCreateImageView(Host.Device, &ViewInfo, Host.Allocator, &View) == VK_SUCCESS;
}

// Record an image-layout transition barrier (all-subresource, color aspect).
void RecordLayoutTransition(VkCommandBuffer      Cmd,
                            VkImage              Image,
                            VkImageLayout        OldLayout,
                            VkImageLayout        NewLayout,
                            VkAccessFlags        SrcAccess,
                            VkAccessFlags        DstAccess,
                            VkPipelineStageFlags SrcStage,
                            VkPipelineStageFlags DstStage)
{
    VkImageMemoryBarrier Barrier = {};
    Barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.oldLayout           = OldLayout;
    Barrier.newLayout           = NewLayout;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image               = Image;
    Barrier.srcAccessMask       = SrcAccess;
    Barrier.dstAccessMask       = DstAccess;
    Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    Barrier.subresourceRange.levelCount = 1;
    Barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(Cmd, SrcStage, DstStage, 0, 0, nullptr, 0, nullptr, 1, &Barrier);
}

// Begin a one-shot primary command buffer from a transient pool. Returns VK_NULL_HANDLE on failure (Pool left as created for the caller to free).
VkCommandBuffer BeginOneShot(const VulkanHost& Host, VkCommandPool& Pool)
{
    VkCommandPoolCreateInfo PoolInfo = {};
    PoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    PoolInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    PoolInfo.queueFamilyIndex = Host.GraphicsQueueFamily;
    if (vkCreateCommandPool(Host.Device, &PoolInfo, Host.Allocator, &Pool) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    VkCommandBufferAllocateInfo AllocInfo = {};
    AllocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    AllocInfo.commandPool        = Pool;
    AllocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    AllocInfo.commandBufferCount = 1;
    VkCommandBuffer Cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(Host.Device, &AllocInfo, &Cmd) != VK_SUCCESS)
    {
        vkDestroyCommandPool(Host.Device, Pool, Host.Allocator);
        Pool = VK_NULL_HANDLE;
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo BeginInfo = {};
    BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(Cmd, &BeginInfo);
    return Cmd;
}

// Submit + wait + free a one-shot buffer and its pool.
void EndOneShot(const VulkanHost& Host, VkCommandPool Pool, VkCommandBuffer Cmd)
{
    vkEndCommandBuffer(Cmd);
    VkSubmitInfo Submit = {};
    Submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submit.commandBufferCount = 1;
    Submit.pCommandBuffers    = &Cmd;
    vkQueueSubmit(Host.GraphicsQueue, 1, &Submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(Host.GraphicsQueue);
    vkDestroyCommandPool(Host.Device, Pool, Host.Allocator);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DESCRIPTOR + PIPELINE BUILDERS
//------------------------------------------------------------------------------------------------------------------------

// Build a compute pipeline + layout from one SPIR-V module and one descriptor-set layout.
bool ConstructComputePipeline(const VulkanHost&     Host,
                              const std::string&    SpirvPath,
                              VkDescriptorSetLayout SetLayout,
                              VkPipelineLayout&     Layout,
                              VkPipeline&           Pipeline)
{
    VkShaderModule Module = ConstructShaderModule(Host, RetrieveShaderBytes(SpirvPath));
    if (Module == VK_NULL_HANDLE)
        return false;

    VkPipelineLayoutCreateInfo LayoutInfo = {};
    LayoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutInfo.setLayoutCount = 1;
    LayoutInfo.pSetLayouts    = &SetLayout;
    if (vkCreatePipelineLayout(Host.Device, &LayoutInfo, Host.Allocator, &Layout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Host.Device, Module, Host.Allocator);
        return false;
    }

    VkComputePipelineCreateInfo PipelineInfo = {};
    PipelineInfo.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    PipelineInfo.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    PipelineInfo.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    PipelineInfo.stage.module = Module;
    PipelineInfo.stage.pName  = "main";
    PipelineInfo.layout       = Layout;

    VkResult Outcome = vkCreateComputePipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInfo, Host.Allocator, &Pipeline);
    vkDestroyShaderModule(Host.Device, Module, Host.Allocator);
    return Outcome == VK_SUCCESS;
}

// Build a fullscreen-triangle graphics pipeline (shared FullscreenTriangle.vert + given frag) for one colour format.
bool ConstructGraphicsPipeline(const VulkanHost&     Host,
                               const std::string&    Directory,
                               const std::string&    FragSpv,
                               VkFormat              ColourFormat,
                               VkDescriptorSetLayout SetLayout,
                               uint32_t              PushSize,
                               VkPipelineLayout&     Layout,
                               VkPipeline&           Pipeline)
{
    VkShaderModule VertexModule   = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/FullscreenTriangle.vert.spv"));
    VkShaderModule FragmentModule = ConstructShaderModule(Host, RetrieveShaderBytes(FragSpv));
    if (VertexModule == VK_NULL_HANDLE || FragmentModule == VK_NULL_HANDLE)
    {
        if (VertexModule   != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        if (FragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        return false;
    }

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.size       = PushSize;

    VkPipelineLayoutCreateInfo LayoutInfo = {};
    LayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutInfo.setLayoutCount         = 1;
    LayoutInfo.pSetLayouts            = &SetLayout;
    LayoutInfo.pushConstantRangeCount = (PushSize > 0) ? 1 : 0;
    LayoutInfo.pPushConstantRanges    = (PushSize > 0) ? &PushRange : nullptr;
    if (vkCreatePipelineLayout(Host.Device, &LayoutInfo, Host.Allocator, &Layout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        return false;
    }

    VkPipelineShaderStageCreateInfo Stages[2] = {};
    Stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = VertexModule;
    Stages[0].pName  = "main";
    Stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = FragmentModule;
    Stages[1].pName  = "main";

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
    BlendAttachment.blendEnable    = VK_FALSE;   // sky writes opaque; grid alpha-blends over it
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
    PipelineInfo.layout              = Layout;

    VkResult Outcome = vkCreateGraphicsPipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInfo, Host.Allocator, &Pipeline);
    vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
    vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
    return Outcome == VK_SUCCESS;
}

// One descriptor-set-layout builder from an array of (binding, type, stage) triples.
struct BindingSpec { uint32_t Binding; VkDescriptorType Type; VkShaderStageFlags Stage; };
bool ConstructSetLayout(const VulkanHost& Host, const BindingSpec* Specs, uint32_t Count, VkDescriptorSetLayout& Layout)
{
    std::vector<VkDescriptorSetLayoutBinding> Bindings(Count);
    for (uint32_t Index = 0; Index < Count; Index++)
    {
        Bindings[Index] = {};
        Bindings[Index].binding         = Specs[Index].Binding;
        Bindings[Index].descriptorType  = Specs[Index].Type;
        Bindings[Index].descriptorCount = 1;
        Bindings[Index].stageFlags      = Specs[Index].Stage;
    }
    VkDescriptorSetLayoutCreateInfo Info = {};
    Info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    Info.bindingCount = Count;
    Info.pBindings    = Bindings.data();
    return vkCreateDescriptorSetLayout(Host.Device, &Info, Host.Allocator, &Layout) == VK_SUCCESS;
}

// Dispatch one compute LUT bake, transitioning the target UNDEFINED→GENERAL, dispatching, then GENERAL→SHADER_READ_ONLY.
void RecordComputeBake(VkCommandBuffer  Cmd,
                       VkPipeline       Pipeline,
                       VkPipelineLayout Layout,
                       VkDescriptorSet  Set,
                       VkImage          Target,
                       uint32_t         Width,
                       uint32_t         Height)
{
    RecordLayoutTransition(Cmd, Target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                           0, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Layout, 0, 1, &Set, 0, nullptr);
    vkCmdDispatch(Cmd, (Width + 7) / 8, (Height + 7) / 8, 1);

    RecordLayoutTransition(Cmd, Target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

// Re-bake the sky-view table into SkyViewImage via its own one-shot render scope. Called only when the sun moved.
void BakeSkyView(SkyAtmospherePass& Pass, const VulkanHost& Host)
{
    VkCommandPool   Pool = VK_NULL_HANDLE;
    VkCommandBuffer Cmd  = BeginOneShot(Host, Pool);
    if (Cmd == VK_NULL_HANDLE)
        return;

    RecordLayoutTransition(Cmd, Pass.SkyViewImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfoKHR ColorAttachment = {};
    ColorAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    ColorAttachment.imageView   = Pass.SkyViewView;
    ColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ColorAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ColorAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfoKHR RenderingInfo = {};
    RenderingInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    RenderingInfo.renderArea.extent    = { SkyViewWidth, SkyViewHeight };
    RenderingInfo.layerCount           = 1;
    RenderingInfo.colorAttachmentCount = 1;
    RenderingInfo.pColorAttachments    = &ColorAttachment;

    Host.CmdBeginRendering(Cmd, &RenderingInfo);

    VkViewport ViewportRegion = {};
    ViewportRegion.width    = (float)SkyViewWidth;
    ViewportRegion.height   = (float)SkyViewHeight;
    ViewportRegion.maxDepth = 1.0f;
    vkCmdSetViewport(Cmd, 0, 1, &ViewportRegion);
    VkRect2D Scissor = {};
    Scissor.extent = { SkyViewWidth, SkyViewHeight };
    vkCmdSetScissor(Cmd, 0, 1, &Scissor);

    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pass.SkyViewPipeline);
    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pass.SkyViewLayout, 0, 1, &Pass.SkyViewSet, 0, nullptr);
    vkCmdDraw(Cmd, 3, 1, 0, 0);

    Host.CmdEndRendering(Cmd);

    RecordLayoutTransition(Cmd, Pass.SkyViewImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    EndOneShot(Host, Pool, Cmd);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSkyAtmospherePass(SkyAtmospherePass& Pass,
                                 const VulkanHost&  Host,
                                 VkFormat           ColourFormat,
                                 const char*        ShaderDirectory)
{
    Pass.ReadyCondition = false;
    if (!Host.DynamicRenderingEnabled || Host.Device == VK_NULL_HANDLE)
        return false;

    Pass.Profile           = Atmosphere::ConstructEarthProfile();
    Pass.SunDirtyCondition = true;
    const std::string Directory = ShaderDirectory;

    // -- Profile UBO (host-visible, persistently mapped) ----------------------------------------------------------------
    {
        VkBufferCreateInfo BufferInfo = {};
        BufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        BufferInfo.size        = sizeof(AtmosphereUniformBlock);
        BufferInfo.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Host.Device, &BufferInfo, Host.Allocator, &Pass.ProfileBuffer) != VK_SUCCESS)
            return false;

        VkMemoryRequirements Requirements;
        vkGetBufferMemoryRequirements(Host.Device, Pass.ProfileBuffer, &Requirements);
        VkMemoryAllocateInfo AllocInfo = {};
        AllocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        AllocInfo.allocationSize  = Requirements.size;
        AllocInfo.memoryTypeIndex = ResolveMemoryType(Host, Requirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (AllocInfo.memoryTypeIndex == UINT32_MAX ||
            vkAllocateMemory(Host.Device, &AllocInfo, Host.Allocator, &Pass.ProfileMemory) != VK_SUCCESS)
            return false;
        vkBindBufferMemory(Host.Device, Pass.ProfileBuffer, Pass.ProfileMemory, 0);
        vkMapMemory(Host.Device, Pass.ProfileMemory, 0, sizeof(AtmosphereUniformBlock), 0, &Pass.ProfileMapping);
        std::memcpy(Pass.ProfileMapping, &Pass.Profile, sizeof(AtmosphereUniformBlock));
    }

    // -- LUT images -----------------------------------------------------------------------------------------------------
    if (!ConstructLutImage(Host, TransmittanceWidth, TransmittanceHeight, VK_IMAGE_USAGE_STORAGE_BIT,
                           Pass.TransmittanceImage, Pass.TransmittanceMemory, Pass.TransmittanceView) ||
        !ConstructLutImage(Host, MultiScatterWidth, MultiScatterHeight, VK_IMAGE_USAGE_STORAGE_BIT,
                           Pass.MultiScatterImage, Pass.MultiScatterMemory, Pass.MultiScatterView) ||
        !ConstructLutImage(Host, SkyViewWidth, SkyViewHeight, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                           Pass.SkyViewImage, Pass.SkyViewMemory, Pass.SkyViewView))
    {
        ISSUE_FAULT("sky-atmosphere", "LUT image allocation failed");
        return false;
    }

    // -- Shared sampler -------------------------------------------------------------------------------------------------
    {
        VkSamplerCreateInfo SamplerInfo = {};
        SamplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        SamplerInfo.magFilter    = VK_FILTER_LINEAR;
        SamplerInfo.minFilter    = VK_FILTER_LINEAR;
        SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(Host.Device, &SamplerInfo, Host.Allocator, &Pass.LinearSampler) != VK_SUCCESS)
            return false;
    }

    // -- Descriptor-set layouts -----------------------------------------------------------------------------------------
    const BindingSpec TransmittanceBindings[] = {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,   VK_SHADER_STAGE_COMPUTE_BIT },
    };
    const BindingSpec MultiScatterBindings[] = {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT }, // transmittance
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_SHADER_STAGE_COMPUTE_BIT },
    };
    const BindingSpec SkyViewBindings[] = {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }, // transmittance
        { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }, // multiscatter
    };
    const BindingSpec DomeBindings[] = {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }, // transmittance
        { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }, // skyview
    };
    if (!ConstructSetLayout(Host, TransmittanceBindings, 2, Pass.TransmittanceSetLayout) ||
        !ConstructSetLayout(Host, MultiScatterBindings,  3, Pass.MultiScatterSetLayout)  ||
        !ConstructSetLayout(Host, SkyViewBindings,       3, Pass.SkyViewSetLayout)       ||
        !ConstructSetLayout(Host, DomeBindings,          3, Pass.DomeSetLayout))
    {
        ISSUE_FAULT("sky-atmosphere", "descriptor-set layout creation failed");
        return false;
    }

    // -- Descriptor pool + sets -----------------------------------------------------------------------------------------
    {
        VkDescriptorPoolSize PoolSizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         4 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          2 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6 },
        };
        VkDescriptorPoolCreateInfo PoolInfo = {};
        PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        PoolInfo.maxSets       = 4;
        PoolInfo.poolSizeCount = 3;
        PoolInfo.pPoolSizes    = PoolSizes;
        if (vkCreateDescriptorPool(Host.Device, &PoolInfo, Host.Allocator, &Pass.DescriptorPool) != VK_SUCCESS)
            return false;

        VkDescriptorSetLayout Layouts[4] = { Pass.TransmittanceSetLayout, Pass.MultiScatterSetLayout,
                                             Pass.SkyViewSetLayout, Pass.DomeSetLayout };
        VkDescriptorSet* Sets[4] = { &Pass.TransmittanceSet, &Pass.MultiScatterSet, &Pass.SkyViewSet, &Pass.DomeSet };
        for (int Index = 0; Index < 4; Index++)
        {
            VkDescriptorSetAllocateInfo AllocInfo = {};
            AllocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            AllocInfo.descriptorPool     = Pass.DescriptorPool;
            AllocInfo.descriptorSetCount = 1;
            AllocInfo.pSetLayouts        = &Layouts[Index];
            if (vkAllocateDescriptorSets(Host.Device, &AllocInfo, Sets[Index]) != VK_SUCCESS)
            {
                ISSUE_FAULT("sky-atmosphere", "descriptor-set allocation failed");
                return false;
            }
        }
    }

    // -- Write descriptors ----------------------------------------------------------------------------------------------
    {
        VkDescriptorBufferInfo UboInfo = { Pass.ProfileBuffer, 0, sizeof(AtmosphereUniformBlock) };
        VkDescriptorImageInfo TransmittanceStore = { VK_NULL_HANDLE, Pass.TransmittanceView, VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo MultiScatterStore  = { VK_NULL_HANDLE, Pass.MultiScatterView,  VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo TransmittanceSample = { Pass.LinearSampler, Pass.TransmittanceView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo MultiScatterSample  = { Pass.LinearSampler, Pass.MultiScatterView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo SkyViewSample       = { Pass.LinearSampler, Pass.SkyViewView,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        VkWriteDescriptorSet Writes[11] = {};
        auto Write = [&](int I, VkDescriptorSet Set, uint32_t Binding, VkDescriptorType Type,
                         VkDescriptorBufferInfo* Buf, VkDescriptorImageInfo* Img)
        {
            Writes[I].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            Writes[I].dstSet          = Set;
            Writes[I].dstBinding      = Binding;
            Writes[I].descriptorCount = 1;
            Writes[I].descriptorType  = Type;
            Writes[I].pBufferInfo     = Buf;
            Writes[I].pImageInfo      = Img;
        };
        // Transmittance bake set: UBO + storage image
        Write(0, Pass.TransmittanceSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &UboInfo, nullptr);
        Write(1, Pass.TransmittanceSet, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  nullptr, &TransmittanceStore);
        // Multi-scatter bake set: UBO + transmittance sampler + storage image
        Write(2, Pass.MultiScatterSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         &UboInfo, nullptr);
        Write(3, Pass.MultiScatterSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &TransmittanceSample);
        Write(4, Pass.MultiScatterSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          nullptr, &MultiScatterStore);
        // Sky-view bake set: UBO + transmittance + multiscatter samplers
        Write(5, Pass.SkyViewSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         &UboInfo, nullptr);
        Write(6, Pass.SkyViewSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &TransmittanceSample);
        Write(7, Pass.SkyViewSet, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &MultiScatterSample);
        // Dome set: UBO + transmittance + skyview samplers
        Write(8,  Pass.DomeSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         &UboInfo, nullptr);
        Write(9,  Pass.DomeSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &TransmittanceSample);
        Write(10, Pass.DomeSet, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &SkyViewSample);

        vkUpdateDescriptorSets(Host.Device, 11, Writes, 0, nullptr);
    }

    // -- Pipelines ------------------------------------------------------------------------------------------------------
    if (!ConstructComputePipeline(Host, Directory + "/Transmittance.comp.spv", Pass.TransmittanceSetLayout,
                                  Pass.TransmittanceLayout, Pass.TransmittancePipeline) ||
        !ConstructComputePipeline(Host, Directory + "/MultiScatter.comp.spv", Pass.MultiScatterSetLayout,
                                  Pass.MultiScatterLayout, Pass.MultiScatterPipeline) ||
        !ConstructGraphicsPipeline(Host, Directory, Directory + "/SkyView.frag.spv", LutFormat,
                                   Pass.SkyViewSetLayout, 0, Pass.SkyViewLayout, Pass.SkyViewPipeline) ||
        !ConstructGraphicsPipeline(Host, Directory, Directory + "/SkyDome.frag.spv", ColourFormat,
                                   Pass.DomeSetLayout, sizeof(SkyDomeConstants), Pass.DomeLayout, Pass.DomePipeline))
    {
        ISSUE_FAULT("sky-atmosphere", "pipeline creation failed — sky will not draw");
        return false;
    }

    Pass.ReadyCondition = true;
    ISSUE_NOTICE("sky-atmosphere", "sky pass ready (transmittance 256x64, multiscatter 32x32, skyview 192x108)");
    return true;
}

void BakeSkyAtmosphereConstants(SkyAtmospherePass& Pass, const VulkanHost& Host)
{
    if (!Pass.ReadyCondition)
        return;

    VkCommandPool   Pool = VK_NULL_HANDLE;
    VkCommandBuffer Cmd  = BeginOneShot(Host, Pool);
    if (Cmd == VK_NULL_HANDLE)
        return;

    RecordComputeBake(Cmd, Pass.TransmittancePipeline, Pass.TransmittanceLayout, Pass.TransmittanceSet,
                      Pass.TransmittanceImage, TransmittanceWidth, TransmittanceHeight);
    // Multi-scatter reads the transmittance LUT, so it runs after (the read-only transition above is the barrier).
    RecordComputeBake(Cmd, Pass.MultiScatterPipeline, Pass.MultiScatterLayout, Pass.MultiScatterSet,
                      Pass.MultiScatterImage, MultiScatterWidth, MultiScatterHeight);

    EndOneShot(Host, Pool, Cmd);
    ISSUE_NOTICE("sky-atmosphere", "boot LUTs baked (transmittance + multi-scatter)");
}

void UpdateSkyAtmosphereProfile(SkyAtmospherePass& Pass, const AtmosphereUniformBlock& Profile)
{
    Pass.Profile = Profile;
    if (Pass.ProfileMapping != nullptr)
        std::memcpy(Pass.ProfileMapping, &Pass.Profile, sizeof(AtmosphereUniformBlock));
    Pass.SunDirtyCondition = true;
}

void RecordSkyAtmospherePass(SkyAtmospherePass&      Pass,
                             const VulkanHost&       Host,
                             VkCommandBuffer         CommandBuffer,
                             VkExtent2D              Extent,
                             const SkyDomeConstants& Constants)
{
    if (!Pass.ReadyCondition)
        return;

    // Re-bake the sun-dependent sky-view table if the sun moved. Its own one-shot (rare event; the swapchain scope stays untouched).
    if (Pass.SunDirtyCondition)
    {
        BakeSkyView(Pass, Host);
        Pass.SunDirtyCondition = false;
    }

    // Draw the fullscreen dome into the caller's open dynamic-rendering scope.
    VkViewport ViewportRegion = {};
    ViewportRegion.width    = (float)Extent.width;
    ViewportRegion.height   = (float)Extent.height;
    ViewportRegion.maxDepth = 1.0f;
    vkCmdSetViewport(CommandBuffer, 0, 1, &ViewportRegion);
    VkRect2D Scissor = {};
    Scissor.extent = Extent;
    vkCmdSetScissor(CommandBuffer, 0, 1, &Scissor);

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pass.DomePipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pass.DomeLayout, 0, 1, &Pass.DomeSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Pass.DomeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyDomeConstants), &Constants);
    vkCmdDraw(CommandBuffer, 3, 1, 0, 0);
}

void FinalizeSkyAtmospherePass(SkyAtmospherePass& Pass, const VulkanHost& Host)
{
    if (Host.Device == VK_NULL_HANDLE)
        return;

    VkAllocationCallbacks* A = (VkAllocationCallbacks*)Host.Allocator;
    if (Pass.DomePipeline)          vkDestroyPipeline(Host.Device, Pass.DomePipeline, A);
    if (Pass.SkyViewPipeline)       vkDestroyPipeline(Host.Device, Pass.SkyViewPipeline, A);
    if (Pass.MultiScatterPipeline)  vkDestroyPipeline(Host.Device, Pass.MultiScatterPipeline, A);
    if (Pass.TransmittancePipeline) vkDestroyPipeline(Host.Device, Pass.TransmittancePipeline, A);
    if (Pass.DomeLayout)            vkDestroyPipelineLayout(Host.Device, Pass.DomeLayout, A);
    if (Pass.SkyViewLayout)         vkDestroyPipelineLayout(Host.Device, Pass.SkyViewLayout, A);
    if (Pass.MultiScatterLayout)    vkDestroyPipelineLayout(Host.Device, Pass.MultiScatterLayout, A);
    if (Pass.TransmittanceLayout)   vkDestroyPipelineLayout(Host.Device, Pass.TransmittanceLayout, A);

    if (Pass.DescriptorPool)          vkDestroyDescriptorPool(Host.Device, Pass.DescriptorPool, A);
    if (Pass.DomeSetLayout)           vkDestroyDescriptorSetLayout(Host.Device, Pass.DomeSetLayout, A);
    if (Pass.SkyViewSetLayout)        vkDestroyDescriptorSetLayout(Host.Device, Pass.SkyViewSetLayout, A);
    if (Pass.MultiScatterSetLayout)   vkDestroyDescriptorSetLayout(Host.Device, Pass.MultiScatterSetLayout, A);
    if (Pass.TransmittanceSetLayout)  vkDestroyDescriptorSetLayout(Host.Device, Pass.TransmittanceSetLayout, A);

    if (Pass.LinearSampler) vkDestroySampler(Host.Device, Pass.LinearSampler, A);

    if (Pass.SkyViewView)        vkDestroyImageView(Host.Device, Pass.SkyViewView, A);
    if (Pass.SkyViewImage)       vkDestroyImage(Host.Device, Pass.SkyViewImage, A);
    if (Pass.SkyViewMemory)      vkFreeMemory(Host.Device, Pass.SkyViewMemory, A);
    if (Pass.MultiScatterView)   vkDestroyImageView(Host.Device, Pass.MultiScatterView, A);
    if (Pass.MultiScatterImage)  vkDestroyImage(Host.Device, Pass.MultiScatterImage, A);
    if (Pass.MultiScatterMemory) vkFreeMemory(Host.Device, Pass.MultiScatterMemory, A);
    if (Pass.TransmittanceView)  vkDestroyImageView(Host.Device, Pass.TransmittanceView, A);
    if (Pass.TransmittanceImage) vkDestroyImage(Host.Device, Pass.TransmittanceImage, A);
    if (Pass.TransmittanceMemory)vkFreeMemory(Host.Device, Pass.TransmittanceMemory, A);

    if (Pass.ProfileMapping) vkUnmapMemory(Host.Device, Pass.ProfileMemory);
    if (Pass.ProfileBuffer)  vkDestroyBuffer(Host.Device, Pass.ProfileBuffer, A);
    if (Pass.ProfileMemory)  vkFreeMemory(Host.Device, Pass.ProfileMemory, A);

    Pass = SkyAtmospherePass{};
}

} // namespace Frontier
