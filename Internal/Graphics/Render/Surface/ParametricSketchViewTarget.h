/*==============================================================================================================================================
                                                        PARAMETRICSKETCHVIEWTARGET.H
==============================================================================================================================================*/
// 🧩 The lean offscreen colour+depth target the parametric-sketch solid-preview inscription renders into, then hands to ImGui as an image. Just
//    TWO attachments — colour (B8G8R8A8_UNORM, COLOR_ATTACHMENT | SAMPLED) and depth (D32_SFLOAT, transient) — against a single-subpass render
//    pass whose colour attachment finishes in SHADER_READ_ONLY so the same frame's ImGui pass can sample it. Raw Vulkan, no VMA, mirroring the
//    engine's image + ImGui_ImplVulkan_AddTexture idiom. One target per preview pane; recreate it when the pane changes size. Wired onto VulkanHost.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDER_SURFACE_PARAMETRICSKETCHVIEWTARGET_H
#define FRONTIER_GRAPHICS_RENDER_SURFACE_PARAMETRICSKETCHVIEWTARGET_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include "imgui.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One preview target: the colour image sampled by ImGui (its own view + sampler + a backend descriptor), a transient depth image, the render
//    pass + framebuffer they share, and the live extent. ReadyStatus gates recording + display — a partial build leaves every handle null and the
//    pane draws its placeholder. Host is borrowed (not owned). The render pass is created once at bring-up; the images / framebuffer / descriptor
//    are rebuilt whenever the pane resizes (ReconfigureParametricSketchViewTarget).
struct ParametricSketchViewTarget
{
    VulkanHost*     Host          = nullptr;          // [-]  - not owned; supplies device / queue / family / physical device

    VkRenderPass    RenderPass    = VK_NULL_HANDLE;   // [-]  - single subpass: colour (-> SHADER_READ_ONLY) + depth (transient)
    VkSampler       Sampler       = VK_NULL_HANDLE;   // [-]  - linear / clamp, samples the colour image for ImGui

    VkImage         ColorImage    = VK_NULL_HANDLE;   // [-]  - B8G8R8A8_UNORM, COLOR_ATTACHMENT | SAMPLED, device-local
    VkDeviceMemory  ColorMemory   = VK_NULL_HANDLE;   // [-]  - backing allocation for ColorImage
    VkImageView     ColorView     = VK_NULL_HANDLE;   // [-]  - colour view (framebuffer attachment 0 + ImGui source)

    VkImage         DepthImage    = VK_NULL_HANDLE;   // [-]  - D32_SFLOAT depth-stencil attachment, transient
    VkDeviceMemory  DepthMemory   = VK_NULL_HANDLE;   // [-]  - backing allocation for DepthImage
    VkImageView     DepthView     = VK_NULL_HANDLE;   // [-]  - depth view (framebuffer attachment 1)

    VkFramebuffer   Framebuffer   = VK_NULL_HANDLE;   // [-]  - { ColorView, DepthView } against RenderPass
    VkDescriptorSet Descriptor    = VK_NULL_HANDLE;   // [-]  - ImGui backend texture for ImGui::Image (samples ColorView)

    uint32_t        Width         = 0;                // [px] - live colour/depth extent width
    uint32_t        Height        = 0;                // [px] - live colour/depth extent height
    bool            ReadyStatus   = false;            // [-]  - true once render pass + images + framebuffer + descriptor are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the render pass + sampler (size-independent) and the first set of images / framebuffer / descriptor at Width x Height. Returns false
// (ReadyStatus stays false, every handle null) on any Vulkan failure. Host must be provisioned and the ImGui Vulkan backend live (the colour
// image is registered with it). Pair with FinalizeParametricSketchViewTarget.
bool InitializeParametricSketchViewTarget(ParametricSketchViewTarget& Target, VulkanHost& Host, uint32_t Width, uint32_t Height);

// Resize to Width x Height: tear down the images / framebuffer / descriptor and rebuild them (the render pass + sampler persist). A no-op when
// the extent already matches (returns true) or when either dimension is zero (returns false, leaving the target unchanged). The device must be
// idle, or no in-flight frame may still sample the old colour image.
bool ReconfigureParametricSketchViewTarget(ParametricSketchViewTarget& Target, uint32_t Width, uint32_t Height);

// Destroy the framebuffer, images, views, descriptor, sampler, and render pass, then reset to empty. The device must be idle. Safe on a
// never-initialized value.
void FinalizeParametricSketchViewTarget(ParametricSketchViewTarget& Target);

} // namespace Frontier

#endif
