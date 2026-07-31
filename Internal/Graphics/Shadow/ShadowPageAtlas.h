/*==============================================================================================================================================
                                                            SHADOWPAGEATLAS.H
==============================================================================================================================================*/
// 🧩 The sun shadows' physical page pool: one R32_UINT storage image carved into 128x128-texel pages, plus the three-state (free / cached / used)
//    bookkeeping that hands pages to the tiles that want them. This is the middle and bottom of §2's three-level indirection — SunShadowClipmap
//    says WHICH tiles are wanted, this unit says WHERE each one's depth lives, and the atlas itself holds the depth. Pages survive across images,
//    which is the whole reason a scrolling camera is cheap: a tile that stayed inside the window keeps the page it already had.
//
// 🔴 THE POOL IS OVERSUBSCRIBED, BY DESIGN AND BY NECESSITY. 32x32 tiles x 6 LODs = 6144 addressable tiles, but a page per tile at 128² R32_UINT
//    would be 384 MiB of atlas for a scene that measurably wants ~759 pages — and it needs an 8192²+ image the guaranteed device floor does not
//    promise. 1024 physical pages (a 4096² atlas, 64 MiB) is a 6:1 oversubscription against ADDRESSABLE tiles but sits ABOVE measured demand, so the
//    cached tier now absorbs scroll churn rather than standing in for capacity the pool never had.
// ⚠️ It was 256 pages until 2026-07-30, a 24:1 ratio, and that was NOT survivable: S1 marks one tile per covered screen pixel, so a 100 m floor that
//    out-spans every level's window drove ~759 requests per image into 256 pages. Every image evicted, pages were recycled between S7's draw and the
//    tracer's read, and the trace found the clear identity everywhere — a fully-lit scene with a healthy-looking marking chain in front of it.
//    🔴 Read "oversubscribed by design" as a statement about ADDRESSABILITY, never as a licence to size the pool below measured demand.
//
// 🔴 A page either exists or it does not — there is no partial page. So when allocation outruns capacity the shadow vanishes in a whole 128x128
//    BLOCK rather than degrading softly, which is why WHERE the loss lands is the entire design question. Under pressure the COARSEST LOD
//    surrenders first (LOD 5 = 512 m, then 4, then 3...), so detail is lost at distance where a missing shadow reads as haze, while LOD 0's
//    near-field contact shadows are reclaimed last. Strict LOD-agnostic LRU was rejected: it can evict a near page mid-pan and punch a visible
//    hole directly in front of the viewer. (User ruling, 2026-07-30; the plan left this policy undefined — see §2 / the Q3 gap.)
//
// ⚠️ A `used` page cannot be reclaimed by ANY policy — it holds depth wanted THIS image. The LOD priority only decides who loses first among
//    CACHED pages, so it does not prevent true exhaustion; that is what the PageCensus readback is for (§4 C6, read one image stale).
//
// 📝 Pure pool mechanics. This unit does not rasterize depth (P6.4 / S7) and does not mark tiles (P6.3b / S1-S3); it owns the atlas image, the
//    free list, the residency-to-page mapping, and the census.

#pragma once
#ifndef FRONTIER_GRAPHICS_SHADOW_SHADOWPAGEATLAS_H
#define FRONTIER_GRAPHICS_SHADOW_SHADOWPAGEATLAS_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Shadow/ShadowTileStore.h"
#include "Graphics/Shadow/SunShadowClipmap.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 R32_UINT because the depth raster resolves casters with imageAtomicMin, which needs an integer format; core VK 1.0, no extension.
constexpr VkFormat ShadowPageAtlasFormat = VK_FORMAT_R32_UINT;

// The atomic-min identity. A texel holding this has had nothing rasterized into it, and is unambiguously "no caster" — distinct from any real
// depth. ⚠️ Also the value ShadowPageClear writes, so the two must never disagree.
constexpr uint32_t ShadowPageClearIdentity = 0xFFFFFFFFu;

