/*==============================================================================================================================================
                                                              DEPLOYMENTBRACKET.H
==============================================================================================================================================*/
// 🧩 The outer chrome bracket over the themed desk. Owns a single full-viewport ImGui window (no title bar, no move/resize), enforces the
//    active theme once, then records the workspace tab strip above the workspace dock. This is the ONE call an application makes each cycle to
//    stand up the whole shell — the tab strip, the four-region dock, and every registered panel flow from here. Every application (Editor,
//    ParametricSketcher, TexturePaint, TextureBake, SimulationSandbox) reuses this same bracket; the only difference is which workspaces it
//    registered on the dock state.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_DEPLOYMENTBRACKET_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_DEPLOYMENTBRACKET_H

#include "WorkspaceDockHost.h"

#include "../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 What the shell shows this cycle besides the desk. Kept tiny — the bracket is chrome, not a workspace.
struct DeploymentBracketDescriptor
{
    const char* ApplicationTitle;   // [-] - Shown at the left of the tab strip (e.g. "Frontier Editor")
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record the entire application shell for this cycle: full-viewport window, theme enforced, title + tab strip, then the workspace dock.
//    The caller supplies the resolved theme and the dock state it populated with workspaces. One call, whole UI.
void ConstructDeploymentBracket(const ThemeConfiguration& Theme, const DeploymentBracketDescriptor& Descriptor,
                                WorkspaceDockState& DockState);

}   // namespace Frontier

#endif
