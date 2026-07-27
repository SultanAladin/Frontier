/*==============================================================================================================================================
                                                          PARAMETRICSKETCHERWORKSPACE.H
==============================================================================================================================================*/
// 🧩 Sketch + solid-preview layout. A PLANAR sketch-plane viewport, the sketch record tree as the outliner, and the constraint/dimension
//    property panel. Composition of shared components; sketch data is placeholder until the ParametricSketcher extension is wired.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_PARAMETRICSKETCHER_PARAMETRICSKETCHERWORKSPACE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_PARAMETRICSKETCHER_PARAMETRICSKETCHERWORKSPACE_H

#include "../../WorkspaceHost/WorkspaceDockHost.h"

#include "../../WorkspaceHost/Viewport/ViewportPanel.h"

#include "../../WorkspaceHost/Outliner/Model/OutlinerModel.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ParametricSketcherWorkspaceState
{
    ViewportPanelState Viewport;          // [-] - Shared viewport, planar sketch plane
    OutlinerModel      SketchRecords;     // [-] - Sketch record tree (placeholder rows)
    bool               ConstraintExpanded;// [-] - Constraint card collapse state
    bool               DimensionExpanded; // [-] - Dimension card collapse state
    float              DimensionValue;    // [mm] - Placeholder driven dimension
    int                ConstraintIndex;   // [-]  - Selected constraint category
    int                ActiveToolIndex;   // [-]  - Selected sketch tool
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeParametricSketcherWorkspace(ParametricSketcherWorkspaceState& State);
WorkspaceDescriptor ResolveParametricSketcherWorkspaceDescriptor(ParametricSketcherWorkspaceState& State);

}   // namespace Frontier

#endif
