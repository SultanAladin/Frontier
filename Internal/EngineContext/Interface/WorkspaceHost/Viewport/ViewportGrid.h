/*==============================================================================================================================================
                                                              VIEWPORTGRID.H
==============================================================================================================================================*/
// 🧩 Ground grid + axis reference drawn into the viewport surface. Until the 3D renderer is wired, this records a screen-space reference grid
//    directly onto the viewport's ImDrawList so a blank viewport still reads as a 3D/2D space with an origin. One grid serves every viewport;
//    a workspace only toggles it via the descriptor.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_VIEWPORT_VIEWPORTGRID_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_VIEWPORT_VIEWPORTGRID_H

#include "imgui.h"

#include "../../../Navigation/Camera/CameraConfiguration.h"

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ViewportGridDescriptor
{
    ImVec2 SurfaceMin;     // [px] - Top-left of the viewport surface (screen space)
    ImVec2 SurfaceMax;     // [px] - Bottom-right of the viewport surface (screen space)
    float  CellPixels;     // [px] - Spacing between minor grid lines
    int    MajorEvery;     // [-]  - Every Nth line drawn as a major (brighter) line
    bool   AxisReference;  // [-]  - Draw the origin cross in axis colours
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record the reference grid onto the current window's draw list, clipped to the surface. Camera pans/zooms the grid so it reads as ground.
void ConstructViewportGrid(const ThemeConfiguration& Theme, const ViewportCamera& Camera, const ViewportGridDescriptor& Descriptor);

}   // namespace Frontier

#endif