// 📝 The atlas is a square grid of pages: 32 x 32 pages of 128² texels = 4096² texels = 64 MiB at 4 bytes/texel.
//    It leaves the 4 GB target room for the GI probe field in P7b.
// 🔴 THE PAGE COUNT IS SIZED AGAINST MEASURED DEMAND, NOT PICKED. S1 is receiver-driven — it marks one tile per COVERED SCREEN PIXEL — so demand
//    tracks the receiver's screen coverage and its spread across LODs, NOT the caster count. On the two-Suzanne + 100 m floor scene the floor
//    out-spans every level's 32-tile window, so each of the 6 levels marks most of its 1024-tile window and S5 requests ~759 pages per image. The
//    previous 16² = 256-page pool served 256 of those and evicted for the rest EVERY image, so pages were recycled between S7's draw and the tracer's
//    read and the trace found the clear identity everywhere — a fully-lit scene with a healthy-looking marking chain in front of it. 1024 pages holds
//    that working set with headroom.
// ⚠️ 4096² IS THE VULKAN GUARANTEED FLOOR for maxImageDimension2D, and ShadowPageAtlasEdge is a single image extent (ShadowPageAtlas.cpp's
//    vkCreateImage). This shape sits exactly ON the floor, which is why the page count was raised by splitting the SAME edge into more, smaller pages
//    rather than by growing the edge: 32 x 256 would need an 8192² image and a maxImageDimension2D query with a fallback that does not exist here.
//    🔴 Do not raise ShadowPageAtlasPageEdge without either lowering ShadowPageResolution to match or adding that device-limit query.
constexpr uint32_t ShadowPageAtlasPageEdge = 32;                                                      // [page]  - pages per atlas edge
constexpr uint32_t ShadowPageCapacity      = ShadowPageAtlasPageEdge * ShadowPageAtlasPageEdge;        // [page]  - 1024 physical pages
constexpr uint32_t ShadowPageAtlasEdge     = ShadowPageAtlasPageEdge * ShadowPageResolution;           // [texel] - 4096

// 🔴 The two invariants a page-geometry edit can violate SILENTLY. Neither produces a build error on its own: the first hands the tracer an image
//    extent the device may refuse (and vkCreateImage failure degrades to ReadyCondition = false, i.e. shadows simply absent), and the second lets a
//    page index overflow into the marking bits of a tile word, corrupting demand flags rather than addressing. ⚠️ SunShadowTrace.glsl mirrors both
//    constants as ShadowTracePageResolution / ShadowTraceAtlasPageEdge and NO compiler checks that copy — edit the two files together.
static_assert(ShadowPageAtlasEdge <= 4096,
              "ShadowPageAtlasEdge exceeds Vulkan's guaranteed maxImageDimension2D floor of 4096; the atlas is one image and this build has no "
              "device-limit query to fall back on. Lower ShadowPageResolution or ShadowPageAtlasPageEdge, or add the query.");
static_assert(ShadowPageCapacity <= (0xFFFFFFFFu >> 8),
              "ShadowPageCapacity no longer fits the 24 bits ShadowTilePageShift leaves above the marking flags in a tile word.");

// The compute local workgroup edge for the pool passes. ⚠️ 32 x 32 = 1024 invocations is EXACTLY Pascal's maxComputeWorkGroupInvocations, chosen
// so one workgroup covers a whole 32x32 tilemap level in a single pass. Zero headroom: never add a Z to the LOCAL size (Z lives in the dispatch).
constexpr uint32_t ShadowPoolWorkgroupEdge = 32;

// 📝 The sentinel for "this tile has no page". Not 0 — page 0 is a perfectly valid physical page, and a zero sentinel would alias onto it, so
//    every unmapped tile would silently read the depth of the atlas's first page.
constexpr uint32_t ShadowPageUnmapped = 0xFFFFFFFFu;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One page's ownership state. Three states, not two — a cached page holds depth from a previous image that nothing wants right now but that
//    would be expensive to re-rasterize, so it is reclaimed only under pressure.
// 🔴 Ownership answers "SHOULD THIS PAGE EXIST", and that is ALL it answers. Whether the depth inside it is still CORRECT is a separate question
//    carried by ShadowPageRecord::ContentStale. Collapsing the two into a fourth Ownership state was tried and is wrong: the states have different
//    lifetimes (Ownership is recomputed every image, staleness persists until a render clears it) and a page can be any combination of the two.
enum class ShadowPageOwnership : uint32_t
{
    Free   = 0,   // in the free list, holds nothing
    Cached = 1,   // holds depth from an earlier image; reclaimable under pressure. Says NOTHING about whether that depth is still correct.
    Used   = 2,   // wanted THIS image; NOT reclaimable by any policy
};

