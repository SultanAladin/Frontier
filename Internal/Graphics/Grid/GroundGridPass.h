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
#include "EngineContext/Math/LinearAlgebra_Float32.h"

#include <vulkan/vulkan.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Per-frame push data, byte-compatible with the GroundGridConstants block in AnalyticGroundPlane.vert / .frag. Layout order and
//    padding match the GLSL std430 push-constant rules exactly: one mat4, seven vec4 (each 16-byte aligned), then nine tightly
//    packed floats. 64 + 112 + 36 = 212 bytes (within the Pascal / GTX-1060 256-byte push-constant limit). The four-float
//    colours carry rgb + an intensity in a. The world reference frame is right-handed Z-up (CoordinateSpace.h): the grid lives
//    on the z = 0 ground plane and the dots sit on the major-line intersections, so DotSpacing is gone — the dots always
//    follow MajorSpacing. DotPixelRadius is a constant SCREEN-space radius so dots never scale with distance. The three axis
//    colours + thickness + toggle draw the origin axes: X red, Y green, Z blue.
struct GroundGridConstants
{
    Matrix4f InverseViewProjection;                                 // [-]  - Clip → world (the whole grid derives from this)
    float   CameraPosition[4]     = { 0.0f, 0.0f, 0.0f, 0.0f };     // [m]  - World eye xyz; w unused
    float   MinorLineColour[4]    = { 0.55f, 0.55f, 0.60f, 0.50f }; // [-]  - Minor line rgb + intensity
    float   MajorLineColour[4]    = { 0.80f, 0.80f, 0.85f, 0.85f }; // [-]  - Major line rgb + intensity
    float   DotColour[4]          = { 0.90f, 0.72f, 0.35f, 0.90f }; // [-]  - Dot rgb + intensity
    float   AxisLineColourX[4]    = { 0.90f, 0.20f, 0.22f, 1.00f }; // [-]  - +X axis line rgb + intensity (red)
    float   AxisLineColourY[4]    = { 0.30f, 0.80f, 0.30f, 1.00f }; // [-]  - +Y axis line rgb + intensity (green)
    float   AxisLineColourZ[4]    = { 0.25f, 0.45f, 0.95f, 1.00f }; // [-]  - +Z axis line rgb + intensity (blue)
    float   MinorSpacing          = 0.5f;                           // [m]  - Minor line spacing
    float   MajorSpacing          = 5.0f;                           // [m]  - Major line spacing (dots ride its intersections)
    float   GridExtent            = 80.0f;                          // [m]  - Fade radius — the grid grows out to here
    float   LineThickness         = 1.5f;                           // [px] - Line half-width factor (derivative-scaled)
    float   DotPixelRadius        = 2.5f;                           // [px] - Dot radius in SCREEN pixels (constant with distance)
    float   AxisLineThickness     = 2.0f;                           // [px] - Origin-axis line half-width factor
    float   LineLayerEnabled      = 1.0f;                           // [-]  - 1 draws the line grid, 0 hides it
    float   DotLayerEnabled       = 1.0f;                           // [-]  - 1 draws the dotted grid, 0 hides it
    float   AxisLayerEnabled      = 1.0f;                           // [-]  - 1 draws the origin axes, 0 hides them
};

// 📝 The pass's device-side resources, built once. Holds no per-frame state — the camera drives it purely through pushed
//    constants at record time, so one pass instance serves every frame and (if wanted) more than one camera.
struct GroundGridPass
{
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;   // [-] - Push-constant-only layout (no descriptor sets)
    VkPipeline       Pipeline       = VK_NULL_HANDLE;   // [-] - Alpha-blended fullscreen-triangle graphics pipeline
    bool             ReadyCondition = false;            // [-] - True once the pipeline built (grid drawn only when ready)
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

// Record one grid draw into an already-open dynamic-rendering scope. Sets viewport + scissor to the extent, binds the
// pipeline, pushes the constants, and draws the three-vertex fullscreen triangle. A no-op when the pass is not ready.
void RecordGroundGridPass(const GroundGridPass&      Pass,
                          VkCommandBuffer            CommandBuffer,
                          VkExtent2D                 Extent,
                          const GroundGridConstants& Constants);

// Destroy the pipeline + layout. Safe on partially-initialized state.
void FinalizeGroundGridPass(GroundGridPass& Pass, const VulkanHost& Host);

} // namespace Frontier

#endif
