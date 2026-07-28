/*==============================================================================================================================================
                                                              VISIBILITYDEPTH.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the renderer-owned scene depth target. Raw Vulkan image + device-local allocation + depth view, mirroring the engine's
//    ParametricSketchViewTarget / BufferAllocation idiom (no VMA). The record path opens its OWN depth-only dynamic-rendering scope and clears
//    to the far plane, so a render-schedule step can drive it without the substrate knowing depth exists. A follow-on barrier hands the image to
//    a compute reduce (the HiZ pyramid) as a sampled source. At this phase the scope only clears; geometry recording lands with the raster path.

#include "Graphics/Visibility/VisibilityDepth.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

void ReportVisibilityDepth(const char* MessageText)
{
    std::fprintf(stderr, "[VisibilityDepth] %s\n", MessageText);
}

// 📝 First memory type allowed by the requirement bitmask carrying every required property bit — mirrors ParametricSketchViewTarget.
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

// Allocate the D32 image + device-local memory + depth view. On any failure every out handle is left null so the caller's ReadyCondition
// stays false. Usage is depth-attachment (a step writes it) plus sampled (the pyramid reads it).
bool ConstructDepthResources(VulkanHost&     Host,
                             uint32_t        Width,
                             uint32_t        Height,
                             VkImage&        OutImage,
                             VkDeviceMemory& OutMemory,
                             VkImageView&    OutView)
{
    OutImage  = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;
    OutView   = VK_NULL_HANDLE;

    VkImageCreateInfo ImageInformation = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ImageInformation.imageType     = VK_IMAGE_TYPE_2D;
    ImageInformation.format        = VisibilityDepthFormat;
    ImageInformation.extent        = { Width, Height, 1 };
    ImageInformation.mipLevels     = 1;
    ImageInformation.arrayLayers   = 1;
    ImageInformation.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageInformation.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageInformation.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ImageInformation.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageInformation.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(Host.Device, &ImageInformation, Host.Allocator, &OutImage) != VK_SUCCESS)
    {
        OutImage = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements MemoryRequirements = {};
    vkGetImageMemoryRequirements(Host.Device, OutImage, &MemoryRequirements);

    bool MemoryTypeFound = false;
    const uint32_t MemoryTypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice,
                                                           MemoryRequirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                           MemoryTypeFound);
    if (!MemoryTypeFound)
    {
        vkDestroyImage(Host.Device, OutImage, Host.Allocator);
        OutImage = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = MemoryTypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &OutMemory) != VK_SUCCESS ||
        vkBindImageMemory(Host.Device, OutImage, OutMemory, 0) != VK_SUCCESS)
    {
        if (OutMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
        vkDestroyImage(Host.Device, OutImage, Host.Allocator);
        OutImage  = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        return false;
    }

    VkImageViewCreateInfo ViewInformation = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ViewInformation.image                       = OutImage;
    ViewInformation.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    ViewInformation.format                      = VisibilityDepthFormat;
    ViewInformation.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    ViewInformation.subresourceRange.levelCount = 1;
    ViewInformation.subresourceRange.layerCount = 1;
    if (vkCreateImageView(Host.Device, &ViewInformation, Host.Allocator, &OutView) != VK_SUCCESS)
    {
        vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
        vkDestroyImage(Host.Device, OutImage, Host.Allocator);
        OutImage  = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        OutView   = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Release the view / image / memory of a target and null the handles. Safe on any partial set.
void ReleaseDepthResources(VisibilityDepth& Target)
{
    if (Target.Host == nullptr || Target.Host->Device == VK_NULL_HANDLE)
        return;
    if (Target.DepthView   != VK_NULL_HANDLE) vkDestroyImageView(Target.Host->Device, Target.DepthView, Target.Host->Allocator);
    if (Target.DepthImage  != VK_NULL_HANDLE) vkDestroyImage(Target.Host->Device, Target.DepthImage, Target.Host->Allocator);
    if (Target.DepthMemory != VK_NULL_HANDLE) vkFreeMemory(Target.Host->Device, Target.DepthMemory, Target.Host->Allocator);
    Target.DepthView   = VK_NULL_HANDLE;
    Target.DepthImage  = VK_NULL_HANDLE;
    Target.DepthMemory = VK_NULL_HANDLE;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeVisibilityDepth(VisibilityDepth& Target, VulkanHost& Host, uint32_t Width, uint32_t Height)
{
    Target.Host           = &Host;
    Target.DepthImage     = VK_NULL_HANDLE;
    Target.DepthMemory    = VK_NULL_HANDLE;
    Target.DepthView      = VK_NULL_HANDLE;
    Target.Width          = 0;
    Target.Height         = 0;
    Target.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Target.ReadyCondition = false;

    if (Width == 0 || Height == 0)
    {
        ReportVisibilityDepth("zero extent — depth target not built");
        return false;
    }
    if (!ConstructDepthResources(Host, Width, Height, Target.DepthImage, Target.DepthMemory, Target.DepthView))
    {
        ReportVisibilityDepth("depth image / memory / view creation failed");
        return false;
    }

    Target.Width          = Width;
    Target.Height         = Height;
    Target.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Target.ReadyCondition = true;
    return true;
}

bool ReconfigureVisibilityDepth(VisibilityDepth& Target, uint32_t Width, uint32_t Height)
{
    if (Width == 0 || Height == 0)
        return false;
    if (Target.ReadyCondition && Target.Width == Width && Target.Height == Height)
        return true;
    if (Target.Host == nullptr)
        return false;

    ReleaseDepthResources(Target);
    Target.ReadyCondition = false;
    Target.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!ConstructDepthResources(*Target.Host, Width, Height, Target.DepthImage, Target.DepthMemory, Target.DepthView))
    {
        Target.Width = Target.Height = 0;
        return false;
    }
    Target.Width          = Width;
    Target.Height         = Height;
    Target.ReadyCondition = true;
    return true;
}

void RecordVisibilityDepthClear(VisibilityDepth& Target, VkCommandBuffer CommandBuffer)
{
    if (!Target.ReadyCondition || Target.Host == nullptr)
        return;

    VkImageSubresourceRange DepthRange = {};
    DepthRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    DepthRange.levelCount = 1;
    DepthRange.layerCount = 1;

    // Transition from whatever the image last held (UNDEFINED on the first frame, SHADER_READ_ONLY after a prior pyramid read) to the
    // depth-attachment layout the rendering scope needs. UNDEFINED source discards the old contents, which is correct — the scope clears.
    VkImageMemoryBarrier ToAttachment = {};
    ToAttachment.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToAttachment.oldLayout           = Target.CurrentLayout;
    ToAttachment.newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    ToAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToAttachment.image               = Target.DepthImage;
    ToAttachment.subresourceRange    = DepthRange;
    ToAttachment.srcAccessMask       = 0;
    ToAttachment.dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToAttachment);

    // Open a depth-only dynamic-rendering scope and clear to the far plane. Self-contained: no colour attachment, its own begin/end, so it
    // composes inside a render-schedule step independent of the substrate's colour scope. Geometry recording lands here in the raster phase.
    VkRenderingAttachmentInfoKHR DepthAttachment = {};
    DepthAttachment.sType                       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    DepthAttachment.imageView                   = Target.DepthView;
    DepthAttachment.imageLayout                 = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    DepthAttachment.loadOp                      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    DepthAttachment.storeOp                     = VK_ATTACHMENT_STORE_OP_STORE;
    DepthAttachment.clearValue.depthStencil     = { 1.0f, 0 };

    VkRenderingInfoKHR RenderingInformation = {};
    RenderingInformation.sType              = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    RenderingInformation.renderArea.extent  = { Target.Width, Target.Height };
    RenderingInformation.layerCount         = 1;
    RenderingInformation.pDepthAttachment   = &DepthAttachment;

    Target.Host->CmdBeginRendering(CommandBuffer, &RenderingInformation);
    Target.Host->CmdEndRendering(CommandBuffer);

    Target.CurrentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
}

void TransitionVisibilityDepthForSampling(VisibilityDepth& Target, VkCommandBuffer CommandBuffer)
{
    if (!Target.ReadyCondition || Target.Host == nullptr)
        return;

    VkImageSubresourceRange DepthRange = {};
    DepthRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    DepthRange.levelCount = 1;
    DepthRange.layerCount = 1;

    VkImageMemoryBarrier ToSampled = {};
    ToSampled.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToSampled.oldLayout           = Target.CurrentLayout;
    ToSampled.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ToSampled.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToSampled.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToSampled.image               = Target.DepthImage;
    ToSampled.subresourceRange    = DepthRange;
    ToSampled.srcAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    ToSampled.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToSampled);

    Target.CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void FinalizeVisibilityDepth(VisibilityDepth& Target)
{
    ReleaseDepthResources(Target);
    Target.Width          = 0;
    Target.Height         = 0;
    Target.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Target.ReadyCondition = false;
    Target.Host           = nullptr;
}

} // namespace Frontier
