/*==============================================================================================================================================
                                                       SKETCHMODELOFFSCREENSURFACE.H
==============================================================================================================================================*/
// 🧩 An offscreen colour target the renderer draws into and ImGui then samples as a texture. This is the seam that lets a GPU pass appear INSIDE
//    an ImGui viewport panel: one VkImage (colour attachment + sampled) with its view + sampler, registered once with the ImGui Vulkan backend so
//    the panel can blit it through ViewportPanelState.RenderedTexture. Recording wraps the caller's draw in the two layout transitions and the
//    dynamic-rendering scope the pass expects — colour-attachment while drawing, shader-read-only while ImGui samples. The surface re-creates
//    itself when the panel's pixel extent changes, so the grid stays pixel-exact on resize. App-local for now; it moves into Internal/Graphics
//    once a second application needs the same target (the matcap phase will).

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELOFFSCREENSURFACE_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELOFFSCREENSURFACE_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include "imgui.h"

#include <cstdint>

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The offscreen target's device resources. Extent is what the image was actually built at, so the caller can compare it
//    against the panel's current pixel size and ask for a rebuild. DescriptorSet is the ImGui-side handle (an ImTextureID in
//    disguise) the panel samples; it is registered once per image and released whenever the image is rebuilt.
struct SketchModelOffscreenSurface
{
    VkImage         Image          = VK_NULL_HANDLE;   // [-]  - Colour target (COLOR_ATTACHMENT | SAMPLED)
    VkDeviceMemory  Memory         = VK_NULL_HANDLE;   // [-]  - Device-local backing store
    VkImageView     View           = VK_NULL_HANDLE;   // [-]  - Single-mip colour view the attachment + sampler bind
    VkSampler       Sampler        = VK_NULL_HANDLE;   // [-]  - Linear clamp sampler ImGui reads through
    VkDescriptorSet DescriptorSet  = VK_NULL_HANDLE;   // [-]  - ImGui backend texture handle (cast to ImTextureID to draw)
    VkFormat        Format         = VK_FORMAT_B8G8R8A8_UNORM; // [-] - Colour format (matches the pass's pipeline format)
    uint32_t        Width          = 0;                // [px] - Built width
    uint32_t        Height         = 0;                // [px] - Built height
    bool            ShaderReadable = false;            // [-]  - True once the first record left it in SHADER_READ_ONLY
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Build (or rebuild) the surface at the requested pixel extent. Destroys any previous image/view/sampler/descriptor first, so
//    the same struct can be resized in place. Returns false with the surface left safely finalizable on any failure.
bool InitializeSketchModelOffscreenSurface(SketchModelOffscreenSurface& Surface,
                                           const Frontier::VulkanHost&  Host,
                                           uint32_t                     Width,
                                           uint32_t                     Height,
                                           VkFormat                     Format);

// 📝 True when the surface is missing or was built at a different extent — the caller then rebuilds before recording.
bool QuerySketchModelOffscreenRebuildRequired(const SketchModelOffscreenSurface& Surface,
                                              uint32_t                           Width,
                                              uint32_t                           Height);

// 📝 The ImGui texture handle for the panel, or 0 when the surface holds nothing samplable yet.
ImTextureID ResolveSketchModelOffscreenTexture(const SketchModelOffscreenSurface& Surface);

// 📝 Transition to colour-attachment, open a dynamic-rendering scope cleared to ClearColour, invoke Record (which issues the
//    pass draws), close the scope, then transition to shader-read-only for ImGui. A no-op when the surface is not built.
void RecordSketchModelOffscreenSurface(SketchModelOffscreenSurface& Surface,
                                       const Frontier::VulkanHost&  Host,
                                       VkCommandBuffer              CommandBuffer,
                                       const float                  ClearColour[4],
                                       void                       (*Record)(VkCommandBuffer, VkExtent2D, void*),
                                       void*                        RecordContext);

// 📝 Destroy every device resource. Safe on a partially-initialized surface; safe to call twice.
void FinalizeSketchModelOffscreenSurface(SketchModelOffscreenSurface& Surface, const Frontier::VulkanHost& Host);

}   // namespace SketchModelViewportValidation

#endif
