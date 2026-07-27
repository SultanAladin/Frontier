/*==============================================================================================================================================
                                                                RECORDARCHIVE.H
==============================================================================================================================================*/
// 🧩 Contiguous packed storage for scene-directory entries (the dense store): a heap-backed entry array with parallel generation and occupancy
//    arrays, plus the canonical MicroUtils VacancyTable recycling dormant slots. Resolve is O(1) via the shared TokenAuthentication; iteration
//    is cache-friendly slot order. Grows on demand up to a population ceiling — a Reserve at load pre-sizes so bulk spawns never realloc mid-cycle.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_SCENE_RECORDARCHIVE_H
#define FRONTIER_ENGINECONTEXT_SCENE_RECORDARCHIVE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "EngineContext/MicroUtils/RecordToken.h"
#include "EngineContext/MicroUtils/TokenAuthentication.h"
#include "EngineContext/MicroUtils/TokenIssuer.h"
#include "EngineContext/MicroUtils/VacancyTable.h"
#include "EngineContext/Scene/RecordEntry.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// [-] - largest permitted live-entry population (~1M). A spawn that would breach this is refused with NullRecordToken rather
//       than growing unbounded. The dense arrays grow toward this on demand; they are never all reserved up front.
constexpr uint32_t MaximumEntryPopulation = 1u << 20;

// [-] - bounded ceiling on the recycle pool of dormant slots. The pool need only hold slots dormant AT ONCE, not the whole
//       population, so it stays a small inline array. When more than this are dormant simultaneously, the surplus slots are
//       simply not recycled (bounded waste, never corruption — see ReclaimVacancy) until churn drains the pool.
constexpr uint32_t VacancyPoolCapacity = 1u << 16;

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The packed backing store. The three primary vectors are parallel — index i in each describes slot i — and are grown ONLY
//    in lockstep through the canonical GrowOneSlot, so they never diverge in length. Tokens (not pointers) name entries across
//    edits, so a growth reallocation is safe: no live pointer survives a spawn. Generation 0 is reserved null; a fresh slot
//    starts at generation 1 on first spawn. EntryType lets the store-generic TokenIssuer default-construct the right entry.
struct RecordArchive
{
    using EntryType = RecordEntry;

    std::vector<RecordEntry> Entries;                  // [-] - dense; index == RecordToken.SlotIndex
    std::vector<uint32_t>    Generations;              // [-] - parallel; live generation of slot i
    std::vector<uint8_t>     Occupancy;                // [-] - parallel; 1 == occupied, 0 == dormant
    VacancyTable<VacancyPoolCapacity> Vacancies;       // [-] - canonical MicroUtils recycle pool of dormant slot indices

    // Head of the top-level nested list — the entries whose EnclosureToken is null. No entry holds the head of the top tier,
    // so the store carries it directly, mirroring an entry's NestedRegionToken one level up.
    RecordToken TopLevelHead = NullRecordToken;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Reserve backing capacity up front so a subsequent batch of spawns (a deserialize / import) grows the arrays at most once
// rather than reallocating per entry mid-load. Clamped to the population ceiling. Call at Initialize / before a bulk load.
void ReserveArchive(RecordArchive& Archive, uint32_t SlotCount);

// Return the archive to empty: clears all entries, generations, occupancy, and the vacancy pool, and drops the top-level head.
// Backing capacity is retained so a rebuild does not re-grow from zero.
void ResetArchive(RecordArchive& Archive) noexcept;

// Total slot capacity currently allocated (occupied plus dormant).
[[nodiscard]] std::size_t ArchiveCapacity(const RecordArchive& Archive) noexcept;

// Count of occupied (live) slots.
[[nodiscard]] std::size_t LivingCount(const RecordArchive& Archive) noexcept;

// True when the slot index is within bounds AND currently occupied.
[[nodiscard]] bool SlotOccupied(const RecordArchive& Archive, uint32_t SlotIndex) noexcept;

// Mint a new entry and return its live token, or NullRecordToken when the population ceiling is reached. The returned entry is
// default-initialized and occupied; the caller fills its title/placement/classification and links it via the AdjacencyTable.
[[nodiscard]] RecordToken SpawnEntry(RecordArchive& Archive);

// Retire the entry a token names: advances its generation (staling every outstanding copy), marks the slot dormant, and returns
// it to the recycle pool. No-op (returns false) on a stale/invalid token. The caller must detach connectivity first.
bool DespawnEntry(RecordArchive& Archive, const RecordToken& Token);

} // namespace Frontier

#endif
