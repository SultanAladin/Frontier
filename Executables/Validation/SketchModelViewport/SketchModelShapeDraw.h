/*==============================================================================================================================================
                                                        SKETCHMODELSHAPEDRAW.H
==============================================================================================================================================*/
// 🧩 The interactive click-to-draw for the 2D sketch primitives. Two seal modes:
//      • FIXED-count families (Line / Rectangle / Centre-Rect / Circle / Ellipse / Arc / Polygon / Slot) seal automatically once their defining
//        count is reached (Line 2, Arc/Ellipse/Slot 3, …). Centre-Rect shares the Rectangle solver but reads its two clicks as centre + corner.
//      • OPEN-ENDED families (Polyline / Bezier / Spline) collect control points click after click and seal on a FINISH gesture — Enter, or a
//        left double-click — once a per-family minimum is met (Spline 3, the rest 2).
//    A committed Sketch* op in the action console ARMS a draw (ArmShapeDraw sets the store's DrawingCategory + DrawingEnabled); each subsequent
//    ground click seats one defining point, and the matching seal verb (AppendParametricSketchShape) records the edit-log/history entry itself.
//    Between clicks the last point rubber-bands under the cursor and the live analytic preview (ConstructParametricSketchShape) is stroked exactly
//    as the sealed shape will be, so what you see is what you get.
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

#include <cstdint>
#include <utility>
#include <vector>

namespace SceneDirectoryInspectorValidation { struct InspectorPanelState; }

