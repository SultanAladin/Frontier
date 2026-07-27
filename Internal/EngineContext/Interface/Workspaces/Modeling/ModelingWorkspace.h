/*==============================================================================================================================================
                                                              MODELINGWORKSPACE.H
==============================================================================================================================================*/
// 🧩 Polygon tools + modifier layout. The 3D modeling workspace: a perspective viewport, the scene outliner on the left, the modeling
//    property panel on the right, and a tool/control strip below. This file owns only the workspace's persistent state + its registry
//    activation — every panel it shows is a SHARED component (ViewportPanel, OutlinerPanel, PropertyPanelBase), so this is composition, not
//    new UI. A blank-but-live workspace: real panels, placeholder scene data until Geometry/Scene are wired.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_MODELING_MODELINGWORKSPACE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_MODELING_MODELINGWORKSPACE_H

#include "../../WorkspaceHost/WorkspaceDockHost.h"

#include "../../WorkspaceHost/Viewport/ViewportPanel.h"

#include "../../WorkspaceHost/Outliner/Model/OutlinerModel.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Persistent modeling-workspace state. Held by the application across cycles and threaded into the dock as the workspace context.
struct ModelingWorkspaceState
{
    ViewportPanelState Viewport;          // [-] - Shared 3D viewport instance
    OutlinerModel      Outliner;          // [-] - Row list built from the scene (placeholder rows for now)
    bool               TransformExpanded; // [-] - Transform card collapse state
    bool               GeometryExpanded;  // [-] - Geometry card collapse state
    bool               ShadingExpanded;   // [-] - Shading card collapse state
    float              TransformPosition[3];// [cm] - Placeholder edit target
    float              TransformRotation[3];// [deg]
    float              TransformScale[3];  // [-]
    int                ActiveToolIndex;   // [-] - Selected polygon tool in the control strip
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Initialize the workspace state (default 3D camera, empty outliner, seeded placeholder rows + transform).
void InitializeModelingWorkspace(ModelingWorkspaceState& State);

// 📝 Build a WorkspaceDescriptor for registration on the dock (Identifier "Modeling"). Points ActivateRegistry + Context at this state.
WorkspaceDescriptor ResolveModelingWorkspaceDescriptor(ModelingWorkspaceState& State);

}   // namespace Frontier

#endif
