/*==============================================================================================================================================
                                                            GPUBAKEDISPATCH.H
==============================================================================================================================================*/
// 🧩 The Vulkan compute path for the texture bakes. When the Baking workspace's COMPUTE toggle is on, the bake lifecycle routes here
//    instead of the CPU worker pool: one owned GpuBakeContext lazily builds a compute pipeline per shader, and each Evaluate* entry
//    uploads its inputs to storage buffers, dispatches the matching .comp, waits a one-shot fence, and reads the device result straight
//    back into the SAME CPU result struct the CPU path fills — a SignedDistanceVolume for the signed-distance bake, a BakedImageBuffer for
//    the surface maps — so every publish path downstream (residency upload, .rsdf / .rsdfvdb write, 2D preview card) is byte-for-byte
//    identical whether the bake ran on the CPU or the GPU. The signed-distance bake exposes two algorithms: an exact per-voxel
//    closest-point-to-triangle bake that matches the CPU ExactBoundingVolume result, and a fast Jump-Flooding bake for the realtime /
//    auto-rebake path. Struct + free-function, no Interface dependency. Every entry returns false on any failure so the caller can fall
//    back to the CPU worker and always complete.

#pragma once
#ifndef FRONTIER_AUTHORING_BAKING_GPUBAKEDISPATCH_H
#define FRONTIER_AUTHORING_BAKING_GPUBAKEDISPATCH_H

#include "SurfaceBakeContract.h"    // 📝 SurfaceMapIdentity / SurfaceBakeParameters / BakedImageBuffer — the surface bake contract
#include "DistanceBakeContract.h"   // 📝 DistanceBakeAlgorithm — the same enum the CPU path selects the fill strategy with

#include <cstdint>

