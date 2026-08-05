/*==============================================================================================================================================
                                                    SKETCHMODELVIEWPORTINPUT.CPP
==============================================================================================================================================*/
// 🧩 The sketch-viewport-local input state declared in the header: a targeted wheel guard that keeps the camera from dollying only while the polygon
//    tool is retuning its side count, and the sticky-tool latch that keeps the last-committed tool active across draw cycles. Reads ImGui's live IO;
//    the latch drives the shared store's arm verb (ArmShapeDraw) for the next cycle.

#include "SketchModelViewportInput.h"

#include "SketchModelShapeDraw.h"

#include "imgui.h"

namespace SketchModelViewportValidation
{

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
    //    itself off. While a draw is still in progress (DrawingEnabled true) this is a no-op, so it never wipes half-placed points. Escape and right-click
    //    now BOTH cancel only the stroke and leave the latch intact, so a cancelled draw re-arms here into a fresh blank stroke of the same tool.
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


void HoldLeftDragFromCamera(bool SuppressLeftDrag)
{
    if (!SuppressLeftDrag)
        return;

    ImGuiIO& Io = ImGui::GetIO();

    // 🔴 Only a PLAIN left-drag orbits in the shared panel (Io.MouseDown[0] with no Shift, and no middle held — those are pan). Match that exact gesture
    //    and zero the frame's mouse delta so the panel's OrbitViewportCamera reads no movement, while the click itself (IsMouseClicked, MousePos) is
    //    untouched so the draw still seats its point. Middle / Shift-left pan and the wheel are left alone — this suppresses ONLY orbit. Self-restoring:
    //    ImGui reseeds Io.MouseDelta from the platform each new frame, so there is no save/restore to unwind.
    const bool PlainLeftDrag = Io.MouseDown[0] && !Io.KeyShift && !Io.MouseDown[2];
    if (PlainLeftDrag)
        Io.MouseDelta = ImVec2(0.0f, 0.0f);
}

}   // namespace SketchModelViewportValidation
