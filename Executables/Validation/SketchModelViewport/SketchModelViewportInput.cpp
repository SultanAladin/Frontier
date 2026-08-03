/*==============================================================================================================================================
                                                    SKETCHMODELVIEWPORTINPUT.CPP
==============================================================================================================================================*/
// 🧩 The two sketch-viewport-local pointer seams declared in the header: a global wheel guard that keeps the camera from dollying while a draw
//    claims the wheel, and a right-drag orbit that layers over the shared panel's left+middle binding. Both read ImGui's live IO and drive the
//    render-canonical ViewportCamera through the shared Navigation verbs — no second camera, no matrices touched here.

#include "SketchModelViewportInput.h"

#include "SketchModelShapeDraw.h"

#include "EngineContext/Navigation/Camera/CameraNavigation/CameraNavigation.h"

#include "imgui.h"

namespace SketchModelViewportValidation
{

namespace
{
    // Match the shared viewport panel's left-drag orbit sensitivity exactly, so right-drag orbit feels identical to the standard bind.
    constexpr float OrbitRadiansPerPixel = 0.008f;   // [rad/px] - pointer delta → orbit angle
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      STICKY TOOL LATCH
//------------------------------------------------------------------------------------------------------------------------

void LatchSketchTool(SketchToolLatch& Latch, Frontier::ParametricSketchShapeCategory Category)
{
    Latch.Latched  = true;
    Latch.Category = Category;
}


void ClearSketchToolLatch(SketchToolLatch& Latch)
{
    Latch.Latched = false;
}


bool SketchToolLatched(const SketchToolLatch& Latch)
{
    return Latch.Latched;
}


void SustainSketchToolCycle(const SketchToolLatch& Latch, Frontier::ParametricSketchShapeStore& Store)
{
    // 🔴 The cycle boundary: a shape sealed this frame IFF a tool is latched but the store's draw fell idle. AdvanceShapeDraw clears DrawingEnabled on
    //    the completing click (one shape per arm), so re-arming the latched category here re-opens the draw for the next click — the tool never turns
    //    itself off. While a draw is still in progress (DrawingEnabled true) this is a no-op, so it never wipes half-placed points. Escape clears the
    //    latch first (ClearSketchToolLatch), so an Escaped tool is NOT re-armed here and the cycle genuinely ends.
    if (!Latch.Latched || Store.DrawingEnabled)
        return;

    ArmShapeDraw(Store, Latch.Category);   // fresh cycle: clears PendingPoints, sets the category, raises DrawingEnabled
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float HoldWheelFromCamera(bool SuppressCamera)
{
    ImGuiIO& Io = ImGui::GetIO();
    if (!SuppressCamera)
        return 0.0f;

    // 🔴 Capture THEN zero: the shared ConstructViewportPanel dollies on any hovered wheel notch, so the notch has to be gone from Io.MouseWheel
    //    before that call reads it. ImGui reseeds Io.MouseWheel from the platform each new frame, so this is self-restoring — no save/restore pair.
    const float Notches = Io.MouseWheel;
    Io.MouseWheel = 0.0f;
    return Notches;
}


void ApplyRightDragOrbit(Frontier::ViewportCamera& Camera, bool Hovered)
{
    const ImGuiIO& Io = ImGui::GetIO();

    // Only while the canvas owns the pointer and the right button is actually held. IsMouseDragging keys off the drag threshold + the accumulated
    // delta, so a single right click (used elsewhere as a dismiss) never nudges the camera; only a real drag orbits.
    if (!Hovered || !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        return;

    const ImVec2 Drag = Io.MouseDelta;
    if (Drag.x == 0.0f && Drag.y == 0.0f)
        return;

    // Same convention as the shared left-drag orbit: drag-right turns the view right (−Yaw), drag-down raises the eye. ConstrainPitch is inside the verb.
    Frontier::OrbitViewportCamera(Camera, -Drag.x * OrbitRadiansPerPixel, -Drag.y * OrbitRadiansPerPixel);
}

}   // namespace SketchModelViewportValidation
