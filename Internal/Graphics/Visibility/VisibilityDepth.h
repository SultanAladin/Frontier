/*==============================================================================================================================================
                                                               VISIBILITYDEPTH.H
==============================================================================================================================================*/
// 🧩 The renderer-owned scene depth target — the first offscreen render target of the visibility pipeline, and the input the HiZ pyramid
//    reduces. A D32_SFLOAT image with DEPTH_STENCIL_ATTACHMENT (a step writes it) | SAMPLED (the pyramid reads it) usage, its own device
//    allocation, and a depth view. Deliberately NOT part of the window substrate: the substrate stays the generic colour-present path and never
//    knows about depth. A depth step opens its OWN depth-only dynamic-rendering scope against this target inside the render schedule, which is
//    the pattern every later offscreen target (visibility buffer, shadow atlas, probe volumes) follows. Built once, rebuilt on resize, one per
//    viewport. Wired onto VulkanHost (borrowed, not owned).

#pragma once
#ifndef FRONTIER_GRAPHICS_VISIBILITY_VISIBILITYDEPTH_H
#define FRONTIER_GRAPHICS_VISIBILITY_VISIBILITYDEPTH_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The scene-depth format. D32_SFLOAT is the universal Pascal depth format (no stencil aspect — the visibility pipeline reconstructs surface
//    identity from the visibility buffer, not stencil), storable and sampleable so the HiZ reduce can read it directly.
constexpr VkFormat VisibilityDepthFormat = VK_FORMAT_D32_SFLOAT;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The owned depth target: the D32 image, its backing allocation, and the depth view a rendering scope binds / the pyramid samples. Host is
//    borrowed. ReadyCondition gates recording — a partial build leaves every handle null and the depth step records nothing. Rebuilt whenever
//    the viewport resizes (ReconfigureVisibilityDepth); the extent it currently spans is kept so a same-size reconfigure is a no-op.
struct VisibilityDepth
{
    VulkanHost*    Host           = nullptr;          // [-]  - not owned; supplies device / physical device / allocator
    VkImage        DepthImage     = VK_NULL_HANDLE;   // [-]  - D32_SFLOAT, DEPTH_STENCIL_ATTACHMENT | SAMPLED, device-local
    VkDeviceMemory DepthMemory    = VK_NULL_HANDLE;   // [-]  - backing allocation for DepthImage
    VkImageView    DepthView      = VK_NULL_HANDLE;   // [-]  - depth-aspect view (attachment + pyramid source)
    uint32_t       Width          = 0;                // [px] - live depth extent width
    uint32_t       Height         = 0;                // [px] - live depth extent height
    VkImageLayout  CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED; // [-] - tracked layout so the depth step transitions from the right source
    bool           ReadyCondition = false;            // [-]  - true once image + memory + view are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Allocate the depth image + memory + view at Width x Height. Returns false (ReadyCondition stays false, every handle null) on any Vulkan
// failure or a zero dimension. Host must be provisioned. Pair with FinalizeVisibilityDepth.
bool InitializeVisibilityDepth(VisibilityDepth& Target, VulkanHost& Host, uint32_t Width, uint32_t Height);

// Resize to Width x Height: tear down the image / memory / view and rebuild them. A no-op when the extent already matches (returns true) or
// when either dimension is zero (returns false, target unchanged). The device must be idle, or an in-flight frame may still read the old image.
bool ReconfigureVisibilityDepth(VisibilityDepth& Target, uint32_t Width, uint32_t Height);

// Record a depth-only rendering scope that clears this target to the far plane (1.0). Opens its own CmdBeginRendering scope against the depth
// view, so it is self-contained inside a render-schedule step and independent of the substrate's colour scope. Leaves the image in
// DEPTH_STENCIL_ATTACHMENT layout (CurrentLayout tracks it). A no-op when ReadyCondition is false. Later phases record geometry here instead of
// only clearing. CommandBuffer must be inside a recording state but OUTSIDE any active rendering scope.
void RecordVisibilityDepthClear(VisibilityDepth& Target, VkCommandBuffer CommandBuffer);

// Barrier the depth image from its current layout to SHADER_READ_ONLY so a compute reduce (the HiZ pyramid) can sample it. Updates
// CurrentLayout. A no-op when ReadyCondition is false. Call after RecordVisibilityDepthClear, before the pyramid dispatch.
void TransitionVisibilityDepthForSampling(VisibilityDepth& Target, VkCommandBuffer CommandBuffer);

// Destroy the view, image, and memory, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeVisibilityDepth(VisibilityDepth& Target);

} // namespace Frontier

#endif
