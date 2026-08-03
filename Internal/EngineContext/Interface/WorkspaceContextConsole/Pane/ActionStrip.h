/*==============================================================================================================================================
                                                             ACTIONSTRIP.H
==============================================================================================================================================*/
// 🧩 The console's right column in its first slide: the open cluster's actions, four across, each carrying a glyph, a wrapped caption, a corner
//    keystroke, and — when its precondition is short — the shortfall that REPLACES that caption under the pointer. Drawn over invisible hit-test
//    buttons rather than through Selectable, which cannot express that layered content without fighting its own padding. Ported from ToolTileGrid.
//
//    🔴 The gate lifted here exactly as it did in the rail. ToolTileGrid took a StratumBit and computed each tile's ToolAvailability itself;
//       ActionStrip takes a VerdictBinding and reads each action's ActionVerdict. An Omitted action is not drawn and takes no cell; a Gated action
//       IS drawn, greyed, and states its Shortfall under the pointer; only a Live action reports activation. Reporting activation for a Gated
//       action would let a caller run something whose precondition is unmet — so, as in the card, this never reports one.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PANE_ACTIONSTRIP_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PANE_ACTIONSTRIP_H

#include "imgui.h"

#include "../Descriptor/ClusterDescriptor.h"
#include "../Predicate/VerdictResolver.h"
#include "../Predicate/SurfacePainter.h"
#include "../Token/PaletteSpecification.h"
#include "../Token/MetricsSpecification.h"

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 What the grid reports back. A gated action still reports its hover (the shortfall must show) but can never report activation — that separation
//    is the whole point of the three-state model, enforced here rather than left to the caller.
struct ConsoleActionOutcome
{
    int HoveredAction;     // [idx]- action the pointer rests on, authored index; -1 for none
    int ActivatedAction;   // [idx]- action clicked, authored index; -1 for none
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The height the cluster's actions occupy in this context — the scroll range, computed without drawing so the pane can size its body before it
// paints. Omitted actions are excluded, which is what makes this differ from a plain count-over-columns.
[[nodiscard]] float MeasureActionStripHeight(const ClusterDescriptor&    Cluster,
                                             const VerdictBinding&       Gate,
                                             float                       ColumnWidth,
                                             const MetricsSpecification& Metrics);

// Draw the cluster's applicable actions into the region at Origin and report what the pointer did. Standing for each action comes from Gate.
// 🔴 An Omitted action is not drawn and takes no cell; a Gated action IS drawn, greyed, states its Shortfall; only a Live action reports activation.
// 📝 Surfaces.PaintTile, when bound and when it claims a tile, replaces THAT tile's glyph with the workspace's own art (paint's nib well). Everything
//    else about the cell — the ground, the hit test, the caption, the shortfall, the keystroke — is unchanged, so a painted tile still reads as a tile.
ConsoleActionOutcome InscribeActionStrip(const SvgIconRegistry*      Icons,
                                         const ClusterDescriptor&    Cluster,
                                         const VerdictBinding&       Gate,
                                         const SurfaceBinding&       Surfaces,
                                         ImVec2                      Origin,
                                         float                       ColumnWidth,
                                         float                       PopScale,
                                         const PaletteSpecification& Palette,
                                         const MetricsSpecification& Metrics);

} // namespace Frontier

#endif
