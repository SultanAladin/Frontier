/*==============================================================================================================================================
                                                    SKETCHMODELSUMMONEDSURFACES.H
==============================================================================================================================================*/
// 🧩 The two summoned surfaces embedded in the sketch-model viewport, and the input arbitration that keeps them apart. Both are EMBEDDED, not
//    re-implemented: the directory + inspector card is the scene-directory inspector's own panel, and the action console is the shared
//    WorkspaceContextConsole driven by the construction catalogue's gate bridge. This unit holds only their cross-frame state and decides which
//    press reaches which surface.
//
//    🔴 The two openers are disjoint by construction. Tab opens the directory card (the inspector's own summon key, which is why the host must NOT
//       set ImGuiConfigFlags_NavEnableKeyboard — ImGui's keyboard nav consumes Tab for widget focus-cycling before any panel sees it). Right-click
//       over the canvas opens the action console: the shared ConstructViewportPanel binds only left + middle on its surface button, so the right
//       press is unclaimed by orbit / pan / dolly. Neither surface answers the other's press, so one press never opens two cards.
//
//    🔴 A summoned surface only opens while nothing else is summoned. The directory card draws its own dismiss veil and treats any outside left
//       press as a dismissal, so letting the console open underneath it would stack two modal surfaces over one canvas.

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELSUMMONEDSURFACES_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELSUMMONEDSURFACES_H

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"
#include "EngineContext/Interface/WorkspaceContextConsole/Pane/WorkspaceContextConsolePane.h"

#include "SceneDirectoryInspectorPanel.h"

#include "SketchModelWorkplaneOverlay.h"
#include "SketchModelShapeDraw.h"
#include "SketchModelCommandTools.h"
#include "SketchModelViewportInput.h"
#include "SketchModelFilletModal.h"
#include "SketchModelInsetModal.h"
#include "SketchModelExtrudeModal.h"
#include "SketchModelBooleanPopup.h"

