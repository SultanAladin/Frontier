/*==============================================================================================================================================
                                                             FRAMEGRAPHPASS.H
==============================================================================================================================================*/
// 🧩 The right "Frame Graph" panel: a header line + big current value, then a dual-line time series (secondary trace grey behind, primary trace blue in front with a head dot) drawn over a dashed 4-row grid with x-axis date labels, closed by a two-entry legend. Both traces share one vertical scale (min/max across both). Reads signal slot 0 (primary series) + slot 1 (secondary series); Payload supplies the header, axis labels, and the two legend names. One Construct call draws one graph body.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_FRAMEGRAPHPASS_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_FRAMEGRAPHPASS_H

#include "InstrumentBodyContext.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw the dual-line graph body: header + big value, dashed grid, date labels, both traces on a shared scale, head dot, and
// legend. Appends to Context.DrawList only.
void ConstructFrameGraph(const InstrumentBodyContext& Context);

}   // namespace Frontier

#endif
