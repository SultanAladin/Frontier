/*==============================================================================================================================================
                                                              SURFELPOOL.H
==============================================================================================================================================*/
// 🧩 The persistent home of every surfel — the device-local storage the whole surfel GI subsystem reads and writes across frames. A "surfel" is a
//    small oriented disc glued to a surface that caches indirect light there; the pool is the flat array of them plus the free-list that hands out
//    and reclaims slots as surfels spawn and age out. This unit OWNS all of that memory and nothing else: it does not slot, spawn, trace, or shade —
//    those are separate submissions that borrow these buffers. Built once at a fixed capacity (the pool never grows mid-run), the buffers cleared to
//    their correct initial states, then left resident for the lifetime of the renderer. POD struct + free functions, mirroring InstanceCullSubmission.
//
//    🔴 THE INITIAL-STATE DISCIPLINE IS LOAD-BEARING, AND IT IS NOT UNIFORM ACROSS THE BUFFERS (PLAN §8 F7 / F21):
//       • FOUR buffers are ZERO-FILLED at init and again is NOT needed each frame here: Moments, Touched, Guiding, SurfelDepth. WebGPU zero-inits
//         every new buffer implicitly; Vulkan does not, so these four carry an explicit vkCmdFillBuffer(0). Omitting any one is a broken port, not a
//         deviation — a non-zeroed Moments/Depth buffer feeds NaNs into the integrate the moment Phase 2 turns it on.
//       • The Pool free-list is SEEDED 0,1,2,…,capacity-1 (an identity stack), NOT zeroed — a zeroed free-list hands out slot 0 to every spawn.
//       • The Age field inside each surfel is NOT touched here. It is seeded by the lifecycle's Prepare pass to SURFEL_LIFE_RECYCLED (F21); zero-
//         filling ages would make every slot read as a live age-0 surfel before a single one has spawned. Prepare is what makes the missing bounds
//         guard safe, so the pool must leave ages alone and let Prepare own them.
//
//    📝 Naming: …Pool because it owns storage and a free-list, not a submission (it records no dispatch of its own). The moments buffer is double-
//       buffered (ping-pong) but this unit only ALLOCATES it and tracks the parity; the swap and the reads/writes belong to the integrate (Phase 2).

#pragma once
#ifndef FRONTIER_GRAPHICS_SURFEL_SURFELPOOL_H
#define FRONTIER_GRAPHICS_SURFEL_SURFELPOOL_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Structural pool constants — ported 1:1 from webgiya constants.ts (desktop profile). These fix the buffer sizes and MUST match the shader-side
//    values in SurfelGrid.glsl and the per-stage .comp files. MaxSurfels is the hard pool capacity; the pool never grows past it.
constexpr uint32_t SurfelMaxCount        = 262144;   // [-] - pool capacity (webgiya MAX_SURFELS, desktop)
constexpr uint32_t SurfelMomentsFloats   = 20;       // [-] - floats per surfel per moments half (webgiya floatsPerMoment)
constexpr uint32_t SurfelGuidingFloats   = 72;       // [-] - SLG_TOTAL_FLOATS = SLG_LOBE_COUNT(64) + SLG_DIM(8)
constexpr uint32_t SurfelDepthTexels     = 4;        // [-] - SURFEL_DEPTH_TEXELS; each surfel owns a 4x4 MSM tile
constexpr uint32_t SurfelDepthFloats     = SurfelDepthTexels * SurfelDepthTexels * 4;  // [-] - 16 texels x vec4 = 64 floats
constexpr uint32_t SurfelStrideFloats    = 8;        // [-] - vec4 posb + vec3 normal + int age, std430 stride 32 B

// 🔴 Lifecycle sentinels — ported 1:1 from webgiya constants.ts. SURFEL_LIFE_RECYCLE is bit 27 (0x8000000), NOT the sign bit, so a recycled age
//    stays a large POSITIVE int and the arithmetic on it never trips a sign. Prepare seeds ages to SURFEL_LIFE_RECYCLED (F21).
constexpr int32_t SurfelLifeRecycle   = 0x8000000;        // [-] - "this slot is recycled" marker base
constexpr int32_t SurfelLifeRecycled  = SurfelLifeRecycle + 1;  // [-] - the exact seed Prepare writes into every age

