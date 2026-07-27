/*==============================================================================================================================================
                                                              UVPROPERTYPANEL.H
==============================================================================================================================================*/
// 🧩 UV property region. Thin composition over PropertyPanelBase + shared Controls — Layout / Display cards bound to the UV workspace state.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_UV_UVPROPERTYPANEL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_UV_UVPROPERTYPANEL_H

#include "UVWorkspace.h"

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record the UV property column for this cycle against the workspace state.
void ConstructUVPropertyPanel(const ThemeConfiguration& Theme, UVWorkspaceState& State);

}   // namespace Frontier

#endif
