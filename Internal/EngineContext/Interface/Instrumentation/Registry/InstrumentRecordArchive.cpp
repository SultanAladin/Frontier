/*==============================================================================================================================================
                                                          INSTRUMENTRECORDARCHIVE.CPP
==============================================================================================================================================*/
// 🧩 The dense instrument store: O(1) register (recycle-or-append) / withdraw (vacate + bump generation) / resolve (token → record)

#include "InstrumentRecordArchive.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Copy a C string into a fixed inline buffer, always NUL-terminating and truncating anything past Capacity-1. Keeps the
    //    record POD (no std::string) and defends against an over-long title scribbling past the buffer.
    void CopyTitleInline(char* Destination, uint32_t Capacity, const char* Source)
    {
        if (Source == nullptr)
        {
            Destination[0] = '\0';
            return;
        }

        uint32_t Index = 0u;
        while (Index + 1u < Capacity && Source[Index] != '\0')
        {
            Destination[Index] = Source[Index];
            ++Index;
        }
        Destination[Index] = '\0';
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Return the store to empty. Every slot is marked vacant; the generations are left rising (never reset) so a token minted
// before the reset can never accidentally match a slot again.
void ResetInstrumentRecordArchive(InstrumentRecordArchive& Store)
{
    for (uint32_t Slot = 0u; Slot < InstrumentCapacity; ++Slot)
    {
        Store.Records[Slot].Occupied = false;
    }
    Store.LiveCount     = 0u;
    Store.HighWaterMark = 0u;
    ResetVacancyTable(Store.Vacancy);
}

// Add one instrument. Prefer a recycled slot; otherwise append at the high-water mark. The chosen slot's generation was
// already advanced at withdraw time (recycled) or starts at 0 (fresh), so the returned token is unique to this occupancy.
InstrumentToken RegisterInstrument(InstrumentRecordArchive& Store, const InstrumentRegistration& Registration)
{
    InstrumentToken Token;   // 📝 Vacant by default — returned as-is when the store is full.

    if (Store.LiveCount >= InstrumentCapacity)
    {
        return Token;
    }

    uint32_t Slot = 0u;
    if (!AcquireVacancy(Store.Vacancy, Slot))
    {
        Slot = Store.HighWaterMark;
        Store.HighWaterMark = Store.HighWaterMark + 1u;
    }

    InstrumentRecordEntry& Record = Store.Records[Slot];

    CopyTitleInline(Record.Title, InstrumentTitleCapacity, Registration.Title);
    Record.Category     = Registration.Category;
    Record.Presentation = Registration.Presentation;

    // 📝 Bind every leading source the app supplied (clamped to the slot bank) and clear its ring; leave the rest idle.
    uint32_t Bound = Registration.SignalCount;
    if (Bound > InstrumentSignalCapacity)
    {
        Bound = InstrumentSignalCapacity;
    }
    Record.SignalCount = Bound;
    for (uint32_t Signal = 0u; Signal < InstrumentSignalCapacity; ++Signal)
    {
        Record.Ingestion[Signal] = (Signal < Bound) ? Registration.Sources[Signal] : SignalIntakeProfile();
        ResetSignalAccumulator(Record.History[Signal]);
    }

    Record.Collapsed    = false;
    Record.Occupied     = true;

    Store.LiveCount = Store.LiveCount + 1u;

    Token.SlotIndex  = Slot;
    Token.Generation = Record.Generation;
    return Token;
}

// Vacate a slot. The generation bump is what invalidates every outstanding token to this slot — the next occupant gets a
// higher generation, so a stale token's match fails in ResolveInstrument.
void WithdrawInstrument(InstrumentRecordArchive& Store, InstrumentToken Token)
{
    if (VacantToken(Token) || Token.SlotIndex >= InstrumentCapacity)
    {
        return;
    }

    InstrumentRecordEntry& Record = Store.Records[Token.SlotIndex];
    if (!Record.Occupied || Record.Generation != Token.Generation)
    {
        return;   // 📝 Already withdrawn, or the token is stale — nothing to do.
    }

    Record.Occupied   = false;
    Record.Generation = Record.Generation + 1u;
    Store.LiveCount   = Store.LiveCount - 1u;
    ReclaimVacancy(Store.Vacancy, Token.SlotIndex);
}

// Map a token to its record, or null. Guards range, occupancy, and generation so a stale/vacant token never returns a live
// record. The pointer is transient — valid until the next structural edit.
InstrumentRecordEntry* ResolveInstrument(InstrumentRecordArchive& Store, InstrumentToken Token)
{
    if (VacantToken(Token) || Token.SlotIndex >= InstrumentCapacity)
    {
        return nullptr;
    }

    InstrumentRecordEntry& Record = Store.Records[Token.SlotIndex];
    if (!Record.Occupied || Record.Generation != Token.Generation)
    {
        return nullptr;
    }
    return &Record;
}

}   // namespace Frontier
