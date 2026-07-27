/*==============================================================================================================================================
                                                       INSTRUMENTRECORDCLASSIFICATION.H
==============================================================================================================================================*/
// 🧩 The category tag that selects which projection pass draws an instrument's body. One value per SegmentUI telemetry panel from LiveTelemetryScene.html: the multi-metric render report, the live single-signal readout with sparkline, the threshold-coloured budget percent, the dual-line frame graph, and the stacked allocation bar. The enclosure pass draws the OLED frame and constructs the body on this.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTRECORDCLASSIFICATION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTRECORDCLASSIFICATION_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 The visual form of an instrument body, chosen at registration and constructed each paint by the enclosure pass. Each
//    value maps 1:1 to one projection pass AND to one panel in the LiveTelemetryScene mock. Backed by uint8 so a record
//    stays compact. Adding a form means adding a value here and a case in the enclosure body switch.
enum class InstrumentRecordClassification : uint8_t
{
    RenderReport   = 0,   // [-] - top-left panel: 2×2 metric grid (value+delta) over 3 dot-matrix rows (RenderReportPass)
    LiveReadout    = 1,   // [-] - top-right panel: huge number + trend arrow + min/max/avg + gradient sparkline (LiveReadoutPass)
    BudgetPercent  = 2,   // [-] - mid-right panel: threshold-coloured percent + caption line (BudgetPercentPass)
    FrameGraph     = 3,   // [-] - right panel: dual-line series over a dashed grid with date labels + legend (FrameGraphPass)
    AllocationBar  = 4    // [-] - bottom panel: weight-proportioned stacked segment bar + swatch legend (AllocationBarPass)
};

}   // namespace Frontier

#endif
