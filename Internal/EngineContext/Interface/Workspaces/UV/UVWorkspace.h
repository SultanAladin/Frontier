/*==============================================================================================================================================
                                                              UVWORKSPACE.H
==============================================================================================================================================*/
// 🧩 2D UV editor layout. A PLANAR viewport (the same shared ViewportPanel, pan/zoom instead of orbit) over the UV island list, with the UV
//    property panel on the right. Composition of shared components — the only difference from Modeling is the projection + the row semantics
//    (islands rather than objects). "UV" is all-caps everywhere per project convention.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_UV_UVWORKSPACE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_UV_UVWORKSPACE_H

#include "../../WorkspaceHost/WorkspaceDockHost.h"

#include "../../WorkspaceHost/Viewport/ViewportPanel.h"

#include "../../WorkspaceHost/Outliner/Model/OutlinerModel.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct UVWorkspaceState
{
    ViewportPanelState Viewport;          // [-] - Shared viewport, planar projection
    OutlinerModel      Islands;           // [-] - UV island list (placeholder rows for now)
    bool               LayoutExpanded;    // [-] - Layout card collapse state
    bool               DisplayExpanded;   // [-] - Display card collapse state
    float              PackMargin;        // [-] - Placeholder island-pack margin
    bool               ShowSeams;         // [-] - Placeholder display toggle
    int                ActiveToolIndex;   // [-] - Selected UV tool
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Initialize the UV workspace (planar camera, seeded island rows).
void InitializeUVWorkspace(UVWorkspaceState& State);

// 📝 Build the WorkspaceDescriptor for dock registration (Identifier "UV").
WorkspaceDescriptor ResolveUVWorkspaceDescriptor(UVWorkspaceState& State);

}   // namespace Frontier

#endif
