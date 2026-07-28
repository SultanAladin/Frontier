/*==============================================================================================================================================
                                                              WINDOWSUBSTRATE.H
==============================================================================================================================================*/
// 🧩 The shared bare-window substrate every application stands on. Owns one decorated native window, the Vulkan host, a minimal swapchain, and
//    the acquire → clear → present frame loop — nothing above that (no ImGui, no dock, no panels). An application's whole main() is: hand it a
//    title, call RunWindowSubstrate, done. Resize is honoured through WindowContext's callback backstop; minimized frames are skipped. This is
//    deliberately the smallest thing that proves the window + present loop is alive, and the one place that machinery lives (no per-app copies).

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDEREXTENSION_DEVICE_WINDOWSUBSTRATE_H
#define FRONTIER_GRAPHICS_RENDEREXTENSION_DEVICE_WINDOWSUBSTRATE_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Platform/Windowing/PlatformWindow.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <functional>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ALIASES
//------------------------------------------------------------------------------------------------------------------------

// 📝 A per-frame recorder an application supplies. When set, the substrate opens a dynamic-rendering scope on the acquired
//    image (clear via loadOp) and calls this to record the draws inside it, then closes the scope and presents. Left empty,
//    the substrate falls back to the plain transfer-clear path. The substrate stays draw-agnostic: it knows a command buffer
//    and an extent, never what is being drawn. Sized once at set-time, invoked per frame with no heap allocation.
using FrameRecorder = std::function<void(VkCommandBuffer CommandBuffer, VkExtent2D Extent)>;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The per-window present machinery. One swapchain, one command buffer recycled each frame, the acquire semaphore + a
//    per-image present-wait semaphore set + the fence that order acquire → clear → present. Built once, rebuilt on resize,
//    finalized once. The present-wait semaphore is per-image (indexed by acquired image) so a signalled semaphore is never
//    re-signalled before its own present has consumed it (Vulkan swapchain semaphore-reuse rule).
struct WindowSubstrate
{
    PlatformWindow             Window;                                            // [-]  - Native decorated window + WSI surface
    VulkanHost                 Host;                                              // [-]  - Instance / device / graphics queue

    VkSwapchainKHR             Swapchain            = VK_NULL_HANDLE;             // [-]  - Current swapchain
    VkFormat                   SurfaceFormat        = VK_FORMAT_B8G8R8A8_UNORM;   // [-]  - Chosen image format
    VkColorSpaceKHR            SurfaceColorSpace    = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; // [-] - Chosen colour space
    VkExtent2D                 Extent               = { 0, 0 };                   // [px] - Current swapchain extent
    std::vector<VkImage>       Images;                                            // [-]  - Swapchain images (owned by the swapchain)
    std::vector<VkImageView>   ImageViews;                                        // [-]  - One colour view per image (dynamic-rendering attachment)

    // 📝 TWO frames in flight. With a single in-flight frame the CPU blocks on the prior frame's fence before it can even
    //    acquire the next image, so under FIFO it lands "just before" / "just after" the vsync boundary on alternating frames
    //    — a 18.7ms / 14.7ms present-cadence SAWTOOTH (proven in MouseTrace.txt) that reads as micro-stutter even when the
    //    mouse deltas are perfectly smooth. A second in-flight slot lets the CPU record frame N+1 while the GPU/present of
    //    frame N is still outstanding, so FIFO paces presents on an even boundary. Per-slot command buffer + acquire semaphore
    //    + fence (all indexed by FrameSlot); the present-wait semaphore stays per-IMAGE (swapchain semaphore-reuse rule).
    static const int           FramesInFlight       = 2;                          // [-]  - In-flight frame slots (double-buffered CPU/GPU overlap)
    VkCommandPool              CommandPool          = VK_NULL_HANDLE;             // [-]  - Pool the per-slot command buffers come from
    VkCommandBuffer            CommandBuffers[FramesInFlight]   = {};             // [-]  - One command buffer per in-flight slot, recorded fresh each frame
    VkSemaphore                AcquireComplete[FramesInFlight]  = {};             // [-]  - Per-slot: signalled when this slot's image is acquired
    VkFence                    FrameFence[FramesInFlight]       = {};             // [-]  - Per-slot CPU-GPU sync (waited before reusing the slot)
    std::vector<VkSemaphore>   RenderCompletePerImage;                            // [-]  - One present-wait semaphore per swapchain image (indexed by acquired image)
    int                        FrameSlot            = 0;                          // [-]  - Round-robin cursor into the in-flight slots

    FrameRecorder              RecordSequence;                                    // [-]  - Optional per-frame draw recorder (empty = transfer-clear only)

    // 📝 Optional per-frame PREAMBLE recorder, invoked after the command buffer begins but BEFORE the swapchain colour scope opens (and
    //    before the UNDEFINED→COLOR_ATTACHMENT transition). This is where offscreen work that manages its OWN rendering scopes / compute
    //    dispatches belongs — a depth prepass, the visibility raster, the HiZ reduce, the shadow atlas — because Vulkan forbids nesting one
    //    dynamic-rendering scope inside another, so those cannot run inside RecordSequence. The substrate stays draw-agnostic: it only knows
    //    "run this before the colour scope". Left empty, nothing runs here. Same signature + no-heap-per-frame contract as RecordSequence.
    FrameRecorder              RecordPreamble;                                    // [-]  - Optional offscreen preamble, run before the colour scope opens
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Stand up the window + Vulkan host + swapchain + frame resources. Returns false with the substrate left safely finalizable on any failure.
bool InitializeWindowSubstrate(WindowSubstrate& Substrate,
                               const char*      TitleText,
                               uint32_t         RequestedWidth,
                               uint32_t         RequestedHeight);

// Drive the acquire → clear → present loop until the window is closed. Rebuilds the swapchain on resize; skips minimized frames.
void RunWindowSubstrate(WindowSubstrate& Substrate);

// Destroy everything InitializeWindowSubstrate created. Safe on partially-initialized state.
void FinalizeWindowSubstrate(WindowSubstrate& Substrate);

// Convenience: Initialize → Run → Finalize a 1600x900 window with the given title. Returns 0 on clean exit, 1 on bring-up failure.
int ExecuteWindowSubstrate(const char* TitleText);

} // namespace Frontier

#endif
