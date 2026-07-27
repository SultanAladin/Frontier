/*==============================================================================================================================================
                                                          INSTRUMENTRECORDARCHIVE.H
==============================================================================================================================================*/
// 🧩 The dense, fixed-capacity home for every live instrument: a contiguous array of records plus a vacancy stack, so add/remove happen at runtime in O(1) and the paint walk is one linear sweep over occupied slots. Add returns a stale-detecting token; resolve maps a token back to its record or null. No heap, no growth — the whole overlay's data lives in this one flat block.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTRECORDARCHIVE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTRECORDARCHIVE_H

#include "InstrumentRecordEntry.h"
#include "InstrumentToken.h"
#include "InstrumentVacancyTable.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A registration request — everything an app supplies to add one instrument. Title is copied (and truncated) into the
//    record's inline buffer; the rest are stored by value. Kept separate from the record so the caller fills a small stack
//    struct, not the full ring-carrying record. Sources[0..SignalCount) are the panel's pull sources in the order its body
//    pass expects (slot 0 = the primary / single value); Presentation.Payload carries the panel's static dressing. A simple
//    readout binds one source; the render report binds seven. Sources past SignalCount stay idle and accumulate nothing.
struct InstrumentRegistration
{
    const char*                    Title           = "";                                          // [-] - display title (copied, truncated)
    InstrumentRecordClassification Category        = InstrumentRecordClassification::LiveReadout;  // [-] - body pass selector
    InstrumentTileDescriptor       Presentation;                                                   // [-] - colours + unit/decimals + panel payload
    SignalIntakeProfile            Sources[InstrumentSignalCapacity];                              // [-] - pull sources, slot 0 = primary/single
    uint32_t                       SignalCount     = 0u;                                           // [-] - how many leading Sources are bound
};

// 📝 The store: Records is the dense slot array (a slot is live only while Occupied), LiveCount is the occupied tally, and
//    HighWaterMark is the highest slot ever appended — the paint walk sweeps 0..HighWaterMark and skips vacant slots, so it
//    never scans the whole capacity once only a few instruments exist. Vacancy recycles removed slots. Flat POD, no pointers.
struct InstrumentRecordArchive
{
    InstrumentRecordEntry  Records[InstrumentCapacity];   // [-] - dense record slots, occupied ones drawn each paint
    uint32_t               LiveCount      = 0u;           // [-] - number of currently occupied slots
    uint32_t               HighWaterMark  = 0u;           // [-] - highest appended slot + 1 (walk upper bound)
    InstrumentVacancyTable Vacancy;                       // [-] - recycled slot indices
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Clear the store to empty (every slot vacant, vacancy stack cleared). Constant-time; frees nothing.
void ResetInstrumentRecordArchive(InstrumentRecordArchive& Store);

// Add one instrument, reusing a recycled slot if one exists else appending. Copies the registration into the record, resets
// its sample rings, bumps the slot's generation, and returns a token addressing it. Returns a vacant token when the store is
// full (LiveCount == InstrumentCapacity) — the caller must check with VacantToken.
InstrumentToken RegisterInstrument(InstrumentRecordArchive& Store, const InstrumentRegistration& Registration);

// Vacate the slot a token addresses: mark it unoccupied, bump its generation so the token (and any copy) goes stale, and
// push the slot to the vacancy stack for reuse. A no-op for a stale or vacant token. The record memory stays put.
void WithdrawInstrument(InstrumentRecordArchive& Store, InstrumentToken Token);

// Resolve a token to its live record, or null when the token is vacant/stale/out of range. The returned pointer is valid
// only until the next Register/Withdraw (which may recycle the slot) — resolve fresh each paint, never cache across edits.
InstrumentRecordEntry* ResolveInstrument(InstrumentRecordArchive& Store, InstrumentToken Token);

}   // namespace Frontier

#endif
