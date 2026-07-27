/*==============================================================================================================================================
                                                           VULKANIMGUIINTERFACE.CPP
==============================================================================================================================================*/
// 🧩 The crash-safe frame lifecycle. Structured after the official ImGui example FrameRender / FramePresent, split into a Begin
//    (acquire + fence wait + command-buffer begin + render-pass begin) and a Submit-and-present, with the six resize rules enforced at the
//    exact points they matter. The acquire result gates the fence reset (rule 4); the present result and the window callback both feed the
//    rebuild flag (rules 2, 3); minimize / zero-extent short-circuits the whole frame (rule 6).

#include "Graphics/RenderExtension/Device/VulkanImguiInterface.h"

#include "Platform/Windowing/PlatformWindow.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL STATE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The image index acquired by BeginImguiFrame, consumed by SubmitAndPresentImguiFrame. One window is rendered at a
    //    time on the main thread, so a single carry value is sufficient and keeps the two calls decoupled.
    uint32_t AcquiredFrameIndex = 0;
    bool     FrameAcquired      = false;

    bool WindowRenderable(const PlatformWindow& Window)
    {
        return !Window.MinimizedCondition && Window.FramebufferWidth > 0 && Window.FramebufferHeight > 0;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeVulkanImguiInterface(VulkanImguiInterface& Interface, VulkanHost& Host, PlatformWindow& Window)
{
    ImGui_ImplVulkanH_Window* Target = &Interface.Window;
    Target->Surface = Window.PresentationSurface;

    // Confirm the queue family can present to this surface (should always hold for the graphics family on desktop).
    VkBool32 PresentSupported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(Host.PhysicalDevice, Host.GraphicsQueueFamily, Target->Surface, &PresentSupported);
    if (PresentSupported != VK_TRUE)
    {
        fprintf(stderr, "[vulkan] graphics queue family cannot present to the window surface\n");
        return false;
    }

    // Choose a surface format + present mode via the ImGui helper (SRGB preferred, FIFO for vsync).
    const VkFormat RequestFormats[] =
    {
        VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM,   VK_FORMAT_R8G8B8_UNORM
    };
    Target->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        Host.PhysicalDevice, Target->Surface, RequestFormats,
        (int)(sizeof(RequestFormats) / sizeof(RequestFormats[0])), VK_COLORSPACE_SRGB_NONLINEAR_KHR);

    const VkPresentModeKHR RequestModes[] = { VK_PRESENT_MODE_FIFO_KHR };
    Target->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        Host.PhysicalDevice, Target->Surface, RequestModes,
        (int)(sizeof(RequestModes) / sizeof(RequestModes[0])));

    uint32_t Width  = 0;
    uint32_t Height = 0;
    QueryFramebufferExtent(Window, Width, Height);

    // 📝 CreateOrResizeWindow builds the swapchain, render pass, image views, framebuffers and per-frame semaphores. The
    //    final image-usage argument is COLOR_ATTACHMENT (this imgui version added that parameter).
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        Host.Instance, Host.PhysicalDevice, Host.Device, Target,
        Host.GraphicsQueueFamily, Host.Allocator,
        (int)Width, (int)Height, Interface.MinimumImageCount, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

    Interface.SwapchainRebuildRequested = false;
    FrameAcquired = false;
    return true;
}

void RebuildSwapchainIfRequested(VulkanImguiInterface& Interface, VulkanHost& Host, PlatformWindow& Window)
{
    // Fold the window's callback backstop into the interface's flag (rule 3).
    if (Window.RebuildRequested)
    {
        Interface.SwapchainRebuildRequested = true;
        Window.RebuildRequested = false;
    }

    if (!Interface.SwapchainRebuildRequested)
        return;

    uint32_t Width  = 0;
    uint32_t Height = 0;
    QueryFramebufferExtent(Window, Width, Height);
    if (Width == 0 || Height == 0)
        return;   // minimized / zero-extent — keep the flag set, rebuild once it has real extent (rule 6)

    // Rule 5: nothing in flight may reference the images being freed.
    vkDeviceWaitIdle(Host.Device);
    ImGui_ImplVulkan_SetMinImageCount(Interface.MinimumImageCount);
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        Host.Instance, Host.PhysicalDevice, Host.Device, &Interface.Window,
        Host.GraphicsQueueFamily, Host.Allocator,
        (int)Width, (int)Height, Interface.MinimumImageCount, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    Interface.Window.FrameIndex = 0;
    Interface.SwapchainRebuildRequested = false;
}

