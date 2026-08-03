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
#include "SketchModelViewportInput.h"

#include "ConstructionCatalogue.h"
#include "ConstructionConsoleBridge.h"

namespace Frontier { struct SvgIconRegistry; }

namespace SketchModelViewportValidation
{

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

    // 📝 The sticky-tool latch: the last-committed Sketch tool stays active, so the user keeps drawing the same primitive click after click. A commit
    //    latches it; every seal re-arms the SAME tool for the next cycle; Escape clears it. Held here (not the shared store) because sticky drawing is
    //    a viewport-interaction rule. Clear at rest — no tool held, nothing re-armed.
    SketchToolLatch ToolLatch = {};   // [-] - the held tool + its category (the cycle owner)

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
