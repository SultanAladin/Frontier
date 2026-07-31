/*==============================================================================================================================================
                                                           SUNSHADOWCLIPMAP.H
==============================================================================================================================================*/
// 🧩 The sun's own scrolling window: a 2D tile lattice laid out in LIGHT space, one level per shadow LOD, that follows the camera by integer
//    tile shifts exactly as the GI voxel clipmap follows it in world space. Each level is SHADOW_TILEMAP_RES² tiles covering twice the world
//    extent of the level below, so shadow detail is dense near the viewer and coarse far away. Scrolling by whole tiles is what makes a moving
//    camera cheap: only the newly-exposed L-strip of tiles is marked dirty, and every tile that stayed inside the window keeps the page — and
//    therefore the rendered depth — it already had.
//
//    Pure CPU math, no Vulkan. This unit answers only "which tiles does the sun care about, and which of them just became stale"; the pages
//    themselves, the atlas, and the depth raster are separate units (P6.2 / P6.4).
//
// 🔴 Light space, NOT world space — this is the whole reason SunShadowClipmap exists rather than another ToroidalClipmapField level. A shadow
//    tile is addressed by `lP.xy` where `lP = SunView * P`, so the lattice basis ROTATES WITH THE SUN. ToroidalClipmapField's cells are floored
//    straight from world XYZ and are rigidly world-axis-aligned. No relabelling of axes reconciles those two; the shared part is the integer
//    arithmetic in ToroidalAddressing.h and nothing else. (PLAN-SunShadowClipmap.md §3 — a ruling reached by two independent fit studies.)
//
// ⚠️ Because the basis rotates, a sun that MOVES invalidates the whole window rather than exposing a strip. That coarse path is deliberately
//    separate from tile scrolling — see SunShadowScrollResult::WholeWindowDirtyCondition.

#pragma once
#ifndef FRONTIER_GRAPHICS_SHADOW_SUNSHADOWCLIPMAP_H
#define FRONTIER_GRAPHICS_SHADOW_SUNSHADOWCLIPMAP_H

#include "EngineContext/Math/LinearAlgebra_Float32.h"
#include "EngineContext/SpatialAcceleration/ToroidalAddressing.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The tilemap shape is EEVEE's (eevee_defines.hh) so the tile budgeting matches a shipping implementation rather than a guess.
// 🔴 THE PAGE RESOLUTION DELIBERATELY DEPARTS FROM EEVEE'S SHADOW_PAGE_RES OF 256, and the reason is our tile budget, not a transcription slip.
//    EEVEE pairs 256² pages with a 4096-tile virtual map and its own LOD distribution; we run a 32² x 6 map at ShadowBaseTileMetres = 0.5, which puts
//    far more FINE-level tiles on flat ground than their setup does. Measured on the two-Suzanne + 100 m floor scene, S1 marks ~759 tiles per image
//    (the floor out-spans every level's window, so each level's 32² window lands on it) — 759 pages of 256² is 199 M depth texels for ~20 k triangles,
//    and at L0 a 256² page over a 0.5 m tile is 2 mm/texel. That density is never consumed. 128² keeps ~4 mm/texel at L0 and is what lets the pool
//    hold 1024 pages in the SAME 64 MiB and the SAME 4096² image the 16 x 256 shape cost. ⚠️ Copying EEVEE's page SIZE while ignoring their tile
//    BUDGET is taking one constant out of a tuned pair; if the tilemap shape or ShadowBaseTileMetres changes, re-derive this rather than restoring 256.
constexpr uint32_t ShadowTilemapResolution = 32;   // [tiles] - tiles per square edge, one level
constexpr uint32_t ShadowTilemapLodCount   = 6;    // [-]     - LODs 0..5
constexpr uint32_t ShadowPageResolution    = 128;  // [texel] - one page's square edge in the atlas

// Level 0 covers this much world per tile; each coarser level doubles it. 32 tiles x 0.5 m = 16 m across at LOD 0, out to 512 m at LOD 5 —
// dense enough for contact shadows at the heads' scale, wide enough to cover the 100 m floor slab at the coarse end.
constexpr float ShadowBaseTileMetres = 0.5f;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 An orthonormal light frame: the basis that turns a world point into the light-space coordinates the tile lattice is addressed in.
//    Forward points ALONG the light's travel (surface -> away from the sun), so a caster's depth grows with distance from the sun.
struct SunShadowBasis
{
    Vector3f RightAxis{ 1.0f, 0.0f, 0.0f };    // [-] - light-space +X
    Vector3f UpAxis{ 0.0f, 1.0f, 0.0f };       // [-] - light-space +Y
    Vector3f ForwardAxis{ 0.0f, 0.0f, -1.0f }; // [-] - light-space +Z, along the light's direction of travel
};

// 📝 An integer tile coordinate in one level's square lattice. Signed, because a tile left of or below the origin is perfectly normal; the
//    modular wrap folds it into the [0, Resolution) physical range at address time.
struct TileCoordinate
{
    int32_t XTile = 0;   // [tile]
    int32_t YTile = 0;   // [tile]
};

