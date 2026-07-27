/*==============================================================================================================================================
                                                                TOKENISSUER.H
==============================================================================================================================================*/
// 🧩 Canonical slot mint/revoke path for any dense record store — the sole authority that hands out generational RecordTokens and retires them.
//    IssueToken recycles a dormant slot from the VacancyTable (or grows the three parallel arrays in lockstep) and stamps its live generation
//    onto the token; RevokeToken advances the slot's generation FIRST so every outstanding copy of the old token becomes detectably stale.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_MICROUTILS_TOKENISSUER_H
#define FRONTIER_ENGINECONTEXT_MICROUTILS_TOKENISSUER_H

#include <cstddef>
#include <cstdint>

#include "EngineContext/MicroUtils/RecordToken.h"
#include "EngineContext/MicroUtils/TokenAuthentication.h"
#include "EngineContext/MicroUtils/VacancyTable.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Grow the three parallel arrays by exactly one slot, in lockstep, so Entries/Generations/Occupancy never diverge in length
//    and their (possible) reallocations coincide on a single call rather than at three independent thresholds. A first-time slot
//    begins at generation 1 (0 is the reserved null). Returns the new slot index. Callers that batch-load reserve up front so
//    this never reallocates per entry on a bulk spawn.
template <typename RecordStoreType>
uint32_t GrowOneSlot(RecordStoreType& Store)
{
    const uint32_t SlotIndex = static_cast<uint32_t>(Store.Entries.size());
    Store.Entries.emplace_back();
    Store.Generations.push_back(1u);
    Store.Occupancy.push_back(1u);
    return SlotIndex;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Acquire a slot for a new entry and return a live token naming it. A dormant slot is recycled when the VacancyTable holds one;
// otherwise the store grows by one. The returned entry is default-initialized and occupied — the caller (spawn path) fills its
// title, placement, and classification. Amortized O(1). Refuses (returns NullRecordToken) when the population ceiling is reached.
template <typename RecordStoreType>
[[nodiscard]] RecordToken IssueToken(RecordStoreType& Store, uint32_t MaximumEntryPopulation)
{
    uint32_t SlotIndex = 0u;

    if (AcquireVacancy(Store.Vacancies, SlotIndex))
    {
        // Recycle a dormant slot. Its generation was already advanced at revoke time, so it names a fresh identity.
        Store.Entries[SlotIndex]   = typename RecordStoreType::EntryType{};
        Store.Occupancy[SlotIndex] = 1u;
    }
    else
    {
        // No dormant slot — grow. Refuse before breaching the population ceiling rather than growing unbounded.
        if (static_cast<std::size_t>(Store.Entries.size()) >= static_cast<std::size_t>(MaximumEntryPopulation))
        {
            return NullRecordToken;
        }
        SlotIndex = GrowOneSlot(Store);
    }

    RecordToken Issued;
    Issued.SlotIndex  = SlotIndex;
    Issued.Generation = Store.Generations[SlotIndex];

    // Record its own token into the entry for self-reference convenience.
    Store.Entries[SlotIndex].SelfToken = Issued;
    return Issued;
}

// Release the slot a token names back to the dormant pool and advance its generation, so every outstanding token to that slot
// becomes detectably stale. No-op (returns false) when the token is already stale/invalid. O(1).
template <typename RecordStoreType>
bool RevokeToken(RecordStoreType& Store, const RecordToken& Token)
{
    if (!AuthenticateToken(Store, Token))
    {
        return false;
    }

    // ① Advance the generation FIRST — this is what makes every outstanding copy of the token detectably stale.
    ++Store.Generations[Token.SlotIndex];
    Store.Occupancy[Token.SlotIndex] = 0u;
    Store.Entries[Token.SlotIndex]   = typename RecordStoreType::EntryType{};

    // ② Deposit the now-dormant slot for reuse.
    ReclaimVacancy(Store.Vacancies, Token.SlotIndex);
    return true;
}

} // namespace Frontier

#endif
