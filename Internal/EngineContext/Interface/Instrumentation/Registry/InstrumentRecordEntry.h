/*==============================================================================================================================================
                                                          INSTRUMENTRECORDENTRY.H
==============================================================================================================================================*/
// 🧩 One instrument, whole: its title, its category (which projection pass draws it), its look + per-panel static payload, its pull sources, and its inline sample rings. This is the record the dense store holds by value — a flat POD block with no pointers into the heap, so the store is a contiguous array the overlay walks each paint with zero indirection. A composite panel reads several signals at once (the render report reads seven), so the record carries a fixed bank of signal slots; a simple panel uses only the leading one or two.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTRECORDENTRY_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTRECORDENTRY_H

#include "InstrumentToken.h"
#include "../Classification/InstrumentRecordClassification.h"
#include "../Classification/InstrumentTileDescriptor.h"
#include "../SignalAccumulation/SignalIntakeProfile.h"
#include "../SignalAccumulation/MetricSignalAccumulator.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The signal-slot bank per record. The busiest panel — the RenderReport — reads seven live figures at once (four metric
//    cells + three dot-matrix rows); the AllocationBar reads six; the FrameGraph reads two; the simple readouts read one.
//    Eight covers them all with a slot to spare, without a variant type — a panel binds only the leading slots it needs and
//    leaves the rest idle. Eight rings per record keeps the record sizeable but still a flat inline POD.
static const uint32_t InstrumentSignalCapacity = 8u;

// 📝 Title storage is inline and fixed so a record stays POD (no std::string, no allocation). Long enough for the SegmentUI
//    card titles ("Memory Allocation", "Render Report") with room to spare; a longer title is truncated at registration.
static const uint32_t InstrumentTitleCapacity = 48u;

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The full instrument record. Occupied + Generation are the store's bookkeeping (a vacant slot keeps its record memory but
//    is skipped by the walk; Generation rises each reuse so stale tokens fail to resolve). Title is inline display text.
//    Category selects the body pass; Presentation tints it AND carries the panel's static dressing. Ingestion[i]/History[i]
//    are the i-th pull source and its ring — the panel form decides which slots mean what (slot 0 is always the primary /
//    single value). SignalCount records how many leading slots the app bound so the accumulation step pulls exactly those.
//    Collapsed drives the dormant linearizer (a collapsed instrument flattens into the bottom-left pill list).
struct InstrumentRecordEntry
{
    bool                          Occupied                      = false;   // [-] - false = vacant slot, skipped by the walk
    uint32_t                      Generation                    = 0u;      // [-] - reuse count; matched against a token

    char                          Title[InstrumentTitleCapacity] = { 0 };  // [-] - inline display title (truncated to fit)
    InstrumentRecordClassification Category = InstrumentRecordClassification::LiveReadout; // [-] - body pass selector
    InstrumentTileDescriptor      Presentation;                            // [-] - solid OLED-dark colours + panel payload

    SignalIntakeProfile           Ingestion[InstrumentSignalCapacity];     // [-] - pull sources; slot 0 = primary/single value
    MetricSignalAccumulator       History[InstrumentSignalCapacity];       // [-] - inline sample rings, one per bound slot
    uint32_t                      SignalCount                   = 0u;      // [-] - how many leading slots are bound (≤ capacity)

    bool                          Collapsed                     = false;   // [-] - true = flattened into the dormant pill list
};

}   // namespace Frontier

#endif
