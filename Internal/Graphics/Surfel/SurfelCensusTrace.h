/*==============================================================================================================================================
                                                          SURFELCENSUSTRACE.H
==============================================================================================================================================*/
// 🧩 A per-frame population census of the surfel pool, written to ONE CSV as a time series. It exists to answer a question the L-key snapshot
//    dump structurally cannot: does the probe population CHURN? A single snapshot shows a level (how many surfels exist right now); churn is a
//    FLOW (how many were born and how many died to arrive at that level). A pool in violent equilibrium — 2000 spawns and 2000 deaths a frame —
//    reads IDENTICALLY to a settled one in any snapshot, so measuring the flows is the only way to see it.
//
//    🔴 THE FLOWS ARE COUNTED ON THE GPU, NOT DERIVED ON THE HOST. Spawns and deaths happen inside SurfelAllocate/SurfelAge as atomics; the host
//       can only see AliveCount before and after. Differencing that gives the NET change and hides the gross flows completely (the equilibrium
//       case above differences to zero). So the shaders atomicAdd into a small counters buffer and this unit reads the tallies. Deaths are split
//       BY CAUSE (TTL vs police execution) because the two imply different fixes — see the Phase-5 lifecycle work.
//
//    🔴 NON-BLOCKING, READ ONE FRAME LATE — the same discipline as GpuTimestampScope, and for the same reason. The readback exists to MEASURE the
//       frame, so it must not perturb it: a copy+submit+fence per frame (what DumpSurfelStateToDisk does per L press) would stall the queue three
//       times a frame and corrupt the very timings being taken. Instead ONE persistent host-visible staging buffer per ring slot, filled by a
//       vkCmdCopyBuffer recorded INSIDE the caller's live command buffer, and read from the ring slot the GPU has certainly retired.
//
//    🔴 BEST-EFFORT, WITH ONE HARD REQUIREMENT. If allocation fails the whole facility no-ops (ReadyCondition stays false) and the renderer runs the
//       unmeasured path untouched — a diagnostic that cannot record must never change what it records. BUT the CountersBuffer is the exception: the
//       lifecycle's Age/Allocate shaders declare pool-set binding 9 unconditionally, so that binding must ALWAYS point at a buffer of at least
//       SurfelCensusSlotCount ints. It may NOT be aliased onto a pool atomic as a fallback (those are 4 bytes each — the tallies would write out of
//       bounds), so the buffer is allocated at init regardless of whether a recording is ever started, and the renderer must skip the surfel
//       lifecycle dispatches entirely if even that 20-byte allocation failed.
//
//    📝 Distinct from DumpSurfelStateToDisk (SurfelPool.h, the L key) on purpose and both are kept: that one is a deep SNAPSHOT of one instant
//       (every live surfel's position/normal/age + this frame's spawn requests, for the HTML viewer); this one is a shallow TIME SERIES of counts
//       across many frames. Different questions, different shapes, different keys.

#pragma once
#ifndef FRONTIER_GRAPHICS_SURFEL_SURFELCENSUSTRACE_H
#define FRONTIER_GRAPHICS_SURFEL_SURFELCENSUSTRACE_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>

