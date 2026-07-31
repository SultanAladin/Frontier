/*==============================================================================================================================================
                                                            SHADOWTILESTORE.H
==============================================================================================================================================*/
// 🧩 The sun shadows' VIRTUAL tile table: one 32-bit word per addressable tile (32x32 tiles x 6 LODs = 6144 words), living in a device-local SSBO
//    that S1/S2/S3 raise bits in with `atomicOr`. This is the TOP of §2's three-level indirection — this unit says which tiles are WANTED and which
//    hold WRONG depth, ShadowPageAtlas says where each wanted tile's depth lives, and the atlas image holds the depth itself.
//
// 🔴 TWO INDEPENDENT BITS, NOT ONE STATE. `Used` (S1, receiver-driven from the id buffer) answers "does this tile need to exist"; `Update` (S2,
//    caster-driven from moving bounds) answers "is the depth in it wrong". They come from different producers reading different data and neither
//    implies the other: an off-screen tile whose caster moved is Update-without-Used (correctly rendered NOTHING, because nothing samples it), and a
//    newly-scrolled-in tile is Used-without-Update only if its page already holds valid depth. A page rasterizes when `Used && Update` — the same
//    conjunction ShadowPageNeedsRender enforces on the physical side, and the reason a static scene costs zero dispatches.
//
// 🔴 THE WORD IS ATOMIC-OR-ONLY DURING MARKING, WHICH DICTATES THE ENCODING. Every S1/S2/S3 write is an `atomicOr` from thousands of uncoordinated
//    invocations, so a bit may only ever be RAISED during a marking pass — there is no read-modify-write that would let one invocation lower another's
//    bit. Clearing therefore cannot happen inside marking; it happens in the per-image reset (S1's predecessor) and in S4/S5. This is why residency
//    is NOT stored here as a mutable flag: it would need clearing mid-pass.
//
// ⚠️ THIS TABLE IS THE GPU MIRROR OF STATE THE CPU ALSO HOLDS, and the duplication is deliberate rather than sloppy. SunShadowClipmap's
//    ResidencyTable and ShadowPageAtlas's PageRecords are the CPU-side truth used by the S4/S5 mirrors and by every probe; this SSBO is what the
//    marking shaders can actually touch. They are reconciled once per image at a defined point (DownloadShadowTileStore), never read as if live.
//
// 📝 Pure storage + bit vocabulary. This unit does not dispatch the marking passes (that is RenderExtension's schedule), does not allocate pages
//    (ShadowPageAtlas), and does not rasterize depth (P6.4 / S7).

#pragma once
#ifndef FRONTIER_GRAPHICS_SHADOW_SHADOWTILESTORE_H
#define FRONTIER_GRAPHICS_SHADOW_SHADOWTILESTORE_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Shadow/SunShadowClipmap.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One tile's state is a single 32-bit word so `atomicOr` covers it in one instruction. The bit layout is mirrored verbatim in
//    Shaders/ShadowTileStore.glsl — 🔴 the two must be edited together, and ValidateShadowTileStoreLayout exists to fail loudly if they drift.
constexpr uint32_t ShadowTileUsedBit     = 1u << 0;   // [-] - this tile is wanted this image, from ANY source. Decides the tile must EXIST.
constexpr uint32_t ShadowTileDirectBit   = 1u << 1;   // [-] - a RECEIVER samples this tile itself (S1). 🔴 Propagation never raises this.
constexpr uint32_t ShadowTileUpdateBit   = 1u << 2;   // [-] - the depth inside is wrong/absent (S2). Decides the tile must be REDRAWN.
constexpr uint32_t ShadowTileCoarseBit   = 1u << 3;   // [-] - raised by propagation: a FINER level's demand reached this tile.
constexpr uint32_t ShadowTileMaskedBit   = 1u << 4;   // [-] - a finer level fully covers this tile, so its INHERITED demand is redundant (S3 masking).

// 🔴 DIRECT AND COARSE ARE NOT MUTUALLY EXCLUSIVE, and this is the whole reason Direct exists as its own bit. A distant surface can sample a coarse
//    tile itself (Direct) while a nearer surface's finer tiles propagate into that same tile (Coarse). Deciding "is this demand inherited?" from the
//    Coarse bit alone therefore masks a tile a receiver is actively reading — a hole in the shadow at exactly the distance where coarse LODs serve
//    the image. Masking must test `Coarse && !Direct`, never `Coarse` alone. (Caught by the probe; the first implementation had this defect.)

