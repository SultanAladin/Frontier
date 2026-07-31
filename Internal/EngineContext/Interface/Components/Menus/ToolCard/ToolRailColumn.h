/*==============================================================================================================================================
                                                        TOOLRAILCOLUMN.H
==============================================================================================================================================*/
// 🧩 The card's left column in its first slide: the filtered list of bands, one row each, with the rules that divide the groups. Split out of the
//    card shell because the filtering and the rule placement are the only genuinely non-obvious logic in the whole card — a band with nothing
//    applicable is dropped rather than dimmed, and dropping rows moves the dividers, so the two decisions cannot be made independently.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOOLCARD_TOOLRAILCOLUMN_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOOLCARD_TOOLRAILCOLUMN_H

#include "imgui.h"

#include "ToolCardSpecification.h"

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A resolved rail can hold at most one row per authored band, so the plan is sized to the catalogue ceiling rather than
//    allocated. The modelling catalogue authors 20 bands; the headroom covers the drafting and paint cards without a revisit.
constexpr int ToolRailRowLimit = 48;    // [idx] - rows one resolved rail can carry


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One row that survived filtering. BandIndex points back into the AUTHORED table, not into this plan — every result the rail
//    reports is in the caller's own indexing, so a filtered rail never makes the caller translate indices back.
struct ToolRailRow
{
    int  BandIndex;         // [idx]- index into the authored band table
    int  LiveTally;         // [idx]- tiles whose precondition is met
    int  ApplicableTally;   // [idx]- tiles not Absent for this stratum
    bool SingleShot;        // [-]  - runs on click; has no page to open
    bool RuleAbove;         // [-]  - draw the dividing rule above this row
};


// 📝 The rail's rows for one stratum, resolved before anything is drawn. Built as a plan rather than decided mid-loop because the
//    rule placement needs to see the rows on BOTH sides of a divider, which a single forward pass that draws as it goes cannot.
struct ToolRailPlan
{
    ToolRailRow Rows[ToolRailRowLimit];   // [-]  - surviving rows, authored order
    int         RowCount;                 // [idx]- how many survived
    float       ContentHeight;            // [px] - total height incl. rules, for the scroll range
};


// 📝 What the rail reports back. A row can be hovered into (which swaps the grid) or clicked (which opens a band or fires a
//    single-shot command) — kept distinct because hovering must never fire a destructive command.
struct ToolRailOutcome
{
    int  HoveredBand;     // [idx]- band the pointer rests on; -1 for none
    int  ActivatedBand;   // [idx]- band clicked; -1 for none
    bool SingleShotFired; // [-]  - the activated band is a command, not a page
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Resolve which bands earn a row at this stratum, and where the dividing rules land once the omissions are accounted for.
// 🔴 Filtering and rule placement are ONE pass by necessity. A rule is authored above a band, but if that band is dropped the rule
//    must migrate to the next surviving row of its group — and if a whole group is dropped, the rule must not appear at all.
//    Resolving these separately strands a rule above nothing, or doubles two rules where one group vanished between them.
[[nodiscard]] ToolRailPlan ResolveToolRailPlan(const ToolBandDescriptor* Bands,
                                               int                       BandCount,
                                               unsigned int              StratumBit,
                                               const ToolCardMetrics&    Metrics);

// Draw the resolved rail into the region starting at Origin, and report what the pointer did.
// ScrollSuppressesHover mutes hover-to-swap while the rail is travelling: the pointer holds still but the rows move under it, so
// every row that passes would fire and the grid would thrash through several bands on a single wheel throw. A click always swaps.
ToolRailOutcome InscribeToolRailColumn(const SvgIconRegistry*    Icons,
                                       const ToolBandDescriptor* Bands,
                                       const ToolRailPlan&       Plan,
                                       int                       OpenBand,
                                       ImVec2                    Origin,
                                       float                     ColumnWidth,
                                       const ToolCardPalette&    Palette,
                                       const ToolCardMetrics&    Metrics,
                                       bool                      ScrollSuppressesHover);

} // namespace Frontier

#endif
