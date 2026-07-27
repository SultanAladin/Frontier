/*==============================================================================================================================================
                                                              ACTIONTOOLBAR.H
==============================================================================================================================================*/
// 🧩 Horizontal strip of executable tools: a row of icon/label buttons, one of which may be marked active (a mode toggle). The caller owns
//    the button set and reads back which one was pressed. Stateless — one definition serves every toolbar (viewport tools, sketch tools,
//    brush tools).

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CARDS_ACTIONTOOLBAR_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CARDS_ACTIONTOOLBAR_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One tool in the strip. IconRasterId is a handle into the shared SvgIconRaster (0 -> caption-only button).
struct ActionToolItem
{
    const char* Caption;       // [-]  - Button caption / tooltip
    unsigned    IconRasterId;  // [-]  - SvgIconRaster texture handle (0 -> no icon)
    bool        Active;        // [-]  - true draws the accent fill (current mode)
    bool        Enabled;       // [-]  - false draws muted + ignores input
};


struct ActionToolbarDescriptor
{
    const char*           Identifier;   // [-]  - Unique id for the strip
    const ActionToolItem* Items;        // [-]  - Caller-owned array of tools
    int                   ItemCount;    // [-]  - Number of tools
    float                 ButtonSize;   // [px] - Square button edge (<=0 -> theme control height)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record the tool strip. Returns the index of the tool pressed this cycle, or -1 if none. No-op-safe (-1) if Items is null.
int ConstructActionToolbar(const ThemeConfiguration& Theme, const ActionToolbarDescriptor& Descriptor);

}   // namespace Frontier

#endif
