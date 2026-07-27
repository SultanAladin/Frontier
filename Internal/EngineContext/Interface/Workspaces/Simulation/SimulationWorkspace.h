/*==============================================================================================================================================
                                                             SIMULATIONWORKSPACE.H
==============================================================================================================================================*/
// 🧩 The minimal viewport-only workspace hosted by SimulationSandbox. Just a perspective viewport plus a slim property column with the
//    playback controls (run/pause, timestep, gravity) — no outliner, no tool strip. Like every workspace it owns only its persistent state
//    and its registry activation; the viewport it shows is the SAME shared ViewportPanel every other workspace uses.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_SIMULATION_SIMULATIONWORKSPACE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_SIMULATION_SIMULATIONWORKSPACE_H

#include "../../WorkspaceHost/WorkspaceDockHost.h"

#include "../../WorkspaceHost/Viewport/ViewportPanel.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Persistent simulation-workspace state. Held by the application across cycles and threaded into the dock as the context.
struct SimulationWorkspaceState
{
    ViewportPanelState Viewport;          // [-]    - Shared 3D viewport instance
    bool               PlaybackExpanded;  // [-]    - Playback card collapse state
    bool               RunningEnabled;    // [-]    - Simulation is advancing (placeholder until Physics is wired)
    float              Timestep;          // [ms]   - Fixed step per tick
    float              GravityStrength;   // [cm/s²]- Downward acceleration
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Initialize the workspace state (default 3D camera, paused, default step + gravity).
void InitializeSimulationWorkspace(SimulationWorkspaceState& State);

// 📝 Build a WorkspaceDescriptor for registration on the dock (Identifier "Simulation").
WorkspaceDescriptor ResolveSimulationWorkspaceDescriptor(SimulationWorkspaceState& State);

}   // namespace Frontier

#endif
