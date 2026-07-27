/*==============================================================================================================================================
                                                                DROPDOWN.H
==============================================================================================================================================*/
// 🧩 Combo-popup selector: a labelled "[ current | ▾ ]" pill that opens a floating list (ControlsPreview.html Dropdown). Hovering an item
//    shows a sharp lighter-grey highlight with a full-height accent bar on the left; the current item carries a filled radio. The selected
//    index is caller-owned. Stateless — one definition serves every popup enum (shading mode, projection, workspace picker). This is the
//    space-saving sibling of SelectionEntry's inline segmented pills.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_DROPDOWN_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_DROPDOWN_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct DropdownDescriptor
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
bool ConstructDropdown(const ThemeConfiguration& Theme, const DropdownDescriptor& Descriptor);

}   // namespace Frontier

#endif
