/*==============================================================================================================================================
                                                             VISIBILITYIMAGE.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the renderer-owned visibility buffer. Raw Vulkan image + device-local allocation + colour view, mirroring VisibilityDepth
//    (no VMA). The raster opens a colour(this)+depth dynamic-rendering scope elsewhere and writes packed identities here; this file only owns the
//    R32_UINT target and the barrier that hands it to a sampling resolve. Layout is tracked across frames so the raster can transition from the
//    prior SHADER_READ_ONLY (a previous frame's resolve) back to COLOR_ATTACHMENT.

#include "Graphics/Visibility/VisibilityImage.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

void ReportVisibilityImage(const char* MessageText)
{
    std::fprintf(stderr, "[VisibilityImage] %s\n", MessageText);
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

// Allocate the R32_UINT image + device-local memory + colour view. On any failure every out handle is left null so the caller's ReadyCondition
// stays false. Usage is colour-attachment (the raster writes it) plus sampled (a resolve reads it) plus transfer-source (validation copy-back).
bool ConstructVisibilityResources(VulkanHost&     Host,
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
    ImageInformation.format        = VisibilityImageFormat;
    ImageInformation.extent        = { Width, Height, 1 };
    ImageInformation.mipLevels     = 1;
    ImageInformation.arrayLayers   = 1;
    ImageInformation.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageInformation.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageInformation.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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
    ViewInformation.format                      = VisibilityImageFormat;
    ViewInformation.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
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
void ReleaseVisibilityResources(VisibilityImage& Target)
{
    if (Target.Host == nullptr || Target.Host->Device == VK_NULL_HANDLE)
        return;
    if (Target.IdView   != VK_NULL_HANDLE) vkDestroyImageView(Target.Host->Device, Target.IdView, Target.Host->Allocator);
    if (Target.IdImage  != VK_NULL_HANDLE) vkDestroyImage(Target.Host->Device, Target.IdImage, Target.Host->Allocator);
    if (Target.IdMemory != VK_NULL_HANDLE) vkFreeMemory(Target.Host->Device, Target.IdMemory, Target.Host->Allocator);
    Target.IdView   = VK_NULL_HANDLE;
    Target.IdImage  = VK_NULL_HANDLE;
    Target.IdMemory = VK_NULL_HANDLE;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeVisibilityImage(VisibilityImage& Target, VulkanHost& Host, uint32_t Width, uint32_t Height)
{
    Target.Host           = &Host;
    Target.IdImage        = VK_NULL_HANDLE;
    Target.IdMemory       = VK_NULL_HANDLE;
    Target.IdView         = VK_NULL_HANDLE;
    Target.Width          = 0;
    Target.Height         = 0;
    Target.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Target.ReadyCondition = false;

    if (Width == 0 || Height == 0)
    {
        ReportVisibilityImage("zero extent — visibility buffer not built");
        return false;
    }
    if (!ConstructVisibilityResources(Host, Width, Height, Target.IdImage, Target.IdMemory, Target.IdView))
    {
        ReportVisibilityImage("visibility image / memory / view creation failed");
        return false;
    }

    Target.Width          = Width;
    Target.Height         = Height;
    Target.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Target.ReadyCondition = true;
    return true;
}

bool ReconfigureVisibilityImage(VisibilityImage& Target, uint32_t Width, uint32_t Height)
{
    if (Width == 0 || Height == 0)
        return false;
    if (Target.ReadyCondition && Target.Width == Width && Target.Height == Height)
        return true;
    if (Target.Host == nullptr)
        return false;

    ReleaseVisibilityResources(Target);
    Target.ReadyCondition = false;
    Target.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!ConstructVisibilityResources(*Target.Host, Width, Height, Target.IdImage, Target.IdMemory, Target.IdView))
    {
        Target.Width = Target.Height = 0;
        return false;
    }
    Target.Width          = Width;
    Target.Height         = Height;
    Target.ReadyCondition = true;
    return true;
}

void TransitionVisibilityImageForSampling(VisibilityImage& Target, VkCommandBuffer CommandBuffer)
{
    if (!Target.ReadyCondition || Target.Host == nullptr)
        return;

    VkImageSubresourceRange ColourRange = {};
    ColourRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ColourRange.levelCount = 1;
    ColourRange.layerCount = 1;

    VkImageMemoryBarrier ToSampled = {};
    ToSampled.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToSampled.oldLayout           = Target.CurrentLayout;
    ToSampled.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ToSampled.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToSampled.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToSampled.image               = Target.IdImage;
    ToSampled.subresourceRange    = ColourRange;
    ToSampled.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ToSampled.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToSampled);

    Target.CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void FinalizeVisibilityImage(VisibilityImage& Target)
{
    ReleaseVisibilityResources(Target);
    Target.Width          = 0;
    Target.Height         = 0;
    Target.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Target.ReadyCondition = false;
    Target.Host           = nullptr;
}

} // namespace Frontier
