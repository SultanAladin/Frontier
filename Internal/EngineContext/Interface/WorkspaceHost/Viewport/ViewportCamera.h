/*==============================================================================================================================================
                                                              VIEWPORTCAMERA.H
==============================================================================================================================================*/
// 🧩 The panel-local orbit/pan/dolly camera the ImGui viewport panel advances directly from pointer PIXEL deltas (TargetX/Y/Z scalars, internal
//    sensitivity + clamps). Distinct from the render-canonical Frontier::ViewportCamera (Navigation/Camera/CameraConfiguration.h, Vector3f Target,
//    radian deltas) — the two do not share a layout, so this one carries the Panel qualifier to keep the symbols from colliding in EngineContext.lib.
//    Pure data + the small set of update functions that advance it from pointer deltas. No ImGui, no rendering. One camera type serves every panel.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_VIEWPORT_VIEWPORTCAMERA_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_VIEWPORT_VIEWPORTCAMERA_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Spherical orbit camera. Yaw/Pitch orbit around Target at Distance; a 2D viewport ignores the angles and pans Target on the XY plane.
struct PanelViewportCamera
{
    float TargetX;         // [cm] - Orbit pivot, world
    float TargetY;         // [cm] - Orbit pivot, world
    float TargetZ;         // [cm] - Orbit pivot, world
    float Distance;        // [cm] - Eye distance from Target (also 2D zoom)
    float Yaw;             // [rad] - Orbit around the up axis
    float Pitch;           // [rad] - Orbit elevation (clamped away from the poles)
    float FieldOfView;     // [rad] - Vertical FOV (3D only)
    bool  Orthographic;    // [-] - true for 2D / ortho viewports
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A sensible default 3/4 view for a fresh 3D viewport.
PanelViewportCamera ResolveDefaultPerspectivePanelCamera();

// 📝 A top-down orthographic camera for a 2D viewport (UV editor, sketch plane).
PanelViewportCamera ResolveDefaultOrthographicPanelCamera();

// 📝 Orbit by pointer delta [px]. No-op for orthographic cameras.
void OrbitPanelViewportCamera(PanelViewportCamera& Camera, float DeltaX, float DeltaY);

// 📝 Pan the target on the view plane by pointer delta [px], scaled by Distance so the drag tracks the cursor at any zoom.
void PanPanelViewportCamera(PanelViewportCamera& Camera, float DeltaX, float DeltaY);

// 📝 Dolly / zoom by a wheel delta. Clamps Distance to a positive range.
void DollyPanelViewportCamera(PanelViewportCamera& Camera, float WheelDelta);

}   // namespace Frontier

#endif
