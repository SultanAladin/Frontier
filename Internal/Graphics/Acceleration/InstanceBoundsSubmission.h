/*==============================================================================================================================================
                                                        INSTANCEBOUNDSSUBMISSION.H
==============================================================================================================================================*/
// 🧩 The TOP level (TLAS) build's first two dispatches: reduce every placed instance's centroid to one scene-wide box, then Morton-code each
//    instance against that box straight into the radix sort's device buffers. Two compute pipelines (InstanceBoundsReduce.comp,
//    InstanceMortonCode.comp), one six-element accumulator, and no geometry of its own — the caller supplies the instance buffer the raster already
//    holds and the arena buffers GeometryArenaSubmission already uploaded. POD + free functions, mirroring VolumeBoundsSubmission.
//
//    🔴 THIS IS THE SIBLING OF VolumeBoundsSubmission, NOT A REPLACEMENT FOR IT, AND THE TWO REDUCE DIFFERENT THINGS. VolumeBoundsSubmission
//       reduces TRIANGLE centroids as (P0+P1+P2)/3 over a vertex/index stream — the BOTTOM level's set, by the bottom level's rule. This one
//       reduces INSTANCE centroids as the centre of a world-space instance box. They were kept apart deliberately rather than fused behind a mode
//       flag, because every failure mode of a fused version is silent: a wrong mode selection yields a well-formed box over the wrong set, which
//       coarsens Morton codes and degrades tree quality with nothing to catch it. Two types make that same mistake a compile error.
//
//    📝 An instance's world box is its mesh's LOCAL-space bottom-level root box transformed by the instance's Model matrix, found by
//       MeshOrdinal -> GeometryArenaSlice -> NodeOffset. So the top level costs one box per INSTANCE, never one per triangle: a scene of 100k
//       instances sharing 12 meshes reduces 100k boxes and re-reads none of the 12 meshes' geometry. That is the whole economic argument for the
//       two-level split, and it is why this submission binds the arena rather than the vertex stream.
//
//    ⚠️ THE MORTON PASS WRITES INTO THE RADIX SORT'S OWN DEVICE BUFFERS. RadixSortSubmission's upload path stages keys from HOST memory, which for
//       a per-frame rebuild would mean device -> host -> device for data the GPU just produced and is about to consume. BindInstanceMortonTarget
//       takes the sort's PrimaryKeyBuffer / PrimaryPayloadBuffer instead and the pass fills them in place; the host then only has to tell the sort
//       how many keys are live (SetRadixSortKeyCount), which is why that function exists.

#pragma once
#ifndef FRONTIER_GRAPHICS_ACCELERATION_INSTANCEBOUNDSSUBMISSION_H
#define FRONTIER_GRAPHICS_ACCELERATION_INSTANCEBOUNDSSUBMISSION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The workgroup width of both shaders (local_size_x). Must match LanesPerGroup in each; the reduce's halving tree assumes a power of two.
constexpr uint32_t InstanceBoundsLanesPerGroup = 256;

// 📝 The accumulator element count: three ordered-int minima followed by three ordered-int maxima.
constexpr uint32_t InstanceBoundsElementCount = 6;

// 📝 Bits per axis in the emitted Morton code, matching MortonBitsPerAxis in InstanceMortonCode.comp. Three axes at 10 bits is 30 of the 32
//    available bits, which the sort's four 8-bit digit passes cover exactly.
constexpr uint32_t InstanceMortonBitsPerAxis = 10;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block both shaders read, matching their constants byte for byte. SliceCount lets a shader reject an out-of-range MeshOrdinal rather
//    than read past the slice table.
struct InstanceBoundsConstants
{
    uint32_t InstanceCount = 0;   // [-] - live instances; the final tile is partial whenever this is not a multiple of InstanceBoundsLanesPerGroup
    uint32_t SliceCount    = 0;   // [-] - entries in the arena's slice table
};

