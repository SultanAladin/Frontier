/*==============================================================================================================================================
                                                          CAMERARESETTRIGGER.H
==============================================================================================================================================*/
// 🧩 The Home button in the shared control row — ports the mockup's `#home` (a house glyph that snaps to the isometric view). This unit owns only
//    the button's screen rectangle + hit test; the .cpp draws the house icon on the draw list and arms an Isometric snap when it fires. Header-only.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_CAMERARESETTRIGGER_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_CAMERARESETTRIGGER_H

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The Home button's placement within the control-row pill, filled by the layout each cycle.
struct CameraResetTrigger
{
    ImVec2 Centre;             // [px] - Button centre
    float  Radius = 18.0f;     // [px] - Button radius (.ctlBtn 36px diameter)
    bool   Hovered = false;    // [-]  - Cursor over the button this cycle
};


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 True when the cursor is within the circular button.
inline bool ResolveTriggerHovered(const ImVec2& Centre, float Radius, const ImVec2& Cursor)
{
    const float DeltaX = Cursor.x - Centre.x;
    const float DeltaY = Cursor.y - Centre.y;
    return (DeltaX * DeltaX + DeltaY * DeltaY) <= (Radius * Radius);
}

}   // namespace Frontier

#endif
