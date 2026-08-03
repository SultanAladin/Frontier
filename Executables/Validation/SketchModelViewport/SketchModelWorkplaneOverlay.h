/*==============================================================================================================================================
                                                    SKETCHMODELWORKPLANEOVERLAY.H
==============================================================================================================================================*/
// 🧩 The viewport-overlay drawer for the authored construction planes. SketchModelViewport.exe does NOT wire the ParametricSketch GPU bridge, so
//    a workplane is drawn as an ImGui DrawList overlay projected into the canvas by the ONE viewport camera (SolveOrbitOrientation +
//    EvaluateProjectionFrame, the same chain the ground grid derives its constants from). It walks the summoned directory's outliner tree for
//    Workplane records, builds a Frontier::Workplane from each record's RecordProfile (the definition + display cues authored in the inspector),
//    solves its Z-up world frame + tessellates the sheet + grid (AssembleWorkplaneBodies), then projects every corner / grid endpoint world→clip→
//    screen and strokes the sheet fill + outline + lattice. Pure read-over-state: it mutates nothing, and draws nothing when no plane is authored.

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELWORKPLANEOVERLAY_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELWORKPLANEOVERLAY_H

#include "imgui.h"

namespace SceneDirectoryInspectorValidation { struct InspectorPanelState; }

namespace SketchModelViewportValidation
{

// The panel state carrying the one viewport camera + the summoned directory; defined in SketchModelViewportPanel.h (forward-declared here to keep
// this header includable from SketchModelSummonedSurfaces.h without a cycle — the overlay functions only take it by const reference).
struct SketchModelViewportState;

// 📝 The interactive workplane-draw phase. A committed References→Workplane in the action console ARMS the draw (Armed); the first ground click
//    seats corner A and begins the sweep (Sweeping); the second click confirms, and only THEN is the plane added to the outliner + history
//    (back to Idle). Escape / a right-click cancels an armed-or-in-progress draw with nothing added. The two corners are world MILLIMETRES on
//    the ground plane (Z=0), so the committed sheet's extent is the real dragged size — no scale slider.
enum class WorkplaneDrawPhase
{
    Idle,      // [-] - no draw in progress; the console arms it
    Armed,     // [-] - armed by the console commit; waiting for the first ground click
    Sweeping,  // [-] - corner A seated; the cursor drags corner B (live rubber-band rectangle)
};

// 📝 The cross-frame state of the interactive draw. Held on SketchModelSummonedState (it must survive between frames). CornerA / CornerB are the
//    two swept ground points in world mm (Z = 0). Held here, not in the outliner, because nothing is added until the second click confirms.
struct WorkplaneDrawState
{
    WorkplaneDrawPhase Phase = WorkplaneDrawPhase::Idle;   // [-]  - where in the draw the reader is
    float CornerAX = 0.0f, CornerAY = 0.0f;                // [mm] - first ground point (world mm, Z=0), seated on the first click
    float CornerBX = 0.0f, CornerBY = 0.0f;                // [mm] - live second ground point (the cursor while sweeping)
    bool  CornerBValid = false;                            // [-]  - the cursor is over the ground this frame (a grazing ray can miss it)
};

// Arm the draw (called when the console commits References→Workplane). A no-op reset to Armed; the actual picking happens in the overlay draw.
void ArmWorkplaneDraw(WorkplaneDrawState& Draw);

// Whether a draw is armed or in progress (the caller suppresses the console / other openers while true).
bool WorkplaneDrawActive(const WorkplaneDrawState& Draw);

// 📝 Draw every authored, displayed construction plane over the viewport canvas for one cycle. CanvasOrigin / CanvasSize are the absolute-screen
//    rect the grid renders into (captured by the panel), so the projection maps world→clip→exactly this rect. Reads State.Viewport.Camera for the
//    projection and State.Summoned.Directory for the authored planes (the outliner tree + the inspector's profile side-table). Call from inside the
//    canvas child, after ConstructViewportPanel and before the summoned cards, so the sheets draw over the grid but under the summoned surfaces.
void RenderSketchModelWorkplaneOverlay(const SketchModelViewportState& State, ImVec2 CanvasOrigin, ImVec2 CanvasSize);

// 📝 Advance the interactive draw for one cycle: cast the cursor to the ground, seat / sweep the corners, stroke the live rubber-band rectangle,
//    and — on the confirming second click — inject a Workplane record into Directory (outliner + profile side-table) sized to the dragged span and
//    log a history revision. Escape / right-click cancels. Called from inside the canvas child, right after RenderSketchModelWorkplaneOverlay, so
//    the rubber band draws over the committed sheets. Directory is the same InspectorPanelState the overlay reads its planes from.
void AdvanceWorkplaneDraw(const SketchModelViewportState&                        State,
                          SceneDirectoryInspectorValidation::InspectorPanelState& Directory,
                          WorkplaneDrawState&                                     Draw,
                          ImVec2 CanvasOrigin, ImVec2 CanvasSize);

}   // namespace SketchModelViewportValidation

#endif