namespace Frontier
{

struct SurfelPool;
struct SurfelTuningState;

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The GPU-side counters buffer layout, as ints. 🔴 THE SHADERS INDEX THESE BY LITERAL — SurfelCensus.glsl mirrors this enum and every
//    atomicAdd site uses those names. Adding a counter means editing both, in one edit, or a shader writes into a slot the host reads as
//    something else. Order is append-only for that reason.
enum SurfelCensusSlot : uint32_t
{
    SurfelCensusSpawned      = 0,   // [-] - successful allocations this frame (SurfelAllocate, after the overflow rollback)
    SurfelCensusSpawnFailed  = 1,   // [-] - allocations that hit pool capacity and rolled back
    SurfelCensusDiedTtl      = 2,   // [-] - deaths by reaching SURFEL_TTL (base metabolism + crowding rent)
    SurfelCensusDiedPolice   = 3,   // [-] - deaths by the spawn pass's despawn vote (the KILL signal)
    SurfelCensusKeepAlive    = 4,   // [-] - surfels that received nonzero keep-alive income this frame
    SurfelCensusSlotCount    = 5,   // [-] - counter count; the buffer is this many ints
};

// The CPU-side ring depth, matching GpuTimestampRingDepth's reasoning: must be >= frames-in-flight so the slot being read is one the GPU has
// retired. Three rings covers a triple-buffered swapchain and costs 3 x 5 ints of staging.
constexpr uint32_t SurfelCensusRingDepth = 3;

// 📝 The per-pass GPU millisecond columns this trace carries alongside the population counts. Names and ORDER mirror RenderExtension::SurfelPassSlot
//    (slotting/spawn/age/integrate/shade/debugSplat) because the renderer copies GpuTimestampScope::ResolvedMillis straight across by index.
//
//    🔴 THE TIMINGS ARE NOT READ OFF THE GPU BY THIS UNIT — THEY ARE HANDED IN, AND THE FRAME ALIGNMENT IS THE CALLER'S CONTRACT. Both this trace and
//       GpuTimestampScope run a 3-deep ring and both read the slot trailing the one being recorded, so at the moment the renderer collects them (in
//       that order: CollectGpuTimestampResults then CollectSurfelCensusRow) the resolved millis describe the SAME frame as the census row. That is why
//       the two numbers may sit on one CSV line at all. Reorder those two collect calls, or change either ring depth alone, and every ms column
//       silently describes a different frame than the counts beside it — a skew that still reads as perfectly plausible data.
constexpr uint32_t SurfelCensusTimingSlotCount = 6;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One frame's census row, as read back off the GPU. Levels come from the pool atomics, flows from the counters buffer.
struct SurfelCensusSample
{
    uint32_t FrameIndex   = 0;      // [-] - the renderer's surfel frame index this row describes
    int32_t  AliveCount   = 0;      // [-] - level: live surfel count
    int32_t  PoolAlloc    = 0;      // [-] - level: free-list stack pointer (allocated slot count)
    int32_t  PoolMax      = 0;      // [-] - level: high-water slot used
    int32_t  Spawned      = 0;      // [-] - flow: allocations this frame
    int32_t  SpawnFailed  = 0;      // [-] - flow: allocations refused at capacity
    int32_t  DiedTtl      = 0;      // [-] - flow: deaths by TTL
    int32_t  DiedPolice   = 0;      // [-] - flow: deaths by despawn vote
    int32_t  KeepAlive    = 0;      // [-] - flow: surfels paid keep-alive income
    bool     Valid        = false;  // [-] - false until the ring slot has been written at least once
};

// 📝 The run's identity, stamped into the CSV header so a file cannot be mistaken for another run's. Passed at BeginSurfelCensusRecording.
//    🔴 THIS EXISTS BECAUSE TWO RUNS PRODUCE INDISTINGUISHABLE FILES OTHERWISE. surfel-census-0000.csv and -0001.csv differ only by sequence number and
//       nothing in the rows says what the field was tuned to, so a mislabelled pair silently inverts a comparison. The settings ride in the file.
struct SurfelCensusRunLabel
{
    float CellDiameter    = 0.0f;    // [m] - world-scale knobs, since they move probe density and therefore every column
    float BaseRadius      = 0.0f;    // [m]
    int   PerCellCap      = 0;       // [-] - the APPLIED per-cell fill ceiling (the value the shaders were looping against)
    float NearFieldBias   = 0.0f;    // [-] - near-field spawn lift (1.0 = inert)
};

// 📝 The trace's owned state. CountersBuffer is the DEVICE-LOCAL tally the shaders atomicAdd into (cleared at the top of every frame); the
//    Staging ring holds the host-visible copies. Recording is: clear counters -> passes tally -> copy counters + the three pool atomics into the
//    record ring slot. Reading is: map the trailing slot, append a CSV row.
struct SurfelCensusTrace
{
    VulkanHost* Host = nullptr;                                   // [-] - not owned

    VkBuffer       CountersBuffer = VK_NULL_HANDLE;                // [-] - SurfelCensusSlotCount ints, device-local, atomicAdd target
    VkDeviceMemory CountersMemory = VK_NULL_HANDLE;

    // One host-visible staging slot per ring: the 5 counters followed by the 3 pool atomics (alive, poolAlloc, poolMax) = 8 ints.
    VkBuffer       StagingBuffer[SurfelCensusRingDepth] = {};      // [-] - host-visible, persistently mapped
    VkDeviceMemory StagingMemory[SurfelCensusRingDepth] = {};
    void*          StagingMapped[SurfelCensusRingDepth] = {};      // [-] - persistent map; never unmapped until Finalize
    uint32_t       StagingFrame[SurfelCensusRingDepth]  = {};      // [-] - the frame index each slot's copy describes
    bool           RingPrimed[SurfelCensusRingDepth]    = {};      // [-] - true once a slot has been copied into

    uint32_t RecordRing = 0;                                       // [-] - the slot this frame's copy writes
    uint32_t CollectRing = 0;                                      // [-] - the slot the next read consumes (trails RecordRing)

