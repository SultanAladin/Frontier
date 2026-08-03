/*==============================================================================================================================================
                                                            SURFELPREFIXSUM.H
==============================================================================================================================================*/
// 🧩 The segmented inclusive prefix sum over the grid's per-cell counts — the arithmetic that turns "how many surfels want cell i" into "where does
//    cell i's slice of the surfel list start". The slotting counts surfels into cells (an atomicAdd per cell), and this scan converts those counts
//    into the per-cell END offsets the slot pass writes back-to-front into. It is NET-NEW to the repo: no prefix scan existed before surfel GI, so
//    this is a faithful port of webgiya's four-pass segmented scan (surfelHashGrid.ts) rather than a clone of an existing Frontier submission.
//
//    🔴 FOUR PASSES, RUN IN ORDER, WITH A BARRIER BETWEEN EACH (PLAN §8 F3/F4):
//       1. ScanSeg     — each 1024-wide segment inclusive-scanned locally in shared memory (SurfelPrefixScanSegment.comp).
//       2. CollectSeg  — gather each segment's total (its last scanned element) into SegmentSums (SurfelPrefixCollectSegment.comp).
//       3. SegPrefix   — SERIAL single-thread inclusive prefix of SegmentSums (SurfelPrefixSegmentReduce.comp). 🔴 F4: the serial variant is the
//                        ported one; the reference's commented-out parallel variant is dead and must NOT be resurrected.
//       4. Merge       — add SegmentSums[seg-1] to every element of segment seg, lifting the local scans into one global prefix (SurfelPrefixMerge.comp).
//       The three inter-pass barriers are the mandatory Vulkan non-deviation: WebGPU inserts them implicitly between compute nodes; miss one and the
//       scan reads a half-written segment array and produces a torn prefix under load. The output overwrites the Offsets buffer IN PLACE.
//
//    📝 This unit OWNS only the SegmentSums scratch buffer, the pipelines, and one descriptor set; the Offsets buffer is BORROWED — the slotting owns
//       it and passes it (with the live element count) at record time, so the descriptor's binding-0 is re-pointed whenever the bound buffer changes.
//       POD struct + free functions, mirroring InstanceCullSubmission.

#pragma once
#ifndef FRONTIER_GRAPHICS_SURFEL_SURFELPREFIXSUM_H
#define FRONTIER_GRAPHICS_SURFEL_SURFELPREFIXSUM_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Scan geometry — ported 1:1 from webgiya surfelHashGrid.ts (WORKGROUP_SIZE 512, SEGMENT_SIZE = 2x). One workgroup owns one segment; one thread
//    owns two elements. These MUST match local_size_x and the SEGMENT_SIZE constant in the four SurfelPrefix*.comp shaders.
constexpr uint32_t SurfelScanWorkgroupSize = 512;                              // [-] - lanes per ScanSeg workgroup
constexpr uint32_t SurfelScanSegmentSize   = SurfelScanWorkgroupSize * 2;      // [-] - 1024 elements per segment
constexpr uint32_t SurfelMergeWorkgroupEdge   = 256;                           // [-] - Merge lane edge (matches SurfelPrefixMerge.comp local_size_x)
// 🔴 CollectSeg dispatches one lane PER SEGMENT and its shader is local_size_x = 64 — NOT 256. Grouping the collect dispatch by SurfelMergeWorkgroupEdge
//    (256) under-launched it: ceil(257/256)=2 groups x 64 lanes = 128 lanes for 257 segments, so segments 128..256 kept STALE SegmentSums from the prior
//    scan and the tail inflated on every re-run. This edge MUST match SurfelPrefixCollectSegment.comp's local_size_x.
constexpr uint32_t SurfelCollectWorkgroupEdge = 64;                            // [-] - CollectSeg lane edge (matches SurfelPrefixCollectSegment.comp local_size_x)

