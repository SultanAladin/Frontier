/*==============================================================================================================================================
                                                       SURFELLIFECYCLESUBMISSION.H
==============================================================================================================================================*/
// 🧩 The per-frame surfel LIFECYCLE: it decides which surfels exist. Four stages, in order — Prepare (once), SpawnRequest, Allocate, Age — turn the
//    visibility buffer into spawn requests, hand out pool slots to fill them, and age / recycle the survivors. It is the half of Phase 1 that makes
//    surfels appear where the camera looks and disappear when they get old; the slotting (SurfelGridSlotting) then buckets whatever this leaves alive.
//
//    🔴 SPAWN RECONSTRUCTS FROM THE VISIBILITY BUFFER, NOT A G-BUFFER (PLAN §"Two facts" ①). webgiya's FindMissing sampled a depth + normal texture per
//       pixel; Frontier has no G-buffer, so SurfelSpawnRequest.comp unpacks the packed identity and re-derives world position + flat normal exactly as
//       SurfaceShade.frag does (shared through SurfelVisibilityReconstruct.glsl). This is the single largest translation decision in Phase 1. The spawn
//       set therefore mirrors SurfaceShade's set 0 bindings (b0 id image, b1/b2/b3 head mesh, b5/b6/b7 floor mesh) — those handles are BORROWED.
//
//    🔴 PHASE-1 REDUCTION. The reference lifecycle is entangled with Phase 2/3 machinery — MSME moment seeding, the SLG "brain transplant", radial-depth
//       tile clears, the `touched` keep-alive / kill-signal economy, per-tile irradiance. Every one of those reads or writes a buffer Phase 1 only
//       ZERO-FILLS (moments / guiding / surfelDepth / touched). They are omitted here and land with the integrate (Phase 2). What ships is the faithful
//       skeleton: seed (F21), reconstruct + coverage-gate spawn, pop-and-commit allocate, +1 age with TTL recycle, alive-count sync. See each .comp.
//
//    🔴 PREPARE RUNS ONCE (F21). It seeds every age to SURFEL_LIFE_RECYCLED and resets the stack atomics; SurfelPool deliberately leaves ages alone so
//       this pass owns them. RecordSurfelLifecyclePrepare must be recorded before the first slotting of the first frame, exactly once (Prepared latches).
//
//    📝 POD struct + free functions, mirroring InstanceCullSubmission. Two descriptor set layouts: the SPAWN layout (visibility set 0 + grid/pool/tile
//       set 1) drives SurfelSpawnRequest; the POOL layout (pool + tile buffers) drives Prepare / Age / Allocate. This unit OWNS the two per-tile request
//       buffers (TileAlloc, TileCandidate) and a point sampler for the id image; the pool, grid, and visibility resources are all BORROWED.

#pragma once
#ifndef FRONTIER_GRAPHICS_SURFEL_SURFELLIFECYCLESUBMISSION_H
#define FRONTIER_GRAPHICS_SURFEL_SURFELLIFECYCLESUBMISSION_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Surfel/SurfelPool.h"
#include "Graphics/Surfel/SurfelGridSlotting.h"

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Spawn tile geometry — MUST match local_size_x/y in SurfelSpawnRequest.comp. One workgroup owns an 8x8 pixel tile; one spawn request per tile.
constexpr uint32_t SurfelSpawnTileEdge = 8;   // [-] - 8x8 pixel tile (webgiya GROUP_SIZE_X/Y)

// The Prepare/Age workgroup edges — MUST match local_size_x in SurfelPrepare.comp (256) and SurfelAge.comp / SurfelAllocate.comp (64).
constexpr uint32_t SurfelPrepareWorkgroupEdge  = 256;
constexpr uint32_t SurfelLifecycleWorkgroupEdge = 64;