// 🔴 The reserved span is NOT padding — the low 8 bits are the only bits marking shaders may `atomicOr`. Bits 8..31 are written exclusively by the
//    allocator (S5) as a packed page index, and mixing the two would let a marking pass corrupt a page mapping. Enforced by ShadowTileFlagMask.
constexpr uint32_t ShadowTileFlagMask    = 0x000000FFu;   // [-] - marking-writable bits
constexpr uint32_t ShadowTilePageShift   = 8;             // [-] - packed page index begins here
constexpr uint32_t ShadowTilePageMask    = 0xFFFFFF00u;   // [-] - allocator-writable bits

// 📝 Total addressable tiles across every level. 32² x 6 = 6144 words = 24 KiB — small enough that the whole table is one SSBO and one clear.
constexpr uint32_t ShadowTileStoreCapacity = ShadowTilemapResolution * ShadowTilemapResolution * ShadowTilemapLodCount;

// The local workgroup edge for the receiver-marking pass (S1). 8x8 = 64 invocations over the id buffer; one invocation per screen pixel.
// ⚠️ Deliberately NOT the 32x32 the level passes use — S1 is dispatched over SCREEN extent, where 8x8 keeps the tail waste small on odd resolutions.
constexpr uint32_t ShadowMarkWorkgroupEdge = 8;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The per-image counters the marking chain produces, mirrored to a host-visible buffer alongside the table so a caller can see what marking asked
//    for before allocation trims it. ⚠️ Like ShadowPageCensus this is read ONE IMAGE STALE — reading it fresh needs a device stall.
struct ShadowTileTally
{
    uint32_t TileUsedCount     = 0;   // [tile] - wanted this image, from any source
    uint32_t TileDirectCount   = 0;   // [tile] - a receiver samples these itself
    uint32_t TileUpdateCount   = 0;   // [tile] - hold wrong depth
    uint32_t TileRenderCount   = 0;   // [tile] - Used AND Update, not masked: what S7 must actually redraw
    uint32_t TileCoarseCount   = 0;   // [tile] - wanted ONLY by propagation (Coarse without Direct) — the ceiling on what masking could ever save
    uint32_t TileMaskedCount   = 0;   // [tile] - demand dropped as redundant by masking
};

// 📝 Slots in the readback ring. 🔴 MUST be >= WindowSubstrate::FramesInFlight, for exactly the reason PickReadbackSlots documents: with fewer slots
//    than frames in flight, the CPU overwrites a slot whose copy is still outstanding. Same value as the pick ring, same argument.
constexpr uint32_t ShadowTileReadbackSlots = 3;

// 📝 The table. One SSBO the shaders bind, a host-visible UPLOAD buffer, a ring of host-visible READBACK buffers, and the CPU mirror the S1/S2/S3
//    mirrors operate on. Host is borrowed. ReadyCondition gates recording.
// 🔴 UPLOAD AND READBACK ARE SEPARATE ALLOCATIONS AND MUST STAY SO. They were one shared buffer, and the aliasing was a live bug: UploadShadowTileStore
//    memcpys the reset mirror into staging on the CPU immediately, with no fence, while the PREVIOUS frame's download copy into those same bytes is
//    still outstanding on the GPU. With FramesInFlight = 2 that raced every image and the tally alternated real counts / all-zero — which reads as "the
//    GPU marked nothing" rather than as a buffer-aliasing mistake. The ring adds 48 KiB on a 24 KiB table; the bug class is worth more than that.
// ⚠️ TileWords is the CPU mirror and is authoritative ONLY between an Upload and the next Download. Treating it as live while the GPU marks is the
//    one misuse this design cannot detect for you.
struct ShadowTileStore
{
    VulkanHost*           Host            = nullptr;             // [-] - not owned

    VkBuffer              TableBuffer     = VK_NULL_HANDLE;      // [-] - device-local SSBO the marking shaders atomicOr
    VkDeviceMemory        TableMemory     = VK_NULL_HANDLE;      // [-] - backing allocation
    VkBuffer              StagingBuffer   = VK_NULL_HANDLE;      // [-] - host-visible UPLOAD source; never a copy destination
    VkDeviceMemory        StagingMemory   = VK_NULL_HANDLE;      // [-] - backing allocation
    void*                 StagingMapping  = nullptr;            // [-] - persistently mapped; coherent memory, no explicit flush

