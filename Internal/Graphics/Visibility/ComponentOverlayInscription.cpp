/*==============================================================================================================================================
                                                      COMPONENTOVERLAYINSCRIPTION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the component (vertex / edge / face) overlay composite. Initialize reads the two SPIR-V modules, builds a five-binding set
//    layout (id image + depth image as combined image samplers, then the vertex / index / instance SSBOs), a pipeline layout carrying the
//    ComponentConstants push range, a point sampler, and a graphics pipeline configured for dynamic rendering against the swapchain colour format
//    (no vertex input, alpha-over blend, no depth). Refresh re-points the set at the borrowed views + buffers. Record binds and draws the fullscreen
//    triangle. Structurally a sibling of SelectionOutlineInscription.cpp — same idiom, own pipeline.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Visibility/ComponentOverlayInscription.h"

// 📝 The header declares nothing unless FRONTIER_POLYGON_AUTHORING is set, so the whole translation unit collapses to empty with it off. The include
//    above stays outside the guard so the flag is picked up from it if a build defines it there.
#ifdef FRONTIER_POLYGON_AUTHORING

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

// Read a whole SPIR-V file into a byte buffer. Empty on failure (missing / unreadable).
std::vector<char> RetrieveOverlayShaderBytes(const std::string& FilePath)
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
VkShaderModule ConstructOverlayShaderModule(const VulkanHost& Host, const std::vector<char>& Bytes)
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

// First memory type allowed by the requirement bitmask carrying every required property bit. Duplicated per unit throughout the graphics tree (the same
// helper appears in VisibilityInscription / ClipmapFieldInspection / SvgIconRegistry) rather than shared from a header — following the existing pattern
// here rather than introducing a new common dependency for eight lines.
uint32_t SelectMemoryTypeIndex(VkPhysicalDevice      PhysicalDevice,
                               uint32_t              CompatibleTypesBitmask,
                               VkMemoryPropertyFlags RequiredProperties,
                               bool&                 FoundEnabled)
{
    VkPhysicalDeviceMemoryProperties MemoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);
    for (uint32_t TypeIterator = 0; TypeIterator < MemoryProperties.memoryTypeCount; ++TypeIterator)
    {
        const bool TypeAllowed = (CompatibleTypesBitmask & (1u << TypeIterator)) != 0;
        const bool PropertiesPresent =
            (MemoryProperties.memoryTypes[TypeIterator].propertyFlags & RequiredProperties) == RequiredProperties;
        if (TypeAllowed && PropertiesPresent)
        {
            FoundEnabled = true;
            return TypeIterator;
        }
    }
    FoundEnabled = false;
    return 0;
}

// Allocate a HOST-VISIBLE storage buffer for one authored-topology table. Host-visible (rather than device-local with a staging copy) because these
// tables are written exactly once per scene load and then read every frame by a fullscreen pass: the upload cost is irrelevant, a transfer command
// buffer and its synchronization are not worth writing, and the read is uniform enough across the draw to be cache-friendly regardless.
bool ConstructTopologyBuffer(VulkanHost& Host, VkDeviceSize ByteCapacity, VkBuffer& OutBuffer, VkDeviceMemory& OutMemory)
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

// Grow (if needed) then fill one table. Reallocates only when the payload outgrows the existing capacity, so a same-size scene reload reuses the memory.
bool UploadTopologyTable(VulkanHost&                  Host,
                         const std::vector<uint32_t>& Payload,
                         VkBuffer&                    Buffer,
                         VkDeviceMemory&              Memory,
                         VkDeviceSize&                Capacity)
{
    const VkDeviceSize RequiredBytes = (VkDeviceSize)Payload.size() * sizeof(uint32_t);
    if (RequiredBytes == 0)
        return false;

    if (RequiredBytes > Capacity)
    {
        if (Buffer != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, Buffer, Host.Allocator);
        if (Memory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Memory, Host.Allocator);
        Buffer   = VK_NULL_HANDLE;
        Memory   = VK_NULL_HANDLE;
        Capacity = 0;
        if (!ConstructTopologyBuffer(Host, RequiredBytes, Buffer, Memory))
            return false;
        Capacity = RequiredBytes;
    }

    void* Mapped = nullptr;
    if (vkMapMemory(Host.Device, Memory, 0, RequiredBytes, 0, &Mapped) != VK_SUCCESS)
        return false;
    std::memcpy(Mapped, Payload.data(), (size_t)RequiredBytes);
    vkUnmapMemory(Host.Device, Memory);
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeComponentOverlayInscription(ComponentOverlayInscription& Inscription,
                                           VulkanHost&                  Host,
                                           VkFormat                     ColourFormat,
                                           const char*                  ShaderDirectory)
{
    Inscription = ComponentOverlayInscription{};
    Inscription.Host = &Host;
    if (!Host.DynamicRenderingEnabled || Host.Device == VK_NULL_HANDLE)
    {
        ISSUE_CAUTION("component-overlay", "dynamic rendering unavailable - overlay will not draw");
        return false;
    }

    // -- Shader modules -------------------------------------------------------------------------------------------------
    const std::string Directory      = ShaderDirectory;
    VkShaderModule    VertexModule   = ConstructOverlayShaderModule(Host, RetrieveOverlayShaderBytes(Directory + "/ComponentOverlay.vert.spv"));
    VkShaderModule    FragmentModule = ConstructOverlayShaderModule(Host, RetrieveOverlayShaderBytes(Directory + "/ComponentOverlay.frag.spv"));
    if (VertexModule == VK_NULL_HANDLE || FragmentModule == VK_NULL_HANDLE)
    {
        if (VertexModule   != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        if (FragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        ISSUE_CAUTION("component-overlay", "pipeline not built - shader modules unavailable, overlay will not draw");
        return false;
    }

    // -- Descriptor set layout: 0 = id image, 1 = depth image, 2 = vertices, 3 = indices, 4 = instances, ------------------
    //    5 = authored source faces, 6 = authored corner vertices, 7 = authored side edges
    VkDescriptorSetLayoutBinding Bindings[ComponentOverlayBindingCount] = {};
    for (uint32_t Slot = 0; Slot < ComponentOverlayBindingCount; ++Slot)
    {
        Bindings[Slot].binding         = Slot;
        Bindings[Slot].descriptorCount = 1;
        Bindings[Slot].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        Bindings[Slot].descriptorType  = (Slot < 2) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                   : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }

    VkDescriptorSetLayoutCreateInfo SetLayoutInfo = {};
    SetLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetLayoutInfo.bindingCount = ComponentOverlayBindingCount;
    SetLayoutInfo.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &SetLayoutInfo, Host.Allocator, &Inscription.SetLayout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        ISSUE_FAULT("component-overlay", "descriptor set layout creation failed");
        return false;
    }

    // -- Descriptor pool + set: two image samplers and six storage buffers ------------------------------------------------
    VkDescriptorPoolSize PoolSizes[2] = {};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSizes[0].descriptorCount = 2;
    PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSizes[1].descriptorCount = ComponentOverlayBindingCount - 2u;   // every binding past the two images is a storage buffer

    VkDescriptorPoolCreateInfo PoolInfo = {};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.maxSets       = 1;
    PoolInfo.poolSizeCount = 2;
    PoolInfo.pPoolSizes    = PoolSizes;
    if (vkCreateDescriptorPool(Host.Device, &PoolInfo, Host.Allocator, &Inscription.DescriptorPool) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        FinalizeComponentOverlayInscription(Inscription);
        ISSUE_FAULT("component-overlay", "descriptor pool creation failed");
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocateInfo = {};
    SetAllocateInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetAllocateInfo.descriptorPool     = Inscription.DescriptorPool;
    SetAllocateInfo.descriptorSetCount = 1;
    SetAllocateInfo.pSetLayouts        = &Inscription.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocateInfo, &Inscription.ResourceSet) != VK_SUCCESS)
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        FinalizeComponentOverlayInscription(Inscription);
        ISSUE_FAULT("component-overlay", "descriptor set allocation failed");
        return false;
    }

    // -- Point sampler (nearest / clamp) ---------------------------------------------------------------------------------
    // ⚠️ A LINEAR filter on the id would average two unrelated ordinals into a third, meaningless one — here that means reconstructing an entirely
    //    wrong triangle and drawing handles on geometry the pixel does not belong to. NEAREST is a correctness requirement.
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
        FinalizeComponentOverlayInscription(Inscription);
        ISSUE_FAULT("component-overlay", "point sampler creation failed");
        return false;
    }

    // -- Pipeline layout: the one set + the ComponentConstants push range (fragment stage) --------------------------------
    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(ComponentOverlayConstants);

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
        FinalizeComponentOverlayInscription(Inscription);
        ISSUE_FAULT("component-overlay", "pipeline layout creation failed");
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

    // 📝 Alpha-over blend so a face tint reads as a wash over the shaded material rather than replacing it, and so handle alpha is honoured. The
    //    fragment stage discards every non-handle pixel, so the blend only ever runs where a handle actually is.
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
        FinalizeComponentOverlayInscription(Inscription);
        ISSUE_FAULT("component-overlay", "graphics pipeline creation failed (VkResult %d)", (int)Outcome);
        return false;
    }

    Inscription.ReadyCondition = true;
    ISSUE_NOTICE("component-overlay", "component overlay ready");
    return true;
}

void RefreshComponentOverlayInscription(ComponentOverlayInscription& Inscription,
                                        const VisibilityImage&       Image,
                                        const VisibilityDepth&       Depth,
                                        VkBuffer                     VertexBuffer,
                                        VkDeviceSize                 VertexByteCapacity,
                                        VkBuffer                     IndexBuffer,
                                        VkDeviceSize                 IndexByteCapacity,
                                        VkBuffer                     InstanceBuffer,
                                        VkDeviceSize                 InstanceByteCapacity)
{
    if (!Inscription.ReadyCondition || Inscription.ResourceSet == VK_NULL_HANDLE)
        return;
    if (!Image.ReadyCondition || Image.IdView    == VK_NULL_HANDLE)
        return;
    if (!Depth.ReadyCondition || Depth.DepthView == VK_NULL_HANDLE)
        return;
    // 📝 Every geometry buffer is mandatory: the reconstruction indexes all three, so a missing one would leave a descriptor unbound and the shader
    //    sampling undefined memory. All-or-nothing, and Record self-skips while any handle is null.
    if (VertexBuffer == VK_NULL_HANDLE || IndexBuffer == VK_NULL_HANDLE || InstanceBuffer == VK_NULL_HANDLE)
        return;
    if (VertexByteCapacity == 0 || IndexByteCapacity == 0 || InstanceByteCapacity == 0)
        return;

    // 📝 Cheap to call every frame: the write is skipped unless a handle actually changed (a resize rebuilt the views, or a scene reload
    //    reallocated the geometry).
    if (Inscription.BoundIdView         == Image.IdView
     && Inscription.BoundDepthView      == Depth.DepthView
     && Inscription.BoundVertexBuffer   == VertexBuffer
     && Inscription.BoundIndexBuffer    == IndexBuffer
     && Inscription.BoundInstanceBuffer == InstanceBuffer)
        return;

    // ⚠️ ORDERING HAZARD. This function can run BEFORE the authored tables are uploaded (it is called at build time and on every resize, while the upload
    //    happens once at scene load), and a descriptor set must have every binding in its layout populated before the shader reads it — an unbound
    //    binding is undefined memory, not a readable empty buffer. So bindings 5-7 are always written: with the real tables when they exist, and
    //    otherwise aliased onto the index buffer purely to keep them legally bound. The shader never trusts those stand-ins on their own; it gates every
    //    authored read on AuthoredTriangleCount having been reported through the push block, so an aliased table reads as "topology unavailable".
    const bool          TopologyResident = Inscription.AuthoredTriangleCount != 0
                                        && Inscription.SourceFaceBuffer   != VK_NULL_HANDLE
                                        && Inscription.CornerVertexBuffer != VK_NULL_HANDLE
                                        && Inscription.SideEdgeBuffer     != VK_NULL_HANDLE;
    const VkBuffer      FacePlaceholder  = TopologyResident ? Inscription.SourceFaceBuffer   : IndexBuffer;
    const VkDeviceSize  FaceRange        = TopologyResident ? Inscription.SourceFaceCapacity   : IndexByteCapacity;
    const VkBuffer      CornerPlaceholder= TopologyResident ? Inscription.CornerVertexBuffer : IndexBuffer;
    const VkDeviceSize  CornerRange      = TopologyResident ? Inscription.CornerVertexCapacity : IndexByteCapacity;
    const VkBuffer      EdgePlaceholder  = TopologyResident ? Inscription.SideEdgeBuffer     : IndexBuffer;
    const VkDeviceSize  EdgeRange        = TopologyResident ? Inscription.SideEdgeCapacity     : IndexByteCapacity;

    VkDescriptorImageInfo IdInfo = {};
    IdInfo.sampler     = Inscription.PointSampler;
    IdInfo.imageView   = Image.IdView;
    IdInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo DepthInfo = {};
    DepthInfo.sampler   = Inscription.PointSampler;
    DepthInfo.imageView = Depth.DepthView;
    // ⚠️ SHADER_READ_ONLY_OPTIMAL, *not* DEPTH_STENCIL_READ_ONLY_OPTIMAL — the same trap SelectionOutlineInscription fell into. The preamble's
    //    TransitionVisibilityDepthForSampling leaves this image in SHADER_READ_ONLY for the HiZ reduce and nothing moves it back before the colour
    //    scope, so naming the depth-aspect layout here trips VUID-vkCmdDraw-imageLayout-00344 on every draw. Read VisibilityDepth.cpp, not intuition.
    DepthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo VertexInfo = {};
    VertexInfo.buffer = VertexBuffer;
    VertexInfo.offset = 0;
    VertexInfo.range  = VertexByteCapacity;

    VkDescriptorBufferInfo IndexInfo = {};
    IndexInfo.buffer = IndexBuffer;
    IndexInfo.offset = 0;
    IndexInfo.range  = IndexByteCapacity;

    VkDescriptorBufferInfo InstanceInfo = {};
    InstanceInfo.buffer = InstanceBuffer;
    InstanceInfo.offset = 0;
    InstanceInfo.range  = InstanceByteCapacity;

    VkDescriptorBufferInfo FaceInfo = {};
    FaceInfo.buffer = FacePlaceholder;
    FaceInfo.offset = 0;
    FaceInfo.range  = FaceRange;

    VkDescriptorBufferInfo CornerInfo = {};
    CornerInfo.buffer = CornerPlaceholder;
    CornerInfo.offset = 0;
    CornerInfo.range  = CornerRange;

    VkDescriptorBufferInfo EdgeInfo = {};
    EdgeInfo.buffer = EdgePlaceholder;
    EdgeInfo.offset = 0;
    EdgeInfo.range  = EdgeRange;

    VkWriteDescriptorSet Writes[ComponentOverlayBindingCount] = {};
    for (uint32_t Slot = 0; Slot < ComponentOverlayBindingCount; ++Slot)
    {
        Writes[Slot].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Slot].dstSet          = Inscription.ResourceSet;
        Writes[Slot].dstBinding      = Slot;
        Writes[Slot].descriptorCount = 1;
        Writes[Slot].descriptorType  = (Slot < 2) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                  : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    Writes[0].pImageInfo  = &IdInfo;
    Writes[1].pImageInfo  = &DepthInfo;
    Writes[2].pBufferInfo = &VertexInfo;
    Writes[3].pBufferInfo = &IndexInfo;
    Writes[4].pBufferInfo = &InstanceInfo;
    Writes[5].pBufferInfo = &FaceInfo;
    Writes[6].pBufferInfo = &CornerInfo;
    Writes[7].pBufferInfo = &EdgeInfo;

    vkUpdateDescriptorSets(Inscription.Host->Device, ComponentOverlayBindingCount, Writes, 0, nullptr);
    Inscription.BoundIdView         = Image.IdView;
    Inscription.BoundDepthView      = Depth.DepthView;
    Inscription.BoundVertexBuffer   = VertexBuffer;
    Inscription.BoundIndexBuffer    = IndexBuffer;
    Inscription.BoundInstanceBuffer = InstanceBuffer;
    ISSUE_TRACE("component-overlay", "descriptors re-pointed at id + depth + geometry%s",
                TopologyResident ? " + authored topology" : " (authored topology not yet uploaded)");
}

bool UploadComponentOverlayTopology(ComponentOverlayInscription& Inscription,
                                    const std::vector<uint32_t>& SourceFace,
                                    const std::vector<uint32_t>& CornerVertex,
                                    const std::vector<uint32_t>& SideEdge)
{
    Inscription.AuthoredTriangleCount = 0;   // cleared first: a failed upload must not leave a stale count enabling reads of a half-written table
    if (Inscription.Host == nullptr || !Inscription.ReadyCondition)
        return false;
    if (SourceFace.empty())
    {
        ISSUE_CAUTION("component-overlay", "authored topology empty - component modes will draw nothing");
        return false;
    }

    // ⚠️ The three tables must agree on the triangle count, because the shader derives one index from the other (CornerBase = Primitive * 3). A mismatch
    //    means the provenance was built from a different triangulation than the one uploaded, and reading it would silently mis-key every handle rather
    //    than fail visibly — so it is rejected outright instead of clamped to the shortest.
    if (CornerVertex.size() != SourceFace.size() * 3u || SideEdge.size() != SourceFace.size() * 3u)
    {
        ISSUE_FAULT("component-overlay", "authored topology inconsistent (faces %zu, corners %zu, sides %zu) - expected 3 per triangle",
                    SourceFace.size(), CornerVertex.size(), SideEdge.size());
        return false;
    }

    if (!UploadTopologyTable(*Inscription.Host, SourceFace,   Inscription.SourceFaceBuffer,   Inscription.SourceFaceMemory,   Inscription.SourceFaceCapacity)
     || !UploadTopologyTable(*Inscription.Host, CornerVertex, Inscription.CornerVertexBuffer, Inscription.CornerVertexMemory, Inscription.CornerVertexCapacity)
     || !UploadTopologyTable(*Inscription.Host, SideEdge,     Inscription.SideEdgeBuffer,     Inscription.SideEdgeMemory,     Inscription.SideEdgeCapacity))
    {
        ISSUE_FAULT("component-overlay", "authored topology upload failed - component modes will draw nothing");
        return false;
    }

    Inscription.AuthoredTriangleCount = (uint32_t)SourceFace.size();

    // 📝 Force the descriptor re-point. Refresh early-returns when the id / depth / geometry handles are unchanged — which they are here, since only the
    //    OWNED topology buffers moved — so without invalidating one of the tracked handles the set would keep pointing at the aliased stand-ins and the
    //    freshly uploaded tables would never be read.
    Inscription.BoundIdView = VK_NULL_HANDLE;

    ISSUE_NOTICE("component-overlay", "authored topology uploaded (%u triangles, %zu face + %zu corner + %zu side entries)",
                 Inscription.AuthoredTriangleCount, SourceFace.size(), CornerVertex.size(), SideEdge.size());
    return true;
}

void RecordComponentOverlayInscription(const ComponentOverlayInscription& Inscription,
                                       VkExtent2D                         Extent,
                                       const ComponentOverlayConstants&   Constants,
                                       VkCommandBuffer                    CommandBuffer)
{
    if (!Inscription.ReadyCondition || Inscription.Pipeline == VK_NULL_HANDLE || Inscription.ResourceSet == VK_NULL_HANDLE)
        return;
    // 📝 never refreshed against live resources — sampling / indexing an unbound descriptor is undefined
    if (Inscription.BoundIdView       == VK_NULL_HANDLE || Inscription.BoundDepthView   == VK_NULL_HANDLE
     || Inscription.BoundVertexBuffer == VK_NULL_HANDLE || Inscription.BoundIndexBuffer == VK_NULL_HANDLE
     || Inscription.BoundInstanceBuffer == VK_NULL_HANDLE)
        return;
    // 📝 Object mode draws no handles, so skip the fullscreen pass entirely rather than dispatch a shader that discards every pixel.
    if (Constants.ComponentMode == (uint32_t)ComponentSelectionMode::Object)
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
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Inscription.PipelineLayout, 0, 1, &Inscription.ResourceSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Inscription.PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ComponentOverlayConstants), &Constants);
    vkCmdDraw(CommandBuffer, 3, 1, 0, 0);
}

void FinalizeComponentOverlayInscription(ComponentOverlayInscription& Inscription)
{
    if (Inscription.Host != nullptr && Inscription.Host->Device != VK_NULL_HANDLE)
    {
        VkDevice                     Device    = Inscription.Host->Device;
        const VkAllocationCallbacks* Allocator = Inscription.Host->Allocator;

        if (Inscription.Pipeline       != VK_NULL_HANDLE) vkDestroyPipeline(Device, Inscription.Pipeline, Allocator);
        if (Inscription.PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Inscription.PipelineLayout, Allocator);
        if (Inscription.PointSampler   != VK_NULL_HANDLE) vkDestroySampler(Device, Inscription.PointSampler, Allocator);
        // 📝 The set is freed with the pool; no separate vkFreeDescriptorSets.
        if (Inscription.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Inscription.DescriptorPool, Allocator);
        if (Inscription.SetLayout      != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Inscription.SetLayout, Allocator);

        // 📝 The authored-topology tables are the only device memory this unit OWNS (the geometry SSBOs are borrowed and belong to the raster), so they
        //    are the only ones it must release.
        if (Inscription.SourceFaceBuffer   != VK_NULL_HANDLE) vkDestroyBuffer(Device, Inscription.SourceFaceBuffer, Allocator);
        if (Inscription.SourceFaceMemory   != VK_NULL_HANDLE) vkFreeMemory(Device, Inscription.SourceFaceMemory, Allocator);
        if (Inscription.CornerVertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, Inscription.CornerVertexBuffer, Allocator);
        if (Inscription.CornerVertexMemory != VK_NULL_HANDLE) vkFreeMemory(Device, Inscription.CornerVertexMemory, Allocator);
        if (Inscription.SideEdgeBuffer     != VK_NULL_HANDLE) vkDestroyBuffer(Device, Inscription.SideEdgeBuffer, Allocator);
        if (Inscription.SideEdgeMemory     != VK_NULL_HANDLE) vkFreeMemory(Device, Inscription.SideEdgeMemory, Allocator);
    }

    Inscription = ComponentOverlayInscription{};
}

} // namespace Frontier

#endif // FRONTIER_POLYGON_AUTHORING
