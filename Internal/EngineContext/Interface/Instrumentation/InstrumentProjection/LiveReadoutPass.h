/*==============================================================================================================================================
                                                             LIVEREADOUTPASS.H
==============================================================================================================================================*/
// 🧩 The top-right "Live FPS" panel: one signal shown huge, a trend arrow (latest vs the window average, green up / coral down), a "min · max · avg" sub-line, and a gradient-filled sparkline of the recent history below. Reads signal slot 0 only. One Construct call draws one readout body into the enclosure's body rectangle.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_LIVEREADOUTPASS_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_LIVEREADOUTPASS_H

#include "InstrumentBodyContext.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw the live-readout body: the big number (slot-0 latest, formatted to the presentation's decimals + unit), the trend
// arrow, the min/max/avg sub-line, and the under-filled sparkline. Appends to Context.DrawList only — no allocation, no
// render pass. A no-op-safe empty window renders zeros.
void ConstructLiveReadout(const InstrumentBodyContext& Context);

}   // namespace Frontier

#endif
