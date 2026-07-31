/*==============================================================================================================================================
                                                          POLYGONACTIONPANEL.H
==============================================================================================================================================*/
// 🧩 The validation surface for the polygon-mutation context menu. No viewport and no polygon complex: buttons and toggles author a
//    SelectionProfile directly, which is the point — every gate class can be driven to both its passing and its failing side in one click, where a
//    real viewport would need a modelled cage per case. The left column fakes the selection; the right shows the resolved menu and the availability
//    tally that proves the gate is doing something.

#pragma once
#ifndef FRONTIER_VALIDATION_POLYGONACTIONVALIDATION_POLYGONACTIONPANEL_H
#define FRONTIER_VALIDATION_POLYGONACTIONVALIDATION_POLYGONACTIONPANEL_H

#include "EngineContext/Interface/Components/Menus/TopologyActionMenu.h"
#include "EngineContext/Interface/Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr int ResolvedEntryCapacity = 128;    // [idx] - Upper bound on rows one menu can show; the catalogue is smaller than this


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Everything the validation window keeps between frames: the authored selection, the display switches, and the last activation
//    so a click leaves visible evidence. Caller-owned, exactly as the component idiom requires.
struct PolygonActionState
{
    SelectionProfile Profile;                          // [-] - The authored selection the gate reads

    bool  RevealGatedEntries = true;                   // [-] - Show greyed rows with their reason, or only live rows
    bool  MenuOpen           = true;                   // [-] - The card is showing
    float MenuAnchorX        = 0.0f;                   // [px]- Where the card grows from; set on right-click
    float MenuAnchorY        = 0.0f;                   // [px]

    // 📝 A right-click asking for the menu, applied only AFTER the card has reported dismissal this frame. Deferred rather than
    //    acted on where it is detected: the one click is both "open here" to the panel and an outside press to an already-open
    //    card, so handling it immediately let the card's own dismissal close what the click had just opened.
    bool  ReanchorRequested  = false;                  // [-] - A right-click is waiting to place the card

    // 📝 The last activation, held so a click is observable in a UI-only app that has nothing to mutate.
    char LastActivatedLabel[64] = {};                  // [-] - Label of the operation last activated
    char LastBranchOption[48]   = {};                  // [-] - Branch option chosen, when the activation came from a branch
    int  ActivationTally        = 0;                   // [idx]- How many activations have happened

    // 📝 Scratch storage for the resolved rows, refilled every frame. Lives in the state rather than on the stack because
    //    ResolvedEntryCapacity entries is ~3 KB and a deep ImGui call stack has better uses for it.
    ResolvedOperationEntry Entries[ResolvedEntryCapacity] = {};
    int                    EntryCount                     = 0;
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record the whole validation surface: selection authoring on the left, the live menu and tally on the right.
void ConstructPolygonActionPanel(const ThemeConfiguration& Theme, PolygonActionState& State);

}   // namespace Frontier

#endif
