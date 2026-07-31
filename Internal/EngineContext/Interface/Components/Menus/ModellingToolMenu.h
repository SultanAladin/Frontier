/*==============================================================================================================================================
                                                            MODELLINGTOOLMENU.H
==============================================================================================================================================*/
// 🧩 The two-pane modelling-tool context menu. Follows Documentation/Prototypes/ModellingToolMenu.html exactly: a slim BAND rail on the left and a
//    GRID of tool tiles on the right, each pane a fixed-height header over its own independently scrolling body. Where TopologyActionMenu is one flat
//    scrolling list of every operation, this card splits the same catalogue into bands so a reader picks a family first and reads eight tiles rather
//    than scanning ninety rows — the rail is the index, the grid is the page.
//    Availability is the same three-state model the shipped gate resolves: a tool whose stratum does not apply is ABSENT from the grid, a tool whose
//    stratum applies but whose topology precondition is short is GATED (greyed, its shortfall replacing the label under the pointer), and everything
//    else is live. Stateless chrome — it renders a band table it does not own, and reports the tool that was activated.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_MODELLINGTOOLMENU_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_MODELLINGTOOLMENU_H

#include "imgui.h"

#include "../../Theme/ThemeConfiguration.h"
#include "PolygonMutation/PolygonGlyphIdentity.h"
#include "PolygonMutation/SelectionPredicate.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One tool tile. StrataMask is the same bitmask the operation gate uses, so a tool declares every selection it serves in one
//    field; Shortfall is the precondition text shown when the tool applies to the stratum but cannot run — null means it always
//    can. Keystroke is drawn in the tile corner and may be null.
struct ModellingToolDescriptor
{
    const char*  Label;         // [-] - Tile caption
    const char*  Keystroke;     // [-] - Accelerator drawn in the tile corner; null for none
    const char*  Shortfall;     // [-] - Why it cannot run ("needs a quad ring"); null when always available
    PolygonGlyph Glyph;         // [-] - Tile artwork
    StratumMask  StrataMask;    // [-] - Selections this tool serves
};


// 📝 One rail row. A band with Tools drives the grid; a band with none is a SINGLE-SHOT row that runs on click (Delete, Dissolve)
//    — destructive, optionless commands with no page to open, which is why they sit below the rail's separator rather than
//    occupying a grid of one tile. SeparatorAbove draws the rule that divides the two groups.
struct ModellingBandDescriptor
{
    const char*                   Caption;         // [-]  - Rail row label, also the grid header title
    const char*                   Keystroke;       // [-]  - Accelerator, single-shot rows only; null otherwise
    PolygonGlyph                  Glyph;           // [-]  - Rail row artwork, also the grid header badge
    const ModellingToolDescriptor* Tools;          // [-]  - Tiles this band opens (borrowed); null makes it single-shot
    int                           ToolCount;       // [idx]- Number of tiles
    StratumMask                   StrataMask;      // [-]  - Single-shot rows only: the selections they serve
    bool                          SeparatorAbove;  // [-]  - Draw the dividing rule above this row
};


// 📝 Everything one card draws. The band table is borrowed, and the stratum plus selected count are the selection profile the
//    header states — the menu resolves availability from them but never owns them.
struct ModellingToolMenuDescriptor
{
    const char*                     Identifier;      // [-]  - Unique id (scopes the ImGui window)
    TopologyStratum                 ActiveStratum;   // [-]  - Drives the header badge, the title, and every availability test
    int                             SelectedCount;   // [idx]- Shown in the left header's pill
    const ModellingBandDescriptor*  Bands;           // [-]  - Rail rows (borrowed, not owned)
    int                             BandCount;       // [-]  - Number of rail rows
    ImVec2                          AnchorPosition;  // [px] - Screen point the card grows from
    float                           HeightLimit;     // [px] - Card height cap; 0 takes the component default
};


// 📝 What the card reports back. ActivatedBand/ActivatedTool index the descriptor tables; a single-shot row reports its band with
//    ActivatedTool left at -1, which is how a caller tells "Delete" from a tile inside a band.
struct ModellingToolMenuResult
{
    int  ActivatedBand;        // [idx] - Band whose tool ran this cycle; -1 when none
    int  ActivatedTool;        // [idx] - Tile within that band; -1 when a single-shot row ran
    int  OpenBand;             // [idx] - Band the grid is currently showing
    bool DismissRequested;     // [-]   - Pointer went down outside the card, or Escape was pressed
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record one modelling-tool card. OpenBand is carried in and out through the result so the caller owns which page is showing —
//    a component that latched it internally could not be driven by a keyboard or restored with a menu.
ModellingToolMenuResult ConstructModellingToolMenu(const ThemeConfiguration&          Theme,
                                                   const ModellingToolMenuDescriptor& Descriptor,
                                                   int&                               OpenBand);

// 📝 Whether a tool applies to a stratum at all. A false result means the tile is ABSENT, not greyed — the distinction the
//    three-state model rests on, exposed so a caller can tally the same way the card draws.
[[nodiscard]] bool ResolveToolApplicability(const ModellingToolDescriptor& Tool, TopologyStratum Stratum);

// 📝 The first band with at least one applicable tool, so a caller never opens the card onto an empty grid. -1 when the whole
//    table is inapplicable to the stratum.
[[nodiscard]] int ResolveFirstPopulatedBand(const ModellingBandDescriptor* Bands, int BandCount, TopologyStratum Stratum);

}   // namespace Frontier

#endif
