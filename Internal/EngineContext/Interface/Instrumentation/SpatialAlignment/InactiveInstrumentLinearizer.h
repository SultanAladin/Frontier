/*==============================================================================================================================================
                                                      INACTIVEINSTRUMENTLINEARIZER.H
==============================================================================================================================================*/
// 🧩 Flattens every collapsed (closed) instrument into a single 1D row of rounded pills docked at the bottom-left of the viewport, mirroring the HTML pill tray: a closed card becomes a little pill titled with its name, and clicking the pill re-opens the card. One borderless ImGui window holds the whole row; the linearizer reports which slot was clicked so the extension can un-collapse it.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INACTIVEINSTRUMENTLINEARIZER_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INACTIVEINSTRUMENTLINEARIZER_H

#include "../Registry/InstrumentRecordArchive.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 What the linearizer reports after laying out the pill row: the slot the user clicked to re-open, or a sentinel when
//    nothing was clicked. The extension reads ReopenRequested + ReopenSlotIndex and un-collapses that record. Trivial POD.
struct InactiveLinearizerOutcome
{
    bool     ReopenRequested = false;         // [-] - a pill was clicked this frame
    uint32_t ReopenSlotIndex = 0xFFFFFFFFu;   // [-] - the clicked pill's store slot (valid only when ReopenRequested)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Lay every collapsed instrument in the store into a bottom-left row of pills and report any click. Walks the store's
// occupied slots, packs the collapsed ones left-to-right into one borderless window pinned to the lower-left, draws each as a
// rounded solid pill with its title, and returns the slot of a clicked pill. A no-op row (no pills) still returns a clean
// outcome. ViewportHeight positions the row above the viewport's bottom edge.
InactiveLinearizerOutcome ConstructInactiveInstrumentRow(InstrumentRecordArchive& Store, float ViewportHeight);

}   // namespace Frontier

#endif
