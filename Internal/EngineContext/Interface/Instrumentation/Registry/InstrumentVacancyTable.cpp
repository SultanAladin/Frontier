/*==============================================================================================================================================
                                                          INSTRUMENTVACANCYTABLE.CPP
==============================================================================================================================================*/
// 🧩 The LIFO of freed slot indices: reset, reclaim (push), acquire (pop) — the store's slot recycler

#include "InstrumentVacancyTable.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Empty the stack. Nothing to free — only the depth resets.
void ResetVacancyTable(InstrumentVacancyTable& Table)
{
    Table.FreedCount = 0u;
}

// Push a slot back onto the recycle stack, guarding the fixed ceiling (a correct store never overflows this, but the guard
// keeps a double-reclaim from scribbling past the array).
void ReclaimVacancy(InstrumentVacancyTable& Table, uint32_t SlotIndex)
{
    if (Table.FreedCount < InstrumentCapacity)
    {
        Table.FreedSlots[Table.FreedCount] = SlotIndex;
        Table.FreedCount = Table.FreedCount + 1u;
    }
}

// Pop the top recycled slot. Returns false when the stack is empty so the caller appends a fresh slot instead.
bool AcquireVacancy(InstrumentVacancyTable& Table, uint32_t& OutSlotIndex)
{
    if (Table.FreedCount == 0u)
    {
        return false;
    }

    Table.FreedCount = Table.FreedCount - 1u;
    OutSlotIndex = Table.FreedSlots[Table.FreedCount];
    return true;
}

}   // namespace Frontier
