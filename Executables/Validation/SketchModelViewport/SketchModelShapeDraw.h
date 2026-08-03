/*==============================================================================================================================================
                                                        SKETCHMODELSHAPEDRAW.H
==============================================================================================================================================*/
// 🧩 The interactive click-to-draw for the 2D sketch primitives (Line / Rectangle / Circle / Ellipse / Polygon / Slot). A committed
//    Sketch* op in the action console ARMS a draw (ArmShapeDraw sets the store's DrawingCategory + DrawingEnabled); each subsequent ground
//    click seats one defining point until the category's defining count is met, at which point AppendParametricSketchShape seals the analytic
//    shape into the store — which records the edit-log/history entry itself. Between clicks the last point rubber-bands under the cursor and the
//    live analytic preview (ConstructParametricSketchShape) is stroked exactly as the sealed shape will be, so what you see is what you get.
//
//    🔴 The store (Frontier::ParametricSketchShapeStore) is the SINGLE source of truth: it owns the analytic shapes, the solver, and the
//       EditLog/Revision snapshot history (undo/redo). This unit only drives the picking + preview and calls the store's own append verb; the
//       outliner rows + History pane are projections reconciled from the store elsewhere. Nothing here duplicates geometry or history state.
//
//    All ground points are authored MILLIMETRES on the plane Z = 0 (the store's unit), cast + projected through SketchModelGroundProjection so
//    this draw and the workplane overlay agree on where a world point lands. Escape / right-click cancels an armed-or-in-progress draw.

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELSHAPEDRAW_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELSHAPEDRAW_H

#include "imgui.h"

#include "ParametricSketchShapeStore.h"

namespace SceneDirectoryInspectorValidation { struct InspectorPanelState; }

namespace SketchModelViewportValidation
{

// The panel state carrying the one viewport camera; defined in SketchModelViewportPanel.h (forward-declared to avoid an include cycle — the
// draw functions only take it by const reference).
struct SketchModelViewportState;

// Arm a primitive draw of Category: clears any pending points, sets the store's DrawingCategory, and raises DrawingEnabled. Called when the
// action console commits the matching Sketch* op. A no-op reset — the picking itself happens in AdvanceShapeDraw.
void ArmShapeDraw(Frontier::ParametricSketchShapeStore& Store, Frontier::ParametricSketchShapeCategory Category);

// Whether a draw is armed or in progress (the caller suppresses the console / other openers while true). Reads the store's DrawingEnabled.
bool ShapeDrawActive(const Frontier::ParametricSketchShapeStore& Store);

// 📝 Stroke every committed, displayed shape in the store over the viewport canvas for one cycle: flatten each via the store's own cache
//    (RetrieveCachedOutline — warms the flatten cache, so the store is non-const), project every point through the shared forward map, and draw
//    the outline + a semi-transparent fill for the closed families. Read-over-state save for the flatten memo; draws nothing when the store is
//    empty. Call from inside the canvas child, after RenderSketchModelWorkplaneOverlay and before AdvanceShapeDraw, so a committed shape sits
//    under the live rubber band.
void RenderSketchModelShapes(const SketchModelViewportState&       State,
                             Frontier::ParametricSketchShapeStore& Store,
                             ImVec2 CanvasOrigin, ImVec2 CanvasSize);

// 📝 Advance the interactive primitive draw for one cycle: cast the cursor to the ground (authored mm), seat a defining point on each left
//    click, rubber-band + analytically preview the shape-in-progress, and — once the category's defining count is reached — seal it via
//    AppendParametricSketchShape (which records the history entry). On a seal the new shape id is returned so the caller can mirror it into the
//    outliner; 0 on any non-sealing frame. Directory is threaded so a later mirror step can read it, but this unit does not mutate it. Escape /
//    right-click cancels. Call from inside the canvas child, after RenderSketchModelWorkplaneOverlay, so the rubber band draws over the sheets.
// 📝 WheelNotches is the frame's wheel movement AS CAPTURED by the panel's global wheel guard (HoldWheelFromCamera), passed in rather than read
//    from Io.MouseWheel here: the guard zeroed Io.MouseWheel so the same notch never also dollied the camera, so this unit must read the captured
//    value. Positive = wheel up. Only the polygon draw spends it (live side count); other categories ignore it.
uint32_t AdvanceShapeDraw(const SketchModelViewportState&                         State,
                          Frontier::ParametricSketchShapeStore&                   Store,
                          SceneDirectoryInspectorValidation::InspectorPanelState& Directory,
                          ImVec2 CanvasOrigin, ImVec2 CanvasSize, float WheelNotches);

// 📝 Mirror a freshly-sealed shape (ShapeId in Store) into the directory as a PROJECTION of the store: append an outliner row (Sketch
//    classification, cyan tint, titled from the shape) and record one History-panel revision (Sketch category) carrying the shape's title +
//    its edit-log detail. The store stays the source of truth — this only reflects it for display, called once per seal (never per frame). A
//    no-op when ShapeId is absent from the store.
void MirrorSketchShapeIntoDirectory(Frontier::ParametricSketchShapeStore&                   Store,
                                    SceneDirectoryInspectorValidation::InspectorPanelState& Directory,
                                    uint32_t                                                ShapeId);

}   // namespace SketchModelViewportValidation

#endif
