/*==============================================================================================================================================
                                                   PARAMETRICSKETCHCURVERASTERIZATION.H
==============================================================================================================================================*/
// 🧩 The thick-line outline rasterization for the parametric-sketch preview: a purpose-built Vulkan graphics pipeline that rasterizes a shape's
//    OUTLINE polyline as a screen-constant-width ribbon composited ON TOP of the matcap solid + grid. It is the outline peer of the matcap surface
//    unit — same shape (one camera UBO at set 0, a stride-32 RenderVertex input, records one body per call so the caller loops) — but three things
//    differ: it carries a PUSH CONSTANT (half stroke width in px + the viewport extent + the body colour + linetype), it BLENDS (straight alpha, so
//    anti-aliased ribbon edges and per-body colour composite over the transparent target), and it packs the RenderVertex fields with a bespoke
//    thick-line meaning (position = endpoint A, normal = partner endpoint B, texcoord = (SideSign, ArcLength)). The CPU expands each polyline segment
//    into a quad of four RenderVertices, so the ordinary ConstructPolygonBufferAllocation upload path is reused unchanged — no new vertex struct, no
//    new uploader. Modular, not standalone: pipeline / layout built once at bring-up; RecordParametricSketchCurveInto rasterizes ONE stroke body per
//    call. Raw Vulkan, no VMA. Wired onto the shared VulkanHost. Guarded by FRONTIER_PARAMETRIC_SKETCH at the sequence level.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDER_SURFACE_PARAMETRICSKETCHCURVERASTERIZATION_H
#define FRONTIER_GRAPHICS_RENDER_SURFACE_PARAMETRICSKETCHCURVERASTERIZATION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Render/Resources/BufferAllocation.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The per-frame camera block bound at set 0 binding 0 — byte-for-byte identical to the matcap surface camera block so an outline registers 1:1
//    with the matcap solid it overlays (same ViewProjection, same layout, no transpose). ViewMatrix is unused by the curve shaders but kept so the
//    block matches the matcap block exactly (one camera-fill path can feed both).
struct ParametricSketchCurveCameraBlock
{
    float ViewProjection[16] = {};   // [-] - world -> clip (orbit / ortho camera), column-major
    float ViewMatrix[16]     = {};   // [-] - world -> camera (unused by the curve shaders; kept for block parity with the matcap camera)
};

// 📝 The per-record push block matching the StrokeConstants push_constant the two curve shaders share. The vertex stage reads HalfWidthPixels +
//    the viewport extent; the fragment stage reads StrokeColour + the Phase-4 linetype fields. Field order + size MUST match the GLSL block. 96
//    bytes: four leading floats (16), a vec4 colour (16), four trailing floats (16) — within the 128-byte push-constant floor every device grants.
struct ParametricSketchStrokeConstants
{
    float HalfWidthPixels = 1.5f;                        // [px] - half the ribbon width; constant on screen at any zoom
    float ViewportWidth   = 1.0f;                        // [px] - target width  (NDC -> pixel scale)
    float ViewportHeight  = 1.0f;                        // [px] - target height (NDC -> pixel scale)
    float Padding         = 0.0f;                        // [-]  - aligns StrokeColour to 16 bytes
    float StrokeColour[4] = { 0.1f, 0.1f, 0.1f, 1.0f };  // [-]  - linear RGBA of this body (dark ink by default)
    float DashPeriodMm    = 0.0f;                        // [mm] - Phase 4 linetype (0 = solid)
    float DashDutyCycle   = 0.5f;                        // [-]  - Phase 4 linetype
    float LineStyle       = 0.0f;                        // [-]  - Phase 4 linetype (0 solid / 1 dashed / 2 centerline)
    float Reserved        = 0.0f;                        // [-]  - tail alignment
};

// 📝 One resident thick-line pipeline: the camera set layout + pipeline layout (with the stroke push range) + graphics pipeline built once, the two
//    shader modules kept for teardown, and the camera UBO + its set. The camera UBO is a single host-visible buffer this owns + maps persistently
//    (rewritten each frame). ReadyStatus gates recording — a partial build leaves every handle null and the outline overlay simply doesn't draw.
//    Host is borrowed (not owned).
struct ParametricSketchCurveRasterization
{
    VulkanHost*           Host            = nullptr;          // [-] - not owned; supplies device / queue / family / physical device

