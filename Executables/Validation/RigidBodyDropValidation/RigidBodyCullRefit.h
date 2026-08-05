/*============================================================================================================================================
                                                            RIGIDBODYCULLREFIT.H
============================================================================================================================================*/
// 🧩 Re-fits the GPU per-instance cull records against moved instances, from OUTSIDE the engine. This lives in the validation target rather than
//    in Internal/Graphics because driving instance transforms per frame is this bed's requirement, not the renderer's — the shipped renderer
//    loads a static scene and fits its records exactly once.
//
//    🔴 WHY IT IS NEEDED AT ALL. A PartitionCullRecord is the mesh-local bounding sphere/cone TRANSFORMED BY the instance's Model. It is therefore
//       valid only for the Model it was fitted against. Fit once at load and then move the body, and the cull tests the body against its SPAWN
//       bounds and rejects it the moment it travels clear of them.
//       ⚠️ The symptom reads like a raster bug, not a cull bug: the TLAS is rebuilt on the GPU from live Model every frame, so the body keeps
//          casting a CORRECT SHADOW while rasterizing nothing. A crate that vanishes but still shades the floor is this and nothing else.
//
//    Why not the engine's own UploadInstanceCullRecords: it ends by clearing the counter buffer through ClearDeviceBuffer, which allocates a
//    command buffer, submits it, and BLOCKS ON A FENCE. Correct once at load; a full pipeline stall every frame. The clear is also redundant here —
//    ResetInstanceCullFrame already zeroes those counters in-band on the frame's own command buffer. So this writes the mapped record buffer and
//    nothing else, which is the only part a move actually invalidates.

#pragma once
#ifndef FRONTIER_VALIDATION_RIGIDBODYDROP_RIGIDBODYCULLREFIT_H
#define FRONTIER_VALIDATION_RIGIDBODYDROP_RIGIDBODYCULLREFIT_H

#include "Graphics/Scene/SuzanneScene.h"
#include "Graphics/Visibility/InstanceCullSubmission.h"

#include <vector>

namespace Frontier
{

// 📝 The mesh-local bounds a refit transforms. Fitted once from the loaded vertex stream (FitRigidBodyLocalBounds) and then reused every frame,
//    because the MESH never changes — only the instance transforms do.
struct RigidBodyLocalBounds
{
    float Sphere[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // [m] - mesh-local bounding sphere (xyz centre, w radius)
    float Cone[4]   = { 0.0f, 0.0f, 0.0f, -1.0f };  // [-] - mesh-local normal cone (xyz axis, w cosine; -1 = non-coneable, never backface-rejected)
};

// Fit the mesh-local bounding sphere over Positions (three floats per vertex, VertexCount vertices). Uses the centroid + max-radius fit the engine
// applies to its own meshes. The cone is left non-coneable (-1): every crate is a closed box whose normals span the full sphere, so no cone can
// reject anything, and claiming a tighter one would drop faces.
void FitRigidBodyLocalBounds(const float* Positions, uint32_t VertexCount, uint32_t PositionStride, RigidBodyLocalBounds& Bounds);

// 📝 The PERSISTENT mapping of the cull's record buffer. Held across frames rather than re-mapped per frame: vkMapMemory / vkUnmapMemory is not a free
//    bookkeeping pair — it can re-establish driver page mappings and cross into the kernel — so paying it 60x a second to write a few hundred bytes is
//    pure overhead. The allocation is HOST_VISIBLE | HOST_COHERENT (InstanceCullSubmission.cpp) and lives for the cull's whole lifetime, so one map at
//    first use stays valid until teardown.
//
// ⚠️ Mapped memory on a coherent host-visible heap is typically WRITE-COMBINED: writes stream, but a READ is very slow. Every field of a record is
//    therefore written exactly once and never read back — do not introduce a read-modify-write over Records.
struct RigidBodyCullMapping
{
    void* Records = nullptr;   // [-] - mapped base of the record buffer (capacity-sized), or null before the first successful map
};

// Re-fit one record per instance from Instances' CURRENT Model transforms and write them straight into the cull's persistently mapped record buffer.
// Writes ONLY that buffer: no counter clear, no submit, no fence, no command-buffer allocation, and after the first call no map either — so it is safe
// to drive every frame. Truncates to RecordCapacity. A no-op when the cull never built (the plain instanced draw path needs no records).
// Call alongside UploadVisibilityScene. Mapping is acquired lazily on the first call and released by ReleaseRigidBodyCullMapping.
//
// 🔴 No explicit flush: the backing allocation is HOST_COHERENT, so writes are visible to the device without vkFlushMappedMemoryRanges. If that
//    allocation ever loses HOST_COHERENT, this function MUST gain a flush — the GPU would otherwise read stale records with no validation error.
void RefitRigidBodyCullRecords(InstanceCullSubmission&                  Cull,
                               RigidBodyCullMapping&                    Mapping,
                               const std::vector<SuzanneSceneInstance>& Instances,
                               const RigidBodyLocalBounds&              Bounds);

// Unmap the record buffer if it was mapped. Idempotent; call before FinalizeInstanceCullSubmission frees the allocation.
void ReleaseRigidBodyCullMapping(InstanceCullSubmission& Cull, RigidBodyCullMapping& Mapping);

} // namespace Frontier

#endif
