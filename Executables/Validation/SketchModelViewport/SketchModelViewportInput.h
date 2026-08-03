/*==============================================================================================================================================
                                                    SKETCHMODELVIEWPORTINPUT.H
==============================================================================================================================================*/
// 🧩 The sketch viewport's own pointer + tool state, layered OVER the shared ConstructViewportPanel without touching it (the shared panel drives the
//    3D modeling view too, so its left+middle+wheel convention must stay put for every other consumer). Two concerns:
//
//      • HoldWheelFromCamera — a targeted wheel guard. ONLY while the polygon tool is actively drawn (center click seated) does the wheel retune the
//        side count instead of dollying: the shared panel dollies on any hovered wheel notch, so this captures the notches and zeroes Io.MouseWheel
//        BEFORE ConstructViewportPanel reads it, then hands the captured value to the draw. For every OTHER tool (and an unclicked polygon) it leaves
//        the wheel alone so zoom keeps working. Self-restoring — ImGui reseeds Io.MouseWheel each frame. Call once, right before ConstructViewportPanel.
//
//      • The sticky-tool latch — the last-committed Sketch tool stays active so the user draws the same primitive click after click; a seal re-arms it,
//        Escape or a right-click clears it. The cycle owner lives here (a viewport-interaction rule, not a property of the geometry model).

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELVIEWPORTINPUT_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELVIEWPORTINPUT_H

#include "ParametricSketchShapeStore.h"

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      STICKY TOOL LATCH
//------------------------------------------------------------------------------------------------------------------------

// 📝 The "last-selected tool stays active" latch — the cycle owner. A committed Sketch op LATCHES its category here; every time a shape SEALS the
//    panel re-arms the SAME category for the next cycle, so the user keeps drawing rectangles (or whatever) click after click without reopening the
//    menu. The latch defines the cycle boundary explicitly: Latched marks a tool is chosen, and each seal is one closed cycle that immediately opens
//    the next. Escape clears the latch to END the cycle (leave the tool). Lives here, not in the shared store, because sticky drawing is a viewport-
//    interaction rule, not a property of the geometry model.
struct SketchToolLatch
{
    bool                                   Latched  = false;                                          // [-] - a tool is held active
    Frontier::ParametricSketchShapeCategory Category = Frontier::ParametricSketchShapeCategory::Line;   // [-] - the held tool
    bool                                   CentreRect = false;                                        // [-] - the held Rectangle draws from its CENTRE (SketchCentreRect): 1st click centre, 2nd click corner, mirrored to the far corner at seal
};

// 📝 Latch a tool active (called when the console commits a Sketch op). Records the category and marks the latch held; the caller also arms the
//    store for the first cycle. Idempotent — re-latching the same tool just refreshes it.
void LatchSketchTool(SketchToolLatch& Latch, Frontier::ParametricSketchShapeCategory Category);

// 📝 Clear the latch — END the sticky cycle so no tool is held. Called on Escape (the user's "I'm done drawing these" gesture). A no-op if nothing
//    was latched.
void ClearSketchToolLatch(SketchToolLatch& Latch);

// 📝 Whether a tool is currently held active (the caller keeps the console + orbit suppressed while true, exactly as it does for an in-progress draw).
bool SketchToolLatched(const SketchToolLatch& Latch);

// 📝 Re-arm the store for the NEXT cycle after a seal. Call once per frame AFTER AdvanceShapeDraw: if a tool is latched but the store's draw has gone
//    idle (a shape just sealed, so DrawingEnabled fell to false), re-arm the latched category so the next click starts a fresh shape of the same kind.
//    This is what makes the tool "always active". A no-op while the latch is clear or a draw is still in progress (DrawingEnabled true).
void SustainSketchToolCycle(const SketchToolLatch& Latch, Frontier::ParametricSketchShapeStore& Store);


// 📝 Targeted wheel guard. If SuppressCamera is true (ONLY the actively-drawn polygon tool passes true), capture the frame's wheel notches, zero
//    Io.MouseWheel so the shared panel's dolly never fires, and RETURN the captured notches for the draw to spend (polygon side count). If false,
//    leave the wheel alone and return 0 so zoom works. Call ONCE per frame, immediately before ConstructViewportPanel, so the camera never sees a
//    notch the draw is claiming.
float HoldWheelFromCamera(bool SuppressCamera);

// 📝 Targeted LEFT-DRAG orbit guard. While a sketch tool is armed the left button belongs to the DRAW (click to seat a point), so a left-click-drag
//    must NOT also orbit the camera. The shared ConstructViewportPanel orbits off Io.MouseDelta whenever Io.MouseDown[0] is held (no Shift, no middle),
//    so this ZEROES Io.MouseDelta for exactly that gesture — a plain left-drag — BEFORE the panel reads it, killing the orbit while leaving the click,
//    the pan (middle / Shift-left), and the wheel untouched. A no-op unless SuppressLeftDrag is true (a tool is armed) AND the current gesture is a plain
//    left-drag; ImGui reseeds Io.MouseDelta each frame so this is self-restoring. Call ONCE per frame, immediately before ConstructViewportPanel.
void HoldLeftDragFromCamera(bool SuppressLeftDrag);

}   // namespace SketchModelViewportValidation

#endif