    VkShaderModule        VertModule      = VK_NULL_HANDLE;   // [-] - ParametricSketchCurve.vert.spv
    VkShaderModule        FragModule      = VK_NULL_HANDLE;   // [-] - ParametricSketchCurve.frag.spv

    VkDescriptorSetLayout CameraSetLayout = VK_NULL_HANDLE;   // [-] - set 0: one uniform buffer, vertex stage
    VkPipelineLayout      PipelineLayout  = VK_NULL_HANDLE;   // [-] - { CameraSetLayout } + one push range (vertex | fragment)
    VkPipeline            Pipeline        = VK_NULL_HANDLE;   // [-] - the thick-line graphics pipeline (blend on, depth test on)

    VkBuffer              CameraBuffer    = VK_NULL_HANDLE;   // [-] - host-visible UBO holding ParametricSketchCurveCameraBlock
    VkDeviceMemory        CameraMemory    = VK_NULL_HANDLE;   // [-] - backing allocation (persistently mapped)
    void*                 CameraMapped    = nullptr;          // [-] - persistent map (write the block, no flush — coherent)

    VkDescriptorPool      DescriptorPool  = VK_NULL_HANDLE;   // [-] - vends the camera set
    VkDescriptorSet       CameraSet       = VK_NULL_HANDLE;   // [-] - set 0 (bound to CameraBuffer)

    bool                  ReadyStatus     = false;            // [-] - true once pipeline + camera set are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the thick-line pipeline (camera set layout, pipeline layout with the stroke push range, graphics pipeline against RenderPass subpass 0),
// the camera UBO + its set. The pipeline expects a stride-32 RenderVertex input (position @0 = endpoint A, normal @12 = partner B, texcoord @24 =
// (SideSign, ArcLength)), straight-alpha blend, and depth test LESS_OR_EQUAL against ParametricSketchViewTarget's D32_SFLOAT depth. Returns false
// (ReadyStatus stays false, every handle null) on a missing .spv / any Vulkan failure — the caller logs and skips the outline overlay. RenderPass is
// the ParametricSketchViewTarget's RenderPass. Pair with the Finalize below.
bool InitializeParametricSketchCurveRasterization(ParametricSketchCurveRasterization& Rasterization,
                                                  VulkanHost&                         Host,
                                                  const char*                         VertSpvPath,
                                                  const char*                         FragSpvPath,
                                                  VkRenderPass                        RenderPass);

// Write Camera into the per-frame UBO (once per frame, before recording). No-op when not ready. Coherent memory — no flush needed.
void RefreshParametricSketchCurveCamera(ParametricSketchCurveRasterization& Rasterization, const ParametricSketchCurveCameraBlock& Camera);

// Record ONE stroke body into the (already-begun) render pass on CommandBuffer: bind the pipeline + camera set, push Constants (width in px is fixed;
// the viewport extent is filled here from Width/Height so the ribbon is constant pixel width), set the viewport / scissor to Width x Height, bind
// Body's vertex + index buffers, and draw its indices. The caller must have begun the view target's render pass and refreshed the camera this frame;
// the caller loops this over every visible stroke body, then ends the pass. No-op when not ready or Body is empty (IndexCount == 0).
void RecordParametricSketchCurveInto(ParametricSketchCurveRasterization&    Rasterization,
                                     VkCommandBuffer                        CommandBuffer,
                                     const PolygonBufferAllocation&         Body,
                                     const ParametricSketchStrokeConstants& Constants,
                                     uint32_t                               Width,
                                     uint32_t                               Height);

// Destroy the pipeline, layout, shader modules, camera buffer, and descriptor pool, then reset to empty. The device must be idle. Safe on a
// never-initialized value.
void FinalizeParametricSketchCurveRasterization(ParametricSketchCurveRasterization& Rasterization);

} // namespace Frontier

#endif
