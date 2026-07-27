/*==============================================================================================================================================
                                                        PARAMETRICSKETCHSOLIDSEQUENCE.H
==============================================================================================================================================*/
// 🧩 The shared GPU consumer that renders the parametric-sketch's lofts + 2D-fill / solid shapes INTO the canvas, camera-locked. It owns the whole
//    lean chain — a chrome matcap image, an offscreen colour+depth target, the two-set matcap pipeline, an upload pool, and the device-local
//    resident geometry — and drives it each frame off the ParametricSketchSceneView / shape-body / loft-body bridges the active view publishes. An
//    ORDERED multi-step chain (Synchronize → RecordInto → PublishImage → Finalize) — hence the …Sequence suffix, not a banned …Pass. Both the
//    standalone sketch build and the full editor drive the SAME struct + free functions, so the GPU-solid path is one source of truth (no drift).
//    Modular, not standalone: one sequence per app, the caller owns the frame loop and threads the four calls below (Synchronize before encode,
//    RecordInto inside the pre-pass, PublishImage before the paint, Finalize at exit). Wired onto the shared VulkanHost.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDER_SURFACE_PARAMETRICSKETCHSOLIDSEQUENCE_H
#define FRONTIER_GRAPHICS_RENDER_SURFACE_PARAMETRICSKETCHSOLIDSEQUENCE_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Render/Surface/ParametricSketchMatcapTexture.h"
#include "Graphics/Render/Surface/ParametricSketchSurfaceInscription.h"
#include "Graphics/Render/Surface/ParametricSketchViewTarget.h"
#include "Graphics/Render/Resources/BufferAllocation.h"
#include "Authoring/ParametricAuthoring/ParametricSketchShapeStore.h"   // 📝 ParametricSketchSceneView + the scene / shape-body / solid-image bridges consumed + published

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One resident matcap body: the ParametricSketchLoftBody / ParametricSketchShapeBody identifier it mirrors, the revision last staged (sentinel
//    forces the first upload), and its device-local vertex + index buffers. Lofts and shapes live in SEPARATE resident vectors + id spaces (their
//    ids can collide), but the upload path is identical, so both reuse this one row type. Re-staged only when the source body's revision advances.
struct ResidentParametricSketchBody
{
    uint32_t                Identifier       = 0;            // [-] - the source body's Identifier this mirrors
    uint32_t                UploadedRevision = 0xFFFFFFFFu;  // [-] - the revision last staged (sentinel forces the first upload)
    PolygonBufferAllocation Buffers;                         // [-] - device-local vertex + index buffers
};

// 📝 The whole GPU-solid consumer for ONE app: the matcap chain + upload pool + both resident geometry vectors + the readiness gate. Every field is
//    default-constructible, so `ParametricSketchSolidSequence Sequence;` costs nothing until InitializeParametricSketchSolidSequence brings the chain
//    up. Enabled is false when any resource failed to come up, so the app runs as the pure-2D sketch it was (every call below is then a no-op). Host
//    is borrowed (not owned).
struct ParametricSketchSolidSequence
{
    VulkanHost*                               Host = nullptr;             // [-] - not owned; supplies device / queue / family / physical device
    ParametricSketchMatcapTexture             Matcap;                     // [-] - chrome matcap disc image + sampler
    ParametricSketchViewTarget                Target;                     // [-] - offscreen colour+depth the inscription renders into, sampled by ImGui
    ParametricSketchSurfaceInscription        SurfaceInscription;         // [-] - the two-set matcap pipeline
    VkCommandPool                             UploadPool = VK_NULL_HANDLE;// [-] - transient pool for staging body geometry
    std::vector<ResidentParametricSketchBody> ResidentLofts;             // [-] - device-local loft geometry, re-staged on revision change
    std::vector<ResidentParametricSketchBody> ResidentShapes;            // [-] - device-local 2D-fill / solid shape geometry, SEPARATE id space from lofts
    bool                                      Enabled = false;            // [-] - true once matcap + target + inscription + pool are all live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bring up the whole chain: the chrome matcap texture, an offscreen target at InitialExtent², the matcap pipeline against that target's render pass,
// and a transient upload pool on the graphics family. Any failure disables the sequence only (Enabled stays false, every call below no-ops) and never
// gates the caller's bring-up — the app falls back to the pure-2D sketch. MatcapPngPath is asset-root-relative; the two .spv paths resolve beside the
// exe (glslang emits them there). The ImGui Vulkan backend must be live (the target's colour image registers with it). Pair with FinalizeParametricSketchSolidSequence.
bool InitializeParametricSketchSolidSequence(ParametricSketchSolidSequence& Sequence,
                                             VulkanHost&                    Host,
                                             const char*                    MatcapPngPath,
                                             const char*                    MatcapVertSpvPath,
                                             const char*                    MatcapFragSpvPath,
                                             uint32_t                       InitialExtent);

// Drive the sequence for this frame, OUTSIDE any render pass (device idle-safe): when the published SceneView is ready, size the target to its canvas
// rect (draining the device on a genuine resize), refresh the matcap camera from its settled matrices, then re-stage any loft body (from
// RetrieveParametricSketchShapeSource's store, when non-null) and shape body (from RetrieveParametricSketchShapeBodies) whose revision advanced. A drained
// re-stage guards the previous frame's in-flight pre-pass draw from a use-after-free (DEVICE_LOST). No-op when the sequence is disabled or SceneView not ready.
void SynchronizeParametricSketchSolidSequence(ParametricSketchSolidSequence& Sequence, const ParametricSketchSceneView& SceneView);

// Record the offscreen solid pass into an (already-begun, still-open) primary command buffer, BEFORE the swapchain pass: begin the target's render
// pass (clear transparent), draw every resident loft then every resident shape through the matcap pipeline (shared depth → self-occlusion), and end
// it — leaving the colour image SHADER_READ_ONLY so this frame's ImGui pass samples it. No-op when disabled or nothing is resident.
void RecordParametricSketchSolidSequenceInto(ParametricSketchSolidSequence& Sequence, VkCommandBuffer CommandBuffer);

// Publish (or clear) the offscreen target's ImGui texture through the reverse bridge (RegisterParametricSketchSolidImage) so the active view composites
// it into its canvas next paint. Publishes the descriptor + extent when the sequence is live and any body is resident; otherwise publishes null (the
// view then draws the pure sketch). One frame late by construction — the pass for this frame runs after the paint — invisible as the camera eases.
void PublishParametricSketchSolidImage(const ParametricSketchSolidSequence& Sequence);

// Tear down the whole chain (resident buffers, upload pool, inscription, target, matcap) and reset to empty. The device must be idle (drain first). Safe
// on a never-initialized value. Note the target's colour descriptor is an ImGui backend texture, so call this while the ImGui Vulkan backend is still up.
void FinalizeParametricSketchSolidSequence(ParametricSketchSolidSequence& Sequence);

} // namespace Frontier

#endif
