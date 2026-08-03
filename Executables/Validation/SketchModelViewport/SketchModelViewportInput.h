/*==============================================================================================================================================
                                                    SKETCHMODELVIEWPORTINPUT.H
==============================================================================================================================================*/
// 🧩 The sketch viewport's own pointer bindings, layered OVER the shared ConstructViewportPanel without touching it (the shared panel drives the
//    3D modeling view too, so its left+middle+wheel convention must stay put for every other consumer). Two seams:
//
//      • HoldWheelFromCamera — a global wheel guard. While an interactive draw is armed (a polygon's side count rides the wheel, a slot's future
//        arc bias could too), the wheel MUST NOT also dolly the camera: the shared panel dollies on any hovered wheel notch, so this captures the
//        notches and zeroes Io.MouseWheel BEFORE ConstructViewportPanel reads it, then hands the captured value to the draw. Restored the next
//        frame automatically (ImGui reseeds Io.MouseWheel per frame). Call once, right before ConstructViewportPanel, every frame.
//
//      • ApplyRightDragOrbit — right-drag orbits the camera, matching the user's muscle memory (right-click-drag = look around). The shared surface
//        button binds only left+middle, so a right press is unclaimed there; this reads the raw right-button delta while the canvas is hovered and
//        turns it into an OrbitViewportCamera call, the same verb + sensitivity the shared left-drag orbit uses. Sketch-viewport-only by living here.

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELVIEWPORTINPUT_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELVIEWPORTINPUT_H

#include "ParametricSketchShapeStore.h"

namespace Frontier { struct ViewportCamera; }

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


// 📝 Global wheel guard. If SuppressCamera is true (a draw is armed), capture the frame's wheel notches, zero Io.MouseWheel so the shared panel's
//    dolly never fires, and RETURN the captured notches for the draw to spend (e.g. polygon side count). If false, leave the wheel alone and
//    return 0. Call ONCE per frame, immediately before ConstructViewportPanel, so the camera never sees a notch the draw is claiming.
float HoldWheelFromCamera(bool SuppressCamera);

// 📝 Right-drag orbit for this viewport only. While the canvas is Hovered, a held right button's pointer delta orbits the camera through the same
//    OrbitViewportCamera verb + sensitivity the shared left-drag uses. A no-op when not hovered or the button is up. Call after ConstructViewportPanel
//    (so the shared surface button has already claimed hover) each frame.
void ApplyRightDragOrbit(Frontier::ViewportCamera& Camera, bool Hovered);

}   // namespace SketchModelViewportValidation

#endif
