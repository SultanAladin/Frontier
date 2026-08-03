/*==============================================================================================================================================
                                                    CONSTRUCTIONCATALOGUEPANEL.H
==============================================================================================================================================*/
// 🧩 The validation surface for the two-slide construction catalogue. Unlike the modelling card, slide 1's LEFT pane is not a read-only probe
//    but the document-state CONTROL surface — the gate's inputs are the only things the reviewer can touch, so every change to the tile field is
//    visibly caused by a state change. 🔴 This target owns its own card shell (ConstructionCard) because the shell reaches the 5-state gate
//    against a live ConstructionDocument, which the shared ToolCard shell — baked to the 3-state StratumBit model — cannot express. The gate-
//    agnostic leaves (parameter column, probe glyph inscription, palette + metrics resolver) ARE reused; only the shell and the two panes it
//    drives are the target's own.

#pragma once
#ifndef FRONTIER_VALIDATION_CONSTRUCTIONCATALOGUE_CONSTRUCTIONCATALOGUEPANEL_H
#define FRONTIER_VALIDATION_CONSTRUCTIONCATALOGUE_CONSTRUCTIONCATALOGUEPANEL_H

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"
#include "EngineContext/Interface/WorkspaceContextConsole/Pane/WorkspaceContextConsolePane.h"

#include "ConstructionCatalogue.h"
#include "ConstructionConsoleBridge.h"

namespace ConstructionCatalogueValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Everything the validation window keeps between frames. The live document is the gate's whole input surface and the ONLY authored state —
//    every tile bucket is recomputed from it each frame, so the invariant holds by construction. The console owns the slide/open-cluster/open-
//    action state (ConsoleFocus + ConsoleCarousel) and the open action's parameter readings (ParameterBlock); the panel only holds them across
//    frames and hands them to the console. 🔴 The document-state editor (dimension chips, count steppers, condition toggles, presets, reveal) now
//    lives in the plain-ImGui scaffolding column, not inside the card — the shared console's slide-2 left is a passive probe area, and per the
//    "gate lift only" scope the interactive editor is scaffolding, not a console pane.
struct ConstructionCatalogueState
{
    ConstructionDocument     Document = {};   // [-] - the gate's live input surface; the reviewer edits it in the scaffolding column
    Frontier::ConsoleFocus   Focus    = {};   // [-] - which cluster's grid + which action's options the console shows (console-owned)
    Frontier::ConsoleCarousel Carousel = {};  // [-] - which slide, and where in the travel (console-owned)
    Frontier::ParameterBlock Parameters = {}; // [-] - the open action's live readings (console's block; empty this proof)

    ConstructionConsoleContext Bridge = {};   // [-] - the resolver's live context: document + catalogue + shortfall arena

    bool  CardOpen    = true;                 // [-] - the console is showing
    float CardAnchorX = 0.0f;                 // [px]- where the console sits; set on right-click
    float CardAnchorY = 0.0f;                 // [px]

    // 📝 A right-click asking for the console, applied only AFTER the console reported dismissal this frame — the one click is both "open here"
    //    and an outside press to an already-open console.
    bool  ReanchorRequested = false;          // [-] - a right-click is waiting to place the console

    // 📝 The last commit, latched so a click is observable in a UI-only app with nothing to mutate.
    char LastCommittedLabel[64]  = {};        // [-]  - label of the operation last applied
    char LastCommittedResult[48] = {};        // [-]  - the result type it produced (or "declared")
    int  CommitTally             = 0;         // [idx]- how many commits have happened
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Seed the state to the prototype's own opening pose: the default empty document, the first populated band open, browse slide.
void InitializeConstructionCatalogueSample(ConstructionCatalogueState& State);

// Record the whole validation surface: the placement field with the live card, plus a thin scaffolding column showing the commit
// readout. The card itself is the thing under review; the scaffolding uses plain ImGui so it can never be mistaken for the card.
void ConstructConstructionCataloguePanel(const Frontier::ThemeConfiguration& Theme,
                                         ConstructionCatalogueState&         State,
                                         const Frontier::SvgIconRegistry*    Icons);

}   // namespace ConstructionCatalogueValidation

#endif
