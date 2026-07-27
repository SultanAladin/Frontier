/*==============================================================================================================================================
                                                                SCENEDIRECTORY.CPP
==============================================================================================================================================*/
// 🧩 High-level tree operations composed over the archive and the adjacency table: spawn-into (mint then splice, retiring the slot on a failed
//    splice so no orphan survives), enclosure-chain depth trace, cycle-safe depth-bounded relocation, and depth-first subtree teardown. Every
//    traversal is bounded by the depth ceiling and re-resolves through the stale-token guard so a mid-operation reallocation never dangles.

#include "EngineContext/Scene/SceneDirectory.h"

#include <vector>

#include "EngineContext/MicroUtils/TokenAuthentication.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// The deepest additional tier carried below EntryToken (0 when it has no nested entries). Derived from a flatten of the moving
// subtree so the depth guard can add it to the destination's own depth. FlattenSubtree from EntryToken lists the nested region
// only (the root is not included), so the maximum IndentDepth is exactly the carried span.
uint32_t SubtreeDepthSpan(const RecordArchive& Archive, const RecordToken& EntryToken)
{
    uint32_t DeepestRelative = 0u;
    for (const FlattenedEntry& Line : FlattenSubtree(Archive, EntryToken))
    {
        if (Line.IndentDepth > DeepestRelative)
        {
            DeepestRelative = Line.IndentDepth;
        }
    }
    return DeepestRelative;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeDirectory(SceneDirectory& Directory, uint32_t ReservedSlots)
{
    ReserveArchive(Directory.Archive, ReservedSlots);
}

void FinalizeDirectory(SceneDirectory& Directory) noexcept
{
    ResetArchive(Directory.Archive);
}

RecordToken SpawnInto(SceneDirectory&    Directory,
                      const std::string& Title,
                      const RecordToken& DestinationEnclosure,
                      const InsertSite&  Site)
{
    const RecordToken Issued = SpawnEntry(Directory.Archive);
    if (!PopulatedToken(Issued))
    {
        return NullRecordToken;   // population ceiling reached
    }

    // Fill the entry's title before splicing. Re-resolve after each potential reallocation point.
    if (RecordEntry* Entry = ResolveEntry(Directory.Archive, Issued))
    {
        Entry->Title = Title;
    }

    // Splice into the destination. On failure (stale destination / bad anchor) retire the slot so no orphan is left behind.
    if (!AttachIntoEnclosure(Directory.Archive, Issued, DestinationEnclosure, Site))
    {
        DespawnEntry(Directory.Archive, Issued);
        return NullRecordToken;
    }

    return Issued;
}

DepthEvaluationOutcome EvaluateEntryDepth(const SceneDirectory& Directory, const RecordToken& TargetToken)
{
    DepthEvaluationOutcome Outcome;

    const RecordEntry* StartingEntry = ResolveEntry(Directory.Archive, TargetToken);
    if (StartingEntry == nullptr)
    {
        return Outcome;   // ResolutionSuccess stays false — the starting token was stale/invalid
    }

    RecordToken EnclosureCursor = StartingEntry->EnclosureToken;

    // Walk one enclosure per iteration until the top level (null enclosure) or the ceiling is hit.
    while (PopulatedToken(EnclosureCursor))
    {
        const RecordEntry* EnclosingEntry = ResolveEntry(Directory.Archive, EnclosureCursor);
        if (EnclosingEntry == nullptr)
        {
            // A dangling enclosure link — the tree is malformed. Report failure rather than a misleading depth.
            Outcome.ResolutionSuccess = false;
            return Outcome;
        }

        ++Outcome.IndentDepth;
        EnclosureCursor = EnclosingEntry->EnclosureToken;

        if (Outcome.IndentDepth >= MaximumDepthBoundary)
        {
            Outcome.DepthBoundaryReached = true;
            break;
        }
    }

    Outcome.ResolutionSuccess = true;
    return Outcome;
}

RelocationOutcome RelocateEntry(SceneDirectory&    Directory,
                                const RecordToken& EntryToken,
                                const RecordToken& DestinationEnclosure,
                                const InsertSite&  Site)
{
    if (ResolveEntry(Directory.Archive, EntryToken) == nullptr)
    {
        return RelocationOutcome::StaleEntry;
    }

    // A populated destination must authenticate; a null destination (top level) is always a valid target.
    if (PopulatedToken(DestinationEnclosure) && ResolveEntry(Directory.Archive, DestinationEnclosure) == nullptr)
    {
        return RelocationOutcome::StaleDestination;
    }

    // Cycle guard: the destination may not be the entry itself, nor lie anywhere within the entry's own subtree.
    if (DestinationEnclosure == EntryToken || EnclosedBy(Directory.Archive, DestinationEnclosure, EntryToken))
    {
        return RelocationOutcome::CycleRefused;
    }

    // Depth guard: destination depth + one (for the entry itself) + the carried subtree span must fit the ceiling.
    uint32_t DestinationDepth = 0u;
    if (PopulatedToken(DestinationEnclosure))
    {
        const DepthEvaluationOutcome DepthOutcome = EvaluateEntryDepth(Directory, DestinationEnclosure);
        if (!DepthOutcome.ResolutionSuccess)
        {
            return RelocationOutcome::StaleDestination;
        }
        DestinationDepth = DepthOutcome.IndentDepth + 1u;   // the destination sits one tier below its own depth
    }

    const uint32_t ProjectedDeepest = DestinationDepth + SubtreeDepthSpan(Directory.Archive, EntryToken);
    if (ProjectedDeepest >= MaximumDepthBoundary)
    {
        return RelocationOutcome::DepthExceeded;
    }

    // All guards passed — detach from the current enclosure and splice into the destination. The nested subtree rides along
    // untouched: only the moving entry's own enclosure/lateral links change; its NestedRegionToken is intact.
    if (!DetachFromEnclosure(Directory.Archive, EntryToken))
    {
        return RelocationOutcome::StaleEntry;
    }
    if (!AttachIntoEnclosure(Directory.Archive, EntryToken, DestinationEnclosure, Site))
    {
        return RelocationOutcome::StaleDestination;
    }

    return RelocationOutcome::Relocated;
}

uint32_t TeardownSubtree(SceneDirectory& Directory, const RecordToken& EntryToken)
{
    if (ResolveEntry(Directory.Archive, EntryToken) == nullptr)
    {
        return 0u;
    }

    // 📝 Snapshot the subtree in depth-first order FIRST (tokens, not pointers), then detach the root and retire every entry.
    //    Collecting before mutating means the walk never observes a half-mended list. FlattenSubtree(root) lists the nested
    //    region only, so the root is retired explicitly alongside the collected descendants.
    const std::vector<FlattenedEntry> Subtree = FlattenSubtree(Directory.Archive, EntryToken);

    // Detach the root from its enclosure so the surrounding list is mended once; the descendants leave with their root.
    DetachFromEnclosure(Directory.Archive, EntryToken);

    uint32_t Reclaimed = 0u;
    for (const FlattenedEntry& Line : Subtree)
    {
        if (DespawnEntry(Directory.Archive, Line.Entry))
        {
            ++Reclaimed;
        }
    }
    if (DespawnEntry(Directory.Archive, EntryToken))
    {
        ++Reclaimed;
    }

    return Reclaimed;
}

} // namespace Frontier
