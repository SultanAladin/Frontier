/*==============================================================================================================================================
                                                          INSTRUMENTATIONEXTENSION.H
==============================================================================================================================================*/
// 🧩 The lifecycle owner and single entry point for the whole telemetry overlay: an app initializes one extension, registers/withdraws instruments at runtime (returned tokens name them), and calls one Refresh per paint that pulls every instrument's signal through its callback, then hosts each live card (solid OLED-dark, drag/resize/dock) and the bottom-left pill tray for collapsed ones. Owns the record store + a scratch span; holds no per-frame heap, so the overlay stays float-free and works on whatever data the app's callbacks feed it. This is the type the standalone app (and any host) builds its window with.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTATIONEXTENSION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTATIONEXTENSION_H

#include "Registry/InstrumentRecordArchive.h"
#include "SignalAccumulation/MetricSignalAccumulator.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The overlay's whole state. Store holds every instrument; ScratchSpan is the single reusable unroll buffer the enclosure
//    borrows each card (sized for one instrument's full signal bank back to back — the render report unrolls seven rings at
//    once), so a paint of N cards allocates nothing. ReadyStatus gates Refresh until Initialize has run. One extension serves
//    an entire app — register as many cards as needed, all sharing this state.
struct InstrumentationExtension
{
    InstrumentRecordArchive Store;                                                        // [-] - every live instrument, dense + recyclable
    float                   ScratchSpan[SignalRingCapacity * InstrumentSignalCapacity] = { 0 }; // [-] - reusable unroll buffer (whole signal bank)
    bool                    ReadyStatus                                                = false; // [-] - true once Initialize ran
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bring the overlay up: clear the store to empty and arm ReadyStatus. No device resources — the overlay draws through the
// hosting ImGui context's draw lists, so there is nothing to allocate or fail. Call once before the first Refresh.
void InitializeInstrumentationExtension(InstrumentationExtension& Extension);

// Add one instrument at runtime and return a token naming it (vacant if the store is full). The registration supplies title,
// category, colours, and pull source(s). Safe to call any frame — the card appears next Refresh.
InstrumentToken RegisterInstrumentCard(InstrumentationExtension& Extension, const InstrumentRegistration& Registration);

// Remove an instrument at runtime. The token goes stale; its slot recycles. A no-op for a stale/vacant token.
void WithdrawInstrumentCard(InstrumentationExtension& Extension, InstrumentToken Token);

// The whole per-paint step, called once between the host's ImGui NewFrame and Render: pull every live instrument's signal
// through its callback into its ring, host each expanded card (constructing its body pass), lay the collapsed ones into the
// bottom-left pill tray, and apply any close/re-open the user clicked. ViewportHeight positions the tray. A no-op until
// ReadyStatus holds. No allocation occurs — the scratch span is reused across every card.
void RefreshInstrumentationExtension(InstrumentationExtension& Extension, float ViewportHeight);

// Tear the overlay down: clear the store. No device resources to release; provided for lifecycle symmetry so a host can
// finalize the overlay alongside its other subsystems.
void FinalizeInstrumentationExtension(InstrumentationExtension& Extension);

}   // namespace Frontier

#endif
