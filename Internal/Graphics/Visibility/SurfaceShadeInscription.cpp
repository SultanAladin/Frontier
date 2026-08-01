/*==============================================================================================================================================
                                                       SURFACESHADEINSCRIPTION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the surface shade. Initialize reads the two SPIR-V modules (the fullscreen-triangle vertex stage is VisibilityInscription's,
//    reused verbatim), builds an eight-binding descriptor set layout + pool + set, one nearest sampler, the owned material UBO, a pipeline layout
//    carrying that set and the ShadeConstants push range, and a graphics pipeline configured for dynamic rendering against the swapchain colour format
//    (alpha-over blend, no depth, no vertex input). Refresh points the set at the borrowed visibility image view and the mesh / instance buffers.
//    Record binds the pipeline + set, pushes the constants, and draws the three-vertex triangle inside the caller's open colour scope.
//
//    The structure deliberately mirrors VisibilityInscription.cpp — same helper shapes, same failure-unwind pattern, same idempotent-Refresh rule —
//    so the two units read as one family rather than two dialects. What differs is only the binding count and the owned UBO.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Visibility/SurfaceShadeInscription.h"
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

// The descriptor bindings, named so the layout / pool / write code below cannot drift out of step with the shader's declarations.
constexpr uint32_t BindingVisibilityImage = 0;
constexpr uint32_t BindingMeshVertices    = 1;
constexpr uint32_t BindingMeshIndices     = 2;
constexpr uint32_t BindingInstances       = 3;
constexpr uint32_t BindingMaterials       = 4;
constexpr uint32_t BindingFloorVertices   = 5;
constexpr uint32_t BindingFloorIndices    = 6;
constexpr uint32_t BindingFloorInstances  = 7;
constexpr uint32_t BindingCount           = 8;

// Read a whole SPIR-V file into a byte buffer. Empty on failure (missing / unreadable), which the caller treats as "skip".
std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
{
    std::vector<char> Bytes;
    FILE* Handle = std::fopen(FilePath.c_str(), "rb");
    if (Handle == nullptr)
    {
        ISSUE_CAUTION("surface-shade", "shader module not found: %s", FilePath.c_str());
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
// per-component helper the other visibility units carry (VisibilityImage / VisibilityInscription) — no shared home exists yet.
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

// Allocate a host-visible + host-coherent buffer of ByteCapacity with the given usage. On any failure both out handles are null. The material table
// is 14 * 96 B and immutable after upload, so host-visible + mapped-once is the right shape — no staging transfer.
bool ConstructHostBuffer(VulkanHost&        Host,
                         VkDeviceSize       ByteCapacity,
                         VkBufferUsageFlags Usage,
                         VkBuffer&          OutBuffer,
                         VkDeviceMemory&    OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteCapacity;
    BufferInformation.usage       = Usage;
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

bool InitializeSurfaceShadeInscription(SurfaceShadeInscription& Shade,
                                       VulkanHost&              Host,
                                       VkFormat                 ColourFormat,
                                       const char*              ShaderDirectory)
{
    Shade = SurfaceShadeInscription{};
    Shade.Host = &Host;
    if (!Host.DynamicRenderingEnabled || Host.Device == VK_NULL_HANDLE)
        return false;

    // -- Shader modules. The fullscreen-triangle vertex stage is VisibilityInscription's, reused as-is: it synthesizes its three corners from
    //    gl_VertexIndex and carries no pass-specific state, so a second identical copy would only be a second thing to keep in sync. ------------
    const std::string Directory     = ShaderDirectory;
    VkShaderModule    VertexModule   = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/VisibilityInscription.vert.spv"));
    VkShaderModule    FragmentModule = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/SurfaceShade.frag.spv"));
    if (VertexModule == VK_NULL_HANDLE || FragmentModule == VK_NULL_HANDLE)
    {
        if (VertexModule   != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        if (FragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        ISSUE_CAUTION("surface-shade", "pipeline not built — shader modules unavailable, surfaces will not shade");
        return false;
    }

    // A single unwind path for every failure after the modules exist, so no branch below can leak them.
    auto ReleaseModules = [&]()
    {
        vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
    };

    // -- Descriptor set layout: b0 = id sampler, b1/b2/b3 = head vertices / indices / instances, b4 = material table,
    //    b5/b6/b7 = the floor's own vertices / indices / instances (all fragment stage) ------------------------------------
    VkDescriptorSetLayoutBinding Bindings[BindingCount] = {};
    Bindings[0].binding         = BindingVisibilityImage;
    Bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Bindings[0].descriptorCount = 1;
    Bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[1].binding         = BindingMeshVertices;
    Bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[1].descriptorCount = 1;
    Bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[2].binding         = BindingMeshIndices;
    Bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[2].descriptorCount = 1;
    Bindings[2].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[3].binding         = BindingInstances;
    Bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[3].descriptorCount = 1;
    Bindings[3].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[4].binding         = BindingMaterials;
    Bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    Bindings[4].descriptorCount = 1;
    Bindings[4].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[5].binding         = BindingFloorVertices;
    Bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[5].descriptorCount = 1;
    Bindings[5].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[6].binding         = BindingFloorIndices;
    Bindings[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[6].descriptorCount = 1;
    Bindings[6].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[7].binding         = BindingFloorInstances;
    Bindings[7].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[7].descriptorCount = 1;
    Bindings[7].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo SetLayoutInfo = {};
    SetLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetLayoutInfo.bindingCount = BindingCount;
    SetLayoutInfo.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &SetLayoutInfo, Host.Allocator, &Shade.SetLayout) != VK_SUCCESS)
    {
        ReleaseModules();
        ISSUE_FAULT("surface-shade", "descriptor set layout creation failed");
        return false;
    }

    // -- Descriptor pool + set. ONE sampler (the id image), SIX storage buffers (three head + three floor) and ONE uniform buffer (the material
    //    table). ⚠️ These counts must track the binding list above exactly: an undersized pool fails allocation outright rather than degrading, which
    //    is the good outcome, but it fails at bring-up far from the binding that caused it. -------------------------------------------------------
    VkDescriptorPoolSize PoolSizes[3] = {};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSizes[0].descriptorCount = 1;
    PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSizes[1].descriptorCount = 6;
    PoolSizes[2].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    PoolSizes[2].descriptorCount = 1;

    VkDescriptorPoolCreateInfo PoolInfo = {};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.maxSets       = 1;
    PoolInfo.poolSizeCount = 3;
    PoolInfo.pPoolSizes    = PoolSizes;
    if (vkCreateDescriptorPool(Host.Device, &PoolInfo, Host.Allocator, &Shade.DescriptorPool) != VK_SUCCESS)
    {
        ReleaseModules();
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "descriptor pool creation failed");
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocate = {};
    SetAllocate.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetAllocate.descriptorPool     = Shade.DescriptorPool;
    SetAllocate.descriptorSetCount = 1;
    SetAllocate.pSetLayouts        = &Shade.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocate, &Shade.ShadeSet) != VK_SUCCESS)
    {
        ReleaseModules();
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "descriptor set allocation failed");
        return false;
    }

    // -- Point sampler (nearest / clamp). A filtered identity is not a blend of two surfaces — it is a DIFFERENT, probably nonexistent triangle,
    //    so linear filtering here would fetch garbage geometry rather than soften an edge. -------------------------------------------------------
    VkSamplerCreateInfo SamplerInfo = {};
    SamplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SamplerInfo.magFilter    = VK_FILTER_NEAREST;
    SamplerInfo.minFilter    = VK_FILTER_NEAREST;
    SamplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(Host.Device, &SamplerInfo, Host.Allocator, &Shade.PointSampler) != VK_SUCCESS)
    {
        ReleaseModules();
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "point sampler creation failed");
        return false;
    }

    // -- The owned material UBO (14 records; immutable after upload) -----------------------------------------------------
    const VkDeviceSize MaterialBytes = (VkDeviceSize)SurfacePresetCount * sizeof(SurfacePresetParameters);
    if (!ConstructHostBuffer(Host, MaterialBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, Shade.MaterialBuffer, Shade.MaterialMemory))
    {
        ReleaseModules();
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "material uniform buffer allocation failed");
        return false;
    }

    // -- Pipeline layout: the one set + the ShadeConstants push range (fragment stage) -----------------------------------
    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(SurfaceShadeConstants);

    VkPipelineLayoutCreateInfo LayoutInfo = {};
    LayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutInfo.setLayoutCount         = 1;
    LayoutInfo.pSetLayouts            = &Shade.SetLayout;
    LayoutInfo.pushConstantRangeCount = 1;
    LayoutInfo.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &LayoutInfo, Host.Allocator, &Shade.PipelineLayout) != VK_SUCCESS)
    {
        ReleaseModules();
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "pipeline layout creation failed");
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
    //    The alpha-over blend is not incidental: it is what makes the Glass preset transparent for free, since the shade writes a sub-1 alpha there
    //    and fixed-function blending composites it over the sky / grid already in the scope.
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
    PipelineInfo.layout              = Shade.PipelineLayout;

    VkResult Outcome = vkCreateGraphicsPipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInfo, Host.Allocator, &Shade.Pipeline);

    ReleaseModules();

    if (Outcome != VK_SUCCESS)
    {
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "graphics pipeline creation failed (VkResult %d)", (int)Outcome);
        return false;
    }

    Shade.ReadyCondition = true;
    ISSUE_NOTICE("surface-shade", "surface shade ready");
    return true;
}

void UploadSurfaceShadeMaterials(SurfaceShadeInscription& Shade, const SurfacePresetParameters* Table)
{
    if (!Shade.ReadyCondition || Shade.Host == nullptr || Shade.MaterialMemory == VK_NULL_HANDLE || Table == nullptr)
        return;

    VulkanHost&        Host          = *Shade.Host;
    const VkDeviceSize MaterialBytes = (VkDeviceSize)SurfacePresetCount * sizeof(SurfacePresetParameters);

    void* Mapped = nullptr;
    if (vkMapMemory(Host.Device, Shade.MaterialMemory, 0, MaterialBytes, 0, &Mapped) != VK_SUCCESS)
    {
        ISSUE_CAUTION("surface-shade", "material table map failed — surfaces will shade from an uninitialized table");
        return;
    }
    std::memcpy(Mapped, Table, (size_t)MaterialBytes);
    vkUnmapMemory(Host.Device, Shade.MaterialMemory);

    // Point b4 at the table. Uploaded once before the render loop, so no in-flight set is being rewritten here.
    VkDescriptorBufferInfo BufferInfo = {};
    BufferInfo.buffer = Shade.MaterialBuffer;
    BufferInfo.offset = 0;
    BufferInfo.range  = MaterialBytes;

    VkWriteDescriptorSet Write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    Write.dstSet          = Shade.ShadeSet;
    Write.dstBinding      = BindingMaterials;
    Write.descriptorCount = 1;
    Write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    Write.pBufferInfo     = &BufferInfo;
    vkUpdateDescriptorSets(Host.Device, 1, &Write, 0, nullptr);
}

void RefreshSurfaceShadeInscription(SurfaceShadeInscription& Shade,
                                    const VisibilityImage&   Image,
                                    VkBuffer                 VertexBuffer,
                                    VkDeviceSize             VertexBytes,
                                    VkBuffer                 IndexBuffer,
                                    VkDeviceSize             IndexBytes,
                                    VkBuffer                 InstanceBuffer,
                                    VkDeviceSize             InstanceBytes,
                                    VkBuffer                 FloorVertexBuffer,
                                    VkDeviceSize             FloorVertexBytes,
                                    VkBuffer                 FloorIndexBuffer,
                                    VkDeviceSize             FloorIndexBytes,
                                    VkBuffer                 FloorInstanceBuffer,
                                    VkDeviceSize             FloorInstanceBytes)
{
    if (!Shade.ReadyCondition || Shade.ShadeSet == VK_NULL_HANDLE)
        return;
    if (!Image.ReadyCondition || Image.IdView == VK_NULL_HANDLE)
        return;
    if (VertexBuffer == VK_NULL_HANDLE || IndexBuffer == VK_NULL_HANDLE || InstanceBuffer == VK_NULL_HANDLE)
        return;

    // 🔴 All three floor handles or none: a partial set would leave one binding undefined while the other two looked real, and the shader has a single
    //    flag to branch on rather than three. Falling back to the head-buffer alias keeps every descriptor defined, and FloorGeometryBound records
    //    which of the two states b5-b7 are actually in so the caller cannot enable floor shading against the alias.
    const bool FloorPresent = FloorVertexBuffer   != VK_NULL_HANDLE
                           && FloorIndexBuffer    != VK_NULL_HANDLE
                           && FloorInstanceBuffer != VK_NULL_HANDLE;

    const VkBuffer     FloorVertexTarget   = FloorPresent ? FloorVertexBuffer   : VertexBuffer;
    const VkDeviceSize FloorVertexRange    = FloorPresent ? FloorVertexBytes    : VertexBytes;
    const VkBuffer     FloorIndexTarget    = FloorPresent ? FloorIndexBuffer    : IndexBuffer;
    const VkDeviceSize FloorIndexRange     = FloorPresent ? FloorIndexBytes     : IndexBytes;
    const VkBuffer     FloorInstanceTarget = FloorPresent ? FloorInstanceBuffer : InstanceBuffer;
    const VkDeviceSize FloorInstanceRange  = FloorPresent ? FloorInstanceBytes  : InstanceBytes;

    // Idempotent, for the same reason VisibilityInscription's Refresh is: this set is bound every frame the shade records, so rewriting it when
    // nothing changed would trip the "descriptor in use by a pending command buffer" rule for no benefit. Only bring-up and resize actually differ,
    // and the caller idles the device on those. Steady state issues zero vkUpdateDescriptorSets.
    const bool Unchanged = Shade.BoundIdView               == Image.IdView
                        && Shade.BoundVertexBuffer         == VertexBuffer
                        && Shade.BoundIndexBuffer          == IndexBuffer
                        && Shade.BoundInstanceBuffer       == InstanceBuffer
                        && Shade.BoundFloorVertexBuffer    == FloorVertexTarget
                        && Shade.BoundFloorIndexBuffer     == FloorIndexTarget
                        && Shade.BoundFloorInstanceBuffer  == FloorInstanceTarget
                        && Shade.FloorGeometryBound        == FloorPresent;
    if (Unchanged)
        return;

    VkDescriptorImageInfo ImageInfo = {};
    ImageInfo.sampler     = Shade.PointSampler;
    ImageInfo.imageView   = Image.IdView;
    ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo VertexInfo   = { VertexBuffer,   0, VertexBytes };
    VkDescriptorBufferInfo IndexInfo    = { IndexBuffer,    0, IndexBytes };
    VkDescriptorBufferInfo InstanceInfo = { InstanceBuffer, 0, InstanceBytes };

    VkDescriptorBufferInfo FloorVertexInfo   = { FloorVertexTarget,   0, FloorVertexRange };
    VkDescriptorBufferInfo FloorIndexInfo    = { FloorIndexTarget,    0, FloorIndexRange };
    VkDescriptorBufferInfo FloorInstanceInfo = { FloorInstanceTarget, 0, FloorInstanceRange };

    VkWriteDescriptorSet Writes[7] = {};
    Writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[0].dstSet          = Shade.ShadeSet;
    Writes[0].dstBinding      = BindingVisibilityImage;
    Writes[0].descriptorCount = 1;
    Writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Writes[0].pImageInfo      = &ImageInfo;

    Writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[1].dstSet          = Shade.ShadeSet;
    Writes[1].dstBinding      = BindingMeshVertices;
    Writes[1].descriptorCount = 1;
    Writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[1].pBufferInfo     = &VertexInfo;

    Writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[2].dstSet          = Shade.ShadeSet;
    Writes[2].dstBinding      = BindingMeshIndices;
    Writes[2].descriptorCount = 1;
    Writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[2].pBufferInfo     = &IndexInfo;

    Writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[3].dstSet          = Shade.ShadeSet;
    Writes[3].dstBinding      = BindingInstances;
    Writes[3].descriptorCount = 1;
    Writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[3].pBufferInfo     = &InstanceInfo;

    Writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[4].dstSet          = Shade.ShadeSet;
    Writes[4].dstBinding      = BindingFloorVertices;
    Writes[4].descriptorCount = 1;
    Writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[4].pBufferInfo     = &FloorVertexInfo;

    Writes[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[5].dstSet          = Shade.ShadeSet;
    Writes[5].dstBinding      = BindingFloorIndices;
    Writes[5].descriptorCount = 1;
    Writes[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[5].pBufferInfo     = &FloorIndexInfo;

    Writes[6].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[6].dstSet          = Shade.ShadeSet;
    Writes[6].dstBinding      = BindingFloorInstances;
    Writes[6].descriptorCount = 1;
    Writes[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[6].pBufferInfo     = &FloorInstanceInfo;

    vkUpdateDescriptorSets(Shade.Host->Device, 7, Writes, 0, nullptr);

    Shade.BoundIdView                = Image.IdView;
    Shade.BoundVertexBuffer          = VertexBuffer;
    Shade.BoundIndexBuffer           = IndexBuffer;
    Shade.BoundInstanceBuffer        = InstanceBuffer;
    Shade.BoundFloorVertexBuffer     = FloorVertexTarget;
    Shade.BoundFloorIndexBuffer      = FloorIndexTarget;
    Shade.BoundFloorInstanceBuffer   = FloorInstanceTarget;
    Shade.FloorGeometryBound         = FloorPresent;
}

void RecordSurfaceShadeInscription(const SurfaceShadeInscription& Shade,
                                   VkExtent2D                     Extent,
                                   const SurfaceShadeConstants&   Constants,
                                   VkCommandBuffer                CommandBuffer)
{
    if (!Shade.ReadyCondition || Shade.ShadeSet == VK_NULL_HANDLE)
        return;
    // Nothing has been bound yet (Refresh has not run, or ran before the mesh existed) — recording now would read undefined descriptors.
    if (Shade.BoundIdView == VK_NULL_HANDLE || Shade.BoundVertexBuffer == VK_NULL_HANDLE)
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

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Shade.Pipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Shade.PipelineLayout,
                            0, 1, &Shade.ShadeSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Shade.PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(SurfaceShadeConstants), &Constants);
    vkCmdDraw(CommandBuffer, 3, 1, 0, 0);
}

void FinalizeSurfaceShadeInscription(SurfaceShadeInscription& Shade)
{
    if (Shade.Host == nullptr || Shade.Host->Device == VK_NULL_HANDLE)
    {
        Shade = SurfaceShadeInscription{};
        return;
    }
    VkDevice                     Device    = Shade.Host->Device;
    const VkAllocationCallbacks* Allocator = Shade.Host->Allocator;

    if (Shade.Pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(Device, Shade.Pipeline, Allocator);
    if (Shade.PipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(Device, Shade.PipelineLayout, Allocator);
    if (Shade.PointSampler != VK_NULL_HANDLE)
        vkDestroySampler(Device, Shade.PointSampler, Allocator);
    if (Shade.DescriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(Device, Shade.DescriptorPool, Allocator);   // frees ShadeSet
    if (Shade.SetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(Device, Shade.SetLayout, Allocator);
    if (Shade.MaterialBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(Device, Shade.MaterialBuffer, Allocator);
    if (Shade.MaterialMemory != VK_NULL_HANDLE)
        vkFreeMemory(Device, Shade.MaterialMemory, Allocator);

    Shade = SurfaceShadeInscription{};
}

} // namespace Frontier
