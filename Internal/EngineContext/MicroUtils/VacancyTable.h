/*==============================================================================================================================================
                                                               VACANCYTABLE.H
==============================================================================================================================================*/
// 🧩 Canonical LIFO of freed slot indices so any dense record store recycles vacated slots instead of growing forever: reclaim pushes a slot
//    back, acquire pops one (or reports empty so the store appends). Fixed inline capacity — no heap — parameterized by the owning store's
//    ceiling so Scene, Revision, and Instrumentation share one implementation instead of forking a prefixed copy each.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_MICROUTILS_VACANCYTABLE_H
#define FRONTIER_ENGINECONTEXT_MICROUTILS_VACANCYTABLE_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A bare LIFO of freed indices sized to the owning store's Capacity. FreedSlots holds the recycled slot numbers; FreedCount
//    is the live depth. When empty, the store has no hole to reuse and appends a fresh slot instead. Trivial POD; no allocation
//    ever. Capacity is the caller's fixed ceiling (Instrumentation passes 64, Scene/Revision pass their own) so the table stays
//    an inline array with no growth path while remaining a single shared component.
template <uint32_t Capacity>
struct VacancyTable
{
    static_assert(Capacity > 0u, "VacancyTable Capacity must be non-zero");

    uint32_t FreedSlots[Capacity] = { 0u };   // [-] - recycled slot indices, most-recently-freed on top
    uint32_t FreedCount           = 0u;       // [-] - number of slots currently available for reuse
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Clear every recycled slot, returning the table to empty. Used when the whole store is reset.
template <uint32_t Capacity>
void ResetVacancyTable(VacancyTable<Capacity>& Table) noexcept
{
    Table.FreedCount = 0u;
}

// Push a freed slot index back for reuse. Guards the fixed ceiling: a correct store never overflows it, but the guard keeps a
// double-reclaim from scribbling past the array.
template <uint32_t Capacity>
void ReclaimVacancy(VacancyTable<Capacity>& Table, uint32_t SlotIndex) noexcept
{
    if (Table.FreedCount < Capacity)
    {
        Table.FreedSlots[Table.FreedCount] = SlotIndex;
        Table.FreedCount = Table.FreedCount + 1u;
    }
}

// Pop the most-recently-freed slot into OutSlotIndex and return true; return false (leaving OutSlotIndex untouched) when no
// slot is available, signalling the store to append a fresh one.
template <uint32_t Capacity>
[[nodiscard]] bool AcquireVacancy(VacancyTable<Capacity>& Table, uint32_t& OutSlotIndex) noexcept
{
    if (Table.FreedCount == 0u)
    {
        return false;
    }

    Table.FreedCount = Table.FreedCount - 1u;
    OutSlotIndex = Table.FreedSlots[Table.FreedCount];
    return true;
}

} // namespace Frontier

#endif