// 📝 A resolved instance-centroid box in ordinary floats, as handed back by RetrieveInstanceBounds. Separate from the device accumulators because
//    the device holds the ordered-int form.
//
//    🔴 EmptyCondition IS NOT A FORMALITY. An untouched accumulator reads back inverted (minimum +inf, maximum -inf) because those are the seeds. A
//       caller that skips this flag and divides by (Maximum - Minimum) gets -inf, then NaN, and every Morton code collapses to one value. Zero
//       instances — and a scene where EVERY instance was dropped for an out-of-range ordinal — are legitimate states, not errors.
struct InstanceBounds
{
    float MinimumX = 0.0f;   // [cm] - smallest instance centroid X in the scene
    float MinimumY = 0.0f;   // [cm] - smallest instance centroid Y
    float MinimumZ = 0.0f;   // [cm] - smallest instance centroid Z
    float MaximumX = 0.0f;   // [cm] - largest instance centroid X
    float MaximumY = 0.0f;   // [cm] - largest instance centroid Y
    float MaximumZ = 0.0f;   // [cm] - largest instance centroid Z
    bool  EmptyCondition = true;   // [-] - true when no instance was reduced; the box above is then meaningless, NOT a point at the origin
};

// 📝 The submission's owned device resources. The accumulator is six words whatever the instance count, so nothing here is sized by the scene —
//    the record path only binds, pushes constants and dispatches.
//
//    🔴 THE DESCRIPTOR SETS ARE WRITTEN AT BIND TIME, NOT RECORD TIME. vkUpdateDescriptorSets on a set already recorded into a live command buffer
//       is undefined, so both binds must happen before recording and not again until the submission has completed.
struct InstanceBoundsSubmission
{
    VulkanHost*           Host             = nullptr;          // [-] - not owned; supplies device / physical device / allocator

    VkPipeline            ReducePipeline   = VK_NULL_HANDLE;   // [-] - InstanceBoundsReduce.comp
    VkDescriptorSetLayout ReduceSetLayout  = VK_NULL_HANDLE;   // [-] - { instances, slices, nodes, bounds accumulator }
    VkPipelineLayout      ReduceLayout     = VK_NULL_HANDLE;   // [-] - ReduceSetLayout + InstanceBoundsConstants push range
    VkDescriptorSet       ReduceSet        = VK_NULL_HANDLE;   // [-] - written by BindInstanceBoundsScene

    VkPipeline            MortonPipeline   = VK_NULL_HANDLE;   // [-] - InstanceMortonCode.comp
    VkDescriptorSetLayout MortonSetLayout  = VK_NULL_HANDLE;   // [-] - { instances, slices, nodes, bounds, morton keys, morton payloads }
    VkPipelineLayout      MortonLayout     = VK_NULL_HANDLE;   // [-] - MortonSetLayout + InstanceBoundsConstants push range
    VkDescriptorSet       MortonSet        = VK_NULL_HANDLE;   // [-] - written by BindInstanceMortonTarget

    VkDescriptorPool      DescriptorPool   = VK_NULL_HANDLE;   // [-] - sized for the two sets above

    VkBuffer              BoundsBuffer     = VK_NULL_HANDLE;   // [-] - device-local six ordered-int accumulators (min XYZ then max XYZ)
    VkDeviceMemory        BoundsMemory     = VK_NULL_HANDLE;   // [-] - backing allocation for BoundsBuffer

    // 🔴 NOT OWNED — the caller's buffers, retained ONLY so the Morton record path can barrier the output it just wrote. A pass that writes a buffer
    //    and does not fence it is trusting its consumer to guess, and the consumer here is a different submission that cannot see these handles.
    VkBuffer              MortonKeyTarget      = VK_NULL_HANDLE;  // [-] - written by the Morton pass, read by the sort's first tally
    VkBuffer              MortonPayloadTarget  = VK_NULL_HANDLE;  // [-] - ditto
    VkDeviceSize          MortonKeyBytes       = 0;               // [B] - range of MortonKeyTarget the barrier must cover
    VkDeviceSize          MortonPayloadBytes   = 0;               // [B] - range of MortonPayloadTarget the barrier must cover

