/*==============================================================================================================================================
                                                    WORKSPACECONTEXTCONSOLEPANE.H
==============================================================================================================================================*/
// 🧩 The console itself: the fixed-size surface, its two-slide carousel, both slides' pinned headers and footers, and the scrolling bodies the
//    strips draw into. This is the only unit that knows the console has two slides — each strip draws one pane and knows nothing of the other. A
//    workspace drives this with its own cluster table and VerdictBinding and gets the whole console; nothing here names a modelling, construction
//    or paint concept. Ported from ToolCard's ToolCardShell, with the gate lifted out: where the card held a StratumBit/StratumName and asked
//    ResolveTileAvailability itself, the console holds neither and routes every standing question through the descriptor's VerdictBinding.
//
//    🔴 Both slides are drawn on every frame while the track is mid-travel: the outgoing slide must remain visible as it leaves, or the console
//       appears to blank and repopulate rather than slide. They are clipped to the surface, not culled by which one is current.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PANE_WORKSPACECONTEXTCONSOLEPANE_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PANE_WORKSPACECONTEXTCONSOLEPANE_H

#include "imgui.h"

#include "../Descriptor/WorkspaceContextConsoleDescriptor.h"
#include "../Token/MetricsSpecification.h"
#include "ParameterPane.h"

namespace Frontier
{

struct SvgIconRegistry;
struct ThemeConfiguration;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which slide the carousel is showing and how far through the travel. Held by the CALLER, not in ImGui storage, so a workspace can drive the
//    console from its own state and answer "is an action open?" without asking the UI. Ported unchanged from ToolCardCarousel.
struct ConsoleCarousel
{
    bool  ShowingOptions;   // [-]  - true once an action is open; the slide the track is heading for
    float Travel;           // [-]  - 0 at the action slide, 1 at the options slide; eased toward the target
    float OpenAge;          // [s]  - seconds since the console opened, for the pop
};


// 📝 What the console has open. The gate-specific fields the card held here (StratumBit, StratumName, SelectedCount) are GONE — those describe a
//    workspace's selection and now live in the workspace's own document, reachable through the VerdictBinding's Context. What stays is only which
//    cluster's grid is shown and which action's parameters the options pane shows.
struct ConsoleFocus
{
    int OpenCluster;   // [idx]- cluster whose grid the action pane shows
    int OpenAction;    // [idx]- action whose parameters the options pane shows; -1 for none
};


// 📝 What the console reports after a cycle. Activation is reported ONCE, on the frame of the click. Ported from ToolCardResult; ActivatedCluster/
//    ActivatedAction replace ActivatedBand/ActivatedTile.
struct ConsoleResult
{
    int  ActivatedCluster;   // [idx]- cluster of the action that was committed; -1 for none
    int  ActivatedAction;    // [idx]- action within it; -1 when a single-shot cluster fired
    bool CommitRequested;    // [-]  - the options footer's commit button was pressed
    bool DismissRequested;   // [-]  - the console asked to close
    // 📝 The options footer's SECOND button, whatever the workspace named it. Reported separately from CommitRequested because the two are different
    //    acts on the same footer: paint's pair is Reset/Select, where Reset re-seeds the open action's values and leaves the slide standing, while
    //    the default pair is Cancel/Apply, where the left button only closes. See FooterSpecification on how a workspace states which it wants.
    bool RevertRequested;    // [-]  - the footer's left button asked the workspace to revert
    // 📝 True on any frame a parameter row's reading moved, so a workspace that previews live (paint re-stamps its stroke ribbon on every frame of a
    //    slider drag) knows to re-resolve without diffing the whole block itself.
    bool ReadingChanged;     // [-]  - a parameter reading changed this frame
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Advance the carousel one frame toward its target slide. Separated from the draw so a caller can step the animation on a frame it chooses not to
// paint, and so the easing is testable without a draw list. Uses Motion/Easing/EvaluateEasing for the carousel and pop curves.
void AdvanceConsoleCarousel(ConsoleCarousel& Carousel, float DeltaSeconds, const MetricsSpecification& Metrics);

// Draw the whole console at the descriptor's anchor and report what the reader did. Focus and Carousel are read AND written — the console owns the
// open cluster, the open action and the slide position across frames, because every one of them changes from inside the console's own hit targets.
// Standing for every action is resolved through Descriptor.Gate, never computed here.
ConsoleResult ConstructWorkspaceContextConsole(const SvgIconRegistry*                    Icons,
                                               const WorkspaceContextConsoleDescriptor& Descriptor,
                                               ImVec2                                   AnchorPosition,
                                               ConsoleFocus&                            Focus,
                                               ConsoleCarousel&                         Carousel,
                                               ParameterBlock&                          Parameters,
                                               const ThemeConfiguration&                Theme);

} // namespace Frontier

#endif