// The count of tiles that cover a WxH image (ceil over the tile edge on each axis). At least one on each axis.
inline uint32_t SurfelSpawnTilesAcross(uint32_t Extent)
{
    if (Extent == 0) return 1;
    return (Extent + SurfelSpawnTileEdge - 1) / SurfelSpawnTileEdge;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The spawn pass push block, byte-compatible with SurfelSpawnRequest.comp's PushBlock. InverseViewProjection + CameraPosition drive the same view-ray
//    reconstruction SurfaceShade uses; GridOrigin is the SNAPPED grid origin (cell indices); ScreenAndTiles packs (width, height, tiles-x, frame). The
//    three Floor* fields mirror SurfaceShadeConstants so the floor identity range is rebased identically.
struct SurfelSpawnConstants
{
    float    InverseViewProjection[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };  // [-] - column-major clip -> world
    float    CameraPosition[4]         = { 0, 0, 0, 0 };   // [m] - raw eye (radius + ray origin); w unused
    float    GridOrigin[4]             = { 0, 0, 0, 0 };   // [m] - snapped grid origin (cell indices); w unused
    int32_t  ScreenAndTiles[4]         = { 0, 0, 0, 0 };   // [-] - x width, y height, z tiles-x, w frame index
    uint32_t FloorPartitionBase        = 0;                // [-] - partition ordinals >= this belong to the floor
    uint32_t FloorShadeEnabled         = 0;                // [-] - 1 when the floor buffers are real
    uint32_t FloorIndexBase            = 0;                // [-] - first index of the floor run in the merged index buffer
    float    SpawnDensityScale         = 1.0f;             // [-] - live spawn-rate multiplier (numpad +/-); 1.0 == the ported baseline throttle
    float    TuneCellDiameter          = 1.0f;             // [m] - live base cell edge (F10 window); seeds from SURFEL_GRID_CELL_DIAMETER
    float    TuneBaseRadius            = 1.2f;             // [m] - live cascade-0 disc radius (F10 window); seeds from SURFEL_BASE_RADIUS
    float    TuneNearFieldBias         = 1.0f;             // [-] - live near-field spawn lift (F10 window); 1.0 == inert baseline
    int32_t  PerCellCap                = SurfelMaxPerCell; // [-] - live per-cell fill ceiling (F10 window Apply); default == the baked max cap
};

// 📝 The lifecycle unit's owned resources. TWO set layouts + their pipelines: the spawn layout binds the visibility set (0) and the grid/pool/tile set
//    (1); the pool layout binds pool + tile buffers for Prepare / Age / Allocate. Owns the two per-tile request buffers (sized for the largest tile grid
//    seen) and the point sampler. Bound* cache the borrowed handles so a descriptor is re-pointed only on change. Prepared latches the one-time seed.
struct SurfelLifecycleSubmission
{
    VulkanHost* Host = nullptr;                              // [-] - not owned

    // --- spawn: visibility set 0 (id image + 6 mesh SSBOs) + grid/pool/tile set 1 ---
    VkDescriptorSetLayout SpawnVisibilityLayout = VK_NULL_HANDLE;   // [-] - set 0: b0 id, b1/b2/b3 head, b5/b6/b7 floor
    VkDescriptorSetLayout SpawnGridLayout       = VK_NULL_HANDLE;   // [-] - set 1: offsets, list, surfels, tileAlloc, tileCandidate, touched (b5, economy)
    VkPipelineLayout      SpawnPipelineLayout   = VK_NULL_HANDLE;   // [-] - both sets + SurfelSpawnConstants push range
    VkPipeline            SpawnPipeline         = VK_NULL_HANDLE;   // [-] - SurfelSpawnRequest.comp
    VkDescriptorSet       SpawnVisibilitySet    = VK_NULL_HANDLE;   // [-] - set 0 (re-pointed on visibility resize)
    VkDescriptorSet       SpawnGridSet          = VK_NULL_HANDLE;   // [-] - set 1
    VkSampler             PointSampler          = VK_NULL_HANDLE;   // [-] - nearest/clamp; a filtered id is a wrong id

    // --- Prepare / Age / Allocate: pool + tile buffers ---
    VkDescriptorSetLayout PoolLayout          = VK_NULL_HANDLE;     // [-] - surfels, pool, poolAlloc, poolMax, alive, tileAlloc, tileCandidate
    VkPipelineLayout      PoolPipelineLayout  = VK_NULL_HANDLE;     // [-] - PoolLayout + push range
    VkPipeline            PreparePipeline     = VK_NULL_HANDLE;     // [-] - SurfelPrepare.comp
    VkPipeline            AgePipeline         = VK_NULL_HANDLE;     // [-] - SurfelAge.comp
    VkPipeline            AllocatePipeline    = VK_NULL_HANDLE;     // [-] - SurfelAllocate.comp
    VkDescriptorSet       PoolSet             = VK_NULL_HANDLE;     // [-] - the bound pool/tile set

    VkDescriptorPool      DescriptorPool = VK_NULL_HANDLE;         // [-] - sized for the three sets

    // --- owned per-tile request buffers ---
    VkBuffer       TileAllocBuffer      = VK_NULL_HANDLE;   // [-] - 1 int per tile: spawn requested this frame
    VkDeviceMemory TileAllocMemory      = VK_NULL_HANDLE;
    VkBuffer       TileCandidateBuffer  = VK_NULL_HANDLE;   // [-] - 2 vec4 per tile: [0] posb, [1] normal
    VkDeviceMemory TileCandidateMemory  = VK_NULL_HANDLE;
    uint32_t       TileCapacity         = 0;                // [-] - tiles the two buffers are sized for

    // --- cached borrowed handles ---
    VkImageView BoundIdView             = VK_NULL_HANDLE;
    VkBuffer    BoundVertexBuffer       = VK_NULL_HANDLE;
    VkBuffer    BoundIndexBuffer        = VK_NULL_HANDLE;
    VkBuffer    BoundInstanceBuffer     = VK_NULL_HANDLE;
    VkBuffer    BoundFloorVertexBuffer  = VK_NULL_HANDLE;
    VkBuffer    BoundFloorIndexBuffer   = VK_NULL_HANDLE;
    VkBuffer    BoundFloorInstanceBuffer = VK_NULL_HANDLE;
    VkBuffer    BoundOffsetsBuffer      = VK_NULL_HANDLE;
    VkBuffer    BoundListBuffer         = VK_NULL_HANDLE;
    VkBuffer    BoundSpawnSurfelBuffer  = VK_NULL_HANDLE;
    VkBuffer    BoundSpawnTouchedBuffer = VK_NULL_HANDLE;   // set 1 b5 (economy)
    VkBuffer    BoundPoolSurfelBuffer   = VK_NULL_HANDLE;
    VkBuffer    BoundPoolOffsetsBuffer  = VK_NULL_HANDLE;   // pool set b7 (rent count)
    VkBuffer    BoundPoolTouchedBuffer  = VK_NULL_HANDLE;   // pool set b8 (income drain)

    bool Prepared       = false;   // [-] - the one-time seed has been recorded
    bool ReadyCondition = false;   // [-] - true once every layout / pipeline / buffer is live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the two set layouts, the two pipeline layouts, the four pipelines (Prepare/Spawn/Age/Allocate), the descriptor pool + three sets, the point
// sampler, and the two per-tile request buffers sized for MaxTileCount tiles. ShaderDirectory locates the four Surfel*.comp.spv. Returns false
// (ReadyCondition stays false, handles null) on any failure. Pair with FinalizeSurfelLifecycleSubmission.
bool InitializeSurfelLifecycleSubmission(SurfelLifecycleSubmission& Lifecycle,
                                         VulkanHost&                Host,
                                         uint32_t                   MaxTileCount,
                                         const char*                ShaderDirectory);

// Point the spawn set 0 at the borrowed visibility image view + the six mesh/instance SSBOs (mirroring RefreshSurfaceShadeInscription). Pass
// VK_NULL_HANDLE for the three Floor* when no floor loaded — they are aliased onto the head buffers so no descriptor is left undefined, and the caller
// must leave FloorShadeEnabled at 0. Point set 1's surfel binding at the pool. The device must be idle. Cheap: writes only on a handle change.
void RefreshSurfelLifecycleVisibility(SurfelLifecycleSubmission& Lifecycle,
                                      VkImageView                IdView,
                                      const SurfelPool&          Pool,
                                      const SurfelGridSlotting&  Slotting,
                                      VkBuffer                   VertexBuffer,
                                      VkBuffer                   IndexBuffer,
                                      VkBuffer                   InstanceBuffer,
                                      VkBuffer                   FloorVertexBuffer   = VK_NULL_HANDLE,
                                      VkBuffer                   FloorIndexBuffer    = VK_NULL_HANDLE,
                                      VkBuffer                   FloorInstanceBuffer = VK_NULL_HANDLE);

// Record the one-time Prepare seed (F21): ages -> SURFEL_LIFE_RECYCLED, free-list -> identity, stack atomics -> 0. A no-op after the first call
// (Prepared latches) or when not ready. Must be recorded before the first slotting. CommandBuffer must be recording, OUTSIDE any rendering scope.
void RecordSurfelLifecyclePrepare(SurfelLifecycleSubmission& Lifecycle,
                                  const SurfelPool&           Pool,
                                  VkCommandBuffer             CommandBuffer);

// Record the spawn stage: SurfelSpawnRequest over the visibility tiles (writes the per-tile requests), a barrier, then SurfelAllocate over the tiles
// (pops pool slots, commits surfels) and its single-lane alive sync. Reads the grid Offsets/List for the coverage gate, so it must run AFTER the
// frame's slotting. Constants supplies the camera + snapped origin + screen/tile dims + floor rebase. A no-op when not ready. Must be OUTSIDE any
// rendering scope. Extent is the visibility image size (drives the tile dispatch).
void RecordSurfelLifecycleSpawn(SurfelLifecycleSubmission&   Lifecycle,
                                const SurfelPool&             Pool,
                                const SurfelSpawnConstants&   Constants,
                                VkExtent2D                    Extent,
                                VkCommandBuffer               CommandBuffer);

// Record the Age stage: the full surfel economy — police execution (touched == kill), crowding rent (over-subscribed cells age faster, read from the
// grid Offsets slice), keep-alive income (touched 5..50 cancels metabolism), then +1 metabolism with TTL recycle to the free-list. Reads the grid
// Offsets + the touched mailbox (pointed at Refresh) and needs the frame's SNAPPED grid origin (the same one slotting/spawn used) to hash each surfel's
// cell. Independent of the spawn set (pool layout only). Run once per frame, after spawn. A no-op when not ready. Must be OUTSIDE any rendering scope.
void RecordSurfelLifecycleAge(SurfelLifecycleSubmission& Lifecycle,
                              const SurfelPool&           Pool,
                              const float                 GridOrigin[3],
                              VkCommandBuffer             CommandBuffer);

// Destroy every layout / pipeline / descriptor / buffer / sampler and reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeSurfelLifecycleSubmission(SurfelLifecycleSubmission& Lifecycle);

} // namespace Frontier

#endif
