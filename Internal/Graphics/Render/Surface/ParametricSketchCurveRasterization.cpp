/*==============================================================================================================================================
                                                  PARAMETRICSKETCHCURVERASTERIZATION.CPP
==============================================================================================================================================*/
// 🧩 The thick-line outline rasterization: build the one-set pipeline once, then rasterize one stroke body per record call as a screen-constant-width
//    ribbon. Set 0 is a host-visible camera UBO this owns + maps persistently (rewritten each frame, byte-identical to the matcap camera block); a
//    push range carries the half stroke width + viewport extent + body colour + linetype. The pipeline consumes the engine's stride-32 RenderVertex
//    (position @0 = endpoint A, normal @12 = partner endpoint B, texcoord @24 = (SideSign, ArcLength)) — the CPU expands each polyline segment into a
//    quad, so no new upload path is needed — and BLENDS straight-alpha so anti-aliased ribbon edges and per-body colour composite over the transparent
//    view target. Depth-tests LESS_OR_EQUAL against ParametricSketchViewTarget's D32_SFLOAT depth so an outline occludes correctly against the solid.
//    Raw Vulkan, no VMA — the custom-pipeline idiom (shader-module load, set layout, pipeline layout + push range, graphics pipeline, descriptor pool
//    + set). Wired onto VulkanHost.

