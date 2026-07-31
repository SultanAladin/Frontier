/*==============================================================================================================================================
                                                          PROJECTIONMODETOGGLE.H
==============================================================================================================================================*/
// 🧩 The Perspective ⇄ Orthographic toggle in the shared control row — ports the mockup's `#projToggle`. It carries the round button rectangle and
//    its hover state, and NOTHING ELSE: the live ViewportCamera's Projection field is the single source of truth for which lens is active, so the
//    icon, the filled "active" pill, and the overlay's own focal length are all derived from the camera each cycle. The mockup flips the cube stage's
//    CSS perspective (640px ⇄ 4000px); the port routes the flip through AlignProjectionMode so the scene lens and the widget cannot disagree.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_PROJECTIONMODETOGGLE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_PROJECTIONMODETOGGLE_H

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The projection toggle's placement + hover only. Which lens is active is NOT stored here — it is read from the live camera's
//    Projection field, so there is exactly one truth. (A duplicate OrthographicEnabled bool lived here once; because the Views
//    menu wrote Camera.Projection directly while the compass wrote only its own copy, the two drifted apart and the cube's icon
//    could show a different lens than the viewport was rendering.) Placed within the control-row pill by the layout each cycle.
struct ProjectionModeToggle
{
    ImVec2 Centre;              // [px] - Button centre
    float  Radius  = 18.0f;     // [px] - Button radius (.ctlBtn 36px diameter)
    bool   Hovered = false;     // [-]  - Cursor over the button this cycle
};

}   // namespace Frontier

#endif
