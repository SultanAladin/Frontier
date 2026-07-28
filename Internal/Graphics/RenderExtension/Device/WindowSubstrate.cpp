/*==============================================================================================================================================
                                                             WINDOWSUBSTRATE.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the shared bare-window substrate: window + Vulkan host bring-up, a minimal swapchain, and a clear-to-colour present
//    loop. VulkanImguiInterface was ImGui-coupled, so the swapchain + present are owned directly here with plain Vulkan (transfer-clear each
//    acquired image, then present). One swapchain, one recycled command buffer, one in-flight frame — the smallest correct present path.

#include "Graphics/RenderExtension/Device/WindowSubstrate.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// Select a straightforward BGRA8 / sRGB surface format when the surface offers it, otherwise take whatever it reports first.
void ResolveSurfaceFormat(WindowSubstrate& Substrate)
{
    uint32_t FormatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(Substrate.Host.PhysicalDevice, Substrate.Window.PresentationSurface, &FormatCount, nullptr);
    if (FormatCount == 0)
        return;

    std::vector<VkSurfaceFormatKHR> Formats(FormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(Substrate.Host.PhysicalDevice, Substrate.Window.PresentationSurface, &FormatCount, Formats.data());

    Substrate.SurfaceFormat     = Formats[0].format;
    Substrate.SurfaceColorSpace = Formats[0].colorSpace;
    for (const VkSurfaceFormatKHR& Candidate : Formats)
    {
        if (Candidate.format == VK_FORMAT_B8G8R8A8_UNORM && Candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            Substrate.SurfaceFormat     = Candidate.format;
            Substrate.SurfaceColorSpace = Candidate.colorSpace;
            return;
        }
    }
}

// Destroy the per-image present-wait semaphores and empty the set. Safe on an empty set.
void FinalizePresentSignalSet(WindowSubstrate& Substrate)
{
    for (VkSemaphore Signal : Substrate.RenderCompletePerImage)
    {
        if (Signal != VK_NULL_HANDLE)
            vkDestroySemaphore(Substrate.Host.Device, Signal, Substrate.Host.Allocator);
    }
    Substrate.RenderCompletePerImage.clear();
}

// (Re)build one present-wait semaphore per swapchain image. The present at image index N waits on entry N, so a signalled
// semaphore is never re-signalled before its own present consumed it (the swapchain semaphore-reuse rule the validation
// layer enforces). Called after the image count is known / changes.
void ConstructPresentSignalSet(WindowSubstrate& Substrate)
{
    FinalizePresentSignalSet(Substrate);

    VkSemaphoreCreateInfo SemaphoreInfo = {};
    SemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    Substrate.RenderCompletePerImage.resize(Substrate.Images.size(), VK_NULL_HANDLE);
    for (VkSemaphore& Signal : Substrate.RenderCompletePerImage)
        vkCreateSemaphore(Substrate.Host.Device, &SemaphoreInfo, Substrate.Host.Allocator, &Signal);
}

// Destroy the per-image colour views and empty the set. Safe on an empty set.
void FinalizeImageViews(WindowSubstrate& Substrate)
{
    for (VkImageView View : Substrate.ImageViews)
    {
        if (View != VK_NULL_HANDLE)
            vkDestroyImageView(Substrate.Host.Device, View, Substrate.Host.Allocator);
    }
    Substrate.ImageViews.clear();
}

// (Re)build one colour image view per swapchain image so the dynamic-rendering colour attachment has a view to bind. Called
// after the image set is (re)established. Views are 2D, single-mip, single-layer, in the swapchain's surface format.
void ConstructImageViews(WindowSubstrate& Substrate)
{
    FinalizeImageViews(Substrate);
    Substrate.ImageViews.resize(Substrate.Images.size(), VK_NULL_HANDLE);
    for (size_t Index = 0; Index < Substrate.Images.size(); ++Index)
    {
        VkImageViewCreateInfo ViewInfo = {};
        ViewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ViewInfo.image                           = Substrate.Images[Index];
        ViewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ViewInfo.format                          = Substrate.SurfaceFormat;
        ViewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ViewInfo.subresourceRange.levelCount     = 1;
        ViewInfo.subresourceRange.layerCount     = 1;
        vkCreateImageView(Substrate.Host.Device, &ViewInfo, Substrate.Host.Allocator, &Substrate.ImageViews[Index]);
    }
}

// (Re)build the swapchain at the window's current framebuffer extent. The old swapchain is retired after the new one is created.
bool ConstructSwapchain(WindowSubstrate& Substrate)
{
    VkSurfaceCapabilitiesKHR Capabilities = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Substrate.Host.PhysicalDevice, Substrate.Window.PresentationSurface, &Capabilities);

    uint32_t Width = 0;
    uint32_t Height = 0;
    QueryFramebufferExtent(Substrate.Window, Width, Height);
    if (Width == 0 || Height == 0)
        return false;

    VkExtent2D Extent = { Width, Height };
    if (Capabilities.currentExtent.width != 0xFFFFFFFFu)
        Extent = Capabilities.currentExtent;

    uint32_t MinimumImages = Capabilities.minImageCount + 1;
    if (Capabilities.maxImageCount > 0 && MinimumImages > Capabilities.maxImageCount)
        MinimumImages = Capabilities.maxImageCount;

    VkSwapchainKHR Previous = Substrate.Swapchain;

    VkSwapchainCreateInfoKHR SwapchainInfo = {};
    SwapchainInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    SwapchainInfo.surface          = Substrate.Window.PresentationSurface;
    SwapchainInfo.minImageCount    = MinimumImages;
    SwapchainInfo.imageFormat      = Substrate.SurfaceFormat;
    SwapchainInfo.imageColorSpace  = Substrate.SurfaceColorSpace;
    SwapchainInfo.imageExtent      = Extent;
    SwapchainInfo.imageArrayLayers = 1;
    SwapchainInfo.imageUsage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    SwapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    SwapchainInfo.preTransform     = Capabilities.currentTransform;
    SwapchainInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    SwapchainInfo.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    SwapchainInfo.clipped          = VK_TRUE;
    SwapchainInfo.oldSwapchain     = Previous;

    VkResult Outcome = vkCreateSwapchainKHR(Substrate.Host.Device, &SwapchainInfo, Substrate.Host.Allocator, &Substrate.Swapchain);
    if (Outcome != VK_SUCCESS)
    {
        fprintf(stderr, "[window] vkCreateSwapchainKHR failed (VkResult %d)\n", (int)Outcome);
        return false;
    }

    if (Previous != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(Substrate.Host.Device, Previous, Substrate.Host.Allocator);

    uint32_t ImageCount = 0;
    vkGetSwapchainImagesKHR(Substrate.Host.Device, Substrate.Swapchain, &ImageCount, nullptr);
    Substrate.Images.resize(ImageCount);
    vkGetSwapchainImagesKHR(Substrate.Host.Device, Substrate.Swapchain, &ImageCount, Substrate.Images.data());
    Substrate.Extent = Extent;

    // Present-wait semaphores + colour views are one-per-image; rebuild both whenever the image count is (re)established.
    ConstructPresentSignalSet(Substrate);
    ConstructImageViews(Substrate);
    return true;
}

// One-time command pool + buffer + sync objects. These outlive individual swapchains.
bool ConstructFrameResources(WindowSubstrate& Substrate)
{
    VkCommandPoolCreateInfo PoolInfo = {};
    PoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    PoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolInfo.queueFamilyIndex = Substrate.Host.GraphicsQueueFamily;
    if (vkCreateCommandPool(Substrate.Host.Device, &PoolInfo, Substrate.Host.Allocator, &Substrate.CommandPool) != VK_SUCCESS)
        return false;

    // One command buffer per in-flight slot so slot N+1 can be recorded while slot N is still executing on the GPU.
    VkCommandBufferAllocateInfo AllocateInfo = {};
    AllocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    AllocateInfo.commandPool        = Substrate.CommandPool;
    AllocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    AllocateInfo.commandBufferCount = WindowSubstrate::FramesInFlight;
    if (vkAllocateCommandBuffers(Substrate.Host.Device, &AllocateInfo, Substrate.CommandBuffers) != VK_SUCCESS)
        return false;

    // 📝 Per-slot acquire semaphore + fence. The acquire semaphore must be per-slot (not shared) so the acquire of slot N+1
    //    does not signal a semaphore slot N's submit is still waiting on. The present-wait semaphores are per-IMAGE and built
    //    alongside the swapchain in ConstructPresentSignalSet, not here. Fences start SIGNALED so the first wait passes through.
    VkSemaphoreCreateInfo SemaphoreInfo = {};
    SemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo FenceInfo = {};
    FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int Slot = 0; Slot < WindowSubstrate::FramesInFlight; ++Slot)
    {
        vkCreateSemaphore(Substrate.Host.Device, &SemaphoreInfo, Substrate.Host.Allocator, &Substrate.AcquireComplete[Slot]);
        vkCreateFence(Substrate.Host.Device, &FenceInfo, Substrate.Host.Allocator, &Substrate.FrameFence[Slot]);
    }
    return true;
}

// Record the acquired image: transition UNDEFINED → TRANSFER_DST, clear to the constant colour, transition → PRESENT_SRC.
void RecordClear(WindowSubstrate& Substrate, VkCommandBuffer Commands, VkImage Target)
{
    (void)Substrate;
    vkResetCommandBuffer(Commands, 0);

    VkCommandBufferBeginInfo BeginInfo = {};
    BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(Commands, &BeginInfo);

    VkImageSubresourceRange ColorRange = {};
    ColorRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ColorRange.levelCount = 1;
    ColorRange.layerCount = 1;

    VkImageMemoryBarrier ToClear = {};
    ToClear.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToClear.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    ToClear.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToClear.image               = Target;
    ToClear.subresourceRange    = ColorRange;
    ToClear.srcAccessMask       = 0;
    ToClear.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToClear);

    VkClearColorValue ClearColour = {};
    ClearColour.float32[0] = 0.00f;
    ClearColour.float32[1] = 0.00f;
    ClearColour.float32[2] = 0.00f;
    ClearColour.float32[3] = 1.00f;
    vkCmdClearColorImage(Commands, Target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ClearColour, 1, &ColorRange);

    VkImageMemoryBarrier ToPresent = {};
    ToPresent.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToPresent.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToPresent.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    ToPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToPresent.image               = Target;
    ToPresent.subresourceRange    = ColorRange;
    ToPresent.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    ToPresent.dstAccessMask       = 0;
    vkCmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToPresent);

    vkEndCommandBuffer(Commands);
}

// Record one frame through the supplied recorder: transition the image UNDEFINED → COLOR_ATTACHMENT, open a dynamic-rendering
// scope (clear via loadOp), let the application record its draws, close the scope, transition → PRESENT_SRC. The substrate owns
// the clear and the layout choreography; the recorder only issues bind + draw commands against the open colour attachment.
void RecordAttachedSequence(WindowSubstrate& Substrate, VkCommandBuffer Commands, uint32_t ImageIndex)
{
    VkImage     Target = Substrate.Images[ImageIndex];
    VkImageView View   = Substrate.ImageViews[ImageIndex];

    vkResetCommandBuffer(Commands, 0);

    VkCommandBufferBeginInfo BeginInfo = {};
    BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(Commands, &BeginInfo);

    // 📝 Offscreen preamble first: depth prepass / visibility raster / HiZ reduce each open and close their OWN scopes here, before the
    //    swapchain colour scope opens (nesting rendering scopes is illegal). Runs inside the command buffer but outside any active scope.
    if (Substrate.RecordPreamble)
        Substrate.RecordPreamble(Commands, Substrate.Extent);

    VkImageSubresourceRange ColorRange = {};
    ColorRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ColorRange.levelCount = 1;
    ColorRange.layerCount = 1;

    VkImageMemoryBarrier ToAttachment = {};
    ToAttachment.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToAttachment.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    ToAttachment.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ToAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToAttachment.image               = Target;
    ToAttachment.subresourceRange    = ColorRange;
    ToAttachment.srcAccessMask       = 0;
    ToAttachment.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToAttachment);

    VkRenderingAttachmentInfoKHR ColorAttachment = {};
    ColorAttachment.sType                 = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    ColorAttachment.imageView             = View;
    ColorAttachment.imageLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ColorAttachment.loadOp                = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ColorAttachment.storeOp               = VK_ATTACHMENT_STORE_OP_STORE;
    ColorAttachment.clearValue.color.float32[0] = 0.00f;
    ColorAttachment.clearValue.color.float32[1] = 0.00f;
    ColorAttachment.clearValue.color.float32[2] = 0.00f;
    ColorAttachment.clearValue.color.float32[3] = 1.00f;

    VkRenderingInfoKHR RenderingInfo = {};
    RenderingInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    RenderingInfo.renderArea.extent    = Substrate.Extent;
    RenderingInfo.layerCount           = 1;
    RenderingInfo.colorAttachmentCount = 1;
    RenderingInfo.pColorAttachments    = &ColorAttachment;

    Substrate.Host.CmdBeginRendering(Commands, &RenderingInfo);
    if (Substrate.RecordSequence)
        Substrate.RecordSequence(Commands, Substrate.Extent);
    Substrate.Host.CmdEndRendering(Commands);

    VkImageMemoryBarrier ToPresent = {};
    ToPresent.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ToPresent.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ToPresent.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    ToPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToPresent.image               = Target;
    ToPresent.subresourceRange    = ColorRange;
    ToPresent.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ToPresent.dstAccessMask       = 0;
    vkCmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &ToPresent);

    vkEndCommandBuffer(Commands);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeWindowSubstrate(WindowSubstrate& Substrate,
                               const char*      TitleText,
                               uint32_t         RequestedWidth,
                               uint32_t         RequestedHeight)
{
    if (!InitializePlatformWindow(Substrate.Window, TitleText, RequestedWidth, RequestedHeight))
    {
        fprintf(stderr, "[window] window creation failed\n");
        return false;
    }

    uint32_t ExtensionCount = 0;
    const char** RequiredExtensions = QueryRequiredInstanceExtensions(ExtensionCount);
    if (!InitializeVulkanHost(Substrate.Host, RequiredExtensions, ExtensionCount))
    {
        fprintf(stderr, "[window] Vulkan host bring-up failed\n");
        return false;
    }

    if (!ConstructPresentationSurface(Substrate.Window, Substrate.Host.Instance))
    {
        fprintf(stderr, "[window] presentation surface creation failed\n");
        return false;
    }

    ResolveSurfaceFormat(Substrate);
    if (!ConstructFrameResources(Substrate) || !ConstructSwapchain(Substrate))
    {
        fprintf(stderr, "[window] present-loop resource creation failed\n");
        return false;
    }
    return true;
}

void RunWindowSubstrate(WindowSubstrate& Substrate)
{
    printf("[window] up. %ux%u, clearing each frame. Close the window to exit.\n", Substrate.Extent.width, Substrate.Extent.height);

    // -- Frame loop. Acquire → clear → present, rebuilding the swapchain whenever the window flags a resize or a present goes stale. --
    while (!QueryWindowCloseRequested(Substrate.Window))
    {
        PollPlatformEvents(Substrate.Window);

        if (Substrate.Window.MinimizedCondition)
            continue;

        if (Substrate.Window.RebuildRequested)
        {
            vkDeviceWaitIdle(Substrate.Host.Device);
            if (!ConstructSwapchain(Substrate))
                continue;
            Substrate.Window.RebuildRequested = false;
        }

        // This frame's in-flight slot. Wait only on THIS slot's fence — not the other slot's — so the CPU can run one frame
        // ahead while the previous slot's GPU work is still outstanding, which is what evens out the FIFO present cadence.
        const int      Slot          = Substrate.FrameSlot;
        VkFence        SlotFence     = Substrate.FrameFence[Slot];
        VkSemaphore    SlotAcquire   = Substrate.AcquireComplete[Slot];
        VkCommandBuffer SlotCommands = Substrate.CommandBuffers[Slot];

        vkWaitForFences(Substrate.Host.Device, 1, &SlotFence, VK_TRUE, UINT64_MAX);

        uint32_t ImageIndex = 0;
        VkResult Acquired = vkAcquireNextImageKHR(Substrate.Host.Device, Substrate.Swapchain, UINT64_MAX,
                                                  SlotAcquire, VK_NULL_HANDLE, &ImageIndex);
        if (Acquired == VK_ERROR_OUT_OF_DATE_KHR)
        {
            Substrate.Window.RebuildRequested = true;
            continue;
        }

        // Reset the fence only once we know we are submitting this frame (after a successful acquire), so an early-out acquire
        // failure never leaves the slot's fence unsignalled and deadlocks the next wait.
        vkResetFences(Substrate.Host.Device, 1, &SlotFence);

        // With a recorder set and dynamic rendering available, draw into a colour attachment (clear + draws); otherwise the
        // plain transfer-clear path. The acquire wait stage matches whichever path writes the image.
        const bool AttachedPath = Substrate.RecordSequence && Substrate.Host.DynamicRenderingEnabled;
        if (AttachedPath)
            RecordAttachedSequence(Substrate, SlotCommands, ImageIndex);
        else
            RecordClear(Substrate, SlotCommands, Substrate.Images[ImageIndex]);

        // The present-wait semaphore is the one bound to THIS acquired image, so present never waits on a semaphore a
        // later frame has already re-signalled.
        VkSemaphore PresentSignal = Substrate.RenderCompletePerImage[ImageIndex];

        VkPipelineStageFlags WaitStage = AttachedPath ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo SubmitInfo = {};
        SubmitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        SubmitInfo.waitSemaphoreCount   = 1;
        SubmitInfo.pWaitSemaphores      = &SlotAcquire;
        SubmitInfo.pWaitDstStageMask    = &WaitStage;
        SubmitInfo.commandBufferCount   = 1;
        SubmitInfo.pCommandBuffers      = &SlotCommands;
        SubmitInfo.signalSemaphoreCount = 1;
        SubmitInfo.pSignalSemaphores    = &PresentSignal;
        vkQueueSubmit(Substrate.Host.GraphicsQueue, 1, &SubmitInfo, SlotFence);

        VkPresentInfoKHR PresentInfo = {};
        PresentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        PresentInfo.waitSemaphoreCount = 1;
        PresentInfo.pWaitSemaphores    = &PresentSignal;
        PresentInfo.swapchainCount     = 1;
        PresentInfo.pSwapchains        = &Substrate.Swapchain;
        PresentInfo.pImageIndices      = &ImageIndex;
        VkResult Presented = vkQueuePresentKHR(Substrate.Host.GraphicsQueue, &PresentInfo);
        if (Presented == VK_ERROR_OUT_OF_DATE_KHR || Presented == VK_SUBOPTIMAL_KHR)
            Substrate.Window.RebuildRequested = true;

        // Advance to the next in-flight slot for the following frame.
        Substrate.FrameSlot = (Substrate.FrameSlot + 1) % WindowSubstrate::FramesInFlight;
    }
}

void FinalizeWindowSubstrate(WindowSubstrate& Substrate)
{
    if (Substrate.Host.Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(Substrate.Host.Device);

    for (int Slot = 0; Slot < WindowSubstrate::FramesInFlight; ++Slot)
    {
        if (Substrate.FrameFence[Slot] != VK_NULL_HANDLE)
            vkDestroyFence(Substrate.Host.Device, Substrate.FrameFence[Slot], Substrate.Host.Allocator);
        if (Substrate.AcquireComplete[Slot] != VK_NULL_HANDLE)
            vkDestroySemaphore(Substrate.Host.Device, Substrate.AcquireComplete[Slot], Substrate.Host.Allocator);
    }
    FinalizePresentSignalSet(Substrate);
    FinalizeImageViews(Substrate);
    if (Substrate.CommandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(Substrate.Host.Device, Substrate.CommandPool, Substrate.Host.Allocator);
    if (Substrate.Swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(Substrate.Host.Device, Substrate.Swapchain, Substrate.Host.Allocator);
    Substrate.Swapchain = VK_NULL_HANDLE;

    // 🔴 Destroy the WSI surface here: it is a child of the instance and must go before FinalizeVulkanHost destroys that
    //    instance. The substrate owns this ordering because it alone holds both the instance and the surface (validation
    //    VUID-vkDestroyInstance-instance-00629 fired while this was omitted).
    if (Substrate.Window.PresentationSurface != VK_NULL_HANDLE && Substrate.Host.Instance != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(Substrate.Host.Instance, Substrate.Window.PresentationSurface, Substrate.Host.Allocator);
        Substrate.Window.PresentationSurface = VK_NULL_HANDLE;
    }

    FinalizeVulkanHost(Substrate.Host);
    FinalizePlatformWindow(Substrate.Window);
}

int ExecuteWindowSubstrate(const char* TitleText)
{
    printf("[window] %s starting...\n", TitleText);

    WindowSubstrate Substrate;
    if (!InitializeWindowSubstrate(Substrate, TitleText, 1600, 900))
    {
        FinalizeWindowSubstrate(Substrate);
        return 1;
    }

    RunWindowSubstrate(Substrate);

    printf("[window] close requested, shutting down.\n");
    FinalizeWindowSubstrate(Substrate);
    return 0;
}

} // namespace Frontier