bool BeginImguiFrame(VulkanImguiInterface& Interface, VulkanHost& Host, PlatformWindow& Window)
{
    FrameAcquired = false;

    // Rule 3 + 5: service any pending rebuild before touching the swapchain.
    RebuildSwapchainIfRequested(Interface, Host, Window);

    // Rule 6: never acquire while minimized or zero-extent.
    if (!WindowRenderable(Window))
        return false;

    ImGui_ImplVulkanH_Window* Target = &Interface.Window;
    VkSemaphore ImageAcquired = Target->FrameSemaphores[Target->SemaphoreIndex].ImageAcquiredSemaphore;

    // Rule 1 + 2: check the acquire result. OUT_OF_DATE → abandon this frame and flag a rebuild.
    VkResult Outcome = vkAcquireNextImageKHR(Host.Device, Target->Swapchain, UINT64_MAX,
                                             ImageAcquired, VK_NULL_HANDLE, &Target->FrameIndex);
    if (Outcome == VK_ERROR_OUT_OF_DATE_KHR || Outcome == VK_SUBOPTIMAL_KHR)
    {
        Interface.SwapchainRebuildRequested = true;
        if (Outcome == VK_ERROR_OUT_OF_DATE_KHR)
            return false;   // image is unusable — skip the frame
    }
    else if (Outcome != VK_SUCCESS)
    {
        fprintf(stderr, "[vulkan] vkAcquireNextImageKHR failed (VkResult %d)\n", (int)Outcome);
        return false;
    }

    ImGui_ImplVulkanH_Frame* FrameData = &Target->Frames[Target->FrameIndex];

    // Rule 4: wait on then reset the in-flight fence only now that the acquire succeeded — never before, or a skipped
    //    submit would strand the fence unsignalled and the next wait would hang.
    vkWaitForFences(Host.Device, 1, &FrameData->Fence, VK_TRUE, UINT64_MAX);
    vkResetFences(Host.Device, 1, &FrameData->Fence);

    vkResetCommandPool(Host.Device, FrameData->CommandPool, 0);
    VkCommandBufferBeginInfo BeginInfo = {};
    BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(FrameData->CommandBuffer, &BeginInfo);

    VkRenderPassBeginInfo RenderPassBegin = {};
    RenderPassBegin.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RenderPassBegin.renderPass               = Target->RenderPass;
    RenderPassBegin.framebuffer              = FrameData->Framebuffer;
    RenderPassBegin.renderArea.extent.width  = (uint32_t)Target->Width;
    RenderPassBegin.renderArea.extent.height = (uint32_t)Target->Height;
    RenderPassBegin.clearValueCount          = 1;
    RenderPassBegin.pClearValues             = &Target->ClearValue;
    vkCmdBeginRenderPass(FrameData->CommandBuffer, &RenderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

    AcquiredFrameIndex = Target->FrameIndex;
    FrameAcquired = true;
    return true;
}

void SubmitAndPresentImguiFrame(VulkanImguiInterface& Interface, VulkanHost& Host, ImDrawData* DrawData)
{
    if (!FrameAcquired)
        return;

    ImGui_ImplVulkanH_Window* Target = &Interface.Window;
    ImGui_ImplVulkanH_Frame*  FrameData = &Target->Frames[AcquiredFrameIndex];

    // 📝 The acquire-wait semaphore stays on the rotating SemaphoreIndex (it was the one passed to vkAcquireNextImageKHR in
    //    BeginImguiFrame). The signal / present-wait semaphore, however, must be indexed by the ACQUIRED IMAGE — not by the
    //    rotating index — so the semaphore a present operation still holds for image i is never re-signalled until image i is
    //    re-acquired. Reusing a per-rotation semaphore for present is exactly what raised the validation FAULT
    //    (swapchain_semaphore_reuse): the binary semaphore could still be in use by an outstanding present when the rotation
    //    wrapped to it. SemaphoreCount == ImageCount + 1, so an image-indexed lookup is always in bounds.
    VkSemaphore ImageAcquired  = Target->FrameSemaphores[Target->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore RenderComplete = Target->FrameSemaphores[AcquiredFrameIndex].RenderCompleteSemaphore;

    ImGui_ImplVulkan_RenderDrawData(DrawData, FrameData->CommandBuffer);

    vkCmdEndRenderPass(FrameData->CommandBuffer);
    vkEndCommandBuffer(FrameData->CommandBuffer);

    const VkPipelineStageFlags WaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo SubmitInfo = {};
    SubmitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitInfo.waitSemaphoreCount   = 1;
    SubmitInfo.pWaitSemaphores      = &ImageAcquired;
    SubmitInfo.pWaitDstStageMask    = &WaitStage;
    SubmitInfo.commandBufferCount   = 1;
    SubmitInfo.pCommandBuffers      = &FrameData->CommandBuffer;
    SubmitInfo.signalSemaphoreCount = 1;
    SubmitInfo.pSignalSemaphores    = &RenderComplete;

    VkResult SubmitOutcome = vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInfo, FrameData->Fence);
    if (SubmitOutcome != VK_SUCCESS)
    {
        fprintf(stderr, "[vulkan] vkQueueSubmit failed (VkResult %d)\n", (int)SubmitOutcome);
        FrameAcquired = false;
        return;
    }

    // Rule 1 + 2: check the present result. SUBOPTIMAL/OUT_OF_DATE → still counted as presented, but flag a rebuild.
    VkPresentInfoKHR PresentInfo = {};
    PresentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    PresentInfo.waitSemaphoreCount = 1;
    PresentInfo.pWaitSemaphores    = &RenderComplete;
    PresentInfo.swapchainCount     = 1;
    PresentInfo.pSwapchains        = &Target->Swapchain;
    PresentInfo.pImageIndices      = &AcquiredFrameIndex;

    VkResult PresentOutcome = vkQueuePresentKHR(Host.GraphicsQueue, &PresentInfo);
    if (PresentOutcome == VK_ERROR_OUT_OF_DATE_KHR || PresentOutcome == VK_SUBOPTIMAL_KHR)
    {
        Interface.SwapchainRebuildRequested = true;
    }
    else if (PresentOutcome != VK_SUCCESS)
    {
        fprintf(stderr, "[vulkan] vkQueuePresentKHR failed (VkResult %d)\n", (int)PresentOutcome);
    }

    // Advance to the next semaphore set (mirrors the ImGui example; SemaphoreCount == ImageCount + 1).
    Target->SemaphoreIndex = (Target->SemaphoreIndex + 1) % Target->SemaphoreCount;
    FrameAcquired = false;
}

void FinalizeVulkanImguiInterface(VulkanImguiInterface& Interface, VulkanHost& Host)
{
    if (Host.Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(Host.Device);
    ImGui_ImplVulkanH_DestroyWindow(Host.Instance, Host.Device, &Interface.Window, Host.Allocator);
}

} // namespace Frontier
