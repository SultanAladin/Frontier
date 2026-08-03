/*==============================================================================================================================================
                                                         MODELLINGTOOLPANEL.H
==============================================================================================================================================*/
// 🧩 The validation surface for the two-slide modelling tool card. No viewport and no polygon complex: a stratum row authors the selection profile
//    directly, which is the point — every band can be driven to its populated, gated and empty states in one click, where a real viewport would need
//    a modelled cage per case. The left column fakes the selection; the card itself is the thing under review.

#pragma once
#ifndef FRONTIER_VALIDATION_MODELLINGTOOLVALIDATION_MODELLINGTOOLPANEL_H
#define FRONTIER_VALIDATION_MODELLINGTOOLVALIDATION_MODELLINGTOOLPANEL_H

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"
#include "EngineContext/Interface/WorkspaceContextConsole/Pane/WorkspaceContextConsolePane.h"

#include "ModellingCatalogue.h"
#include "ModellingConsoleBridge.h"

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Everything the validation window keeps between frames. The console's own cross-frame state (which slide, which cluster, which action, the live
//    parameter readings) is held HERE rather than latched inside the component, so it can be driven from a key or restored to a cluster without the
//    component having to expose private storage. 🔴 The stratum is NOT part of the console's focus any more — the card used to carry StratumBit and
//    StratumName in its selection struct, but standing is now the workspace's business, so the active stratum lives in the bridge context the
//    resolver reads.
struct ModellingToolState
{
    unsigned int             StratumBit = 0u;   // [-] - the active selection stratum; the gate's whole input
    ModellingConsoleContext  Bridge     = {};   // [-] - the resolver's live context: stratum + the converted probe
    ConsoleFocus             Focus      = {};   // [-] - which cluster's grid + which action's options (console-owned)
    ConsoleCarousel          Carousel   = {};   // [-] - which slide, and where in the travel (console-owned)
    ParameterBlock           Parameters = {};   // [-] - the open action's live readings

    bool  CardOpen    = true;             // [-] - the card is showing
    float CardAnchorX = 0.0f;             // [px]- where the card sits; set on right-click
    float CardAnchorY = 0.0f;             // [px]

    // 📝 A right-click asking for the card, applied only AFTER the card has reported dismissal this frame. Deferred rather than acted
    //    on where it is detected: the one click is both "open here" to the panel and an outside press to an already-open card, so
    //    handling it immediately let the card's own dismissal close what the click had just opened.
    bool  ReanchorRequested = false;      // [-] - a right-click is waiting to place the card

    // 📝 The last commit, held so a click is observable in a UI-only app that has nothing to mutate.
    char LastCommittedLabel[64] = {};     // [-]  - label of the tool last applied
    char LastCommittedBand[48]  = {};     // [-]  - band it came from
    int  CommitTally            = 0;      // [idx]- how many commits have happened
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Seed the state to the prototype's own opening pose: the Face stratum, its authored selection size, the first populated band open.
void InitializeModellingToolSample(ModellingToolState& State);

// Record the whole validation surface: selection authoring on the left, the live card and its commit readout on the right.
void ConstructModellingToolPanel(const ThemeConfiguration& Theme, ModellingToolState& State, const SvgIconRegistry* Icons);

}   // namespace Frontier

#endif
