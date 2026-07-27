/*==============================================================================================================================================
                                                                VALUESLIDER.H
==============================================================================================================================================*/
// 🧩 Draggable numeric range: a labelled slider over a [Minimum, Maximum] band. The value is OWNED by the caller and passed in by pointer —
//    the control is stateless, so one ValueSlider definition serves every slider in every panel (the GlassButtonPass rule).

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_VALUESLIDER_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_VALUESLIDER_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Per-instance description of one slider. No persistent state — the caller keeps *Value alive across cycles.
struct ValueSliderDescriptor
{
    const char* Label;         // [-] - Row label
    float*      Value;         // [-] - Caller-owned value, edited in place
    float       Minimum;       // [-] - Lower bound
    float       Maximum;       // [-] - Upper bound
    const char* Format;        // [-] - printf format for the readout, e.g. "%.2f" (null -> "%.3f")
    const char* Unit;          // [-] - Optional unit suffix drawn in the pill's grey cap (e.g. "°", "%", "px"); null -> no cap
    bool        Enabled;       // [-] - false draws muted + ignores input
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record one slider row. Returns true on the cycle the value changed. No-op-safe if Value is null.
bool ConstructValueSlider(const ThemeConfiguration& Theme, const ValueSliderDescriptor& Descriptor);

}   // namespace Frontier

#endif