namespace Frontier
{

struct VulkanHost;
struct RenderVertexStream;
struct SignedDistanceVolume;
struct SurfaceSampleField;
struct TriangleRayVolume;
struct Vector3d;

// 📝 One in-flight asynchronous exact-distance dispatch: the command buffer + fence submitted to the compute queue, plus the host storage
//    buffers (triangles + full-grid voxels) kept mapped and alive until the fence signals, and the sub-box the readback narrows into the
//    target volume. Heap-owned by the caller (opaque here so the UI header stays Vulkan-free); the compute runs on the GPU across as many
//    frames as it needs while the render thread keeps drawing the previous resident volume. The exact path is a single dispatch, so one
//    fence covers the whole bake. Poll it with PollDistanceExactGpu; on VK_SUCCESS drain it with ResolveDistanceExactGpu.
struct GpuBakeAsyncRun;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The owned compute objects, held opaquely so this header stays free of the Vulkan headers (the .cpp pulls the full implementation).
//    One context lives beside the VulkanHost for the whole app; its pipelines, descriptor pool, transient command pool, and reusable
//    staging buffers are built lazily on the first dispatch of each shader and torn down at Finalize. InitializeEnabled reports whether the
//    device + transient command pool came up; when false every Evaluate* declines so the caller falls back to the CPU.
struct GpuBakeContext
{
    void* OpaqueImplementation = nullptr;   // [-] - heap GpuBakeContextImplementation (owns the Vulkan objects)
    bool  InitializeEnabled    = false;     // [-] - the transient command pool + device came up; Evaluate* may dispatch
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bring the compute context up against the live device: cache the device handles, create the transient command pool + descriptor pool,
// and load the bake compute SPIR-V off disk (ShaderDirectory is the folder the .comp.spv files were compiled into). Pipelines build
// lazily on first use. Returns true (and sets Context.InitializeEnabled) when the context is ready; on any failure the context is left
// safe to Finalize and every Evaluate* declines. ⚠️ Initialization-time only — do not call during the main loop.
bool InitializeGpuBakeContext(GpuBakeContext& Context, VulkanHost& Device, const char* ShaderDirectory);

// Tear down every owned compute object in reverse creation order (waits the device idle first). Null-guarded and safe on a context that
// never initialized. ⚠️ Call before FinalizeVulkanHost.
void FinalizeGpuBakeContext(GpuBakeContext& Context, VulkanHost& Device);

// Bake a dense signed-distance volume on the GPU. Derives the padded AABB / MaximumDistance from the stream exactly as the CPU baker
// does, uploads the triangle list, dispatches the exact per-voxel bake (Algorithm == ExactBoundingVolume, matches the CPU result) or the
// Jump-Flooding bake (Algorithm == FastSweeping, the fast realtime path), and reads the R16_SNORM voxels back into OutVolume with the same
// bounds / resolution / decode scale the CPU path writes. Returns false on invalid input or any Vulkan failure (caller falls back to CPU).
bool EvaluateDistanceVolumeGpu(GpuBakeContext&           Context,
                               VulkanHost&               Device,
                               const RenderVertexStream& Stream,
                               uint32_t                  Resolution,
                               DistanceBakeAlgorithm     Algorithm,
                               SignedDistanceVolume&     OutVolume);

// Re-bake only the voxels inside an inclusive voxel sub-box [MinX,MaxX]×[MinY,MaxY]×[MinZ,MaxZ] on an already-baked OutVolume, in place,
// reusing its fixed bounds / resolution / decode scale (never re-derives bounds) — the GPU partner of EvaluateDistanceRegion for the
// incremental / auto-rebake path. The triangle list is uploaded whole so the closest-triangle query stays exact at any dirty voxel, but the
// exact per-voxel bake is DISPATCHED bounded to the sub-box (BoxMinimum + BoxExtent) and writes straight into OutVolume.Voxels — only the
// dirty voxels recompute, no whole-grid pass. Returns false on a mismatch or any Vulkan failure (caller falls back to CPU region / full bake).
bool EvaluateDistanceRegionGpu(GpuBakeContext&           Context,
                               VulkanHost&               Device,
                               SignedDistanceVolume&     OutVolume,
                               const RenderVertexStream& Stream,
                               uint32_t                  MinX,
                               uint32_t                  MinY,
                               uint32_t                  MinZ,
                               uint32_t                  MaxX,
                               uint32_t                  MaxY,
                               uint32_t                  MaxZ);

// ── Asynchronous exact-distance dispatch (submit now → poll fence → drain on completion) ────────────────────────────────────────────────
// The synchronous EvaluateDistanceVolumeGpu / EvaluateDistanceRegionGpu block the render thread on vkWaitForFences for the whole compute.
// These three split that: Begin records + submits the exact dispatch (whole-grid or sub-box) and returns an in-flight run WITHOUT waiting;
// Poll reports whether the fence has signaled (never blocks); Resolve narrows the finished voxels into OutVolume and tears the run down.
// While a run is in flight the caller keeps rendering the previous resident volume, so a Full / Grow rebake no longer freezes the viewport.
// ⚠️ FastSweeping (jump-flood) has no async entry this slice — it is a multi-dispatch pass; callers requesting it stay on the sync path.

// Begin an asynchronous exact bake. Mirrors EvaluateDistanceVolumeGpu (BoxMinimum/BoxExtent == nullptr, whole grid → OutVolume re-derives
// its bounds) OR EvaluateDistanceRegionGpu (a sub-box on OutVolume's FIXED bounds — pass OutVolume already seeded, resolution fixed). On a
// whole-grid begin OutBounds/OutResolution are derived from Stream and returned so Resolve can publish them; on a region begin they are
// taken from OutVolume. Returns a heap GpuBakeAsyncRun the caller owns (delete via ResolveDistanceExactGpu or DiscardDistanceExactGpu), or
// nullptr on any failure (caller falls back to the CPU / sync path). Sets the caller's one-in-flight gate — do not Begin a second run until
// the first resolves (the shared descriptor pool must not be reset under an in-flight dispatch).
// NarrowMode selects how the finished voxels reach the resident image: 0 CPU narrow (Resolve memcpy-narrows int32 → int16, the caller then
// uploads), 1 GPU narrow shader (a dedicated DistanceNarrow pass packs int32 → int16 into a device-local buffer in the same command
// buffer), 2 the exact shader packs int16 directly (no int32 intermediate). Modes 1/2 leave the packed device buffer for the
// CopyPackedBufferToResident path and skip the CPU narrow; a region run always degrades to mode 0 (the packed store is whole-grid only).
GpuBakeAsyncRun* BeginDistanceExactGpu(GpuBakeContext&           Context,
                                       VulkanHost&               Device,
                                       const RenderVertexStream& Stream,
                                       SignedDistanceVolume&     OutVolume,
                                       bool                      RegionRun,
                                       int                       NarrowMode,
                                       uint32_t                  MinX,
                                       uint32_t                  MinY,
                                       uint32_t                  MinZ,
                                       uint32_t                  MaxX,
                                       uint32_t                  MaxY,
                                       uint32_t                  MaxZ);

// After a modes-1/2 run resolves, hand the packed device buffer to CopyPackedBufferToResident. Retrieve the buffer handle
// here (VK_NULL_HANDLE when the run was mode 0 / region / degenerate — the caller then falls back to the CPU-narrow upload path).
void* ResolvePackedBufferHandle(const GpuBakeAsyncRun* Run);   // returns VkBuffer as void* (header stays Vulkan-free); null if none

// Free a modes-1/2 run that ResolveDistanceExactGpu kept alive (its packed device buffer + the run struct). Call ONLY after
// CopyPackedBufferToResident has recorded + waited its transfer — the compute fence already signaled (Poll returned true) and the copy
// drained its own fence, so this is a plain buffer free with NO device-wait-idle (unlike DiscardDistanceExactGpu). Safe on nullptr.
void FinalizePackedRunGpu(GpuBakeContext& Context, GpuBakeAsyncRun* Run);

// Non-blocking fence check on an in-flight run. Returns true once the compute has finished (vkGetFenceStatus == VK_SUCCESS), false while it
// is still running. Never waits. Null-safe (false).
bool PollDistanceExactGpu(GpuBakeContext& Context, const GpuBakeAsyncRun* Run);

// Drain a finished run: narrow the computed voxels into OutVolume (whole grid, or only the sub-box for a region run — leaving the rest of
// OutVolume.Voxels intact), tear down the command buffer / fence / host buffers, and delete the run. Call ONLY after PollDistanceExactGpu
// returns true. Returns false (and still frees the run) if the run or context is degenerate. OutVolume must be the SAME volume passed to
// Begin (region) or a fresh volume the whole-grid begin will fill with the derived bounds / resolution.
bool ResolveDistanceExactGpu(GpuBakeContext& Context, GpuBakeAsyncRun* Run, SignedDistanceVolume& OutVolume);

// Abandon an in-flight or finished run without reading it back (device-wait-idle then tear down). Safe on nullptr. For error unwinding.
void DiscardDistanceExactGpu(GpuBakeContext& Context, GpuBakeAsyncRun* Run);

// Encode one surface map on the GPU from a CPU-rasterized SurfaceSampleField. The field is still scan-converted on the CPU (cheap); the
// GPU work is the per-texel encode and — for bent normal / thickness — the hemisphere ray trace against the uploaded ray volume, which is
// the compute-heavy part. Uploads the field (+ the ray volume triangles when the identity traces) to storage buffers, dispatches the
// encoder selected by Identity, and reads the R8G8B8A8 image back into Out.Pixels (Edge² × 4). Produces the SAME BakedImageBuffer the
// matching CPU encoder produces. Returns false on empty input or any Vulkan failure.
bool EvaluateSurfaceMapGpu(GpuBakeContext&              Context,
                           VulkanHost&                  Device,
                           const SurfaceSampleField&    Field,
                           const TriangleRayVolume&     Volume,
                           const SurfaceBakeParameters& Parameters,
                           SurfaceMapIdentity           Identity,
                           BakedImageBuffer&            Out);

} // namespace Frontier

#endif
