/*==============================================================================================================================================
                                                              SKETCHPROPERTYPANEL.H
==============================================================================================================================================*/
// 🧩 Constraint/dimension property region. Thin composition over PropertyPanelBase + shared Controls, bound to the sketcher state.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_PARAMETRICSKETCHER_SKETCHPROPERTYPANEL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_PARAMETRICSKETCHER_SKETCHPROPERTYPANEL_H

#include "ParametricSketcherWorkspace.h"

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructSketchPropertyPanel(const ThemeConfiguration& Theme, ParametricSketcherWorkspaceState& State);

}   // namespace Frontier

#endif