// 📝 TTL threshold — a slot is LIVE when its Age is below this. There is no other host copy: the shader-side authority is
//    SURFEL_TTL in SurfelGrid.glsl (currently 500). 🔴 Keep the two in lockstep — a drift here mis-classifies live surfels
//    in the dump readback only (this constant is not fed to any shader), but the dump's liveness filter must match the field.
constexpr int32_t SurfelTtl = 500;                        // [-] - Age < SurfelTtl (and != SurfelLifeRecycle) == live

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One surfel as it sits in the pool's surfel buffer, std430 stride 32 B. 🔴 FIELD ORDER IS EXACT (PLAN §8 F14): posb (xyz position + w spare),
//    normal (xyz), then age. webgiya packs it as 8 floats — posb at 0, normal at 16, age at 28 — and the slotting/spawn/trace all index by this
//    stride. Reordering (e.g. age before normal) mis-strides the whole array and every surfel after the first reads a blend of its neighbours.
struct SurfelRecord
{
    float   PositionX = 0.0f;  // @0
    float   PositionY = 0.0f;  // @4
    float   PositionZ = 0.0f;  // @8
    float   PositionW = 0.0f;  // @12 - spare (webgiya posb.w)
    float   NormalX   = 0.0f;  // @16
    float   NormalY   = 0.0f;  // @20
    float   NormalZ   = 0.0f;  // @24
    int32_t Age       = 0;     // @28 - seeded by Prepare, NOT by the pool
};

// 📝 The pool's owned device resources. Every buffer is device-local. The three single-int atomics (AliveCount, PoolAllocCount, PoolMaxCount) are the
//    stack/alive bookkeeping the lifecycle drives; SurfelBuffer holds the records; PoolBuffer is the free-list stack seeded 0..cap-1; the remaining
//    four (Moments, Touched, Guiding, SurfelDepth) are the F7 zero-filled buffers. MomentsParity tracks the ping-pong half the integrate will swap.
struct SurfelPool
{
    VulkanHost* Host = nullptr;                       // [-] - not owned; supplies device / physical device / allocator

    VkBuffer       SurfelBuffer    = VK_NULL_HANDLE;  // [-] - capacity x SurfelRecord (stride 32 B)
    VkDeviceMemory SurfelMemory    = VK_NULL_HANDLE;
    VkBuffer       PoolBuffer      = VK_NULL_HANDLE;  // [-] - capacity x int free-list stack, seeded 0..cap-1
    VkDeviceMemory PoolMemory      = VK_NULL_HANDLE;
    VkBuffer       AliveCountBuffer = VK_NULL_HANDLE; // [-] - 1 x int atomic: live surfel count
    VkDeviceMemory AliveCountMemory = VK_NULL_HANDLE;
    VkBuffer       PoolAllocBuffer  = VK_NULL_HANDLE; // [-] - 1 x int atomic: free-list stack pointer
    VkDeviceMemory PoolAllocMemory  = VK_NULL_HANDLE;
    VkBuffer       PoolMaxBuffer     = VK_NULL_HANDLE;// [-] - 1 x int atomic: high-water slot used
    VkDeviceMemory PoolMaxMemory     = VK_NULL_HANDLE;

    // --- the four F7 zero-filled buffers ---
    VkBuffer       MomentsBuffer    = VK_NULL_HANDLE; // [-] - capacity x SurfelMomentsFloats x 2 (double-buffered)
    VkDeviceMemory MomentsMemory    = VK_NULL_HANDLE;
    VkBuffer       TouchedBuffer    = VK_NULL_HANDLE; // [-] - capacity x int (per-surfel touched flag)
    VkDeviceMemory TouchedMemory    = VK_NULL_HANDLE;
    VkBuffer       GuidingBuffer    = VK_NULL_HANDLE; // [-] - capacity x SurfelGuidingFloats (SLG lobe weights)
    VkDeviceMemory GuidingMemory    = VK_NULL_HANDLE;
    VkBuffer       SurfelDepthBuffer = VK_NULL_HANDLE;// [-] - capacity x SurfelDepthFloats (MSM radial-depth tiles)
    VkDeviceMemory SurfelDepthMemory = VK_NULL_HANDLE;