#include "ConstructionCatalogue.h"
#include "ConstructionConsoleBridge.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace Frontier { struct SvgIconRegistry; }

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which topological STRATUM a pick resolves against — the CAD "selection mode" (Blender's 1/2/3, the CadWorkspace prototype's vertex/edge/face
//    filter). WholeShape is the historical behaviour (pick a whole ParametricSketchShape, green-highlighted); Vertex catches a shape's defining
//    control points; Edge catches an outline segment. The stratum is what feeds the construction gate its ActiveDimension: WholeShape on an open
//    curve reads Edge, on a closed profile reads Wire; a Vertex pick reads Vertex; an Edge pick reads Edge. Face is deferred — this 2D sketch
//    surface has no pickable face element yet (a closed profile's INTERIOR is the nearest thing, handled through the Wire reading). A viewport-
//    interaction rule, so it lives here beside the tool latch, NOT on the shared geometry store.
enum class SelectionStratum
{
    WholeShape = 0,   // [-] - pick a whole shape (the historical idle-tool behaviour)
    Vertex     = 1,   // [-] - pick a shape's defining control point
    Edge       = 2,   // [-] - pick a shape's outline segment
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Everything the two summoned surfaces keep between frames. The inspector half is one InspectorPanelState (its own directory tree, property
//    bags, revision store and carousel travel); the console half is the console's own cross-frame trio (Focus + Carousel + Parameters) plus the
//    gate bridge's live context and the document the gate reads.
struct SketchModelSummonedState
{
    // -- The directory + inspector card, opened on Tab (the inspector panel owns its own summon placement + travel) --
    SceneDirectoryInspectorValidation::InspectorPanelState Directory = {};   // [-] - tree + property cards + history

    // -- The action console, opened on a right-click over the canvas --
    ConstructionCatalogueValidation::ConstructionDocument       Document = {};   // [-] - the gate's whole input surface
    ConstructionCatalogueValidation::ConstructionConsoleContext Bridge   = {};   // [-] - resolver context: document + catalogue + shortfall arena
    Frontier::ConsoleFocus    ConsoleFocus    = {};   // [-]  - which cluster's grid + which action's options (console-owned)
    Frontier::ConsoleCarousel ConsoleCarousel = {};   // [-]  - which slide, and where in the travel (console-owned)
    Frontier::ParameterBlock  ConsoleReadings = {};   // [-]  - the open action's live readings (console's block)

    // 📝 The interactive workplane draw, armed by a References→Workplane commit in the console. While active it suppresses the console reopen and
    //    owns the canvas clicks (first click seats a corner, second confirms + adds the plane). Idle at rest — nothing drawn, nothing suppressed.
    WorkplaneDrawState WorkplaneDraw = {};   // [-] - the ground-sweep draw phase + its two corners (mm)

    // 📝 The 2D sketch-primitive store: the SINGLE source of truth for drawn shapes (analytic definitions + solver + EditLog/Revision history).
    //    A committed Sketch* op in the console arms a draw on it (DrawingEnabled/DrawingCategory); AdvanceShapeDraw seats points + seals shapes.
    //    The outliner rows + History pane are projections mirrored from this store. Empty at rest — nothing drawn, nothing suppressed.
    Frontier::ParametricSketchShapeStore ShapeStore = {};   // [-] - drawn primitives + their edit-log history (world mm)

    // 📝 Shape identity ⇄ outliner row. MirrorSketchShapeIntoDirectory issues a fresh directory RecordToken per sealed shape but the store's own
    //    ShapeId is not carried on the row, so a right-click PICK (which resolves a store ShapeId) needs this back-map to reach the row whose
    //    Properties card to open. One entry appended per seal; never pruned here (shapes are not deleted in this validation build). A ShapeId absent
    //    from the map means the pick landed on a shape that was never mirrored — the right-click then opens nothing.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ShapeRowTokens = {};   // [-] - store ShapeId -> directory RecordToken

    // 📝 The sticky-tool latch: the last-committed Sketch tool stays active, so the user keeps drawing the same primitive click after click. A commit
    //    latches it; every seal re-arms the SAME tool for the next cycle; Escape clears it. Held here (not the shared store) because sticky drawing is
    //    a viewport-interaction rule. Clear at rest — no tool held, nothing re-armed.
    SketchToolLatch ToolLatch = {};   // [-] - the held tool + its category (the cycle owner)

    // 📝 The active selection STRATUM (whole-shape / vertex / edge), cycled by the 1/2/3 hotkeys. It decides what a pick catches AND — through
    //    ResolveActiveDimension — the ConstructionDimension fed to the gate each frame, so the Q console's Sketch-Modify ops (Fillet / Chamfer /
    //    Trim / Extend / Offset) surface + gate live off the current selection instead of staying hidden under a pinned ActiveDimension = Nothing.
    //    A viewport-interaction rule (like ToolLatch), not a property of the geometry model. WholeShape at rest.
    SelectionStratum Stratum = SelectionStratum::WholeShape;   // [-] - the CAD selection mode (1 whole / 2 vertex / 3 edge)

    // 📝 The one Bevel/Chamfer modal (the Plasticity `B` tool). Armed by a Fillet/Chamfer commit in the Q console: the next canvas click picks a
    //    corner + Activates it, the drag sets the magnitude (sign chooses fillet↔chamfer), a confirm commits via FilletShapeCorner/ChamferShapeCorner.
    //    Held here (not the shared store) because it is a viewport-interaction modal. Idle at rest — nothing armed, nothing previewed.
    SketchModelFilletModal FilletModal = {};   // [-] - the corner-edit drag modal (Fillet/Chamfer)

    // 📝 Set on the frame a Fillet/Chamfer op commits in the console, before a corner is picked: the modal is "armed to pick" but not yet Activated
    //    on a corner. The next canvas click resolves the nearest corner vertex and Activates the modal on it. Cleared on Activate or cancel.
    bool FilletPickPending = false;   // [-] - a Fillet/Chamfer op was chosen; awaiting the corner pick

    // 📝 The last FilletModal.CommitSerial the panel logged a History revision for. The modal bumps CommitSerial once per fresh-corner commit; the
    //    panel compares it here and, when it advanced, records ONE Sketch revision ("Filleted / Chamfered corner N") — so each corner edit shows in
    //    the History panel like a drawn shape, while a redo-box re-adjust (which leaves the serial untouched) does NOT spam a new revision.
    uint32_t FilletHistorySerial = 0;   // [-] - last CommitSerial logged to the revision store

    // 📝 The four 2D MODIFY command tools (Trim / Cut / Join / Remove) as one sticky click-to-apply driver. Armed by the matching Sketch* commit in the
    //    Q console; the next canvas click applies the tool's verb through the store, and the tool STAYS ARMED for the next target (sticky), releasing on
    //    Escape / right-click / a new op. Held here (like FilletModal) because command-tool arming is a viewport-interaction rule. Idle at rest.
    SketchModelCommandToolState CommandTools = {};   // [-] - the armed command tool + its Join pick set + weld tolerance

    // 📝 The last CommandTools.ApplySerial the panel logged a History revision for — the command-tool twin of FilletHistorySerial. Bumped once per
    //    successful apply; the panel compares it here and records ONE Sketch revision per apply ("Trimmed / Cut / Joined / Removed shape N").
    uint32_t CommandHistorySerial = 0;   // [-] - last ApplySerial logged to the revision store

    // 📝 The one OFFSET drag modal (the ported `I` tool). Armed by a SketchOffset commit in the Q console on the selected closed shapes: the pointer's
    //    radial drag grows a signed offset distance (live preview), a confirm appends the offset Profiles (AppendOffsetResult, originals kept). Held here
    //    beside FilletModal because offset arming is a viewport-interaction rule. Idle at rest.
    SketchModelInsetModal InsetModal = {};   // [-] - the offset drag modal + its retained last commit (for the redo box)

    // 📝 Set on the frame an Offset op commits in the console, before a shape is picked — the offset twin of FilletPickPending. Offset now follows the
    //    Fillet/Chamfer TWO-PHASE flow (pick, then drag): the commit arms the tool "to pick" but does NOT Activate the drag; the NEXT canvas click over an
    //    edge / face resolves the hovered shape and Activates the modal on THAT ONE shape. Cleared on Activate or cancel. (Was a one-shot immediate arm on
    //    the whole selection; the pending phase makes "select tool, then point at the edge and drag" work like the chamfer tool.)
    bool InsetPickPending = false;   // [-] - an Offset op was chosen; awaiting the edge / face pick

    // 📝 The last InsetModal.CommitSerial the panel logged a History revision for — the offset twin of FilletHistorySerial. Bumped once per fresh
    //    commit; the panel compares it here and records ONE Sketch revision per offset ("Offset N shape(s)"); a redo-box slide leaves it untouched.
    uint32_t InsetHistorySerial = 0;   // [-] - last InsetModal.CommitSerial logged to the revision store

    // 📝 The one EXTRUDE sweep modal (the Blender `E` gesture). A SweepPrism commit in the Q console captures the selected profiles at ZERO height and
    //    the pointer's travel along the projected sweep axis grows it live; a click confirms, Esc / right-click restores the arm-time state. Held here
    //    beside InsetModal because extrude arming is a viewport-interaction rule.
    //    🔴 This modal writes the live height straight onto the shapes every frame (the growing solid IS the preview), which is why it — alone among the
    //       modals here — needs an arm-time snapshot to cancel against. See SketchModelExtrudeModal.h.
    SketchModelExtrudeModal ExtrudeModal = {};   // [-] - the extrude sweep drag modal + its retained last confirm

    // 📝 Set on the frame a SweepPrism op commits with NOTHING selected — the extrude twin of InsetPickPending. With a selection the commit arms the drag
    //    DIRECTLY (select → E → you are already extruding, Blender's order); with none there is no operand, so the tool waits and the next canvas click
    //    over a profile both picks it and starts the drag there. Cleared on Activate or cancel.
    bool ExtrudePickPending = false;   // [-] - a SweepPrism op was chosen with no selection; awaiting the profile pick

    // 📝 The console's Direction reading (Symmetric) stashed at commit time so a PHASE-1 pick arms with the SAME option a direct arm would have. Read only
    //    while ExtrudePickPending stands; meaningless otherwise.
    bool ExtrudeSymmetricPending = false;   // [-] - the pending pick should straddle the sketch plane

    // 📝 The last ExtrudeModal.CommitSerial the panel logged a History revision for — the extrude twin of InsetHistorySerial. One Sketch revision per
    //    confirmed sweep ("Extruded N shape(s)").
    uint32_t ExtrudeHistorySerial = 0;   // [-] - last ExtrudeModal.CommitSerial logged to the revision store

    // 📝 The one BOOLEAN popup. Unlike every other sketch op this is NOT armed from the Q console — it reconciles itself against the live selection
    //    each frame and opens the moment two closed shapes are selected, because a boolean's operands ARE the selection and no drag gesture is
    //    needed. Dismissable (Esc / right-click / Cancel) and it will not re-open for the operand set it remembers being declined.
    SketchModelBooleanPopup BooleanPopup = {};   // [-] - the boolean settings card + its operand order

    // 📝 The last BooleanPopup.CommitSerial the panel logged a History revision for — the boolean twin of InsetHistorySerial. One Sketch revision
    //    per applied boolean ("Union 2 shapes").
    uint32_t BooleanHistorySerial = 0;   // [-] - last BooleanPopup.CommitSerial logged to the revision store

    bool  ConsoleOpen    = false;   // [-]  - the console is showing (closed until a right-click asks for it)
    float ConsoleAnchorX = 0.0f;    // [px] - where the console sits; set from the pointer on summon
    float ConsoleAnchorY = 0.0f;    // [px]

    // 📝 A right-click asking for the console, applied only AFTER the console reported dismissal this frame — the one press is both "open here"
    //    and an outside press to an already-open console, so acting on it immediately would reopen what it just dismissed.
    bool  ConsoleReanchorRequested = false;   // [-] - a right-click is waiting to place the console

    // 📝 The canvas rect the console's right-click is armed over, in absolute screen coordinates, reported by the panel each frame so a press over
    //    a band is not read as a canvas press. A zero-extent field arms the whole viewport (what "no field reported yet" means on cycle one).
    float SummonFieldLeft   = 0.0f;   // [px] - left edge of the armed canvas rect
    float SummonFieldTop    = 0.0f;   // [px] - top edge
    float SummonFieldWidth  = 0.0f;   // [px] - 0 arms the whole viewport
    float SummonFieldHeight = 0.0f;   // [px] - 0 arms the whole viewport
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Seed both surfaces to their opening pose: an EMPTY directory (no rows, no selection) with an EMPTY history, a default construction document,
//    and the console closed. Call once before the frame loop.
void InitializeSketchModelSummonedSurfaces(SketchModelSummonedState& State);

// 📝 Report the canvas rect the console's right-click summon is armed over, in absolute screen coordinates. Call each frame from inside the canvas
//    child, before ConstructSketchModelSummonedSurfaces, so a press over a band or over the directory card is not mistaken for a canvas press.
void ConfineSketchModelSummonField(SketchModelSummonedState& State, ImVec2 CanvasOrigin, ImVec2 CanvasSpan);

// 📝 Record both summoned surfaces for one cycle and arbitrate their openers. Call ONCE per frame, after the viewport column has been recorded, so
//    each card draws over the canvas rather than under it. Neither surface draws anything while closed.
void ConstructSketchModelSummonedSurfaces(const Frontier::ThemeConfiguration& Theme,
                                          SketchModelSummonedState&           State,
                                          const Frontier::SvgIconRegistry*    Icons);

}   // namespace SketchModelViewportValidation

#endif
