/*==============================================================================================================================================
                                                            RENDERREPORTPASS.H
==============================================================================================================================================*/
// 🧩 The top-left "Render Report" panel: a 2×2 metric grid (each cell a status dot + label + big mono value + a delta line that arrows up/down against the previous sample) above a "Memory & I/O" block of dot-matrix rows (each a name + amount line over a 4-row lit-cell field whose fill is the signal against its capacity). The busiest panel — it reads several signals at once: the leading Payload.MetricCount slots feed the metric cells, the next Payload.MatrixCount slots feed the matrix rows. One Construct call draws one report body.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_RENDERREPORTPASS_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_RENDERREPORTPASS_H

#include "InstrumentBodyContext.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw the report body: the metric grid then the dot-matrix rows. Metric cells read signal slots [0, MetricCount); matrix
// rows read slots [MetricCount, MetricCount + MatrixCount). Appends to Context.DrawList only.
void ConstructRenderReport(const InstrumentBodyContext& Context);

}   // namespace Frontier

#endif
