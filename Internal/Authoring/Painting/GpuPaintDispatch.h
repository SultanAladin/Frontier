/*==============================================================================================================================================
                                                             GPUPAINTDISPATCH.H
==============================================================================================================================================*/
// 🧩 The Vulkan graphics path that stamps one paint dab into a texture-paint layer's Albedo image. A GpuPaintContext owns a
//    transient command pool and lazily builds ONE graphics pipeline (the PaintUvRaster vert/frag pair) plus a render pass and a
//    per-target framebuffer cache. Each RecordPaintStamp rasterizes the model into its own UV space against the layer image as a
//    colour attachment: the vertex stage lands each triangle at its UV, and the fragment stage reprojects to the on-screen brush
//    circle, testing whether each texel is under the brush and blending the brush colour over the layer (src-alpha). The stamp is
//    one-shot — a transient command buffer submitted on the graphics queue under a fence this call waits on, so the layer image is
//    fully painted on return and stays in SHADER_READ_ONLY_OPTIMAL for the surface sampler. Mirrors GpuBakeDispatch's opaque
//    struct + free-function shape; the header stays Vulkan-free (handles pass as void*). InitializeEnabled reports readiness — when
//    false RecordPaintStamp is a no-op so the caller degrades gracefully.

#pragma once
#ifndef FRONTIER_AUTHORING_PAINTING_GPUPAINTDISPATCH_H
#define FRONTIER_AUTHORING_PAINTING_GPUPAINTDISPATCH_H

#include <cstdint>

namespace Frontier
{

struct VulkanHost;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The owned graphics objects, held opaquely so this header stays free of the Vulkan headers (the .cpp pulls the full
//    implementation). One context lives beside the VulkanHost for the whole app; its render pass, pipeline, framebuffer cache,
//    and transient command pool are built at Initialize and torn down at Finalize. InitializeEnabled reports whether the device +
//    transient command pool + pipeline came up; when false RecordPaintStamp declines (the stroke silently does nothing).
struct GpuPaintContext
{
    void* OpaqueImplementation = nullptr;   // [-] - heap GpuPaintContextImplementation (owns the Vulkan objects)
    bool  InitializeEnabled    = false;     // [-] - the pipeline + transient command pool came up; RecordPaintStamp may stamp
};

// 📝 One paint dab's inputs, for ANY channel. The matrices + brush parameters ride the shader's push-constant block; the target +
//    geometry handles select the framebuffer and the mesh to rasterize. All Vulkan handles pass as void* so this header stays
//    Vulkan-free — the .cpp reinterprets them. ModelViewProjection is Projection . View . Model, pre-multiplied on the CPU so the
//    push block holds one mat4. TargetColour is what the stamp deposits: for Albedo it is the straight brush RGBA, for a scalar
//    channel the value rides component 0 (rgb = value, the coverage-alpha blend mixes the stored value toward it), for Emissive it
//    is the HDR colour. TargetFormatCode selects the render-pass / pipeline the dispatcher caches per VkFormat.
struct PaintStampInputs
{
    float    ModelViewProjection[16];   // [-]  - world -> clip for the fragment reprojection (Projection . View . Model)
    float    BrushCentre[2];            // [px] - stroke sample centre in canvas pixels
    float    BrushRadius;               // [px] - brush footprint radius on screen (brush Size / 2)
    float    Hardness;                  // [0-1]- inner solid fraction; falloff runs from Hardness*Radius to Radius
    float    Opacity;                   // [0-1]- overall stroke strength
    float    Flow;                      // [0-1]- per-stamp deposit fraction
    float    TargetColour[4];           // [-]  - what the stamp deposits (Albedo RGBA / scalar in [0] / Emissive HDR)
    float    TargetExtent[2];           // [px] - canvas width/height the reprojection maps NDC into
    void*    TargetImage;               // [-]  - VkImage: the layer channel's image, the colour attachment
    void*    TargetView;                // [-]  - VkImageView over TargetImage, the framebuffer attachment
    int      TargetFormatCode;          // [-]  - VkFormat of the target channel (selects the cached pass/pipeline)
    uint32_t TextureExtent[2];          // [px] - channel image width/height (the UV-raster viewport)
    void*    VertexBuffer;              // [-]  - VkBuffer: the specimen's RenderVertex stream (position @0, normal @12, uv @24)
    void*    IndexBuffer;               // [-]  - VkBuffer: the specimen's uint32 triangle-list indices
    uint32_t IndexCount;                // [-]  - index count to draw
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bring the paint context up against the live device: cache the graphics queue/family, create the transient command pool, load the
// PaintUvRaster SPIR-V (ShaderDirectory is the folder the .vert.spv / .frag.spv were compiled into), and build the render pass +
// graphics pipeline. Returns true (and sets Context.InitializeEnabled) when the context is ready; on any failure the context is left
// safe to Finalize and RecordPaintStamp declines. ⚠️ Initialization-time only — do not call during the main loop.
bool InitializeGpuPaintContext(GpuPaintContext& Context, VulkanHost& Device, const char* ShaderDirectory);

// Stamp one paint dab into the channel image described by Inputs (any PBR channel, selected by TargetFormatCode). Transitions the
// image COLOUR_ATTACHMENT for the pass and back to SHADER_READ_ONLY_OPTIMAL after, rasterizes the model into UV space, and blends
// the target over the layer (coverage-alpha, so scalar channels converge toward TargetColour[0]). The render pass + pipeline for the
// target's VkFormat are built lazily and cached. Synchronous — submits on the graphics queue under a fence this call waits on. A
// no-op (returns false) when the context is not initialized or Inputs is degenerate (null image/geometry, zero indices). Safe to
// call many times per frame (once per stroke sample per enabled channel).
bool RecordPaintStamp(GpuPaintContext& Context, VulkanHost& Device, const PaintStampInputs& Inputs);

// Tear down every owned graphics object in reverse creation order (waits the device idle first). Null-guarded and safe on a context
// that never initialized. ⚠️ Call before FinalizeVulkanHost.
void FinalizeGpuPaintContext(GpuPaintContext& Context, VulkanHost& Device);

} // namespace Frontier

#endif