    VkBuffer              ReadbackBuffer[ShadowTileReadbackSlots]  = {};   // [-] - host-visible download ring; never an upload source
    VkDeviceMemory        ReadbackMemory[ShadowTileReadbackSlots]  = {};   // [-] - backing allocations
    void*                 ReadbackMapping[ShadowTileReadbackSlots] = {};   // [-] - persistently mapped, coherent
    uint32_t              ReadbackCursor  = 0;                   // [-] - next slot Download records into; the slot Resolve reads is the OLDEST
    uint32_t              ReadbackFilled  = 0;                   // [-] - downloads recorded so far, capped at the ring size; gates the first reads

    std::vector<uint32_t> TileWords;                             // [-] - CPU mirror, ShadowTileStoreCapacity entries
    uint32_t              LevelCount      = 0;                   // [-] - LODs the table covers
    uint32_t              TileResolution   = 0;                  // [-] - tiles per level edge
    ShadowTileTally       Tally;                                 // [-] - previous image's counts
    bool                  ReadyCondition  = false;               // [-] - true once buffers + mirror are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Allocate both buffers and size the mirror for LevelCount levels of TileResolution² tiles. Every word starts zero (no demand, no page).
// Returns false (ReadyCondition stays false, handles null) on any failure. Host must be provisioned.
bool InitializeShadowTileStore(ShadowTileStore& Store,
                               VulkanHost&      Host,
                               uint32_t         LevelCount     = ShadowTilemapLodCount,
                               uint32_t         TileResolution = ShadowTilemapResolution);

// Flat index into TileWords for one tile of one level, via the clipmap's toroidal wrap so the CPU addresses the slot the GPU will.
// Returns UINT32_MAX when Level is out of range. 🔴 Same wrap as ResolveShadowTileSlot — a divergence here silently crosses tiles between the two
//    tables, which reads as shadows from the wrong region rather than as an addressing bug.
[[nodiscard]] uint32_t ResolveShadowTileWordIndex(const ShadowTileStore&  Store,
                                                 const SunShadowClipmap& Clipmap,
                                                 uint32_t                Level,
                                                 TileCoordinate          LightTile);

//------------------------------------------------------------------------------------------------------------------------
//                                            PER-IMAGE RESET  (the pass S1 depends on)
//------------------------------------------------------------------------------------------------------------------------

// 🔴 Clear the DEMAND bits (Used / Coarse / Masked) on every tile while PRESERVING Update and the packed page index. Call once per image before S1.
//    This asymmetry is the whole correctness argument of the reset: demand is a statement about THIS image and must not persist, but staleness is a
//    statement about the CONTENT of a page and must survive until a render clears it — the same lifetime mismatch that keeps ContentStale out of
//    ShadowPageOwnership. A reset that wiped Update would let a moved caster's tile read clean one image later with nothing redrawn.
void ResetShadowTileDemand(ShadowTileStore& Store);

//------------------------------------------------------------------------------------------------------------------------
//                                            S1 / S2 / S3  (CPU mirrors of the marking chain)
//------------------------------------------------------------------------------------------------------------------------

// S1 mirror: raise Used on the tile a receiver at WorldPosition samples, at one level. The GPU pass does this per screen pixel from the id buffer;
// this mirror exists so the schedule is testable without a device.
// 🔴 The producer must read the ID BUFFER, not depth. Where nothing rasterized, depth holds the clear value and reconstruction yields a far-plane
//    point that over-marks distant tiles; VisibilityImage's 0xFFFFFFFF sentinel is unambiguous and survives a reverse-Z switch.
void MarkShadowTileUsed(ShadowTileStore& Store, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile);

// S2 mirror: raise Update on the tile — its depth is wrong. 🔴 Call for PREVIOUS bounds as well as current: a caster's vacated tiles still hold its
// depth and would otherwise project a shadow it has walked out of, and a DELETED caster has no current bounds at all, so the previous-bounds call is
// the only one that fires. Not motion blur; cache invalidation.
void MarkShadowTileUpdate(ShadowTileStore& Store, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile);

// S3 mirror: propagate demand from the finest level upward, raising Used|Coarse on the covering tile at each coarser level.
// 🔴 DIRECTION IS FINE -> COARSE, and getting it backwards is silently plausible. A tile wanted at LOD 0 must also be backed at every coarser level,
//    because the tracer falls back to a coarser page when a fine one is missing (pool exhaustion, or the per-image render budget) — so the coarse
//    tile is the SAFETY NET and must exist. Propagating coarse->fine instead would mark 4x the tiles per level and exhaust the pool immediately.
// 📝 Verified as a separate bottom-up pass fanned out by atomicOr in current EEVEE, not an inline per-LOD loop.
void PropagateShadowTileDemand(ShadowTileStore& Store, const SunShadowClipmap& Clipmap);

// S3 masking mirror: lower redundant COARSE demand. A coarse tile whose whole footprint is already covered by directly-used finer tiles is wanted by
// nobody, so it takes Masked and stops requesting a page.
// 🔴 THIS ONLY EVER DROPS INHERITED DEMAND. A coarse tile carrying its OWN Used bit (a receiver genuinely samples at that LOD) is never masked, and
//    a coarse tile whose footprint is only PARTLY covered is never masked — masking a partially-covered tile would punch a hole exactly where the
//    fine level ran out of pages, which is the case the coarse fallback exists to serve.
// 📝 Upstream calls this Tile Masking: "untag the lower LOD tiles that are completely overlapped by higher LOD tiles."
uint32_t MaskRedundantShadowTiles(ShadowTileStore& Store, const SunShadowClipmap& Clipmap);

//------------------------------------------------------------------------------------------------------------------------
//                                                      QUERIES AND TRANSFER
//------------------------------------------------------------------------------------------------------------------------

// The raw word for a tile, or 0 when out of range. Prefer the predicates below over hand-testing bits at call sites.
[[nodiscard]] uint32_t ResolveShadowTileWord(const ShadowTileStore& Store, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile);

// True when this tile must be rasterized this image: wanted now AND holding wrong depth, and not masked away as redundant.
// 🔴 The single gate the S7 dispatch should test. Used alone means no caching at all; Update alone redraws tiles nothing samples.
[[nodiscard]] bool ShadowTileNeedsRender(const ShadowTileStore& Store, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile);

// Whether a tile carries demand that survived masking — i.e. the allocator should give it a page.
[[nodiscard]] bool ShadowTileRequestsPage(const ShadowTileStore& Store, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile);

// Clear the Update bit on one tile, asserting its depth has just been rasterized. S7 owns this call, exactly as it owns MarkShadowPageRendered —
// 🔴 a clear that does not correspond to a real render IS the silent-wrong-shadow bug.
void MarkShadowTileRendered(ShadowTileStore& Store, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile);

// Recount the tally from the mirror. Cheap; call after the image's marking settles.
void RefreshShadowTileTally(ShadowTileStore& Store);

// Push the CPU mirror to the device SSBO / pull the device SSBO back into the ring's next slot. Records into CommandBuffer outside any rendering scope.
// ⚠️ The download is visible to the CPU only after the submission carrying it has completed; read it stale, never in the same image.
void UploadShadowTileStore(ShadowTileStore& Store, VkCommandBuffer CommandBuffer);
void DownloadShadowTileStore(ShadowTileStore& Store, VkCommandBuffer CommandBuffer);

// Copy the OLDEST readback slot into TileWords. Separate from DownloadShadowTileStore because the copy is only valid once the GPU is done, and this unit
// deliberately does not own a fence — the ring's depth is what stands in for one.
// 🔴 CALL THIS BEFORE ResetShadowTileDemand + UploadShadowTileStore in the same image, not after. The reset clears the mirror this overwrites, so
//    resolving afterwards discards the GPU's marks and every count reads zero — indistinguishable from the GPU having marked nothing.
// 📝 A no-op until the ring has been filled once, so the initial zero-fill is never mistaken for a real "no demand" reading.
void ResolveShadowTileDownload(ShadowTileStore& Store);

// 🔴 Assert that Shaders/ShadowTileStore.glsl still declares the same bit values and capacity this header does. The bit layout is duplicated across
//    a C++ and a GLSL translation unit that no compiler cross-checks, so drift is invisible until shadows are subtly wrong in a way that looks like a
//    marking bug. Returns false and leaves a reason in OutReason on any mismatch. Probe-only; not called per image.
[[nodiscard]] bool ValidateShadowTileStoreLayout(const char* ShaderSourcePath, std::vector<const char*>& OutReasons);

// Destroy both buffers and reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeShadowTileStore(ShadowTileStore& Store);

} // namespace Frontier

#endif
