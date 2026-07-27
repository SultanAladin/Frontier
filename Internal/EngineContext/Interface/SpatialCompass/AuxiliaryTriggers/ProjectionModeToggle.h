/*==============================================================================================================================================
                                                          PROJECTIONMODETOGGLE.H
==============================================================================================================================================*/
// 🧩 The Perspective ⇄ Orthographic toggle in the shared control row — ports the mockup's `#projToggle`. It carries the round button rectangle,
//    the current mode (drives the icon + the filled "active" pill when orthographic), and its hover state. The mockup flips the cube stage's CSS
//    perspective (640px ⇄ 4000px); the port both flips the overlay's own projection focal length AND the live camera's orthographic mode so the
//    scene view matches. Header-only; the .cpp draws the glyph and, when it fires, flips OrthographicEnabled.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_PROJECTIONMODETOGGLE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_PROJECTIONMODETOGGLE_H

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The projection toggle's placement + state. OrthographicEnabled false = Perspective (default), true = Orthographic (the
//    "active" filled pill in the mockup). Placed within the control-row pill by the layout each cycle.
struct ProjectionModeToggle
{
    ImVec2 Centre;                      // [px] - Button centre
    float  Radius              = 18.0f; // [px] - Button radius (.ctlBtn 36px diameter)
    bool   Hovered             = false; // [-]  - Cursor over the button this cycle
    bool   OrthographicEnabled = false; // [-]  - false = Perspective (default), true = Orthographic
};

}   // namespace Frontier

#endif
