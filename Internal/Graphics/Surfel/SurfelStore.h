/*==============================================================================================================================================
                                                              SURFELSTORE.H
==============================================================================================================================================*/
// 🧩 The persistent device-local home of the surfel field: thirteen buffers plus two atlases, allocated once at a fixed capacity and left resident
//    for the renderer's lifetime. A surfel is a small oriented disc glued to a surface that caches the indirect light arriving there; this unit owns
//    that storage and NOTHING else — it records no dispatch, walks no grid, traces no ray. The lifecycle, trace and integrate submissions borrow
//    these handles. POD struct + free functions, mirroring the SurfelPool / InstanceCullSubmission idiom the rest of Graphics uses.
//
// 🔴 THE INITIAL STATE IS NOT UNIFORM ACROSS THE BUFFERS, AND IT IS LOAD-BEARING. Falcor's createStructuredBuffer zero-initializes and takes an
//    explicit seed pointer; Vulkan gives neither, so each buffer's correct starting bytes are written here or not at all:
//       ① CounterBuffer is seeded from SurfelCounterSeed — FreeSurfel = SurfelTotalLimit, everything else 0. 🔴 A ZERO-FILLED COUNTER READS AS
//          "NO FREE SLOTS" AND NOTHING EVER SPAWNS. The field stays empty forever with no error anywhere: the single most silent failure here.
//       ② VacancyTable is seeded to the identity 0,1,2,…,limit-1 (upstream's std::iota free list), NOT zeroed — a zeroed table hands slot 0 to
//          every spawn, so the whole field collapses onto one surfel.
//       ③ Everything else zero-fills, which for CellSpanBuffer / ReservationBuffer / ReferenceTally is also their correct per-frame reset.
//       ④ SurfelBuffer's MeanEstimator is deliberately NOT seeded here even though Inconsistency must start at 1.0 — a slot's estimator is seeded by
//          SeedSurfel() at SPAWN, which is the only moment the slot becomes live. Pre-seeding 65536 estimators the spawn overwrites anyway would
//          need a staged 9 MB upload for no effect.
//
// 📝 Naming: …Store because it owns storage (SKILL-Naming §0: contiguous-memory register), not a …Submission — it records nothing. The atlases are
//    images rather than buffers because the shade path samples them bilinearly through the 7x7 tile border; a buffer would need a manual filter.

#pragma once
#ifndef FRONTIER_GRAPHICS_SURFEL_SURFELSTORE_H
#define FRONTIER_GRAPHICS_SURFEL_SURFELSTORE_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Surfel/SurfelTypes.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One device-local buffer and the allocation backing it. Every storage buffer in the store is this pair, so the allocate / release / readback
//    helpers take one of these instead of two loose handles that can be freed out of step.
struct SurfelBufferSlot
{
    VkBuffer       Buffer     = VK_NULL_HANDLE;   // [-] - the device-local buffer itself
    VkDeviceMemory Memory     = VK_NULL_HANDLE;   // [-] - its backing allocation
    VkDeviceSize   ByteLength = 0;                // [B] - bytes allocated, retained for the clears and for the readback bounds
};

// 📝 One device-local image plus the view the passes bind. Both atlases are STORAGE (the integrate writes them) and SAMPLED (the shade reads them).
struct SurfelAtlasSlot
{
    VkImage        Image      = VK_NULL_HANDLE;   // [-] - the atlas image
    VkDeviceMemory Memory     = VK_NULL_HANDLE;   // [-] - its backing allocation
    VkImageView    View       = VK_NULL_HANDLE;   // [-] - the full-subresource view the descriptors point at
    VkFormat       Format     = VK_FORMAT_UNDEFINED;  // [-] - R32_SFLOAT (irradiance) or R32G32_SFLOAT (depth moments)
    uint32_t       Width      = 0;                // [px] - SurfelAtlasWidth
    uint32_t       Height     = 0;                // [px] - SurfelAtlasHeight
};

// 📝 The whole surfel field's storage. Buffer roles, in the order the frame touches them:
//       LiveIndex / PendingIndex — the double-buffered live set. SurfelCounterReset copies Live into Pending and zeroes ValidSurfel; the census reads
//          Pending and rebuilds Live. ⚠️ One frame's Live is the next frame's Pending; the copy direction is load-bearing (plan §2 ①).
//       VacancyTable  — the free-slot stack, seeded to the identity. Spawn pops, recycle pushes.
//       CellSpan      — per cell: how many surfels it holds + where its run in CellList starts. Zeroed every frame by the census.
//       CellList      — the flat (surfel, intersected cell) index table. 🔴 Sized SurfelTotalLimit * 125, NOT cellCount * PerCellLimit; see
//          SurfelTypes.h. PerCellLimit only gates spawning and costs no memory here.
//       Reservation   — per cell, one uint the spawn uses to reserve a slot before committing. Zeroed by the offset scan.
//       ReferenceTally— per surfel, how many screen tiles referenced it; drives the sleep decision against SurfelReferenceThreshold.
//       HitLocator    — per surfel, the packed (instance, primitive, barycentric) surface anchor, so the census can re-derive position and normal from
//          live vertex data each frame instead of letting a surfel drift off a moving body. uvec4, matching upstream's packed TriangleHit.
//       RecycleRecord — per surfel life / frame / status bookkeeping.
//       RayOutcome    — the frame's traced-ray results, a shared POOL claimed atomically (SurfelRayBudget entries).
//       Counter       — the six shared counters. 🔴 Seeded, never zeroed (see the header note).
//       CounterReadback — HOST-VISIBLE staging the counters are copied into so the host can read the population without stalling. ⚠️ Read it a frame
//          LATE: upstream submits non-blocking and reads the previous frame's copy (plan §2 ⑤). Reading it the same frame means a device wait.
struct SurfelStore
{
    VulkanHost* Host = nullptr;                   // [-] - not owned; supplies device / physical device / allocator / queue

