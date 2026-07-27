/*==============================================================================================================================================
                                                        INSTRUMENTTILEDESCRIPTOR.H
==============================================================================================================================================*/
// 🧩 The per-instrument look AND the static per-panel payload: the OLED-dark enclosure colours plus everything a composite panel needs that is NOT a live signal — the metric-grid cell labels/tints/units, the dot-matrix row names + capacities, the budget caption, the frame-graph axis labels + legend, and the allocation-bar segment names + colours. Solid opaque panels, flat fills — no glass, no blur. One of these is stored per instrument so an app configures each card independently, and every field is inline POD so a record stays a flat heap-free block.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTTILEDESCRIPTOR_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTTILEDESCRIPTOR_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The worst-case entry counts across every panel form, so the static payload's inline arrays are sized once and never
//    grow. RenderReport reads the most: 4 metric cells + 3 dot-matrix rows. AllocationBar reads 6 segments. FrameGraph
//    carries 7 date labels. A panel uses only the leading N entries it needs; the rest stay zeroed and unread.
static const uint32_t PanelMetricCapacity  = 4u;    // [-] - metric-grid cells in RenderReport
static const uint32_t PanelMatrixCapacity  = 3u;    // [-] - dot-matrix rows in RenderReport
static const uint32_t PanelSegmentCapacity = 6u;    // [-] - stacked segments in AllocationBar (also its legend rows)
static const uint32_t PanelAxisCapacity    = 7u;    // [-] - date labels along the FrameGraph x-axis
static const uint32_t PanelLabelCapacity   = 16u;   // [-] - inline chars per short label (cell name, segment name, date)
static const uint32_t PanelCaptionCapacity = 128u;  // [-] - inline chars for the budget caption line

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A packed colour carried as four bytes so the specification stays a flat POD and an app sets colours without pulling in
//    ImGui's ImU32 packing. The projection passes convert to ImGui's native packing once at draw time. Alpha 255 = fully
//    opaque (the default) — the panels are solid, never translucent.
struct InstrumentColour
{
    uint8_t Red    = 255;   // [-] - red channel
    uint8_t Green  = 255;   // [-] - green channel
    uint8_t Blue   = 255;   // [-] - blue channel
    uint8_t Alpha  = 255;   // [-] - opacity (255 = solid; the enclosure is never see-through)
};

// 📝 One metric-grid cell in the RenderReport: a coloured status dot, a short label ("Frame","GPU","Draws","Rays"), the unit
//    suffix drawn small after the big value ("ms","%","M",""), and the decimal count the value formats with. The live figure
//    comes from the matching signal ring; this is only the cell's static dressing.
struct PanelMetricCell
{
    InstrumentColour DotTint                          = { 61, 220, 132, 255 }; // [-] - the cell's status dot
    char             Label[PanelLabelCapacity]        = { 0 };                 // [-] - cell name (inline, truncated)
    char             UnitSuffix[8]                    = { 0 };                  // [-] - small unit after the value
    uint8_t          DecimalPlaces                    = 1u;                     // [-] - fractional digits of the value
};

// 📝 One dot-matrix row in the RenderReport's Memory & I/O section: a row name ("VRAM","RAM","Disk I/O"), the lit-cell tint,
//    and the capacity the live signal is divided by to get the 0..1 fill the matrix lights to. Capacity 0 falls back to the
//    row's own running maximum so a fill still animates.
struct PanelMatrixRow
{
    InstrumentColour LitTint                          = { 61, 220, 132, 255 }; // [-] - colour of a lit cell
    char             Label[PanelLabelCapacity]        = { 0 };                 // [-] - row name (inline, truncated)
    float            Capacity                         = 0.0f;                   // [signal] - denominator for the 0..1 fill (0 = auto)
    char             UnitSuffix[8]                    = { 0 };                  // [-] - amount unit shown at the row's right ("MiB","MB/s")
};

