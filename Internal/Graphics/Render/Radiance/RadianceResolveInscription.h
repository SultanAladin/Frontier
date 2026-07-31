/*==============================================================================================================================================
                                                       RADIANCERESOLVEINSCRIPTION.H
==============================================================================================================================================*/
// 🧩 The one place linear scene radiance becomes a display colour. A fullscreen-triangle fragment unit that samples the RadianceTarget, applies
//    exposure, tone maps ONCE through Khronos PBR Neutral, and writes the result into the swapchain's already-open colour scope. Everything the
//    scene draws (sky, surface shade, and later the sun-shadowed and GI-lit surfaces) accumulates in linear HDR upstream of this; this unit is the
//    single exit from that space.
//
// 📝 THE ORDERING CONTRACT this unit defines — the frame is now TWO colour scopes, not one:
//
//        radiance scope (R16G16B16A16_SFLOAT)          swapchain scope (_SRGB)
//        ├─ sky dome      -> linear radiance           ├─ THIS RESOLVE   (tone map, first draw)
//        └─ surface shade -> linear radiance           ├─ ground grid    ┐
//           (alpha-over blends in LINEAR)              ├─ selection outline │ authored display-referred
//                        │                             ├─ component overlay │ colours, NOT tone mapped
//                        └── TransitionForSampling ──> └─ clipmap lattice  ┘
//
//    The overlays draw AFTER the resolve, straight onto the swapchain, so a hand-picked handle colour reaches the screen exactly as authored. Tone
//    mapping them would desaturate and roll off precisely the saturated cues a user relies on to read the viewport.
//
// ⚠️ This unit must be the FIRST draw in the swapchain scope, and the scope's loadOp must be DONT_CARE or CLEAR — the resolve writes every pixel
//    with blending DISABLED (it replaces, it does not composite). Recording it after an overlay would erase that overlay.
// ⚠️ Reuses VisibilityInscription.vert.spv for the fullscreen triangle rather than carrying a second identical copy.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDER_RADIANCE_RADIANCERESOLVEINSCRIPTION_H
#define FRONTIER_GRAPHICS_RENDER_RADIANCE_RADIANCERESOLVEINSCRIPTION_H

#include "Graphics/Render/Radiance/RadianceTarget.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 OperatorIndex values, matching the branch in RadianceResolve.frag. The bypass is not dead weight: it is the only way to see the raw linear
//    buffer clamped, which is how you tell "the curve is wrong" apart from "the radiance feeding it is wrong".
constexpr uint32_t RadianceOperatorPbrNeutral = 0;   // [-] - Khronos PBR Neutral, the standard path
constexpr uint32_t RadianceOperatorBypass     = 1;   // [-] - linear clamp to [0,1], for A/B inspection

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block the resolve fragment stage reads, byte-compatible with the ResolveConstants block in RadianceResolve.frag.
//
//    Exposure closes the fourth tonemapping defect: the surface path previously had NO exposure control, so the only brightness knob was the light
//    intensity — and turning that up silently moved the tone map's operating point, changing the curve's shape rather than just the level. Exposure
//    is a clean pre-curve multiplier: it slides radiance along the curve without redefining it.
struct RadianceResolveConstants
{
    float    Exposure      = 1.0f;                         // [-] - linear multiplier applied BEFORE the curve; 1.0 leaves radiance untouched
    uint32_t OperatorIndex = RadianceOperatorPbrNeutral;   // [-] - RadianceOperator* selector
    uint32_t Pad0          = 0;                            // [-] - std430 push tail pad
    uint32_t Pad1          = 0;                            // [-] - std430 push tail pad
};

// 📝 The resolve's owned device resources. Pipeline + layout, a one-binding set pointing at the radiance view, and a linear-filter sampler.
//
//    The sampler is LINEAR rather than the point sampler the visibility units use, and that is deliberate but currently inert: the shader fetches
//    with texelFetch (a 1:1 blit needs no filtering), so the filter mode is never exercised today. It is linear so that a future scaled resolve —
//    render at 70% and upscale, or a downsampled bloom read — does not need the sampler rebuilt. A filtered RADIANCE is a legitimate blend of two
//    light values, unlike a filtered IDENTITY which is a nonexistent triangle; that is why the choice differs from SurfaceShadeInscription's.
//
//    The radiance view is BORROWED; Refresh (re)points the set at it and must run again after every resize, because ReconfigureRadianceTarget
//    rebuilds the view and the old handle goes stale.
struct RadianceResolveInscription
{
    VulkanHost*           Host           = nullptr;          // [-] - not owned; supplies device / allocator
    VkPipeline            Pipeline       = VK_NULL_HANDLE;   // [-] - fullscreen-triangle resolve pipeline (dynamic rendering, blend DISABLED)
    VkPipelineLayout      PipelineLayout = VK_NULL_HANDLE;   // [-] - one set + the ResolveConstants push range
    VkDescriptorSetLayout SetLayout      = VK_NULL_HANDLE;   // [-] - set 0: b0 = radiance sampler
    VkDescriptorPool      DescriptorPool = VK_NULL_HANDLE;   // [-] - pool sized for one resolve set
    VkDescriptorSet       ResolveSet     = VK_NULL_HANDLE;   // [-] - the bound set
    VkSampler             LinearSampler  = VK_NULL_HANDLE;   // [-] - linear / clamp; see the note above on why not nearest

    VkImageView           BoundRadianceView = VK_NULL_HANDLE; // [-] - the view b0 currently points at; Refresh writes only on change
    bool                  ReadyCondition    = false;          // [-] - true once pipeline + layout + descriptors are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the pipeline, layout, descriptor set layout, pool, set (left unpointed until Refresh), and the linear sampler. SwapchainFormat is the
// _SRGB swapchain format the dynamic-rendering pipeline writes into — NOT RadianceTargetFormat, which is what it reads FROM. Reads
// VisibilityInscription.vert.spv and RadianceResolve.frag.spv from ShaderDirectory. Returns false (ReadyCondition stays false) if dynamic
// rendering is unavailable, a shader is missing, or any Vulkan step fails. Pair with Finalize.
bool InitializeRadianceResolveInscription(RadianceResolveInscription& Resolve,
                                          VulkanHost&                 Host,
                                          VkFormat                    SwapchainFormat,
                                          const char*                 ShaderDirectory);

// Point the descriptor set at the borrowed radiance target's colour view. Call once everything is first ready and again after every resize
// (ReconfigureRadianceTarget rebuilds the view). A no-op when either side is not ready. The device must be idle — an in-flight frame may still read
// the set. Cheap: one vkUpdateDescriptorSets, and only when the view handle actually changed.
void RefreshRadianceResolveInscription(RadianceResolveInscription& Resolve, const RadianceTarget& Target);

// Record the resolve into an already-open swapchain colour scope: set viewport + scissor, bind the pipeline + set, push the constants, and draw the
// three-vertex fullscreen triangle. The radiance target must already be in SHADER_READ_ONLY (see TransitionRadianceTargetForSampling). A no-op when
// not ready. ⚠️ CommandBuffer must be INSIDE the swapchain colour scope, and this must be the scope's FIRST draw.
void RecordRadianceResolveInscription(const RadianceResolveInscription& Resolve,
                                      VkExtent2D                        Extent,
                                      const RadianceResolveConstants&   Constants,
                                      VkCommandBuffer                   CommandBuffer);

// Destroy the pipeline, layout, descriptor plumbing, and sampler, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeRadianceResolveInscription(RadianceResolveInscription& Resolve);

} // namespace Frontier

#endif