namespace SketchModelViewportValidation
{

// The panel state carrying the one viewport camera; defined in SketchModelViewportPanel.h (forward-declared to avoid an include cycle — the
// draw functions only take it by const reference).
struct SketchModelViewportState;

// The one corner-edit drag modal + its pick-pending flag, both defined in SketchModelFilletModal.h / SketchModelSummonedSurfaces.h (forward-
// declared so this header stays cycle-free — AdvanceSketchFilletModal takes them by reference).
struct SketchModelFilletModal;

// The one offset drag modal, defined in SketchModelInsetModal.h (forward-declared — AdvanceSketchInsetModal takes it by reference).
struct SketchModelInsetModal;

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
// 📝 CentreRect selects the CENTRE-rectangle affordance for a Rectangle draw (the SketchCentreRect op): the first click is the box CENTRE and the
//    second is one corner, which is mirrored about the centre to seal a full centred box. Ignored for every non-Rectangle category. It rides the
//    viewport-local tool latch, not the store, so the geometry model stays unaware of this pure draw-gesture variant.
//    Draw-time SNAP is ALWAYS ON (no toggle, no hotkey): after the cursor is cast to the ground, the store's ResolveSnapCandidate scans every OTHER
//    displayed shape for an important point within a constant-on-screen catch radius (endpoint / midpoint / round-family centre / nearest point along
//    an outline) and, when one is caught, the seated point + rubber end snap exactly onto it and a marker glyph cues which target matched. A shape
//    never snaps to itself (the in-progress run is not yet in the store), so the magnet only ever catches already-committed geometry.
// 📝 Suppressed FREEZES the draw for the frame WITHOUT disarming the tool: no point is seated, no rubber band advances, and — critically — the
//    right-click cancel branch does NOT fire. The tool stays armed (DrawingEnabled untouched) and resumes the instant Suppressed clears. The panel
//    raises it while the Properties card is open (a right-click on a shape opened the card; the tool must pause, not cancel, until the card closes)
//    and on the frame a right-click PICK consumed the press (so that same press is not also read as a stroke-cancel here).
uint32_t AdvanceShapeDraw(const SketchModelViewportState&                         State,
                          Frontier::ParametricSketchShapeStore&                   Store,
                          SceneDirectoryInspectorValidation::InspectorPanelState& Directory,
                          ImVec2 CanvasOrigin, ImVec2 CanvasSize, float WheelNotches, bool CentreRect,
                          bool Suppressed);

// 📝 Advance WHOLE-SHAPE selection for one cycle when NO draw tool is armed (the idle-tool behaviour): cast the cursor to the ground, hover-pick the
//    shape under it (Store.Hovered, read by RenderSketchModelShapes for the dim-green trail), and on a left click resolve the pick into the store's
//    selection — a plain click REPLACES (single-select, or clears the set on an empty click), a Shift click TOGGLES the hit in/out of the ordered
//    SelectionSet (Store.Selected tracks its back()). The catch radius is held constant on screen (a pixel offset cast to mm), so a pick feels the
//    same at every zoom. A no-op while a Sketch* tool is armed (AdvanceShapeDraw owns the press then). Selected shapes stroke green in the render pass.
//    🔴 Sub-element (vertex / edge / face) picking is a later phase; this resolves the whole shape only, per the confirmed "whole-shape first" scope.
void AdvanceShapeSelection(const SketchModelViewportState&       State,
                           Frontier::ParametricSketchShapeStore& Store,
                           ImVec2 CanvasOrigin, ImVec2 CanvasSize);

// 📝 Advance SUB-ELEMENT (vertex / edge) picking against the SELECTED shape, for the CAD selection strata (2 vertex / 3 edge). Runs only when a
//    shape is selected (Store.Selected != 0) and VertexStratum/EdgeStratum requests it; casts the cursor to pixels through the SAME screen-space
//    projection AdvanceShapeSelection uses (perspective-correct, constant on-screen catch radius) and resolves the nearest defining point (vertex)
//    or outline segment (edge) of the selected shape. The result is written into the store's existing component-selection slots — AlignPickCategory
//    (0 none / 1 vertex / 2 edge) + AlignPickEndA/EndB (world mm) — the same slots the CAD Properties "align to selected edge" path already reads,
//    so nothing new is invented on the geometry model. ResolveActiveDimension reads those slots back to feed the construction gate its dimension.
//    On a HOVER it updates the slots for the highlight; on a left CLICK it latches the pick. Clears the slots (category 0) when the stratum is
//    WholeShape or nothing is selected. Draws the caught vertex diamond / edge overlay through the shared forward map. Call from inside the canvas
//    child, after AdvanceShapeSelection, gated by the panel on no summoned surface owning the press.
void AdvanceElementSelection(const SketchModelViewportState&       State,
                             Frontier::ParametricSketchShapeStore& Store,
                             bool VertexStratum, bool EdgeStratum,
                             ImVec2 CanvasOrigin, ImVec2 CanvasSize);

// 📝 Mirror a freshly-sealed shape (ShapeId in Store) into the directory as a PROJECTION of the store: append an outliner row (Sketch
//    classification, cyan tint, titled from the shape) and record one History-panel revision (Sketch category) carrying the shape's title +
//    its edit-log detail. The store stays the source of truth — this only reflects it for display, called once per seal (never per frame). A
//    no-op when ShapeId is absent from the store. The directory RecordToken issued for the new row is written to OutRowToken (0 on the no-op)
//    so the caller can record a ShapeId→token back-map — a right-click PICK needs it to open that row's Properties card.
void MirrorSketchShapeIntoDirectory(Frontier::ParametricSketchShapeStore&                   Store,
                                    SceneDirectoryInspectorValidation::InspectorPanelState& Directory,
                                    uint32_t                                                ShapeId,
                                    uint32_t&                                               OutRowToken);

// 📝 Reconcile the outliner rows against the store, called once per frame AFTER the tools have run. Drawn shapes mirror themselves incrementally on
//    seal; but Offset APPENDS fresh Profiles and Join CONSUMES curves into one shape entirely outside that seal path, so those results never touch the
//    outliner on their own. This pass closes that gap with three surgical fixes over the ShapeRowTokens (ShapeId → row token) back-map:
//      • a store shape with no row (an offset / join result) is mirrored in — landing in Profiles or Curves by its ClosedEnabled;
//      • a row whose ShapeId has left the store (join-consumed, cut, deleted) is removed, and its back-map pair dropped;
//      • a row now in the WRONG leaf folder (its shape's ClosedEnabled flipped — Join closed it, Cut opened it) is re-homed to the matching folder.
//    A pure projection of the store: it never mutates geometry, only the display tree + the back-map. Cheap — a linear pass over a handful of rows.
void ReconcileSketchDirectory(Frontier::ParametricSketchShapeStore&                    Store,
                              SceneDirectoryInspectorValidation::InspectorPanelState&  Directory,
                              std::vector<std::pair<uint32_t, uint32_t>>&              ShapeRowTokens);

// 📝 A right-click over the canvas that HITS a committed shape opens that shape's Properties card directly — the one-gesture replacement for
//    Tab → select the row → slide to inspect. Screen-space pick (the same ResolveScreenSpacePick the left-click selection uses), then: select the
//    shape in the store (so it also strokes green), map its ShapeId → directory RecordToken through ShapeRowTokens, set that token as the outliner
//    selection, and request the inspector open ON the inspect/properties face. Returns true IFF it consumed the right-click on a hit (the caller
//    then suppresses the stroke-cancel + console for that press). Idle-tool only: a no-op while a draw is armed (right-click cancels the stroke
//    there) — the caller gates on that. An empty right-click (no shape under the cursor) returns false and is left for the other consumers.
bool AdvanceShapePropertiesInvoke(const SketchModelViewportState&                          State,
                                  Frontier::ParametricSketchShapeStore&                    Store,
                                  SceneDirectoryInspectorValidation::InspectorPanelState&  Directory,
                                  const std::vector<std::pair<uint32_t, uint32_t>>&        ShapeRowTokens,
                                  ImVec2 CanvasOrigin, ImVec2 CanvasSize);

// 📝 Advance the BEVEL/CHAMFER corner-edit modal (the ported Plasticity `B` tool) for one cycle, and render its live preview + HUD. Two phases,
//    keyed off the modal's own Armed flag and PickPending:
//      • PICK-PENDING (a Fillet/Chamfer commit raised PickPending, no corner armed yet): hover-highlight the nearest DEFINING corner of the
//        selected — or, if none, the first — shape in screen pixels (the same projection AdvanceElementSelection uses); a left click Activates the
//        modal on that corner (which clears PickPending) and captures the drag origin. Esc / right-click abandons the pick.
//      • ARMED (a corner is captured): fold the cursor into a signed magnitude (project the pointer offset onto the interior bisector — INTO the
//        corner = fillet, OUT past it = chamfer), clamp to the safe limit, render the analytic preview (a sampled arc for a fillet, the TangentA→
//        TangentB edge for a chamfer) plus the corner marker + a readout pill, and on a left click / Enter COMMIT via FilletShapeCorner /
//        ChamferShapeCorner; Esc / right-click cancels with no change.
//    Returns true whenever it OWNED the press this cycle (pick click, commit, or cancel), so the panel vetoes the normal selection / draw on that
//    frame. A no-op returning false while both idle. Call from inside the canvas child, after AdvanceElementSelection, when no summoned surface
//    owns the press. PickPending is the caller's flag (SketchModelSummonedState) threaded in by reference so this unit clears it on Activate/abandon.
bool AdvanceSketchFilletModal(const SketchModelViewportState&       State,
                              Frontier::ParametricSketchShapeStore& Store,
                              SketchModelFilletModal&               Modal,
                              bool&                                 PickPending,
                              ImVec2 CanvasOrigin, ImVec2 CanvasSize);

// 📝 Advance the OFFSET drag modal for one cycle, and render its live preview + HUD. TWO-PHASE like the fillet modal: an Offset commit in the Q console
//    raised PickPending (no target yet). PHASE 1 (PickPending, not yet armed) hover-highlights the shape under the cursor and a left click Activates the
//    modal on THAT ONE shape (the edge / face pointed at) — "select the tool, then point and drag". PHASE 2 (armed) casts the cursor to the ground, folds
//    its radial distance from the target's centroid into a signed offset distance (typed digits override; a leading '-' / an inward drag deflates),
//    strokes the offset outline (ResolveSketchInsetModalPreview) in the guide tint, and on a left click / Enter COMMITs via AppendOffsetResult; Esc /
//    right-click cancels. STICKY: a fresh commit (CommitSerial advanced) re-raises PickPending so the tool stays active for the next edge / face. Returns
//    true whenever it OWNED the press this cycle (pick, commit, or cancel), so the panel vetoes the normal selection / draw that frame. PickPending is the
//    caller's flag (SketchModelSummonedState) threaded in by reference so this unit clears / re-raises it. A no-op returning false while fully idle.
bool AdvanceSketchInsetModal(const SketchModelViewportState&       State,
                             Frontier::ParametricSketchShapeStore& Store,
                             SketchModelInsetModal&                Modal,
                             bool&                                 PickPending,
                             ImVec2 CanvasOrigin, ImVec2 CanvasSize);

}   // namespace SketchModelViewportValidation

#endif
