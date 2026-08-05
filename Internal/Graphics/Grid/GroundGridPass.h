/*==============================================================================================================================================
                                                              GROUNDGRIDPASS.H
==============================================================================================================================================*/
// 🧩 A modular GPU pass that draws a Blender-style analytic ground grid — minor + major lines and a dotted layer — reconstructed entirely from
//    the camera's inverse view-projection (no vertex buffers, one fullscreen triangle). Built once (pipeline + layout + shader modules), then
//    recorded once per frame into an open dynamic-rendering scope with the per-frame GroundGridConstants pushed in. Every visual knob (spacing,
//    colour, thickness, extent, layer toggles) lives in the push struct so a UI could steer it later — this pass hooks no UI itself. It serves
//    any camera and any swapchain colour format handed to it at initialize; nothing here is bound to one host.

#pragma once
#ifndef FRONTIER_GRAPHICS_GRID_GROUNDGRIDPASS_H
#define FRONTIER_GRAPHICS_GRID_GROUNDGRIDPASS_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Visibility/VisibilityDepth.h"
#include "EngineContext/Math/LinearAlgebra_Float32.h"

#include <vulkan/vulkan.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Per-frame push data, byte-compatible with the GroundGridConstants block in AnalyticGroundPlane.vert / .frag. Layout order and
//    padding match the GLSL std430 push-constant rules exactly: one mat4, eight vec4 (each 16-byte aligned), then eleven tightly
//    packed floats. 64 + 128 + 44 = 236 bytes (within the Pascal / GTX-1060 256-byte push-constant limit). The four-float
//    colours carry rgb + an intensity in a. The world reference frame is right-handed Z-up (CoordinateSpace.h): the grid lives
//    on the z = 0 ground plane and the dots sit on the major-line intersections, so DotSpacing is gone — the dots always
//    follow MajorSpacing. DotPixelRadius is a constant SCREEN-space radius so dots never scale with distance. The three axis
//    colours + thickness + toggle draw the origin axes: X red, Y green, Z blue.
//    GridExtent and FadeSharpness together shape the radial dissolve: the extent is WHERE it ends, the sharpness is HOW fast it
//    gets there. A sharpness of 1 fades perfectly linearly out to the extent; 2 (the default) squares the falloff, which reads
//    as the grid thinning out well before the extent. UI that exposes "how far the grid reaches" should drive both.
struct GroundGridConstants
{
    Matrix4f InverseViewProjection;                                 // [-]  - Clip → world (the whole grid derives from this)
    float   CameraPosition[4]     = { 0.0f, 0.0f, 0.0f, 0.0f };     // [m]  - World eye xyz; w unused
    float   FocalCentre[4]        = { 0.0f, 0.0f, 0.0f, 0.0f };     // [m]  - Orbit Target xyz — radial fade origin; w unused
    float   MinorLineColour[4]    = { 0.55f, 0.55f, 0.60f, 0.50f }; // [-]  - Minor line rgb + intensity
    float   MajorLineColour[4]    = { 0.80f, 0.80f, 0.85f, 0.85f }; // [-]  - Major line rgb + intensity
    float   DotColour[4]          = { 1.00f, 1.00f, 1.00f, 0.90f }; // [-]  - Dot rgb + intensity (neutral white)
    float   AxisLineColourX[4]    = { 0.90f, 0.20f, 0.22f, 1.00f }; // [-]  - +X axis line rgb + intensity (red)
    float   AxisLineColourY[4]    = { 0.30f, 0.80f, 0.30f, 1.00f }; // [-]  - +Y axis line rgb + intensity (green)
    float   AxisLineColourZ[4]    = { 0.25f, 0.45f, 0.95f, 1.00f }; // [-]  - +Z axis line rgb + intensity (blue)
    float   MinorSpacing          = 0.5f;                           // [m]  - Minor line spacing
    float   MajorSpacing          = 5.0f;                           // [m]  - Major line spacing (dots ride its intersections)
    float   GridExtent            = 80.0f;                          // [m]  - Fade radius — the grid grows out to here
    float   FadeSharpness         = 2.0f;                           // [-]  - Falloff exponent: 1 linear, higher fades in closer
    float   LineThickness         = 1.5f;                           // [px] - Line half-width factor (derivative-scaled)
    float   DotPixelRadius        = 2.5f;                           // [px] - Dot radius in SCREEN pixels (constant with distance)
    float   AxisLineThickness     = 2.0f;                           // [px] - Origin-axis line half-width factor
    float   LineLayerEnabled      = 1.0f;                           // [-]  - 1 draws the line grid, 0 hides it
    float   DotLayerEnabled       = 1.0f;                           // [-]  - 1 draws the dotted grid, 0 hides it
    float   AxisLayerEnabled      = 1.0f;                           // [-]  - 1 draws the origin axes, 0 hides them
    float   OrthographicEnabled   = 0.0f;                           // [-]  - 1 = parallel projection (see the ray note below)
    float   DepthNearPlane        = 0.1f;                           // [m]  - MUST match the frame's projection near plane (see below)
    float   DepthTestEnabled      = 1.0f;                           // [-]  - 1 occludes the grid behind scene geometry, 0 draws over all
};
// 236 + 8 = 244 bytes, still inside the 256-byte push budget — but only 12 bytes of slack remain. The next knob added here needs a
// descriptor-backed uniform block instead, not another push float.

