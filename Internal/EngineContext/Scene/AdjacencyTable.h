/*==============================================================================================================================================
                                                                ADJACENCYTABLE.H
==============================================================================================================================================*/
// 🧩 The connectivity store for the scene directory: the tree links between entries, held as intrusive doubly-linked nested lists on the entries
//    themselves (no heap link nodes). Attaches an entry into an enclosure, detaches it, walks same-tier peers, tests containment for the cycle
//    guard, and flattens a subtree depth-first for the outliner. All splices are O(1) pointer surgery on tokens; a null enclosure is the top tier.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_SCENE_ADJACENCYTABLE_H
#define FRONTIER_ENGINECONTEXT_SCENE_ADJACENCYTABLE_H

#include <cstdint>
#include <vector>

#include "EngineContext/MicroUtils/RecordToken.h"
#include "EngineContext/Scene/RecordArchive.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// [-] - deepest permitted enclosure chain (top-level == 0). The flatten walk is bounded by this so a malformed (cyclic) tree
//       terminates cleanly rather than recursing without end. Folded from the source ConnectivitySpecification.
constexpr uint32_t MaximumDepthBoundary = 64u;

//------------------------------------------------------------------------------------------------------------------------
//                                                              TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Where an entry lands within its new enclosure's nested list. Before/After name an existing peer as the anchor; First/Last
//    ignore the anchor.
enum class InsertPlacement : uint32_t
{
    First,    // head of the nested list
    Last,     // tail of the nested list
    Before,   // immediately ahead of AnchorPeer
    After,    // immediately behind AnchorPeer
};

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A fully-specified insertion site: the placement rule plus the peer it references (ignored for First/Last).
struct InsertSite
{
    InsertPlacement Placement  = InsertPlacement::Last;   // [-] - placement rule within the target nested list
    RecordToken     AnchorPeer = NullRecordToken;         // [-] - existing peer for Before/After (ignored otherwise)
};

// 📝 One flattened line: the entry plus its computed indentation depth relative to the flatten root (root == 0). This is the
//    canonical outliner draw order.
struct FlattenedEntry
{
    RecordToken Entry       = NullRecordToken;   // [-] - the entry on this line
    uint32_t    IndentDepth = 0u;                // [-] - tiers below the flatten origin
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Attach EntryToken into EnclosureToken's nested list at the given site. Assumes EntryToken is currently detached (its
// enclosure and lateral links are null). Writes the entry's EnclosureToken and splices the lateral links. Returns false when
// either token is stale or the site's anchor is not actually nested under EnclosureToken.
bool AttachIntoEnclosure(RecordArchive&     Archive,
                         const RecordToken& EntryToken,
                         const RecordToken& EnclosureToken,
                         const InsertSite&  Site);

// Detach EntryToken from its current enclosure's nested list, mending the neighbours' lateral links and clearing the entry's
// own enclosure/lateral links. Leaves the entry valid but top-level-detached. Returns false when stale.
bool DetachFromEnclosure(RecordArchive& Archive, const RecordToken& EntryToken);

// Collect the immediate nested entries of EnclosureToken (one tier down) in list order. A null EnclosureToken yields the
// top-level entries. O(nested).
[[nodiscard]] std::vector<RecordToken> CollectNested(const RecordArchive& Archive, const RecordToken& EnclosureToken);

// True when CandidateEnclosure appears anywhere on EntryToken's enclosure chain (EntryToken is contained by it, directly or
// transitively). Used by relocate's cycle guard. O(depth).
[[nodiscard]] bool EnclosedBy(const RecordArchive& Archive,
                              const RecordToken&    EntryToken,
                              const RecordToken&    CandidateEnclosure);

// The peer immediately after EntryToken on its tier, or NullRecordToken when it is the tail. Returns null on a stale token.
[[nodiscard]] RecordToken NextLateral(const RecordArchive& Archive, const RecordToken& EntryToken);

// The peer immediately before EntryToken on its tier, or NullRecordToken when it is the head. Returns null on a stale token.
[[nodiscard]] RecordToken PreviousLateral(const RecordArchive& Archive, const RecordToken& EntryToken);

// The head of EntryToken's own tier (walk predecessors to the front). Returns EntryToken itself when it is the head.
[[nodiscard]] RecordToken LateralHead(const RecordArchive& Archive, const RecordToken& EntryToken);

// The tail of EntryToken's own tier (walk successors to the back). Returns EntryToken itself when it is the tail.
[[nodiscard]] RecordToken LateralTail(const RecordArchive& Archive, const RecordToken& EntryToken);

// Flatten the subtree rooted at RootEnclosure in depth-first pre-order: each entry appears immediately before its own nested
// region. A null RootEnclosure flattens the entire directory from the top level. Depth- and population-bounded against runaway
// cycles. This is the canonical outliner draw order.
[[nodiscard]] std::vector<FlattenedEntry> FlattenSubtree(const RecordArchive& Archive, const RecordToken& RootEnclosure);

} // namespace Frontier

#endif
