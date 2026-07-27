/*==============================================================================================================================================
                                                              TEXTUREBAKEWORKSPACE.H
==============================================================================================================================================*/
// 🧩 Bake set list + bake controls. A 3D preview viewport, the bake-set list as the outliner, and the bake property panel (output + maps).
//    Composition of shared components; bake settings are placeholder until Baking is wired.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_TEXTUREBAKE_TEXTUREBAKEWORKSPACE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_TEXTUREBAKE_TEXTUREBAKEWORKSPACE_H

#include "../../WorkspaceHost/WorkspaceDockHost.h"

#include "../../WorkspaceHost/Viewport/ViewportPanel.h"

#include "../../WorkspaceHost/Outliner/Model/OutlinerModel.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct TextureBakeWorkspaceState
{
    ViewportPanelState Viewport;          // [-] - Shared 3D preview viewport
    OutlinerModel      BakeSets;          // [-] - Bake-set list (placeholder rows)
    bool               OutputExpanded;    // [-] - Output card collapse state
    bool               MapsExpanded;      // [-] - Maps card collapse state
    int                ResolutionIndex;   // [-] - Selected output resolution
    bool               BakeAmbientOcclusion;// [-] - Placeholder map toggle
    bool               BakeNormal;        // [-] - Placeholder map toggle
    bool               BakeCurvature;     // [-] - Placeholder map toggle
    int                ActiveToolIndex;   // [-] - Selected bake action
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeTextureBakeWorkspace(TextureBakeWorkspaceState& State);
WorkspaceDescriptor ResolveTextureBakeWorkspaceDescriptor(TextureBakeWorkspaceState& State);

}   // namespace Frontier

#endif
