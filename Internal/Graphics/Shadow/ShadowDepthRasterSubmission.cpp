/*==============================================================================================================================================
                                                   SHADOWDEPTHRASTERSUBMISSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of S7. Initialize builds a four-binding set layout (storage image 0 = the atlas, storage buffer 1 = the page mapping, storage
//    buffer 2 = the caster instances, storage buffer 3 = the per-page coverage words), one pipeline layout with the push range, and a
//    ZERO-ATTACHMENT graphics pipeline over the stride-32 RenderVertex input. Record opens one rendering scope per clipmap level sized to that
//    level's 32-tile window, pushes the level's light basis / toroidal origin / window centre, and issues an instanced vkCmdDrawIndexed; the fragment
//    stage resolves each fragment's atlas texel through the mapping, keeps the nearest depth with imageAtomicMin, and atomicOrs the coverage bit that
//    tells the tracer this page really was drawn. Raw Vulkan, no VMA, mirroring the ShadowPageClearSubmission idioms.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Shadow/ShadowDepthRasterSubmission.h"

// 📝 For SuzanneSceneInstance only, and solely so the static_assert below can pin the vec4 stride ShadowDepthRaster.vert hand-counts. This unit does
//    not otherwise touch the scene record — it binds the instance buffer as an opaque handle.
#include "Graphics/Scene/SuzanneScene.h"

#include <cmath>
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

void ReportShadowDepthRaster(const char* MessageText)
{
    std::fprintf(stderr, "[ShadowDepthRaster] %s\n", MessageText);
}

// 📝 The five bindings ShadowDepthRaster.vert/.frag declare between them.
constexpr uint32_t AtlasImageBinding    = 0;   // fragment: the atomic-min target
constexpr uint32_t PageMappingBinding   = 1;   // fragment: tile -> page
constexpr uint32_t CasterInstanceBinding = 2;  // vertex:   per-instance transforms
constexpr uint32_t PageCoverageBinding  = 3;   // fragment: one word per physical page, atomicOr'd to record that this page was drawn
constexpr uint32_t PageRenderMaskBinding = 4;  // fragment: one word per physical page, 1 = S6 primed it this image so S7 may write it

// 🔴 THE STRIDE ShadowDepthRaster.vert WALKS ITS INSTANCE BUFFER WITH, PINNED HERE BECAUSE THE SHADER TRANSCRIBES IT BY HAND AND NOTHING ELSE CAN
//    CATCH A DRIFT. That stage reads the record as a raw `vec4 InstanceWords[]` rather than a struct — it has to, so the std140 field order cannot
//    drift from the C++ — but the consequence is that the element COUNT per record is a literal in GLSL, the array is unsized so any stride is
//    in-bounds, and Vulkan validation is silent. The shader said 8 until 2026-07-31, having counted only the members it consumes (4 model columns +
//    3 normal-basis + 1 tint) and dropped the (PartitionId, MaterialId, Padding[2]) vec4 it does not read. It therefore strided 128 bytes through a
//    144-byte array: instance 0 was correct, instance 1 was off by one vec4, and instance 8 read instance 7's record outright.
// 🔴 THAT BUG IS INVISIBLE TO EVERY COUNTER IN THE PIPELINE AND READS AS A PAGING FAULT. Casters come out stretched along one axis (a NormalBasis row
//    lands in a model-matrix scale slot), one lands at the world origin (Tint {1,1,1,1} / Padding {0,0} land in the translation column), and the rest
//    are flung outside the window where they scribble depth into unrelated tiles — which looks like the shadow window tiling across the ground.
// ⚠️ The visibility raster is immune to this class of mistake because it declares the record as a std140 STRUCT and lets the compiler compute the
//    stride. Only S7 hand-counts, so only S7 needs this guard. If SuzanneSceneInstance gains or loses a member, this assert fires and the GLSL
//    `ShadowCasterInstanceStride` must move with it.
constexpr uint32_t ShadowCasterInstanceStride = 9;   // [vec4] - must equal ShadowDepthRaster.vert's ShadowCasterInstanceStride
static_assert(sizeof(SuzanneSceneInstance) == ShadowCasterInstanceStride * 4 * sizeof(float),
              "SuzanneSceneInstance's size no longer matches the vec4 stride ShadowDepthRaster.vert walks its instance buffer with. Update "
              "ShadowCasterInstanceStride in BOTH this file and Internal/Graphics/Shadow/Shaders/ShadowDepthRaster.vert — a mismatch makes every "
              "caster after the first read its transform from the wrong record, which no counter or validation layer can see.");

// 🔴 The record path rasterizes a window of ShadowTilemapResolution * ShadowPageResolution pixels on a side, one fragment per page texel. 4096 is the
//    figure Vulkan GUARANTEES for maxViewportDimensions / maxFramebufferWidth / maxFramebufferHeight, so the current 32 * 128 sits EXACTLY at the
//    floor and needs no device query — but one step past it is a viewport the driver is free to reject or silently clamp, and a clamp would go back to
//    partially-filled pages with every counter still reporting success. That failure is invisible in the census, which is precisely why this is a
//    compile-time stop rather than a runtime warning.
// ⚠️ Raising either constant means adding a real vkGetPhysicalDeviceProperties limit query and a fallback that splits the window into several draws.
constexpr uint32_t ShadowDepthWindowTexelEdge = ShadowTilemapResolution * ShadowPageResolution;
static_assert(ShadowDepthWindowTexelEdge <= 4096,
              "S7's raster window exceeds Vulkan's guaranteed 4096 viewport/framebuffer dimension and there is no device-limit query to fall back "
              "on. Lower ShadowTilemapResolution or ShadowPageResolution, or add the query plus a multi-draw split.");

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

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeShadowDepthRasterSubmission(ShadowDepthRasterSubmission& Raster,
                                           VulkanHost&                  Host,
                                           const ShadowPageAtlas&       Atlas,
                                           const char*                  ShaderDirectory)
{
    Raster = ShadowDepthRasterSubmission{};

    if (Host.Device == VK_NULL_HANDLE || ShaderDirectory == nullptr)
    {
        ReportShadowDepthRaster("no device or shader directory — S7 disabled");
        return false;
    }
    if (!Atlas.ReadyCondition || Atlas.AtlasStorageView == VK_NULL_HANDLE || Atlas.MappingBuffer == VK_NULL_HANDLE)
    {
        ReportShadowDepthRaster("page atlas not ready — S7 disabled");
        return false;
    }

    // 🔴 Dynamic rendering is required: a zero-attachment pass through a VkRenderPass would need a render pass object with no attachments AND a
    //    matching framebuffer, which several drivers reject outright. The visibility raster already depends on this entry point.
    if (Host.CmdBeginRendering == nullptr || Host.CmdEndRendering == nullptr)
    {
        ReportShadowDepthRaster("dynamic rendering unavailable — S7 disabled");
        return false;
    }

    // ⚠️ The fragment stage writes a storage image. Without this feature the pass is not merely slow, it is invalid.
    if (!Host.FragmentStoresAndAtomicsEnabled)
    {
        ReportShadowDepthRaster("fragmentStoresAndAtomics absent — S7 disabled, no shadow depth will be rasterized");
        return false;
    }

    Raster.Host = &Host;

    //---- set layout ------------------------------------------------------------------------------------------------------
    VkDescriptorSetLayoutBinding Bindings[5] = {};
    Bindings[0].binding         = AtlasImageBinding;
    Bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    Bindings[0].descriptorCount = 1;
    Bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    Bindings[1].binding         = PageMappingBinding;
    Bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[1].descriptorCount = 1;
    Bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    Bindings[2].binding         = CasterInstanceBinding;
    Bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[2].descriptorCount = 1;
    Bindings[2].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    Bindings[3].binding         = PageCoverageBinding;
    Bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[3].descriptorCount = 1;
    Bindings[3].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    Bindings[4].binding         = PageRenderMaskBinding;
    Bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[4].descriptorCount = 1;
    Bindings[4].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = 5;
    LayoutInformation.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Raster.SetLayout) != VK_SUCCESS)
    {
        ReportShadowDepthRaster("descriptor set layout creation failed");
        FinalizeShadowDepthRasterSubmission(Raster);
        return false;
    }

    //---- pipeline layout -------------------------------------------------------------------------------------------------
    // ⚠️ The push range spans BOTH stages: each declares the block independently and both read from offset 0.
    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(ShadowDepthRasterConstants);

    VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipelineLayoutInformation.setLayoutCount         = 1;
    PipelineLayoutInformation.pSetLayouts            = &Raster.SetLayout;
    PipelineLayoutInformation.pushConstantRangeCount = 1;
    PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &PipelineLayoutInformation, Host.Allocator, &Raster.RasterLayout) != VK_SUCCESS)
    {
        ReportShadowDepthRaster("pipeline layout creation failed");
        FinalizeShadowDepthRasterSubmission(Raster);
        return false;
    }

    //---- shader modules --------------------------------------------------------------------------------------------------
    const std::string Directory(ShaderDirectory);
    const std::vector<char> VertexBytes   = RetrieveShaderBytes(Directory + "/ShadowDepthRaster.vert.spv");
    const std::vector<char> FragmentBytes = RetrieveShaderBytes(Directory + "/ShadowDepthRaster.frag.spv");

    VkShaderModule VertexModule   = ConstructShaderModule(Host, VertexBytes);
    VkShaderModule FragmentModule = ConstructShaderModule(Host, FragmentBytes);
    if (VertexModule == VK_NULL_HANDLE || FragmentModule == VK_NULL_HANDLE)
    {
        if (VertexModule != VK_NULL_HANDLE)   vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
        if (FragmentModule != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
        ReportShadowDepthRaster("ShadowDepthRaster.vert.spv / .frag.spv missing or malformed");
        FinalizeShadowDepthRasterSubmission(Raster);
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

    //---- vertex input ----------------------------------------------------------------------------------------------------
    // 📝 The same stride-32 RenderVertex the visibility raster binds (position @0, normal @12, texcoord @24). Only the position is declared here —
    //    a caster's depth does not depend on its shading attributes, and an attribute the shader never reads costs fetch bandwidth per vertex.
    VkVertexInputBindingDescription BindingDescription = {};
    BindingDescription.binding   = 0;
    BindingDescription.stride    = 32;
    BindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription AttributeDescription = {};
    AttributeDescription.location = 0;
    AttributeDescription.binding  = 0;
    AttributeDescription.format   = VK_FORMAT_R32G32B32_SFLOAT;
    AttributeDescription.offset   = 0;

    VkPipelineVertexInputStateCreateInfo VertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VertexInput.vertexBindingDescriptionCount   = 1;
    VertexInput.pVertexBindingDescriptions      = &BindingDescription;
    VertexInput.vertexAttributeDescriptionCount = 1;
    VertexInput.pVertexAttributeDescriptions    = &AttributeDescription;

    VkPipelineInputAssemblyStateCreateInfo InputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo ViewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    ViewportState.viewportCount = 1;
    ViewportState.scissorCount  = 1;

    //---- rasterizer ------------------------------------------------------------------------------------------------------
    // 🔴 CULLING IS OFF, and that is a shadow-specific decision rather than an oversight. Back-face culling from the LIGHT's point of view discards the
    //    far side of a closed caster — which is exactly the surface that would have written the far depth — and for an open or single-sided mesh (the
    //    floor slab, a plane) it can discard the ONLY face, so the object stops casting a shadow entirely. Depth-only passes elsewhere cull front faces
    //    to fight acne, but that trades one artifact for a missing caster; with atomic-min resolve the near surface wins anyway, so keeping both faces
    //    is both simpler and strictly more correct here.
    VkPipelineRasterizationStateCreateInfo Rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    Rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    Rasterizer.cullMode    = VK_CULL_MODE_NONE;
    Rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Rasterizer.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 🔴 DEPTH TEST AND DEPTH WRITE BOTH OFF, because there is no depth attachment to test against — the atlas cannot be one (R32_UINT,
    //    STORAGE|SAMPLED). Occlusion is the fragment stage's imageAtomicMin. Enabling either here is invalid against a scope with no depth attachment.
    VkPipelineDepthStencilStateCreateInfo DepthStencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    DepthStencil.depthTestEnable  = VK_FALSE;
    DepthStencil.depthWriteEnable = VK_FALSE;

    // 🔴 ZERO COLOUR ATTACHMENTS. The fragment stage's only output is the storage-image atomic; it declares no `out` variable at all. A dummy colour
    //    attachment would force a 4096² throwaway target for nothing.
    VkPipelineColorBlendStateCreateInfo ColourBlend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    ColourBlend.attachmentCount = 0;
    ColourBlend.pAttachments    = nullptr;

    const VkDynamicState DynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo DynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    DynamicState.dynamicStateCount = 2;
    DynamicState.pDynamicStates    = DynamicStates;

    // 📝 Dynamic rendering needs the attachment formats up front. Zero colour, no depth, no stencil — the pipeline matches the scope the record path opens.
    VkPipelineRenderingCreateInfoKHR RenderingInformation = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
    RenderingInformation.colorAttachmentCount    = 0;
    RenderingInformation.pColorAttachmentFormats = nullptr;
    RenderingInformation.depthAttachmentFormat   = VK_FORMAT_UNDEFINED;
    RenderingInformation.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    PipelineInformation.pNext               = &RenderingInformation;
    PipelineInformation.stageCount          = 2;
    PipelineInformation.pStages             = Stages;
    PipelineInformation.pVertexInputState   = &VertexInput;
    PipelineInformation.pInputAssemblyState = &InputAssembly;
    PipelineInformation.pViewportState      = &ViewportState;
    PipelineInformation.pRasterizationState = &Rasterizer;
    PipelineInformation.pMultisampleState   = &Multisample;
    PipelineInformation.pDepthStencilState  = &DepthStencil;
    PipelineInformation.pColorBlendState    = &ColourBlend;
    PipelineInformation.pDynamicState       = &DynamicState;
    PipelineInformation.layout              = Raster.RasterLayout;
    PipelineInformation.renderPass          = VK_NULL_HANDLE;   // dynamic rendering
    PipelineInformation.subpass             = 0;

    const VkResult PipelineResult = vkCreateGraphicsPipelines(Host.Device, VK_NULL_HANDLE, 1,
                                                             &PipelineInformation, Host.Allocator, &Raster.RasterPipeline);
    vkDestroyShaderModule(Host.Device, VertexModule, Host.Allocator);
    vkDestroyShaderModule(Host.Device, FragmentModule, Host.Allocator);
    if (PipelineResult != VK_SUCCESS)
    {
        ReportShadowDepthRaster("graphics pipeline creation failed");
        FinalizeShadowDepthRasterSubmission(Raster);
        return false;
    }

    //---- pool + one set per caster mesh ----------------------------------------------------------------------------------
    // 🔴 Sized for ShadowCasterSetCapacity sets, not one: each caster mesh needs its OWN set so that b2 is never rewritten between two draws that are
    //    both still queued. Every set carries a copy of the same b0/b1, hence the multiplied counts.
    VkDescriptorPoolSize PoolSizes[2] = {};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    PoolSizes[0].descriptorCount = ShadowCasterSetCapacity;          // b0, once per set
    PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSizes[1].descriptorCount = 4u * ShadowCasterSetCapacity;     // b1 + b2 + b3 + b4, once per set

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = ShadowCasterSetCapacity;
    PoolInformation.poolSizeCount = 2;
    PoolInformation.pPoolSizes    = PoolSizes;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Raster.DescriptorPool) != VK_SUCCESS)
    {
        ReportShadowDepthRaster("descriptor pool creation failed");
        FinalizeShadowDepthRasterSubmission(Raster);
        return false;
    }

    // 📝 All sets are allocated here, up front, rather than on demand: allocation can fail, and discovering that mid-frame (when a second caster mesh
    //    first appears) would drop that mesh's shadow with no way to report it. Their b2 stays unwritten until AcquireShadowCasterSet points it.
    VkDescriptorSetLayout SetLayouts[ShadowCasterSetCapacity] = {};
    for (uint32_t Index = 0; Index < ShadowCasterSetCapacity; ++Index)
        SetLayouts[Index] = Raster.SetLayout;

    VkDescriptorSetAllocateInfo SetAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocation.descriptorPool     = Raster.DescriptorPool;
    SetAllocation.descriptorSetCount = ShadowCasterSetCapacity;
    SetAllocation.pSetLayouts        = SetLayouts;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, Raster.CasterSets) != VK_SUCCESS)
    {
        ReportShadowDepthRaster("descriptor set allocation failed");
        FinalizeShadowDepthRasterSubmission(Raster);
        return false;
    }

    //---- point every set's b0 at the atlas image and b1 at the mapping ---------------------------------------------------
    // ⚠️ VK_IMAGE_LAYOUT_GENERAL, matching what the caller transitions the atlas to. A storage image descriptor's layout must equal the layout the
    //    image is actually in when the draw executes.
    VkDescriptorImageInfo AtlasInfo = {};
    AtlasInfo.imageView   = Atlas.AtlasStorageView;
    AtlasInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo MappingInfo = {};
    MappingInfo.buffer = Atlas.MappingBuffer;
    MappingInfo.offset = 0;
    MappingInfo.range  = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo CoverageInfo = {};
    CoverageInfo.buffer = Atlas.CoverageBuffer;
    CoverageInfo.offset = 0;
    CoverageInfo.range  = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo RenderMaskInfo = {};
    RenderMaskInfo.buffer = Atlas.RenderMaskBuffer;
    RenderMaskInfo.offset = 0;
    RenderMaskInfo.range  = VK_WHOLE_SIZE;

    VkWriteDescriptorSet Writes[4u * ShadowCasterSetCapacity] = {};
    for (uint32_t Index = 0; Index < ShadowCasterSetCapacity; ++Index)
    {
        VkWriteDescriptorSet& AtlasWrite = Writes[Index * 4u + 0u];
        AtlasWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        AtlasWrite.dstSet          = Raster.CasterSets[Index];
        AtlasWrite.dstBinding      = AtlasImageBinding;
        AtlasWrite.descriptorCount = 1;
        AtlasWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        AtlasWrite.pImageInfo      = &AtlasInfo;

        VkWriteDescriptorSet& MappingWrite = Writes[Index * 4u + 1u];
        MappingWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        MappingWrite.dstSet          = Raster.CasterSets[Index];
        MappingWrite.dstBinding      = PageMappingBinding;
        MappingWrite.descriptorCount = 1;
        MappingWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        MappingWrite.pBufferInfo     = &MappingInfo;

        VkWriteDescriptorSet& CoverageWrite = Writes[Index * 4u + 2u];
        CoverageWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        CoverageWrite.dstSet          = Raster.CasterSets[Index];
        CoverageWrite.dstBinding      = PageCoverageBinding;
        CoverageWrite.descriptorCount = 1;
        CoverageWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        CoverageWrite.pBufferInfo     = &CoverageInfo;

        VkWriteDescriptorSet& RenderMaskWrite = Writes[Index * 4u + 3u];
        RenderMaskWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        RenderMaskWrite.dstSet          = Raster.CasterSets[Index];
        RenderMaskWrite.dstBinding      = PageRenderMaskBinding;
        RenderMaskWrite.descriptorCount = 1;
        RenderMaskWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        RenderMaskWrite.pBufferInfo     = &RenderMaskInfo;
    }

    vkUpdateDescriptorSets(Host.Device, (uint32_t)(4u * ShadowCasterSetCapacity), Writes, 0, nullptr);

    // 📝 Binding 2 (the caster instances) is left unwritten here — the visibility rasters own those buffers and may not exist yet. The caller points one
    //    per mesh with AcquireShadowCasterSet before the first record; ReadyCondition does not imply any set is complete, which the record path tests by
    //    rejecting a set it did not hand out.
    Raster.ReadyCondition = true;
    return true;
}

VkDescriptorSet AcquireShadowCasterSet(ShadowDepthRasterSubmission& Raster, VkBuffer InstanceBuffer, VkDeviceSize InstanceBytes)
{
    if (!Raster.ReadyCondition || Raster.Host == nullptr)
        return VK_NULL_HANDLE;
    if (InstanceBuffer == VK_NULL_HANDLE || InstanceBytes == 0)
        return VK_NULL_HANDLE;

    // 📝 The cached-hit path, which is what makes calling this every image free: an already-pointed buffer returns its existing set and touches no
    //    descriptor, so it is safe with frames in flight.
    for (uint32_t Index = 0; Index < Raster.CasterSetCount; ++Index)
    {
        if (Raster.CasterBuffers[Index] == InstanceBuffer)
            return Raster.CasterSets[Index];
    }

    // ⚠️ Exhaustion is reported rather than silently aliasing onto an existing set — aliasing would make one mesh cast with another's transforms, which
    //    reads as a shadow of the wrong shape rather than as a missing resource.
    if (Raster.CasterSetCount >= ShadowCasterSetCapacity)
    {
        ReportShadowDepthRaster("caster set capacity exhausted — this mesh will not cast a shadow");
        return VK_NULL_HANDLE;
    }

    const uint32_t Index = Raster.CasterSetCount;

    VkDescriptorBufferInfo InstanceInfo = {};
    InstanceInfo.buffer = InstanceBuffer;
    InstanceInfo.offset = 0;
    InstanceInfo.range  = InstanceBytes;

    VkWriteDescriptorSet Write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    Write.dstSet          = Raster.CasterSets[Index];
    Write.dstBinding      = CasterInstanceBinding;
    Write.descriptorCount = 1;
    Write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Write.pBufferInfo     = &InstanceInfo;

    vkUpdateDescriptorSets(Raster.Host->Device, 1, &Write, 0, nullptr);

    Raster.CasterBuffers[Index] = InstanceBuffer;
    Raster.CasterSetCount       = Index + 1u;
    return Raster.CasterSets[Index];
}

uint32_t RecordShadowDepthRaster(ShadowDepthRasterSubmission&   Raster,
                                 const ShadowPageAtlas&         Atlas,
                                 const SunShadowClipmap&        Clipmap,
                                 VkDescriptorSet                CasterSet,
                                 const PolygonBufferAllocation& Mesh,
                                 uint32_t                       InstanceCount,
                                 VkCommandBuffer                CommandBuffer)
{
    Raster.LastDrawnLevels = 0;

    if (!Raster.ReadyCondition || Raster.Host == nullptr)
        return 0;
    if (!Atlas.ReadyCondition || !Clipmap.ReadyCondition)
        return 0;
    if (Mesh.IndexCount == 0 || Mesh.IndexBuffer == VK_NULL_HANDLE || Mesh.VertexBuffer == VK_NULL_HANDLE)
        return 0;
    if (InstanceCount == 0)
        return 0;

    // ⚠️ The set must be one this unit handed out, not merely non-null: a set from anywhere else would have binding 2 pointing at something that is not
    //    an instance buffer (or nothing at all), and the vertex stage would read it as transforms. Silently drawing nothing is the safe degradation.
    bool SetRecognized = false;
    for (uint32_t Index = 0; Index < Raster.CasterSetCount && !SetRecognized; ++Index)
        SetRecognized = (Raster.CasterSets[Index] == CasterSet);
    if (!SetRecognized)
        return 0;

    // 🔴 Nothing to do when no page needs redrawing, and returning early is the WHOLE POINT of the cache rather than a micro-optimization: on a static
    //    scene this is the steady state, and S6 will have cleared zero pages for the same reason. Skipping the scope also avoids a needless barrier.
    if (Atlas.Census.PageRenderCount == 0)
        return 0;

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Raster.RasterPipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Raster.RasterLayout,
                            0, 1, &CasterSet, 0, nullptr);

    VkDeviceSize VertexOffset = 0;
    vkCmdBindVertexBuffers(CommandBuffer, 0, 1, &Mesh.VertexBuffer, &VertexOffset);
    vkCmdBindIndexBuffer(CommandBuffer, Mesh.IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    const uint32_t LevelCount = (uint32_t)Clipmap.Levels.size();
    for (uint32_t Level = 0; Level < LevelCount; ++Level)
    {
        const SunShadowLevel& LevelState = Clipmap.Levels[Level];

        // ⚠️ An unseeded level has never been centred, so its origin is meaningless and its tiles address a lattice nothing has allocated against.
        if (!LevelState.OriginSeeded)
            continue;

        ShadowDepthRasterConstants Constants;
        Constants.LightRightAxis[0]   = Clipmap.Basis.RightAxis.XCoord;
        Constants.LightRightAxis[1]   = Clipmap.Basis.RightAxis.YCoord;
        Constants.LightRightAxis[2]   = Clipmap.Basis.RightAxis.ZCoord;
        Constants.LightUpAxis[0]      = Clipmap.Basis.UpAxis.XCoord;
        Constants.LightUpAxis[1]      = Clipmap.Basis.UpAxis.YCoord;
        Constants.LightUpAxis[2]      = Clipmap.Basis.UpAxis.ZCoord;
        Constants.LightForwardAxis[0] = Clipmap.Basis.ForwardAxis.XCoord;
        Constants.LightForwardAxis[1] = Clipmap.Basis.ForwardAxis.YCoord;
        Constants.LightForwardAxis[2] = Clipmap.Basis.ForwardAxis.ZCoord;

        // 📝 The window's centre light tile. ToroidalOrigin is the NEGATED window corner (ADD form), so the corner is -Origin and the centre is
        //    -Origin + Resolution/2. Getting this wrong offsets every caster by half a window, which reads as shadows detached from their objects.
        const float HalfResolution = 0.5f * (float)LevelState.Resolution;
        Constants.WindowCentreTile[0] = (float)(-LevelState.ToroidalOrigin.XTile) + HalfResolution;
        Constants.WindowCentreTile[1] = (float)(-LevelState.ToroidalOrigin.YTile) + HalfResolution;

        Constants.ToroidalOrigin[0] = LevelState.ToroidalOrigin.XTile;
        Constants.ToroidalOrigin[1] = LevelState.ToroidalOrigin.YTile;
        Constants.BaseTileMetres    = Clipmap.Levels[0].TileMetres;
        Constants.Level             = Level;

        vkCmdPushConstants(CommandBuffer, Raster.RasterLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(Constants), &Constants);

        // 🔴 THE VIEWPORT IS SIZED IN TEXELS, NOT IN TILES, AND THE DIFFERENCE IS A FACTOR OF 16384 IN COVERAGE. The rasterizer emits exactly one
        //    fragment per VIEWPORT PIXEL, and the fragment stage writes exactly one atlas texel per invocation — a fragment shader cannot subdivide
        //    its own fragment. Sizing this window at LevelState.Resolution (32, the window edge in TILES, as the code did until 2026-07-30) therefore
        //    produced 32x32 = 1024 fragments per level and filled ONE TEXEL PER TILE, leaving 16383 of every page's 128² texels at the atomic-min
        //    identity. On screen that is isolated dark specks scattered where a caster happened to cover a tile's single sampled texel, with no
        //    coherent shadow anywhere — and CRITICALLY, every census counter stays healthy while it happens: S5 allocates, S6 clears, S7 draws and
        //    reports its levels, MarkShadowPageRendered clears ContentStale. Nothing in the tally can see that the pages came out empty.
        // 📝 The vertex stage needs no matching change: it maps the window onto NDC [-1,1] via (LateralTile - WindowCentreTile) * 2/32, which is
        //    normalized and so independent of the pixel count. Widening the viewport just scan-converts that same NDC span into more fragments —
        //    ShadowPageResolution per tile edge, i.e. one fragment per page texel, which is the density the fragment stage's WithinTile * 128 texel
        //    resolve has always assumed. That resolve reads InLightPosition and never gl_FragCoord, so it is correct at any viewport size.
        // ⚠️ ShadowPageResolution is the same constant the fragment stage divides a tile by (pushed as Constants.PageResolution). These two must move
        //    together: a viewport denser than the page resolution wastes fragments colliding on one texel, sparser leaves holes.
        // ⚠️ Derived from the LEVEL's own Resolution rather than the ShadowDepthWindowTexelEdge constant, because a level is allowed to carry a
        //    non-default tilemap resolution (AllocateSunShadowClipmap takes it as a parameter). The static_assert above covers the default; this
        //    clamp covers a level that was configured larger, and clamping is the safe direction — it under-fills pages rather than handing the
        //    driver an invalid viewport.
        const uint32_t WindowTexelEdge = (LevelState.Resolution * ShadowPageResolution > 4096u)
                                       ? 4096u
                                       : LevelState.Resolution * ShadowPageResolution;

        // 📝 A zero-attachment scope: the render area exists only to define the region the raster clips and scan-converts against.
        VkRenderingInfoKHR Scope = { VK_STRUCTURE_TYPE_RENDERING_INFO_KHR };
        Scope.renderArea.extent    = { WindowTexelEdge, WindowTexelEdge };
        Scope.layerCount           = 1;
        Scope.colorAttachmentCount = 0;
        Scope.pColorAttachments    = nullptr;
        Scope.pDepthAttachment     = nullptr;
        Scope.pStencilAttachment   = nullptr;

        Raster.Host->CmdBeginRendering(CommandBuffer, &Scope);

        VkViewport ViewportRegion = {};
        ViewportRegion.width    = (float)WindowTexelEdge;
        ViewportRegion.height   = (float)WindowTexelEdge;
        ViewportRegion.minDepth = 0.0f;
        ViewportRegion.maxDepth = 1.0f;
        vkCmdSetViewport(CommandBuffer, 0, 1, &ViewportRegion);

        VkRect2D Scissor = {};
        Scissor.extent = { WindowTexelEdge, WindowTexelEdge };
        vkCmdSetScissor(CommandBuffer, 0, 1, &Scissor);

        vkCmdDrawIndexed(CommandBuffer, Mesh.IndexCount, InstanceCount, 0, 0, 0);

        Raster.Host->CmdEndRendering(CommandBuffer);
        ++Raster.LastDrawnLevels;
    }

    // 🔴 Barrier the atomics against the tracer's reads AND against the NEXT image's S6 clear. The second direction is the one that is easy to miss:
    //    without it, next image's clear compute can begin writing the identity into a page while this image's fragment atomics are still landing, so a
    //    freshly cleared page silently reacquires last image's depth. Both hazards are inside VK_IMAGE_LAYOUT_GENERAL, so no layout transition covers
    //    either — see the matching note in ShadowPageClearSubmission.
    //
    // 📝 With several caster meshes this records once per mesh, which is redundant but NOT wrong, and it must not be "optimized" into a single barrier
    //    the caller records after the last mesh. Two S7 draws contend for the same texels, and while imageAtomicMin needs no ordering BETWEEN them (min
    //    is commutative, which is exactly why the meshes can be recorded in any order), the barrier's real job is the boundary with the compute passes
    //    on either side. A per-mesh barrier is a few microseconds; hoisting it out and getting the boundary wrong is a race that only shows on one driver.
    VkImageSubresourceRange AtlasRange = {};
    AtlasRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    AtlasRange.levelCount = 1;
    AtlasRange.layerCount = 1;

    VkImageMemoryBarrier AtlasBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    AtlasBarrier.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    AtlasBarrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    AtlasBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    AtlasBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    AtlasBarrier.image               = Atlas.AtlasImage;
    AtlasBarrier.subresourceRange    = AtlasRange;
    AtlasBarrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    AtlasBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &AtlasBarrier);

    return Raster.LastDrawnLevels;
}

void FinalizeShadowDepthRasterSubmission(ShadowDepthRasterSubmission& Raster)
{
    if (Raster.Host == nullptr || Raster.Host->Device == VK_NULL_HANDLE)
    {
        Raster = ShadowDepthRasterSubmission{};
        return;
    }

    VkDevice                     Device    = Raster.Host->Device;
    const VkAllocationCallbacks* Allocator = Raster.Host->Allocator;

    if (Raster.RasterPipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, Raster.RasterPipeline, Allocator);
    if (Raster.RasterLayout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Raster.RasterLayout, Allocator);
    // 📝 The set is freed with the pool; no explicit vkFreeDescriptorSets.
    if (Raster.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, Raster.DescriptorPool, Allocator);
    if (Raster.SetLayout      != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Raster.SetLayout, Allocator);

    Raster = ShadowDepthRasterSubmission{};
}

} // namespace Frontier
