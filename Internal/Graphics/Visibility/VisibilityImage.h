/*==============================================================================================================================================
                                                              VISIBILITYIMAGE.H
==============================================================================================================================================*/
// 🧩 The renderer-owned visibility buffer — the colour target of the hardware visibility raster, paired with VisibilityDepth. A single-channel
//    R32_UINT image: each covered pixel stores a packed surface identity (partition ordinal in the high bits, primitive ordinal in the low bits)
//    that a later resolve unpacks to reconstruct shading inputs. Usage is COLOR_ATTACHMENT (the raster writes it) | SAMPLED | TRANSFER_SRC (a
//    resolve reads it, and validation can copy it back for inspection). Cleared to the empty sentinel (all-ones) so uncovered pixels are
//    distinguishable from partition 0 / primitive 0. Its own device allocation and a colour view; NOT part of the window substrate — it is an
//    offscreen target the preamble drives, exactly like VisibilityDepth. Built once, rebuilt on resize, one per viewport, host borrowed.
//    When the device exposes int64 shader atomics this also owns the software micro-raster's PACKED target — an R64_UINT (depth|id) word per
//    pixel the compute raster atomicMaxes into, then a resolve unpacks to this same IdImage + VisibilityDepth. The packed target is a storage
//    image where image atomics are enabled, a device-local SSBO where only buffer atomics are, and absent otherwise (software path gated off).

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

// 📝 The software micro-raster's packed-target format. One R64_UINT word per pixel carries (DepthKey << 32) | Identity, and the raster resolves
//    overlapping triangles with a single atomicMax on that word — which is precisely why 64 bits are required: depth and identity must move
//    together in one atomic or a pixel can end up with one triangle's depth and another's id. Gated on int64 image atomics; the buffer route
//    below carries the identical word layout for devices that expose only buffer atomics.
constexpr VkFormat VisibilityPackedFormat = VK_FORMAT_R64_UINT;

// 📝 One packed word is 8 bytes in both routes, so the buffer fallback spans Width * Height * this.
constexpr VkDeviceSize VisibilityPackedWordByteSize = 8;

// 📝 The empty packed word. atomicMax keeps the NEAREST surface by construction — DepthKey is built so nearer depth yields the LARGER key — so an
//    uncovered pixel must hold the minimum, zero. That is the opposite pole from VisibilityEmptySentinel above (all-ones), which is a *resolved*
//    identity rather than a max-competing word; the two sentinels are not interchangeable. A zero word reads as DepthKey 0 (the far plane), so the
//    resolve treats a zero high half as "no surface" and emits VisibilityEmptySentinel there.
constexpr uint64_t VisibilityPackedEmptySentinel = 0u;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The owned visibility target: the R32_UINT image, its backing allocation, and the colour view a rendering scope binds / a resolve samples.
//    Host is borrowed. ReadyCondition gates recording — a partial build leaves every handle null and the raster records nothing. Rebuilt whenever
//    the viewport resizes (ReconfigureVisibilityImage); the extent it currently spans is kept so a same-size reconfigure is a no-op. CurrentLayout
//    tracks the image layout across frames so the raster transitions from the right source.
// 📝 Which route backs the software micro-raster's packed atomicMax target. Chosen once at build time from the host's int64-atomics feature
//    flags: an R64_UINT storage image where image atomics are enabled (the fast, preferred route), a device-local R64 SSBO where only buffer
//    atomics are enabled, and None where neither is — in which case no packed target is built and the caller keeps the software path gated off.
enum class VisibilityPackedRoute
{
    None,          // neither int64-atomics feature — software raster unavailable, hardware raster only
    StorageImage,  // R64_UINT storage image, imageAtomicMax (preferred)
    StorageBuffer, // device-local R64 SSBO (Width * Height words), buffer atomicMax (fallback)
};

struct VisibilityImage
{
    VulkanHost*    Host           = nullptr;          // [-]  - not owned; supplies device / physical device / allocator
    VkImage        IdImage        = VK_NULL_HANDLE;   // [-]  - R32_UINT, COLOR_ATTACHMENT | SAMPLED | TRANSFER_SRC, device-local
    VkDeviceMemory IdMemory       = VK_NULL_HANDLE;   // [-]  - backing allocation for IdImage
    VkImageView    IdView         = VK_NULL_HANDLE;   // [-]  - colour-aspect view (attachment + resolve source)

    // ─── Software micro-raster packed atomicMax target (one route active; the others stay null) ───
    VisibilityPackedRoute PackedRoute        = VisibilityPackedRoute::None; // [-]  - which route below is live
    VkImage               PackedImage        = VK_NULL_HANDLE;  // [-]  - StorageImage route: R64_UINT, STORAGE | TRANSFER_DST, device-local
    VkDeviceMemory        PackedImageMemory  = VK_NULL_HANDLE;  // [-]  - backing allocation for PackedImage
    VkImageView           PackedImageView    = VK_NULL_HANDLE;  // [-]  - storage-aspect view (raster writes, resolve reads)
    VkImageLayout         PackedImageLayout  = VK_IMAGE_LAYOUT_UNDEFINED; // [-] - tracked layout for the packed storage image
    VkBuffer              PackedBuffer       = VK_NULL_HANDLE;  // [-]  - StorageBuffer route: R64 SSBO, STORAGE | TRANSFER_DST, device-local
    VkDeviceMemory        PackedBufferMemory = VK_NULL_HANDLE;  // [-]  - backing allocation for PackedBuffer
    VkDeviceSize          PackedBufferBytes  = 0;               // [B]  - live byte span of PackedBuffer (Width * Height * 8)

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

// Clear the software micro-raster's packed target to VisibilityPackedEmptySentinel (zero — the far-plane / no-surface word) so the frame's
// atomicMax starts from a clean floor. StorageImage route: transition to GENERAL then vkCmdClearColorImage; StorageBuffer route: vkCmdFillBuffer.
// A no-op when PackedRoute is None. Records into CommandBuffer; barrier the target to raster-writable afterwards via the transition helper below.
void ClearVisibilityPackedTarget(VisibilityImage& Target, VkCommandBuffer CommandBuffer);

// Barrier the packed storage-image route to GENERAL so the software raster can imageAtomicMax into it (a no-op for the buffer route, whose
// atomics need no layout). Call after ClearVisibilityPackedTarget, before the raster dispatch. A no-op when PackedRoute is not StorageImage.
void TransitionVisibilityPackedForRaster(VisibilityImage& Target, VkCommandBuffer CommandBuffer);

// Destroy the view, image, and memory, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeVisibilityImage(VisibilityImage& Target);

} // namespace Frontier

#endif
