/*==============================================================================================================================================
                                                       SURFELLIFECYCLESUBMISSION.H
==============================================================================================================================================*/
// 🧩 The per-frame surfel bookkeeping recorded as four compute dispatches: roll the counters over, census last frame's survivors, size each cell's
//    run in the cell-list table, then write the entries. On return the field has an aged population, a rebuilt live set, a freed vacancy stack, a
//    claimed ray budget, and a cell grid the trace and the shade can look surfels up through. Ported 1:1 from W298/SurfelGI
//    (SurfelPreparePass + SurfelUpdatePass's three entry points). Owns pipelines and descriptors; BORROWS every buffer from SurfelStore.
//
// 🔴 THE FOUR DISPATCHES ARE ONE INDIVISIBLE SEQUENCE. Three orderings inside it are load-bearing and none fails loudly when broken:
//       ⓞ the cell spans are zeroed BEFORE the census, by a buffer fill. Upstream omits this and its counts accumulate across frames at a steady 2x —
//          see the long note at the fill in the .cpp. Skip it and the cell table's usable capacity halves, visibly only under load.
//       ① the LiveIndex -> PendingIndex buffer copy sits between dispatch 1 and dispatch 2 (upstream SurfelGI.cpp:100, a copyResource straight
//          after the prepare dispatch). Pass 1 moves the COUNT, this submission moves the LIST. Reverse the direction and the census re-processes
//          the set it is writing.
//       ② the offset scan (3) leaves SurfelCellSpan::SurfelCount at zero for the scatter (4) to use as a write cursor, having consumed it as a
//          count from the census (2). A missing barrier at either boundary puts every cell's entries one full run out of place.
//    ⚠️ There is no partial-record mode. RecordSurfelLifecycle either records all four with their barriers or records nothing.
//
// 📝 One descriptor set, one pipeline layout, four pipelines. Every pass includes Shaders/SurfelStoreBindings.glsl, so all four were compiled
//    against the SAME twelve-binding set 0 and the SAME push range — which is what lets this bind the set and push the constants ONCE and then run
//    the sequence with nothing but pipeline swaps and barriers in between.
// ⚠️ The set is written when the bound store handles change, not every frame. SurfelStore allocates once and stays resident, so in practice that is
//    a single write at first record; the comparison exists because a store rebuild (a resolution or budget change) must not leave stale descriptors.

#pragma once
#ifndef FRONTIER_GRAPHICS_SURFEL_SURFELLIFECYCLESUBMISSION_H
#define FRONTIER_GRAPHICS_SURFEL_SURFELLIFECYCLESUBMISSION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Surfel/SurfelStore.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 THE HOST MIRROR OF THE PUSH BLOCK IN Shaders/SurfelStoreBindings.glsl, FIELD FOR FIELD IN THE SAME ORDER. Scalars only, for the same reason
//    every record in SurfelTypes.h is scalar: a vec3 in the GLSL block would carry 16-byte alignment and shift every field after it, which reads
//    as a plausible field of wrong surfels rather than as an error. The asserts below pin it.
// 🔴 THE CAMERA ORIGIN IS PUSHED ONCE AND USED BY BOTH THE CENSUS AND THE SCATTER. Each resolves a surfel's cell from it, and
//    ResolveCellCoordinate's round() is free to go either way on an exact half-cell tie (SurfelCellGrid.glsl:65). Two different origins inside one
//    frame can therefore put a boundary surfel in the census's cell and the scatter's neighbour, leaving one reserved slot never written.
struct SurfelLifecycleConstants
{
    float    CameraX = 0.0f, CameraY = 0.0f, CameraZ = 0.0f;   // [m]   - the frame's camera origin; the cell grid is relative to it
    uint32_t ResolutionX = 0u, ResolutionY = 0u;               // [px]  - render extent, for the screen-projected radius
    float    VerticalFieldOfView = 0.0f;                       // [rad] - vertical FOV, same
    float    TargetArea = 0.0f;                                // [px²] - screen area one surfel aims to cover (SurfelGridProportions::TargetArea)
    float    VarianceSensitivity = 1.0f;                       // [-]   - how hard MSME variance pushes a surfel's ray count up the ladder
    uint32_t MinimumRayCount = 2u;                             // [-]   - ray ladder floor (and a sleeping surfel's ceiling)
    uint32_t MaximumRayCount = 8u;                             // [-]   - ray ladder ceiling
    uint32_t LockEnabled = 0u;                                 // [-]   - non-zero freezes the field: no radius, ray or recycle writes (upstream mLockSurfel)
    uint32_t CellCount = 0u;                                   // [-]   - CellDimension³; the offset scan's dispatch bound
};

// 📝 The four pipelines and the plumbing they share. ShaderDirectory is not retained — pipelines are built once at initialize and the directory has
//    no meaning afterwards.
struct SurfelLifecycleSubmission
{
    VulkanHost* Host = nullptr;                                   // [-] - not owned; supplies device / queue / allocator

    VkDescriptorSetLayout SetLayout      = VK_NULL_HANDLE;        // [-] - the twelve storage bindings of set 0
    VkPipelineLayout      PipelineLayout = VK_NULL_HANDLE;        // [-] - that set + the SurfelLifecycleConstants push range
    VkDescriptorPool      DescriptorPool = VK_NULL_HANDLE;        // [-] - holds the one set
    VkDescriptorSet       StoreSet       = VK_NULL_HANDLE;        // [-] - the store's buffers, written when the handles change