    uint32_t              InstanceCount    = 0;                // [-] - live instances set by the last scene bind; 0 makes the record path a no-op
    uint32_t              SliceCount       = 0;                // [-] - slice-table entries set by the last scene bind
    bool                  SceneBound       = false;            // [-] - true once BindInstanceBoundsScene has written the reduce set
    bool                  MortonTargetBound = false;           // [-] - true once BindInstanceMortonTarget has written the morton set
    bool                  ReadyCondition   = false;            // [-] - true once pipelines + layouts + descriptors + buffer are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build both compute pipelines, their set/pipeline layouts, the descriptor pool + two sets, and the six-word accumulator buffer. ShaderDirectory
// locates InstanceBoundsReduce.comp.spv and InstanceMortonCode.comp.spv. Returns false (ReadyCondition stays false, handles null) when the host has
// no device, either shader is missing, or an allocation fails. Host must be provisioned. Pair with FinalizeInstanceBoundsSubmission.
bool InitializeInstanceBoundsSubmission(InstanceBoundsSubmission& Bounds,
                                        VulkanHost&               Host,
                                        const char*               ShaderDirectory);

// Point both descriptor sets at the scene: the instance array the raster holds, and the arena's slice and node buffers. Records InstanceCount and
// SliceCount. Must be called before recording, and NOT while a command buffer that already bound these sets is in flight. A no-op when not ready.
//
// 🔴 SliceCount MUST BE THE ARENA'S ACTUAL SLICE COUNT, NOT THE INSTANCE COUNT. Both shaders use it as the bound on a valid MeshOrdinal; passing
//    the instance count instead admits ordinals past the end of the slice table, which reads whatever memory follows and produces a box that is
//    well-formed and wrong.
bool BindInstanceBoundsScene(InstanceBoundsSubmission& Bounds,
                             VkBuffer                  InstanceBuffer,
                             VkDeviceSize              InstanceByteSize,
                             VkBuffer                  SliceBuffer,
                             VkDeviceSize              SliceByteSize,
                             VkBuffer                  NodeBuffer,
                             VkDeviceSize              NodeByteSize,
                             uint32_t                  InstanceCount,
                             uint32_t                  SliceCount);

// Point the Morton pass at the buffers it writes — in practice the radix sort's PrimaryKeyBuffer and PrimaryPayloadBuffer, so the codes land where
// the sort's tally already reads them and no host round trip is needed. Both must hold at least InstanceCount uints and carry STORAGE usage. Must
// be called after BindInstanceBoundsScene (which writes the shared bindings of the same set) and before recording. A no-op when not ready.
bool BindInstanceMortonTarget(InstanceBoundsSubmission& Bounds,
                              VkBuffer                  MortonKeyBuffer,
                              VkDeviceSize              MortonKeyByteSize,
                              VkBuffer                  MortonPayloadBuffer,
                              VkDeviceSize              MortonPayloadByteSize);

// Record the reseed and the reduce: two fills to seed the six accumulators, a transfer->compute barrier, then one dispatch of
// ceil(InstanceCount / InstanceBoundsLanesPerGroup) workgroups, then a compute barrier so the Morton pass (or a readback) sees the result. A no-op
// when not ready, the scene is unbound, or InstanceCount is 0. CommandBuffer must be recording, OUTSIDE any rendering scope.
//
// 🔴 THE RESEED IS PART OF THIS RECORDING AND MUST NOT BE HOISTED OUT. Reusing the accumulators across frames without reseeding returns the union
//    of every rebuild so far — a box that only ever grows, is always well-formed, and silently coarsens Morton precision. For a per-frame TLAS this
//    is worse than for the load-time bottom level: the union accumulates for the whole session, so an object that ever visited the far edge of the
//    map keeps the box stretched there forever.
void RecordInstanceBoundsReduce(InstanceBoundsSubmission& Bounds, VkCommandBuffer CommandBuffer);

// Record the Morton emit: one dispatch of ceil(InstanceCount / InstanceBoundsLanesPerGroup) workgroups writing one key and one payload per
// instance, then a compute->compute barrier so the sort's first tally sees them. A no-op when not ready, either bind is missing, or InstanceCount
// is 0. Must be recorded AFTER RecordInstanceBoundsReduce in the same command buffer — it reads the box that pass produces.
void RecordInstanceMortonCode(InstanceBoundsSubmission& Bounds, VkCommandBuffer CommandBuffer);

// Copy the six accumulators back to host memory through a staging buffer, blocking until the copy completes, and resolve them out of the
// ordered-int domain. Sets OutBounds.EmptyCondition when the accumulators are still at their seeds. Intended for the validation gate, not for a
// per-frame stall — the Morton pass reads the box on the device and needs no readback.
bool RetrieveInstanceBounds(InstanceBoundsSubmission& Bounds,
                            VkCommandPool             CommandPool,
                            InstanceBounds&           OutBounds);

// Report the device accumulator buffer, for a consumer that wants the box on the GPU.
VkBuffer RetrieveInstanceBoundsBuffer(const InstanceBoundsSubmission& Bounds);

// Destroy the pipelines / layouts / descriptors / buffer, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeInstanceBoundsSubmission(InstanceBoundsSubmission& Bounds);

} // namespace Frontier

#endif
