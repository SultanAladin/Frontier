/*==============================================================================================================================================
                                                              RADIANCETARGET.H
==============================================================================================================================================*/
// 🧩 The renderer-owned LINEAR HDR colour target — the scene's radiance buffer, written by the sky and the surface shade before any tone mapping
//    happens. An R16G16B16A16_SFLOAT image: each pixel holds unbounded linear radiance rather than a display-encoded colour, so values above 1.0
//    survive instead of clipping. Usage is COLOR_ATTACHMENT (the scene units write it) | SAMPLED (the resolve reads it). Its own device allocation
//    and a colour view; NOT part of the window substrate — an offscreen target the frame drives, exactly like VisibilityImage / VisibilityDepth.
//    Built once, rebuilt on resize, one per viewport, host borrowed.
//
// 📝 Why this exists — it fixes two defects that no tone-map operator swap can reach:
//      • DOUBLE TONE MAP. The sky and the surface shade each mapped independently and then composited, so two different roll-offs met in one
//        image and the horizon could not match. Now both write linear radiance here and exactly ONE resolve maps the result.
//      • BLENDING IN DISPLAY SPACE. Alpha-over is only physically correct on linear light. The glass presets blended their already-encoded
//        output over an already-encoded sky, which is the wrong operation. Blending now happens here, in linear.
//
// ⚠️ SCENE ONLY. The display-referred overlays (ground grid, selection outline, component handles, clipmap lattice, id-hash resolve) deliberately
//    do NOT render here — they carry authored colours that must survive to the screen unaltered, and a tone map would desaturate and roll them
//    off. They draw straight onto the swapchain AFTER the resolve. See RadianceResolveInscription.h for that ordering contract.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDER_RADIANCE_RADIANCETARGET_H
#define FRONTIER_GRAPHICS_RENDER_RADIANCE_RADIANCETARGET_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The radiance format. R16G16B16A16_SFLOAT is the conservative HDR choice: half float carries the >1.0 highlights a tone map needs to roll
//    off, and the format is mandatory-supported for COLOR_ATTACHMENT | SAMPLED | BLEND in the Vulkan spec's required feature set, so it needs no
//    capability probe on the Pascal floor.
// ⚠️ Do NOT drop to B10G11R11_UFLOAT_PACK32 to save bandwidth: it has no alpha channel, and the glass presets' alpha-over compositing needs one.
constexpr VkFormat RadianceTargetFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The owned radiance target: the half-float image, its backing allocation, and the colour view a rendering scope binds / the resolve samples.
//    Host is borrowed. ReadyCondition gates recording — a partial build leaves every handle null and the caller falls back to writing the
//    swapchain directly. CurrentLayout tracks the layout across frames so each frame transitions from the right source (the previous frame left
//    it in SHADER_READ_ONLY after the resolve sampled it).
struct RadianceTarget
{
    VulkanHost*    Host           = nullptr;          // [-]  - not owned; supplies device / physical device / allocator
    VkImage        ColourImage    = VK_NULL_HANDLE;   // [-]  - R16G16B16A16_SFLOAT, COLOR_ATTACHMENT | SAMPLED, device-local
    VkDeviceMemory ColourMemory   = VK_NULL_HANDLE;   // [-]  - backing allocation for ColourImage
    VkImageView    ColourView     = VK_NULL_HANDLE;   // [-]  - colour-aspect view (attachment + resolve source)

    uint32_t       Width          = 0;                // [px] - live extent width
    uint32_t       Height         = 0;                // [px] - live extent height
    VkImageLayout  CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED; // [-] - tracked layout so each frame transitions from the right source
    bool           ReadyCondition = false;            // [-]  - true once image + memory + view are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Allocate the half-float image + memory + view at Width x Height. Returns false (ReadyCondition stays false, every handle null) on any Vulkan
// failure or a zero dimension. Host must be provisioned. Pair with FinalizeRadianceTarget.
bool InitializeRadianceTarget(RadianceTarget& Target, VulkanHost& Host, uint32_t Width, uint32_t Height);

// Resize to Width x Height: tear down the image / memory / view and rebuild them. A no-op when the extent already matches (returns true) or when
// either dimension is zero (returns false, target unchanged). The device must be idle, or an in-flight frame may still read the old image.
bool ReconfigureRadianceTarget(RadianceTarget& Target, uint32_t Width, uint32_t Height);

// Barrier the target from its current layout to COLOR_ATTACHMENT_OPTIMAL so a rendering scope can write it. Updates CurrentLayout. A no-op when
// ReadyCondition is false. Call before opening the radiance scope each frame.
void TransitionRadianceTargetForRendering(RadianceTarget& Target, VkCommandBuffer CommandBuffer);

// Barrier the target to SHADER_READ_ONLY so the resolve can sample it. Updates CurrentLayout. A no-op when ReadyCondition is false. Call after
// the radiance scope closes, before the resolve records.
void TransitionRadianceTargetForSampling(RadianceTarget& Target, VkCommandBuffer CommandBuffer);

// Destroy the view, image, and memory, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeRadianceTarget(RadianceTarget& Target);

} // namespace Frontier

#endif
