/*==============================================================================================================================================
                                                         VISIBILITYRASTERIZATION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the hardware visibility raster. Initialize reads the two SPIR-V modules, builds a set layout (one storage buffer, vertex
//    stage) + a pipeline layout carrying the ViewProjection push range, a graphics pipeline configured for dynamic rendering (R32_UINT colour +
//    D32 depth formats, stride-32 RenderVertex input, back-face cull, depth LESS_OR_EQUAL write), and a host-visible instance storage buffer sized
//    for MaxInstances. Upload memcpy's an instance list into the mapped buffer and points the descriptor at it. Record opens its OWN colour+depth
//    dynamic-rendering scope, clears both, binds the pipeline / instance set / borrowed mesh, and issues one instanced vkCmdDrawIndexed — hardware
//    depth testing keeps the nearest surface's packed identity per pixel. Raw Vulkan, no VMA, mirroring the VisibilityDepth / grid-pass idioms.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Visibility/VisibilityRasterization.h"

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

void ReportVisibilityRaster(const char* MessageText)
{
    std::fprintf(stderr, "[VisibilityRaster] %s\n", MessageText);
}

// First memory type allowed by the requirement bitmask carrying every required property bit.
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

// Read a whole SPIR-V file into a byte buffer. Empty on failure (missing / unreadable).
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

// Wrap a SPIR-V byte buffer in a VkShaderModule. VK_NULL_HANDLE on failure.
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

// Allocate a host-visible + host-coherent storage buffer of ByteCapacity. On any failure both out handles are null.
bool ConstructInstanceBuffer(VulkanHost&     Host,
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

// Build the set layout (binding 0 = storage buffer, vertex stage), pipeline layout (that set + the ViewProjection push range), the pool, and one
// allocated set. On any failure everything claimed is released and false is returned.
bool ConstructDescriptorPlumbing(VisibilityRasterization& Raster)
{
    VkDevice Device = Raster.Host->Device;

    // b0 = the per-instance storage buffer; b1 = the GPU cull's survivor list (both read in the vertex stage). b1 is always present so one pipeline /
    // set layout serves both the plain draw (CullActive 0, b1 unread) and the indirect draw (CullActive 1, gl_InstanceIndex remapped through b1).
    VkDescriptorSetLayoutBinding Bindings[2] = {};
    Bindings[0].binding         = 0;
    Bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[0].descriptorCount = 1;
    Bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    Bindings[1].binding         = 1;
    Bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[1].descriptorCount = 1;
    Bindings[1].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo SetLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    SetLayoutInfo.bindingCount = 2;
    SetLayoutInfo.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Device, &SetLayoutInfo, Raster.Host->Allocator, &Raster.SetLayout) != VK_SUCCESS)
    {
        Raster.SetLayout = VK_NULL_HANDLE;
        return false;
    }

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(VisibilityRasterConstants);

    VkPipelineLayoutCreateInfo LayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    LayoutInfo.setLayoutCount         = 1;
    LayoutInfo.pSetLayouts            = &Raster.SetLayout;
    LayoutInfo.pushConstantRangeCount = 1;
    LayoutInfo.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Device, &LayoutInfo, Raster.Host->Allocator, &Raster.PipelineLayout) != VK_SUCCESS)
    {
        Raster.PipelineLayout = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = 2;   // b0 instance + b1 survivor

    VkDescriptorPoolCreateInfo PoolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInfo.maxSets       = 1;
    PoolInfo.poolSizeCount = 1;
    PoolInfo.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Device, &PoolInfo, Raster.Host->Allocator, &Raster.DescriptorPool) != VK_SUCCESS)
    {
        Raster.DescriptorPool = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocate.descriptorPool     = Raster.DescriptorPool;
    SetAllocate.descriptorSetCount = 1;
    SetAllocate.pSetLayouts        = &Raster.SetLayout;
    if (vkAllocateDescriptorSets(Device, &SetAllocate, &Raster.InstanceSet) != VK_SUCCESS)
    {
        Raster.InstanceSet = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Build the dynamic-rendering graphics pipeline against the R32_UINT colour + D32 depth formats. Destroys the two modules before returning.
bool ConstructRasterPipeline(VisibilityRasterization& Raster,
                             VkFormat                 ColourFormat,
                             VkFormat                 DepthFormat,
                             const char*              ShaderDirectory)
{
    const VulkanHost& Host      = *Raster.Host;
    const std::string Directory = ShaderDirectory;
    VkShaderModule VertexModule   = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/VisibilityRaster.vert.spv"));
    VkShaderModule FragmentModule = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/VisibilityRaster.frag.spv"));
    if (VertexModule == VK_NULL_HANDLE || FragmentModule == VK_NULL_HANDLE)
    {
        if (VertexModule   != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        if (FragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        ReportVisibilityRaster("shader modules unavailable — visibility raster will not draw");
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

    // Vertex input: the engine's stride-32 RenderVertex (position @0, normal @12, texcoord @24).
    VkVertexInputBindingDescription BindingDescription = {};
    BindingDescription.binding   = 0;
    BindingDescription.stride    = 32;
    BindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription AttributeDescriptions[3] = {};
    AttributeDescriptions[0].location = 0; AttributeDescriptions[0].binding = 0; AttributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; AttributeDescriptions[0].offset = 0;
    AttributeDescriptions[1].location = 1; AttributeDescriptions[1].binding = 0; AttributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT; AttributeDescriptions[1].offset = 12;
    AttributeDescriptions[2].location = 2; AttributeDescriptions[2].binding = 0; AttributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;    AttributeDescriptions[2].offset = 24;

    VkPipelineVertexInputStateCreateInfo VertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VertexInput.vertexBindingDescriptionCount   = 1;
    VertexInput.pVertexBindingDescriptions      = &BindingDescription;
    VertexInput.vertexAttributeDescriptionCount = 3;
    VertexInput.pVertexAttributeDescriptions    = AttributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo InputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo Viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    Viewport.viewportCount = 1;
    Viewport.scissorCount  = 1;

    // Cull back faces: the Suzanne meshes are closed solids wound counter-clockwise, so back-face cull halves the raster work with no visible loss.
    VkPipelineRasterizationStateCreateInfo Rasterization = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    Rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    Rasterization.cullMode    = VK_CULL_MODE_BACK_BIT;
    Rasterization.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Rasterization.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Nearer fragments win, and the depth is written so the paired D32 target carries scene depth for the HiZ reduce.
    VkPipelineDepthStencilStateCreateInfo Depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    Depth.depthTestEnable  = VK_TRUE;
    Depth.depthWriteEnable = VK_TRUE;
    Depth.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    // The R32_UINT identity write is integer — no blending, full write mask. (Blending is undefined for integer attachments.)
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
    PipelineInfo.layout              = Raster.PipelineLayout;

    VkResult Outcome = vkCreateGraphicsPipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInfo, Host.Allocator, &Raster.Pipeline);
    vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
    vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
    if (Outcome != VK_SUCCESS)
    {
        Raster.Pipeline = VK_NULL_HANDLE;
        ReportVisibilityRaster("graphics pipeline creation failed");
        return false;
    }
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeVisibilityRasterization(VisibilityRasterization& Raster,
                                       VulkanHost&              Host,
                                       VkFormat                 ColourFormat,
                                       VkFormat                 DepthFormat,
                                       uint32_t                 MaxInstances,
                                       const char*              ShaderDirectory)
{
    Raster = VisibilityRasterization{};
    Raster.Host = &Host;

    if (!Host.DynamicRenderingEnabled || Host.Device == VK_NULL_HANDLE)
    {
        ReportVisibilityRaster("dynamic rendering unavailable — visibility raster not built");
        return false;
    }
    if (MaxInstances == 0)
        MaxInstances = 1;

    if (!ConstructDescriptorPlumbing(Raster))
    {
        ReportVisibilityRaster("descriptor plumbing creation failed");
        FinalizeVisibilityRasterization(Raster);
        return false;
    }

    const VkDeviceSize Capacity = (VkDeviceSize)MaxInstances * sizeof(SuzanneSceneInstance);
    if (!ConstructInstanceBuffer(Host, Capacity, Raster.InstanceBuffer, Raster.InstanceMemory))
    {
        ReportVisibilityRaster("instance storage buffer allocation failed");
        FinalizeVisibilityRasterization(Raster);
        return false;
    }
    Raster.InstanceCapacity = Capacity;

    // Point b0 at the whole instance buffer once; Upload only rewrites its bytes, never rebinds. Seed b1 (survivor) at the instance buffer too so the
    // set is fully written and legal to bind before the cull exists — it is never READ while CullActive is 0, and BindVisibilitySurvivorBuffer repoints
    // it at the cull's real survivor buffer once that is built.
    VkDescriptorBufferInfo BufferInfo = {};
    BufferInfo.buffer = Raster.InstanceBuffer;
    BufferInfo.offset = 0;
    BufferInfo.range  = Capacity;
    VkWriteDescriptorSet Writes[2] = {};
    Writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[0].dstSet          = Raster.InstanceSet;
    Writes[0].dstBinding      = 0;
    Writes[0].descriptorCount = 1;
    Writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[0].pBufferInfo     = &BufferInfo;
    Writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[1].dstSet          = Raster.InstanceSet;
    Writes[1].dstBinding      = 1;
    Writes[1].descriptorCount = 1;
    Writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[1].pBufferInfo     = &BufferInfo;
    vkUpdateDescriptorSets(Host.Device, 2, Writes, 0, nullptr);

    if (!ConstructRasterPipeline(Raster, ColourFormat, DepthFormat, ShaderDirectory))
    {
        FinalizeVisibilityRasterization(Raster);
        return false;
    }

    Raster.ReadyCondition = true;
    return true;
}

void UploadVisibilityScene(VisibilityRasterization& Raster, const std::vector<SuzanneSceneInstance>& Instances)
{
    if (!Raster.ReadyCondition || Raster.Host == nullptr || Raster.InstanceMemory == VK_NULL_HANDLE)
        return;

    const uint32_t MaxInstances = (uint32_t)(Raster.InstanceCapacity / sizeof(SuzanneSceneInstance));
    uint32_t Count = (uint32_t)Instances.size();
    if (Count > MaxInstances)
    {
        ReportVisibilityRaster("scene exceeds instance capacity — truncating");
        Count = MaxInstances;
    }

    void* Mapped = nullptr;
    if (vkMapMemory(Raster.Host->Device, Raster.InstanceMemory, 0, Raster.InstanceCapacity, 0, &Mapped) != VK_SUCCESS)
    {
        ReportVisibilityRaster("instance buffer map failed");
        Raster.InstanceCount = 0;
        return;
    }
    if (Count > 0)
        std::memcpy(Mapped, Instances.data(), (size_t)Count * sizeof(SuzanneSceneInstance));
    vkUnmapMemory(Raster.Host->Device, Raster.InstanceMemory);

    Raster.InstanceCount = Count;
}

void BeginVisibilityScope(VisibilityRasterization& Raster,
                          VisibilityImage&         Image,
                          VisibilityDepth&         Depth,
                          VkCommandBuffer          CommandBuffer,
                          bool                     PreserveContents)
{
    if (!Raster.ReadyCondition || Raster.Host == nullptr)
        return;
    if (!Image.ReadyCondition || !Depth.ReadyCondition)
        return;

    // When PreserveContents is true the images are already in their attachment layouts from an earlier fill this frame, and the barrier's job flips
    // from a plain layout move to a write-after-write fence: the LOAD must not begin until the earlier fill's attachment writes are complete and
    // available, or this scope's draws would race the contents they mean to preserve. So the source side names the producing stage + write access
    // instead of TOP_OF_PIPE / 0. On the ordinary (clear) path the source stays TOP_OF_PIPE / 0 — nothing this frame wrote the image yet.
    const VkPipelineStageFlags ColourSourceStage  = PreserveContents ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    const VkAccessFlags        ColourSourceAccess  = PreserveContents ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : 0;
    const VkPipelineStageFlags DepthSourceStage    = PreserveContents
                                                   ? (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT)
                                                   : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    const VkAccessFlags        DepthSourceAccess   = PreserveContents ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0;

    // Transition the visibility image to COLOR_ATTACHMENT (from UNDEFINED on the first frame, SHADER_READ_ONLY after a prior resolve, or already
    // COLOR_ATTACHMENT when re-opened with PreserveContents — a same-layout WAW fence in that case).
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
    ToColour.srcAccessMask       = ColourSourceAccess;
    ToColour.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         ColourSourceStage,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToColour);

    // Transition the depth image to DEPTH_STENCIL_ATTACHMENT (from UNDEFINED first frame, SHADER_READ_ONLY after a reduce, or already the attachment
    // layout when re-opened with PreserveContents). The dst side names both write + read: a preserved-depth scope depth-tests against the loaded values.
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
    ToDepth.srcAccessMask       = DepthSourceAccess;
    ToDepth.dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         DepthSourceStage,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToDepth);

    // Open a colour(visibility)+depth dynamic-rendering scope. When PreserveContents is false CLEAR the id buffer to the empty sentinel and depth to
    // the far plane ONCE — every mesh the caller draws after this shares the cleared buffer and depth-tests against what earlier meshes wrote (the
    // modern one-clear / N-mesh pattern). When PreserveContents is true LOAD both instead, re-opening the scope over an id + depth an earlier fill this
    // frame already wrote, so the meshes drawn here append and depth-test against those contents (late-survivor append / software-floor composite).
    const VkAttachmentLoadOp ContentLoadOp = PreserveContents ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;

    VkRenderingAttachmentInfoKHR ColourAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
    ColourAttachment.imageView   = Image.IdView;
    ColourAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ColourAttachment.loadOp      = ContentLoadOp;
    ColourAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    ColourAttachment.clearValue.color.uint32[0] = VisibilityEmptySentinel;

    VkRenderingAttachmentInfoKHR DepthAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
    DepthAttachment.imageView               = Depth.DepthView;
    DepthAttachment.imageLayout             = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    DepthAttachment.loadOp                  = ContentLoadOp;
    DepthAttachment.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
    DepthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfoKHR RenderingInformation = { VK_STRUCTURE_TYPE_RENDERING_INFO_KHR };
    RenderingInformation.renderArea.extent    = { Image.Width, Image.Height };
    RenderingInformation.layerCount           = 1;
    RenderingInformation.colorAttachmentCount = 1;
    RenderingInformation.pColorAttachments    = &ColourAttachment;
    RenderingInformation.pDepthAttachment     = &DepthAttachment;

    Raster.Host->CmdBeginRendering(CommandBuffer, &RenderingInformation);

    VkViewport ViewportRegion = {};
    ViewportRegion.width    = (float)Image.Width;
    ViewportRegion.height   = (float)Image.Height;
    ViewportRegion.minDepth = 0.0f;
    ViewportRegion.maxDepth = 1.0f;
    vkCmdSetViewport(CommandBuffer, 0, 1, &ViewportRegion);

    VkRect2D Scissor = {};
    Scissor.extent = { Image.Width, Image.Height };
    vkCmdSetScissor(CommandBuffer, 0, 1, &Scissor);
}

void DrawVisibilityMesh(VisibilityRasterization&         Raster,
                        VkDescriptorSet                  InstanceSet,
                        const PolygonBufferAllocation&   Mesh,
                        uint32_t                         InstanceCount,
                        const VisibilityRasterConstants& Constants,
                        bool                             Indirect,
                        VkBuffer                         ArgumentBuffer,
                        VkCommandBuffer                  CommandBuffer,
                        uint32_t                         FirstIndex,
                        uint32_t                         IndexCount)
{
    if (!Raster.ReadyCondition || Raster.Host == nullptr)
        return;
    if (Mesh.IndexCount == 0 || Mesh.IndexBuffer == VK_NULL_HANDLE)
        return;
    if (!Indirect && InstanceCount == 0)
        return;
    if (Indirect && ArgumentBuffer == VK_NULL_HANDLE)
        return;

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Raster.Pipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Raster.PipelineLayout, 0, 1, &InstanceSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Raster.PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VisibilityRasterConstants), &Constants);

    VkDeviceSize VertexOffset = 0;
    vkCmdBindVertexBuffers(CommandBuffer, 0, 1, &Mesh.VertexBuffer, &VertexOffset);
    vkCmdBindIndexBuffer(CommandBuffer, Mesh.IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    if (Indirect)
        // The cull's ArgumentBuffer holds one VkDrawIndexedIndirectCommand at offset 0; its instanceCount == the survivor count the vertex stage remaps.
        vkCmdDrawIndexedIndirect(CommandBuffer, ArgumentBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
    else
    {
        // 📝 IndexCount 0 is the whole-buffer default (the single-mesh case); a merged caller names its own sub-range. Clamped to what the buffer
        //    actually holds so a stale placement under-draws rather than reading past the allocation.
        const uint32_t DrawFirstIndex = (FirstIndex < Mesh.IndexCount) ? FirstIndex : 0u;
        const uint32_t RemainingRun   = Mesh.IndexCount - DrawFirstIndex;
        const uint32_t DrawIndexCount = (IndexCount == 0u || IndexCount > RemainingRun) ? RemainingRun : IndexCount;
        vkCmdDrawIndexed(CommandBuffer, DrawIndexCount, InstanceCount, DrawFirstIndex, 0, 0);
    }
}

void EndVisibilityScope(VisibilityRasterization& Raster,
                        VisibilityImage&         Image,
                        VisibilityDepth&         Depth,
                        VkCommandBuffer          CommandBuffer)
{
    if (!Raster.ReadyCondition || Raster.Host == nullptr)
        return;
    if (!Image.ReadyCondition || !Depth.ReadyCondition)
        return;

    Raster.Host->CmdEndRendering(CommandBuffer);

    Image.CurrentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    Depth.CurrentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
}

void RecordVisibilityRasterization(VisibilityRasterization&         Raster,
                                   VisibilityImage&                 Image,
                                   VisibilityDepth&                 Depth,
                                   const PolygonBufferAllocation&   Mesh,
                                   const VisibilityRasterConstants& Constants,
                                   VkCommandBuffer                  CommandBuffer)
{
    if (!Raster.ReadyCondition || Raster.Host == nullptr)
        return;
    if (!Image.ReadyCondition || !Depth.ReadyCondition)
        return;
    if (Mesh.IndexCount == 0 || Mesh.IndexBuffer == VK_NULL_HANDLE || Raster.InstanceCount == 0)
        return;

    BeginVisibilityScope(Raster, Image, Depth, CommandBuffer);
    DrawVisibilityMesh(Raster, Raster.InstanceSet, Mesh, Raster.InstanceCount, Constants, false, VK_NULL_HANDLE, CommandBuffer);
    EndVisibilityScope(Raster, Image, Depth, CommandBuffer);
}

void BindVisibilitySurvivorBuffer(VisibilityRasterization& Raster, VkBuffer SurvivorBuffer, VkDeviceSize SurvivorBytes)
{
    if (!Raster.ReadyCondition || Raster.Host == nullptr || SurvivorBuffer == VK_NULL_HANDLE)
        return;
    if (Raster.BoundSurvivorBuffer == SurvivorBuffer)
        return;

    VkDescriptorBufferInfo SurvivorInfo = {};
    SurvivorInfo.buffer = SurvivorBuffer;
    SurvivorInfo.offset = 0;
    SurvivorInfo.range  = SurvivorBytes;
    VkWriteDescriptorSet Write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    Write.dstSet          = Raster.InstanceSet;
    Write.dstBinding      = 1;
    Write.descriptorCount = 1;
    Write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Write.pBufferInfo     = &SurvivorInfo;
    vkUpdateDescriptorSets(Raster.Host->Device, 1, &Write, 0, nullptr);

    Raster.BoundSurvivorBuffer = SurvivorBuffer;
}

void RecordVisibilityRasterizationIndirect(VisibilityRasterization&         Raster,
                                           VisibilityImage&                 Image,
                                           VisibilityDepth&                 Depth,
                                           const PolygonBufferAllocation&   Mesh,
                                           const VisibilityRasterConstants& Constants,
                                           VkBuffer                         ArgumentBuffer,
                                           VkCommandBuffer                  CommandBuffer)
{
    if (!Raster.ReadyCondition || Raster.Host == nullptr)
        return;
    if (!Image.ReadyCondition || !Depth.ReadyCondition)
        return;
    if (Mesh.IndexCount == 0 || Mesh.IndexBuffer == VK_NULL_HANDLE || ArgumentBuffer == VK_NULL_HANDLE)
        return;

    BeginVisibilityScope(Raster, Image, Depth, CommandBuffer);
    DrawVisibilityMesh(Raster, Raster.InstanceSet, Mesh, Raster.InstanceCount, Constants, true, ArgumentBuffer, CommandBuffer);
    EndVisibilityScope(Raster, Image, Depth, CommandBuffer);
}

void FinalizeVisibilityRasterization(VisibilityRasterization& Raster)
{
    if (Raster.Host == nullptr || Raster.Host->Device == VK_NULL_HANDLE)
    {
        Raster = VisibilityRasterization{};
        return;
    }
    VkDevice Device = Raster.Host->Device;
    const VkAllocationCallbacks* Allocator = Raster.Host->Allocator;

    if (Raster.Pipeline       != VK_NULL_HANDLE) vkDestroyPipeline(Device, Raster.Pipeline, Allocator);
    if (Raster.PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Raster.PipelineLayout, Allocator);
    if (Raster.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Raster.DescriptorPool, Allocator);   // frees InstanceSet
    if (Raster.SetLayout      != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Raster.SetLayout, Allocator);
    if (Raster.InstanceBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, Raster.InstanceBuffer, Allocator);
    if (Raster.InstanceMemory != VK_NULL_HANDLE) vkFreeMemory(Device, Raster.InstanceMemory, Allocator);

    Raster = VisibilityRasterization{};
}

} // namespace Frontier
