/*==============================================================================================================================================
                                                              BOOLEANENTRY.H
==============================================================================================================================================*/
// 🧩 True/false toggle: a labelled checkbox. The flag is caller-owned. Stateless — one definition serves every toggle (visibility, cast
//    shadow, wireframe, snap-enabled). Boolean fields end in "Enabled" per house convention.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_BOOLEANENTRY_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_BOOLEANENTRY_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct BooleanEntryDescriptor
{
    const char* Label;         // [-] - Row label
    bool*       Value;         // [-] - Caller-owned flag, edited in place
    bool        Enabled;       // [-] - false draws muted + ignores input
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record one toggle row. Returns true on the cycle the flag changed. No-op-safe if Value is null.
bool ConstructBooleanEntry(const ThemeConfiguration& Theme, const BooleanEntryDescriptor& Descriptor);

}   // namespace Frontier

#endif
