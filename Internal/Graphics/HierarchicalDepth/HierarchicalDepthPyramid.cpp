/*==============================================================================================================================================
                                                        HIERARCHICALDEPTHPYRAMID.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the HiZ pyramid. One R32_SFLOAT mip-chain image (STORAGE | SAMPLED, device-local) built raw over Vulkan (no VMA), a
//    per-mip storage view (compute write) and per-mip sampled view (read as the next level's source), a nearest/clamp point sampler, and the
//    HierarchicalDepthReduce.comp pipeline driven once per mip. Descriptor sets are one-per-level: level 0's source is the external scene depth
//    (bound at record time from the VisibilityDepth), higher levels' sources are the pyramid's own sampled views (bound at build time). Each
//    dispatch is fenced from the next by a storage-write -> shader-read barrier so a level is complete before the level above reads it.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/HierarchicalDepth/HierarchicalDepthPyramid.h"

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

void ReportHierarchicalDepth(const char* MessageText)
{
    std::fprintf(stderr, "[HierarchicalDepth] %s\n", MessageText);
}

// 📝 First memory type allowed by the requirement bitmask carrying every required property bit — mirrors VisibilityDepth / ParametricSketchViewTarget.
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

// The mip-count of a full pyramid: floor(log2(max(W,H))) + 1, so the coarsest level is 1x1.
uint32_t ResolveLevelCount(uint32_t Width, uint32_t Height)
{
    uint32_t Largest = Width > Height ? Width : Height;
    uint32_t Levels  = 1;
    while (Largest > 1) { Largest >>= 1; ++Levels; }
    return Levels;
}

// The extent of a given mip (halved per level, floored, never below 1).
VkExtent2D ResolveLevelExtent(uint32_t BaseWidth, uint32_t BaseHeight, uint32_t Level)
{
    uint32_t LevelWidth  = BaseWidth  >> Level;
    uint32_t LevelHeight = BaseHeight >> Level;
    if (LevelWidth  == 0) LevelWidth  = 1;
    if (LevelHeight == 0) LevelHeight = 1;
    return { LevelWidth, LevelHeight };
}

// The reduce push data mirrors HierarchicalDepthReduce.comp's ReduceConstants.
struct ReduceConstants
{
    int32_t DestinationExtentX;
    int32_t DestinationExtentY;
    int32_t SourceExtentX;
    int32_t SourceExtentY;
};

std::vector<char> RetrieveShaderBytes(const std::string& FilePath)
{
    std::vector<char> Bytes;
    FILE* Handle = std::fopen(FilePath.c_str(), "rb");
    if (Handle == nullptr)
    {
        ReportHierarchicalDepth("reduce shader module not found");
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

VkShaderModule ConstructShaderModule(VulkanHost& Host, const std::vector<char>& Bytes)
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

// Build the size-independent objects: descriptor-set layout ({ sampler2D source, storage image destination }), pipeline layout (that layout +
// ReduceConstants push range), and the reduce compute pipeline. Point sampler too (nearest / clamp; the reduce fetches exact texels). Returns
// false with every handle left null on any failure.
bool ConstructReducePipeline(HierarchicalDepthPyramid& Pyramid, const char* ShaderDirectory)
{
    VulkanHost& Host = *Pyramid.Host;

    VkSamplerCreateInfo SamplerInformation = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    SamplerInformation.magFilter    = VK_FILTER_NEAREST;
    SamplerInformation.minFilter    = VK_FILTER_NEAREST;
    SamplerInformation.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    SamplerInformation.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerInformation.maxLod       = 0.0f;
    if (vkCreateSampler(Host.Device, &SamplerInformation, Host.Allocator, &Pyramid.PointSampler) != VK_SUCCESS)
    {
        Pyramid.PointSampler = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorSetLayoutBinding Bindings[2] = {};
    Bindings[0].binding         = 0;                                          // sampler2D source
    Bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Bindings[0].descriptorCount = 1;
    Bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    Bindings[1].binding         = 1;                                          // storage image destination
    Bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    Bindings[1].descriptorCount = 1;
    Bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = 2;
    LayoutInformation.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Pyramid.DescriptorLayout) != VK_SUCCESS)
    {
        Pyramid.DescriptorLayout = VK_NULL_HANDLE;
        return false;
    }

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(ReduceConstants);

    VkPipelineLayoutCreateInfo PipelineLayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    PipelineLayoutInformation.setLayoutCount         = 1;
    PipelineLayoutInformation.pSetLayouts            = &Pyramid.DescriptorLayout;
    PipelineLayoutInformation.pushConstantRangeCount = 1;
    PipelineLayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &PipelineLayoutInformation, Host.Allocator, &Pyramid.PipelineLayout) != VK_SUCCESS)
    {
        Pyramid.PipelineLayout = VK_NULL_HANDLE;
        return false;
    }

    const std::string Directory = ShaderDirectory;
    VkShaderModule ReduceModule = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/HierarchicalDepthReduce.comp.spv"));
    if (ReduceModule == VK_NULL_HANDLE)
    {
        ReportHierarchicalDepth("reduce shader module build failed");
        return false;
    }

    VkPipelineShaderStageCreateInfo StageInformation = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    StageInformation.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    StageInformation.module = ReduceModule;
    StageInformation.pName  = "main";

    VkComputePipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    PipelineInformation.stage  = StageInformation;
    PipelineInformation.layout = Pyramid.PipelineLayout;
    const VkResult PipelineResult = vkCreateComputePipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInformation, Host.Allocator, &Pyramid.ReducePipeline);
    vkDestroyShaderModule(Host.Device, ReduceModule, Host.Allocator);
    if (PipelineResult != VK_SUCCESS)
    {
        Pyramid.ReducePipeline = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Allocate the mip-chain image + device-local memory, then a storage view and a sampled view per level. On any failure everything built so far
// is torn down by the caller's release path. Usage is STORAGE (compute writes each mip) | SAMPLED (each mip read as the source of the one above).
bool ConstructPyramidResources(HierarchicalDepthPyramid& Pyramid, uint32_t Width, uint32_t Height)
{
    VulkanHost&    Host       = *Pyramid.Host;
    const uint32_t LevelCount = ResolveLevelCount(Width, Height);

    VkImageCreateInfo ImageInformation = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ImageInformation.imageType     = VK_IMAGE_TYPE_2D;
    ImageInformation.format        = HierarchicalDepthFormat;
    ImageInformation.extent        = { Width, Height, 1 };
    ImageInformation.mipLevels     = LevelCount;
    ImageInformation.arrayLayers   = 1;
    ImageInformation.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageInformation.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageInformation.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ImageInformation.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageInformation.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(Host.Device, &ImageInformation, Host.Allocator, &Pyramid.PyramidImage) != VK_SUCCESS)
    {
        Pyramid.PyramidImage = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetImageMemoryRequirements(Host.Device, Pyramid.PyramidImage, &MemoryRequirements);

    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice,
                                                           MemoryRequirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                           MemoryTypeFound);
    if (!MemoryTypeFound)
        return false;

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &Pyramid.PyramidMemory) != VK_SUCCESS ||
        vkBindImageMemory(Host.Device, Pyramid.PyramidImage, Pyramid.PyramidMemory, 0) != VK_SUCCESS)
    {
        return false;
    }

    Pyramid.StorageViews.resize(LevelCount, VK_NULL_HANDLE);
    Pyramid.SampledViews.resize(LevelCount, VK_NULL_HANDLE);
    for (uint32_t Level = 0; Level < LevelCount; ++Level)
    {
        VkImageViewCreateInfo ViewInformation = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        ViewInformation.image                           = Pyramid.PyramidImage;
        ViewInformation.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ViewInformation.format                          = HierarchicalDepthFormat;
        ViewInformation.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ViewInformation.subresourceRange.baseMipLevel   = Level;
        ViewInformation.subresourceRange.levelCount     = 1;
        ViewInformation.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(Host.Device, &ViewInformation, Host.Allocator, &Pyramid.StorageViews[Level]) != VK_SUCCESS)
            return false;
        if (vkCreateImageView(Host.Device, &ViewInformation, Host.Allocator, &Pyramid.SampledViews[Level]) != VK_SUCCESS)
            return false;
    }

    Pyramid.Width      = Width;
    Pyramid.Height     = Height;
    Pyramid.LevelCount = LevelCount;
    return true;
}

// Allocate the descriptor pool (one set per level) and one set per level, then pre-bind each level's STORAGE destination and — for levels >= 1 —
// its sampled source (the level below). Level 0's source is the external scene depth, bound at record time in ReduceHierarchicalDepthPyramid.
bool ConstructPyramidDescriptors(HierarchicalDepthPyramid& Pyramid)
{
    VulkanHost&    Host       = *Pyramid.Host;
    const uint32_t LevelCount = Pyramid.LevelCount;

    VkDescriptorPoolSize PoolSizes[2] = {};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSizes[0].descriptorCount = LevelCount;
    PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    PoolSizes[1].descriptorCount = LevelCount;

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = LevelCount;
    PoolInformation.poolSizeCount = 2;
    PoolInformation.pPoolSizes    = PoolSizes;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Pyramid.DescriptorPool) != VK_SUCCESS)
    {
        Pyramid.DescriptorPool = VK_NULL_HANDLE;
        return false;
    }

    std::vector<VkDescriptorSetLayout> Layouts(LevelCount, Pyramid.DescriptorLayout);
    VkDescriptorSetAllocateInfo SetAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocation.descriptorPool     = Pyramid.DescriptorPool;
    SetAllocation.descriptorSetCount = LevelCount;
    SetAllocation.pSetLayouts        = Layouts.data();
    Pyramid.LevelSets.resize(LevelCount, VK_NULL_HANDLE);
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, Pyramid.LevelSets.data()) != VK_SUCCESS)
        return false;

    // Pre-bind the storage destination for every level, and the sampled source for every level >= 1 (the pyramid's own level below). Level 0's
    // source binding is left for record time (it reads the external VisibilityDepth, whose view the pyramid does not own).
    for (uint32_t Level = 0; Level < LevelCount; ++Level)
    {
        VkDescriptorImageInfo DestinationInformation = {};
        DestinationInformation.imageView   = Pyramid.StorageViews[Level];
        DestinationInformation.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet DestinationWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        DestinationWrite.dstSet          = Pyramid.LevelSets[Level];
        DestinationWrite.dstBinding      = 1;
        DestinationWrite.descriptorCount = 1;
        DestinationWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        DestinationWrite.pImageInfo      = &DestinationInformation;

        if (Level == 0)
        {
            vkUpdateDescriptorSets(Host.Device, 1, &DestinationWrite, 0, nullptr);
            continue;
        }

        // The source is sampled while the whole chain sits in GENERAL during the reduce loop (the inter-level barriers stay GENERAL->GENERAL),
        // so the descriptor's declared layout must be GENERAL — not SHADER_READ_ONLY — to match the layout in force at sample time.
        VkDescriptorImageInfo SourceInformation = {};
        SourceInformation.sampler     = Pyramid.PointSampler;
        SourceInformation.imageView   = Pyramid.SampledViews[Level - 1];
        SourceInformation.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet SourceWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        SourceWrite.dstSet          = Pyramid.LevelSets[Level];
        SourceWrite.dstBinding      = 0;
        SourceWrite.descriptorCount = 1;
        SourceWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        SourceWrite.pImageInfo      = &SourceInformation;

        VkWriteDescriptorSet Writes[2] = { SourceWrite, DestinationWrite };
        vkUpdateDescriptorSets(Host.Device, 2, Writes, 0, nullptr);
    }
    return true;
}

// Release the size-dependent objects (descriptor pool + sets, per-mip views, image, memory) and null their handles. The size-independent
// pipeline / layout / sampler persist. Safe on any partial set.
void ReleaseSizedResources(HierarchicalDepthPyramid& Pyramid)
{
    if (Pyramid.Host == nullptr || Pyramid.Host->Device == VK_NULL_HANDLE)
        return;
    VkDevice Device = Pyramid.Host->Device;
    const VkAllocationCallbacks* Allocator = Pyramid.Host->Allocator;

    if (Pyramid.DescriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(Device, Pyramid.DescriptorPool, Allocator);   // frees its sets
    Pyramid.DescriptorPool = VK_NULL_HANDLE;
    Pyramid.LevelSets.clear();

    for (VkImageView View : Pyramid.StorageViews)
        if (View != VK_NULL_HANDLE) vkDestroyImageView(Device, View, Allocator);
    for (VkImageView View : Pyramid.SampledViews)
        if (View != VK_NULL_HANDLE) vkDestroyImageView(Device, View, Allocator);
    Pyramid.StorageViews.clear();
    Pyramid.SampledViews.clear();

    if (Pyramid.PyramidImage  != VK_NULL_HANDLE) vkDestroyImage(Device, Pyramid.PyramidImage, Allocator);
    if (Pyramid.PyramidMemory != VK_NULL_HANDLE) vkFreeMemory(Device, Pyramid.PyramidMemory, Allocator);
    Pyramid.PyramidImage  = VK_NULL_HANDLE;
    Pyramid.PyramidMemory = VK_NULL_HANDLE;
    Pyramid.Width         = 0;
    Pyramid.Height        = 0;
    Pyramid.LevelCount    = 0;
    Pyramid.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

// Build both the sized image/views and the descriptors over Width x Height. On failure everything is released and false is returned.
bool ConstructSizedResources(HierarchicalDepthPyramid& Pyramid, uint32_t Width, uint32_t Height)
{
    if (!ConstructPyramidResources(Pyramid, Width, Height) || !ConstructPyramidDescriptors(Pyramid))
    {
        ReleaseSizedResources(Pyramid);
        return false;
    }
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeHierarchicalDepthPyramid(HierarchicalDepthPyramid& Pyramid,
                                        VulkanHost&               Host,
                                        uint32_t                  Width,
                                        uint32_t                  Height,
                                        const char*               ShaderDirectory)
{
    Pyramid = HierarchicalDepthPyramid{};
    Pyramid.Host = &Host;

    if (Width == 0 || Height == 0)
    {
        ReportHierarchicalDepth("zero extent — pyramid not built");
        return false;
    }
    if (!ConstructReducePipeline(Pyramid, ShaderDirectory))
    {
        ReportHierarchicalDepth("reduce pipeline / layout / sampler build failed");
        FinalizeHierarchicalDepthPyramid(Pyramid);
        return false;
    }
    if (!ConstructSizedResources(Pyramid, Width, Height))
    {
        ReportHierarchicalDepth("pyramid image / views / descriptors build failed");
        FinalizeHierarchicalDepthPyramid(Pyramid);
        return false;
    }

    Pyramid.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Pyramid.ReadyCondition = true;
    return true;
}

bool ReconfigureHierarchicalDepthPyramid(HierarchicalDepthPyramid& Pyramid, uint32_t Width, uint32_t Height)
{
    if (Width == 0 || Height == 0)
        return false;
    if (Pyramid.ReadyCondition && Pyramid.Width == Width && Pyramid.Height == Height)
        return true;
    if (Pyramid.Host == nullptr || Pyramid.ReducePipeline == VK_NULL_HANDLE)
        return false;

    ReleaseSizedResources(Pyramid);
    Pyramid.ReadyCondition = false;

    if (!ConstructSizedResources(Pyramid, Width, Height))
        return false;

    Pyramid.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Pyramid.ReadyCondition = true;
    return true;
}

void ReduceHierarchicalDepthPyramid(HierarchicalDepthPyramid& Pyramid, const VisibilityDepth& Depth, VkCommandBuffer CommandBuffer)
{
    if (!Pyramid.ReadyCondition || Pyramid.Host == nullptr)
        return;
    if (!Depth.ReadyCondition || Depth.CurrentLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        return;

    VulkanHost& Host = *Pyramid.Host;

    // Bind level 0's source to the external scene depth now (its view lives in the VisibilityDepth, not the pyramid). Higher levels' sources were
    // pre-bound at build time. The write is safe here because the sets are not in flight — the reduce for this frame has not been submitted yet.
    VkDescriptorImageInfo DepthSource = {};
    DepthSource.sampler     = Pyramid.PointSampler;
    DepthSource.imageView   = Depth.DepthView;
    DepthSource.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet DepthSourceWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    DepthSourceWrite.dstSet          = Pyramid.LevelSets[0];
    DepthSourceWrite.dstBinding      = 0;
    DepthSourceWrite.descriptorCount = 1;
    DepthSourceWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    DepthSourceWrite.pImageInfo      = &DepthSource;
    vkUpdateDescriptorSets(Host.Device, 1, &DepthSourceWrite, 0, nullptr);

    // Move the whole mip chain to GENERAL for storage writes. UNDEFINED source discards the prior frame's contents (every texel is rewritten).
    VkImageSubresourceRange WholeChain = {};
    WholeChain.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    WholeChain.levelCount = Pyramid.LevelCount;
    WholeChain.layerCount = 1;

    VkImageMemoryBarrier ToGeneral = {};
    ToGeneral.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToGeneral.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    ToGeneral.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    ToGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToGeneral.image               = Pyramid.PyramidImage;
    ToGeneral.subresourceRange    = WholeChain;
    ToGeneral.srcAccessMask       = 0;
    ToGeneral.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToGeneral);

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pyramid.ReducePipeline);

    for (uint32_t Level = 0; Level < Pyramid.LevelCount; ++Level)
    {
        const VkExtent2D DestinationExtent = ResolveLevelExtent(Pyramid.Width, Pyramid.Height, Level);
        const VkExtent2D SourceExtent      = (Level == 0)
                                             ? VkExtent2D{ Depth.Width, Depth.Height }
                                             : ResolveLevelExtent(Pyramid.Width, Pyramid.Height, Level - 1);

        ReduceConstants Constants = {};
        Constants.DestinationExtentX = (int32_t)DestinationExtent.width;
        Constants.DestinationExtentY = (int32_t)DestinationExtent.height;
        Constants.SourceExtentX      = (int32_t)SourceExtent.width;
        Constants.SourceExtentY      = (int32_t)SourceExtent.height;
        vkCmdPushConstants(CommandBuffer, Pyramid.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants), &Constants);

        vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pyramid.PipelineLayout, 0, 1, &Pyramid.LevelSets[Level], 0, nullptr);

        const uint32_t GroupsX = (DestinationExtent.width  + HierarchicalDepthWorkgroupEdge - 1) / HierarchicalDepthWorkgroupEdge;
        const uint32_t GroupsY = (DestinationExtent.height + HierarchicalDepthWorkgroupEdge - 1) / HierarchicalDepthWorkgroupEdge;
        vkCmdDispatch(CommandBuffer, GroupsX, GroupsY, 1);

        // Fence this level's writes before the level above reads it as a sampled source. Not needed after the last level (nothing reads it here).
        if (Level + 1 < Pyramid.LevelCount)
        {
            VkImageMemoryBarrier WriteToRead = {};
            WriteToRead.sType                        = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            WriteToRead.oldLayout                    = VK_IMAGE_LAYOUT_GENERAL;
            WriteToRead.newLayout                    = VK_IMAGE_LAYOUT_GENERAL;
            WriteToRead.srcQueueFamilyIndex          = VK_QUEUE_FAMILY_IGNORED;
            WriteToRead.dstQueueFamilyIndex          = VK_QUEUE_FAMILY_IGNORED;
            WriteToRead.image                        = Pyramid.PyramidImage;
            WriteToRead.subresourceRange.aspectMask  = VK_IMAGE_ASPECT_COLOR_BIT;
            WriteToRead.subresourceRange.baseMipLevel = Level;
            WriteToRead.subresourceRange.levelCount  = 1;
            WriteToRead.subresourceRange.layerCount  = 1;
            WriteToRead.srcAccessMask                = VK_ACCESS_SHADER_WRITE_BIT;
            WriteToRead.dstAccessMask                = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(CommandBuffer,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &WriteToRead);
        }
    }

    // Hand the whole chain to shader-read for a downstream cull consumer (none yet; this leaves the layout the header advertises).
    VkImageMemoryBarrier ToSampled = {};
    ToSampled.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToSampled.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    ToSampled.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ToSampled.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToSampled.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToSampled.image               = Pyramid.PyramidImage;
    ToSampled.subresourceRange    = WholeChain;
    ToSampled.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    ToSampled.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToSampled);

    Pyramid.CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void FinalizeHierarchicalDepthPyramid(HierarchicalDepthPyramid& Pyramid)
{
    if (Pyramid.Host != nullptr && Pyramid.Host->Device != VK_NULL_HANDLE)
    {
        VkDevice Device = Pyramid.Host->Device;
        const VkAllocationCallbacks* Allocator = Pyramid.Host->Allocator;

        ReleaseSizedResources(Pyramid);

        if (Pyramid.ReducePipeline   != VK_NULL_HANDLE) vkDestroyPipeline(Device, Pyramid.ReducePipeline, Allocator);
        if (Pyramid.PipelineLayout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, Pyramid.PipelineLayout, Allocator);
        if (Pyramid.DescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, Pyramid.DescriptorLayout, Allocator);
        if (Pyramid.PointSampler     != VK_NULL_HANDLE) vkDestroySampler(Device, Pyramid.PointSampler, Allocator);
    }

    VulkanHost* PreservedHost = Pyramid.Host;
    Pyramid = HierarchicalDepthPyramid{};
    (void)PreservedHost;
}

} // namespace Frontier