// 📝 One resident square window of tiles at a single LOD. ToroidalOrigin is the light-space tile the physical [0,0] corner currently maps to;
//    advancing it IS the scroll. TileMetres is this level's tile edge, doubling per coarser level.
struct SunShadowLevel
{
    uint32_t             Resolution   = ShadowTilemapResolution;  // [tiles] - square edge count
    float                TileMetres   = ShadowBaseTileMetres;     // [m]     - light-space world edge of one tile
    TileCoordinate       ToroidalOrigin;                          // [tile]  - light tile mapped to physical corner [0,0]
    bool                 OriginSeeded = false;                    // [-]     - false until the first scroll centres this level

    std::vector<uint8_t> ResidencyTable;                          // [-]     - 1 == the tile's page is valid, 0 == needs re-render; length Resolution²
};

// 📝 What one image's scroll of a single level exposed. ExposedStrips holds the tiles that were NOT inside the window before the shift and so
//    must be re-rendered. At most 2 for a 2D window (one per axis), which is the bound the fixed array encodes.
// ⚠️ The two strips OVERLAP at the corner when both axes move. Intended — re-marking a tile dirty is idempotent, and subtracting the
//    intersection risks leaving a corner tile holding another region's depth.
struct SunShadowScrollResult
{
    uint32_t       Level             = 0;
    TileCoordinate OriginBefore;
    TileCoordinate OriginAfter;
    TileCoordinate TileShift;                       // [tile] - OriginAfter - OriginBefore

    uint32_t       ExposedStripCount = 0;           // [-] - 0, 1, or 2
    ToroidalStrip  ExposedStrips[2];                // [-] - the axis-aligned strips exposed by the shift
    bool           WholeWindowDirtyCondition = false; // [-] - true when the LIGHT rotated: every tile is stale, strips are meaningless
};

// 📝 The whole sun window: the light basis it is expressed in, plus one scrolling tile lattice per LOD.
//    SolarDirectionPrevious is what makes rotation detectable; it is compared, not merely recorded.
struct SunShadowClipmap
{
    SunShadowBasis              Basis;
    Vector3f                    SolarDirectionPrevious{ 0.0f, 0.0f, 1.0f }; // [-] - unit surface->sun of the previous image
    bool                        SolarDirectionSeeded = false;               // [-] - false until the first Refresh records a direction
    std::vector<SunShadowLevel> Levels;
    bool                        ReadyCondition = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the orthonormal light frame for a unit surface->sun direction (the same convention AtmosphereProfile::SolarDirection uses).
// 🔴 Gram-Schmidt against world +Z, falling back to +X when the sun is within ~1e-3 of vertical. Without that fallback a directly-overhead sun
//    makes the cross product degenerate and the basis collapses to NaN — the failure appears as the shadows vanishing at exactly local noon,
//    which reads as a shadow-math bug rather than a basis one.
[[nodiscard]] SunShadowBasis SolveSunShadowBasis(Vector3f SolarDirection);

// Allocate LevelCount LODs, each Resolution² tiles with the level-0 tile edge doubling per level, and clear all residency. Safe to call again;
// it rebuilds from scratch. LevelCount is clamped to ShadowTilemapLodCount.
void InitializeSunShadowClipmap(SunShadowClipmap& Clipmap,
                                uint32_t          LevelCount  = ShadowTilemapLodCount,
                                uint32_t          Resolution  = ShadowTilemapResolution,
                                float             BaseTileMetres = ShadowBaseTileMetres);

// Re-centre one level on the observer and report what that exposed. Does NOT mutate residency — Integrate does, so a caller can inspect the
// scroll before committing to it. SolarDirection is the CURRENT image's unit surface->sun vector.
[[nodiscard]] SunShadowScrollResult EvaluateSunShadowScroll(const SunShadowClipmap& Clipmap,
                                                            uint32_t                Level,
                                                            Vector3f                ObserverPosition,
                                                            Vector3f                SolarDirection);

// Commit a scroll: advance the level's origin and clear residency for every exposed tile (or for the whole window when the light rotated).
// Pair with Evaluate; passing a result from a different level or a stale clipmap state is a caller error.
void IntegrateSunShadowResidency(SunShadowClipmap& Clipmap, const SunShadowScrollResult& Scroll);

// Advance every level for this image against the observer and the sun, committing each scroll. Records SolarDirection as the previous
// direction for the NEXT image's rotation test, and refreshes Basis. This is the per-image entry point (plan steps C1-C4).
void RefreshSunShadowClipmap(SunShadowClipmap& Clipmap, Vector3f ObserverPosition, Vector3f SolarDirection);

// The light-space tile a world point falls in, at one level. The inverse of the addressing the tile lattice is built on.
[[nodiscard]] TileCoordinate ResolveSunShadowTile(const SunShadowClipmap& Clipmap, uint32_t Level, Vector3f WorldPosition);

// The physical (wrapped) tile a light-space tile currently maps to, and the flat index into that level's dense tables.
[[nodiscard]] TileCoordinate ResolveSunShadowPhysicalTile(const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile);
[[nodiscard]] uint32_t       ResolveSunShadowTileIndex(const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile);

// True when the tile's page currently holds valid depth. A vacant tile must be re-rendered before it is sampled.
[[nodiscard]] bool SunShadowTileResident(const SunShadowClipmap& Clipmap, uint32_t Level, TileCoordinate LightTile);

// Release every level and reset to empty. Safe on a never-initialized value.
void FinalizeSunShadowClipmap(SunShadowClipmap& Clipmap);

} // namespace Frontier

#endif
