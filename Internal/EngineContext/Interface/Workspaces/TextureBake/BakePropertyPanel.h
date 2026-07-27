/*==============================================================================================================================================
                                                              BAKEPROPERTYPANEL.H
==============================================================================================================================================*/
// 🧩 Bake property region. Thin composition over PropertyPanelBase + shared Controls — Output / Maps cards bound to the bake state.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_TEXTUREBAKE_BAKEPROPERTYPANEL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_TEXTUREBAKE_BAKEPROPERTYPANEL_H

#include "TextureBakeWorkspace.h"

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructBakePropertyPanel(const ThemeConfiguration& Theme, TextureBakeWorkspaceState& State);

}   // namespace Frontier

#endif
