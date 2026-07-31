/*==============================================================================================================================================
                                                         SOFTWARERASTERIZATION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the software micro-raster (PLAN §6). Two owned pipelines built at Initialize: a COMPUTE raster (one of the two route
//    variants — SoftwareRasterizationImage.comp.spv for the storage-image route, SoftwareRasterizationBuffer.comp.spv for the buffer fallback)
//    that atomicMaxes a packed (depth|id) word per covered pixel, and a fullscreen GRAPHICS resolve (VisibilityInscription.vert + the matching
//    SoftwareResolve*.frag) that unpacks that word into the exact R32_UINT visibility buffer + D32 depth the hardware raster fills. The route is
//    chosen once from the host's int64-atomics feature flags (image preferred, buffer fallback, None gates the whole component off). Raw Vulkan, no
//    VMA, mirroring the InstanceCullSubmission compute idiom (shader-bytes / module / SelectMemoryTypeIndex) and the VisibilityRasterization
//    graphics-pipeline idiom (VkPipelineRenderingCreateInfoKHR chained for dynamic rendering). Every scene resource is borrowed; this owns only its
//    two pipelines + layouts + descriptor plumbing.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Visibility/SoftwareRasterization.h"

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

void ReportSoftwareRaster(const char* MessageText)
{
    std::fprintf(stderr, "[SoftwareRaster] %s\n", MessageText);
}

std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
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

VkShaderModule ConstructShaderModule(const VulkanHost& Host, const std::vector<char>& Bytes)
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

// The packed-target binding's descriptor type for the built route: a storage image where imageAtomicMax runs, a storage buffer where the SSBO
// atomicMax runs. Shared by the raster set (b4) and the resolve set (b0), which both point at the one packed target.
VkDescriptorType PackedDescriptorType(VisibilityPackedRoute Route)
{
    return (Route == VisibilityPackedRoute::StorageImage) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
}

// Build the compute raster's set layout (b0..b3 storage buffers = instances / survivors / vertices / indices; b4 = the packed target, image or
// buffer by route) + a pipeline layout carrying the SoftwareRasterConstants push range. Returns false with every handle null on any failure.
bool ConstructRasterPipelineLayout(SoftwareRasterization& Software)
{
    VulkanHost& Host = *Software.Host;

    VkDescriptorSetLayoutBinding Bindings[5] = {};
    for (int Index = 0; Index < 4; ++Index)
    {
        Bindings[Index].binding         = (uint32_t)Index;
        Bindings[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[Index].descriptorCount = 1;
        Bindings[Index].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    Bindings[4].binding         = 4;
    Bindings[4].descriptorType  = PackedDescriptorType(Software.Route);
    Bindings[4].descriptorCount = 1;
    Bindings[4].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = 5;
    LayoutInformation.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Software.RasterSetLayout) != VK_SUCCESS)
    {
        Software.RasterSetLayout = VK_NULL_HANDLE;
        return false;
    }

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(SoftwareRasterConstants);

    VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipelineLayoutInformation.setLayoutCount         = 1;
    PipelineLayoutInformation.pSetLayouts            = &Software.RasterSetLayout;
    PipelineLayoutInformation.pushConstantRangeCount = 1;
    PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &PipelineLayoutInformation, Host.Allocator, &Software.RasterLayout) != VK_SUCCESS)
    {
        Software.RasterLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Build the compute raster pipeline from the route's SPV variant. Destroys the module before returning. False with the pipeline null on any failure.
bool ConstructRasterPipeline(SoftwareRasterization& Software, const char* ShaderDirectory)
{
    VulkanHost& Host = *Software.Host;
    const std::string Directory = ShaderDirectory;
    const std::string ShaderName = (Software.Route == VisibilityPackedRoute::StorageImage)
                                       ? "/SoftwareRasterizationImage.comp.spv"
                                       : "/SoftwareRasterizationBuffer.comp.spv";
    VkShaderModule Module = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + ShaderName));
    if (Module == VK_NULL_HANDLE)
    {
        ReportSoftwareRaster("raster compute shader module unavailable — software raster will not run");
        return false;
    }

    VkPipelineShaderStageCreateInfo StageInformation = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    StageInformation.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    StageInformation.module = Module;
    StageInformation.pName  = "main";

    VkComputePipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    PipelineInformation.stage  = StageInformation;
    PipelineInformation.layout = Software.RasterLayout;
    const VkResult Outcome = vkCreateComputePipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInformation, Host.Allocator, &Software.RasterPipeline);
    vkDestroyShaderModule(Host.Device, Module, Host.Allocator);
    if (Outcome != VK_SUCCESS)
    {
        Software.RasterPipeline = VK_NULL_HANDLE;
        ReportSoftwareRaster("raster compute pipeline creation failed");
        return false;
    }
    return true;
}

// Build the resolve's set layout (b0 = the packed target, image or buffer by route, fragment stage) + a pipeline layout carrying the
// SoftwareResolveConstants push range. Returns false with every handle null on any failure.
bool ConstructResolvePipelineLayout(SoftwareRasterization& Software)
{
    VulkanHost& Host = *Software.Host;

    VkDescriptorSetLayoutBinding Binding = {};
    Binding.binding         = 0;
    Binding.descriptorType  = PackedDescriptorType(Software.Route);
    Binding.descriptorCount = 1;
    Binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = 1;
    LayoutInformation.pBindings    = &Binding;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Software.ResolveSetLayout) != VK_SUCCESS)
    {
        Software.ResolveSetLayout = VK_NULL_HANDLE;
        return false;
    }

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(SoftwareResolveConstants);

    VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipelineLayoutInformation.setLayoutCount         = 1;
    PipelineLayoutInformation.pSetLayouts            = &Software.ResolveSetLayout;
    PipelineLayoutInformation.pushConstantRangeCount = 1;
    PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &PipelineLayoutInformation, Host.Allocator, &Software.ResolveLayout) != VK_SUCCESS)
    {
        Software.ResolveLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Build the fullscreen resolve graphics pipeline against the R32_UINT colour + D32 depth formats (dynamic rendering, matching the hardware raster's
// attachments so the resolve writes byte-identical output). No vertex input — VisibilityInscription.vert emits a fullscreen triangle from
// gl_VertexIndex; the fragment stage writes OutIdentity + gl_FragDepth. Destroys the two modules before returning. False on any failure.
bool ConstructResolvePipeline(SoftwareRasterization& Software,
                              VkFormat               ColourFormat,
                              VkFormat               DepthFormat,
                              const char*            ShaderDirectory)
{
    const VulkanHost& Host      = *Software.Host;
    const std::string Directory = ShaderDirectory;
    const std::string FragmentName = (Software.Route == VisibilityPackedRoute::StorageImage)
                                         ? "/SoftwareResolveImage.frag.spv"
                                         : "/SoftwareResolveBuffer.frag.spv";
    VkShaderModule VertexModule   = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/VisibilityInscription.vert.spv"));
    VkShaderModule FragmentModule = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + FragmentName));
    if (VertexModule == VK_NULL_HANDLE || FragmentModule == VK_NULL_HANDLE)
    {
        if (VertexModule   != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        if (FragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        ReportSoftwareRaster("resolve shader modules unavailable — software raster will not resolve");
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

    // No vertex buffer — the fullscreen triangle is generated in the vertex shader from gl_VertexIndex.
    VkPipelineVertexInputStateCreateInfo VertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    VkPipelineInputAssemblyStateCreateInfo InputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo Viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    Viewport.viewportCount = 1;
    Viewport.scissorCount  = 1;

    // The fullscreen triangle covers the whole target; no cull (a single front-facing triangle) so orientation cannot drop it.
    VkPipelineRasterizationStateCreateInfo Rasterization = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    Rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    Rasterization.cullMode    = VK_CULL_MODE_NONE;
    Rasterization.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Rasterization.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // The resolve OWNS depth: it writes the recovered scene depth through gl_FragDepth with the test always passing, so the D32 target ends up
    // carrying exactly what the hardware raster's depth test would have written for the same nearest surface (HiZ reads it downstream).
    VkPipelineDepthStencilStateCreateInfo Depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    Depth.depthTestEnable  = VK_TRUE;
    Depth.depthWriteEnable = VK_TRUE;
    Depth.depthCompareOp   = VK_COMPARE_OP_ALWAYS;

    // The R32_UINT identity write is integer — no blending, full write mask.
    VkPipelineColorBlendAttachmentState BlendAttachment = {};
    BlendAttachment.blendEnable    = VK_FALSE;
    BlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo ColorBlend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    ColorBlend.attachmentCount = 1;
    ColorBlend.pAttachments    = &BlendAttachment;

    VkDynamicState DynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo Dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    Dynamic.dynamicStateCount = 2;
    Dynamic.pDynamicStates    = DynamicStates;

    VkPipelineRenderingCreateInfoKHR RenderingInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
    RenderingInfo.colorAttachmentCount    = 1;
    RenderingInfo.pColorAttachmentFormats = &ColourFormat;
    RenderingInfo.depthAttachmentFormat   = DepthFormat;

    VkGraphicsPipelineCreateInfo PipelineInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    PipelineInfo.pNext               = &RenderingInfo;
    PipelineInfo.stageCount          = 2;
    PipelineInfo.pStages             = Stages;
    PipelineInfo.pVertexInputState   = &VertexInput;
    PipelineInfo.pInputAssemblyState = &InputAssembly;
    PipelineInfo.pViewportState      = &Viewport;
    PipelineInfo.pRasterizationState = &Rasterization;
    PipelineInfo.pMultisampleState   = &Multisample;
    PipelineInfo.pDepthStencilState  = &Depth;
    PipelineInfo.pColorBlendState    = &ColorBlend;
    PipelineInfo.pDynamicState       = &Dynamic;
    PipelineInfo.layout              = Software.ResolveLayout;

    VkResult Outcome = vkCreateGraphicsPipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInfo, Host.Allocator, &Software.ResolvePipeline);
    vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
    vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
    if (Outcome != VK_SUCCESS)
    {
        Software.ResolvePipeline = VK_NULL_HANDLE;
        ReportSoftwareRaster("resolve graphics pipeline creation failed");
        return false;
    }
    return true;
}

// Allocate the descriptor pool + the two sets (one raster, one resolve). Sized for five storage buffers + one packed target on the raster set and
// one packed target on the resolve set — the packed-target type is the route's descriptor type. Bindings are written later (scene / packed target
// bind separately). Returns false on any failure.
bool ConstructDescriptors(SoftwareRasterization& Software)
{
    VulkanHost& Host = *Software.Host;

    const VkDescriptorType PackedType = PackedDescriptorType(Software.Route);

    // Raster set: b0..b3 storage buffers; b4 packed target. Resolve set: b0 packed target. Pool sizes cover both sets together.
    VkDescriptorPoolSize PoolSizes[2] = {};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSizes[0].descriptorCount = 4;   // raster b0..b3
    PoolSizes[1].type            = PackedType;
    PoolSizes[1].descriptorCount = 2;   // raster b4 + resolve b0

    // When the buffer route makes the packed target a storage buffer too, both pool entries share the same type. That is legal — the pool simply
    // reserves both counts of that type — so no merge is needed.
    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 2;
    PoolInformation.poolSizeCount = 2;
    PoolInformation.pPoolSizes    = PoolSizes;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Software.DescriptorPool) != VK_SUCCESS)
    {
        Software.DescriptorPool = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorSetAllocateInfo RasterAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    RasterAllocation.descriptorPool     = Software.DescriptorPool;
    RasterAllocation.descriptorSetCount = 1;
    RasterAllocation.pSetLayouts        = &Software.RasterSetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &RasterAllocation, &Software.RasterSet) != VK_SUCCESS)
    {
        Software.RasterSet = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorSetAllocateInfo ResolveAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ResolveAllocation.descriptorPool     = Software.DescriptorPool;
    ResolveAllocation.descriptorSetCount = 1;
    ResolveAllocation.pSetLayouts        = &Software.ResolveSetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &ResolveAllocation, &Software.ResolveSet) != VK_SUCCESS)
    {
        Software.ResolveSet = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSoftwareRasterization(SoftwareRasterization& Software,
                                     VulkanHost&            Host,
                                     VkFormat               ColourFormat,
                                     VkFormat               DepthFormat,
                                     const char*            ShaderDirectory)
{
    Software = SoftwareRasterization{};
    Software.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportSoftwareRaster("no device — software raster not built");
        return false;
    }
    if (!Host.DynamicRenderingEnabled)
    {
        ReportSoftwareRaster("dynamic rendering unavailable — software raster not built");
        return false;
    }

    // The route is fixed here from the host's int64-atomics features (image preferred, buffer fallback). None means neither feature is enabled —
    // the software path is unavailable on this device, so build nothing and leave ReadyCondition false; the caller keeps the software path off.
    if (Host.ShaderImageInt64AtomicsEnabled)
        Software.Route = VisibilityPackedRoute::StorageImage;
    else if (Host.ShaderBufferInt64AtomicsEnabled)
        Software.Route = VisibilityPackedRoute::StorageBuffer;
    else
        Software.Route = VisibilityPackedRoute::None;

    if (Software.Route == VisibilityPackedRoute::None)
    {
        ReportSoftwareRaster("int64 shader atomics unavailable — software raster gated off");
        return false;
    }

    if (!ConstructRasterPipelineLayout(Software))
    {
        ReportSoftwareRaster("raster pipeline layout creation failed");
        FinalizeSoftwareRasterization(Software);
        return false;
    }
    if (!ConstructRasterPipeline(Software, ShaderDirectory))
    {
        FinalizeSoftwareRasterization(Software);
        return false;
    }
    if (!ConstructResolvePipelineLayout(Software))
    {
        ReportSoftwareRaster("resolve pipeline layout creation failed");
        FinalizeSoftwareRasterization(Software);
        return false;
    }
    if (!ConstructResolvePipeline(Software, ColourFormat, DepthFormat, ShaderDirectory))
    {
        FinalizeSoftwareRasterization(Software);
        return false;
    }
    if (!ConstructDescriptors(Software))
    {
        ReportSoftwareRaster("descriptor plumbing creation failed");
        FinalizeSoftwareRasterization(Software);
        return false;
    }

    Software.ReadyCondition = true;
    return true;
}

void BindSoftwareRasterScene(SoftwareRasterization& Software,
                             VkBuffer               InstanceBuffer,
                             VkBuffer               SurvivorBuffer,
                             VkBuffer               VertexBuffer,
                             VkBuffer               IndexBuffer)
{
    if (!Software.ReadyCondition || Software.Host == nullptr)
        return;
    if (InstanceBuffer == VK_NULL_HANDLE || SurvivorBuffer == VK_NULL_HANDLE ||
        VertexBuffer   == VK_NULL_HANDLE || IndexBuffer    == VK_NULL_HANDLE)
        return;

    // Only rewrite the bindings whose buffer changed — a rebind on an unchanged scene is a pointless descriptor churn.
    const bool InstanceChanged = (InstanceBuffer != Software.BoundInstanceBuffer);
    const bool SurvivorChanged = (SurvivorBuffer != Software.BoundSurvivorBuffer);
    const bool VertexChanged   = (VertexBuffer   != Software.BoundVertexBuffer);
    const bool IndexChanged    = (IndexBuffer    != Software.BoundIndexBuffer);
    if (!InstanceChanged && !SurvivorChanged && !VertexChanged && !IndexChanged)
        return;

    VkDescriptorBufferInfo Infos[4] = {};
    Infos[0] = { InstanceBuffer, 0, VK_WHOLE_SIZE };
    Infos[1] = { SurvivorBuffer, 0, VK_WHOLE_SIZE };
    Infos[2] = { VertexBuffer,   0, VK_WHOLE_SIZE };
    Infos[3] = { IndexBuffer,    0, VK_WHOLE_SIZE };
    const bool Changed[4] = { InstanceChanged, SurvivorChanged, VertexChanged, IndexChanged };

    VkWriteDescriptorSet Writes[4] = {};
    uint32_t WriteCount = 0;
    for (int Index = 0; Index < 4; ++Index)
    {
        if (!Changed[Index])
            continue;
        Writes[WriteCount].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[WriteCount].dstSet          = Software.RasterSet;
        Writes[WriteCount].dstBinding      = (uint32_t)Index;
        Writes[WriteCount].descriptorCount = 1;
        Writes[WriteCount].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[WriteCount].pBufferInfo     = &Infos[Index];
        ++WriteCount;
    }
    if (WriteCount > 0)
        vkUpdateDescriptorSets(Software.Host->Device, WriteCount, Writes, 0, nullptr);

    Software.BoundInstanceBuffer = InstanceBuffer;
    Software.BoundSurvivorBuffer = SurvivorBuffer;
    Software.BoundVertexBuffer   = VertexBuffer;
    Software.BoundIndexBuffer    = IndexBuffer;
}

void BindSoftwarePackedTarget(SoftwareRasterization& Software, VisibilityImage& Image)
{
    if (!Software.ReadyCondition || Software.Host == nullptr)
        return;
    // The packed target must exist and its route must match what this component built the pipelines for.
    if (Image.PackedRoute != Software.Route || Software.Route == VisibilityPackedRoute::None)
        return;

    VkDevice Device = Software.Host->Device;

    if (Software.Route == VisibilityPackedRoute::StorageImage)
    {
        if (Image.PackedImageView == VK_NULL_HANDLE || Image.PackedImageView == Software.BoundPackedView)
            return;

        // The storage image is bound in GENERAL — the layout the raster atomicMaxes and the resolve reads it in.
        VkDescriptorImageInfo PackedInfo = {};
        PackedInfo.imageView   = Image.PackedImageView;
        PackedInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet Writes[2] = {};
        Writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[0].dstSet          = Software.RasterSet;
        Writes[0].dstBinding      = 4;
        Writes[0].descriptorCount = 1;
        Writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        Writes[0].pImageInfo      = &PackedInfo;
        Writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[1].dstSet          = Software.ResolveSet;
        Writes[1].dstBinding      = 0;
        Writes[1].descriptorCount = 1;
        Writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        Writes[1].pImageInfo      = &PackedInfo;
        vkUpdateDescriptorSets(Device, 2, Writes, 0, nullptr);

        Software.BoundPackedView = Image.PackedImageView;
    }
    else // StorageBuffer
    {
        if (Image.PackedBuffer == VK_NULL_HANDLE || Image.PackedBuffer == Software.BoundPackedBuffer)
            return;

        VkDescriptorBufferInfo PackedInfo = { Image.PackedBuffer, 0, VK_WHOLE_SIZE };

        VkWriteDescriptorSet Writes[2] = {};
        Writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[0].dstSet          = Software.RasterSet;
        Writes[0].dstBinding      = 4;
        Writes[0].descriptorCount = 1;
        Writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[0].pBufferInfo     = &PackedInfo;
        Writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[1].dstSet          = Software.ResolveSet;
        Writes[1].dstBinding      = 0;
        Writes[1].descriptorCount = 1;
        Writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[1].pBufferInfo     = &PackedInfo;
        vkUpdateDescriptorSets(Device, 2, Writes, 0, nullptr);

        Software.BoundPackedBuffer = Image.PackedBuffer;
    }
}

void RecordSoftwareRasterization(SoftwareRasterization&         Software,
                                 VisibilityImage&               Image,
                                 VisibilityDepth&               Depth,
                                 const SoftwareRasterConstants& Constants,
                                 VkCommandBuffer                CommandBuffer)
{
    if (!Software.ReadyCondition || Software.Host == nullptr)
        return;
    if (!Image.ReadyCondition || !Depth.ReadyCondition)
        return;
    if (Image.PackedRoute != Software.Route)
        return;
    if (Constants.InstanceCount == 0 || Constants.TriangleCount == 0)
        return;

    VulkanHost& Host = *Software.Host;

    // ─── Clear the packed target to the empty sentinel, then hand it to the raster (GENERAL for the image route; no transition for the buffer). ───
    ClearVisibilityPackedTarget(Image, CommandBuffer);
    TransitionVisibilityPackedForRaster(Image, CommandBuffer);

    // ─── Dispatch the compute raster: one lane per (instance * triangle), rounded up to the flat workgroup lane count. ───
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Software.RasterPipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Software.RasterLayout, 0, 1, &Software.RasterSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Software.RasterLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SoftwareRasterConstants), &Constants);

    const uint64_t Triangles = (uint64_t)Constants.InstanceCount * (uint64_t)Constants.TriangleCount;
    const uint32_t Groups    = (uint32_t)((Triangles + SoftwareRasterWorkgroupLanes - 1) / SoftwareRasterWorkgroupLanes);
    vkCmdDispatch(CommandBuffer, Groups, 1, 1);

    // ─── Fence the raster's atomicMax writes before the resolve reads the packed target (compute-write -> fragment-read). ───
    VkMemoryBarrier RasterBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    RasterBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    RasterBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 1, &RasterBarrier, 0, nullptr, 0, nullptr);

    // ─── Transition the R32 id buffer + D32 depth to attachment layouts (from UNDEFINED first frame, or SHADER_READ_ONLY after a prior sampling). ───
    VkImageSubresourceRange ColourRange = {};
    ColourRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ColourRange.levelCount = 1;
    ColourRange.layerCount = 1;
    VkImageMemoryBarrier ToColour = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    ToColour.oldLayout           = Image.CurrentLayout;
    ToColour.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ToColour.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToColour.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToColour.image               = Image.IdImage;
    ToColour.subresourceRange    = ColourRange;
    ToColour.srcAccessMask       = 0;
    ToColour.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToColour);

    VkImageSubresourceRange DepthRange = {};
    DepthRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    DepthRange.levelCount = 1;
    DepthRange.layerCount = 1;
    VkImageMemoryBarrier ToDepth = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    ToDepth.oldLayout           = Depth.CurrentLayout;
    ToDepth.newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    ToDepth.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToDepth.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToDepth.image               = Depth.DepthImage;
    ToDepth.subresourceRange    = DepthRange;
    ToDepth.srcAccessMask       = 0;
    ToDepth.dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToDepth);

    // ─── Open the colour(id)+depth dynamic-rendering scope. CLEAR both — the resolve writes every pixel (id + gl_FragDepth), so the clear values
    //     only cover the (impossible) case of a pixel the fullscreen triangle misses; they match the hardware raster's clears for safety. ───
    VkRenderingAttachmentInfoKHR ColourAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
    ColourAttachment.imageView   = Image.IdView;
    ColourAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ColourAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ColourAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    ColourAttachment.clearValue.color.uint32[0] = VisibilityEmptySentinel;

    VkRenderingAttachmentInfoKHR DepthAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
    DepthAttachment.imageView               = Depth.DepthView;
    DepthAttachment.imageLayout             = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    DepthAttachment.loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR;
    DepthAttachment.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
    DepthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfoKHR RenderingInformation = { VK_STRUCTURE_TYPE_RENDERING_INFO_KHR };
    RenderingInformation.renderArea.extent    = { Image.Width, Image.Height };
    RenderingInformation.layerCount           = 1;
    RenderingInformation.colorAttachmentCount = 1;
    RenderingInformation.pColorAttachments    = &ColourAttachment;
    RenderingInformation.pDepthAttachment     = &DepthAttachment;

    Host.CmdBeginRendering(CommandBuffer, &RenderingInformation);

    VkViewport ViewportRegion = {};
    ViewportRegion.width    = (float)Image.Width;
    ViewportRegion.height   = (float)Image.Height;
    ViewportRegion.minDepth = 0.0f;
    ViewportRegion.maxDepth = 1.0f;
    vkCmdSetViewport(CommandBuffer, 0, 1, &ViewportRegion);

    VkRect2D Scissor = {};
    Scissor.extent = { Image.Width, Image.Height };
    vkCmdSetScissor(CommandBuffer, 0, 1, &Scissor);

    SoftwareResolveConstants ResolveConstants = {};
    ResolveConstants.TargetExtentX = Image.Width;
    ResolveConstants.TargetExtentY = Image.Height;

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Software.ResolvePipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Software.ResolveLayout, 0, 1, &Software.ResolveSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Software.ResolveLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SoftwareResolveConstants), &ResolveConstants);

    // The fullscreen triangle: three vertices, no vertex buffer (VisibilityInscription.vert derives the positions from gl_VertexIndex).
    vkCmdDraw(CommandBuffer, 3, 1, 0, 0);

    Host.CmdEndRendering(CommandBuffer);

    // Leave both targets in their attachment layouts, exactly as the hardware raster's End scope does, so the downstream sampling transitions are
    // identical regardless of which raster produced the frame.
    Image.CurrentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    Depth.CurrentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
}

void FinalizeSoftwareRasterization(SoftwareRasterization& Software)
{
    if (Software.Host == nullptr || Software.Host->Device == VK_NULL_HANDLE)
    {
        Software = SoftwareRasterization{};
        return;
    }
    VkDevice Device = Software.Host->Device;
    const VkAllocationCallbacks* Allocator = Software.Host->Allocator;

    if (Software.RasterPipeline   != VK_NULL_HANDLE) vkDestroyPipeline(Device, Software.RasterPipeline, Allocator);
    if (Software.ResolvePipeline  != VK_NULL_HANDLE) vkDestroyPipeline(Device, Software.ResolvePipeline, Allocator);
    if (Software.RasterLayout     != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Software.RasterLayout, Allocator);
    if (Software.ResolveLayout    != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Software.ResolveLayout, Allocator);
    if (Software.DescriptorPool   != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Software.DescriptorPool, Allocator); // frees both sets
    if (Software.RasterSetLayout  != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Software.RasterSetLayout, Allocator);
    if (Software.ResolveSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Software.ResolveSetLayout, Allocator);

    Software = SoftwareRasterization{};
}

} // namespace Frontier