    uint32_t Capacity      = 0;     // [-] - surfel capacity every per-surfel buffer is sized for
    uint32_t MomentsParity = 0;     // [-] - ping-pong parity; read half = parity, write half = 1-parity (integrate swaps it)
    bool     ReadyCondition = false;// [-] - true once every buffer is allocated and cleared to its initial state
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Allocate every pool buffer at Capacity surfels and clear each to its correct INITIAL STATE (see the F7/F21 discipline in the file header): the four
// zero-fill buffers to 0, the free-list to the identity stack 0..cap-1, the three atomics to 0. Ages are LEFT UNTOUCHED (Prepare owns them).
// CommandPool submits the clears and the function waits, so the pool is resident and correct on return. Returns false (ReadyCondition stays false,
// handles null) on any failure. Pair with FinalizeSurfelPool.
bool InitializeSurfelPool(SurfelPool&   Pool,
                          VulkanHost&   Host,
                          VkCommandPool CommandPool,
                          uint32_t      Capacity);

// The read-half byte offset into MomentsBuffer for the current parity (MomentsParity * Capacity * SurfelMomentsFloats floats). The write half is the
// other one. Mirrors webgiya getOffsets(): readOffset = parity*cap, writeOffset = (1-parity)*cap.
VkDeviceSize SurfelMomentsReadOffset(const SurfelPool& Pool);
VkDeviceSize SurfelMomentsWriteOffset(const SurfelPool& Pool);

// Flip the moments ping-pong parity. Called by the integrate (Phase 2) AFTER a frame's allocate/resolve, per PLAN §6 ordering — Phase 1 only allocates
// the buffer, so this is here for completeness and exercised by the gate.
void SwapSurfelMoments(SurfelPool& Pool);

// Destroy every buffer + backing memory and reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeSurfelPool(SurfelPool& Pool);

//------------------------------------------------------------------------------------------------------------------------
//                                                       DIAGNOSTIC DUMP
//------------------------------------------------------------------------------------------------------------------------

// 🩺 Copy the live surfel records + this-frame's per-tile spawn-request buffers OFF the GPU and write three files under
//    OutputDirectory (typically _ClaudeScratch/logs): surfel-live.csv, surfel-spawns.csv, surfel-dump.json. This is a
//    keypress-triggered diagnostic (the 'L' key), NOT a per-frame path, so it may — and does — take the cheap route: it
//    records a one-shot copy of every source buffer into host-visible staging on its OWN transient command buffer, submits
//    it, and waits on a fence (a localized stall the L press pays for). It touches no pool state and records nothing into
//    the caller's frame command buffer, so it is safe to call between frames.
//
//    🔴 THE FIRE-POINT CONTRACT: the caller must have ALREADY submitted (and ideally waited on) the frame whose spawn the
//       user pressed L on, so that TileAllocBuffer / TileCandidateBuffer hold that frame's fully-written requests before the
//       copy is recorded. This function does its own copy+submit+wait; it does not synchronise against the caller's frame.
//
//    The tile buffers are passed as raw handles + a tile count rather than the SurfelLifecycleSubmission type on purpose:
//    SurfelLifecycleSubmission.h already includes THIS header, so referencing that type here would form an include cycle.
//    Pass Lifecycle.TileAllocBuffer, Lifecycle.TileCandidateBuffer, and Lifecycle.TileCapacity. Either tile handle may be
//    VK_NULL_HANDLE (spawn dump is then skipped, live dump still written). Returns false on any allocation/submit failure.
// DumpSequence is a monotonic per-press counter: each L press passes a distinct value so successive dumps ACCUMULATE as a
// numbered set (surfel-dump-0000.json, -0001, …) instead of overwriting. OutputDirectory is created if it does not exist.
bool DumpSurfelStateToDisk(const SurfelPool& Pool,
                           VkBuffer          TileAllocBuffer,
                           VkBuffer          TileCandidateBuffer,
                           uint32_t          TileCount,
                           VkCommandPool     CommandPool,
                           const float       CameraEye[3],
                           uint32_t          FrameIndex,
                           uint32_t          DumpSequence,
                           const char*       OutputDirectory);

} // namespace Frontier

#endif