// 📝 What one physical page currently is. OwnerLevel / OwnerTile / OwnerSlot are meaningful only when Ownership != Free.
// 🔴 OwnerSlot is STORED, not recomputed from OwnerTile at reclaim time. The slot a tile occupies depends on the clipmap's toroidal origin, which
//    SCROLLS — so by the time a page is evicted, re-deriving its slot from the current origin can name a different slot than the one it was filed
//    under. Detach would then clear an innocent slot and leave the real mapping dangling: two tiles pointing at one page, one of them reading
//    another region's depth. Caught by the churn probe, which is the only test that scrolls between allocation and eviction.
// 🔴 ContentStale IS NOT A FOURTH OWNERSHIP STATE, and the reason is a LIFETIME MISMATCH that makes merging them incorrect rather than merely
//    inelegant. Ownership is recomputed every image (OpenShadowPageImage demotes every Used page to Cached), but staleness must SURVIVE image
//    boundaries — it is cleared only by a render that actually re-rasterizes the page. A merged state would be wiped by the per-image demotion, so
//    a page tagged stale in image N would silently read "clean" in image N+1 with nothing having redrawn it: exactly the bug the flag exists to
//    prevent, reintroduced by the encoding. Render a page only when it is BOTH wanted and stale — `Ownership == Used && ContentStale`.
//
// 🔴 WITHOUT THIS FLAG THE CACHE IS SILENTLY WRONG, which is why it is a prerequisite for S2 and not a later refinement. RequestShadowPage's
//    cache-hit path returns a tile's existing page unconditionally, so a caster that MOVES leaves its page Cached, takes the hit, and serves depth
//    from the object's OLD position forever — a shadow frozen in place while the object walks away from it. The only tools available before this
//    flag were "re-render everything" (throws away the cache the pool is built around) or "trust the cache" (permanently wrong image).
struct ShadowPageRecord
{
    ShadowPageOwnership Ownership   = ShadowPageOwnership::Free;
    uint32_t            OwnerLevel  = 0;                          // [-]    - LOD that owns this page
    TileCoordinate      OwnerTile;                                // [tile] - light-space tile that owns it
    uint32_t            OwnerSlot   = UINT32_MAX;                 // [-]    - the mapping slot it is filed under; authoritative over OwnerTile
    uint64_t            LastUsedImage = 0;                        // [-]    - image ordinal this page was last wanted; ties broken oldest-first
    bool                ContentStale  = true;                     // [-]    - depth inside is wrong/absent; survives images until a render clears it
};

// 📝 The census, mirrored to a host-visible buffer so the CPU can warn on over-subscription. ⚠️ Read ONE IMAGE STALE (§4 C6) — reading it fresh
//    would need a device stall, which costs more than the warning is worth.
struct ShadowPageCensus
{
    uint32_t PageUsedCount     = 0;   // [page] - wanted this image
    uint32_t PageCachedCount   = 0;   // [page] - holding reusable depth
    uint32_t PageFreeCount     = 0;   // [page] - available
    uint32_t PageRequestCount  = 0;   // [page] - tiles that ASKED for a page this image
    uint32_t PageStarvedCount  = 0;   // [page] - asked and did NOT get one — the exhaustion signal
    uint32_t PageEvictedCount  = 0;   // [page] - cached pages reclaimed under pressure this image
    uint32_t PageStaleCount    = 0;   // [page] - held but holding wrong/absent depth
    uint32_t PageRenderCount   = 0;   // [page] - wanted AND stale: what the depth raster must actually redraw this image
};

// 📝 The pool. Atlas image + storage view, the per-page ownership records, the per-tile page mapping (one entry per addressable tile across every
//    level, so LevelCount * Resolution² entries), and the census. Host is borrowed. ReadyCondition gates recording.
struct ShadowPageAtlas
{
    VulkanHost*                   Host           = nullptr;                  // [-] - not owned

    VkImage                       AtlasImage     = VK_NULL_HANDLE;           // [-] - R32_UINT 4096² (32² pages of 128²), STORAGE | SAMPLED, device-local
    VkDeviceMemory                AtlasMemory    = VK_NULL_HANDLE;           // [-] - backing allocation
    VkImageView                   AtlasStorageView = VK_NULL_HANDLE;         // [-] - compute / fragment write target
    VkImageView                   AtlasSampledView = VK_NULL_HANDLE;         // [-] - the tracer's read view
    VkImageLayout                 CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED; // [-] - tracked across the whole image

