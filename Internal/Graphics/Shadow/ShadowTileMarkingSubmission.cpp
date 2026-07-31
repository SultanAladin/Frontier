/*==============================================================================================================================================
                                                   SHADOWTILEMARKINGSUBMISSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the sun-shadow marking chain's GPU side. Initialize builds one descriptor set layout shared by all three passes (storage
//    buffer 0 = the tile table, combined samplers 1/2 = the id buffer + scene depth, uniform 3 = the per-level toroidal origins, storage 4 = the
//    caster bounds), three pipeline layouts differing only in their push range, the two compute pipelines and S2's attachment-less graphics pipeline,
//    a host-visible coherent origin uniform buffer, and a point sampler. RefreshBindings re-points the three externally-owned bindings; RefreshOrigins
//    republishes the clipmap's window corners. The three Record functions push their constants and dispatch/draw, each barriering the table for the
//    next reader. Raw Vulkan, no VMA, mirroring the InstanceCullSubmission idioms.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Shadow/ShadowTileMarkingSubmission.h"

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

void ReportShadowTileMarking(const char* MessageText)
{
    std::fprintf(stderr, "[ShadowTileMarking] %s\n", MessageText);
}

// 📝 The binding numbers every one of the three shaders agrees on. Shared deliberately, so no set has to be rebound between passes.
constexpr uint32_t TileTableBinding    = 0;
constexpr uint32_t VisibilityBinding   = 1;
constexpr uint32_t SceneDepthBinding   = 2;
constexpr uint32_t LevelOriginBinding  = 3;
constexpr uint32_t CasterBoundsBinding = 4;

// 📝 The std140 mirror of ShadowLevelBlock. ⚠️ ivec4 per level, not ivec2: std140 rounds every array element up to 16 bytes, so the padding is
//    declared rather than left implicit — the shader comment makes the same point from the other side.
struct ShadowLevelOriginBlock
{
    int32_t LevelOrigin[ShadowTilemapLodCount][4] = {};   // [tile] - [.0]/[.1] = that level's ToroidalOrigin; [.2]/[.3] unused padding
};

std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
{
    std::vector<char> Bytes;
    FILE* Handle = std::fopen(FilePath.c_str(), "rb");
    if (Handle == nullptr)
        return Bytes;
    std::fseek(Handle, 0, SEEK_END);
    const long Size = std::ftell(Handle);
    std::fseek(Handle, 0, SEEK_SET);
    if (Size > 0)
    {
        Bytes.resize(static_cast<size_t>(Size));
        const size_t ReadBytes = std::fread(Bytes.data(), 1, static_cast<size_t>(Size), Handle);
        if (ReadBytes != static_cast<size_t>(Size))
            Bytes.clear();
    }
    std::fclose(Handle);
    return Bytes;
}

VkShaderModule ConstructShaderModule(const VulkanHost& Host, const std::vector<char>& Bytes)
{
    if (Bytes.empty() || (Bytes.size() % 4) != 0)
        return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo ModuleInformation = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ModuleInformation.codeSize = Bytes.size();
    ModuleInformation.pCode    = reinterpret_cast<const uint32_t*>(Bytes.data());

    VkShaderModule Module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(Host.Device, &ModuleInformation, Host.Allocator, &Module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return Module;
}

// One buffer + its backing allocation. Returns false with both handles null on any failure, so the caller's cleanup path stays uniform.
bool ProvisionUniformBuffer(VulkanHost&     Host,
                            VkDeviceSize    ByteSize,
                            VkBuffer&       OutBuffer,
                            VkDeviceMemory& OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteSize;
    BufferInformation.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Host.Device, &BufferInformation, Host.Allocator, &OutBuffer) != VK_SUCCESS)
    {
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements Requirements = {};
    vkGetBufferMemoryRequirements(Host.Device, OutBuffer, &Requirements);

    VkPhysicalDeviceMemoryProperties MemoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(Host.PhysicalDevice, &MemoryProperties);

    // ⚠️ HOST_COHERENT as well as HOST_VISIBLE: the block is 96 bytes and rewritten every image, so the coherent memory type's cost is irrelevant and
    //    the omitted vkFlushMappedMemoryRanges is a whole bug class avoided.
    const VkMemoryPropertyFlags Wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t                    TypeIndex = UINT32_MAX;
    for (uint32_t Index = 0; Index < MemoryProperties.memoryTypeCount; ++Index)
    {
        const bool TypeAllowed = (Requirements.memoryTypeBits & (1u << Index)) != 0;
        if (TypeAllowed && (MemoryProperties.memoryTypes[Index].propertyFlags & Wanted) == Wanted)
        {
            TypeIndex = Index;
            break;
        }
    }
    if (TypeIndex == UINT32_MAX)
    {
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo Allocation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    Allocation.allocationSize  = Requirements.size;
    Allocation.memoryTypeIndex = TypeIndex;
    if (vkAllocateMemory(Host.Device, &Allocation, Host.Allocator, &OutMemory) != VK_SUCCESS ||
        vkBindBufferMemory(Host.Device, OutBuffer, OutMemory, 0) != VK_SUCCESS)
    {
        if (OutMemory != VK_NULL_HANDLE)
            vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Build the one set layout all three passes share. Every binding is visible to COMPUTE and FRAGMENT, plus VERTEX for the caster bounds S2's vertex
// stage reads — one layout serving three pipelines is what keeps the set from being rebound mid-chain.
bool ConstructMarkingSetLayout(ShadowTileMarkingSubmission& Marking)
{
    VulkanHost& Host = *Marking.Host;

    VkDescriptorSetLayoutBinding Bindings[5] = {};

    Bindings[0].binding         = TileTableBinding;
    Bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[0].descriptorCount = 1;
    Bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    Bindings[1].binding         = VisibilityBinding;
    Bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Bindings[1].descriptorCount = 1;
    Bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    Bindings[2].binding         = SceneDepthBinding;
    Bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Bindings[2].descriptorCount = 1;
    Bindings[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    Bindings[3].binding         = LevelOriginBinding;
    Bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    Bindings[3].descriptorCount = 1;
    Bindings[3].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    Bindings[4].binding         = CasterBoundsBinding;
    Bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[4].descriptorCount = 1;
    Bindings[4].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = 5;
    LayoutInformation.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Marking.SetLayout) != VK_SUCCESS)
    {
        Marking.SetLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// One pipeline layout per pass: same set, different push range and stage. Returns false with the handle null on failure.
bool ConstructPipelineLayout(ShadowTileMarkingSubmission& Marking,
                             uint32_t                     PushByteSize,
                             VkShaderStageFlags           PushStages,
                             VkPipelineLayout&            OutLayout)
{
    VulkanHost& Host = *Marking.Host;

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = PushStages;
    PushRange.offset     = 0;
    PushRange.size       = PushByteSize;

    VkPipelineLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    LayoutInformation.setLayoutCount         = 1;
    LayoutInformation.pSetLayouts            = &Marking.SetLayout;
    LayoutInformation.pushConstantRangeCount = 1;
    LayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &LayoutInformation, Host.Allocator, &OutLayout) != VK_SUCCESS)
    {
        OutLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Build one compute pipeline from a .spv module beside ShaderDirectory. Destroys the module before returning.
// 🔴 DispatchBaseEnabled must be true for any pipeline that will be recorded with vkCmdDispatchBase and a NON-ZERO base group — the spec requires
//    VK_PIPELINE_CREATE_DISPATCH_BASE_BIT at CREATION time, and omitting it is undefined behaviour the driver is free to execute, silently drop, or
//    mis-offset. It is S3's per-level baseGroupZ that needs it; S1 dispatches from origin and does not. Caught by the validation layer, not by a
//    wrong count — S3 propagating nothing looks exactly like a shader-logic bug from the tally alone.
bool ConstructComputePipeline(ShadowTileMarkingSubmission& Marking,
                              const std::string&           ModulePath,
                              VkPipelineLayout             Layout,
                              bool                         DispatchBaseEnabled,
                              VkPipeline&                  OutPipeline)
{
    VulkanHost& Host = *Marking.Host;

    VkShaderModule Module = ConstructShaderModule(Host, RetrieveShaderBytes(ModulePath));
    if (Module == VK_NULL_HANDLE)
    {
        ReportShadowTileMarking("compute shader module unavailable — that marking pass will not run");
        return false;
    }

    VkPipelineShaderStageCreateInfo StageInformation = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    StageInformation.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    StageInformation.module = Module;
    StageInformation.pName  = "main";

    VkComputePipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    PipelineInformation.stage  = StageInformation;
    PipelineInformation.layout = Layout;
    PipelineInformation.flags  = DispatchBaseEnabled ? VK_PIPELINE_CREATE_DISPATCH_BASE_BIT : 0u;

    const VkResult Outcome = vkCreateComputePipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInformation, Host.Allocator, &OutPipeline);
    vkDestroyShaderModule(Host.Device, Module, Host.Allocator);
    if (Outcome != VK_SUCCESS)
    {
        OutPipeline = VK_NULL_HANDLE;
        ReportShadowTileMarking("compute pipeline creation failed");
        return false;
    }
    return true;
}

// Build S2's graphics pipeline: a 4-vertex triangle strip with NO vertex bindings (the corners come from gl_VertexIndex) and NO attachments at all.
// 🔴 Depth test AND depth write are both off, and colorAttachmentCount is 0 — the entire output is the fragment stage's atomicOr into the tile table.
//    rasterizerDiscardEnable therefore MUST stay VK_FALSE: discarding the raster would discard the side effect that IS the work.
bool ConstructTagPipeline(ShadowTileMarkingSubmission& Marking, const std::string& ShaderDirectory)
{
    VulkanHost& Host = *Marking.Host;

    VkShaderModule VertexModule   = ConstructShaderModule(Host, RetrieveShaderBytes(ShaderDirectory + "/ShadowTileTagInscription.vert.spv"));
    VkShaderModule FragmentModule = ConstructShaderModule(Host, RetrieveShaderBytes(ShaderDirectory + "/ShadowTileTagInscription.frag.spv"));
    if (VertexModule == VK_NULL_HANDLE || FragmentModule == VK_NULL_HANDLE)
    {
        if (VertexModule   != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, VertexModule,   Host.Allocator);
        if (FragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        ReportShadowTileMarking("caster-tag shader modules unavailable — S2 will not run");
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

    // No vertex buffers: the quad's four corners are derived from gl_VertexIndex and the caster from gl_InstanceIndex.
    VkPipelineVertexInputStateCreateInfo VertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    VkPipelineInputAssemblyStateCreateInfo InputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    // The tilemap is a fixed 32² lattice, so the viewport is static rather than dynamic — it never tracks the swapchain extent.
    VkViewport ViewportRegion = {};
    ViewportRegion.width    = static_cast<float>(ShadowTilemapResolution);
    ViewportRegion.height   = static_cast<float>(ShadowTilemapResolution);
    ViewportRegion.minDepth = 0.0f;
    ViewportRegion.maxDepth = 1.0f;

    VkRect2D Scissor = {};
    Scissor.extent.width  = ShadowTilemapResolution;
    Scissor.extent.height = ShadowTilemapResolution;

    VkPipelineViewportStateCreateInfo ViewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    ViewportState.viewportCount = 1;
    ViewportState.pViewports    = &ViewportRegion;
    ViewportState.scissorCount  = 1;
    ViewportState.pScissors     = &Scissor;

    VkPipelineRasterizationStateCreateInfo Rasterization = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    Rasterization.depthClampEnable        = VK_FALSE;
    Rasterization.rasterizerDiscardEnable = VK_FALSE;   // 🔴 see the banner — the side effect IS the work
    Rasterization.polygonMode             = VK_POLYGON_MODE_FILL;
    // ⚠️ Culling OFF. The quad's winding depends on the sign of the light basis axes, which flip as the sun crosses an axis; a culled quad would
    //    silently stop tagging for half the day. Coverage is the only thing this raster produces, so a back-facing quad is just as valid.
    Rasterization.cullMode                = VK_CULL_MODE_NONE;
    Rasterization.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Rasterization.lineWidth               = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 🔴 Both off. The vertex stage pancakes z to 0 because only 2D coverage matters; a depth test against a nonexistent attachment would be invalid,
    //    and a write would need one.
    VkPipelineDepthStencilStateCreateInfo DepthStencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    DepthStencil.depthTestEnable  = VK_FALSE;
    DepthStencil.depthWriteEnable = VK_FALSE;

    // No colour attachments, so no blend attachments either.
    VkPipelineColorBlendStateCreateInfo ColorBlend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    ColorBlend.attachmentCount = 0;

    VkPipelineRenderingCreateInfoKHR RenderingInformation = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
    RenderingInformation.colorAttachmentCount    = 0;
    RenderingInformation.depthAttachmentFormat   = VK_FORMAT_UNDEFINED;
    RenderingInformation.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    PipelineInformation.pNext               = &RenderingInformation;
    PipelineInformation.stageCount          = 2;
    PipelineInformation.pStages             = Stages;
    PipelineInformation.pVertexInputState   = &VertexInput;
    PipelineInformation.pInputAssemblyState = &InputAssembly;
    PipelineInformation.pViewportState      = &ViewportState;
    PipelineInformation.pRasterizationState = &Rasterization;
    PipelineInformation.pMultisampleState   = &Multisample;
    PipelineInformation.pDepthStencilState  = &DepthStencil;
    PipelineInformation.pColorBlendState    = &ColorBlend;
    PipelineInformation.layout              = Marking.TagLayout;
    PipelineInformation.renderPass          = VK_NULL_HANDLE;   // dynamic rendering

    const VkResult Outcome = vkCreateGraphicsPipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInformation,
                                                       Host.Allocator, &Marking.TagPipeline);
    vkDestroyShaderModule(Host.Device, VertexModule,   Host.Allocator);
    vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
    if (Outcome != VK_SUCCESS)
    {
        Marking.TagPipeline = VK_NULL_HANDLE;
        ReportShadowTileMarking("caster-tag graphics pipeline creation failed — S2 will not run");
        return false;
    }
    return true;
}

// Allocate the pool + one set and point binding 0 at the tile table and binding 3 at the owned origin block. Bindings 1/2/4 reference resources this
// unit does not own and are written by RefreshShadowTileMarkingBindings instead.
bool ConstructMarkingDescriptors(ShadowTileMarkingSubmission& Marking, const ShadowTileStore& Store)
{
    VulkanHost& Host = *Marking.Host;

    VkDescriptorPoolSize PoolSizes[3] = {};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSizes[0].descriptorCount = 2;   // tile table + caster bounds
    PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSizes[1].descriptorCount = 2;   // id buffer + depth
    PoolSizes[2].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    PoolSizes[2].descriptorCount = 1;   // level origins

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 1;
    PoolInformation.poolSizeCount = 3;
    PoolInformation.pPoolSizes    = PoolSizes;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Marking.DescriptorPool) != VK_SUCCESS)
    {
        Marking.DescriptorPool = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocation.descriptorPool     = Marking.DescriptorPool;
    SetAllocation.descriptorSetCount = 1;
    SetAllocation.pSetLayouts        = &Marking.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, &Marking.MarkingSet) != VK_SUCCESS)
    {
        Marking.MarkingSet = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorBufferInfo TableInformation  = { Store.TableBuffer,      0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo OriginInformation = { Marking.OriginBuffer,   0, VK_WHOLE_SIZE };

    VkWriteDescriptorSet Writes[2] = {};
    Writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[0].dstSet          = Marking.MarkingSet;
    Writes[0].dstBinding      = TileTableBinding;
    Writes[0].descriptorCount = 1;
    Writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[0].pBufferInfo     = &TableInformation;

    Writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[1].dstSet          = Marking.MarkingSet;
    Writes[1].dstBinding      = LevelOriginBinding;
    Writes[1].descriptorCount = 1;
    Writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    Writes[1].pBufferInfo     = &OriginInformation;

    vkUpdateDescriptorSets(Host.Device, 2, Writes, 0, nullptr);
    return true;
}

// Fence marking writes to the tile table against the next pass's read of it. Every pass in the chain both reads and writes the table, so the mask is
// symmetric — and 🔴 this is what makes S3's per-level loop actually ordered.
void BarrierTileTableWrites(VkCommandBuffer CommandBuffer, VkPipelineStageFlags SourceStages, VkPipelineStageFlags DestinationStages)
{
    VkMemoryBarrier TableBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    TableBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    TableBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(CommandBuffer, SourceStages, DestinationStages, 0, 1, &TableBarrier, 0, nullptr, 0, nullptr);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeShadowTileMarkingSubmission(ShadowTileMarkingSubmission& Marking,
                                           VulkanHost&                  Host,
                                           const ShadowTileStore&       Store,
                                           const char*                  ShaderDirectory)
{
    Marking      = ShadowTileMarkingSubmission{};
    Marking.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportShadowTileMarking("no device — marking chain not built");
        return false;
    }
    if (!Store.ReadyCondition || Store.TableBuffer == VK_NULL_HANDLE)
    {
        ReportShadowTileMarking("tile store not ready — marking chain not built");
        return false;
    }
    // S2 needs a rendering scope with no attachments, which only dynamic rendering expresses. S1/S3 do not care, so this gates the tag pipeline alone.
    const bool DynamicRenderingAvailable = Host.DynamicRenderingEnabled && Host.CmdBeginRendering != nullptr && Host.CmdEndRendering != nullptr;

    if (!ProvisionUniformBuffer(Host, sizeof(ShadowLevelOriginBlock), Marking.OriginBuffer, Marking.OriginMemory))
    {
        ReportShadowTileMarking("level-origin uniform buffer allocation failed");
        FinalizeShadowTileMarkingSubmission(Marking);
        return false;
    }
    if (vkMapMemory(Host.Device, Marking.OriginMemory, 0, VK_WHOLE_SIZE, 0, &Marking.OriginMapping) != VK_SUCCESS)
    {
        Marking.OriginMapping = nullptr;
        ReportShadowTileMarking("level-origin uniform buffer mapping failed");
        FinalizeShadowTileMarkingSubmission(Marking);
        return false;
    }
    // 📝 Zero the block before anything can read it: an unwritten origin would address a window corner of garbage, and the first image's dispatch may
    //    precede the first RefreshShadowTileOrigins if a caller reorders them.
    std::memset(Marking.OriginMapping, 0, sizeof(ShadowLevelOriginBlock));

    // A point sampler for both target reads. ⚠️ NEAREST, not LINEAR: binding 1 carries packed integer surface identities, which must never be
    //    interpolated — a filtered id is a different surface, not a blend of two.
    VkSamplerCreateInfo SamplerInformation = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    SamplerInformation.magFilter    = VK_FILTER_NEAREST;
    SamplerInformation.minFilter    = VK_FILTER_NEAREST;
    SamplerInformation.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    SamplerInformation.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.maxLod       = 0.0f;
    if (vkCreateSampler(Host.Device, &SamplerInformation, Host.Allocator, &Marking.TargetSampler) != VK_SUCCESS)
    {
        Marking.TargetSampler = VK_NULL_HANDLE;
        ReportShadowTileMarking("point sampler creation failed");
        FinalizeShadowTileMarkingSubmission(Marking);
        return false;
    }

    if (!ConstructMarkingSetLayout(Marking))
    {
        ReportShadowTileMarking("descriptor set layout creation failed");
        FinalizeShadowTileMarkingSubmission(Marking);
        return false;
    }
    if (!ConstructPipelineLayout(Marking, sizeof(ShadowMarkConstants), VK_SHADER_STAGE_COMPUTE_BIT, Marking.MarkLayout) ||
        !ConstructPipelineLayout(Marking, sizeof(ShadowPropagateConstants), VK_SHADER_STAGE_COMPUTE_BIT, Marking.PropagateLayout))
    {
        ReportShadowTileMarking("compute pipeline layout creation failed");
        FinalizeShadowTileMarkingSubmission(Marking);
        return false;
    }
    if (!ConstructMarkingDescriptors(Marking, Store))
    {
        ReportShadowTileMarking("descriptor pool / set allocation failed");
        FinalizeShadowTileMarkingSubmission(Marking);
        return false;
    }

    const std::string Directory = ShaderDirectory;
    if (!ConstructComputePipeline(Marking, Directory + "/MarkVisibleShadowPages.comp.spv", Marking.MarkLayout, false, Marking.MarkPipeline) ||
        !ConstructComputePipeline(Marking, Directory + "/ShadowTileLevelPropagate.comp.spv", Marking.PropagateLayout, true, Marking.PropagatePipeline))
    {
        FinalizeShadowTileMarkingSubmission(Marking);
        return false;
    }

    // 🔴 S2 is OPTIONAL and its absence is not a failure. Its fragment stage writes an SSBO, which needs fragmentStoresAndAtomics; without that
    //    feature the pipeline would be a validation error. S1 and S3 remain individually meaningful (receiver demand and its propagation still mark),
    //    so a device missing either prerequisite loses staleness tagging rather than the whole chain.
    if (!Host.FragmentStoresAndAtomicsEnabled)
    {
        ReportShadowTileMarking("fragmentStoresAndAtomics absent — S2 caster tagging disabled, S1/S3 still active");
    }
    else if (!DynamicRenderingAvailable)
    {
        ReportShadowTileMarking("dynamic rendering absent — S2 caster tagging disabled, S1/S3 still active");
    }
    else if (ConstructPipelineLayout(Marking, sizeof(ShadowTagConstants), VK_SHADER_STAGE_VERTEX_BIT, Marking.TagLayout) &&
             ConstructTagPipeline(Marking, Directory))
    {
        Marking.TagPipelineEnabled = true;
    }

    Marking.ReadyCondition = true;
    return true;
}

void RefreshShadowTileMarkingBindings(ShadowTileMarkingSubmission& Marking,
                                      VkImageView                  VisibilityView,
                                      VkImageView                  DepthView,
                                      VkBuffer                     CasterBoundsBuffer,
                                      uint32_t                     CasterCount)
{
    if (!Marking.ReadyCondition || Marking.Host == nullptr || Marking.MarkingSet == VK_NULL_HANDLE)
        return;

    VulkanHost& Host = *Marking.Host;
    Marking.CasterCount = CasterCount;

    VkDescriptorImageInfo  VisibilityInformation = {};
    VkDescriptorImageInfo  DepthInformation      = {};
    VkDescriptorBufferInfo BoundsInformation     = {};
    VkWriteDescriptorSet   Writes[3]             = {};
    uint32_t               WriteCount            = 0;

    if (VisibilityView != VK_NULL_HANDLE && VisibilityView != Marking.BoundVisibilityView)
    {
        VisibilityInformation.sampler     = Marking.TargetSampler;
        VisibilityInformation.imageView   = VisibilityView;
        VisibilityInformation.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        Writes[WriteCount].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[WriteCount].dstSet          = Marking.MarkingSet;
        Writes[WriteCount].dstBinding      = VisibilityBinding;
        Writes[WriteCount].descriptorCount = 1;
        Writes[WriteCount].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        Writes[WriteCount].pImageInfo      = &VisibilityInformation;
        ++WriteCount;
        Marking.BoundVisibilityView = VisibilityView;
    }
    if (DepthView != VK_NULL_HANDLE && DepthView != Marking.BoundDepthView)
    {
        DepthInformation.sampler     = Marking.TargetSampler;
        DepthInformation.imageView   = DepthView;
        DepthInformation.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        Writes[WriteCount].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[WriteCount].dstSet          = Marking.MarkingSet;
        Writes[WriteCount].dstBinding      = SceneDepthBinding;
        Writes[WriteCount].descriptorCount = 1;
        Writes[WriteCount].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        Writes[WriteCount].pImageInfo      = &DepthInformation;
        ++WriteCount;
        Marking.BoundDepthView = DepthView;
    }
    if (CasterBoundsBuffer != VK_NULL_HANDLE && CasterBoundsBuffer != Marking.BoundBoundsBuffer)
    {
        BoundsInformation.buffer = CasterBoundsBuffer;
        BoundsInformation.offset = 0;
        BoundsInformation.range  = VK_WHOLE_SIZE;

        Writes[WriteCount].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[WriteCount].dstSet          = Marking.MarkingSet;
        Writes[WriteCount].dstBinding      = CasterBoundsBinding;
        Writes[WriteCount].descriptorCount = 1;
        Writes[WriteCount].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[WriteCount].pBufferInfo     = &BoundsInformation;
        ++WriteCount;
        Marking.BoundBoundsBuffer = CasterBoundsBuffer;
    }

    if (WriteCount > 0)
        vkUpdateDescriptorSets(Host.Device, WriteCount, Writes, 0, nullptr);
}

void RefreshShadowTileOrigins(ShadowTileMarkingSubmission& Marking, const SunShadowClipmap& Clipmap)
{
    if (!Marking.ReadyCondition || Marking.OriginMapping == nullptr)
        return;

    // 📝 Rebuilt whole rather than patched per level: the block is 96 bytes, and zeroing the tail is what keeps a shrunk clipmap from leaving a coarse
    //    level pointing at a window that no longer exists.
    ShadowLevelOriginBlock Block;
    const size_t           LevelCount = Clipmap.Levels.size() < ShadowTilemapLodCount ? Clipmap.Levels.size()
                                                                                      : static_cast<size_t>(ShadowTilemapLodCount);
    for (size_t LevelIndex = 0; LevelIndex < LevelCount; ++LevelIndex)
    {
        Block.LevelOrigin[LevelIndex][0] = Clipmap.Levels[LevelIndex].ToroidalOrigin.XTile;
        Block.LevelOrigin[LevelIndex][1] = Clipmap.Levels[LevelIndex].ToroidalOrigin.YTile;
    }
    std::memcpy(Marking.OriginMapping, &Block, sizeof(Block));
}

void RecordShadowReceiverMarking(ShadowTileMarkingSubmission& Marking,
                                 const ShadowMarkConstants&   Constants,
                                 VkCommandBuffer              CommandBuffer)
{
    if (!Marking.ReadyCondition || Marking.MarkPipeline == VK_NULL_HANDLE)
        return;
    // ⚠️ Both target bindings must actually point somewhere. A descriptor left unwritten is not merely empty — reading it is undefined behaviour, so
    //    this early-out is a correctness gate rather than an optimization.
    if (Marking.BoundVisibilityView == VK_NULL_HANDLE || Marking.BoundDepthView == VK_NULL_HANDLE)
        return;
    if (Constants.ScreenExtentX <= 0 || Constants.ScreenExtentY <= 0)
        return;

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Marking.MarkPipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Marking.MarkLayout, 0, 1, &Marking.MarkingSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Marking.MarkLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ShadowMarkConstants), &Constants);

    const uint32_t GroupsX = (static_cast<uint32_t>(Constants.ScreenExtentX) + ShadowMarkWorkgroupEdge - 1) / ShadowMarkWorkgroupEdge;
    const uint32_t GroupsY = (static_cast<uint32_t>(Constants.ScreenExtentY) + ShadowMarkWorkgroupEdge - 1) / ShadowMarkWorkgroupEdge;
    vkCmdDispatch(CommandBuffer, GroupsX, GroupsY, 1);

    // S1's Used|Direct must be visible to S2's fragment stage and to S3's first level.
    BarrierTileTableWrites(CommandBuffer,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void RecordShadowCasterTagging(ShadowTileMarkingSubmission& Marking,
                               const SunShadowClipmap&      Clipmap,
                               VkCommandBuffer              CommandBuffer)
{
    if (!Marking.ReadyCondition || !Marking.TagPipelineEnabled || Marking.TagPipeline == VK_NULL_HANDLE)
        return;
    if (Marking.CasterCount == 0 || Marking.BoundBoundsBuffer == VK_NULL_HANDLE || Clipmap.Levels.empty())
        return;

    VulkanHost& Host = *Marking.Host;

    // 📝 A scope with NO attachments of any kind. The render area still has to be the tilemap's extent because gl_FragCoord — which the fragment stage
    //    derives the physical slot from — is defined relative to it.
    VkRenderingInfoKHR RenderingInformation = { VK_STRUCTURE_TYPE_RENDERING_INFO_KHR };
    RenderingInformation.renderArea.extent.width  = ShadowTilemapResolution;
    RenderingInformation.renderArea.extent.height = ShadowTilemapResolution;
    RenderingInformation.layerCount               = 1;
    RenderingInformation.colorAttachmentCount     = 0;

    Host.CmdBeginRendering(CommandBuffer, &RenderingInformation);
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Marking.TagPipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Marking.TagLayout, 0, 1, &Marking.MarkingSet, 0, nullptr);

    const size_t LevelCount = Clipmap.Levels.size() < ShadowTilemapLodCount ? Clipmap.Levels.size()
                                                                            : static_cast<size_t>(ShadowTilemapLodCount);
    for (size_t LevelIndex = 0; LevelIndex < LevelCount; ++LevelIndex)
    {
        const SunShadowLevel& Level = Clipmap.Levels[LevelIndex];

        // 📝 The window's centre in LIGHT-TILE units, which is what the vertex stage maps to NDC. ToroidalOrigin is the ADD-form (negated) corner, so
        //    the resident span is [-Origin, -Origin + Resolution) and its centre is -Origin + Resolution/2.
        ShadowTagConstants Constants;
        Constants.LightRightAxis[0] = Clipmap.Basis.RightAxis.XCoord;
        Constants.LightRightAxis[1] = Clipmap.Basis.RightAxis.YCoord;
        Constants.LightRightAxis[2] = Clipmap.Basis.RightAxis.ZCoord;
        Constants.LightUpAxis[0]    = Clipmap.Basis.UpAxis.XCoord;
        Constants.LightUpAxis[1]    = Clipmap.Basis.UpAxis.YCoord;
        Constants.LightUpAxis[2]    = Clipmap.Basis.UpAxis.ZCoord;
        Constants.WindowCentreTileX = static_cast<float>(-Level.ToroidalOrigin.XTile) + 0.5f * static_cast<float>(Level.Resolution);
        Constants.WindowCentreTileY = static_cast<float>(-Level.ToroidalOrigin.YTile) + 0.5f * static_cast<float>(Level.Resolution);
        Constants.BaseTileMetres    = ShadowBaseTileMetres;
        Constants.Level             = static_cast<uint32_t>(LevelIndex);
        Constants.EdgeExpandTexels  = ShadowTagEdgeExpandTexels;

        vkCmdPushConstants(CommandBuffer, Marking.TagLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowTagConstants), &Constants);

        // 🔴 ONE sub-draw, the CURRENT bounds. The shader is written for a previous-bounds sub-draw too, but no previous-transform source exists yet,
        //    so a caster that MOVED will not invalidate the page it vacated. Adding a second draw here without that data would just tag the same
        //    tiles twice. Tracked in EngineDocs/Backlog.md.
        vkCmdDraw(CommandBuffer, 4, Marking.CasterCount, 0, 0);
    }
    Host.CmdEndRendering(CommandBuffer);

    // S2's Update must be visible to S3's reads and to any later consumer of the table.
    BarrierTileTableWrites(CommandBuffer,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void RecordShadowTilePropagation(ShadowTileMarkingSubmission& Marking,
                                 uint32_t                     LevelCount,
                                 VkCommandBuffer              CommandBuffer)
{
    if (!Marking.ReadyCondition || Marking.PropagatePipeline == VK_NULL_HANDLE)
        return;
    if (LevelCount < 2)
        return;   // one level has nothing coarser to propagate into

    const uint32_t BoundedLevelCount = LevelCount > ShadowTilemapLodCount ? ShadowTilemapLodCount : LevelCount;

    ShadowPropagateConstants Constants;
    Constants.LevelCount = BoundedLevelCount;

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Marking.PropagatePipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Marking.PropagateLayout, 0, 1, &Marking.MarkingSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Marking.PropagateLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ShadowPropagateConstants), &Constants);

    // 🔴 THIS LOOP IS THE LEVEL ORDERING, AND IT CANNOT BE COLLAPSED. Each dispatch is 1 x 1 x 1 with baseGroupZ set to its level, which the shader reads
    //    back as gl_GlobalInvocationID.z — ⚠️ NOT gl_WorkGroupID.z, which excludes the dispatch base and is therefore always 0 here; the barrier between
    //    consecutive dispatches is what makes level N's freshly-raised Coarse bits
    //    visible when level N+1 runs. A single 1 x 1 x BoundedLevelCount dispatch would run every level concurrently — Vulkan orders nothing between
    //    workgroups — so LOD 0's demand would reach LOD 1 and stop, nondeterministically.
    // 📝 The last level is skipped as a source: it has no coarser level to write into, and the shader early-outs on exactly that condition anyway.
    for (uint32_t Level = 0; Level + 1u < BoundedLevelCount; ++Level)
    {
        vkCmdDispatchBase(CommandBuffer, 0, 0, Level, 1, 1, 1);

        if (Level + 2u < BoundedLevelCount)
        {
            BarrierTileTableWrites(CommandBuffer,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
    }

    // Fence the whole chain's writes for whatever reads the table next (the S4/S5 allocator, or the download).
    BarrierTileTableWrites(CommandBuffer,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);
}

void FinalizeShadowTileMarkingSubmission(ShadowTileMarkingSubmission& Marking)
{
    if (Marking.Host == nullptr || Marking.Host->Device == VK_NULL_HANDLE)
    {
        Marking = ShadowTileMarkingSubmission{};
        return;
    }

    VulkanHost& Host = *Marking.Host;

    if (Marking.MarkPipeline      != VK_NULL_HANDLE) vkDestroyPipeline(Host.Device, Marking.MarkPipeline,      Host.Allocator);
    if (Marking.TagPipeline       != VK_NULL_HANDLE) vkDestroyPipeline(Host.Device, Marking.TagPipeline,       Host.Allocator);
    if (Marking.PropagatePipeline != VK_NULL_HANDLE) vkDestroyPipeline(Host.Device, Marking.PropagatePipeline, Host.Allocator);

    if (Marking.MarkLayout      != VK_NULL_HANDLE) vkDestroyPipelineLayout(Host.Device, Marking.MarkLayout,      Host.Allocator);
    if (Marking.TagLayout       != VK_NULL_HANDLE) vkDestroyPipelineLayout(Host.Device, Marking.TagLayout,       Host.Allocator);
    if (Marking.PropagateLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Host.Device, Marking.PropagateLayout, Host.Allocator);

    // 📝 The pool frees its one set; vkFreeDescriptorSets would be redundant.
    if (Marking.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Host.Device, Marking.DescriptorPool, Host.Allocator);
    if (Marking.SetLayout      != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Host.Device, Marking.SetLayout,  Host.Allocator);
    if (Marking.TargetSampler  != VK_NULL_HANDLE) vkDestroySampler(Host.Device, Marking.TargetSampler,          Host.Allocator);

    if (Marking.OriginMapping != nullptr)
        vkUnmapMemory(Host.Device, Marking.OriginMemory);
    if (Marking.OriginBuffer != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, Marking.OriginBuffer, Host.Allocator);
    if (Marking.OriginMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Marking.OriginMemory,    Host.Allocator);

    Marking = ShadowTileMarkingSubmission{};
}

} // namespace Frontier
