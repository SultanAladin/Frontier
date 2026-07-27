/*==============================================================================================================================================
                                                          INSTRUMENTENCLOSUREPASS.H
==============================================================================================================================================*/
// 🧩 Hosts one instrument in its own ImGui window — a solid OLED-dark card (flat, never glass) with a title, a close affordance, and native drag/resize/dock — then draws the title, resolves the body rectangle, and constructs exactly one body pass on the instrument's classification. This is the seam between the record store and the five body passes: the enclosure owns the frame, the body passes own their visual form. One Construct call hosts one instrument; the extension loops it over every live record.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTENCLOSUREPASS_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTENCLOSUREPASS_H

#include "../Registry/InstrumentRecordEntry.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 What the enclosure reports back after hosting one instrument, so the extension can act on user intent without the pass
//    reaching into the store. CloseRequested fires when the card's close affordance was clicked this frame (the extension then
//    collapses the instrument into the inactive pill list). Trivial POD.
struct InstrumentEnclosureOutcome
{
    bool CloseRequested = false;   // [-] - the close affordance was clicked this frame
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Host one instrument as a solid dark card: push the OLED colours, open an ImGui window keyed to the record's slot (so
// position/size/dock persist across frames via the .ini), draw the title bar with a close 'x', resolve the inner body
// rectangle, and construct the matching body pass with the record's unrolled sample windows. Returns the outcome (close
// intent). The record supplies its own title/category/colours; ScratchSpan/ScratchCapacity back the range unrolling (must
// hold at least SignalRingCapacity floats). SlotIndex disambiguates the window id so two same-titled cards stay distinct.
InstrumentEnclosureOutcome ConstructInstrumentEnclosure(InstrumentRecordEntry& Record,
                                                        uint32_t               SlotIndex,
                                                        float*                 ScratchSpan,
                                                        uint32_t               ScratchCapacity);

}   // namespace Frontier

#endif
