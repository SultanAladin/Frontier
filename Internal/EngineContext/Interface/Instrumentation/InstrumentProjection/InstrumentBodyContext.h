/*==============================================================================================================================================
                                                          INSTRUMENTBODYCONTEXT.H
==============================================================================================================================================*/
// 🧩 The uniform record-time payload every projection pass consumes: the pixel rectangle to draw the body into, the draw list to draw onto, the resolved presentation (accents + panel static payload), and the sample window(s) already unrolled from the accumulators — one window per bound signal slot. One of these is filled per instrument per paint by the enclosure pass, then handed to exactly one body pass, so all passes share a single call shape and the enclosure constructs on classification alone.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTBODYCONTEXT_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTBODYCONTEXT_H

#include "../Classification/InstrumentTileDescriptor.h"
#include "../Registry/InstrumentRecordEntry.h"
#include "../SignalAccumulation/SignalEvaluationInterval.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Everything a body pass needs and nothing it owns. DrawList is the ImGui draw list for the hosting window (the pass
//    appends primitives to it — no pipeline, no barrier, no extra render pass, so no GPU stall). BodyMinimum/BodyMaximum are
//    the body's pixel rectangle (title bar already excluded by the enclosure). Ranges[0..RangeCount) are the unrolled sample
//    windows, one per the record's bound signal slots, in registration order (slot 0 first). Presentation carries the accent
//    colours + the panel static payload (cell labels, segment names, caption, …). Passed by const-ref; the pass reads and
//    draws, retaining nothing.
struct InstrumentBodyContext
{
    ImDrawList*                     DrawList        = nullptr;   // [-] - hosting window's draw list (primitives appended here)
    ImVec2                          BodyMinimum;                 // [px] - body rect top-left (title bar excluded)
    ImVec2                          BodyMaximum;                 // [px] - body rect bottom-right
    SignalEvaluationInterval        Ranges[InstrumentSignalCapacity]; // [-] - one unrolled window per bound signal slot
    uint32_t                        RangeCount      = 0u;        // [-] - number of populated windows (== record SignalCount)
    const InstrumentTileDescriptor* Presentation    = nullptr;   // [-] - resolved accents + panel static payload (borrowed)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Pack an InstrumentColour into ImGui's native ImU32. The one place the four-byte POD colour meets ImGui's packing, so every
// pass tints identically. Inlined — a trivial byte shuffle.
inline ImU32 ResolveInstrumentColour(const InstrumentColour& Colour)
{
    return IM_COL32(Colour.Red, Colour.Green, Colour.Blue, Colour.Alpha);
}

// Blend an InstrumentColour toward transparent at the given 0..1 alpha, packed to ImU32 — for the muted grid lines, under-fill
// gradients, and unlit dots the SegmentUI panels draw at reduced opacity over the OLED body.
inline ImU32 ResolveInstrumentColourAlpha(const InstrumentColour& Colour, float Alpha)
{
    if (Alpha < 0.0f) { Alpha = 0.0f; }
    if (Alpha > 1.0f) { Alpha = 1.0f; }
    return IM_COL32(Colour.Red, Colour.Green, Colour.Blue, static_cast<int>(Alpha * 255.0f));
}

// The window at slot Index, or an empty window when the slot is unbound — so a body pass reads its expected slots without
// bounds-checking RangeCount at every use.
inline const SignalEvaluationInterval& ResolveBodyRange(const InstrumentBodyContext& Context, uint32_t Index)
{
    static const SignalEvaluationInterval EmptyRange;
    return (Index < Context.RangeCount) ? Context.Ranges[Index] : EmptyRange;
}

}   // namespace Frontier

#endif
