/*==============================================================================================================================================
                                                                ADJACENCYTABLE.CPP
==============================================================================================================================================*/
// 🧩 Intrusive doubly-linked nested-list surgery for the enclosure connections plus same-tier navigation and depth-first flattening. Attach and
//    detach are O(1) splices on the entries' lateral links; a null enclosure routes through the archive's top-level head. Every dereference goes
//    through the canonical stale-token guard, and the flatten walk is bounded by the depth ceiling and an entry budget so a cyclic tree ends cleanly.

#include "EngineContext/Scene/AdjacencyTable.h"

#include "EngineContext/MicroUtils/TokenAuthentication.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// The head of a nested list lives either in the enclosing entry (its NestedRegionToken) or, for the top tier, in the archive
// itself (TopLevelHead). This resolves a mutable reference to whichever applies so splicing is uniform. Returns nullptr when a
// populated enclosure token is stale.
RecordToken* NestedHeadReference(RecordArchive& Archive, const RecordToken& EnclosureToken)
{
    if (!PopulatedToken(EnclosureToken))
    {
        return &Archive.TopLevelHead;
    }

    RecordEntry* EnclosingEntry = ResolveEntry(Archive, EnclosureToken);
    if (EnclosingEntry == nullptr)
    {
        return nullptr;
    }
    return &EnclosingEntry->NestedRegionToken;
}

// Resolve the head of a tier's nested list for the read-only flatten/collect paths: the archive's top-level head for a null
// enclosure, otherwise the enclosing entry's nested region head. Returns null when a populated token is stale.
RecordToken TierHead(const RecordArchive& Archive, const RecordToken& EnclosureToken)
{
    if (!PopulatedToken(EnclosureToken))
    {
        return Archive.TopLevelHead;
    }
    const RecordEntry* EnclosingEntry = ResolveEntry(Archive, EnclosureToken);
    return (EnclosingEntry != nullptr) ? EnclosingEntry->NestedRegionToken : NullRecordToken;
}