// 📝 One stacked segment in the AllocationBar: a name and a colour. The segment's proportional width is its live signal's
//    latest value taken as a weight against the sum of all segments (exactly the HTML's flex-grow model).
struct PanelSegment
{
    InstrumentColour Tint                             = { 61, 220, 132, 255 }; // [-] - segment + legend swatch colour
    char             Label[PanelLabelCapacity]        = { 0 };                 // [-] - segment name (inline, truncated)
};

// 📝 The static payload for whichever panel this instrument is — a flat superset. A form reads only the members it needs:
//    RenderReport reads MetricCells + MatrixRows; AllocationBar reads Segments; FrameGraph reads AxisLabels + the two legend
//    names; BudgetPercent reads Caption; LiveReadout reads none of these (its dressing is the presentation accents + unit).
//    Every array is fixed inline, so the whole payload is POD with no heap.
struct PanelStaticPayload
{
    PanelMetricCell MetricCells[PanelMetricCapacity];              // [-] - RenderReport metric-grid cells
    uint32_t        MetricCount                       = 0u;        // [-] - active metric cells (≤ PanelMetricCapacity)

    PanelMatrixRow  MatrixRows[PanelMatrixCapacity];              // [-] - RenderReport dot-matrix rows
    uint32_t        MatrixCount                        = 0u;       // [-] - active matrix rows (≤ PanelMatrixCapacity)

    PanelSegment    Segments[PanelSegmentCapacity];               // [-] - AllocationBar stacked segments
    uint32_t        SegmentCount                       = 0u;       // [-] - active segments (≤ PanelSegmentCapacity)

    char            AxisLabels[PanelAxisCapacity][PanelLabelCapacity] = { { 0 } }; // [-] - FrameGraph x-axis date labels
    uint32_t        AxisCount                          = 0u;       // [-] - active axis labels (≤ PanelAxisCapacity)
    char            PrimaryLegend[PanelLabelCapacity]  = { 0 };    // [-] - FrameGraph front (primary) series legend name
    char            SecondaryLegend[PanelLabelCapacity]= { 0 };    // [-] - FrameGraph back (secondary) series legend name

    char            Caption[PanelCaptionCapacity]      = { 0 };    // [-] - BudgetPercent caption sentence
    char            Header[PanelLabelCapacity + 24]    = { 0 };    // [-] - small header line above a value (FrameGraph "· 8s window")
};

// 📝 Everything cosmetic about one instrument, resolved once per paint. EnclosureFill is the solid card body (OLED near-black
//    by default, the HTML #0a0a0c); EnclosureBorder is the hairline frame (#161618); TitleTint is the drag-bar label;
//    AccentPrimary/AccentSecondary tint the live data (a dual-line uses both, single-signal forms use only Primary).
//    UnitSuffix + DecimalPlaces format a single-value readout ("60"/"16.6ms"). Payload carries the per-panel static dressing.
//    No pointers, no ownership.
struct InstrumentTileDescriptor
{
    InstrumentColour EnclosureFill      = { 10, 10, 12, 255 };    // [-] - solid card body (HTML #0a0a0c)
    InstrumentColour EnclosureBorder    = { 22, 22, 24, 255 };    // [-] - hairline frame (HTML #161618)
    InstrumentColour TitleTint          = { 108, 108, 116, 255 }; // [-] - drag-bar title text (HTML --muted)
    InstrumentColour AccentPrimary      = { 90, 108, 245, 255 };  // [-] - front trace / primary fill (HTML --blue #5a6cf5)
    InstrumentColour AccentSecondary    = { 138, 138, 146, 255 }; // [-] - back trace / secondary fill (HTML #8a8a92)

    char             UnitSuffix[8]      = { 0 };                  // [-] - static unit label for a single readout ("ms","fps")
    uint8_t          DecimalPlaces      = 1;                      // [-] - fractional digits a single readout formats with

    PanelStaticPayload Payload;                                   // [-] - per-panel static dressing (labels, caps, legend, …)
};

}   // namespace Frontier

#endif
