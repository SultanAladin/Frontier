/*==============================================================================================================================================
                                                           SIMULATIONPROPERTYPANEL.H
==============================================================================================================================================*/
// 🧩 Simulation property region. Thin composition over PropertyPanelBase + shared Controls — a single Playback card (run toggle, timestep,
//    gravity) bound to the workspace state. No card chrome or control is defined here.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_SIMULATION_SIMULATIONPROPERTYPANEL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_SIMULATION_SIMULATIONPROPERTYPANEL_H

#include "SimulationWorkspace.h"

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record the simulation property column for this cycle against the workspace state.
void ConstructSimulationPropertyPanel(const ThemeConfiguration& Theme, SimulationWorkspaceState& State);

}   // namespace Frontier

#endif
