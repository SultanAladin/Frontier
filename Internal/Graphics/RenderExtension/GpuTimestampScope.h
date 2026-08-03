/*==============================================================================================================================================
                                                          GPUTIMESTAMPSCOPE.H
==============================================================================================================================================*/
// 🧩 A best-effort GPU wall-clock probe. Owns ONE VkQueryPool of timestamp queries and brackets any number of named passes with a begin/end pair,
//    so the caller can attribute real GPU milliseconds to each per-frame dispatch/draw. It exists because nothing in Graphics measured GPU cost —
//    "the integrate is the hot path" was a guess, and the temporal-shadow / convergence-gate work should be aimed by numbers, not by hunch.
//
//    🔴 RESULTS ARE READ ONE FRAME LATE, NEVER BLOCKING. Timestamps resolve asynchronously on the GPU; reading THIS frame's pool with WAIT would
//       stall the CPU on the GPU. So the pool is sized for FramesInFlight rings and CollectResults reads the OLDEST ring with no wait bit — the
//       slot the GPU has certainly finished. A ring never yet written reads back as zero, which the caller shows as "no sample yet", not a fault.
//
//    🔴 BEST-EFFORT, LIKE THE SHADOW SET. If the device reports timestampComputeAndGraphics off or the graphics queue's timestampValidBits == 0,
//       the whole facility no-ops (ReadyCondition stays false); Begin/EndScope/Collect become nothing and ResolvedMillis returns 0. A profiler that
//       cannot measure must never change what it measures, so an unsupported device runs EXACTLY the unbracketed path.
//
//    📝 POD struct + free functions, mirroring the surfel submission units. No owned buffers beyond the query pool. vkCmdWriteTimestamp is legal both
//       inside and outside a dynamic-rendering scope, so the same scope brackets a compute dispatch (open command buffer) and the shade (radiance scope).

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDEREXTENSION_GPUTIMESTAMPSCOPE_H
#define FRONTIER_GRAPHICS_RENDEREXTENSION_GPUTIMESTAMPSCOPE_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The most passes one scope can bracket. Six today (Slotting/Spawn/Age/Integrate/Shade/DebugSplat); a small ceiling keeps the pool tiny (two
//    queries per slot per ring) while leaving headroom. A begin/end past this cap is a silent no-op, never an over-write of another slot's query.
constexpr uint32_t GpuTimestampMaxSlots = 16;

// The CPU-side ring depth. Must be >= the renderer's frames-in-flight so CollectResults always reads a ring the GPU has retired. Two is the common
// double-buffered case; a third ring is cheap insurance against a triple-buffered swapchain and costs 16 more 8-byte queries.
constexpr uint32_t GpuTimestampRingDepth = 3;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The probe's owned state. QueryPool holds GpuTimestampMaxSlots*2*GpuTimestampRingDepth timestamps; NanosecondsPerTick converts a tick delta to
//    real time (VkPhysicalDeviceLimits.timestampPeriod). ResolvedMillis[] is the last successfully-read per-slot delta for the ring Collect read.
//    RecordRing is the ring the NEXT Begin/EndScope pair writes into; CollectRing trails it by the ring depth. SlotUsed marks which slots were
//    bracketed this record pass so Collect only converts real pairs.
struct GpuTimestampScope
{
    VulkanHost* Host = nullptr;                              // [-] - not owned; supplies device + physical-device limits

    VkQueryPool QueryPool = VK_NULL_HANDLE;                  // [-] - GpuTimestampMaxSlots*2*GpuTimestampRingDepth timestamp queries

    double NanosecondsPerTick = 0.0;                         // [-] - timestampPeriod (ns per tick); 0 until Initialize succeeds

    uint32_t RecordRing = 0;                                 // [-] - the ring index Begin/EndScope write this frame
    uint32_t CollectRing = 0;                                // [-] - the ring index CollectResults reads (trails RecordRing by the depth)

    bool     SlotUsed[GpuTimestampMaxSlots] = {};            // [-] - which slots were bracketed in the ring currently being recorded
    bool     RingPrimed[GpuTimestampRingDepth] = {};         // [-] - true once a ring has been written at least once (unwritten reads back zero)

    float    ResolvedMillis[GpuTimestampMaxSlots] = {};      // [-] - last read per-slot GPU milliseconds (0 = no sample yet / unsupported)

    bool     ReadyCondition = false;                         // [-] - true once the pool exists AND the device supports timestamps on the graphics queue
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Create the query pool and read timestampPeriod from the physical device. Returns false (ReadyCondition stays false, every call a no-op) when the
// device reports no timestamp support on the graphics queue (timestampComputeAndGraphics off OR the queue family's timestampValidBits == 0). Pair
// with FinalizeGpuTimestampScope. Never fatal: an unsupported device simply runs unmeasured.
bool InitializeGpuTimestampScope(GpuTimestampScope& Scope, VulkanHost& Host);

// Reset the ring the coming frame will write and advance to it. Call ONCE at the top of a frame's command recording, BEFORE the first BeginScope and
// AFTER the command buffer's vkBeginCommandBuffer (vkCmdResetQueryPool needs an open command buffer). A no-op when not ready.
void BeginGpuTimestampFrame(GpuTimestampScope& Scope, VkCommandBuffer CommandBuffer);

// Write the "begin" timestamp for a slot (top-of-pipe, so it stamps when the GPU reaches this point). Slot must be < GpuTimestampMaxSlots. A no-op
// when not ready or the slot is out of range. Pair each BeginScope with exactly one EndScope in the same frame.
void BeginGpuTimestampScope(GpuTimestampScope& Scope, VkCommandBuffer CommandBuffer, uint32_t Slot);

// Write the "end" timestamp for a slot (bottom-of-pipe, so it stamps when all work up to here has completed — the pair delta is the pass's GPU time).
// A no-op when not ready or the slot was not begun this frame.
void EndGpuTimestampScope(GpuTimestampScope& Scope, VkCommandBuffer CommandBuffer, uint32_t Slot);

// Read back the ring that trails the one now being recorded (the GPU has retired it) with NO wait bit, convert each begun slot's tick delta to
// milliseconds into ResolvedMillis[], and advance the collect ring. Call ONCE per frame; safe before the first frames have primed the ring (a slot
// with no result yet keeps its previous / zero value). A no-op when not ready.
void CollectGpuTimestampResults(GpuTimestampScope& Scope);

// Destroy the query pool and reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeGpuTimestampScope(GpuTimestampScope& Scope);

} // namespace Frontier

#endif
