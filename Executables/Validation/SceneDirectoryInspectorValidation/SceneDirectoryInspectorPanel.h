/*==============================================================================================================================================
                                                    SCENEDIRECTORYINSPECTORPANEL.H
==============================================================================================================================================*/
// 🧩 The drawing half of the summoned scene-directory inspector. It owns the summon (Tab / right-click over a bare viewport), the outer
//    directory ⇄ inspect carousel, the inner Properties ⇄ History carousel, the metadata pane, the property cards with every field widget,
//    and the branching history rail (branch pills · timeline · undo / redo). 🔴 The directory ROWS are NOT drawn here: the rail hosts the
//    reused SketchOutliner panel (ConstructSketchOutlinerPanel over State.Directory), which owns rows / selection / rename / drag / eye /
//    filter / menus. This half reads the selection SketchOutliner surfaces and drives the cards + history off it, keeping a profile side-table
//    keyed by the SketchOutliner token. All caller-owned state lives in one InspectorPanelState for the window's life; the two entry points
//    seed it once and draw one frame. Types live in the app-local namespace SceneDirectoryInspectorValidation, never Frontier.

#pragma once
#ifndef FRONTIER_VALIDATION_SCENEDIRECTORYINSPECTOR_PANEL_H
#define FRONTIER_VALIDATION_SCENEDIRECTORYINSPECTOR_PANEL_H

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"

#include "SceneDirectoryInspector.h"
#include "EngineContext/Interface/WorkspaceHost/SketchOutliner/SketchOutlinerPanel.h"

#include <utility>
#include <vector>

namespace Frontier { struct SvgIconRegistry; }

namespace SceneDirectoryInspectorValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STATE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which inner inspector face is shown (the pc-seg segmented control / inspectTrack.show-history).
enum class InspectorFace { Properties, History };

// 📝 Everything the inspector keeps between frames. The reused SketchOutliner directory (tree + selection + rename + filter, all caller-owned),
//    a profile side-table keyed by SketchOutliner token, the branching revision store, the carousel travel (outer + inner, eased), and the
//    summon placement + open state. The directory + profiles + revisions are the authored state; every pane is rebuilt from them each frame.
struct InspectorPanelState
{
    // -- Directory (the reused SketchOutliner owns the tree, selection, rename, drag, eye, filter, menus) --
    Frontier::SketchOutlinerUi::SketchOutlinerState Directory;   // [-]  - the recordStore + all row interaction

    // -- Property side-table: the reused tree node carries no profile bag, so profiles hang off the token here --
    std::vector<std::pair<RecordToken, RecordProfile>> Profiles;  // [-] - token -> established property bag

    // -- Selection echo: the token whose cards are currently shown, to detect a selection change across frames --
    RecordToken              ShownToken = 0;             // [-]  - last token the cards were resolved for (0 = none)

    // -- Branching revision store --
    RevisionStore            Revisions;                  // [-]  - the revisionStore (branches + active + fork-on-record)

    // -- Add-record fold (the metadata action list's in-place classification grid) --
    bool                     AddFoldOpen = false;        // [-]  - addFoldOpen

    // -- Carousel travel (outer directory<->inspect, inner properties<->history), eased 0..1 --
    InspectorFace            Face = InspectorFace::Properties; // [-] - which inner face
    bool                     OnInspect = false;          // [-]  - outer slide: false=directory, true=inspect (menuTrack.to-inspect)
    float                    OuterTravel = 0.0f;         // [-]  - 0 at directory, 1 at inspect; eased toward OnInspect
    float                    InnerTravel = 0.0f;         // [-]  - 0 at properties, 1 at history; eased toward Face

    // -- Inner-pane vertical scroll: the Properties + History bodies draw into a fixed clip rect, so tall content (a Workplane's two
    //    cards, a long timeline) needs its own scroll offset. Held per face, clamped each frame to [0, content - viewport], fed by the
    //    wheel while the pane is hovered. --
    float                    PropertiesScroll = 0.0f;    // [px] - Properties body scroll offset (0 = top)
    float                    HistoryScroll    = 0.0f;    // [px] - History body scroll offset (0 = top)

    // -- Summon placement + open --
    bool                     SummonOpen = false;         // [-]  - the card is showing (summonOpen)
    float                    SummonX = 0.0f;             // [px] - card top-left (clamped on summon)
    float                    SummonY = 0.0f;             // [px]
    float                    OpenAge = 0.0f;             // [s]  - seconds since summon, for the pop

    // -- Deferred summon request (a right-click / Tab arriving this frame, applied after the card reports dismissal) --
    bool                     SummonRequested = false;    // [-]  - a summon is waiting to place the card
    float                    RequestX = 0.0f;            // [px] - where it wants to open
    float                    RequestY = 0.0f;            // [px]

    // 📝 One-shot: the pending summon should land ON the inspect/Properties face rather than slide 1 (the directory). A right-click PICK
    //    that already resolved a row sets this so the card opens straight on that row's Properties, skipping the directory step. The applier
    //    honours it AFTER placement (only when a selection exists) and self-clears it, so a plain Tab summon still opens on the directory.
    bool                     OpenOnInspect = false;      // [-]  - deferred summon opens on inspect, not the directory

    // -- Per-card fold memory (foldMemory: classification/title -> collapsed) is small + string-keyed; kept as a parallel vector --
    std::vector<std::string> CollapsedCards;             // [-]  - "classification/Title" keys the reviewer collapsed
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The profile bag for a token in the side-table, inserting a fresh (unpopulated) one if absent. Stable across the call (vector re-alloc
// is fine — the reference is taken after any insert).
RecordProfile& ProfileFor(InspectorPanelState& State, RecordToken Token);

// The SDI classification a token's row carries. The directory runs the SDI content profile, so a row's own opaque ClassificationId IS the
// classification — this reads it off the tree, and reports Solid (a safe card schema) when the token resolves to no row.
RecordClassification KindFor(const InspectorPanelState& State, RecordToken Token);

// Seed the state to its opening pose: an EMPTY directory (no rows, no selection) and an EMPTY history (one "Trunk" branch carrying no
// revisions). Everything in the tree arrives through the outliner's add menu; every revision arrives from an observed tree delta.
// 🔴 The history needs the one empty branch, not zero branches: every revision verb early-returns unless Active indexes a live branch,
//    so a branch-less store would swallow each LogRevision silently. Call once before the frame loop.
void InitializeInspectorSample(InspectorPanelState& State);

// Draw the whole inspector for one frame over the current viewport. Handles the Tab / right-click summon, both carousels, the directory
// rows + interactions, the property cards, and the history rail. Icons supplies the reference glyphs (a null / empty registry falls back
// to text, so the panel still renders). Call inside a docked-full host window each frame.
void ConstructSceneDirectoryInspectorPanel(const Frontier::ThemeConfiguration& Theme,
                                           InspectorPanelState&                State,
                                           const Frontier::SvgIconRegistry*    Icons);

}   // namespace SceneDirectoryInspectorValidation

#endif
