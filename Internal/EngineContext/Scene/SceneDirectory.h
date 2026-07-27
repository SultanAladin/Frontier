/*==============================================================================================================================================
                                                                SCENEDIRECTORY.H
==============================================================================================================================================*/
// 🧩 The owning tree container for the scene: it holds the dense RecordArchive and offers the high-level tree operations every editor drives —
//    spawn an entry into an enclosure, evaluate an entry's indentation depth, cycle-safe / depth-bounded relocation (the outliner drag re-enclose),
//    and recursive teardown of a subtree. Connectivity surgery lives in the AdjacencyTable; identity lives in the archive; this composes them.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_SCENE_SCENEDIRECTORY_H
#define FRONTIER_ENGINECONTEXT_SCENE_SCENEDIRECTORY_H

#include <cstdint>
#include <string>

#include "EngineContext/MicroUtils/RecordToken.h"
#include "EngineContext/Scene/AdjacencyTable.h"
#include "EngineContext/Scene/RecordArchive.h"
#include "EngineContext/Scene/RecordEntry.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                              TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Why a relocate was refused, or that it succeeded. Distinct causes let the caller surface a precise outliner message (a
//    cycle attempt reads differently from a depth-ceiling breach).
enum class RelocationOutcome : uint32_t
{
    Relocated,           // the entry moved successfully
    StaleEntry,          // the moving entry's token did not authenticate
    StaleDestination,    // the destination enclosure token did not authenticate
    CycleRefused,        // the destination lies within the moving entry's own subtree
    DepthExceeded,       // the move would push the subtree past MaximumDepthBoundary
    PopulationExceeded,  // no free slot: the archive is at its population ceiling
};

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The result of an indentation-depth trace up an entry's enclosure chain. ResolutionSuccess is false when the starting
//    token was stale or the chain dangled; DepthBoundaryReached flags that the ceiling was hit before the top level.
struct DepthEvaluationOutcome
{
    uint32_t IndentDepth          = 0u;      // [-] - tiers between the entry and the top level (top-level == 0)
    bool     ResolutionSuccess    = false;   // [-] - true only when the full chain resolved cleanly
    bool     DepthBoundaryReached = false;   // [-] - true when the depth ceiling stopped the walk early
};

// 📝 The owning tree container. Owns the archive by value; every consumer holds the directory and drives it through the free
//    functions below. Copyable/movable follows the archive (heap-backed vectors), so pass by reference in hot paths.
struct SceneDirectory
{
    RecordArchive Archive;   // [-] - the dense store of entries plus its recycle pool and top-level head
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Reserve backing capacity up front (before a bulk load) so a batch of spawns grows the archive at most once.
void InitializeDirectory(SceneDirectory& Directory, uint32_t ReservedSlots);

// Return the directory to empty, retaining backing capacity for a rebuild.
void FinalizeDirectory(SceneDirectory& Directory) noexcept;

// Spawn a titled entry directly into DestinationEnclosure at the given site (null enclosure == top level). Returns the new
// entry's token, or NullRecordToken when the population ceiling is reached or the destination/anchor is stale (in which case
// the freshly-minted slot is retired so no orphan is left behind).
[[nodiscard]] RecordToken SpawnInto(SceneDirectory&    Directory,
                                    const std::string& Title,
                                    const RecordToken& DestinationEnclosure,
                                    const InsertSite&  Site);

// Trace TargetToken's enclosure chain to its indentation depth, bounded by MaximumDepthBoundary against a malformed chain.
[[nodiscard]] DepthEvaluationOutcome EvaluateEntryDepth(const SceneDirectory& Directory, const RecordToken& TargetToken);

// Re-enclose EntryToken under DestinationEnclosure at the given site, carrying its whole nested subtree. Refuses cycles and
// depth-ceiling breaches; on refusal the tree is left exactly as it was. On success the moved subtree's cached DepthTier
// values are stale until recomputed by the caller (depth is derived, not stored eagerly).
RelocationOutcome RelocateEntry(SceneDirectory&    Directory,
                                const RecordToken& EntryToken,
                                const RecordToken& DestinationEnclosure,
                                const InsertSite&  Site);

// Detach EntryToken and retire it together with every entry nested beneath it, depth-first. Returns the number of entries
// reclaimed (0 when the token was stale). Connectivity is mended as each entry leaves.
uint32_t TeardownSubtree(SceneDirectory& Directory, const RecordToken& EntryToken);

} // namespace Frontier

#endif
