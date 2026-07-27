/*==============================================================================================================================================
                                                           VULKANIMGUIINTERFACE.H
==============================================================================================================================================*/
// 🧩 Per-window swapchain + crash-safe ImGui frame lifecycle. Wraps ImGui's own ImGui_ImplVulkanH_Window helper (which encapsulates correct
//    image/view/framebuffer/semaphore recreation) and layers the six resize-crash rules from EngineDocs/PLAN-Windowing.md on top: check both
//    acquire and present results, rebuild on OUT_OF_DATE, honour the window's callback backstop, reset the fence only after a good acquire,
//    wait-idle before recreating, and skip zero-extent / minimized frames. This is where the previous application's resize crash is prevented.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDEREXTENSION_DEVICE_VULKANIMGUIINTERFACE_H
#define FRONTIER_GRAPHICS_RENDEREXTENSION_DEVICE_VULKANIMGUIINTERFACE_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include "backends/imgui_impl_vulkan.h"

namespace Frontier
{

struct PlatformWindow;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Owns one OS window's swapchain via the ImGui helper struct. SwapchainRebuildRequested is the interface's own copy of the
//    "stale" condition (raised by a SUBOPTIMAL/OUT_OF_DATE present or mirrored from the window's callback backstop).
struct VulkanImguiInterface
{
    ImGui_ImplVulkanH_Window Window;                       // [-] - Swapchain, frames, semaphores, render pass
    uint32_t                 MinimumImageCount        = 2; // [-] - Minimum in-flight images (>= 2)
    bool                     SwapchainRebuildRequested = false; // [-] - Swapchain is stale; rebuild before the next acquire
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Select the surface format + present mode and create the first swapchain sized to the window's framebuffer.
bool InitializeVulkanImguiInterface(VulkanImguiInterface& Interface, VulkanHost& Host, PlatformWindow& Window);

// Rule 3 + 5: if the window's callback raised a resize (or a prior present flagged stale), wait-idle and recreate the
// swapchain at the fresh extent. Skips the rebuild while the window is minimized / zero-extent.
void RebuildSwapchainIfRequested(VulkanImguiInterface& Interface, VulkanHost& Host, PlatformWindow& Window);

// Rule 6 + 1/2: returns false when the frame must be skipped (minimized, zero-extent, or acquire returned OUT_OF_DATE —
// in which case a rebuild is flagged). On true the caller records ImGui draw data and calls SubmitAndPresentImguiFrame.
bool BeginImguiFrame(VulkanImguiInterface& Interface, VulkanHost& Host, PlatformWindow& Window);

// Rule 4 + 1/2: submits the recorded draw data and presents. Resets the in-flight fence only after the earlier acquire
// succeeded; a SUBOPTIMAL/OUT_OF_DATE present flags a rebuild for next frame (this frame still presents).
void SubmitAndPresentImguiFrame(VulkanImguiInterface& Interface, VulkanHost& Host, ImDrawData* DrawData);

// Destroy the swapchain and its per-frame resources.
void FinalizeVulkanImguiInterface(VulkanImguiInterface& Interface, VulkanHost& Host);

} // namespace Frontier

#endif
