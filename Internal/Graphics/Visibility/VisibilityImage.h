/*==============================================================================================================================================
                                                              VISIBILITYIMAGE.H
==============================================================================================================================================*/
// 🧩 The renderer-owned visibility buffer — the colour target of the hardware visibility raster, paired with VisibilityDepth. A single-channel
//    R32_UINT image: each covered pixel stores a packed surface identity (partition ordinal in the high bits, primitive ordinal in the low bits)
//    that a later resolve unpacks to reconstruct shading inputs. Usage is COLOR_ATTACHMENT (the raster writes it) | SAMPLED | TRANSFER_SRC (a
//    resolve reads it, and validation can copy it back for inspection). Cleared to the empty sentinel (all-ones) so uncovered pixels are
//    distinguishable from partition 0 / primitive 0. Its own device allocation and a colour view; NOT part of the window substrate — it is an
//    offscreen target the preamble drives, exactly like VisibilityDepth. Built once, rebuilt on resize, one per viewport, host borrowed.

#pragma once
#ifndef FRONTIER_GRAPHICS_VISIBILITY_VISIBILITYIMAGE_H
#define FRONTIER_GRAPHICS_VISIBILITY_VISIBILITYIMAGE_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The visibility-buffer format. R32_UINT holds one packed identity per pixel — universally colour-attachment-and-storage capable on Pascal,
//    and integer so the packed id survives with no blending / no filtering / no sRGB conversion.
constexpr VkFormat VisibilityImageFormat = VK_FORMAT_R32_UINT;

// 📝 The empty-pixel sentinel a cleared visibility buffer carries. All-ones is not a reachable (partition, primitive) pack for any real draw, so a
//    resolve treats it as "no surface here" (background / sky). The raster overwrites it wherever geometry is covered.
constexpr uint32_t VisibilityEmptySentinel = 0xFFFFFFFFu;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The owned visibility target: the R32_UINT image, its backing allocation, and the colour view a rendering scope binds / a resolve samples.
//    Host is borrowed. ReadyCondition gates recording — a partial build leaves every handle null and the raster records nothing. Rebuilt whenever
//    the viewport resizes (ReconfigureVisibilityImage); the extent it currently spans is kept so a same-size reconfigure is a no-op. CurrentLayout
//    tracks the image layout across frames so the raster transitions from the right source.
struct VisibilityImage
{
    VulkanHost*    Host           = nullptr;          // [-]  - not owned; supplies device / physical device / allocator
    VkImage        IdImage        = VK_NULL_HANDLE;   // [-]  - R32_UINT, COLOR_ATTACHMENT | SAMPLED | TRANSFER_SRC, device-local
    VkDeviceMemory IdMemory       = VK_NULL_HANDLE;   // [-]  - backing allocation for IdImage
    VkImageView    IdView         = VK_NULL_HANDLE;   // [-]  - colour-aspect view (attachment + resolve source)
    uint32_t       Width          = 0;                // [px] - live extent width
    uint32_t       Height         = 0;                // [px] - live extent height
    VkImageLayout  CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED; // [-] - tracked layout so the raster transitions from the right source
    bool           ReadyCondition = false;            // [-]  - true once image + memory + view are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Allocate the R32_UINT image + memory + view at Width x Height. Returns false (ReadyCondition stays false, every handle null) on any Vulkan
// failure or a zero dimension. Host must be provisioned. Pair with FinalizeVisibilityImage.
bool InitializeVisibilityImage(VisibilityImage& Target, VulkanHost& Host, uint32_t Width, uint32_t Height);

// Resize to Width x Height: tear down the image / memory / view and rebuild them. A no-op when the extent already matches (returns true) or when
// either dimension is zero (returns false, target unchanged). The device must be idle, or an in-flight frame may still read the old image.
bool ReconfigureVisibilityImage(VisibilityImage& Target, uint32_t Width, uint32_t Height);

// Barrier the visibility image from its current layout to SHADER_READ_ONLY so a compute / fragment resolve can sample it. Updates CurrentLayout.
// A no-op when ReadyCondition is false. Call after the raster records, before a resolve reads it.
void TransitionVisibilityImageForSampling(VisibilityImage& Target, VkCommandBuffer CommandBuffer);

// Destroy the view, image, and memory, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeVisibilityImage(VisibilityImage& Target);

} // namespace Frontier

#endif
