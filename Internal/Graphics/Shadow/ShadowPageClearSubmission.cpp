/*==============================================================================================================================================
                                                    SHADOWPAGECLEARSUBMISSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of S6. Initialize builds a two-binding set layout (storage image 0 = the atlas, storage buffer 1 = the render list), one pipeline
//    layout with the push range, the compute pipeline, and the device-local list buffer plus its host-visible staging source. Record walks the atlas's
//    page records to collect the pages ShadowPageNeedsRender selects, memcpys them into the staging mapping, copies to the device buffer, and dispatches
//    one workgroup per 32² patch per listed page. Raw Vulkan, no VMA, mirroring the ShadowTileMarkingSubmission idioms.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Shadow/ShadowPageClearSubmission.h"

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

void ReportShadowPageClear(const char* MessageText)
{
    std::fprintf(stderr, "[ShadowPageClear] %s\n", MessageText);
}

// 📝 The two bindings ShadowPageClear.comp declares.
constexpr uint32_t AtlasImageBinding = 0;
constexpr uint32_t ClearListBinding  = 1;

// 🔴 A 128-texel page edge over a 32-texel workgroup edge is exactly 4 groups, and the shader has no partial-patch path beyond its bounds test. Derived
//    rather than written as 8 so a change to either constant cannot leave a page partially cleared — which would read as depth holes inside a shadow.
constexpr uint32_t ClearGroupsPerPageEdge = (ShadowPageResolution + ShadowPoolWorkgroupEdge - 1) / ShadowPoolWorkgroupEdge;

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

// One buffer + its backing allocation, with the caller choosing the usage and memory properties. Returns false with both handles null on any failure so
// the cleanup path stays uniform.
bool ProvisionBuffer(VulkanHost&           Host,
                     VkDeviceSize          ByteSize,
                     VkBufferUsageFlags    Usage,
                     VkMemoryPropertyFlags Wanted,
                     VkBuffer&             OutBuffer,
                     VkDeviceMemory&       OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteSize;
    BufferInformation.usage       = Usage;
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

    uint32_t TypeIndex = UINT32_MAX;
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

bool ConstructClearSetLayout(ShadowPageClearSubmission& Clear)
{
    VulkanHost& Host = *Clear.Host;

    VkDescriptorSetLayoutBinding Bindings[2] = {};

    Bindings[0].binding         = AtlasImageBinding;
    Bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    Bindings[0].descriptorCount = 1;
    Bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    Bindings[1].binding         = ClearListBinding;
    Bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Bindings[1].descriptorCount = 1;
    Bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutInformation.bindingCount = 2;
    LayoutInformation.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &LayoutInformation, Host.Allocator, &Clear.SetLayout) != VK_SUCCESS)
    {
        Clear.SetLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Allocate the pool + one set, pointing binding 0 at the atlas storage view and binding 1 at the owned list buffer. Both are known at init, so unlike the
// marking chain there is no refresh path here.
bool ConstructClearDescriptors(ShadowPageClearSubmission& Clear, const ShadowPageAtlas& Atlas)
{
    VulkanHost& Host = *Clear.Host;

    VkDescriptorPoolSize PoolSizes[2] = {};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    PoolSizes[0].descriptorCount = 1;
    PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 1;
    PoolInformation.poolSizeCount = 2;
    PoolInformation.pPoolSizes    = PoolSizes;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Clear.DescriptorPool) != VK_SUCCESS)
    {
        Clear.DescriptorPool = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocation.descriptorPool     = Clear.DescriptorPool;
    SetAllocation.descriptorSetCount = 1;
    SetAllocation.pSetLayouts        = &Clear.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocation, &Clear.ClearSet) != VK_SUCCESS)
    {
        Clear.ClearSet = VK_NULL_HANDLE;
        return false;
    }

    // ⚠️ GENERAL, not SHADER_READ_ONLY_OPTIMAL — this descriptor writes the image, and the layout declared here must match the layout the atlas is
    //    actually in when the dispatch runs. TransitionShadowPageAtlas is what guarantees that; this field only states the expectation.
    VkDescriptorImageInfo AtlasInformation = {};
    AtlasInformation.imageView   = Atlas.AtlasStorageView;
    AtlasInformation.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo ListInformation = { Clear.ListBuffer, 0, VK_WHOLE_SIZE };

    VkWriteDescriptorSet Writes[2] = {};
    Writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[0].dstSet          = Clear.ClearSet;
    Writes[0].dstBinding      = AtlasImageBinding;
    Writes[0].descriptorCount = 1;
    Writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    Writes[0].pImageInfo      = &AtlasInformation;

    Writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[1].dstSet          = Clear.ClearSet;
    Writes[1].dstBinding      = ClearListBinding;
    Writes[1].descriptorCount = 1;
    Writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    Writes[1].pBufferInfo     = &ListInformation;

    vkUpdateDescriptorSets(Host.Device, 2, Writes, 0, nullptr);
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeShadowPageClearSubmission(ShadowPageClearSubmission& Clear,
                                         VulkanHost&                Host,
                                         const ShadowPageAtlas&     Atlas,
                                         const char*                ShaderDirectory)
{
    Clear      = ShadowPageClearSubmission{};
    Clear.Host = &Host;

    if (Host.Device == VK_NULL_HANDLE)
    {
        ReportShadowPageClear("no device — clear pass not built");
        return false;
    }
    if (!Atlas.ReadyCondition || Atlas.AtlasStorageView == VK_NULL_HANDLE)
    {
        ReportShadowPageClear("page atlas not ready — clear pass not built");
        return false;
    }

    const VkDeviceSize ListBytes = sizeof(uint32_t) * ShadowPageCapacity;

    if (!ProvisionBuffer(Host, ListBytes,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         Clear.ListBuffer, Clear.ListMemory))
    {
        ReportShadowPageClear("render-list buffer allocation failed");
        FinalizeShadowPageClearSubmission(Clear);
        return false;
    }
    if (!ProvisionBuffer(Host, ListBytes,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         Clear.ListStaging, Clear.ListStagingMemory))
    {
        ReportShadowPageClear("render-list staging buffer allocation failed");
        FinalizeShadowPageClearSubmission(Clear);
        return false;
    }
    if (vkMapMemory(Host.Device, Clear.ListStagingMemory, 0, VK_WHOLE_SIZE, 0, &Clear.ListStagingMapping) != VK_SUCCESS)
    {
        Clear.ListStagingMapping = nullptr;
        ReportShadowPageClear("render-list staging mapping failed");
        FinalizeShadowPageClearSubmission(Clear);
        return false;
    }
    std::memset(Clear.ListStagingMapping, 0, static_cast<size_t>(ListBytes));

    if (!ConstructClearSetLayout(Clear))
    {
        ReportShadowPageClear("descriptor set layout creation failed");
        FinalizeShadowPageClearSubmission(Clear);
        return false;
    }
    if (!ConstructClearDescriptors(Clear, Atlas))
    {
        ReportShadowPageClear("descriptor pool / set creation failed");
        FinalizeShadowPageClearSubmission(Clear);
        return false;
    }

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(ShadowPageClearConstants);

    VkPipelineLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    LayoutInformation.setLayoutCount         = 1;
    LayoutInformation.pSetLayouts            = &Clear.SetLayout;
    LayoutInformation.pushConstantRangeCount = 1;
    LayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &LayoutInformation, Host.Allocator, &Clear.ClearLayout) != VK_SUCCESS)
    {
        Clear.ClearLayout = VK_NULL_HANDLE;
        ReportShadowPageClear("pipeline layout creation failed");
        FinalizeShadowPageClearSubmission(Clear);
        return false;
    }

    const std::string Directory = (ShaderDirectory != nullptr) ? ShaderDirectory : "Shaders";
    VkShaderModule    Module    = ConstructShaderModule(Host, RetrieveShaderBytes(Directory + "/ShadowPageClear.comp.spv"));
    if (Module == VK_NULL_HANDLE)
    {
        ReportShadowPageClear("ShadowPageClear.comp.spv unavailable — clear pass not built");
        FinalizeShadowPageClearSubmission(Clear);
        return false;
    }

    VkPipelineShaderStageCreateInfo StageInformation = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    StageInformation.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    StageInformation.module = Module;
    StageInformation.pName  = "main";

    // 📝 No VK_PIPELINE_CREATE_DISPATCH_BASE_BIT: unlike S3, this pass dispatches from the origin — z indexes the render list densely, so there is no
    //    base group to offset by.
    VkComputePipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    PipelineInformation.stage  = StageInformation;
    PipelineInformation.layout = Clear.ClearLayout;

    const VkResult Outcome = vkCreateComputePipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInformation, Host.Allocator, &Clear.ClearPipeline);
    vkDestroyShaderModule(Host.Device, Module, Host.Allocator);
    if (Outcome != VK_SUCCESS)
    {
        Clear.ClearPipeline = VK_NULL_HANDLE;
        ReportShadowPageClear("compute pipeline creation failed");
        FinalizeShadowPageClearSubmission(Clear);
        return false;
    }

    Clear.ReadyCondition = true;
    return true;
}

uint32_t RecordShadowPageClear(ShadowPageClearSubmission& Clear, const ShadowPageAtlas& Atlas, VkCommandBuffer CommandBuffer)
{
    Clear.LastClearedCount = 0;

    if (!Clear.ReadyCondition || Clear.ClearPipeline == VK_NULL_HANDLE || CommandBuffer == VK_NULL_HANDLE)
        return 0;
    if (!Atlas.ReadyCondition)
        return 0;

    // Collect the pages that are BOTH wanted this image and holding wrong depth. 📝 Written straight into the staging mapping — the list is at most
    //    ShadowPageCapacity uints (1 KiB), so there is no intermediate vector to size or reuse.
    uint32_t* ListEntries = static_cast<uint32_t*>(Clear.ListStagingMapping);
    if (ListEntries == nullptr)
        return 0;

    uint32_t       PageCount = 0;
    const uint32_t PageTotal = static_cast<uint32_t>(Atlas.PageRecords.size());
    for (uint32_t Page = 0; Page < PageTotal && PageCount < ShadowPageCapacity; ++Page)
    {
        if (ShadowPageNeedsRender(Atlas, Page))
            ListEntries[PageCount++] = Page;
    }

    // 🔴 ZERO IS THE EXPECTED STEADY STATE, NOT AN ERROR. Once every wanted page has been rasterized once, a static scene selects nothing and this pass
    //    disappears entirely. Recording a dispatch of z = 0 is legal but pointless, and skipping it keeps the barrier below off the timeline too.
    if (PageCount == 0)
        return 0;

    const VkDeviceSize CopyBytes = sizeof(uint32_t) * PageCount;

    VkBufferCopy CopyRegion = {};
    CopyRegion.size = CopyBytes;
    vkCmdCopyBuffer(CommandBuffer, Clear.ListStaging, Clear.ListBuffer, 1, &CopyRegion);

    // The list must be visible to the dispatch that indexes it.
    VkBufferMemoryBarrier ListBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    ListBarrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    ListBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    ListBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ListBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ListBarrier.buffer              = Clear.ListBuffer;
    ListBarrier.offset              = 0;
    ListBarrier.size                = CopyBytes;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 1, &ListBarrier, 0, nullptr);

    ShadowPageClearConstants Constants;
    Constants.PageCount = PageCount;

    vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Clear.ClearPipeline);
    vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Clear.ClearLayout, 0, 1, &Clear.ClearSet, 0, nullptr);
    vkCmdPushConstants(CommandBuffer, Clear.ClearLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ShadowPageClearConstants), &Constants);
    vkCmdDispatch(CommandBuffer, ClearGroupsPerPageEdge, ClearGroupsPerPageEdge, PageCount);

    // 🔴 The clear must complete before S7's imageAtomicMin touches the same texels, and the hazard is WRITE-after-WRITE within one image — the kind no
    //    layout transition covers, because the layout does not change. Without this barrier the clear can land AFTER a caster's depth and overwrite it
    //    with the identity, which reads as casters flickering in and out rather than as a missing barrier.
    VkImageMemoryBarrier AtlasBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    AtlasBarrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    AtlasBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    AtlasBarrier.oldLayout           = VK_IMAGE_LAYOUT_GENERAL;
    AtlasBarrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    AtlasBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    AtlasBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    AtlasBarrier.image               = Atlas.AtlasImage;
    AtlasBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &AtlasBarrier);

    Clear.LastClearedCount = PageCount;
    return PageCount;
}

void FinalizeShadowPageClearSubmission(ShadowPageClearSubmission& Clear)
{
    if (Clear.Host == nullptr || Clear.Host->Device == VK_NULL_HANDLE)
    {
        Clear = ShadowPageClearSubmission{};
        return;
    }

    VulkanHost& Host = *Clear.Host;

    if (Clear.ClearPipeline != VK_NULL_HANDLE)  vkDestroyPipeline(Host.Device, Clear.ClearPipeline, Host.Allocator);
    if (Clear.ClearLayout != VK_NULL_HANDLE)    vkDestroyPipelineLayout(Host.Device, Clear.ClearLayout, Host.Allocator);
    if (Clear.DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Host.Device, Clear.DescriptorPool, Host.Allocator);
    if (Clear.SetLayout != VK_NULL_HANDLE)      vkDestroyDescriptorSetLayout(Host.Device, Clear.SetLayout, Host.Allocator);

    if (Clear.ListStagingMapping != nullptr)     vkUnmapMemory(Host.Device, Clear.ListStagingMemory);
    if (Clear.ListStaging != VK_NULL_HANDLE)     vkDestroyBuffer(Host.Device, Clear.ListStaging, Host.Allocator);
    if (Clear.ListStagingMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Clear.ListStagingMemory, Host.Allocator);
    if (Clear.ListBuffer != VK_NULL_HANDLE)      vkDestroyBuffer(Host.Device, Clear.ListBuffer, Host.Allocator);
    if (Clear.ListMemory != VK_NULL_HANDLE)      vkFreeMemory(Host.Device, Clear.ListMemory, Host.Allocator);

    Clear = ShadowPageClearSubmission{};
}

} // namespace Frontier
