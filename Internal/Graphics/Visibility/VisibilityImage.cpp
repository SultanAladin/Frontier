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

// 📝 Which packed route the host's int64-atomics features permit. Image atomics win when both the storage image and the fallback buffer are
//    possible — the image route is a single texel fetch in the resolve versus a computed linear index — so it is checked first.
VisibilityPackedRoute ResolvePackedRoute(const VulkanHost& Host)
{
    if (Host.ShaderImageInt64AtomicsEnabled)  return VisibilityPackedRoute::StorageImage;
    if (Host.ShaderBufferInt64AtomicsEnabled) return VisibilityPackedRoute::StorageBuffer;
    return VisibilityPackedRoute::None;
}

// Allocate the R64_UINT storage image + device-local memory + storage view for the image route. Usage is storage (the raster atomicMaxes it,
// the resolve reads it) plus transfer-dst (the per-frame sentinel clear). On any failure every out handle is left null.
bool ConstructPackedImage(VulkanHost&     Host,
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
    ImageInformation.format        = VisibilityPackedFormat;
    ImageInformation.extent        = { Width, Height, 1 };
    ImageInformation.mipLevels     = 1;
    ImageInformation.arrayLayers   = 1;
    ImageInformation.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageInformation.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageInformation.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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
    ViewInformation.format                      = VisibilityPackedFormat;
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

// Allocate the device-local R64 SSBO (ByteSize bytes) for the buffer route. Usage is storage (raster atomicMax + resolve read) plus transfer-dst
// (the per-frame sentinel fill). On any failure both out handles are left null.
bool ConstructPackedBuffer(VulkanHost&     Host,
                           VkDeviceSize    ByteSize,
                           VkBuffer&       OutBuffer,
                           VkDeviceMemory& OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = ByteSize;
    BufferInformation.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
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

// Build the packed atomicMax target for whichever route the host permits. None leaves every packed handle null and is a success (the software
// path simply stays gated off). A failed build of the chosen route reports and returns false so the caller can decline the software path without
// failing the whole visibility buffer — the hardware raster still works. Fills the PackedRoute / handle / byte-span fields on Target.
bool ConstructPackedTarget(VisibilityImage& Target, VulkanHost& Host, uint32_t Width, uint32_t Height)
{
    Target.PackedRoute        = ResolvePackedRoute(Host);
    Target.PackedImage        = VK_NULL_HANDLE;
    Target.PackedImageMemory  = VK_NULL_HANDLE;
    Target.PackedImageView    = VK_NULL_HANDLE;
    Target.PackedImageLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Target.PackedBuffer       = VK_NULL_HANDLE;
    Target.PackedBufferMemory = VK_NULL_HANDLE;
    Target.PackedBufferBytes  = 0;

    if (Target.PackedRoute == VisibilityPackedRoute::StorageImage)
    {
        if (!ConstructPackedImage(Host, Width, Height,
                                  Target.PackedImage, Target.PackedImageMemory, Target.PackedImageView))
        {
            ReportVisibilityImage("packed storage-image (R64) creation failed — software raster unavailable this build");
            Target.PackedRoute = VisibilityPackedRoute::None;
            return false;
        }
        return true;
    }
    if (Target.PackedRoute == VisibilityPackedRoute::StorageBuffer)
    {
        const VkDeviceSize ByteSpan = (VkDeviceSize)Width * (VkDeviceSize)Height * VisibilityPackedWordByteSize;
        if (!ConstructPackedBuffer(Host, ByteSpan, Target.PackedBuffer, Target.PackedBufferMemory))
        {
            ReportVisibilityImage("packed storage-buffer (R64) allocation failed — software raster unavailable this build");
            Target.PackedRoute = VisibilityPackedRoute::None;
            return false;
        }
        Target.PackedBufferBytes = ByteSpan;
        return true;
    }
    return true; // None — no packed target, software path gated off by the caller
}

// Release the packed target's handles (whichever route is live) and null them. Safe on any partial set.
void ReleasePackedTarget(VisibilityImage& Target)
{
    if (Target.Host == nullptr || Target.Host->Device == VK_NULL_HANDLE)
        return;
    if (Target.PackedImageView    != VK_NULL_HANDLE) vkDestroyImageView(Target.Host->Device, Target.PackedImageView, Target.Host->Allocator);
    if (Target.PackedImage        != VK_NULL_HANDLE) vkDestroyImage(Target.Host->Device, Target.PackedImage, Target.Host->Allocator);
    if (Target.PackedImageMemory  != VK_NULL_HANDLE) vkFreeMemory(Target.Host->Device, Target.PackedImageMemory, Target.Host->Allocator);
    if (Target.PackedBuffer       != VK_NULL_HANDLE) vkDestroyBuffer(Target.Host->Device, Target.PackedBuffer, Target.Host->Allocator);
    if (Target.PackedBufferMemory != VK_NULL_HANDLE) vkFreeMemory(Target.Host->Device, Target.PackedBufferMemory, Target.Host->Allocator);
    Target.PackedImage        = VK_NULL_HANDLE;
    Target.PackedImageMemory  = VK_NULL_HANDLE;
    Target.PackedImageView    = VK_NULL_HANDLE;
    Target.PackedImageLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Target.PackedBuffer       = VK_NULL_HANDLE;
    Target.PackedBufferMemory = VK_NULL_HANDLE;
    Target.PackedBufferBytes  = 0;
    Target.PackedRoute        = VisibilityPackedRoute::None;
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
    Target.PackedRoute        = VisibilityPackedRoute::None;
    Target.PackedImage        = VK_NULL_HANDLE;
    Target.PackedImageMemory  = VK_NULL_HANDLE;
    Target.PackedImageView    = VK_NULL_HANDLE;
    Target.PackedImageLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Target.PackedBuffer       = VK_NULL_HANDLE;
    Target.PackedBufferMemory = VK_NULL_HANDLE;
    Target.PackedBufferBytes  = 0;
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

    // 📝 The packed target is best-effort: a device without int64 atomics builds no packed route (software path gated off), and a route that
    //    fails to allocate degrades to None rather than failing the whole visibility buffer — the hardware raster path still records.
    ConstructPackedTarget(Target, Host, Width, Height);

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
    ReleasePackedTarget(Target);
    Target.ReadyCondition = false;
    Target.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!ConstructVisibilityResources(*Target.Host, Width, Height, Target.IdImage, Target.IdMemory, Target.IdView))
    {
        Target.Width = Target.Height = 0;
        return false;
    }
    ConstructPackedTarget(Target, *Target.Host, Width, Height);
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

void ClearVisibilityPackedTarget(VisibilityImage& Target, VkCommandBuffer CommandBuffer)
{
    if (Target.Host == nullptr)
        return;

    if (Target.PackedRoute == VisibilityPackedRoute::StorageImage && Target.PackedImage != VK_NULL_HANDLE)
    {
        VkImageSubresourceRange PackedRange = {};
        PackedRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        PackedRange.levelCount = 1;
        PackedRange.layerCount = 1;

        // vkCmdClearColorImage requires a TRANSFER_DST / GENERAL layout; transition from whatever the prior frame left (a previous raster's
        // GENERAL, or UNDEFINED on the first frame) to TRANSFER_DST so the clear is legal.
        VkImageMemoryBarrier ToClear = {};
        ToClear.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ToClear.oldLayout           = Target.PackedImageLayout;
        ToClear.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ToClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ToClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ToClear.image               = Target.PackedImage;
        ToClear.subresourceRange    = PackedRange;
        ToClear.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        ToClear.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(CommandBuffer,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &ToClear);

        // The packed word is (DepthKey << 32) | Identity; the empty word is zero, so every 32-bit component of the R64 texel is cleared to 0.
        VkClearColorValue ClearWord = {};
        ClearWord.uint32[0] = (uint32_t)(VisibilityPackedEmptySentinel & 0xFFFFFFFFu);
        ClearWord.uint32[1] = (uint32_t)(VisibilityPackedEmptySentinel >> 32);
        vkCmdClearColorImage(CommandBuffer, Target.PackedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearWord, 1, &PackedRange);

        Target.PackedImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
    else if (Target.PackedRoute == VisibilityPackedRoute::StorageBuffer && Target.PackedBuffer != VK_NULL_HANDLE)
    {
        // vkCmdFillBuffer writes a repeating 32-bit pattern; the empty word is zero, so a zero fill clears every 64-bit word to the sentinel.
        vkCmdFillBuffer(CommandBuffer, Target.PackedBuffer, 0, Target.PackedBufferBytes,
                        (uint32_t)(VisibilityPackedEmptySentinel & 0xFFFFFFFFu));
    }
}

void TransitionVisibilityPackedForRaster(VisibilityImage& Target, VkCommandBuffer CommandBuffer)
{
    if (Target.Host == nullptr || Target.PackedRoute != VisibilityPackedRoute::StorageImage || Target.PackedImage == VK_NULL_HANDLE)
        return;

    VkImageSubresourceRange PackedRange = {};
    PackedRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    PackedRange.levelCount = 1;
    PackedRange.layerCount = 1;

    // From the clear's TRANSFER_DST to GENERAL, the only layout in which a storage image accepts imageAtomicMax from the software raster.
    VkImageMemoryBarrier ToGeneral = {};
    ToGeneral.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToGeneral.oldLayout           = Target.PackedImageLayout;
    ToGeneral.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    ToGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToGeneral.image               = Target.PackedImage;
    ToGeneral.subresourceRange    = PackedRange;
    ToGeneral.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    ToGeneral.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(CommandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToGeneral);

    Target.PackedImageLayout = VK_IMAGE_LAYOUT_GENERAL;
}

void FinalizeVisibilityImage(VisibilityImage& Target)
{
    ReleaseVisibilityResources(Target);
    ReleasePackedTarget(Target);
    Target.Width          = 0;
    Target.Height         = 0;
    Target.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Target.ReadyCondition = false;
    Target.Host           = nullptr;
}

} // namespace Frontier
