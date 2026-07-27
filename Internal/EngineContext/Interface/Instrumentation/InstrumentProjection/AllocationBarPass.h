/*==============================================================================================================================================
                                                            ALLOCATIONBARPASS.H
==============================================================================================================================================*/
// 🧩 The bottom "Memory Allocation" panel: a horizontal stacked bar whose segments are proportioned by weight (each segment's live value against the sum of all), over a swatch legend showing each segment's name + value. Reads one signal slot per segment (in Payload.Segments order) and Payload.Segments[] for the names + colours. One Construct call draws one allocation body.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_ALLOCATIONBARPASS_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_ALLOCATIONBARPASS_H

#include "InstrumentBodyContext.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw the stacked bar + legend. Segment widths are proportional to their latest values; the legend lists each name + value.
// Appends to Context.DrawList only.
void ConstructAllocationBar(const InstrumentBodyContext& Context);

}   // namespace Frontier

#endif
