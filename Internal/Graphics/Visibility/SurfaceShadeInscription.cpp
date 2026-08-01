/*==============================================================================================================================================
                                                       SURFACESHADEINSCRIPTION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the surface shade. Initialize reads the two SPIR-V modules (the fullscreen-triangle vertex stage is VisibilityInscription's,
//    reused verbatim), builds an eleven-binding descriptor set layout + pool + set, two nearest samplers, the owned material and sun-shadow-trace UBOs,
//    a pipeline layout carrying that set and the ShadeConstants push range, and a graphics pipeline configured for dynamic rendering against the
//    swapchain colour format (alpha-over blend, no depth, no vertex input). Refresh points the set at the borrowed visibility image view, the mesh /
//    instance buffers, and the sun shadow atlas + page mapping. Record binds the pipeline + set, pushes the constants, and draws the three-vertex
//    triangle inside the caller's open colour scope.
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
constexpr uint32_t BindingShadowAtlas     = 8;
constexpr uint32_t BindingShadowMapping   = 9;
constexpr uint32_t BindingShadowTrace     = 10;
constexpr uint32_t BindingShadowCoverage  = 11;
constexpr uint32_t BindingCount           = 12;

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
    //    b5/b6/b7 = the floor's own vertices / indices / instances, b8 = sun shadow atlas, b9 = tile->page mapping,
    //    b10 = the per-image trace block, b11 = the per-page coverage words (all fragment stage) ---------------------------
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
    Bindings[8].binding         = BindingShadowAtlas;
    Bindings[8].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Bindings[8].descriptorCount = 1;
    Bindings[8].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[9].binding         = BindingShadowMapping;
    Bindings[9].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[9].descriptorCount = 1;
    Bindings[9].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[10].binding         = BindingShadowTrace;
    Bindings[10].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    Bindings[10].descriptorCount = 1;
    Bindings[10].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    Bindings[11].binding         = BindingShadowCoverage;
    Bindings[11].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[11].descriptorCount = 1;
    Bindings[11].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

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

    // -- Descriptor pool + set. TWO samplers (id + shadow atlas), EIGHT storage buffers (three head + three floor + the page mapping + the page
    //    coverage) and TWO uniform buffers (the material table + the trace block). ⚠️ These counts must track the binding list above exactly: an
    //    undersized pool fails allocation outright rather than degrading, which is the good outcome, but it fails at bring-up far from the binding
    //    that caused it. -------------------------------------------------------------------------------------------------------------------------
    VkDescriptorPoolSize PoolSizes[3] = {};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSizes[0].descriptorCount = 2;
    PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSizes[1].descriptorCount = 8;
    PoolSizes[2].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    PoolSizes[2].descriptorCount = 2;

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

    // -- The shadow atlas sampler. 🔴 NEAREST IS A CORRECTNESS REQUIREMENT, NOT A QUALITY CHOICE. The atlas holds a monotonic uint depth ENCODING, so
    //    the average of two encoded depths is not the depth of anything, and a bilinear tap across a PAGE boundary averages depth from two unrelated
    //    regions of the world. That reads as a shadow with soft wrong fringes rather than as a filtering mistake. Softness comes from multiple discrete
    //    taps (the SMRT follow-up), never from the sampler. A separate sampler from PointSampler only because the two are conceptually independent —
    //    the settings happen to coincide today, and collapsing them would silently couple two unrelated correctness rules. -------------------------
    VkSamplerCreateInfo ShadowSamplerInfo = {};
    ShadowSamplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ShadowSamplerInfo.magFilter    = VK_FILTER_NEAREST;
    ShadowSamplerInfo.minFilter    = VK_FILTER_NEAREST;
    ShadowSamplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ShadowSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ShadowSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ShadowSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(Host.Device, &ShadowSamplerInfo, Host.Allocator, &Shade.ShadowSampler) != VK_SUCCESS)
    {
        ReleaseModules();
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "shadow atlas sampler creation failed");
        return false;
    }

    // -- The owned trace UBO (b10), mapped once and left mapped: it is rewritten every image, so a map/unmap pair per image would be pure overhead.
    //    HOST_COHERENT, so no explicit flush is needed — see the struct's note on why that is a bug class avoided rather than a shortcut. -----------
    if (!ConstructHostBuffer(Host, (VkDeviceSize)sizeof(SunShadowTraceBlock), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             Shade.TraceBuffer, Shade.TraceMemory))
    {
        ReleaseModules();
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "sun shadow trace uniform buffer allocation failed");
        return false;
    }
    if (vkMapMemory(Host.Device, Shade.TraceMemory, 0, (VkDeviceSize)sizeof(SunShadowTraceBlock), 0, &Shade.TraceMapping) != VK_SUCCESS)
    {
        Shade.TraceMapping = nullptr;
        ReleaseModules();
        FinalizeSurfaceShadeInscription(Shade);
        ISSUE_FAULT("surface-shade", "sun shadow trace uniform buffer map failed");
        return false;
    }

    // ⚠️ Seed the mapping with a DEFAULT-CONSTRUCTED block rather than leaving it undefined. LevelCount is 0 there, so a shade that records before the
    //    first upload walks no levels and reads fully lit — the safe direction. Undefined memory could name a level count of anything.
    {
        const SunShadowTraceBlock SeedBlock;
        std::memcpy(Shade.TraceMapping, &SeedBlock, sizeof(SunShadowTraceBlock));
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
                                    VkDeviceSize             FloorInstanceBytes,
                                    VkImageView              ShadowAtlasView,
                                    VkBuffer                 ShadowMappingBuffer,
                                    VkDeviceSize             ShadowMappingBytes,
                                    VkBuffer                 ShadowCoverageBuffer,
                                    VkDeviceSize             ShadowCoverageBytes)
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

    // 🔴 Both shadow handles or neither, for the floor triple's reason and one more: a resident atlas with no mapping is unaddressable (the mapping IS
    //    the addressing — there is no affine map from a world position to an atlas texel), and a mapping with no atlas has nothing to read. The alias
    //    targets are chosen for TYPE compatibility: the visibility image is R32_UINT exactly as the atlas is, so b8's usampler2D is satisfied, and the
    //    index buffer is a uint SSBO exactly as the mapping is. ⚠️ Both aliases READ CLEANLY AND MEAN NOTHING, which is why SunShadowEnabled — not a
    //    handle or a length test — has to be what the shader branches on.
    // 🔴 The COVERAGE buffer joins the same all-or-nothing group, and it is load-bearing rather than a third handle to keep tidy: it is what the tracer
    //    tests to decide a page was actually drawn into, so binding a real atlas against an aliased coverage array would make every page read as
    //    never-drawn (the alias holds vertex indices, some of which are zero) and the walk would fall out of every level into fully lit.
    const bool ShadowPresent = ShadowAtlasView      != VK_NULL_HANDLE
                            && ShadowMappingBuffer  != VK_NULL_HANDLE
                            && ShadowCoverageBuffer != VK_NULL_HANDLE;

    const VkImageView  ShadowAtlasTarget   = ShadowPresent ? ShadowAtlasView     : Image.IdView;
    const VkBuffer     ShadowMappingTarget = ShadowPresent ? ShadowMappingBuffer : IndexBuffer;
    const VkDeviceSize ShadowMappingRange  = ShadowPresent ? ShadowMappingBytes  : IndexBytes;
    const VkBuffer     ShadowCoverageTarget = ShadowPresent ? ShadowCoverageBuffer : IndexBuffer;
    const VkDeviceSize ShadowCoverageRange  = ShadowPresent ? ShadowCoverageBytes  : IndexBytes;

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
                        && Shade.FloorGeometryBound        == FloorPresent
                        && Shade.BoundShadowAtlasView      == ShadowAtlasTarget
                        && Shade.BoundShadowMappingBuffer  == ShadowMappingTarget
                        && Shade.BoundShadowCoverageBuffer == ShadowCoverageTarget
                        && Shade.ShadowAtlasBound          == ShadowPresent;
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

    // ⚠️ The shadow atlas is SAMPLED, so this layout must be what the atlas is actually in when the shade executes — TransitionShadowPageAtlas to
    //    SHADER_READ_ONLY_OPTIMAL after S7 and before the radiance scope opens. A descriptor naming a layout the image is not in is undefined, and the
    //    validation layer is the only thing that will say so.
    VkDescriptorImageInfo ShadowAtlasInfo = {};
    ShadowAtlasInfo.sampler     = Shade.ShadowSampler;
    ShadowAtlasInfo.imageView   = ShadowAtlasTarget;
    ShadowAtlasInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo ShadowMappingInfo  = { ShadowMappingTarget,  0, ShadowMappingRange };
    VkDescriptorBufferInfo ShadowCoverageInfo = { ShadowCoverageTarget, 0, ShadowCoverageRange };
    VkDescriptorBufferInfo TraceInfo          = { Shade.TraceBuffer,    0, (VkDeviceSize)sizeof(SunShadowTraceBlock) };

    VkWriteDescriptorSet Writes[11] = {};
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

    Writes[7].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[7].dstSet          = Shade.ShadeSet;
    Writes[7].dstBinding      = BindingShadowAtlas;
    Writes[7].descriptorCount = 1;
    Writes[7].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Writes[7].pImageInfo      = &ShadowAtlasInfo;

    Writes[8].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[8].dstSet          = Shade.ShadeSet;
    Writes[8].dstBinding      = BindingShadowMapping;
    Writes[8].descriptorCount = 1;
    Writes[8].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[8].pBufferInfo     = &ShadowMappingInfo;

    // b10 points at a buffer this unit OWNS, so it never changes handle — but it is written here anyway rather than at Initialize, because the set is
    // only allocated by then and an unwritten binding is undefined memory rather than a safely empty one.
    Writes[9].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[9].dstSet          = Shade.ShadeSet;
    Writes[9].dstBinding      = BindingShadowTrace;
    Writes[9].descriptorCount = 1;
    Writes[9].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    Writes[9].pBufferInfo     = &TraceInfo;

    Writes[10].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[10].dstSet          = Shade.ShadeSet;
    Writes[10].dstBinding      = BindingShadowCoverage;
    Writes[10].descriptorCount = 1;
    Writes[10].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[10].pBufferInfo     = &ShadowCoverageInfo;

    vkUpdateDescriptorSets(Shade.Host->Device, 11, Writes, 0, nullptr);

    Shade.BoundIdView                = Image.IdView;
    Shade.BoundVertexBuffer          = VertexBuffer;
    Shade.BoundIndexBuffer           = IndexBuffer;
    Shade.BoundInstanceBuffer        = InstanceBuffer;
    Shade.BoundFloorVertexBuffer     = FloorVertexTarget;
    Shade.BoundFloorIndexBuffer      = FloorIndexTarget;
    Shade.BoundFloorInstanceBuffer   = FloorInstanceTarget;
    Shade.FloorGeometryBound         = FloorPresent;
    Shade.BoundShadowAtlasView       = ShadowAtlasTarget;
    Shade.BoundShadowMappingBuffer   = ShadowMappingTarget;
    Shade.BoundShadowCoverageBuffer  = ShadowCoverageTarget;
    Shade.ShadowAtlasBound           = ShadowPresent;
}

void UploadSurfaceShadeTraceBlock(SurfaceShadeInscription& Shade, const SunShadowTraceBlock& Block)
{
    if (!Shade.ReadyCondition || Shade.TraceMapping == nullptr)
        return;

    // One memcpy into coherent memory. No descriptor write (b10's handle never changes) and no flush, so this is safe to call every image even while a
    // previous frame is in flight — the buffer's CONTENTS are what change, which is the same hazard class as any per-image uniform update.
    std::memcpy(Shade.TraceMapping, &Block, sizeof(SunShadowTraceBlock));
}

SunShadowTraceBlock SolveSurfaceShadeTraceBlock(const SunShadowClipmap& Clipmap,
                                                float                   DepthOriginMetres,
                                                float                   DepthRangeMetres,
                                                float                   DepthBias,
                                                SunShadowDebugView      DebugView,
                                                float                   ShadowAngleRadians,
                                                uint32_t                SoftRayCount,
                                                uint32_t                SoftStepCount)
{
    SunShadowTraceBlock Block;

    Block.LightRightAxis[0]   = Clipmap.Basis.RightAxis.XCoord;
    Block.LightRightAxis[1]   = Clipmap.Basis.RightAxis.YCoord;
    Block.LightRightAxis[2]   = Clipmap.Basis.RightAxis.ZCoord;
    Block.LightUpAxis[0]      = Clipmap.Basis.UpAxis.XCoord;
    Block.LightUpAxis[1]      = Clipmap.Basis.UpAxis.YCoord;
    Block.LightUpAxis[2]      = Clipmap.Basis.UpAxis.ZCoord;
    Block.LightForwardAxis[0] = Clipmap.Basis.ForwardAxis.XCoord;
    Block.LightForwardAxis[1] = Clipmap.Basis.ForwardAxis.YCoord;
    Block.LightForwardAxis[2] = Clipmap.Basis.ForwardAxis.ZCoord;

    // 🔴 COPIED VERBATIM, NOT NEGATED. ToroidalOrigin is already in ADD form (the negated window corner), and the shader adds it — matching
    //    ResolveSunShadowPhysicalTile, which does WrapToroidalIndex(LightTile + ToroidalOrigin, Modulus). Flipping the sign here still produces
    //    in-range, distinct, plausible slots, so nothing looks broken; the reader would simply disagree with S7 about which tile owns which slot.
    const uint32_t LevelCount = (uint32_t)Clipmap.Levels.size() < ShadowTilemapLodCount
                              ? (uint32_t)Clipmap.Levels.size()
                              : ShadowTilemapLodCount;
    for (uint32_t Level = 0; Level < LevelCount; ++Level)
    {
        Block.ToroidalOrigins[Level][0] = Clipmap.Levels[Level].ToroidalOrigin.XTile;
        Block.ToroidalOrigins[Level][1] = Clipmap.Levels[Level].ToroidalOrigin.YTile;
    }
    // Levels beyond the clipmap's own count keep the zero-initialized origin, and LevelCount below stops the walk before it reaches them.

    // 📝 Taken from level 0 rather than the ShadowBaseTileMetres constant, so a clipmap built with a non-default base still traces correctly. The
    //    shader derives every coarser level as `BaseTileMetres * (1 << Level)`, which the differential probe confirmed matches each level's TileMetres.
    if (!Clipmap.Levels.empty())
        Block.BaseTileMetres = Clipmap.Levels[0].TileMetres;

    Block.DepthOriginMetres   = DepthOriginMetres;
    Block.DepthRangeMetres    = DepthRangeMetres;
    Block.DepthBias           = DepthBias;
    Block.LevelCount          = LevelCount;
    Block.SunShadowDebugMode  = (uint32_t)DebugView;

    // 🔴 CLAMPED HERE, ON THE HOST, AS WELL AS IN THE SHADER'S LOOP BOUND. The shader's `&&` guards keep it correct either way, but a caller passing 64
    //    rays would silently get 4 with no way to tell — so the clamp lives where the value is authored and the shader's is the backstop, not the policy.
    Block.ShadowAngleRadians  = ShadowAngleRadians > 0.0f ? ShadowAngleRadians : 0.0f;
    Block.SoftRayCount        = SoftRayCount  > 4u  ? 4u  : SoftRayCount;
    Block.SoftStepCount       = SoftStepCount > 16u ? 16u : SoftStepCount;
    return Block;
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
    if (Shade.ShadowSampler != VK_NULL_HANDLE)
        vkDestroySampler(Device, Shade.ShadowSampler, Allocator);
    if (Shade.DescriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(Device, Shade.DescriptorPool, Allocator);   // frees ShadeSet
    if (Shade.SetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(Device, Shade.SetLayout, Allocator);
    if (Shade.MaterialBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(Device, Shade.MaterialBuffer, Allocator);
    if (Shade.MaterialMemory != VK_NULL_HANDLE)
        vkFreeMemory(Device, Shade.MaterialMemory, Allocator);

    // ⚠️ Unmap BEFORE freeing. The trace buffer is persistently mapped, and freeing memory that is still mapped is undefined — it happens to work on
    //    most drivers, which is exactly what makes it a latent defect rather than a visible one. The material UBO needs no counterpart: it maps and
    //    unmaps inside its upload.
    if (Shade.TraceMapping != nullptr && Shade.TraceMemory != VK_NULL_HANDLE)
        vkUnmapMemory(Device, Shade.TraceMemory);
    if (Shade.TraceBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(Device, Shade.TraceBuffer, Allocator);
    if (Shade.TraceMemory != VK_NULL_HANDLE)
        vkFreeMemory(Device, Shade.TraceMemory, Allocator);

    Shade = SurfaceShadeInscription{};
}

} // namespace Frontier