    std::vector<ShadowPageRecord> PageRecords;                               // [-] - ShadowPageCapacity entries
    std::vector<uint32_t>         FreeList;                                  // [-] - indices of Free pages; back() is popped first
    std::vector<uint32_t>         TilePageMapping;                           // [-] - per tile -> page index, or ShadowPageUnmapped

    // 📝 The GPU mirror of TilePageMapping: S6 and S7 cannot read the CPU vector, so it is copied per image. Upload-only — nothing on the device
    //    writes it back while S5 stays a CPU pass, so there is no readback ring here and no aliasing hazard of the kind ShadowTileStore documents.
    VkBuffer                      MappingBuffer   = VK_NULL_HANDLE;          // [-] - device-local SSBO; one uint per addressable tile
    VkDeviceMemory                MappingMemory   = VK_NULL_HANDLE;          // [-] - backing allocation
    VkBuffer                      MappingStaging  = VK_NULL_HANDLE;          // [-] - host-visible upload source
    VkDeviceMemory                MappingStagingMemory = VK_NULL_HANDLE;     // [-] - backing allocation
    void*                         MappingStagingMapping = nullptr;           // [-] - persistently mapped; coherent, no explicit flush

    // 📝 ONE WORD PER PHYSICAL PAGE RECORDING WHETHER S7 ACTUALLY RASTERIZED INTO IT — 0 means no caster fragment landed there since the page was last
    //    primed, non-zero means at least one did. Written ONLY by ShadowDepthRaster.frag's atomicOr, reset by ClearShadowPageCoverage for exactly the
    //    pages S6 clears, and read ONLY by the tracer. Device-local with no staging: nothing on the host ever needs its contents.
    //
    // 🔴 THIS REPLACES A CONTENT PROBE THAT COULD NOT BE MADE CORRECT, and the distinction is fact versus heuristic. A page is allocated from RECEIVER
    //    demand (S1 marks per screen pixel) but filled from CASTER coverage (S7 rasterizes the caster mesh), so a tile a receiver looks at but no caster
    //    projects onto keeps the clear identity — and a tracer reporting it resident answers "nothing occludes me" from a page holding no information,
    //    masking the coarser level that DOES have depth. The previous defence sampled five texels and called the page empty if all five held the
    //    identity, which is unsound in the common direction: a caster covering only part of a tile (every silhouette edge, every small object) leaves
    //    those five texels untouched while the page is genuinely drawn. The tap then reported a miss, the walk fell outward through every level, and the
    //    receiver came out at the initial visibility of 1.0 — PAGE-SIZED WHITE BLOCKS punched through the shadow, clustered where partial coverage is
    //    common and drifting as the window scrolls tile boundaries across caster silhouettes. No count in the chain can see it: the page is allocated,
    //    cleared, drawn and marked rendered, all correctly.
    // ⚠️ A COVERAGE BIT CANNOT HAVE THAT FAILURE MODE because it is written by the same fragment that writes the depth — one caster fragment anywhere in
    //    the page sets it, so "drawn" is recorded rather than inferred. It also removes four texelFetches per level per shaded pixel.
    // 🔴 RESET WITH THE DEPTH IT DESCRIBES, NEVER PER IMAGE, and choosing wrong here inverts the bug rather than fixing it. This word has the ATLAS
    //    IMAGE's lifetime, not the frame's: a page drawn in image N and merely cached in N+1 still holds that depth, and S7 deliberately skips it (the
    //    census reports nothing to render — that early-out IS the cache). A per-image fill would therefore zero a bit that nothing is left to re-raise,
    //    every cached page would read as never-drawn, the walk would exhaust all six levels, and the scene would come out FULLY LIT. Resetting exactly
    //    the ShadowPageNeedsRender set keeps coverage and depth in step by construction — see ClearShadowPageCoverage.
    VkBuffer                      CoverageBuffer  = VK_NULL_HANDLE;          // [-] - device-local SSBO; one uint per physical page
    VkDeviceMemory                CoverageMemory  = VK_NULL_HANDLE;          // [-] - backing allocation

