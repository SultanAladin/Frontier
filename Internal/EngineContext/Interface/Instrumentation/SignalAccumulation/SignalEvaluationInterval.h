/*==============================================================================================================================================
                                                        SIGNALEVALUATIONINTERVAL.H
==============================================================================================================================================*/
// 🧩 A read-only window over a signal accumulator's samples: a contiguous span (pointer + count) already unrolled from the ring into oldest→newest order, plus the min/max the projection passes need to scale a plot without a second walk of the data.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_SIGNALEVALUATIONINTERVAL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_SIGNALEVALUATIONINTERVAL_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A borrowed view — never owns the samples. Samples points into a caller-supplied scratch span that the accumulator
//    filled in oldest→newest order (the ring already unrolled), so a projection pass reads it as a plain forward array.
//    Minimum/Maximum bound the visible samples; Latest is the newest value (the head), handy for a numeric readout without
//    indexing. Count == 0 marks an empty window (nothing accumulated yet) and the pointer must not be dereferenced.
struct SignalEvaluationInterval
{
    const float* Samples  = nullptr;   // [-] - borrowed span, oldest→newest; valid for Count entries only
    uint32_t     Count    = 0u;        // [-] - number of valid samples in the window (0 = empty)
    float        Minimum  = 0.0f;      // [signal] - smallest value across the window
    float        Maximum  = 0.0f;      // [signal] - largest value across the window
    float        Latest   = 0.0f;      // [signal] - newest sample (the head), or 0 when empty
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The value-span extent, guarded against a flat window (Maximum == Minimum) so a divide-by-extent normalization stays
// finite — a projection pass divides by this to map a sample into 0..1 plot space.
inline float RangeExtent(const SignalEvaluationInterval& Range)
{
    float Extent = Range.Maximum - Range.Minimum;
    return Extent > 1e-6f ? Extent : 1.0f;
}

}   // namespace Frontier

#endif