// Explicit depth-first pre-order walk. Each entry appears immediately before its own nested region; the walk resumes at the
// lateral successor after descending. Bounded by the depth ceiling and a remaining-entry budget so a malformed tree terminates.
void FlattenRecurse(const RecordArchive&         Archive,
                    const RecordToken&           EnclosureToken,
                    uint32_t                     Depth,
                    std::size_t&                 RemainingBudget,
                    std::vector<FlattenedEntry>& Output)
{
    if (Depth >= MaximumDepthBoundary || RemainingBudget == 0)
    {
        return;
    }

    RecordToken Cursor = TierHead(Archive, EnclosureToken);
    while (PopulatedToken(Cursor) && RemainingBudget > 0)
    {
        const RecordEntry* Entry = ResolveEntry(Archive, Cursor);
        if (Entry == nullptr)
        {
            break;   // dangling lateral link — stop this tier rather than dereference a stale token
        }

        Output.push_back(FlattenedEntry{ Cursor, Depth });
        --RemainingBudget;

        // Descend into this entry's own nested region before advancing to its lateral successor (pre-order).
        if (PopulatedToken(Entry->NestedRegionToken))
        {
            FlattenRecurse(Archive, Cursor, Depth + 1u, RemainingBudget, Output);
        }

        Cursor = Entry->LateralSuccessorToken;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        ATTACH INTO ENCLOSURE
//------------------------------------------------------------------------------------------------------------------------

bool AttachIntoEnclosure(RecordArchive&     Archive,
                         const RecordToken& EntryToken,
                         const RecordToken& EnclosureToken,
                         const InsertSite&  Site)
{
    RecordEntry* Entry = ResolveEntry(Archive, EntryToken);
    if (Entry == nullptr)
    {
        return false;
    }

    // A populated enclosure must itself resolve; a null enclosure means the top tier and is always valid.
    if (PopulatedToken(EnclosureToken) && ResolveEntry(Archive, EnclosureToken) == nullptr)
    {
        return false;
    }

    Entry->EnclosureToken = EnclosureToken;

    RecordToken* HeadReference = NestedHeadReference(Archive, EnclosureToken);
    if (HeadReference == nullptr)
    {
        return false;
    }

    // Resolve the placement into a concrete predecessor/successor pair, then splice the entry between them.
    RecordToken PredecessorToken = NullRecordToken;
    RecordToken SuccessorToken   = NullRecordToken;

    switch (Site.Placement)
    {
        case InsertPlacement::First:
        {
            SuccessorToken = *HeadReference;
            break;
        }
        case InsertPlacement::Last:
        {
            // Walk to the tail. O(nested) — acceptable; callers that reorder hot lists use Before/After instead.
            RecordToken Cursor = *HeadReference;
            while (PopulatedToken(Cursor))
            {
                PredecessorToken = Cursor;
                const RecordEntry* CursorEntry = ResolveEntry(Archive, Cursor);
                Cursor = (CursorEntry != nullptr) ? CursorEntry->LateralSuccessorToken : NullRecordToken;
            }
            break;
        }
        case InsertPlacement::Before:
        {
            const RecordEntry* AnchorEntry = ResolveEntry(Archive, Site.AnchorPeer);
            if (AnchorEntry == nullptr || AnchorEntry->EnclosureToken != EnclosureToken)
            {
                return false;   // anchor is stale or not actually nested here
            }
            SuccessorToken   = Site.AnchorPeer;
            PredecessorToken = AnchorEntry->LateralPredecessorToken;
            break;
        }
        case InsertPlacement::After:
        {
            const RecordEntry* AnchorEntry = ResolveEntry(Archive, Site.AnchorPeer);
            if (AnchorEntry == nullptr || AnchorEntry->EnclosureToken != EnclosureToken)
            {
                return false;
            }
            PredecessorToken = Site.AnchorPeer;
            SuccessorToken   = AnchorEntry->LateralSuccessorToken;
            break;
        }
    }

    // Splice: re-resolve endpoints (the archive may have reallocated during the tail walk) and wire the four links.
    Entry = ResolveEntry(Archive, EntryToken);
    if (Entry == nullptr)
    {
        return false;
    }
    Entry->LateralPredecessorToken = PredecessorToken;
    Entry->LateralSuccessorToken   = SuccessorToken;

    if (PopulatedToken(PredecessorToken))
    {
        if (RecordEntry* PredecessorEntry = ResolveEntry(Archive, PredecessorToken))
        {
            PredecessorEntry->LateralSuccessorToken = EntryToken;
        }
    }
    else
    {
        // No predecessor — the entry is the new head of this nested list. Re-resolve the head (post-realloc-safe).
        RecordToken* Head = NestedHeadReference(Archive, EnclosureToken);
        if (Head == nullptr)
        {
            return false;
        }
        *Head = EntryToken;
    }

    if (PopulatedToken(SuccessorToken))
    {
        if (RecordEntry* SuccessorEntry = ResolveEntry(Archive, SuccessorToken))
        {
            SuccessorEntry->LateralPredecessorToken = EntryToken;
        }
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        DETACH FROM ENCLOSURE
//------------------------------------------------------------------------------------------------------------------------

bool DetachFromEnclosure(RecordArchive& Archive, const RecordToken& EntryToken)
{
    RecordEntry* Entry = ResolveEntry(Archive, EntryToken);
    if (Entry == nullptr)
    {
        return false;
    }

    const RecordToken EnclosureToken   = Entry->EnclosureToken;
    const RecordToken PredecessorToken = Entry->LateralPredecessorToken;
    const RecordToken SuccessorToken   = Entry->LateralSuccessorToken;

    // Mend the predecessor→successor link, or advance the list head when the entry WAS the head.
    if (PopulatedToken(PredecessorToken))
    {
        if (RecordEntry* PredecessorEntry = ResolveEntry(Archive, PredecessorToken))
        {
            PredecessorEntry->LateralSuccessorToken = SuccessorToken;
        }
    }
    else
    {
        RecordToken* Head = NestedHeadReference(Archive, EnclosureToken);
        if (Head != nullptr)
        {
            *Head = SuccessorToken;
        }
    }

    if (PopulatedToken(SuccessorToken))
    {
        if (RecordEntry* SuccessorEntry = ResolveEntry(Archive, SuccessorToken))
        {
            SuccessorEntry->LateralPredecessorToken = PredecessorToken;
        }
    }

    // Clear the detached entry's own links — it is now a valid but unlinked top-level-detached entry.
    Entry = ResolveEntry(Archive, EntryToken);
    if (Entry == nullptr)
    {
        return false;
    }
    Entry->EnclosureToken          = NullRecordToken;
    Entry->LateralPredecessorToken = NullRecordToken;
    Entry->LateralSuccessorToken   = NullRecordToken;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           COLLECT NESTED
//------------------------------------------------------------------------------------------------------------------------

std::vector<RecordToken> CollectNested(const RecordArchive& Archive, const RecordToken& EnclosureToken)
{
    std::vector<RecordToken> Nested;

    RecordToken Cursor = TierHead(Archive, EnclosureToken);
    while (PopulatedToken(Cursor))
    {
        Nested.push_back(Cursor);
        const RecordEntry* CursorEntry = ResolveEntry(Archive, Cursor);
        Cursor = (CursorEntry != nullptr) ? CursorEntry->LateralSuccessorToken : NullRecordToken;
    }

    return Nested;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENCLOSED BY
//------------------------------------------------------------------------------------------------------------------------

bool EnclosedBy(const RecordArchive& Archive, const RecordToken& EntryToken, const RecordToken& CandidateEnclosure)
{
    if (!PopulatedToken(CandidateEnclosure))
    {
        return false;   // the top level encloses nothing in the containment sense used by the cycle guard
    }

    const RecordEntry* Entry = ResolveEntry(Archive, EntryToken);
    if (Entry == nullptr)
    {
        return false;
    }

    // Bound the walk by the depth ceiling so a corrupt cyclic chain cannot spin forever.
    RecordToken Cursor = Entry->EnclosureToken;
    uint32_t    Steps  = 0u;
    while (PopulatedToken(Cursor) && Steps < MaximumDepthBoundary)
    {
        if (Cursor == CandidateEnclosure)
        {
            return true;
        }
        const RecordEntry* CursorEntry = ResolveEntry(Archive, Cursor);
        Cursor = (CursorEntry != nullptr) ? CursorEntry->EnclosureToken : NullRecordToken;
        ++Steps;
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           LATERAL STEPS
//------------------------------------------------------------------------------------------------------------------------

RecordToken NextLateral(const RecordArchive& Archive, const RecordToken& EntryToken)
{
    const RecordEntry* Entry = ResolveEntry(Archive, EntryToken);
    return (Entry != nullptr) ? Entry->LateralSuccessorToken : NullRecordToken;
}

RecordToken PreviousLateral(const RecordArchive& Archive, const RecordToken& EntryToken)
{
    const RecordEntry* Entry = ResolveEntry(Archive, EntryToken);
    return (Entry != nullptr) ? Entry->LateralPredecessorToken : NullRecordToken;
}

RecordToken LateralHead(const RecordArchive& Archive, const RecordToken& EntryToken)
{
    RecordToken Cursor = EntryToken;
    while (const RecordEntry* Entry = ResolveEntry(Archive, Cursor))
    {
        if (!PopulatedToken(Entry->LateralPredecessorToken))
        {
            break;
        }
        Cursor = Entry->LateralPredecessorToken;
    }
    return Cursor;
}

RecordToken LateralTail(const RecordArchive& Archive, const RecordToken& EntryToken)
{
    RecordToken Cursor = EntryToken;
    while (const RecordEntry* Entry = ResolveEntry(Archive, Cursor))
    {
        if (!PopulatedToken(Entry->LateralSuccessorToken))
        {
            break;
        }
        Cursor = Entry->LateralSuccessorToken;
    }
    return Cursor;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         SUBTREE FLATTENING
//------------------------------------------------------------------------------------------------------------------------

std::vector<FlattenedEntry> FlattenSubtree(const RecordArchive& Archive, const RecordToken& RootEnclosure)
{
    std::vector<FlattenedEntry> Output;

    std::size_t RemainingBudget = static_cast<std::size_t>(MaximumEntryPopulation);
    FlattenRecurse(Archive, RootEnclosure, 0u, RemainingBudget, Output);

    return Output;
}

} // namespace Frontier
