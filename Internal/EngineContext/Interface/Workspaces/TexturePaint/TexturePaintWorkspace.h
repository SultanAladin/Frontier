/*==============================================================================================================================================
                                                              TEXTUREPAINTWORKSPACE.H
==============================================================================================================================================*/
// 🧩 Brush/channel/layer layout. A 3D viewport to paint on, the paint layer stack as the outliner, and the paint property panel (brush +
//    channel). Composition of shared components; brush state is placeholder until Painting is wired.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_TEXTUREPAINT_TEXTUREPAINTWORKSPACE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_TEXTUREPAINT_TEXTUREPAINTWORKSPACE_H

#include "../../WorkspaceHost/WorkspaceDockHost.h"

#include "../../WorkspaceHost/Viewport/ViewportPanel.h"

#include "../../WorkspaceHost/Outliner/Model/OutlinerModel.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct TexturePaintWorkspaceState
{
    ViewportPanelState Viewport;          // [-] - Shared 3D viewport
    OutlinerModel      Layers;            // [-] - Paint layer stack (placeholder rows)
    bool               BrushExpanded;     // [-] - Brush card collapse state
    bool               ChannelExpanded;   // [-] - Channel card collapse state
    float              BrushRadius;       // [px] - Placeholder brush size
    float              BrushFlow;         // [-]  - Placeholder brush flow
    float              BrushColor[4];     // [-]  - Placeholder brush colour RGBA
    int                ActiveChannelIndex;// [-]  - Selected paint channel
    int                ActiveToolIndex;   // [-]  - Selected brush tool
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeTexturePaintWorkspace(TexturePaintWorkspaceState& State);
WorkspaceDescriptor ResolveTexturePaintWorkspaceDescriptor(TexturePaintWorkspaceState& State);

}   // namespace Frontier

#endif
