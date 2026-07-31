/*==============================================================================================================================================
                                                             RADIANCETARGET.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the renderer-owned linear HDR radiance target. Raw Vulkan image + device-local allocation + colour view, mirroring
//    VisibilityImage (no VMA). This file owns only the R16G16B16A16_SFLOAT target and the two barriers that hand it between writing and sampling;
//    the scene units open the scope elsewhere. Layout is tracked across frames so each frame transitions from the prior SHADER_READ_ONLY (the
//    previous frame's resolve) back to COLOR_ATTACHMENT.

#include "Graphics/Render/Radiance/RadianceTarget.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

void ReportRadianceTarget(const char* MessageText)
{
    std::fprintf(stderr, "[RadianceTarget] %s\n", MessageText);
}

// 📝 First memory type allowed by the requirement bitmask carrying every required property bit — mirrors VisibilityImage / VisibilityDepth.
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

// Allocate the half-float image + device-local memory + colour view. On any failure every out handle is left null so the caller's ReadyCondition
// stays false. Usage is colour-attachment (the scene units write it) plus sampled (the resolve reads it).
bool ConstructRadianceResources(VulkanHost&     Host,
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
    ImageInformation.format        = RadianceTargetFormat;
    ImageInformation.extent        = { Width, Height, 1 };
    ImageInformation.mipLevels     = 1;
    ImageInformation.arrayLayers   = 1;
    ImageInformation.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageInformation.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageInformation.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ImageInformation.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageInformation.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(Host.Device, &ImageInformation, nullptr, &OutImage) != VK_SUCCESS)
    {
        ReportRadianceTarget("vkCreateImage failed for the radiance target");
        return false;
    }

    VkMemoryRequirements MemoryRequirement = {};
    vkGetImageMemoryRequirements(Host.Device, OutImage, &MemoryRequirement);

    bool           TypeFound   = false;
    const uint32_t MemoryIndex = SelectMemoryTypeIndex(Host.PhysicalDevice, MemoryRequirement.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, TypeFound);
    if (!TypeFound)
    {
        ReportRadianceTarget("no device-local memory type for the radiance target");
        vkDestroyImage(Host.Device, OutImage, nullptr);
        OutImage = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirement.size;
    AllocateInformation.memoryTypeIndex = MemoryIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, nullptr, &OutMemory) != VK_SUCCESS)
    {
        ReportRadianceTarget("vkAllocateMemory failed for the radiance target");
        vkDestroyImage(Host.Device, OutImage, nullptr);
        OutImage = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindImageMemory(Host.Device, OutImage, OutMemory, 0) != VK_SUCCESS)
    {
        ReportRadianceTarget("vkBindImageMemory failed for the radiance target");
        vkFreeMemory(Host.Device, OutMemory, nullptr);
        vkDestroyImage(Host.Device, OutImage, nullptr);
        OutMemory = VK_NULL_HANDLE;
        OutImage  = VK_NULL_HANDLE;
        return false;
    }

    VkImageViewCreateInfo ViewInformation = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ViewInformation.image                       = OutImage;
    ViewInformation.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
    ViewInformation.format                      = RadianceTargetFormat;
    ViewInformation.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewInformation.subresourceRange.levelCount = 1;
    ViewInformation.subresourceRange.layerCount = 1;

    if (vkCreateImageView(Host.Device, &ViewInformation, nullptr, &OutView) != VK_SUCCESS)
    {
        ReportRadianceTarget("vkCreateImageView failed for the radiance target");
        vkFreeMemory(Host.Device, OutMemory, nullptr);
        vkDestroyImage(Host.Device, OutImage, nullptr);
        OutMemory = VK_NULL_HANDLE;
        OutImage  = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

// Destroy whatever is live and null every handle. Shared by the resize path and the finalizer.
void ReleaseRadianceResources(RadianceTarget& Target)
{
    if (Target.Host == nullptr || Target.Host->Device == VK_NULL_HANDLE)
        return;

    if (Target.ColourView != VK_NULL_HANDLE)   vkDestroyImageView(Target.Host->Device, Target.ColourView, nullptr);
    if (Target.ColourImage != VK_NULL_HANDLE)  vkDestroyImage(Target.Host->Device, Target.ColourImage, nullptr);
    if (Target.ColourMemory != VK_NULL_HANDLE) vkFreeMemory(Target.Host->Device, Target.ColourMemory, nullptr);

    Target.ColourView    = VK_NULL_HANDLE;
    Target.ColourImage   = VK_NULL_HANDLE;
    Target.ColourMemory  = VK_NULL_HANDLE;
    Target.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

// 📝 One barrier shape serves both transitions — only the layouts, access masks, and stages differ. Kept as one helper so the two public
//    transitions cannot drift apart.
void BarrierRadianceLayout(RadianceTarget&       Target,
                           VkCommandBuffer       CommandBuffer,
                           VkImageLayout         TargetLayout,
                           VkAccessFlags         SourceAccess,
                           VkAccessFlags         TargetAccess,
                           VkPipelineStageFlags  SourceStage,
                           VkPipelineStageFlags  TargetStage)
{
    if (!Target.ReadyCondition || Target.CurrentLayout == TargetLayout)
        return;

    VkImageMemoryBarrier Barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    Barrier.oldLayout                       = Target.CurrentLayout;
    Barrier.newLayout                       = TargetLayout;
    Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image                           = Target.ColourImage;
    Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    Barrier.subresourceRange.levelCount     = 1;
    Barrier.subresourceRange.layerCount     = 1;
    Barrier.srcAccessMask                   = SourceAccess;
    Barrier.dstAccessMask                   = TargetAccess;

    vkCmdPipelineBarrier(CommandBuffer, SourceStage, TargetStage, 0, 0, nullptr, 0, nullptr, 1, &Barrier);
    Target.CurrentLayout = TargetLayout;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeRadianceTarget(RadianceTarget& Target, VulkanHost& Host, uint32_t Width, uint32_t Height)
{
    Target = RadianceTarget{};
    Target.Host = &Host;

    if (Width == 0 || Height == 0)
    {
        ReportRadianceTarget("refusing a zero-dimension radiance target");
        return false;
    }

    if (!ConstructRadianceResources(Host, Width, Height, Target.ColourImage, Target.ColourMemory, Target.ColourView))
        return false;

    Target.Width          = Width;
    Target.Height         = Height;
    Target.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Target.ReadyCondition = true;
    return true;
}

bool ReconfigureRadianceTarget(RadianceTarget& Target, uint32_t Width, uint32_t Height)
{
    if (Target.Host == nullptr)
        return false;
    if (Width == 0 || Height == 0)
        return false;
    if (Target.ReadyCondition && Target.Width == Width && Target.Height == Height)
        return true;

    ReleaseRadianceResources(Target);
    Target.ReadyCondition = false;

    if (!ConstructRadianceResources(*Target.Host, Width, Height, Target.ColourImage, Target.ColourMemory, Target.ColourView))
        return false;

    Target.Width          = Width;
    Target.Height         = Height;
    Target.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Target.ReadyCondition = true;
    return true;
}

void TransitionRadianceTargetForRendering(RadianceTarget& Target, VkCommandBuffer CommandBuffer)
{
    // ⚠️ The source access is the resolve's SHADER_READ from the PREVIOUS frame (or nothing on the first), so the wait is on the fragment stage
    //    that sampled it — not on a transfer or a host write.
    BarrierRadianceLayout(Target, CommandBuffer,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_ACCESS_SHADER_READ_BIT,
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
}

void TransitionRadianceTargetForSampling(RadianceTarget& Target, VkCommandBuffer CommandBuffer)
{
    BarrierRadianceLayout(Target, CommandBuffer,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_ACCESS_SHADER_READ_BIT,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void FinalizeRadianceTarget(RadianceTarget& Target)
{
    ReleaseRadianceResources(Target);
    Target.Width          = 0;
    Target.Height         = 0;
    Target.ReadyCondition = false;
    Target.Host           = nullptr;
}

} // namespace Frontier
