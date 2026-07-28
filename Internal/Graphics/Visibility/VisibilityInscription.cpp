/*==============================================================================================================================================
                                                         VISIBILITYINSCRIPTION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the visibility inscription. Initialize reads the two SPIR-V modules, builds a one-binding (combined image sampler)
//    descriptor set layout + pool + set, a point sampler, a pipeline layout carrying that set and the InscriptionConstants push range, and a
//    fullscreen-triangle graphics pipeline configured for dynamic rendering against the swapchain colour format (alpha-over blend, no depth, no
//    vertex input). Refresh points the descriptor set at the borrowed visibility image's current view (re-called after each resize). Record binds
//    the pipeline + set, pushes the constants, and draws the three-vertex triangle inside the caller's open colour scope. Finalize tears it down.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Visibility/VisibilityInscription.h"
#include "Graphics/RenderExtension/Diagnostics/DiagnosticArchive.h"

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

// Read a whole SPIR-V file into a byte buffer. Empty on failure (missing / unreadable), which the caller treats as "skip".
std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
{
    std::vector<char> Bytes;
    FILE* Handle = std::fopen(FilePath.c_str(), "rb");
    if (Handle == nullptr)
    {
        ISSUE_CAUTION("visibility-inscription", "shader module not found: %s", FilePath.c_str());
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

// First memory type satisfying the compatible-type bitmask and every required property flag. FoundEnabled is false when none matches. Mirrors the
// per-component helper the other visibility units carry (VisibilityImage / VisibilityDepth) — no shared home exists yet.
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

// Allocate a host-visible + host-coherent storage buffer of ByteCapacity. On any failure both out handles are null. The source-face table is tiny
// and static, so host-visible + mapped (written once) is the right shape — no staging transfer, matching the raster's instance buffer.
bool ConstructSourceFaceBuffer(VulkanHost&     Host,
                               VkDeviceSize    ByteCapacity,
                               VkBuffer&       OutBuffer,
                               VkDeviceMemory& OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteCapacity;
    BufferInformation.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Host.Device, &BufferInformation, Host.Allocator, &OutBuffer) != VK_SUCCESS)
    {
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Host.Device, OutBuffer, &MemoryRequirements);

    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice,
                                                           MemoryRequirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                           MemoryTypeFound);
    if (!MemoryTypeFound)
    {
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &OutMemory) != VK_SUCCESS ||
        vkBindBufferMemory(Host.Device, OutBuffer, OutMemory, 0) != VK_SUCCESS)
    {
        if (OutMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        return false;
    }
    return true;
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

bool InitializeVisibilityInscription(VisibilityInscription& Inscription,
                                     VulkanHost&            Host,
                                     VkFormat               ColourFormat,
                                     const char*            ShaderDirectory)
{
    Inscription = VisibilityInscription{};
    Inscription.Host = &Host;
    if (!Host.DynamicRenderingEnabled || Host.Device == VK_NULL_HANDLE)
        return false;

    // -- Shader modules -------------------------------------------------------------------------------------------------
    const std::string Directory       = ShaderDirectory;
    VkShaderModule    VertexModule     = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/VisibilityInscription.vert.spv"));
    VkShaderModule    FragmentModule   = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/VisibilityInscription.frag.spv"));
    if (VertexModule == VK_NULL_HANDLE || FragmentModule == VK_NULL_HANDLE)
    {
        if (VertexModule   != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        if (FragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        ISSUE_CAUTION("visibility-inscription", "pipeline not built — shader modules unavailable, composite will not draw");
        return false;
    }

    // -- Descriptor set layout: binding 0 = combined image sampler, binding 1 = source-face storage buffer (both fragment stage) --------------
    VkDescriptorSetLayoutBinding Bindings[2] = {};
    Bindings[0].binding         = 0;
    Bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Bindings[0].descriptorCount = 1;
    Bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[1].binding         = 1;
    Bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
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
        ISSUE_FAULT("visibility-inscription", "descriptor set layout creation failed");
        return false;
    }

    // -- Descriptor pool + set (one combined image sampler + one storage buffer) ----------------------------------------
    VkDescriptorPoolSize PoolSizes[2] = {};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSizes[0].descriptorCount = 1;
    PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo PoolInfo = {};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.maxSets       = 1;
    PoolInfo.poolSizeCount = 2;
    PoolInfo.pPoolSizes    = PoolSizes;
    if (vkCreateDescriptorPool(Host.Device, &PoolInfo, Host.Allocator, &Inscription.DescriptorPool) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        FinalizeVisibilityInscription(Inscription);
        ISSUE_FAULT("visibility-inscription", "descriptor pool creation failed");
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocate = {};
    SetAllocate.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetAllocate.descriptorPool     = Inscription.DescriptorPool;
    SetAllocate.descriptorSetCount = 1;
    SetAllocate.pSetLayouts        = &Inscription.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocate, &Inscription.ImageSet) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        FinalizeVisibilityInscription(Inscription);
        ISSUE_FAULT("visibility-inscription", "descriptor set allocation failed");
        return false;
    }

    // -- Point sampler (nearest / clamp): the packed id must not be filtered ---------------------------------------------
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
        FinalizeVisibilityInscription(Inscription);
        ISSUE_FAULT("visibility-inscription", "point sampler creation failed");
        return false;
    }

    // -- Pipeline layout: the one sampler set + the InscriptionConstants push range (fragment stage) --------------------
    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(VisibilityInscriptionConstants);

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
        FinalizeVisibilityInscription(Inscription);
        ISSUE_FAULT("visibility-inscription", "pipeline layout creation failed");
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

    // -- Fixed-function state: no vertex input, triangle list, dynamic viewport/scissor, alpha-over blend, no depth -----
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
    PipelineInfo.layout              = Inscription.PipelineLayout;

    VkResult Outcome = vkCreateGraphicsPipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInfo, Host.Allocator, &Inscription.Pipeline);

    vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
    vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);

    if (Outcome != VK_SUCCESS)
    {
        FinalizeVisibilityInscription(Inscription);
        ISSUE_FAULT("visibility-inscription", "graphics pipeline creation failed (VkResult %d)", (int)Outcome);
        return false;
    }

    Inscription.ReadyCondition = true;
    ISSUE_NOTICE("visibility-inscription", "inscription ready");
    return true;
}

void RefreshVisibilityInscription(VisibilityInscription& Inscription, const VisibilityImage& Image)
{
    if (!Inscription.ReadyCondition || Inscription.ImageSet == VK_NULL_HANDLE)
        return;
    if (!Image.ReadyCondition || Image.IdView == VK_NULL_HANDLE)
        return;

    // Idempotent: the set already points at this exact view, so rewriting it would be a no-op change that still trips the
    // "descriptor in use by a pending command buffer" rule (the set is bound every frame the resolve composites). Only the
    // handful of frames where the view actually changed (first bring-up, each resize) do the write — and the caller idles the
    // device first on those. Steady state issues zero vkUpdateDescriptorSets, so no in-flight set is ever rewritten.
    if (Inscription.BoundIdView == Image.IdView)
        return;

    VkDescriptorImageInfo ImageInfo = {};
    ImageInfo.sampler     = Inscription.PointSampler;
    ImageInfo.imageView   = Image.IdView;
    ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet Write = {};
    Write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Write.dstSet          = Inscription.ImageSet;
    Write.dstBinding      = 0;
    Write.descriptorCount = 1;
    Write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Write.pImageInfo      = &ImageInfo;
    vkUpdateDescriptorSets(Inscription.Host->Device, 1, &Write, 0, nullptr);
    Inscription.BoundIdView = Image.IdView;
}

void UploadInscriptionSourceFaces(VisibilityInscription& Inscription, const std::vector<uint32_t>& TriangleSourceFace)
{
    if (!Inscription.ReadyCondition || Inscription.Host == nullptr || Inscription.ImageSet == VK_NULL_HANDLE)
        return;
    if (TriangleSourceFace.empty())
        return;

    VulkanHost& Host = *Inscription.Host;
    const VkDeviceSize RequiredBytes = (VkDeviceSize)TriangleSourceFace.size() * sizeof(uint32_t);

    // Grow the buffer only when the current one cannot hold the table (first upload, or a larger scene). The device must be idle at upload — the
    // caller uploads once at scene-load, before the render loop begins, so destroying the old buffer here references nothing in flight.
    if (RequiredBytes > Inscription.SourceFaceCapacity)
    {
        if (Inscription.SourceFaceBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, Inscription.SourceFaceBuffer, Host.Allocator);
        if (Inscription.SourceFaceMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Inscription.SourceFaceMemory, Host.Allocator);
        Inscription.SourceFaceBuffer   = VK_NULL_HANDLE;
        Inscription.SourceFaceMemory   = VK_NULL_HANDLE;
        Inscription.SourceFaceCapacity = 0;
        if (!ConstructSourceFaceBuffer(Host, RequiredBytes, Inscription.SourceFaceBuffer, Inscription.SourceFaceMemory))
        {
            ISSUE_CAUTION("visibility-inscription", "source-face table allocation failed — topology wireframe unavailable");
            return;
        }
        Inscription.SourceFaceCapacity = RequiredBytes;
    }

    void* Mapped = nullptr;
    if (vkMapMemory(Host.Device, Inscription.SourceFaceMemory, 0, RequiredBytes, 0, &Mapped) != VK_SUCCESS)
    {
        ISSUE_CAUTION("visibility-inscription", "source-face table map failed — topology wireframe unavailable");
        return;
    }
    std::memcpy(Mapped, TriangleSourceFace.data(), (size_t)RequiredBytes);
    vkUnmapMemory(Host.Device, Inscription.SourceFaceMemory);
    Inscription.SourceFaceCount = (uint32_t)TriangleSourceFace.size();

    // Point binding 1 at the table. Written once at scene load with no frame in flight, so the pending-set rule does not apply here.
    VkDescriptorBufferInfo BufferInfo = {};
    BufferInfo.buffer = Inscription.SourceFaceBuffer;
    BufferInfo.offset = 0;
    BufferInfo.range  = RequiredBytes;

    VkWriteDescriptorSet Write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    Write.dstSet          = Inscription.ImageSet;
    Write.dstBinding      = 1;
    Write.descriptorCount = 1;
    Write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Write.pBufferInfo     = &BufferInfo;
    vkUpdateDescriptorSets(Host.Device, 1, &Write, 0, nullptr);
}

void RecordVisibilityInscription(const VisibilityInscription&          Inscription,
                                 VkExtent2D                            Extent,
                                 const VisibilityInscriptionConstants& Constants,
                                 VkCommandBuffer                       CommandBuffer)
{
    if (!Inscription.ReadyCondition || Inscription.ImageSet == VK_NULL_HANDLE)
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

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Inscription.Pipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Inscription.PipelineLayout,
                            0, 1, &Inscription.ImageSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Inscription.PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(VisibilityInscriptionConstants), &Constants);
    vkCmdDraw(CommandBuffer, 3, 1, 0, 0);
}

void FinalizeVisibilityInscription(VisibilityInscription& Inscription)
{
    if (Inscription.Host == nullptr || Inscription.Host->Device == VK_NULL_HANDLE)
    {
        Inscription = VisibilityInscription{};
        return;
    }
    VkDevice                     Device    = Inscription.Host->Device;
    const VkAllocationCallbacks* Allocator = Inscription.Host->Allocator;

    if (Inscription.Pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(Device, Inscription.Pipeline, Allocator);
    if (Inscription.PipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(Device, Inscription.PipelineLayout, Allocator);
    if (Inscription.PointSampler != VK_NULL_HANDLE)
        vkDestroySampler(Device, Inscription.PointSampler, Allocator);
    if (Inscription.DescriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(Device, Inscription.DescriptorPool, Allocator);   // frees ImageSet
    if (Inscription.SetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(Device, Inscription.SetLayout, Allocator);
    if (Inscription.SourceFaceBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(Device, Inscription.SourceFaceBuffer, Allocator);
    if (Inscription.SourceFaceMemory != VK_NULL_HANDLE)
        vkFreeMemory(Device, Inscription.SourceFaceMemory, Allocator);

    Inscription = VisibilityInscription{};
}

} // namespace Frontier