    VkPipeline CounterResetPipeline  = VK_NULL_HANDLE;            // [-] - pass 1, SurfelCounterReset.comp
    VkPipeline CellCensusPipeline    = VK_NULL_HANDLE;            // [-] - pass 2, SurfelCellCensus.comp
    VkPipeline CellOffsetScanPipeline = VK_NULL_HANDLE;           // [-] - pass 3, SurfelCellOffsetScan.comp
    VkPipeline CellScatterPipeline   = VK_NULL_HANDLE;            // [-] - pass 4, SurfelCellScatter.comp

    VkBuffer BoundRecordBuffer = VK_NULL_HANDLE;                  // [-] - the store handle the set was last written against; guards a stale set
    uint32_t Capacity          = 0u;                              // [-] - surfel capacity the census/scatter dispatches are sized from
    uint32_t CellCount         = 0u;                              // [-] - cell count the offset scan's dispatch is sized from
    bool     ReadyCondition    = false;                           // [-] - true only once the layout, descriptors and all four pipelines exist
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the set layout, the pipeline layout with the push range, the descriptor pool and set, and all four compute pipelines from the four
// SurfelCellCensus / SurfelCounterReset / SurfelCellOffsetScan / SurfelCellScatter `.comp.spv` modules in ShaderDirectory.
//
// Two-phase, no-partial-submission contract, matching InitializeSurfelStore: Submission is reset to its empty value first and ReadyCondition is
// raised only once every step succeeded; any failure releases what was claimed and returns false with all handles null. Records nothing and submits
// nothing. Pair with FinalizeSurfelLifecycleSubmission.
bool InitializeSurfelLifecycleSubmission(SurfelLifecycleSubmission& Submission,
                                         VulkanHost&                Host,
                                         const SurfelStore&         Store,
                                         const char*                ShaderDirectory);

// Record the cell-span clear, the four dispatches, the Live->Pending copy and every barrier between them onto CommandBuffer. Records only — no submit,
// no fence, no wait; this rides the caller's frame command buffer.
//
// ⚠️ The caller owns the barrier AFTER this. The trace, the integrate and the shade read SurfelRecords / CellSpan / CellList / RayOutcomes, and the
//    fence that makes the scatter's writes visible to them belongs to whichever unit reads them next.
// 📝 A no-op (recording nothing at all) when either side is not ready, so a failed store or a failed pipeline build degrades to "no GI this frame"
//    rather than to a partially-recorded sequence.
void RecordSurfelLifecycle(SurfelLifecycleSubmission&     Submission,
                           const SurfelStore&             Store,
                           const SurfelLifecycleConstants& Constants,
                           VkCommandBuffer                CommandBuffer);

// Destroy the four pipelines, the layouts and the descriptor pool, then reset Submission to its empty value. Null-guarded on every branch, so it is
// safe on an empty or partially-built submission and is idempotent. ⚠️ The device must be idle, or the caller must otherwise guarantee no frame in
// flight still references these pipelines.
void FinalizeSurfelLifecycleSubmission(SurfelLifecycleSubmission& Submission);

//------------------------------------------------------------------------------------------------------------------------
//                                                        LAYOUT ASSERTS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The push block is the one place this unit and its four shaders exchange values without a descriptor to check them. std430 scalars have 4-byte
//    alignment and no interior padding on either side, so these offsets are the whole contract — and a reordering that leaves sizeof() unchanged is
//    exactly what they exist to catch.
static_assert(offsetof(SurfelLifecycleConstants, CameraX)             ==  0, "Push CameraX must sit at 0");
static_assert(offsetof(SurfelLifecycleConstants, ResolutionX)         == 12, "Push ResolutionX must sit at 12");
static_assert(offsetof(SurfelLifecycleConstants, VerticalFieldOfView) == 20, "Push VerticalFieldOfView must sit at 20");
static_assert(offsetof(SurfelLifecycleConstants, TargetArea)          == 24, "Push TargetArea must sit at 24");
static_assert(offsetof(SurfelLifecycleConstants, VarianceSensitivity) == 28, "Push VarianceSensitivity must sit at 28");
static_assert(offsetof(SurfelLifecycleConstants, MinimumRayCount)     == 32, "Push MinimumRayCount must sit at 32");
static_assert(offsetof(SurfelLifecycleConstants, MaximumRayCount)     == 36, "Push MaximumRayCount must sit at 36");
static_assert(offsetof(SurfelLifecycleConstants, LockEnabled)         == 40, "Push LockEnabled must sit at 40");
static_assert(offsetof(SurfelLifecycleConstants, CellCount)           == 44, "Push CellCount must sit at 44");
static_assert(sizeof(SurfelLifecycleConstants) == 48, "The push block is 48 bytes; the GLSL block must match exactly");

// 📝 Vulkan guarantees only 128 bytes of push range, so this must stay well inside it — it does, at 48.
static_assert(sizeof(SurfelLifecycleConstants) <= 128, "Push blocks above 128 B are not portably available");

} // namespace Frontier

#endif
