/*==============================================================================================================================================
                                                              SELECTIONENTRY.H
==============================================================================================================================================*/
// 🧩 Enumerated dropdown: a labelled combo over a fixed set of option strings. The selected index is caller-owned. Stateless — one
//    definition serves every enum field (shading mode, projection, brush falloff, workspace picker).

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_SELECTIONENTRY_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_SELECTIONENTRY_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SelectionEntryDescriptor
{
    const char*        Label;        // [-] - Row label
    int*               SelectedIndex;// [-] - Caller-owned index into Options, edited in place
    const char* const* Options;      // [-] - Array of option labels
    int                OptionCount;  // [-] - Number of options
    bool               Enabled;      // [-] - false draws muted + ignores input
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record one dropdown row. Returns true on the cycle the selection changed. No-op-safe if SelectedIndex/Options is null.
bool ConstructSelectionEntry(const ThemeConfiguration& Theme, const SelectionEntryDescriptor& Descriptor);

}   // namespace Frontier

#endif