// 🔴 DepthNearPlane is not decorative and must not be hardcoded: the shader both linearizes the sampled depth AND scales its own ray
//    parameter with it, so a value that disagrees with the projection matrix mis-scales BOTH sides of the occlusion compare. The
//    failure is silent and asymmetric — too small hides the grid entirely, too large lets it draw through everything, i.e. it
//    degrades straight back to the bug this test exists to fix. AssembleGridConstants fills it from Camera.NearPlane.

// 🔴 The lens flag is not cosmetic: the two projections need structurally DIFFERENT rays. Perspective rays fan out from the single
//    eye; parallel rays share one direction with origins spread across the near plane. Feeding a perspective ray set through an
//    orthographic matrix stretches and shears the grid, so AssembleGridConstants must set this from Camera.Projection. Also fill
//    FocalCentre with the orbit Target — under ortho the eye can sit arbitrarily far out without changing the framing, so the
//    radial fade keys off the view centre instead.

// 📝 The pass's device-side resources, built once. Holds no per-frame state beyond the depth view it currently points at — the camera
//    drives it purely through pushed constants at record time, so one pass instance serves every frame and (if wanted) more than one
//    camera. The descriptor set exists solely to read the scene depth for the occlusion test; the grid still writes no depth itself.
struct GroundGridPass
{
    VkPipelineLayout      PipelineLayout = VK_NULL_HANDLE;   // [-] - Push constants + the depth set
    VkPipeline            Pipeline       = VK_NULL_HANDLE;   // [-] - Alpha-blended fullscreen-triangle graphics pipeline
    VkDescriptorSetLayout SetLayout      = VK_NULL_HANDLE;   // [-] - set 0: b0 = scene depth sampler
    VkDescriptorPool      DescriptorPool = VK_NULL_HANDLE;   // [-] - single-set pool for DepthSet
    VkDescriptorSet       DepthSet       = VK_NULL_HANDLE;   // [-] - points at the renderer-owned scene depth view
    VkSampler             PointSampler   = VK_NULL_HANDLE;   // [-] - NEAREST + clamp; the shader texelFetches, so filtering is inert
    VkImageView           BoundDepthView = VK_NULL_HANDLE;   // [-] - view DepthSet currently references; Refresh rewrites only on change
    // 🔴 A 1x1 D32 placeholder the set is written to at init, BEFORE the real depth target exists. The fragment shader statically declares the
    //    set-0 depth sampler, so every draw must bind a set whose binding 0 is not just BOUND but WRITTEN (VUID-vkCmdDraw-None-08114 checks static
    //    shader use, ignoring the DepthTestEnabled runtime guard). Refresh swaps DepthSet to the real view once it arrives; this owned image keeps
    //    binding 0 valid during the startup frames in between. Never sampled — DepthTestEnabled is 0 while BoundDepthView is the placeholder.
    VkImage               PlaceholderImage  = VK_NULL_HANDLE; // [-] - 1x1 D32 stand-in so binding 0 is always a written descriptor
    VkDeviceMemory        PlaceholderMemory = VK_NULL_HANDLE; // [-] - backing allocation for PlaceholderImage
    VkImageView           PlaceholderView   = VK_NULL_HANDLE; // [-] - depth-aspect view of the placeholder; the init descriptor write targets it
    bool                  ReadyCondition = false;            // [-] - True once the pipeline built (grid drawn only when ready)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the pipeline + layout for the given surface colour format (the dynamic-rendering attachment format). Loads the two
// SPIR-V modules from ShaderDirectory. Returns false (ReadyCondition left false) if the device lacks dynamic rendering or a
// module is missing; the caller then simply skips the grid draw.
bool InitializeGroundGridPass(GroundGridPass& Pass,
                              const VulkanHost& Host,
                              VkFormat          ColourFormat,
                              const char*       ShaderDirectory);

// Point the depth set at Depth's view. Idempotent — a no-op when the view already matches BoundDepthView, so this is safe to call
// every frame; it only writes the descriptor after a resize rebuilt the depth image. Must be called at least once before recording
// with DepthTestEnabled set, and again after any ReconfigureVisibilityDepth. The device must be idle when the view actually changes.
void RefreshGroundGridPass(GroundGridPass& Pass, const VulkanHost& Host, const VisibilityDepth& Depth);

// Record one grid draw into an already-open dynamic-rendering scope. Sets viewport + scissor to the extent, binds the
// pipeline, pushes the constants, and draws the three-vertex fullscreen triangle. A no-op when the pass is not ready.
// ⚠️ The scene depth must already be in SHADER_READ_ONLY and hold this frame's contents. Recording with DepthTestEnabled while the
//    depth is unwritten samples undefined memory and occludes the grid by garbage; the caller owns that ordering.
void RecordGroundGridPass(const GroundGridPass&      Pass,
                          VkCommandBuffer            CommandBuffer,
                          VkExtent2D                 Extent,
                          const GroundGridConstants& Constants);

// Destroy the pipeline + layout, set layout, pool, and sampler. Safe on partially-initialized state.
void FinalizeGroundGridPass(GroundGridPass& Pass, const VulkanHost& Host);

} // namespace Frontier

#endif
