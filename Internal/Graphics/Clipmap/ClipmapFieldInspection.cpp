/*==============================================================================================================================================
                                                        CLIPMAPFIELDINSPECTION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the development-only clipmap visualization. Initialize builds a host-visible record buffer, a one-binding descriptor set,
//    a shared push layout, and two graphics pipelines against the swapchain colour format (line list for the wire-cube lattice, point list for
//    the probe markers) configured for dynamic rendering. Refresh walks the field once per frame and writes one ClipmapCellRecord per cell worth
//    drawing straight into the persistent mapping. Record binds each pipeline and issues one instanced draw. Finalize tears it all down.
// 📝 The whole translation unit is inside FRONTIER_DEVELOPMENT_PROFILE, so a shipping build compiles an empty file.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Clipmap/ClipmapFieldInspection.h"

#ifdef FRONTIER_DEVELOPMENT_PROFILE

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

// Read a whole SPIR-V file into a byte buffer. Empty on failure, which the caller treats as "skip the inspection".
std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
{
    std::vector<char> Bytes;
    FILE* Handle = std::fopen(FilePath.c_str(), "rb");
    if (Handle == nullptr)
    {
        ISSUE_CAUTION("clipmap-inspection", "shader module not found: %s", FilePath.c_str());
        return Bytes;
    }
    std::fseek(Handle, 0, SEEK_END);
    long Size = std::ftell(Handle);
    std::fseek(Handle, 0, SEEK_SET);
    if (Size > 0)
    {
        Bytes.resize((size_t)Size);
        size_t ReadCount = std::fread(Bytes.data(), 1, (size_t)Size, Handle);
        if (ReadCount != (size_t)Size)
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

    VkShaderModuleCreateInfo ModuleInfo = {};
    ModuleInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ModuleInfo.codeSize = Bytes.size();
    ModuleInfo.pCode    = reinterpret_cast<const uint32_t*>(Bytes.data());

    VkShaderModule Module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(Host.Device, &ModuleInfo, Host.Allocator, &Module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return Module;
}

// First memory type allowed by the requirement bitmask carrying every required property bit.
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

// Build one graphics pipeline over the shared layout. Topology and the point-size capability are the only differences between the lattice
// (line list) and probe (point list) variants, so one builder serves both.
VkPipeline ConstructInspectionPipeline(const VulkanHost&   Host,
                                       VkPipelineLayout    Layout,
                                       VkFormat            ColourFormat,
                                       VkShaderModule      VertexModule,
                                       VkShaderModule      FragmentModule,
                                       VkPrimitiveTopology Topology)
{
    VkPipelineShaderStageCreateInfo Stages[2] = {};
    Stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    Stages[0].module = VertexModule;
    Stages[0].pName  = "main";
    Stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    Stages[1].module = FragmentModule;
    Stages[1].pName  = "main";

    VkPipelineVertexInputStateCreateInfo VertexInput = {};
    VertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo InputAssembly = {};
    InputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    InputAssembly.topology = Topology;

    VkPipelineViewportStateCreateInfo ViewportState = {};
    ViewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    ViewportState.viewportCount = 1;
    ViewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo Rasterization = {};
    Rasterization.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    Rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    Rasterization.cullMode    = VK_CULL_MODE_NONE;
    Rasterization.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Rasterization.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample = {};
    Multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Straight alpha blend — the lattice composites as an overlay onto the already-shaded colour target.
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

    VkPipelineColorBlendStateCreateInfo ColourBlend = {};
    ColourBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ColourBlend.attachmentCount = 1;
    ColourBlend.pAttachments    = &BlendAttachment;

    VkDynamicState DynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo DynamicState = {};
    DynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynamicState.dynamicStateCount = 2;
    DynamicState.pDynamicStates    = DynamicStates;

    // 📝 The substrate's colour scope carries NO depth attachment, so no depth-stencil state is declared and the lattice composites as an
    //    X-ray overlay. That is deliberate: a depth-tested lattice would hide exactly the occluded cells worth inspecting.
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
    PipelineInfo.pViewportState      = &ViewportState;
    PipelineInfo.pRasterizationState = &Rasterization;
    PipelineInfo.pMultisampleState   = &Multisample;
    PipelineInfo.pColorBlendState    = &ColourBlend;
    PipelineInfo.pDynamicState       = &DynamicState;
    PipelineInfo.layout              = Layout;

    VkPipeline Pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInfo, Host.Allocator, &Pipeline) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return Pipeline;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeClipmapFieldInspection(ClipmapFieldInspection& Inspection,
                                      const VulkanHost&       Host,
                                      VkFormat                ColourFormat,
                                      const char*             ShaderDirectory)
{
    Inspection.ReadyCondition = false;
    Inspection.Host           = &Host;
    if (!Host.DynamicRenderingEnabled || Host.Device == VK_NULL_HANDLE)
        return false;

    // -- Record buffer: host-visible, persistently mapped, refilled every frame ------------------------------------------
    const VkDeviceSize BufferBytes = (VkDeviceSize)sizeof(ClipmapCellRecord) * ClipmapInspectionCellCapacity;

    VkBufferCreateInfo BufferInfo = {};
    BufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    BufferInfo.size        = BufferBytes;
    BufferInfo.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Host.Device, &BufferInfo, Host.Allocator, &Inspection.CellBuffer) != VK_SUCCESS)
    {
        ISSUE_FAULT("clipmap-inspection", "cell buffer creation failed");
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Host.Device, Inspection.CellBuffer, &MemoryRequirements);

    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice,
                                                           MemoryRequirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                           MemoryTypeFound);
    if (!MemoryTypeFound)
    {
        ISSUE_FAULT("clipmap-inspection", "no host-visible memory type for the cell buffer");
        FinalizeClipmapFieldInspection(Inspection);
        return false;
    }

    VkMemoryAllocateInfo AllocateInfo = {};
    AllocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocateInfo.allocationSize  = MemoryRequirements.size;
    AllocateInfo.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInfo, Host.Allocator, &Inspection.CellMemory) != VK_SUCCESS ||
        vkBindBufferMemory(Host.Device, Inspection.CellBuffer, Inspection.CellMemory, 0) != VK_SUCCESS ||
        vkMapMemory(Host.Device, Inspection.CellMemory, 0, BufferBytes, 0, &Inspection.CellMapping) != VK_SUCCESS)
    {
        ISSUE_FAULT("clipmap-inspection", "cell buffer allocation / mapping failed");
        FinalizeClipmapFieldInspection(Inspection);
        return false;
    }
    std::memset(Inspection.CellMapping, 0, (size_t)BufferBytes);

    // -- Descriptor layout + pool + set: one storage-buffer binding -------------------------------------------------------
    VkDescriptorSetLayoutBinding CellBinding = {};
    CellBinding.binding         = 0;
    CellBinding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    CellBinding.descriptorCount = 1;
    CellBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo DescriptorLayoutInfo = {};
    DescriptorLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    DescriptorLayoutInfo.bindingCount = 1;
    DescriptorLayoutInfo.pBindings    = &CellBinding;
    if (vkCreateDescriptorSetLayout(Host.Device, &DescriptorLayoutInfo, Host.Allocator, &Inspection.DescriptorLayout) != VK_SUCCESS)
    {
        ISSUE_FAULT("clipmap-inspection", "descriptor layout creation failed");
        FinalizeClipmapFieldInspection(Inspection);
        return false;
    }

    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo PoolInfo = {};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.maxSets       = 1;
    PoolInfo.poolSizeCount = 1;
    PoolInfo.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInfo, Host.Allocator, &Inspection.DescriptorPool) != VK_SUCCESS)
    {
        ISSUE_FAULT("clipmap-inspection", "descriptor pool creation failed");
        FinalizeClipmapFieldInspection(Inspection);
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocateInfo = {};
    SetAllocateInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetAllocateInfo.descriptorPool     = Inspection.DescriptorPool;
    SetAllocateInfo.descriptorSetCount = 1;
    SetAllocateInfo.pSetLayouts        = &Inspection.DescriptorLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocateInfo, &Inspection.CellSet) != VK_SUCCESS)
    {
        ISSUE_FAULT("clipmap-inspection", "descriptor set allocation failed");
        FinalizeClipmapFieldInspection(Inspection);
        return false;
    }

    VkDescriptorBufferInfo BufferBinding = {};
    BufferBinding.buffer = Inspection.CellBuffer;
    BufferBinding.offset = 0;
    BufferBinding.range  = BufferBytes;

    VkWriteDescriptorSet CellWrite = {};
    CellWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    CellWrite.dstSet          = Inspection.CellSet;
    CellWrite.dstBinding      = 0;
    CellWrite.descriptorCount = 1;
    CellWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    CellWrite.pBufferInfo     = &BufferBinding;
    vkUpdateDescriptorSets(Host.Device, 1, &CellWrite, 0, nullptr);

    // -- Shared pipeline layout: the cell set + the constants push range ---------------------------------------------------
    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(ClipmapInspectionConstants);

    VkPipelineLayoutCreateInfo LayoutInfo = {};
    LayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    LayoutInfo.setLayoutCount         = 1;
    LayoutInfo.pSetLayouts            = &Inspection.DescriptorLayout;
    LayoutInfo.pushConstantRangeCount = 1;
    LayoutInfo.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &LayoutInfo, Host.Allocator, &Inspection.PipelineLayout) != VK_SUCCESS)
    {
        ISSUE_FAULT("clipmap-inspection", "pipeline layout creation failed");
        FinalizeClipmapFieldInspection(Inspection);
        return false;
    }

    // -- Shader modules + the two pipelines --------------------------------------------------------------------------------
    const std::string Directory = ShaderDirectory;
    VkShaderModule LatticeVertex   = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/ClipmapDebugLattice.vert.spv"));
    VkShaderModule LatticeFragment = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/ClipmapDebugLattice.frag.spv"));
    VkShaderModule ProbeVertex     = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/ClipmapDebugProbe.vert.spv"));
    VkShaderModule ProbeFragment   = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/ClipmapDebugProbe.frag.spv"));

    if (LatticeVertex != VK_NULL_HANDLE && LatticeFragment != VK_NULL_HANDLE)
    {
        Inspection.LatticePipeline = ConstructInspectionPipeline(Host, Inspection.PipelineLayout, ColourFormat,
                                                                 LatticeVertex, LatticeFragment, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    }
    if (ProbeVertex != VK_NULL_HANDLE && ProbeFragment != VK_NULL_HANDLE)
    {
        Inspection.ProbePipeline = ConstructInspectionPipeline(Host, Inspection.PipelineLayout, ColourFormat,
                                                               ProbeVertex, ProbeFragment, VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
    }

    if (LatticeVertex   != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, LatticeVertex,   Host.Allocator);
    if (LatticeFragment != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, LatticeFragment, Host.Allocator);
    if (ProbeVertex     != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, ProbeVertex,     Host.Allocator);
    if (ProbeFragment   != VK_NULL_HANDLE) vkDestroyShaderModule(Host.Device, ProbeFragment,   Host.Allocator);

    if (Inspection.LatticePipeline == VK_NULL_HANDLE)
    {
        ISSUE_CAUTION("clipmap-inspection", "lattice pipeline unavailable — clipmap visualization disabled");
        FinalizeClipmapFieldInspection(Inspection);
        return false;
    }

    Inspection.ReadyCondition = true;
    ISSUE_NOTICE("clipmap-inspection", "clipmap visualization ready (probe markers %s)",
                 Inspection.ProbePipeline != VK_NULL_HANDLE ? "on" : "unavailable");
    return true;
}

void RefreshClipmapInspection(ClipmapFieldInspection&            Inspection,
                              const ToroidalClipmapField&        Field,
                              Vector3f                           CameraPosition,
                              const std::vector<CellCoordinate>&  OccupiedCells,
                              uint32_t                           OccupiedLevel,
                              int32_t                            ShellRadiusCells,
                              int32_t                            OccupiedRadiusCells)
{
    Inspection.CellCount        = 0;
    Inspection.DroppedCellCount = 0;
    Inspection.RemoteCellCount  = 0;
    if (!Inspection.ReadyCondition || Inspection.CellMapping == nullptr)
        return;

    ClipmapCellRecord* Records = reinterpret_cast<ClipmapCellRecord*>(Inspection.CellMapping);
    uint32_t Cursor = 0;

    // 📝 The lattice is deliberately SPARSE so it does not flood the view: it draws only (a) the cells that hold scene geometry, anywhere in the
    //    field, and (b) a small grid right around the camera for spatial reference. Every other vacant/resident cell is skipped entirely — an
    //    empty cell far from both the camera and the geometry carries no information worth a wire-cube. See ImportantNotes 2026-07-29.
    const uint32_t OccupancyLevel = std::min(OccupiedLevel, Field.LevelCount - 1u);

    // -- (a) Occupied cells: one record each, on the occupancy level, WITHIN a radius of the camera cell -----------------
    // 📝 The radius gate is what keeps this cost bounded. Without it the count scales with how much surface the scene has, not with how much is
    //    worth looking at: a ground slab voxelizes to well over a thousand cells, and since the lattice spends 72 vertices per cell and composites
    //    with no depth attachment (every cage overdraws whatever is in front of it), a wide floor alone dominates the frame. A cell far from the
    //    camera is also the one you are least able to read. Cells outside the radius are counted in RemoteCellCount, NOT DroppedCellCount — they
    //    were withheld by this policy, whereas a drop means the capacity cap was hit and the overlay is lying about coverage.
    {
        const ClipmapLevel&  Window     = Field.Levels[OccupancyLevel];
        const CellCoordinate CameraCell = ResolveCameraCell(Field, OccupancyLevel, CameraPosition);

        for (const CellCoordinate& Occupied : OccupiedCells)
        {
            // Chebyshev distance, so the kept region is a cube of cells matching the lattice's own shape (not a sphere cutting corners off it).
            const int32_t SpanX = std::abs(Occupied.XCell - CameraCell.XCell);
            const int32_t SpanY = std::abs(Occupied.YCell - CameraCell.YCell);
            const int32_t SpanZ = std::abs(Occupied.ZCell - CameraCell.ZCell);
            if (std::max({ SpanX, SpanY, SpanZ }) > OccupiedRadiusCells)
            {
                ++Inspection.RemoteCellCount;
                continue;
            }

            if (Cursor >= ClipmapInspectionCellCapacity) { ++Inspection.DroppedCellCount; continue; }

            const CellCoordinate Physical  = ResolvePhysicalCell(Field, OccupancyLevel, Occupied);
            const uint32_t       FlatIndex = FlattenPhysicalCell(Field, OccupancyLevel, Physical);

            ClipmapCellRecord& Record = Records[Cursor];
            Record.CentreMetres[0] = ((float)Occupied.XCell + 0.5f) * Window.CellMetres;
            Record.CentreMetres[1] = ((float)Occupied.YCell + 0.5f) * Window.CellMetres;
            Record.CentreMetres[2] = ((float)Occupied.ZCell + 0.5f) * Window.CellMetres;
            Record.EdgeMetres      = Window.CellMetres;
            Record.Category        = (uint32_t)ClipmapCellCategory::Occupied;
            Record.LevelIndex      = OccupancyLevel;
            Record.RelightRamp     = Window.RelightRamp[FlatIndex];
            Record.Reserved        = 0.0f;
            ++Cursor;
        }
    }

    // -- (b) Camera-region grid: a small shell on the FINE level (0) only, so one clean grid follows the camera instead of ---
    //        four concentric levels stacking into a moiré. Vacant/resident category still drives colour + the relight ramp.
    {
        const uint32_t       FineLevel  = 0;
        const ClipmapLevel&  Window     = Field.Levels[FineLevel];
        const CellCoordinate CameraCell = ResolveCameraCell(Field, FineLevel, CameraPosition);
        const int32_t        Radius     = std::min(ShellRadiusCells, (int32_t)(Window.Resolution / 2));

        for (int32_t ZOffset = -Radius; ZOffset <= Radius; ++ZOffset)
            for (int32_t YOffset = -Radius; YOffset <= Radius; ++YOffset)
                for (int32_t XOffset = -Radius; XOffset <= Radius; ++XOffset)
                {
                    if (Cursor >= ClipmapInspectionCellCapacity) { ++Inspection.DroppedCellCount; continue; }

                    const CellCoordinate WorldCell{ CameraCell.XCell + XOffset, CameraCell.YCell + YOffset, CameraCell.ZCell + ZOffset };
                    const CellCoordinate Physical  = ResolvePhysicalCell(Field, FineLevel, WorldCell);
                    const uint32_t       FlatIndex = FlattenPhysicalCell(Field, FineLevel, Physical);
                    const bool ResidentCondition = Window.ResidencyTable[FlatIndex] != 0;

                    ClipmapCellRecord& Record = Records[Cursor];
                    Record.CentreMetres[0] = ((float)WorldCell.XCell + 0.5f) * Window.CellMetres;
                    Record.CentreMetres[1] = ((float)WorldCell.YCell + 0.5f) * Window.CellMetres;
                    Record.CentreMetres[2] = ((float)WorldCell.ZCell + 0.5f) * Window.CellMetres;
                    Record.EdgeMetres      = Window.CellMetres;
                    Record.Category        = ResidentCondition ? (uint32_t)ClipmapCellCategory::Resident
                                                               : (uint32_t)ClipmapCellCategory::Vacant;
                    Record.LevelIndex      = FineLevel;
                    Record.RelightRamp     = Window.RelightRamp[FlatIndex];
                    Record.Reserved        = 0.0f;
                    ++Cursor;
                }
    }

    Inspection.CellCount = Cursor;
}

void RecordClipmapInspection(const ClipmapFieldInspection&     Inspection,
                             VkCommandBuffer                   CommandBuffer,
                             VkExtent2D                        Extent,
                             const ClipmapInspectionConstants& Constants)
{
    if (!Inspection.ReadyCondition || Inspection.CellCount == 0)
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

    // Refresh the viewport size in the push data so the lattice's screen-space quad expansion maps a pixel width back to an NDC offset for
    // this frame's extent (the caller's Constants carry the matrix + tuning; the viewport is the one field that follows the swapchain resize).
    ClipmapInspectionConstants PushData = Constants;
    PushData.ViewportWidth  = (float)Extent.width;
    PushData.ViewportHeight = (float)Extent.height;

    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Inspection.PipelineLayout,
                            0, 1, &Inspection.CellSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Inspection.PipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(ClipmapInspectionConstants), &PushData);

    // The wire-cube lattice: 12 edges, each a screen-space quad of 2 triangles == 72 triangle vertices per cell, one instance per cell.
    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Inspection.LatticePipeline);
    vkCmdDraw(CommandBuffer, 72, Inspection.CellCount, 0, 0);

    // The probe markers: one point per cell (vacant cells collapse to size 0 in the vertex step).
    if (Inspection.ProbePipeline != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Inspection.ProbePipeline);
        vkCmdDraw(CommandBuffer, 1, Inspection.CellCount, 0, 0);
    }
}

void FinalizeClipmapFieldInspection(ClipmapFieldInspection& Inspection)
{
    if (Inspection.Host == nullptr || Inspection.Host->Device == VK_NULL_HANDLE)
    {
        Inspection.ReadyCondition = false;
        return;
    }
    const VulkanHost& Host = *Inspection.Host;

    if (Inspection.LatticePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Host.Device, Inspection.LatticePipeline, Host.Allocator);
        Inspection.LatticePipeline = VK_NULL_HANDLE;
    }
    if (Inspection.ProbePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(Host.Device, Inspection.ProbePipeline, Host.Allocator);
        Inspection.ProbePipeline = VK_NULL_HANDLE;
    }
    if (Inspection.PipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(Host.Device, Inspection.PipelineLayout, Host.Allocator);
        Inspection.PipelineLayout = VK_NULL_HANDLE;
    }
    if (Inspection.DescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(Host.Device, Inspection.DescriptorPool, Host.Allocator);
        Inspection.DescriptorPool = VK_NULL_HANDLE;
        Inspection.CellSet        = VK_NULL_HANDLE;
    }
    if (Inspection.DescriptorLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(Host.Device, Inspection.DescriptorLayout, Host.Allocator);
        Inspection.DescriptorLayout = VK_NULL_HANDLE;
    }
    if (Inspection.CellMapping != nullptr)
    {
        vkUnmapMemory(Host.Device, Inspection.CellMemory);
        Inspection.CellMapping = nullptr;
    }
    if (Inspection.CellBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(Host.Device, Inspection.CellBuffer, Host.Allocator);
        Inspection.CellBuffer = VK_NULL_HANDLE;
    }
    if (Inspection.CellMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(Host.Device, Inspection.CellMemory, Host.Allocator);
        Inspection.CellMemory = VK_NULL_HANDLE;
    }

    Inspection.CellCount        = 0;
    Inspection.DroppedCellCount = 0;
    Inspection.ReadyCondition   = false;
}

} // namespace Frontier

#endif // FRONTIER_DEVELOPMENT_PROFILE
