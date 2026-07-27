/*==============================================================================================================================================
                                                              SPATIALCOMPASS.H
==============================================================================================================================================*/
// 🧩 The CAD spatial compass overlay — a 1:1 C++ port of Documentation/Mockups/ViewOrientationCube.html. It draws a translucent, orbit-
//    tracking cube in the top-right of a viewport surface (on top of the rendered scene, via the hosting window's ImGui draw list — CPU-projected,
//    no separate GPU pass), lets the user click a face to snap the camera straight-on to that view with an eased transition, drag the cube to
//    orbit the live camera, roll 90deg with four arrows, reset Home to isometric, and toggle Perspective ⇄ Orthographic. It reads + writes the
//    Interface-layer ViewportCamera the viewport already owns, so a face pick moves the real scene camera. One Construct call draws one instance;
//    a viewport that wants the compass holds one SpatialCompassState and calls ConstructSpatialCompass each cycle.
//
//    RUNS ON: the CPU for the projection + hit-testing (rotate eight-odd points, perspective-divide, point-in-quad) and the GPU only insofar as
//    ImGui rasterizes the resulting 2D draw-list primitives — exactly like every other Interface overlay. There is no dedicated render pass.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_SPATIALCOMPASS_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_SPATIALCOMPASS_H

#include "SpatialCompassContext.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Initialize a compass's persistent state: build the six face patches from the configuration and clear interaction /
//    transition state. Call once when the owning viewport is initialized.
void InitializeSpatialCompass(SpatialCompassState& State);

// 📝 Record + drive one cycle of the spatial compass. Projects the cube from the live camera, draws it (faces + captions +
//    roll arrows + the shared-background control row) onto Context.DrawList within the top-right of Context.SurfaceMin..Max,
//    applies pointer input (hover highlight, face-click snap, drag orbit, roll, Home, projection toggle), advances the eased
//    snap by Context.DeltaSeconds, and writes any camera change back into *Context.Camera. Returns this cycle's outcome.
IntersectionResult ConstructSpatialCompass(const SpatialCompassContext& Context,
                                           SpatialCompassState& State);

}   // namespace Frontier

#endif
