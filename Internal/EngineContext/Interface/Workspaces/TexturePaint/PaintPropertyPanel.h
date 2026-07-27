/*==============================================================================================================================================
                                                              PAINTPROPERTYPANEL.H
==============================================================================================================================================*/
// 🧩 Paint property region. Thin composition over PropertyPanelBase + shared Controls — Brush / Channel cards bound to the paint state.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_TEXTUREPAINT_PAINTPROPERTYPANEL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_TEXTUREPAINT_PAINTPROPERTYPANEL_H

#include "TexturePaintWorkspace.h"

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructPaintPropertyPanel(const ThemeConfiguration& Theme, TexturePaintWorkspaceState& State);

}   // namespace Frontier

#endif
