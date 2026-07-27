/*==============================================================================================================================================
                                                              OUTLINERPANEL.H
==============================================================================================================================================*/
// 🧩 THE shared outliner. Records a flattened OutlinerModel (rows built by the workspace from its Scene assembly) under an OutlinerConfiguration
//    that parameterizes it per workspace. Reports every interaction back by RECORD TOKEN (never a row index / slot) so the workspace can route
//    the click to Scene selection, an expand toggle, or a visibility flip without the wrong-id class of bug. One definition serves the modeling
//    tree, the UV island list, the paint layer stack, and the sketch record tree.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_OUTLINER_OUTLINERPANEL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_OUTLINER_OUTLINERPANEL_H

#include "OutlinerConfiguration.h"

#include "Model/OutlinerModel.h"

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 What kind of interaction a row reported this cycle.
enum class OutlinerInteraction
{
    None,          // [-] - Nothing this cycle
    Selected,      // [-] - Row body clicked (route to Scene selection)
    ToggledExpand, // [-] - Collapse arrow clicked
    ToggledVisible // [-] - Eye toggle clicked
};


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The single interaction reported this cycle, addressed by record token. The workspace applies it against its own Scene assembly.
struct OutlinerPanelResult
{
    OutlinerInteraction Interaction;    // [-] - What happened
    OutlinerRecordToken Record;         // [-] - Which record entry it happened to
    bool                AdditiveSelect; // [-] - Ctrl held during a Selected interaction (extend selection)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record the outliner body for a model. Returns the (single) interaction this cycle, addressed by record token.
OutlinerPanelResult ConstructOutlinerPanel(const ThemeConfiguration& Theme, const OutlinerConfiguration& Configuration,
                                           const OutlinerModel& Model);

}   // namespace Frontier

#endif
