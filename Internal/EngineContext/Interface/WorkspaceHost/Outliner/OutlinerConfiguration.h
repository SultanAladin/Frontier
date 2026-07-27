/*==============================================================================================================================================
                                                              OUTLINERCONFIGURATION.H
==============================================================================================================================================*/
// 🧩 Per-workspace parameterization of the shared outliner: which columns show, indent/row rhythm (defaulted from the theme), and the header
//    caption. This is how ONE OutlinerPanel serves every workspace differently without a per-workspace copy — the workspace passes a config.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_OUTLINER_OUTLINERCONFIGURATION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_OUTLINER_OUTLINERCONFIGURATION_H

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct OutlinerConfiguration
{
    const char* HeaderCaption;     // [-] - Column header (e.g. "Scene", "Islands", "Layers") — null hides the header
    float       RowHeight;         // [px] - Per-row height (<=0 -> theme RowHeight)
    float       IndentWidth;       // [px] - Per-depth indent (<=0 -> theme IndentWidth)
    bool        VisibilityColumn;  // [-] - Show the eye toggle
    bool        TintChips;         // [-] - Show the record entry colour chip
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A default configuration (all columns on, metrics from the theme). A workspace tweaks the returned struct.
OutlinerConfiguration ResolveDefaultOutlinerConfiguration(const ThemeConfiguration& Theme, const char* HeaderCaption);

}   // namespace Frontier

#endif