// The number of segments an Offsets array of ElementCount entries splits into: ceil(ElementCount / SEGMENT_SIZE), at least one.
inline uint32_t SurfelScanSegmentCount(uint32_t ElementCount)
{
    if (ElementCount == 0) return 1;
    return (ElementCount + SurfelScanSegmentSize - 1) / SurfelScanSegmentSize;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The push block every prefix pass reads. Not every field is consumed by every pass (ScanSeg/Merge use ElementCount; CollectSeg uses both;
//    SegPrefix uses only SegmentCount) but one shared layout keeps the pipeline layout single. std430 scalar ints, 8 bytes.
struct SurfelPrefixConstants
{
    int32_t ElementCount = 0;   // [-] - totalCells + 1 (the live entry count)
    int32_t SegmentCount = 0;   // [-] - ceil(ElementCount / SEGMENT_SIZE)
};

// 📝 The prefix-sum unit's owned resources. Four compute pipelines share one two-binding set layout (Offsets @0 borrowed, SegmentSums @1 owned).
//    SegmentSums is sized once for the maximum segment count the caller declares; BoundOffsetsBuffer caches the last Offsets handle so the descriptor
//    is re-pointed only when the borrowed buffer changes. ReadyCondition gates recording.
struct SurfelPrefixSum
{
    VulkanHost*           Host           = nullptr;          // [-] - not owned
    VkDescriptorSetLayout SetLayout      = VK_NULL_HANDLE;   // [-] - Offsets @0 + SegmentSums @1, both compute storage
    VkPipelineLayout      PipelineLayout = VK_NULL_HANDLE;   // [-] - SetLayout + SurfelPrefixConstants push range
    VkDescriptorPool      DescriptorPool = VK_NULL_HANDLE;   // [-] - one set
    VkDescriptorSet       PrefixSet      = VK_NULL_HANDLE;   // [-] - the bound set

    VkPipeline            ScanSegPipeline    = VK_NULL_HANDLE;  // [-] - pass 1: per-segment local scan
    VkPipeline            CollectSegPipeline = VK_NULL_HANDLE;  // [-] - pass 2: gather segment totals
    VkPipeline            SegPrefixPipeline  = VK_NULL_HANDLE;  // [-] - pass 3: serial cross-segment prefix
    VkPipeline            MergePipeline      = VK_NULL_HANDLE;  // [-] - pass 4: lift local scans to global

    VkBuffer              SegmentSumsBuffer = VK_NULL_HANDLE;   // [-] - owned scratch: one int per segment
    VkDeviceMemory        SegmentSumsMemory = VK_NULL_HANDLE;

    VkBuffer              BoundOffsetsBuffer = VK_NULL_HANDLE;  // [-] - last Offsets handle bound at @0 (re-pointed on change)

    uint32_t              MaxSegments    = 0;                   // [-] - SegmentSums capacity in ints
    bool                  ReadyCondition = false;               // [-] - true once pipelines + layout + descriptors + buffer are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the four compute pipelines, the shared two-binding set layout + push range, the descriptor pool + one set, and the owned SegmentSums buffer
// sized for the segment count of MaxElementCount entries. ShaderDirectory locates the four SurfelPrefix*.comp.spv. Returns false (ReadyCondition stays
// false, handles null) on any failure. Pair with FinalizeSurfelPrefixSum.
bool InitializeSurfelPrefixSum(SurfelPrefixSum& Prefix,
                               VulkanHost&      Host,
                               uint32_t         MaxElementCount,
                               const char*      ShaderDirectory);

// Record the full four-pass scan of OffsetsBuffer's first ElementCount ints IN PLACE, with the three mandatory inter-pass barriers. Binds OffsetsBuffer
// at @0 (re-pointing the descriptor if it changed). The caller must have already barriered OffsetsBuffer for compute read/write (the count pass wrote
// it). On return the caller must barrier OffsetsBuffer before the slot pass reads it. A no-op when not ready or ElementCount is 0. CommandBuffer must be
// recording, OUTSIDE any rendering scope.
void RecordSurfelPrefixSum(SurfelPrefixSum& Prefix,
                           VkBuffer         OffsetsBuffer,
                           uint32_t         ElementCount,
                           VkCommandBuffer  CommandBuffer);

// Destroy the pipelines / layout / descriptors / SegmentSums buffer, then reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeSurfelPrefixSum(SurfelPrefixSum& Prefix);

} // namespace Frontier

#endif
