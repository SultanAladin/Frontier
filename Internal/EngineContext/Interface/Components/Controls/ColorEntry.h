/*==============================================================================================================================================
                                                                COLORENTRY.H
==============================================================================================================================================*/
// 🧩 RGBA picker: a labelled colour swatch that opens ImGui's picker popup. The four channels (0-1) are caller-owned. Stateless — one
//    definition serves every colour field (material albedo, light tint, brush colour).

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_COLORENTRY_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_COLORENTRY_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ColorEntryDescriptor
{
    const char* Label;         // [-] - Row label
    float*      Channels;      // [-] - Caller-owned array (RGB or RGBA, 0-1), edited in place
    bool        IncludeAlpha;  // [-] - true -> RGBA (4 floats), false -> RGB (3 floats)
    bool        Enabled;       // [-] - false draws muted + ignores input
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record one colour swatch row. Returns true on the cycle any channel changed. No-op-safe if Channels is null.
bool ConstructColorEntry(const ThemeConfiguration& Theme, const ColorEntryDescriptor& Descriptor);

}   // namespace Frontier

#endif
