/*==============================================================================================================================================
                                                          INSTRUMENTVACANCYTABLE.H
==============================================================================================================================================*/
// 🧩 A last-in-first-out stack of freed slot indices so the record store recycles vacated slots instead of growing forever: reclaim pushes a slot back, acquire pops one (or reports empty so the store appends). Fixed capacity, inline storage — no heap, matching the float-free store it serves.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTVACANCYTABLE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTVACANCYTABLE_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The hard ceiling on live instruments — the store, the vacancy stack, and the alignment list all size to this. 64 is far
//    more cards than any overlay shows at once; fixed so every container is an inline array with no growth path.
static const uint32_t InstrumentCapacity = 64u;

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A bare LIFO of freed indices. FreedSlots holds the recycled slot numbers; FreedCount is the live depth. When empty, the
//    store has no hole to reuse and appends a fresh slot instead. Trivial POD; no allocation ever.
struct InstrumentVacancyTable
{
    uint32_t FreedSlots[InstrumentCapacity] = { 0u };   // [-] - recycled slot indices, most-recently-freed on top
    uint32_t FreedCount                     = 0u;       // [-] - number of slots currently available for reuse
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Clear every recycled slot, returning the table to empty. Used when the whole store is reset.
void ResetVacancyTable(InstrumentVacancyTable& Table);

// Push a freed slot index back for reuse. Ignored if the table is somehow full (it never can be past InstrumentCapacity).
void ReclaimVacancy(InstrumentVacancyTable& Table, uint32_t SlotIndex);

// Pop the most-recently-freed slot into OutSlotIndex and return true; return false (leaving OutSlotIndex untouched) when no
// slot is available, signalling the store to append a fresh one.
bool AcquireVacancy(InstrumentVacancyTable& Table, uint32_t& OutSlotIndex);

}   // namespace Frontier

#endif
