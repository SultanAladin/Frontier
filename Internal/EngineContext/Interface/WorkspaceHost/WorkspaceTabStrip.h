/*==============================================================================================================================================
                                                              WORKSPACETABSTRIP.H
==============================================================================================================================================*/
// 🧩 The workspace tab row: one selectable tab per registered workspace, driven entirely from the dock state. Clicking a tab requests the
//    switch (applied next cycle by the dock host). The strip names no concrete workspace — it walks WorkspaceDockState.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_WORKSPACETABSTRIP_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_WORKSPACETABSTRIP_H

#include "WorkspaceDockHost.h"

#include "../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record the tab row for the registered workspaces. A click calls RequestWorkspaceActivation on the state. Call once per cycle above the
//    dock. Returns the index the user clicked this cycle, or -1.
int ConstructWorkspaceTabStrip(const ThemeConfiguration& Theme, WorkspaceDockState& State);

}   // namespace Frontier

#endif