    // 📝 ONE WORD PER PHYSICAL PAGE CARRYING ShadowPageNeedsRender TO THE DEVICE — 1 means S6 primed this page to the identity this image and S7 must
    //    rasterize into it, 0 means the page holds valid cached depth that S7 must leave alone. Uploaded once per image by UploadShadowPageRenderMask
    //    and read (never written) by ShadowDepthRaster.frag.
    //
    // 🔴 S7 DRAWS WHOLE TILE WINDOWS, SO WITHOUT THIS MASK ITS WRITE SET IS EVERY MAPPED PAGE WHILE S6's CLEAR SET IS ONLY THE STALE ONES, and the
    //    asymmetry is silent in every count in the chain. Measured: 1024 pages written against 12 primed, so 1012 cached pages take this image's
    //    imageAtomicMin on top of depth they already held. Because min is monotonic and never releases, a caster's OLD depth survives in those pages
    //    forever — its shadow stays where the object no longer stands while a correct shadow also appears, and the affected set is the contiguous
    //    allocated region of the atlas, which reads back through the mapping as a neatly packed grid of caster silhouettes in empty ground.
    // 🔴 THE MASK IS THE COMPLEMENT OF THE CACHE, SO IT MUST BE REBUILT FROM ShadowPageNeedsRender EVERY IMAGE, never accumulated. A stale mask that
    //    still names last image's stale pages re-authorizes writes into pages that have since been marked clean, which reintroduces the same blend.
    // ⚠️ THIS IS NOT THE COVERAGE BUFFER AND THE TWO CANNOT BE MERGED, despite both being one word per page. Coverage is RAISED BY S7 DURING the pass,
    //    so gating on it would let the first caster fragment in a page silence every later one — every page would keep exactly one caster's depth.
    //    This mask is fixed for the whole pass by construction, which is what makes it a sound gate.
    VkBuffer                      RenderMaskBuffer        = VK_NULL_HANDLE;  // [-] - device-local SSBO; one uint per physical page
    VkDeviceMemory                RenderMaskMemory        = VK_NULL_HANDLE;  // [-] - backing allocation
    VkBuffer                      RenderMaskStaging       = VK_NULL_HANDLE;  // [-] - host-visible upload source
    VkDeviceMemory                RenderMaskStagingMemory = VK_NULL_HANDLE;  // [-] - backing allocation
    void*                         RenderMaskStagingMapping = nullptr;        // [-] - persistently mapped; coherent, no explicit flush

