/*==============================================================================================================================================
                                                        TOOLCARDSHELL.H
==============================================================================================================================================*/
// 🧩 The card itself: the fixed-size surface, its two-slide carousel, both slides' pinned headers and footers, and the scrolling bodies the four
//    columns draw into. This is the only unit that knows a card has two slides — the columns each draw one pane and know nothing of the other.
//    A workspace drives this with its OWN tables and gets the whole card; nothing here names a modelling concept.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOOLCARD_TOOLCARDSHELL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOOLCARD_TOOLCARDSHELL_H

#include "imgui.h"

#include "ToolCardSpecification.h"
#include "ToolParameterColumn.h"
#include "ToolRailColumn.h"

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which slide the carousel is showing, and how far through the travel it is. Held by the CALLER rather than in ImGui storage so a
//    workspace can drive the card from its own state and answer "is a tool open?" without asking the UI.
struct ToolCardCarousel
{
    bool  ShowingOptions;   // [-]  - true once a tool is open; the slide the track is heading for
    float Travel;          // [-]  - 0 at the tool slide, 1 at the options slide; eased toward the target
    float OpenAge;         // [s]  - seconds since the card opened, for the pop
};


// 📝 What the card is currently authored against, and what it has open. Separated from the carousel because these change on selection
//    and on click, where the carousel changes on every frame of a transition.
struct ToolCardSelection
{
    unsigned int StratumBit;      // [-]  - the single bit of the active selection stratum
    const char*  StratumName;     // [-]  - badge key + footer word
    int          SelectedCount;   // [idx]- how many components are picked
    int          OpenBand;        // [idx]- band whose tiles the grid shows
    int          OpenTile;        // [idx]- tile whose parameters the options pane shows; -1 for none
};


// 📝 Everything one card is authored from. A workspace fills this once with its own static tables.
struct ToolCardDescriptor
{
    const char*                       Identifier;      // [-]  - ImGui id scope
    const ToolBandDescriptor*         Bands;           // [-]  - the catalogue (borrowed)
    int                               BandCount;       // [idx]
    const ToolProbeReadoutDescriptor* Probe;           // [-]  - readout for the active stratum; null draws no probe
    ImVec2                            AnchorPosition;  // [px] - top-left the card opens at
};


// 📝 What the card reports after a cycle. Activation is reported ONCE, on the frame of the click.
struct ToolCardResult
{
    int  ActivatedBand;      // [idx]- band of the tool that was committed; -1 for none
    int  ActivatedTile;      // [idx]- tile within it; -1 when a single-shot band fired
    bool CommitRequested;    // [-]  - the options footer's Apply was pressed
    bool DismissRequested;   // [-]  - the card asked to close
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Advance the carousel one frame toward its target slide. Separated from the draw so a caller can step the animation on a frame it
// chooses not to paint, and so the easing is testable without a draw list.
void AdvanceToolCardCarousel(ToolCardCarousel& Carousel, float DeltaSeconds, const ToolCardMetrics& Metrics);

// Draw the whole card and report what the reader did. Selection and Carousel are read AND written — the card owns the open band, the
// open tile and the slide position across frames, because every one of them changes from inside the card's own hit targets.
// 🔴 Both slides are drawn on every frame while the track is mid-travel: the outgoing slide must remain visible as it leaves, or the
//    card appears to blank and repopulate rather than to slide. They are clipped to the card, not culled by which one is "current".
ToolCardResult ConstructToolCard(const SvgIconRegistry*    Icons,
                                 const ToolCardDescriptor& Descriptor,
                                 ToolCardSelection&        Selection,
                                 ToolCardCarousel&         Carousel,
                                 ToolParameterBlock&       Parameters,
                                 const ThemeConfiguration& Theme);

} // namespace Frontier

#endif
