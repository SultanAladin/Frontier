/*==============================================================================================================================================
                                                        PARAMETRICSKETCHCURVESEQUENCE.H
==============================================================================================================================================*/
// 🧩 The shared GPU consumer that rasterizes the parametric-sketch's shape OUTLINES into the canvas as screen-constant-width strokes, camera-locked.
//    It is the outline peer of ParametricSketchSolidSequence: it owns the whole lean chain — an offscreen colour+depth target, the thick-line curve
//    pipeline, an upload pool, and the device-local resident stroke geometry — and drives it each frame off the ParametricSketchSceneView + the
//    stroke-body bridge the active view publishes (RetrieveParametricSketchStrokeBodies). An ORDERED multi-step chain (Synchronize → RecordInto →
//    PublishImage → Finalize) — hence the …Sequence suffix. Both the standalone sketch build and the full editor drive the SAME struct + free
//    functions, so the GPU-outline path is one source of truth (no drift). The result is published through the stroke-image reverse bridge
//    (RegisterParametricSketchStrokeImage) and the view draws it LAST, on top of the fill → solid → grid layers. Modular, not standalone: one
//    sequence per app, the caller threads the four calls (Synchronize before encode, RecordInto inside the pre-pass, PublishImage before the paint,
//    Finalize at exit). Wired onto the shared VulkanHost. Guarded by FRONTIER_PARAMETRIC_SKETCH.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDER_SURFACE_PARAMETRICSKETCHCURVESEQUENCE_H
#define FRONTIER_GRAPHICS_RENDER_SURFACE_PARAMETRICSKETCHCURVESEQUENCE_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Render/Surface/ParametricSketchCurveRasterization.h"
#include "Graphics/Render/Surface/ParametricSketchViewTarget.h"
#include "Graphics/Render/Resources/BufferAllocation.h"
#include "Authoring/ParametricAuthoring/ParametricSketchShapeStore.h"   // 📝 ParametricSketchSceneView + the stroke-body / stroke-image bridges consumed + published

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One resident stroke body: the ParametricSketchStrokeBody identifier it mirrors, the revision last staged (sentinel forces the first upload),
//    the ribbon geometry's device buffers, and the per-body draw constants (colour + linetype) resolved at stage time so recording pushes them
//    straight through. Re-staged only when the source body's revision advances.
struct ResidentParametricSketchStroke
{
    uint32_t                        Identifier       = 0;            // [-] - the source stroke body's Identifier this mirrors
    uint32_t                        UploadedRevision = 0xFFFFFFFFu;  // [-] - the revision last staged (sentinel forces the first upload)
    PolygonBufferAllocation         Buffers;                         // [-] - device-local ribbon vertex + index buffers (segment quads)
    ParametricSketchStrokeConstants Constants;                       // [-] - resolved colour + linetype pushed at record time
};

// 📝 The whole GPU-outline consumer for ONE app: the target + curve pipeline + upload pool + resident ribbon geometry + the readiness gate. Every
//    field is default-constructible, so `ParametricSketchCurveSequence Sequence;` costs nothing until Initialize brings the chain up. Enabled is false
//    when any resource failed to come up, so the app runs as the pure-2D sketch it was (every call below is then a no-op). Host is borrowed (not owned).
struct ParametricSketchCurveSequence
{
    VulkanHost*                                 Host = nullptr;        // [-] - not owned; supplies device / queue / family / physical device
    ParametricSketchViewTarget                  Target;                // [-] - offscreen colour+depth the strokes render into, sampled by ImGui
    ParametricSketchCurveRasterization          CurveRasterization;    // [-] - the thick-line pipeline
    VkCommandPool                               UploadPool = VK_NULL_HANDLE;   // [-] - transient pool for staging ribbon geometry
    std::vector<ResidentParametricSketchStroke> ResidentStrokes;       // [-] - device-local ribbon geometry, re-staged on revision change
    float                                       HalfWidthPixels = 1.5f;// [px] - half the on-screen stroke thickness (constant at any zoom)
    bool                                        Enabled = false;       // [-] - true once target + pipeline + pool are all live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bring up the whole chain: an offscreen target at InitialExtent², the thick-line pipeline against that target's render pass, and a transient upload
// pool on the graphics family. Any failure disables the sequence only (Enabled stays false, every call below no-ops) and never gates the caller's
// bring-up — the app falls back to the pure-2D sketch. The two .spv paths resolve beside the exe (glslc emits them there). The ImGui Vulkan backend
// must be live (the target's colour image registers with it). Pair with FinalizeParametricSketchCurveSequence.
bool InitializeParametricSketchCurveSequence(ParametricSketchCurveSequence& Sequence,
                                             VulkanHost&                    Host,
                                             const char*                    CurveVertSpvPath,
                                             const char*                    CurveFragSpvPath,
                                             uint32_t                       InitialExtent);

// Drive the sequence for this frame, OUTSIDE any render pass (device idle-safe): when the published SceneView is ready, size the target to its canvas
// rect (draining the device on a genuine resize), refresh the curve camera from its settled matrices, then re-stage any stroke body (from
// RetrieveParametricSketchStrokeBodies) whose revision advanced — expanding each polyline into ribbon segment quads. A drained re-stage guards the
// previous frame's in-flight pre-pass draw from a use-after-free (DEVICE_LOST). No-op when the sequence is disabled or SceneView not ready.
void SynchronizeParametricSketchCurveSequence(ParametricSketchCurveSequence& Sequence, const ParametricSketchSceneView& SceneView);

// Record the offscreen outline pass into an (already-begun, still-open) primary command buffer, BEFORE the swapchain pass: begin the target's render
// pass (clear transparent), draw every resident stroke through the thick-line pipeline, and end it — leaving the colour image SHADER_READ_ONLY so this
// frame's ImGui pass samples it. No-op when disabled or nothing is resident.
void RecordParametricSketchCurveSequenceInto(ParametricSketchCurveSequence& Sequence, VkCommandBuffer CommandBuffer);

// Publish (or clear) the offscreen target's ImGui texture through the reverse bridge (RegisterParametricSketchStrokeImage) so the active view composites
// it on top next paint. Publishes the descriptor + extent when the sequence is live and any stroke is resident; otherwise publishes null (the view then
// draws no outline overlay). One frame late by construction — invisible as the camera eases.
void PublishParametricSketchStrokeImage(const ParametricSketchCurveSequence& Sequence);

// Tear down the whole chain (resident buffers, upload pool, curve pipeline, target) and reset to empty. The device must be idle (drain first). Safe on a
// never-initialized value. Note the target's colour descriptor is an ImGui backend texture, so call this while the ImGui Vulkan backend is still up.
void FinalizeParametricSketchCurveSequence(ParametricSketchCurveSequence& Sequence);

} // namespace Frontier

#endif
