/*==============================================================================================================================================
                                                              RAILSTRIP.H
==============================================================================================================================================*/
// 🧩 The console's left column in its first slide: the filtered list of clusters, one row each, with the rules that divide the groups. Split out
//    because the filtering and the rule placement are the only genuinely non-obvious logic in the console — a cluster with nothing applicable is
//    dropped rather than dimmed, and dropping rows moves the dividers, so the two decisions cannot be made independently.
//
//    🔴 This is where the gate stopped being modelling's. ToolRailColumn took a StratumBit and called ResolveTileAvailability itself; RailStrip
//       takes a VerdictBinding and asks the resolver "is this action Omitted?" for each action, dropping a cluster only when EVERY action is
//       Omitted. Modelling's 3-state, construction's 5-state and paint's reveal conditions all reach the rail as the same Omitted/Gated/Live
//       answer, so one filter serves all three.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PANE_RAILSTRIP_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PANE_RAILSTRIP_H

#include "imgui.h"

#include "../Descriptor/ClusterDescriptor.h"
#include "../Predicate/VerdictResolver.h"
#include "../Token/PaletteSpecification.h"
#include "../Token/MetricsSpecification.h"

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A resolved rail holds at most one row per authored cluster, so the plan is sized to the catalogue ceiling rather than allocated. Modelling
//    authors 20 clusters; the headroom covers construction and paint without a revisit.
constexpr int ConsoleRailRowLimit = 48;    // [idx] - rows one resolved rail can carry


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One row that survived filtering. ClusterIndex points back into the AUTHORED table, not into this plan, so a filtered rail never makes the
//    caller translate indices back. LiveTally/ApplicableTally are the resolver's counts: how many of the cluster's actions are Live, and how many
//    are not Omitted.
struct ConsoleRailRow
{
    int  ClusterIndex;      // [idx]- index into the authored cluster table
    int  LiveTally;         // [idx]- actions the resolver reports Live
    int  ApplicableTally;   // [idx]- actions the resolver reports not Omitted
    bool SingleShot;        // [-]  - runs on click; has no page to open
    bool RuleAbove;         // [-]  - draw the dividing rule above this row
};


// 📝 The rail's rows for the current context, resolved before anything is drawn. Built as a plan rather than decided mid-loop because rule
//    placement needs to see the rows on BOTH sides of a divider, which a single forward pass drawing as it goes cannot.
struct ConsoleRailPlan
{
    ConsoleRailRow Rows[ConsoleRailRowLimit];   // [-]  - surviving rows, authored order
    int            RowCount;                    // [idx]- how many survived
    float          ContentHeight;               // [px] - total height incl. rules, for the scroll range
};


// 📝 What the rail reports back. A row can be hovered into (swaps the grid) or clicked (opens a cluster or fires a single-shot command) — kept
//    distinct because hovering must never fire a destructive command.
struct ConsoleRailOutcome
{
    int  HoveredCluster;    // [idx]- cluster the pointer rests on; -1 for none
    int  ActivatedCluster;  // [idx]- cluster clicked; -1 for none
    bool SingleShotFired;   // [-]  - the activated cluster is a command, not a page
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Resolve which clusters earn a row in this context, and where the dividing rules land once the omissions are accounted for. Standing comes from
// Gate, never from a mask read here.
// 🔴 Filtering and rule placement are ONE pass by necessity: a rule authored above a dropped cluster must migrate to the next surviving row of its
//    group, and a rule above a wholly-dropped group must not appear at all. Resolving these separately strands or doubles a rule.
[[nodiscard]] ConsoleRailPlan ResolveConsoleRailPlan(const ClusterDescriptor*    Clusters,
                                                     int                         ClusterCount,
                                                     const VerdictBinding&       Gate,
                                                     const MetricsSpecification& Metrics);

// Draw the resolved rail into the region at Origin and report what the pointer did. ScrollSuppressesHover mutes hover-to-swap while the rail is
// travelling — the pointer holds still but the rows move under it, so every row that passes would fire and the grid would thrash. A click always swaps.
ConsoleRailOutcome InscribeRailStrip(const SvgIconRegistry*      Icons,
                                     const ClusterDescriptor*    Clusters,
                                     const ConsoleRailPlan&      Plan,
                                     int                         OpenCluster,
                                     ImVec2                      Origin,
                                     float                       ColumnWidth,
                                     const PaletteSpecification& Palette,
                                     const MetricsSpecification& Metrics,
                                     bool                        ScrollSuppressesHover);

} // namespace Frontier

#endif