#include "Graphics/Render/Surface/ParametricSketchCurveRasterization.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void ReportCurve(const char* MessageText)
    {
        std::fprintf(stderr, "[ParametricSketchCurveRasterization] %s\n", MessageText);
    }

    // 📝 First memory type allowed by the requirement bitmask carrying every required property bit — mirrors BufferAllocation / the matcap surface so
    //    the whole engine selects memory identically.
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

    // 📝 Read a whole file into a byte vector. Empty return signals failure (missing .spv). Small local asset loaded once — no partial handling.
    std::vector<unsigned char> ReadWholeFile(const char* Path)
    {
        std::vector<unsigned char> Bytes;
        std::FILE* Handle = std::fopen(Path, "rb");
        if (Handle == nullptr) return Bytes;
        std::fseek(Handle, 0, SEEK_END);
        long Length = std::ftell(Handle);
        std::fseek(Handle, 0, SEEK_SET);
        if (Length > 0)
        {
            Bytes.resize((size_t)Length);
            size_t ReadCount = std::fread(Bytes.data(), 1, (size_t)Length, Handle);
            if (ReadCount != (size_t)Length) Bytes.clear();
        }
        std::fclose(Handle);
        return Bytes;
    }

    // 📝 Load a SPIR-V blob into a shader module. Empty / non-multiple-of-4 file → null module (caller degrades to no overlay).
    VkShaderModule LoadShaderModule(VkDevice Device, const char* SpvPath)
    {
        std::vector<unsigned char> Blob = ReadWholeFile(SpvPath);
        if (Blob.empty() || (Blob.size() % 4) != 0) return VK_NULL_HANDLE;

        VkShaderModuleCreateInfo ModuleInformation = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        ModuleInformation.codeSize = Blob.size();
        ModuleInformation.pCode    = reinterpret_cast<const uint32_t*>(Blob.data());
        VkShaderModule Module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(Device, &ModuleInformation, nullptr, &Module) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        return Module;
    }

    // 📝 Host-visible / coherent uniform buffer of ByteSize, left persistently mapped so the camera block is rewritten each frame with a plain memcpy
    //    (no flush). On any failure both out-handles are null + the map pointer null (returns false).
    bool AllocateMappedUniformBuffer(VulkanHost&     Host,
                                     VkDeviceSize    ByteSize,
                                     VkBuffer&       OutBuffer,
                                     VkDeviceMemory& OutMemory,
                                     void*&          OutMapped)
    {
        OutBuffer = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        OutMapped = nullptr;

        VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        BufferInformation.size        = ByteSize;
        BufferInformation.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Host.Device, &BufferInformation, nullptr, &OutBuffer) != VK_SUCCESS)
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
            vkDestroyBuffer(Host.Device, OutBuffer, nullptr);
            OutBuffer = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        AllocateInformation.allocationSize  = MemoryRequirements.size;
        AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
        if (vkAllocateMemory(Host.Device, &AllocateInformation, nullptr, &OutMemory) != VK_SUCCESS ||
            vkBindBufferMemory(Host.Device, OutBuffer, OutMemory, 0) != VK_SUCCESS)
        {
            if (OutMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, OutMemory, nullptr);
            vkDestroyBuffer(Host.Device, OutBuffer, nullptr);
            OutBuffer = VK_NULL_HANDLE;
            OutMemory = VK_NULL_HANDLE;
            return false;
        }

        if (vkMapMemory(Host.Device, OutMemory, 0, ByteSize, 0, &OutMapped) != VK_SUCCESS)
        {
            vkFreeMemory(Host.Device, OutMemory, nullptr);
            vkDestroyBuffer(Host.Device, OutBuffer, nullptr);
            OutBuffer = VK_NULL_HANDLE;
            OutMemory = VK_NULL_HANDLE;
            OutMapped = nullptr;
            return false;
        }
        return true;
    }

    // 📝 The one set layout: set 0 one uniform buffer (vertex stage — the camera block). Single binding. On failure the handle is null + false.
    bool BuildSetLayout(ParametricSketchCurveRasterization& Rasterization)
    {
        VkDevice Device = Rasterization.Host->Device;

        VkDescriptorSetLayoutBinding CameraBinding = {};
        CameraBinding.binding         = 0;
        CameraBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        CameraBinding.descriptorCount = 1;
        CameraBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo CameraLayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        CameraLayoutInformation.bindingCount = 1;
        CameraLayoutInformation.pBindings    = &CameraBinding;
        if (vkCreateDescriptorSetLayout(Device, &CameraLayoutInformation, nullptr, &Rasterization.CameraSetLayout) != VK_SUCCESS)
        {
            Rasterization.CameraSetLayout = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    // 📝 Build the graphics pipeline: { CameraSetLayout } layout with ONE push range (the stroke constants, visible to both stages), stride-32
    //    RenderVertex input (position @0 = endpoint A, normal @12 = partner B, texcoord @24 = (SideSign, ArcLength)), triangle list, no cull (the
    //    ribbon quads are two-sided), depth test LESS_OR_EQUAL + write, ONE colour attachment with STRAIGHT-ALPHA blend (src·a + dst·(1-a)) so the ink
    //    composites over the transparent target, dynamic viewport / scissor, against RenderPass subpass 0. On any failure the pipeline-layout is
    //    released and false is returned.
    bool BuildPipeline(ParametricSketchCurveRasterization& Rasterization, VkRenderPass RenderPass)
    {
        VkDevice Device = Rasterization.Host->Device;

        VkPushConstantRange StrokeRange = {};
        StrokeRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        StrokeRange.offset     = 0;
        StrokeRange.size       = (uint32_t)sizeof(ParametricSketchStrokeConstants);

        VkPipelineLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        LayoutInformation.setLayoutCount         = 1;
        LayoutInformation.pSetLayouts            = &Rasterization.CameraSetLayout;
        LayoutInformation.pushConstantRangeCount = 1;
        LayoutInformation.pPushConstantRanges    = &StrokeRange;
        if (vkCreatePipelineLayout(Device, &LayoutInformation, nullptr, &Rasterization.PipelineLayout) != VK_SUCCESS)
        {
            Rasterization.PipelineLayout = VK_NULL_HANDLE;
            return false;
        }

        VkPipelineShaderStageCreateInfo Stages[2] = {};
        Stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        Stages[0].module = Rasterization.VertModule;
        Stages[0].pName  = "main";
        Stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        Stages[1].module = Rasterization.FragModule;
        Stages[1].pName  = "main";

        // ─── vertex input: the engine's stride-32 RenderVertex, thick-line packing (posA @0, posB @12, (side, arc) @24) ───
        VkVertexInputBindingDescription BindingDescription = {};
        BindingDescription.binding   = 0;
        BindingDescription.stride    = 32;
        BindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription AttributeDescriptions[3] = {};
        AttributeDescriptions[0].location = 0;   // endpoint A (this corner)
        AttributeDescriptions[0].binding  = 0;
        AttributeDescriptions[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
        AttributeDescriptions[0].offset   = 0;
        AttributeDescriptions[1].location = 1;   // endpoint B (partner)
        AttributeDescriptions[1].binding  = 0;
        AttributeDescriptions[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
        AttributeDescriptions[1].offset   = 12;
        AttributeDescriptions[2].location = 2;   // (SideSign, ArcLength)
        AttributeDescriptions[2].binding  = 0;
        AttributeDescriptions[2].format   = VK_FORMAT_R32G32_SFLOAT;
        AttributeDescriptions[2].offset   = 24;

        VkPipelineVertexInputStateCreateInfo VertexInformation = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        VertexInformation.vertexBindingDescriptionCount   = 1;
        VertexInformation.pVertexBindingDescriptions      = &BindingDescription;
        VertexInformation.vertexAttributeDescriptionCount = 3;
        VertexInformation.pVertexAttributeDescriptions    = AttributeDescriptions;

        VkPipelineInputAssemblyStateCreateInfo AssemblyInformation = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        AssemblyInformation.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo ViewportInformation = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        ViewportInformation.viewportCount = 1;
        ViewportInformation.scissorCount  = 1;

        // 📝 No cull: the segment quad's winding depends on the stroke direction + which side each corner sits on, so a fixed cull mode would drop
        //    half the ribbons. The stroke has no interior, so there is nothing to cull for.
        VkPipelineRasterizationStateCreateInfo RasterInformation = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        RasterInformation.polygonMode = VK_POLYGON_MODE_FILL;
        RasterInformation.cullMode    = VK_CULL_MODE_NONE;
        RasterInformation.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        RasterInformation.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo MultisampleInformation = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        MultisampleInformation.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // 📝 Outlines sit ON TOP of the solid but must still depth-test against it (an edge behind a solid face is hidden). LESS_OR_EQUAL + write, same
        //    D32 depth as the matcap so the two register in one buffer.
        VkPipelineDepthStencilStateCreateInfo DepthInformation = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        DepthInformation.depthTestEnable  = VK_TRUE;
        DepthInformation.depthWriteEnable = VK_TRUE;
        DepthInformation.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        // 📝 Straight-alpha blend so an anti-aliased ribbon edge + a translucent ink colour composite over the TRANSPARENT view target the sequence
        //    clears: out = src.rgb·src.a + dst.rgb·(1 - src.a); the alpha channel accumulates so the ImGui composite over the canvas is correct.
        VkPipelineColorBlendAttachmentState BlendAttachment = {};
        BlendAttachment.blendEnable         = VK_TRUE;
        BlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        BlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        BlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
        BlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        BlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        BlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
        BlendAttachment.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo BlendInformation = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        BlendInformation.attachmentCount = 1;
        BlendInformation.pAttachments    = &BlendAttachment;

        VkDynamicState DynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo DynamicInformation = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        DynamicInformation.dynamicStateCount = 2;
        DynamicInformation.pDynamicStates    = DynamicStates;

        VkGraphicsPipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        PipelineInformation.stageCount          = 2;
        PipelineInformation.pStages             = Stages;
        PipelineInformation.pVertexInputState   = &VertexInformation;
        PipelineInformation.pInputAssemblyState = &AssemblyInformation;
        PipelineInformation.pViewportState      = &ViewportInformation;
        PipelineInformation.pRasterizationState = &RasterInformation;
        PipelineInformation.pMultisampleState   = &MultisampleInformation;
        PipelineInformation.pDepthStencilState  = &DepthInformation;
        PipelineInformation.pColorBlendState    = &BlendInformation;
        PipelineInformation.pDynamicState       = &DynamicInformation;
        PipelineInformation.layout              = Rasterization.PipelineLayout;
        PipelineInformation.renderPass          = RenderPass;
        PipelineInformation.subpass             = 0;
        if (vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &PipelineInformation, nullptr, &Rasterization.Pipeline) != VK_SUCCESS)
        {
            Rasterization.Pipeline = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    // 📝 Descriptor pool (1 uniform buffer, 1 set) plus the camera set bound to the mapped UBO. On any failure the pool is released and false returned
    //    with the set handle null.
    bool BuildDescriptorSet(ParametricSketchCurveRasterization& Rasterization)
    {
        VkDevice Device = Rasterization.Host->Device;

        VkDescriptorPoolSize PoolSize = {};
        PoolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        PoolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        PoolInformation.maxSets       = 1;
        PoolInformation.poolSizeCount = 1;
        PoolInformation.pPoolSizes    = &PoolSize;
        if (vkCreateDescriptorPool(Device, &PoolInformation, nullptr, &Rasterization.DescriptorPool) != VK_SUCCESS)
        {
            Rasterization.DescriptorPool = VK_NULL_HANDLE;
            return false;
        }

        VkDescriptorSetAllocateInfo CameraAllocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        CameraAllocate.descriptorPool     = Rasterization.DescriptorPool;
        CameraAllocate.descriptorSetCount = 1;
        CameraAllocate.pSetLayouts        = &Rasterization.CameraSetLayout;
        if (vkAllocateDescriptorSets(Device, &CameraAllocate, &Rasterization.CameraSet) != VK_SUCCESS)
        {
            Rasterization.CameraSet = VK_NULL_HANDLE;
            return false;
        }

        VkDescriptorBufferInfo CameraBufferInformation = {};
        CameraBufferInformation.buffer = Rasterization.CameraBuffer;
        CameraBufferInformation.offset = 0;
        CameraBufferInformation.range  = VK_WHOLE_SIZE;

        VkWriteDescriptorSet Write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        Write.dstSet          = Rasterization.CameraSet;
        Write.dstBinding      = 0;
        Write.descriptorCount = 1;
        Write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        Write.pBufferInfo     = &CameraBufferInformation;
        vkUpdateDescriptorSets(Device, 1, &Write, 0, nullptr);
        return true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeParametricSketchCurveRasterization(ParametricSketchCurveRasterization& Rasterization,
                                                  VulkanHost&                         Host,
                                                  const char*                         VertSpvPath,
                                                  const char*                         FragSpvPath,
                                                  VkRenderPass                        RenderPass)
{
    Rasterization = ParametricSketchCurveRasterization{};
    Rasterization.Host        = &Host;
    Rasterization.ReadyStatus = false;

    if (VertSpvPath == nullptr || FragSpvPath == nullptr || RenderPass == VK_NULL_HANDLE)
    {
        ReportCurve("null bring-up argument — outline overlay disabled");
        return false;
    }

    Rasterization.VertModule = LoadShaderModule(Host.Device, VertSpvPath);
    Rasterization.FragModule = LoadShaderModule(Host.Device, FragSpvPath);
    if (Rasterization.VertModule == VK_NULL_HANDLE || Rasterization.FragModule == VK_NULL_HANDLE)
    {
        ReportCurve("shader module load failed — outline overlay disabled");
        FinalizeParametricSketchCurveRasterization(Rasterization);
        return false;
    }

    if (!BuildSetLayout(Rasterization))
    {
        ReportCurve("descriptor set layout creation failed — outline overlay disabled");
        FinalizeParametricSketchCurveRasterization(Rasterization);
        return false;
    }

    if (!BuildPipeline(Rasterization, RenderPass))
    {
        ReportCurve("pipeline build failed — outline overlay disabled");
        FinalizeParametricSketchCurveRasterization(Rasterization);
        return false;
    }

    if (!AllocateMappedUniformBuffer(Host, (VkDeviceSize)sizeof(ParametricSketchCurveCameraBlock),
                                     Rasterization.CameraBuffer, Rasterization.CameraMemory, Rasterization.CameraMapped))
    {
        ReportCurve("camera uniform buffer allocation failed — outline overlay disabled");
        FinalizeParametricSketchCurveRasterization(Rasterization);
        return false;
    }

    if (!BuildDescriptorSet(Rasterization))
    {
        ReportCurve("descriptor set allocation failed — outline overlay disabled");
        FinalizeParametricSketchCurveRasterization(Rasterization);
        return false;
    }

    Rasterization.ReadyStatus = true;
    return true;
}

void RefreshParametricSketchCurveCamera(ParametricSketchCurveRasterization& Rasterization, const ParametricSketchCurveCameraBlock& Camera)
{
    if (!Rasterization.ReadyStatus || Rasterization.CameraMapped == nullptr) return;
    std::memcpy(Rasterization.CameraMapped, &Camera, sizeof(ParametricSketchCurveCameraBlock));
}

void RecordParametricSketchCurveInto(ParametricSketchCurveRasterization&    Rasterization,
                                     VkCommandBuffer                        CommandBuffer,
                                     const PolygonBufferAllocation&         Body,
                                     const ParametricSketchStrokeConstants& Constants,
                                     uint32_t                               Width,
                                     uint32_t                               Height)
{
    if (!Rasterization.ReadyStatus || CommandBuffer == VK_NULL_HANDLE || Body.IndexCount == 0 ||
        Body.VertexBuffer == VK_NULL_HANDLE || Body.IndexBuffer == VK_NULL_HANDLE)
        return;

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Rasterization.Pipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Rasterization.PipelineLayout,
                            0, 1, &Rasterization.CameraSet, 0, nullptr);

    // 📝 Fill the viewport extent into the push block here (the caller supplies the width in px; the extent is per-target) so the vertex stage turns
    //    the pixel width into a constant-on-screen NDC offset.
    ParametricSketchStrokeConstants PushBlock = Constants;
    PushBlock.ViewportWidth  = (float)Width;
    PushBlock.ViewportHeight = (float)Height;
    vkCmdPushConstants(CommandBuffer, Rasterization.PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, (uint32_t)sizeof(ParametricSketchStrokeConstants), &PushBlock);

    VkViewport ViewportRect = {};
    ViewportRect.x        = 0.0f;
    ViewportRect.y        = 0.0f;
    ViewportRect.width    = (float)Width;
    ViewportRect.height   = (float)Height;
    ViewportRect.minDepth = 0.0f;
    ViewportRect.maxDepth = 1.0f;
    vkCmdSetViewport(CommandBuffer, 0, 1, &ViewportRect);

    VkRect2D ScissorRect = {};
    ScissorRect.offset = { 0, 0 };
    ScissorRect.extent = { Width, Height };
    vkCmdSetScissor(CommandBuffer, 0, 1, &ScissorRect);

    VkDeviceSize VertexOffset = 0;
    vkCmdBindVertexBuffers(CommandBuffer, 0, 1, &Body.VertexBuffer, &VertexOffset);
    vkCmdBindIndexBuffer(CommandBuffer, Body.IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(CommandBuffer, Body.IndexCount, 1, 0, 0, 0);
}

void FinalizeParametricSketchCurveRasterization(ParametricSketchCurveRasterization& Rasterization)
{
    if (Rasterization.Host != nullptr)
    {
        VkDevice Device = Rasterization.Host->Device;

        if (Rasterization.DescriptorPool  != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Rasterization.DescriptorPool, nullptr);   // frees the set
        if (Rasterization.CameraMapped    != nullptr && Rasterization.CameraMemory != VK_NULL_HANDLE) vkUnmapMemory(Device, Rasterization.CameraMemory);
        if (Rasterization.CameraBuffer    != VK_NULL_HANDLE) vkDestroyBuffer(Device, Rasterization.CameraBuffer, nullptr);
        if (Rasterization.CameraMemory    != VK_NULL_HANDLE) vkFreeMemory(Device, Rasterization.CameraMemory, nullptr);
        if (Rasterization.Pipeline        != VK_NULL_HANDLE) vkDestroyPipeline(Device, Rasterization.Pipeline, nullptr);
        if (Rasterization.PipelineLayout  != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Rasterization.PipelineLayout, nullptr);
        if (Rasterization.CameraSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Rasterization.CameraSetLayout, nullptr);
        if (Rasterization.VertModule      != VK_NULL_HANDLE) vkDestroyShaderModule(Device, Rasterization.VertModule, nullptr);
        if (Rasterization.FragModule      != VK_NULL_HANDLE) vkDestroyShaderModule(Device, Rasterization.FragModule, nullptr);
    }
    Rasterization = ParametricSketchCurveRasterization{};
}

} // namespace Frontier
