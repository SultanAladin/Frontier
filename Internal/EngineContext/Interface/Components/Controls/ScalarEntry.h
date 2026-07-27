/*==============================================================================================================================================
                                                                SCALARENTRY.H
==============================================================================================================================================*/
// 🧩 Single numeric entry: a labelled drag-field for one float with optional soft bounds and a step. Value is caller-owned; the control is
//    stateless so one definition serves every scalar field.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_SCALARENTRY_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_SCALARENTRY_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ScalarEntryDescriptor
{
    const char* Label;        // [-] - Row label
    float*      Value;        // [-] - Caller-owned value, edited in place
    float       Step;         // [-] - Drag sensitivity per pixel (<=0 -> 0.01)
    float       Minimum;      // [-] - Soft lower clamp (Minimum == Maximum -> unclamped)
    float       Maximum;      // [-] - Soft upper clamp
    const char* Format;       // [-] - printf format (null -> "%.3f")
    const char* Unit;         // [-] - Optional unit suffix in the pill's grey cap (e.g. "×", "cm"); null -> no cap
    bool        Enabled;      // [-] - false draws muted + ignores input
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record one scalar drag-field. Returns true on the cycle the value changed. No-op-safe if Value is null.
bool ConstructScalarEntry(const ThemeConfiguration& Theme, const ScalarEntryDescriptor& Descriptor);

}   // namespace Frontier

#endif
