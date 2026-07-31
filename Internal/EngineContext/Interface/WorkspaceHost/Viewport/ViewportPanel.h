/*==============================================================================================================================================
                                                              VIEWPORTPANEL.H
==============================================================================================================================================*/
// 🧩 THE one shared viewport, parameterized (2D/3D, camera, source). Every workspace's central view is an instance of this — the 3D modeling
//    view, the 2D UV editor, the paint view, the sketch plane. It owns a ViewportCamera, translates pointer input into orbit/pan/dolly,
//    records the reference grid, and reserves the surface rectangle where the renderer's texture will blit (a themed placeholder until then).
//    A workspace holds a ViewportPanelState across cycles and passes it in — no per-workspace viewport copy exists (the "shared, not
//    duplicated" rule).

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_VIEWPORT_VIEWPORTPANEL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_VIEWPORT_VIEWPORTPANEL_H

#include "imgui.h"

#include "../../../Navigation/Camera/CameraConfiguration.h"

#include "../../Theme/ThemeConfiguration.h"
#include "../../SpatialCompass/SpatialCompassContext.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMS
//------------------------------------------------------------------------------------------------------------------------

enum class ViewportProjection
{
    Perspective,   // [-] - 3D orbit view (modeling, paint)
    Planar         // [-] - 2D pan/zoom view (UV, sketch)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Persistent per-viewport state a workspace owns across cycles. Initialize once with InitializeViewportPanelState.
struct ViewportPanelState
{
    ViewportCamera     Camera;         // [-] - Orbit/pan/dolly camera (the render-canonical spec; Navigation verbs drive it)
    ViewportProjection Projection;     // [-] - 2D or 3D behaviour
    ImTextureID        RenderedTexture;// [-] - Renderer output handle to blit (0 -> draw the placeholder grid only); ImGui's 64-bit texture id (a VkDescriptorSet) so it does not truncate
    bool               GridEnabled;    // [-] - Show the reference grid
    bool               AxisEnabled;    // [-] - Show the origin axis cross

    // 📝 The CAD spatial compass overlay (3D viewports only). Persistent across cycles; drawn on top of the scene by
    //    ConstructViewportPanel. A 2D/planar viewport leaves it initialized-but-unused.
    bool                SpatialCompassEnabled;   // [-] - Show the spatial compass (3D only)
    SpatialCompassState SpatialCompass;          // [-] - Compass overlay state
};


// 📝 What the viewport reports back this cycle (so the workspace can react — e.g. picking under the cursor once the renderer lands).
struct ViewportPanelResult
{
    bool   Hovered;        // [-] - Pointer is over the surface
    bool   CameraChanged;  // [-] - Camera moved this cycle
    ImVec2 SurfaceMin;     // [px] - Surface rect top-left (screen space)
    ImVec2 SurfaceMax;     // [px] - Surface rect bottom-right (screen space)
    ImVec2 LocalPointer;   // [px] - Pointer position within the surface (0,0 = top-left)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Initialize a viewport with the default camera for its projection.
void InitializeViewportPanelState(ViewportPanelState& State, ViewportProjection Projection);

// 📝 Record the viewport surface: reserve the rect, blit the rendered texture (or the placeholder), record the grid, apply pointer input.
//    Call from a workspace's viewport panel callback. Returns interaction + surface geometry for downstream picking.
ViewportPanelResult ConstructViewportPanel(const ThemeConfiguration& Theme, ViewportPanelState& State);

}   // namespace Frontier

#endif
