/*==============================================================================================================================================
                                                                RECORDARCHIVE.CPP
==============================================================================================================================================*/
// 🧩 Dense-store lifecycle: reserve, reset, and the spawn/despawn path that delegates identity to the canonical MicroUtils TokenIssuer. Growth is
//    always in lockstep across the three parallel arrays; tokens (never pointers) name entries across edits so a growth reallocation stays safe.

#include "EngineContext/Scene/RecordArchive.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ReserveArchive(RecordArchive& Archive, uint32_t SlotCount)
{
    const uint32_t ClampedCount = (SlotCount > MaximumEntryPopulation) ? MaximumEntryPopulation : SlotCount;

    Archive.Entries.reserve(ClampedCount);
    Archive.Generations.reserve(ClampedCount);
    Archive.Occupancy.reserve(ClampedCount);
}

void ResetArchive(RecordArchive& Archive) noexcept
{
    // 📝 clear() keeps the reserved capacity, so a rebuild does not re-grow the arrays from zero.
    Archive.Entries.clear();
    Archive.Generations.clear();
    Archive.Occupancy.clear();
    ResetVacancyTable(Archive.Vacancies);
    Archive.TopLevelHead = NullRecordToken;
}

std::size_t ArchiveCapacity(const RecordArchive& Archive) noexcept
{
    return Archive.Entries.size();
}

std::size_t LivingCount(const RecordArchive& Archive) noexcept
{
    // 📝 Derived by scanning occupancy rather than subtracting the vacancy depth: a bounded vacancy pool can silently drop a
    //    surplus dormant slot, so pool depth is not a reliable proxy for the dormant count. This is O(capacity) but exact.
    std::size_t Living = 0;
    for (std::size_t SlotIndex = 0; SlotIndex < Archive.Occupancy.size(); ++SlotIndex)
    {
        if (Archive.Occupancy[SlotIndex] != 0u)
        {
            ++Living;
        }
    }
    return Living;
}

bool SlotOccupied(const RecordArchive& Archive, uint32_t SlotIndex) noexcept
{
    return static_cast<std::size_t>(SlotIndex) < Archive.Occupancy.size() && Archive.Occupancy[SlotIndex] != 0u;
}

RecordToken SpawnEntry(RecordArchive& Archive)
{
    // Identity, recycling, and lockstep growth all live in the canonical issuer; the ceiling guards unbounded growth.
    return IssueToken(Archive, MaximumEntryPopulation);
}

bool DespawnEntry(RecordArchive& Archive, const RecordToken& Token)
{
    // Generation advance + occupancy clear + slot recycle, all through the canonical issuer's revoke path.
    return RevokeToken(Archive, Token);
}

} // namespace Frontier
