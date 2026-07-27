/*==============================================================================================================================================
                                                        METRICSIGNALACCUMULATOR.H
==============================================================================================================================================*/
// 🧩 A fixed-capacity contiguous float ring for one instrument's sample history: a plain inline array plus a head index, appended to each paint and read back as an oldest→newest window. No heap, no growth, no per-frame allocation — the ring lives inside the instrument record, so the whole overlay stays float-free (it never touches the allocator during a frame).

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_METRICSIGNALACCUMULATOR_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_METRICSIGNALACCUMULATOR_H

#include "SignalEvaluationInterval.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The ring depth — how many samples of history one instrument retains. 128 covers every projection form (a sparkline, a
//    dual-trend graph, a dot-matrix fill) at a comfortable width without the record growing large. Compile-time fixed so the
//    ring is an inline array; a record store of N instruments is exactly N of these back to back, zero indirection.
static const uint32_t SignalRingCapacity = 128u;

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The ring itself: Samples is the raw storage, WriteHead is the next slot to overwrite, and FilledCount saturates at
//    SignalRingCapacity once the ring wraps (so a young instrument reports only its real samples, not stale zeros). All
//    inline — trivially copyable and default-zeroed. RetrieveEvaluationRange unrolls it into a caller's scratch span.
struct MetricSignalAccumulator
{
    float    Samples[SignalRingCapacity] = { 0.0f };   // [signal] - raw ring storage, indexed modulo capacity
    uint32_t WriteHead                   = 0u;         // [-] - next slot AppendSignalSample overwrites
    uint32_t FilledCount                 = 0u;         // [-] - valid samples so far (saturates at SignalRingCapacity)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Drop every retained sample, returning the accumulator to its just-constructed state without freeing anything (there is
// nothing to free). Used when an instrument is re-registered onto a recycled slot.
void ResetSignalAccumulator(MetricSignalAccumulator& Accumulator);

// Append one sample to the ring, advancing the head and saturating the fill count. O(1), no allocation — this is the whole
// per-paint write. The oldest sample is overwritten once the ring is full.
void AppendSignalSample(MetricSignalAccumulator& Accumulator, float Sample);

// Unroll the ring into ScratchSpan in oldest→newest order and return a borrowed window over it (with min/max/latest filled).
// ScratchCapacity caps how many samples are copied; the returned Range borrows ScratchSpan, so it stays valid only as long
// as the caller's scratch does. A projection pass calls this once, then reads the window as a plain forward array.
SignalEvaluationInterval RetrieveEvaluationRange(const MetricSignalAccumulator& Accumulator,
                                                 float*                         ScratchSpan,
                                                 uint32_t                       ScratchCapacity);

}   // namespace Frontier

#endif
