/*==============================================================================================================================================
                                                          SIGNALINTAKEPROFILE.H
==============================================================================================================================================*/
// 🧩 The pull-callback contract that lets an instrument work on ANY data an app feeds it: a function pointer + an opaque context the overlay calls once per paint to retrieve the next sample. No per-frame heap, no ownership — the app writes one tiny retriever per instrument and the overlay pulls through it. This is the whole seam between the instrumentation UI and the host's numbers.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_SIGNALINTAKEPROFILE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_SIGNALINTAKEPROFILE_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 The pull retriever: given the app's opaque context, return the instrument's current scalar. Called once per instrument
//    per paint from the overlay's accumulation step — must be cheap and non-blocking (read a live counter, not compute one).
//    The context is whatever the app registered alongside it (a pointer to its telemetry aggregate, a channel index, …); the
//    overlay never dereferences it, only forwards it. A null retriever leaves the instrument idle (it accumulates nothing).
typedef float (*SignalRetriever)(void* IngestionContext);

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One pull source, stored by value inside the instrument record. Retrieve is the app's per-instrument function; Context is
//    forwarded to it verbatim. ScaleFactor and Bias affine-map the raw pull into display units at accumulation time
//    (Value * ScaleFactor + Bias) so an app can feed bytes and show MiB without touching the retriever. Fully trivial — no
//    allocation, no destructor — so a record store of these stays a flat POD block the overlay can memcpy or reset at will.
struct SignalIntakeProfile
{
    SignalRetriever Retrieve     = nullptr;   // [-] - app pull callback, or null for an idle instrument
    void*           Context      = nullptr;   // [-] - opaque payload forwarded to Retrieve verbatim
    float           ScaleFactor  = 1.0f;      // [-] - multiplies the raw pull before accumulation (raw→display units)
    float           Bias         = 0.0f;      // [display] - added after scaling (raw→display units)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Pull one display-unit sample through the profile (raw * ScaleFactor + Bias). Returns 0 for an idle profile so the caller
// need not branch on a null retriever before accumulating.
inline float ResolveIngestedSample(const SignalIntakeProfile& Profile)
{
    if (Profile.Retrieve == nullptr)
    {
        return 0.0f;
    }
    return Profile.Retrieve(Profile.Context) * Profile.ScaleFactor + Profile.Bias;
}

}   // namespace Frontier

#endif