    // 📝 The per-pass GPU millis for the row about to be written, handed in by the renderer each frame (see SurfelCensusTimingSlotCount). Held on the
    //    struct rather than passed to Collect so the collect call site stays a bare one-liner and a caller that never supplies timings simply gets
    //    zeros — the population columns are unaffected, which keeps the facility useful on a device with no timestamp support.
    float    PassMillis[SurfelCensusTimingSlotCount] = {};         // [ms] - slotting, spawn, age, integrate, shade, debugSplat

    bool        Recording   = false;                               // [-] - true while a trace is capturing (the key toggles this)
    std::string OutputPath;                                        // [-] - the open CSV's path (empty when not recording)
    void*       OutputFile  = nullptr;                             // [-] - FILE* held open for the whole trace (append per frame)
    uint32_t    RowsWritten = 0;                                   // [-] - rows appended to the current CSV
    uint32_t    TraceSequence = 0;                                 // [-] - monotonic per-trace counter; names each CSV

    bool ReadyCondition = false;                                   // [-] - true once counters + every ring slot allocated and mapped
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Fill a run label from the live tuning state. 🔴 EVERY CALL SITE MUST USE THIS RATHER THAN ASSIGNING FIELDS. There are two independent arm points
// (the FRONTIER_SURFEL_CENSUS auto-arm at startup and the K-key interactive arm) and a hand-written fill at each one means a newly added flag gets
// stamped at whichever site the author remembered. The failure is silent and it inverts conclusions: the CSV asserts the flag was OFF while the run
// measured it ON, which reads as a clean result for the wrong configuration. One composer, both sites, so adding a field cannot be half-wired.
SurfelCensusRunLabel ComposeSurfelCensusRunLabel(const SurfelTuningState& Tuning);

// Allocate the device-local counters buffer + the host-visible staging ring and map every slot persistently. Returns false (ReadyCondition stays
// false, every call below a no-op) on any allocation failure. Pair with FinalizeSurfelCensusTrace.
bool InitializeSurfelCensusTrace(SurfelCensusTrace& Trace, VulkanHost& Host);

// Zero the counters buffer. 🔴 Call ONCE per frame at the TOP of the surfel command recording, BEFORE the spawn/age/allocate dispatches tally into
// it — a missed clear makes every row a running total instead of a per-frame flow. Needs an open command buffer (records a vkCmdFillBuffer) and
// emits the barrier that makes the fill visible to the following dispatches. A no-op when not ready.
void BeginSurfelCensusFrame(SurfelCensusTrace& Trace, VkCommandBuffer CommandBuffer);

// Record the copy of the counters + the three pool atomics into this frame's ring slot, and advance the record ring. 🔴 Call ONCE per frame AFTER
// the last pass that tallies (the Age dispatch) and after its barrier, inside the same command buffer. Records only copies — never waits. A no-op
// when not ready or not Recording.
void RecordSurfelCensusCopy(SurfelCensusTrace& Trace,
                            const SurfelPool&  Pool,
                            VkCommandBuffer    CommandBuffer,
                            uint32_t           FrameIndex);

// Copy this frame's per-pass GPU millis into the trace so the next collected row carries them. Millis must point at SurfelCensusTimingSlotCount
// floats in SurfelPassSlot order (GpuTimestampScope::ResolvedMillis is exactly that). 🔴 Call AFTER CollectGpuTimestampResults and BEFORE
// CollectSurfelCensusRow in the same frame — that ordering is what makes the ms columns describe the same frame as the counts (see the note on
// SurfelCensusTimingSlotCount). Safe with a null pointer (leaves the previous values) and when not recording.
void SupplySurfelCensusTimings(SurfelCensusTrace& Trace, const float* Millis, uint32_t MillisCount);

// Read the ring slot that trails the one now being recorded (the GPU has retired it) and append one CSV row. Never waits on the GPU. Call ONCE per
// frame, anywhere outside command recording. A no-op when not ready, not Recording, or the trailing slot was never written.
void CollectSurfelCensusRow(SurfelCensusTrace& Trace);

// Open a new numbered CSV under Directory and start capturing (writes the header row). Each call names a distinct file so successive traces
// accumulate. A no-op when not ready or already Recording. Returns false if the file could not be opened. Label is stamped into the header comments
// so the file records WHICH A/B arm produced it — pass nullptr only for a run whose settings do not matter.
bool BeginSurfelCensusRecording(SurfelCensusTrace& Trace, const char* Directory, const SurfelCensusRunLabel* Label = nullptr);

// Close the open CSV and stop capturing. Safe when not Recording. Returns the row count the trace wrote.
uint32_t EndSurfelCensusRecording(SurfelCensusTrace& Trace);

// Destroy the counters buffer + the staging ring (unmapping every slot) and close any open CSV. The device must be idle. Safe on a
// never-initialized value.
void FinalizeSurfelCensusTrace(SurfelCensusTrace& Trace);

} // namespace Frontier

#endif