    uint32_t                      LevelCount     = 0;                        // [-] - LODs the mapping covers
    uint32_t                      TileResolution = 0;                        // [-] - tiles per level edge
    uint64_t                      ImageOrdinal   = 0;                        // [-] - advanced once per image; drives LastUsedImage
    ShadowPageCensus              Census;                                    // [-] - previous image's counts
    bool                          ReadyCondition = false;                    // [-] - true once image + view + tables are live
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Allocate the atlas image + views and size the bookkeeping for LevelCount levels of TileResolution² tiles. Every page starts Free and every tile
// starts unmapped. Returns false (ReadyCondition stays false, handles null) on any failure. Host must be provisioned.
bool InitializeShadowPageAtlas(ShadowPageAtlas& Atlas,
                              VulkanHost&       Host,
                              uint32_t          LevelCount     = ShadowTilemapLodCount,
                              uint32_t          TileResolution = ShadowTilemapResolution);

// Flat index into TilePageMapping for one tile of one level. Uses the clipmap's toroidal wrap, so a scrolled window addresses the same slot the
// GPU will. Returns UINT32_MAX when Level is out of range.
[[nodiscard]] uint32_t ResolveShadowTileSlot(const ShadowPageAtlas&  Atlas,
                                            const SunShadowClipmap& Clipmap,
                                            uint32_t                Level,
                                            TileCoordinate          LightTile);

// The physical page currently backing a tile, or ShadowPageUnmapped. A tile with no page must not be sampled — its depth does not exist.
[[nodiscard]] uint32_t ResolveShadowTilePage(const ShadowPageAtlas&  Atlas,
                                            const SunShadowClipmap& Clipmap,
                                            uint32_t                Level,
                                            TileCoordinate          LightTile);

// The atlas texel offset of a physical page's top-left corner. Pages are laid out row-major in a ShadowPageAtlasPageEdge² grid.
void ResolveShadowPageOrigin(uint32_t PageIndex, uint32_t& OutTexelX, uint32_t& OutTexelY);

// S4 (CPU mirror): demote every Used page to Cached and open a new image. Call once per image BEFORE requesting pages, so "used" means
// "wanted this image" rather than "wanted at some point".
void OpenShadowPageImage(ShadowPageAtlas& Atlas);

// S5 (CPU mirror): claim a page for a tile, marking it Used. Returns the page index, or ShadowPageUnmapped when the pool is exhausted.
// 🔴 Reuses the tile's existing page when it already has one (that is the cache hit that makes scrolling cheap), takes from the free list next,
//    and only then evicts — COARSEST LOD FIRST, oldest-first within a level. Never evicts a page that is Used this image, and never evicts to
//    serve a level coarser than the victim's own (which would let LOD 5 thrash LOD 0's pages back and forth).
uint32_t RequestShadowPage(ShadowPageAtlas& Atlas, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile);

// Break a tile's page mapping and return the page to the free list. A no-op when the tile has none.
void ReleaseShadowTilePage(ShadowPageAtlas& Atlas, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile);

// Drop every page a level owns (the C4 whole-window path — a rotated sun makes every cached page describe the wrong region).
void InvalidateShadowLevelPages(ShadowPageAtlas& Atlas, uint32_t Level);

// Drop the pages a SCROLL just invalidated: every tile in the exposed strips, or the level's whole set when the light rotated. Returns the number of
// pages released.
//
// 🔴 THIS IS THE BRIDGE BETWEEN THE CLIPMAP'S RESIDENCY AND THE ATLAS, AND WITHOUT IT A SCROLL CORRUPTS THE IMAGE RATHER THAN COSTING A REDRAW.
//    IntegrateSunShadowResidency zeroes SunShadowLevel::ResidencyTable for each newly-exposed tile, but that table is CPU-side bookkeeping the GPU
//    never sees — the tracer resolves depth through TilePageMapping. A scrolled slot changes which GROUND it addresses while its mapping still points
//    at the page holding the PREVIOUS ground's depth, so the reader gets a page that is resident, holds real depth, and describes somewhere else.
//    Every counter stays healthy: the page was allocated, cleared and rasterized correctly, just for a region that has since scrolled away.
//
// 🔴 CALL IT AFTER IntegrateSunShadowResidency, NEVER BEFORE. Integrate advances ToroidalOrigin as its first act, and the slot each tile resolves to
//    is a function of that origin — releasing against the pre-scroll origin unmaps the slots the window is ABOUT to reuse and spares the ones actually
//    holding stale depth, which is worse than not calling it at all.
//
// ⚠️ The strips overlap at the corner when both axes moved; releasing a tile twice is a no-op by construction, so the overlap needs no subtraction.
// 📝 Released pages return to the free list, so the next DriveShadowPageAllocation re-grants and re-renders them. The cost of a scroll is therefore
//    bounded by the exposed strip area, not by the window — which is the entire point of a toroidal window.
uint32_t InvalidateScrolledShadowPages(ShadowPageAtlas&             Atlas,
                                       const SunShadowClipmap&      Clipmap,
                                       const SunShadowScrollResult& Scroll);

//------------------------------------------------------------------------------------------------------------------------
//                                                    STALENESS (S2 prerequisite)
//------------------------------------------------------------------------------------------------------------------------

// Mark one tile's page as holding wrong depth. A no-op when the tile has no page — there is nothing to invalidate, and the page it eventually gets
// starts stale anyway. Cheap enough to call per overlapped tile.
void InvalidateShadowTileContent(ShadowPageAtlas& Atlas, const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile);

// Mark every page whose tile overlaps a caster's world-space bounding sphere as stale, across every level. Returns the number of pages newly
// tagged (already-stale pages are not recounted), so a caller can log how much re-rasterization a change actually cost.
//
// 🔴 CALL THIS TWICE PER CHANGED CASTER — once with its PREVIOUS bounds and once with its CURRENT bounds. This is not motion blur and it is not
//    redundancy: the two calls answer different questions. Current bounds cover the tiles the caster now darkens; PREVIOUS bounds cover the tiles
//    it has VACATED, which still hold its depth and would otherwise keep projecting a shadow the object has walked out of. The proof that the pair
//    is load-bearing rather than symmetric is DELETION — a removed caster has no current bounds at all, yet the tiles it used to occupy must still
//    be invalidated, so the previous-bounds call is the only one that fires. (Upstream EEVEE keeps a past/current caster pair for exactly this;
//    an earlier reading of it as a motion-vector history was wrong — see PLAN-SunShadowClipmap.md P6.3c.)
//
// ⚠️ A SPHERE OVER-MARKS. Tiles in the sphere's corners get tagged without the caster touching them, costing re-rasterization that changes nothing.
//    Accepted knowingly: the bounds already exist per instance and are already world-space, so this needs no new producer. An OBB per caster is the
//    accuracy upgrade, and it only ever REMOVES tags — never adds one — so it cannot turn a correct image incorrect.
uint32_t InvalidateShadowPagesInSphere(ShadowPageAtlas&        Atlas,
                                       const SunShadowClipmap& Clipmap,
                                       Vector3f                WorldCentre,
                                       float                   WorldRadius);

// Clear the staleness flag on one page, asserting its depth has just been rasterized. The depth raster (S7) owns this call; nothing else may clear
// the flag, because a clear that does not correspond to a real render is precisely the silent-wrong-shadow bug.
void MarkShadowPageRendered(ShadowPageAtlas& Atlas, uint32_t PageIndex);

// True when this page must be re-rasterized this image: wanted now AND holding wrong depth. The single gate the raster dispatch should test — the
// reason a static scene costs nothing is that this returns false for every page once the first image has drawn them.
[[nodiscard]] bool ShadowPageNeedsRender(const ShadowPageAtlas& Atlas, uint32_t PageIndex);

// Declare every page S7 just rasterized clean, and lower the matching tile's Update bit. This is the call that makes the cache real: until it runs,
// nothing on the device path ever clears staleness, so S6 re-clears and S7 redraws the same pages every image forever.
//
// 🔴 CALL THIS ONCE, AFTER THE LAST CASTER MESH HAS BEEN RECORDED — never per mesh. The claim "this page holds correct depth" only becomes true once
//    EVERY caster has been submitted into it; marking after the first of two meshes caches a HALF-DRAWN page (heads shadowing, floor not) that the
//    cache will then never redraw. The one-call-per-image shape is what encodes that.
//
// 🔴 IT MUST SELECT THE SAME PAGES S6 CLEARED, which is why it re-tests ShadowPageNeedsRender rather than taking a list. S7 draws whole tile WINDOWS,
//    but only the pages whose tiles were both wanted and stale actually received depth; a page that was merely wanted already held valid depth and was
//    neither cleared nor redrawn. Marking a wider set clean would declare depth valid for a page nothing rasterized this image.
//
// ⚠️ MUST RUN AFTER RecordShadowDepthRaster HAS BEEN RECORDED, NOT AFTER IT HAS EXECUTED. These are CPU-side flags and the command buffer has only been
//    built, not submitted. That is sound because the flags describe what the *submission* will contain, and the submission is ordered; it is unsound
//    only if the recording is later abandoned, which the caller must not do between the two calls.
// 📝 Store may be a null-equivalent (not ready), in which case only the page flags are lowered. Returns the number of pages marked.
uint32_t MarkShadowDepthPagesRendered(ShadowPageAtlas& Atlas, ShadowTileStore& Store, const SunShadowClipmap& Clipmap);

//------------------------------------------------------------------------------------------------------------------------
//                                            S5 DRIVE + GPU MAPPING  (what S6/S7 address through)
//------------------------------------------------------------------------------------------------------------------------

// Walk the marking chain's downloaded demand and claim a page for every tile that survived masking, coarsest level FIRST.
// 🔴 THE LEVEL ORDER IS LOAD-BEARING AND IS THE OPPOSITE OF S3's. RequestShadowPage's reclaim only ever takes a victim from a level COARSER than the
//    requester, so a page is available to evict only if the coarse level has already been served. Requesting fine-to-coarse instead lets LOD 0 drain
//    the free list, then find nothing evictable (every coarse page is still Free, not Cached) and starve the coarse levels outright — the coarse
//    fallback the tracer relies on would be the first thing lost, which is exactly backwards from the LOD priority the header rules on.
// ⚠️ Reads Store.TileWords, which is authoritative ONLY between a Download-resolve and the next Reset (see ShadowTileStore.h). Call it in that window.
// 📝 Returns the number of tiles that asked for a page. Starvation is not reported here — it lands in Census.PageStarvedCount as usual.
uint32_t DriveShadowPageAllocation(ShadowPageAtlas& Atlas, const ShadowTileStore& Store, const SunShadowClipmap& Clipmap);

// Push TilePageMapping to the device SSBO S6/S7 resolve their target texels through. Records a copy outside any rendering scope.
// 🔴 The mapping is uploaded AFTER DriveShadowPageAllocation and BEFORE S6/S7 record, every image. It changes whenever the window scrolls or a page is
//    evicted, so a stale upload points a page at the region it held LAST image — depth rasterized into the wrong page, which reads as shadows smeared
//    across the world rather than as a missed copy.
void UploadShadowPageMapping(ShadowPageAtlas& Atlas, VkCommandBuffer CommandBuffer);

// Push ShadowPageNeedsRender, page by page, to the device SSBO S7 gates its depth write on. Records a copy plus the barrier that orders it before the
// fragment reads, and returns how many pages were authorized.
//
// 🔴 THE SELECTION IS S6's, EXACTLY — the same ShadowPageNeedsRender predicate RecordShadowPageClear and ClearShadowPageCoverage build their sets from.
//    S7 rasterizes whole tile windows and cannot know which pages were primed, so this mask is the only thing that keeps its write set from being every
//    mapped page. Widening it re-admits the atomic-min blend over cached depth; narrowing it leaves a primed page at the clear identity, which reads as
//    a fully-lit hole rather than as a missing write.
// ⚠️ CALL IT AFTER DriveShadowPageAllocation AND BEFORE THE FIRST RecordShadowDepthRaster, alongside UploadShadowPageMapping. The records must already
//    describe this image's ownership, and the upload must be visible to the fragment stage that tests it.
// 📝 Unlike S6's list this is uploaded even when the count is zero, so a static scene's mask correctly authorizes nothing. The whole-pass early-out in
//    RecordShadowDepthRaster still skips the draw entirely in that case.
uint32_t UploadShadowPageRenderMask(ShadowPageAtlas& Atlas, VkCommandBuffer CommandBuffer);

// Reset the coverage word of every page that is about to be re-rasterized, so the bits S7 raises describe the depth now in the page. Records the fills
// plus the barrier that orders them before S7's atomicOr, and returns how many pages were reset. Call it immediately after RecordShadowPageClear and
// BEFORE the first RecordShadowDepthRaster.
// 🔴 THE SELECTION IS S6's, EXACTLY — ShadowPageNeedsRender, page by page — and a whole-buffer fill here is not a simplification but an inversion of
//    the bug. Coverage describes the CONTENT of a page and therefore lives as long as that content does; a cached page keeps valid depth across images
//    and S7 skips it, so zeroing its bit leaves nothing to raise it again and the tracer reads a genuinely-drawn page as empty. Resetting the same set
//    whose depth is being discarded keeps the two facts in step by construction.
// 📝 Returns 0 on a static scene, which is the steady state, for the same reason RecordShadowPageClear does.
uint32_t ClearShadowPageCoverage(ShadowPageAtlas& Atlas, VkCommandBuffer CommandBuffer);

// Make S7's coverage writes visible to the tracer's fragment reads. Records a buffer barrier only; call it after the last RecordShadowDepthRaster and
// before the shading pass that includes SunShadowTrace.glsl.
void BarrierShadowPageCoverageForRead(ShadowPageAtlas& Atlas, VkCommandBuffer CommandBuffer);

// Recount the census from the page records. Cheap; call after the image's allocations settle.
void RefreshShadowPageCensus(ShadowPageAtlas& Atlas);

// True when the pool could not serve every request in the last counted image — the exhaustion signal C6 warns on.
[[nodiscard]] bool ShadowPageOverSubscribed(const ShadowPageAtlas& Atlas);

// Transition the atlas between compute/fragment write (GENERAL) and tracer read (SHADER_READ_ONLY_OPTIMAL). Records into CommandBuffer outside
// any active rendering scope. A no-op when already in the requested layout.
void TransitionShadowPageAtlas(ShadowPageAtlas& Atlas, VkCommandBuffer CommandBuffer, VkImageLayout TargetLayout);

// Destroy the view / image / memory and reset to empty. The device must be idle. Safe on a never-initialized value.
void FinalizeShadowPageAtlas(ShadowPageAtlas& Atlas);

} // namespace Frontier

#endif
