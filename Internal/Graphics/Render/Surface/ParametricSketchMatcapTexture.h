/*==============================================================================================================================================
                                                       PARAMETRICSKETCHMATCAPTEXTURE.H
==============================================================================================================================================*/
// 🧩 The chrome matcap image the parametric-sketch solid-preview inscription samples: a PNG decoded once (stb_image, CPU) into a device-local
//    sampled image with its own view + linear/clamp sampler, ready to bind at set 1 of ParametricSketchSurfaceInscription. There is NO ImGui
//    descriptor here — the image is sampled by a CUSTOM pipeline, not drawn by ImGui — so the inscription writes it into its own descriptor set.
//    Raw Vulkan, no VMA, against the shared VulkanHost. One texture serves every loft body; decode it once at bring-up.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDER_SURFACE_PARAMETRICSKETCHMATCAPTEXTURE_H
#define FRONTIER_GRAPHICS_RENDER_SURFACE_PARAMETRICSKETCHMATCAPTEXTURE_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One resident matcap image: the device-local image + its backing memory + a colour view + a linear/clamp sampler, all owned. ReadyStatus
//    gates use — a failed decode / upload leaves every handle null and the surface inscription degrades to a flat clear. Left in SHADER_READ_ONLY
//    layout after upload, so the inscription binds it directly. Host is borrowed (not owned).
struct ParametricSketchMatcapTexture
{
    VulkanHost*     Host         = nullptr;          // [-]  - not owned; supplies device / queue / family / physical device
    VkImage         Image        = VK_NULL_HANDLE;   // [-]  - device-local R8G8B8A8_UNORM sampled image
    VkDeviceMemory  Memory       = VK_NULL_HANDLE;   // [-]  - backing allocation for Image
    VkImageView     View         = VK_NULL_HANDLE;   // [-]  - colour view bound at set 1 binding 0
    VkSampler       Sampler      = VK_NULL_HANDLE;   // [-]  - linear filter, clamp-to-edge
    uint32_t        Width        = 0;                // [px] - decoded image width
    uint32_t        Height       = 0;                // [px] - decoded image height
    bool            ReadyStatus  = false;            // [-]  - true once the image is resident + the sampler is live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Decode PngPath (stb_image, forced RGBA8), stage it into a device-local sampled image, and build a linear/clamp sampler. Returns false
// (ReadyStatus stays false, every handle null) on a missing file / decode failure / any Vulkan failure — the caller logs and continues with a
// flat preview. Host must already be provisioned. Pair with FinalizeParametricSketchMatcapTexture.
bool InitializeParametricSketchMatcapTexture(ParametricSketchMatcapTexture& Texture, VulkanHost& Host, const char* PngPath);

// Destroy the sampler, view, image, and memory, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeParametricSketchMatcapTexture(ParametricSketchMatcapTexture& Texture);

} // namespace Frontier

#endif
