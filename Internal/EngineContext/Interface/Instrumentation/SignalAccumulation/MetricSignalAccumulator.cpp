/*==============================================================================================================================================
                                                        METRICSIGNALACCUMULATOR.CPP
==============================================================================================================================================*/
// 🧩 The fixed-capacity float ring: append in O(1), unroll into a caller's scratch as an oldest→newest window with min/max/latest

#include "MetricSignalAccumulator.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Clear the ring back to empty. Nothing is freed — the inline array stays; only the bookkeeping resets so the next append
// begins a fresh history. Samples are left as-is (they are unreachable while FilledCount is 0).
void ResetSignalAccumulator(MetricSignalAccumulator& Accumulator)
{
    Accumulator.WriteHead   = 0u;
    Accumulator.FilledCount = 0u;
}

// Write one sample at the head, advance modulo capacity, and grow the fill count until the ring is saturated. The oldest
// sample is silently overwritten once full — that is the ring's whole point.
void AppendSignalSample(MetricSignalAccumulator& Accumulator, float Sample)
{
    Accumulator.Samples[Accumulator.WriteHead] = Sample;
    Accumulator.WriteHead = (Accumulator.WriteHead + 1u) % SignalRingCapacity;

    if (Accumulator.FilledCount < SignalRingCapacity)
    {
        Accumulator.FilledCount = Accumulator.FilledCount + 1u;
    }
}

// Copy the valid samples into ScratchSpan in oldest→newest order and return a window over that scratch. The oldest live
// sample sits FilledCount slots behind the head; walking forward from there (wrapping once) yields chronological order.
// Min/max/latest are gathered in the same single pass so a projection pass needs no second walk.
SignalEvaluationInterval RetrieveEvaluationRange(const MetricSignalAccumulator& Accumulator,
                                                 float*                         ScratchSpan,
                                                 uint32_t                       ScratchCapacity)
{
    SignalEvaluationInterval Range;

    // 📝 Nothing accumulated yet, or no room to unroll into — return the empty window (pointer null, count zero).
    if (Accumulator.FilledCount == 0u || ScratchSpan == nullptr || ScratchCapacity == 0u)
    {
        return Range;
    }

    uint32_t Available = Accumulator.FilledCount;
    if (Available > ScratchCapacity)
    {
        Available = ScratchCapacity;   // 📝 Caller's scratch caps how many of the newest samples we surface.
    }

    // 📝 Start index = the position `Available` samples behind the head, wrapped into the ring.
    uint32_t Start = (Accumulator.WriteHead + SignalRingCapacity - Available) % SignalRingCapacity;

    float Minimum = Accumulator.Samples[Start];
    float Maximum = Accumulator.Samples[Start];

    for (uint32_t Offset = 0u; Offset < Available; ++Offset)
    {
        float Value = Accumulator.Samples[(Start + Offset) % SignalRingCapacity];
        ScratchSpan[Offset] = Value;

        if (Value < Minimum) { Minimum = Value; }
        if (Value > Maximum) { Maximum = Value; }
    }

    Range.Samples = ScratchSpan;
    Range.Count   = Available;
    Range.Minimum = Minimum;
    Range.Maximum = Maximum;
    Range.Latest  = ScratchSpan[Available - 1u];
    return Range;
}

}   // namespace Frontier
