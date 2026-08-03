/*==============================================================================================================================================
                                                         VOLUMEBOUNDSSUBMISSION.H
==============================================================================================================================================*/
// 🧩 A TRIANGLE-centroid bounding-box reduce: every triangle centroid in a vertex/index stream folded into one axis-aligned box, for a Morton pass
//    that normalises those same centroids into the unit cube. One compute pipeline (VolumeBoundsReduce.comp), one six-element accumulator buffer,
//    one dispatch of ceil(TriangleCount / 256) workgroups. It does NOT own geometry — the caller supplies the vertex and index buffers the raster
//    already holds, both of which already carry STORAGE usage. POD + free functions; borrows the VulkanHost, owns the accumulator + readback staging.
//
//    🔴 THIS IS NOT THE TOP LEVEL'S BOUNDS PASS, DESPITE WHAT THIS HEADER USED TO SAY. It reduces triangles, by the triangle rule
//       ((P0+P1+P2)/3), over a geometry stream. The TLAS partitions INSTANCES and needs a box over instance centroids, which is a different set,
//       a different centroid rule and a different descriptor layout — InstanceBoundsSubmission, the sibling of this file. Wiring the TLAS against
//       this box would normalise instance centroids against a triangle-centroid box: well-formed, plausible, and wrong in a way that only shows up
//       as degraded tree quality. Nothing in the current tree calls this yet; it is retained for the per-mesh uses the bottom level may want.
//
//    🔴 THE ACCUMULATORS ARE ORDERED-INT AND MUST BE RESEEDED BEFORE EVERY DISPATCH. The shader min/maxes a bit-pattern reinterpretation of each
//       float rather than the float itself, because this device has no float atomic MIN/MAX. Stated precisely, because the loose version of this
//       claim is misleading: VK_EXT_shader_atomic_float IS available here and reports shaderBufferFloat32Atomics and ...AtomicAdd — but that
//       extension only covers ADD and EXCHANGE. Atomic min/max on floats lives in VK_EXT_shader_atomic_float2, which this device does NOT expose.
//       So the ordered-int form is not a workaround for a disabled feature; it is the only way to do this operation here. Two consequences the
//       caller cannot ignore:
//         · The seed is +inf for the three minima and -inf for the three maxima, in the ORDERED domain. RecordVolumeBoundsReduce writes it with
//           vkCmdFillBuffer... but fill takes ONE 32-bit pattern, and the two seeds differ, so it is written as two fills over two ranges.
//         · Skipping the reseed does not fail — it silently returns the UNION of this rebuild and every previous one. A stale-union box is
//           well-formed and monotonically grows, so it degrades Morton precision a little more each frame with no visible error. This mirrors the
//           refit-counter trap the plan records: an unzeroed accumulator is the classic LBVH silent-wrong.
//
//    ⚠️ THE CENTROID RULE MUST MATCH THE MORTON PASS EXACTLY, INCLUDING THE DIVISION. This shader reduces (P0 + P1 + P2) / 3. If the Morton pass
//       computes the centroid any other way — even a mathematically equal reassociation that rounds differently — a centroid can land marginally
//       outside the box that was reduced for it, and its normalised coordinate leaves [0,1]. VolumeMortonCode.comp clamps, so the symptom is not a
//       crash but a handful of primitives quantised onto a shared edge value: extra duplicate Morton codes, which is precisely the input the tree
//       build is most sensitive to.

#pragma once
#ifndef FRONTIER_GRAPHICS_ACCELERATION_VOLUMEBOUNDSSUBMISSION_H
#define FRONTIER_GRAPHICS_ACCELERATION_VOLUMEBOUNDSSUBMISSION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The workgroup width of VolumeBoundsReduce.comp (local_size_x), and therefore the tile size of the shared-memory reduction. Must match
//    LanesPerGroup in the shader; the halving tree assumes it is a power of two.
constexpr uint32_t VolumeBoundsLanesPerGroup = 256;

// 📝 The accumulator element count: three ordered-int minima followed by three ordered-int maxima.
constexpr uint32_t VolumeBoundsElementCount = 6;


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block VolumeBoundsReduce.comp reads, matching its VolumeBoundsConstants byte-for-byte.
struct VolumeBoundsConstants
{
    uint32_t TriangleCount = 0;   // [-] - live triangles; the final tile is partial whenever this is not a multiple of VolumeBoundsLanesPerGroup
};

// 📝 A resolved scene box in ordinary floats, as handed back by RetrieveVolumeBounds. Separate from the device accumulators because the device
//    holds the ordered-int form, which is meaningless to a caller.
//
//    🔴 EmptyCondition IS NOT A FORMALITY. An untouched accumulator reads back inverted (minimum +inf, maximum -inf) because those are the seeds.
//       A caller that skips this flag and divides by (Maximum - Minimum) to normalise gets -inf, then NaN, and every Morton code collapses to the
//       same value — which the sort handles perfectly and the tree build then chokes on. Zero triangles is a legitimate state, not an error.
struct VolumeBounds
{
    float MinimumX = 0.0f;   // [cm] - smallest centroid X in the scene
    float MinimumY = 0.0f;   // [cm] - smallest centroid Y in the scene
    float MinimumZ = 0.0f;   // [cm] - smallest centroid Z in the scene
    float MaximumX = 0.0f;   // [cm] - largest centroid X in the scene
    float MaximumY = 0.0f;   // [cm] - largest centroid Y in the scene
    float MaximumZ = 0.0f;   // [cm] - largest centroid Z in the scene
    bool  EmptyCondition = true;   // [-] - true when no triangle was reduced; the box above is then meaningless, NOT a point at the origin
};

