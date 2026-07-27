/*==============================================================================================================================================
                                                   PARAMETRICSKETCHSURFACEINSCRIPTION.CPP
==============================================================================================================================================*/
// 🧩 The lean chrome-matcap surface inscription: build the two-set pipeline once, then composite one loft body per record call. Set 0 is a
//    host-visible camera UBO this owns and maps persistently (rewritten each frame from the orbit camera); set 1 is the borrowed chrome matcap
//    sampler, written once. The pipeline consumes the engine's stride-32 RenderVertex (position @0, normal @12, texcoord @24) and depth-tests
//    LESS_OR_EQUAL against ParametricSketchViewTarget's D32_SFLOAT depth. Raw Vulkan, no VMA — the custom-pipeline idiom (shader-module load,
//    set-layout, pipeline layout, graphics pipeline, descriptor pool + set), recording into its own render pass directly. Wired onto VulkanHost.

#include "Graphics/Render/Surface/ParametricSketchSurfaceInscription.h"

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
    void ReportSurface(const char* MessageText)
    {
        std::fprintf(stderr, "[ParametricSketchSurfaceInscription] %s\n", MessageText);
    }

    // 📝 First memory type allowed by the requirement bitmask carrying every required property bit — mirrors BufferAllocation so the whole engine
    //    selects memory identically.
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

    // 📝 Load a SPIR-V blob into a shader module. Empty / non-multiple-of-4 file → null module (caller degrades to no preview).
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

    // 📝 Host-visible / coherent uniform buffer of ByteSize, left persistently mapped so the camera block is rewritten each frame with a plain
    //    memcpy (no flush). On any failure both out-handles are null + the map pointer null (returns false).
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

    // 📝 The two set layouts: set 0 one uniform buffer (vertex stage — the camera block), set 1 one combined image sampler (fragment stage — the
    //    matcap). Both are single-binding. On failure the created layout(s) are released and false is returned with the handles null.
    bool BuildSetLayouts(ParametricSketchSurfaceInscription& Inscription)
    {
        VkDevice Device = Inscription.Host->Device;

        VkDescriptorSetLayoutBinding CameraBinding = {};
        CameraBinding.binding         = 0;
        CameraBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        CameraBinding.descriptorCount = 1;
        CameraBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo CameraLayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        CameraLayoutInformation.bindingCount = 1;
        CameraLayoutInformation.pBindings    = &CameraBinding;
        if (vkCreateDescriptorSetLayout(Device, &CameraLayoutInformation, nullptr, &Inscription.CameraSetLayout) != VK_SUCCESS)
        {
            Inscription.CameraSetLayout = VK_NULL_HANDLE;
            return false;
        }

        VkDescriptorSetLayoutBinding MatcapBinding = {};
        MatcapBinding.binding         = 0;
        MatcapBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        MatcapBinding.descriptorCount = 1;
        MatcapBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo MatcapLayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        MatcapLayoutInformation.bindingCount = 1;
        MatcapLayoutInformation.pBindings    = &MatcapBinding;
        if (vkCreateDescriptorSetLayout(Device, &MatcapLayoutInformation, nullptr, &Inscription.MatcapSetLayout) != VK_SUCCESS)
        {
            vkDestroyDescriptorSetLayout(Device, Inscription.CameraSetLayout, nullptr);
            Inscription.CameraSetLayout = VK_NULL_HANDLE;
            Inscription.MatcapSetLayout = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    // 📝 Build the graphics pipeline: { CameraSetLayout, MatcapSetLayout } layout with no push ranges, stride-32 RenderVertex input (position @0,
    //    normal @12, texcoord @24), triangle list, back-face cull off (loft sheets are two-sided; the frag flips the normal), depth test
    //    LESS_OR_EQUAL + write, one opaque colour attachment, dynamic viewport / scissor, against RenderPass subpass 0. On any failure the
    //    created pipeline-layout is released and false is returned.
    bool BuildPipeline(ParametricSketchSurfaceInscription& Inscription, VkRenderPass RenderPass)
    {
        VkDevice Device = Inscription.Host->Device;

        VkDescriptorSetLayout SetLayouts[2] = { Inscription.CameraSetLayout, Inscription.MatcapSetLayout };
        VkPipelineLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        LayoutInformation.setLayoutCount = 2;
        LayoutInformation.pSetLayouts    = SetLayouts;
        if (vkCreatePipelineLayout(Device, &LayoutInformation, nullptr, &Inscription.PipelineLayout) != VK_SUCCESS)
        {
            Inscription.PipelineLayout = VK_NULL_HANDLE;
            return false;
        }

        VkPipelineShaderStageCreateInfo Stages[2] = {};
        Stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        Stages[0].module = Inscription.VertModule;
        Stages[0].pName  = "main";
        Stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        Stages[1].module = Inscription.FragModule;
        Stages[1].pName  = "main";

        // ─── vertex input: the engine's stride-32 RenderVertex (position @0, normal @12, texcoord @24) ───
        VkVertexInputBindingDescription BindingDescription = {};
        BindingDescription.binding   = 0;
        BindingDescription.stride    = 32;
        BindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription AttributeDescriptions[3] = {};
        AttributeDescriptions[0].location = 0;
        AttributeDescriptions[0].binding  = 0;
        AttributeDescriptions[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
        AttributeDescriptions[0].offset   = 0;
        AttributeDescriptions[1].location = 1;
        AttributeDescriptions[1].binding  = 0;
        AttributeDescriptions[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
        AttributeDescriptions[1].offset   = 12;
        AttributeDescriptions[2].location = 2;
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

        // 📝 Cull nothing: a loft can be a single open sheet the camera sees from either side, and the fragment shader already faces the normal
        //    toward the camera so both faces shade. A closed solid still reads correctly with depth testing.
        VkPipelineRasterizationStateCreateInfo RasterInformation = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        RasterInformation.polygonMode = VK_POLYGON_MODE_FILL;
        RasterInformation.cullMode    = VK_CULL_MODE_NONE;
        RasterInformation.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        RasterInformation.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo MultisampleInformation = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        MultisampleInformation.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // 📝 Nearer fragments win — LESS_OR_EQUAL against the camera's [-1, 1] NDC depth (CameraConfiguration).
        VkPipelineDepthStencilStateCreateInfo DepthInformation = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        DepthInformation.depthTestEnable  = VK_TRUE;
        DepthInformation.depthWriteEnable = VK_TRUE;
        DepthInformation.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        // 📝 One opaque colour attachment, no blend — the matcap write is fully opaque (alpha 1) and clears to the pane background.
        VkPipelineColorBlendAttachmentState BlendAttachment = {};
        BlendAttachment.blendEnable    = VK_FALSE;
        BlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

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
        PipelineInformation.layout              = Inscription.PipelineLayout;
        PipelineInformation.renderPass          = RenderPass;
        PipelineInformation.subpass             = 0;
        if (vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &PipelineInformation, nullptr, &Inscription.Pipeline) != VK_SUCCESS)
        {
            Inscription.Pipeline = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    // 📝 Descriptor pool (1 uniform buffer + 1 combined image sampler, 2 sets) plus both sets: the camera set bound to the mapped UBO, the matcap
    //    set bound to the borrowed view + sampler. On any failure the pool is released and false is returned with the set handles null.
    bool BuildDescriptorSets(ParametricSketchSurfaceInscription& Inscription, VkImageView MatcapView, VkSampler MatcapSampler)
    {
        VkDevice Device = Inscription.Host->Device;

        VkDescriptorPoolSize PoolSizes[2] = {};
        PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        PoolSizes[0].descriptorCount = 1;
        PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        PoolSizes[1].descriptorCount = 1;

        VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        PoolInformation.maxSets       = 2;
        PoolInformation.poolSizeCount = 2;
        PoolInformation.pPoolSizes    = PoolSizes;
        if (vkCreateDescriptorPool(Device, &PoolInformation, nullptr, &Inscription.DescriptorPool) != VK_SUCCESS)
        {
            Inscription.DescriptorPool = VK_NULL_HANDLE;
            return false;
        }

        VkDescriptorSetAllocateInfo CameraAllocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        CameraAllocate.descriptorPool     = Inscription.DescriptorPool;
        CameraAllocate.descriptorSetCount = 1;
        CameraAllocate.pSetLayouts        = &Inscription.CameraSetLayout;
        if (vkAllocateDescriptorSets(Device, &CameraAllocate, &Inscription.CameraSet) != VK_SUCCESS)
        {
            Inscription.CameraSet = VK_NULL_HANDLE;
            return false;
        }

        VkDescriptorSetAllocateInfo MatcapAllocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        MatcapAllocate.descriptorPool     = Inscription.DescriptorPool;
        MatcapAllocate.descriptorSetCount = 1;
        MatcapAllocate.pSetLayouts        = &Inscription.MatcapSetLayout;
        if (vkAllocateDescriptorSets(Device, &MatcapAllocate, &Inscription.MatcapSet) != VK_SUCCESS)
        {
            Inscription.MatcapSet = VK_NULL_HANDLE;
            return false;
        }

        VkDescriptorBufferInfo CameraBufferInformation = {};
        CameraBufferInformation.buffer = Inscription.CameraBuffer;
        CameraBufferInformation.offset = 0;
        CameraBufferInformation.range  = VK_WHOLE_SIZE;

        VkDescriptorImageInfo MatcapImageInformation = {};
        MatcapImageInformation.sampler     = MatcapSampler;
        MatcapImageInformation.imageView   = MatcapView;
        MatcapImageInformation.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet Writes[2] = {};
        Writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[0].dstSet          = Inscription.CameraSet;
        Writes[0].dstBinding      = 0;
        Writes[0].descriptorCount = 1;
        Writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        Writes[0].pBufferInfo     = &CameraBufferInformation;
        Writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[1].dstSet          = Inscription.MatcapSet;
        Writes[1].dstBinding      = 0;
        Writes[1].descriptorCount = 1;
        Writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        Writes[1].pImageInfo      = &MatcapImageInformation;
        vkUpdateDescriptorSets(Device, 2, Writes, 0, nullptr);
        return true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeParametricSketchSurfaceInscription(ParametricSketchSurfaceInscription& Inscription,
                                                  VulkanHost&                         Host,
                                                  const char*                         VertSpvPath,
                                                  const char*                         FragSpvPath,
                                                  VkRenderPass                        RenderPass,
                                                  VkImageView                         MatcapView,
                                                  VkSampler                           MatcapSampler)
{
    Inscription = ParametricSketchSurfaceInscription{};
    Inscription.Host        = &Host;
    Inscription.ReadyStatus = false;

    if (VertSpvPath == nullptr || FragSpvPath == nullptr || RenderPass == VK_NULL_HANDLE ||
        MatcapView == VK_NULL_HANDLE || MatcapSampler == VK_NULL_HANDLE)
    {
        ReportSurface("null bring-up argument — preview disabled");
        return false;
    }

    Inscription.VertModule = LoadShaderModule(Host.Device, VertSpvPath);
    Inscription.FragModule = LoadShaderModule(Host.Device, FragSpvPath);
    if (Inscription.VertModule == VK_NULL_HANDLE || Inscription.FragModule == VK_NULL_HANDLE)
    {
        ReportSurface("shader module load failed — preview disabled");
        FinalizeParametricSketchSurfaceInscription(Inscription);
        return false;
    }

    if (!BuildSetLayouts(Inscription))
    {
        ReportSurface("descriptor set layout creation failed — preview disabled");
        FinalizeParametricSketchSurfaceInscription(Inscription);
        return false;
    }

    if (!BuildPipeline(Inscription, RenderPass))
    {
        ReportSurface("pipeline build failed — preview disabled");
        FinalizeParametricSketchSurfaceInscription(Inscription);
        return false;
    }

    if (!AllocateMappedUniformBuffer(Host, (VkDeviceSize)sizeof(ParametricSketchSurfaceCameraBlock),
                                     Inscription.CameraBuffer, Inscription.CameraMemory, Inscription.CameraMapped))
    {
        ReportSurface("camera uniform buffer allocation failed — preview disabled");
        FinalizeParametricSketchSurfaceInscription(Inscription);
        return false;
    }

    if (!BuildDescriptorSets(Inscription, MatcapView, MatcapSampler))
    {
        ReportSurface("descriptor set allocation failed — preview disabled");
        FinalizeParametricSketchSurfaceInscription(Inscription);
        return false;
    }

    Inscription.ReadyStatus = true;
    return true;
}

void RefreshParametricSketchSurfaceCamera(ParametricSketchSurfaceInscription& Inscription, const ParametricSketchSurfaceCameraBlock& Camera)
{
    if (!Inscription.ReadyStatus || Inscription.CameraMapped == nullptr) return;
    std::memcpy(Inscription.CameraMapped, &Camera, sizeof(ParametricSketchSurfaceCameraBlock));
}

void RecordParametricSketchSurfaceInto(ParametricSketchSurfaceInscription& Inscription,
                                       VkCommandBuffer                     CommandBuffer,
                                       const PolygonBufferAllocation&      Body,
                                       uint32_t                            Width,
                                       uint32_t                            Height)
{
    if (!Inscription.ReadyStatus || CommandBuffer == VK_NULL_HANDLE || Body.IndexCount == 0 ||
        Body.VertexBuffer == VK_NULL_HANDLE || Body.IndexBuffer == VK_NULL_HANDLE)
        return;

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Inscription.Pipeline);

    VkDescriptorSet Sets[2] = { Inscription.CameraSet, Inscription.MatcapSet };
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Inscription.PipelineLayout,
                            0, 2, Sets, 0, nullptr);

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

void FinalizeParametricSketchSurfaceInscription(ParametricSketchSurfaceInscription& Inscription)
{
    if (Inscription.Host != nullptr)
    {
        VkDevice Device = Inscription.Host->Device;

        if (Inscription.DescriptorPool  != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Inscription.DescriptorPool, nullptr);   // frees both sets
        if (Inscription.CameraMapped    != nullptr && Inscription.CameraMemory != VK_NULL_HANDLE) vkUnmapMemory(Device, Inscription.CameraMemory);
        if (Inscription.CameraBuffer    != VK_NULL_HANDLE) vkDestroyBuffer(Device, Inscription.CameraBuffer, nullptr);
        if (Inscription.CameraMemory    != VK_NULL_HANDLE) vkFreeMemory(Device, Inscription.CameraMemory, nullptr);
        if (Inscription.Pipeline        != VK_NULL_HANDLE) vkDestroyPipeline(Device, Inscription.Pipeline, nullptr);
        if (Inscription.PipelineLayout  != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Inscription.PipelineLayout, nullptr);
        if (Inscription.CameraSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Inscription.CameraSetLayout, nullptr);
        if (Inscription.MatcapSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Inscription.MatcapSetLayout, nullptr);
        if (Inscription.VertModule      != VK_NULL_HANDLE) vkDestroyShaderModule(Device, Inscription.VertModule, nullptr);
        if (Inscription.FragModule      != VK_NULL_HANDLE) vkDestroyShaderModule(Device, Inscription.FragModule, nullptr);
    }
    Inscription = ParametricSketchSurfaceInscription{};
}

} // namespace Frontier