    SurfelBufferSlot SurfelRecords     = {};      // [-] - SurfelTotalLimit x Surfel (stride 100 B)
    SurfelBufferSlot HitLocator        = {};      // [-] - SurfelTotalLimit x uvec4 packed surface anchor
    SurfelBufferSlot LiveIndex         = {};      // [-] - SurfelTotalLimit x uint, this frame's live set
    SurfelBufferSlot PendingIndex      = {};      // [-] - SurfelTotalLimit x uint, last frame's live set awaiting the census
    SurfelBufferSlot VacancyTable      = {};      // [-] - SurfelTotalLimit x uint, seeded 0..limit-1
    SurfelBufferSlot CellSpan          = {};      // [-] - CellCount x SurfelCellSpan (8 B)
    SurfelBufferSlot CellList          = {};      // [-] - SurfelCellListCapacity x uint
    SurfelBufferSlot Reservation       = {};      // [-] - CellCount x uint
    SurfelBufferSlot ReferenceTally    = {};      // [-] - SurfelTotalLimit x uint
    SurfelBufferSlot RecycleRecords    = {};      // [-] - SurfelTotalLimit x SurfelRecycleRecord (12 B)
    SurfelBufferSlot RayOutcomes       = {};      // [-] - SurfelRayBudget x SurfelRayOutcome (48 B)
    SurfelBufferSlot Counter           = {};      // [-] - SurfelCounterSlotCount x uint, SEEDED from SurfelCounterSeed
    SurfelBufferSlot CounterReadback   = {};      // [-] - host-visible mirror of Counter, read one frame late

    SurfelAtlasSlot  IrradianceAtlas   = {};      // [-] - R32_SFLOAT, one 7x7 tile per surfel
    SurfelAtlasSlot  DepthAtlas        = {};      // [-] - R32G32_SFLOAT, the depth-moment tiles

    SurfelGridProportions Proportions  = {};      // [-] - the calibrated grid the buffers above were sized against
    uint32_t Capacity                  = 0;       // [-] - surfel capacity every per-surfel buffer is sized for
    uint32_t CellCount                 = 0;       // [-] - CellDimension³, the count CellSpan / Reservation were sized against
    bool     ReadyCondition            = false;   // [-] - true only once every allocation AND every seed/clear succeeded
};

// 📝 The six counters read back to the host, decoded from the staging buffer. Mirrors SurfelCounterOffset's order.
struct SurfelPopulation
{
    uint32_t LiveCount      = 0;   // [-] - surfels the census kept this frame (ValidSurfel)
    uint32_t PendingCount   = 0;   // [-] - surfels the census was handed (DirtySurfel)
    uint32_t VacancyCount   = 0;   // [-] - free slots remaining (FreeSurfel)
    uint32_t CellCursor     = 0;   // [-] - entries claimed in CellList (Cell)
    uint32_t RequestedRays  = 0;   // [-] - rays the trace was asked for (RequestedRay)
    uint32_t MissedRays     = 0;   // [-] - rays that left the scene (MissBounce)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Allocate every buffer and both atlases against Proportions, then write each one's correct initial bytes on a one-shot command buffer this function
// submits and WAITS on — so the store is fully resident and correctly seeded when it returns, with no caller-side synchronization. The atlases are
// also transitioned out of UNDEFINED here (to GENERAL) and cleared, because a layout the first pass does not itself transition is a validation error
// and an undefined-content sample.
//
// Two-phase, no-partial-store contract: Store is released to its empty value first and populated only if every step succeeds; any failure releases
// everything claimed so far, leaves ReadyCondition false with all handles null, and returns false. CommandPool must be created against
// Host.GraphicsQueueFamily. Pair with FinalizeSurfelStore.
bool InitializeSurfelStore(SurfelStore&                 Store,
                           VulkanHost&                  Host,
                           VkCommandPool                CommandPool,
                           const SurfelGridProportions& Proportions);

// Destroy every buffer, image, view and allocation, then reset Store to its empty value. Null-guarded on every branch, so it is safe on an
// already-empty or partially-built store and is idempotent. ⚠️ The device must be idle, or the caller must otherwise guarantee no frame in flight
// still references this memory.
void FinalizeSurfelStore(SurfelStore& Store);

// Record a copy of the counter buffer into its host-visible staging mirror. Records only — no submit, no fence, no wait: this rides the caller's frame
// command buffer, which is what keeps the population readback free.
// ⚠️ Pair it with RetrieveSurfelPopulation ONE FRAME LATER. Reading the mirror in the same frame this recorded means the copy has not executed and the
//    bytes are last frame's at best, uninitialized on frame zero (plan §2 ⑤).
void RecordSurfelCounterReadback(const SurfelStore& Store, VkCommandBuffer CommandBuffer);

// Decode the staging mirror into Result. Returns false (Result untouched) if the store is not ready or the mapping fails. Cheap — a map, a 24-byte
// read, an unmap — but it reports whatever the last EXECUTED RecordSurfelCounterReadback left there, so it is only meaningful a frame after that call.
bool RetrieveSurfelPopulation(const SurfelStore& Store, SurfelPopulation& Result);

// Total device bytes the store claimed across every buffer and both atlases — what the F10 window reports and what the budget decision is checked
// against. Excludes the host-visible readback mirror, which is not device-local.
VkDeviceSize ResolveSurfelStoreFootprint(const SurfelStore& Store);

} // namespace Frontier

#endif