// 📝 The reduce's owned device resources. Everything is size-independent — the accumulator is six words whatever the triangle count — so the record
//    path only binds, pushes constants and dispatches. The geometry buffers are supplied per-record and never owned.
//
//    🔴 THE DESCRIPTOR SET IS WRITTEN AT UPLOAD TIME, NOT AT RECORD TIME. vkUpdateDescriptorSets on a set already recorded into a live command
//       buffer is undefined, so BindVolumeBoundsGeometry must be called before recording, and not again until the submission has completed. The
//       radix sort dodged this with pre-baked parity sets; here there is only one binding combination per rebuild, so a single set suffices.
struct VolumeBoundsSubmission
{
    VulkanHost*           Host             = nullptr;          // [-] - not owned; supplies device / physical device / allocator

    VkPipeline            ReducePipeline   = VK_NULL_HANDLE;   // [-] - VolumeBoundsReduce.comp
    VkDescriptorSetLayout ReduceSetLayout  = VK_NULL_HANDLE;   // [-] - { vertices, indices, bounds accumulator }
    VkPipelineLayout      ReduceLayout     = VK_NULL_HANDLE;   // [-] - ReduceSetLayout + VolumeBoundsConstants push range
    VkDescriptorPool      DescriptorPool   = VK_NULL_HANDLE;   // [-] - sized for the single set below
    VkDescriptorSet       ReduceSet        = VK_NULL_HANDLE;   // [-] - written by BindVolumeBoundsGeometry, read by the record path

    VkBuffer              BoundsBuffer     = VK_NULL_HANDLE;   // [-] - device-local six ordered-int accumulators (min XYZ then max XYZ)
    VkDeviceMemory        BoundsMemory     = VK_NULL_HANDLE;   // [-] - backing allocation for BoundsBuffer

    uint32_t              TriangleCount    = 0;                // [-] - live triangles set by the last bind; 0 makes the record path a no-op
    bool                  GeometryBound    = false;            // [-] - true once BindVolumeBoundsGeometry has written the set
    bool                  ReadyCondition   = false;            // [-] - true once pipeline + layout + descriptors + buffer are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the compute pipeline, its set/pipeline layout, the descriptor pool + set, and the six-word accumulator buffer. ShaderDirectory locates
// VolumeBoundsReduce.comp.spv. Returns false (ReadyCondition stays false, handles null) when the host has no device, the shader is missing, or an
// allocation fails. Host must be provisioned. Pair with FinalizeVolumeBoundsSubmission.
bool InitializeVolumeBoundsSubmission(VolumeBoundsSubmission& Bounds,
                                      VulkanHost&             Host,
                                      const char*             ShaderDirectory);

// Point the descriptor set at the caller's geometry and record the triangle count. VertexBuffer must be the stride-32 RenderVertex stream and
// IndexBuffer three indices per triangle, both with STORAGE usage. Must be called before RecordVolumeBoundsReduce, and NOT while a command buffer
// that already bound this set is still in flight. A no-op when ReadyCondition is false.
bool BindVolumeBoundsGeometry(VolumeBoundsSubmission& Bounds,
                              VkBuffer                VertexBuffer,
                              VkDeviceSize            VertexByteSize,
                              VkBuffer                IndexBuffer,
                              VkDeviceSize            IndexByteSize,
                              uint32_t                TriangleCount);

// Record the reseed and the reduce: two fills to seed the six accumulators, a transfer→compute barrier, then one dispatch of
// ceil(TriangleCount / VolumeBoundsLanesPerGroup) workgroups, then a compute→host barrier so a following readback sees the result. A no-op when not
// ready, geometry is unbound, or TriangleCount is 0. CommandBuffer must be recording, OUTSIDE any rendering scope.
//
// 🔴 THE RESEED IS PART OF THIS RECORDING AND MUST NOT BE HOISTED OUT. Reusing the accumulators across rebuilds without reseeding returns the union
//    of every rebuild so far — a box that only ever grows, is always well-formed, and silently coarsens Morton precision. Recording the seed with
//    the dispatch is what makes that impossible to forget.
void RecordVolumeBoundsReduce(VolumeBoundsSubmission& Bounds, VkCommandBuffer CommandBuffer);

// Copy the six accumulators back to host memory through a staging buffer, blocking until the copy completes, and resolve them out of the ordered-int
// domain into floats. Sets OutBounds.EmptyCondition when the accumulators are still at their seeds. Returns false when not ready or the readback
// fails. Intended for the validation gate and for the host-side Morton normalisation, not for a per-frame stall.
bool RetrieveVolumeBounds(VolumeBoundsSubmission& Bounds,
                          VkCommandPool           CommandPool,
                          VolumeBounds&           OutBounds);

// Report the device accumulator buffer, so a later dispatch (the Morton pass) can read the box on the GPU without a host round trip.
VkBuffer RetrieveVolumeBoundsBuffer(const VolumeBoundsSubmission& Bounds);

// Destroy the pipeline / layout / descriptors / buffer, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeVolumeBoundsSubmission(VolumeBoundsSubmission& Bounds);

} // namespace Frontier

#endif
