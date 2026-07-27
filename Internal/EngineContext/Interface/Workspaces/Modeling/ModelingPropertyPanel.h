/*==============================================================================================================================================
                                                              MODELINGPROPERTYPANEL.H
==============================================================================================================================================*/
// 🧩 Modeling property region. A thin composition over PropertyPanelBase + shared Controls — Transform / Geometry / Shading cards. No card
//    chrome or control is defined here; this file only chooses which shared controls appear and binds them to the workspace's state.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_MODELING_MODELINGPROPERTYPANEL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_MODELING_MODELINGPROPERTYPANEL_H

#include "ModelingWorkspace.h"

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record the modeling property column for this cycle against the workspace state.
void ConstructModelingPropertyPanel(const ThemeConfiguration& Theme, ModelingWorkspaceState& State);

}   // namespace Frontier

#endif
