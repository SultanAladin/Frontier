/*==============================================================================================================================================
                                                        TOOLTILEGRID.H
==============================================================================================================================================*/
// 🧩 The card's right column in its first slide: the open band's tiles, four across, each carrying a glyph, a wrapped caption, a corner keystroke,
//    and — when its precondition is short — the shortfall that REPLACES that caption under the pointer. Drawn over invisible hit-test buttons
//    rather than through Selectable, which cannot express that layered content without fighting its own padding.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOOLCARD_TOOLTILEGRID_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOOLCARD_TOOLTILEGRID_H

#include "imgui.h"

#include "ToolCardSpecification.h"

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 What the grid reports back. A gated tile still reports its hover (the shortfall must show) but can never report activation —
//    that separation is the whole point of the three-state model, so it is enforced here rather than left to the caller.
struct ToolTileOutcome
{
    int HoveredTile;     // [idx]- tile the pointer rests on, authored index; -1 for none
    int ActivatedTile;   // [idx]- tile clicked, authored index; -1 for none
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The height the band's tiles occupy at this stratum — the scroll range, computed without drawing so the shell can size its body
// before it paints. Absent tiles are excluded, which is what makes this differ from a plain count-over-columns.
[[nodiscard]] float MeasureToolTileGridHeight(const ToolBandDescriptor& Band,
                                              unsigned int              StratumBit,
                                              float                     ColumnWidth,
                                              const ToolCardMetrics&    Metrics);

// Draw the band's applicable tiles into the region starting at Origin, and report what the pointer did.
// 🔴 An Absent tile is not drawn at all and takes no cell; a Gated tile IS drawn, greyed, and states its shortfall. Reporting an
//    activation for a gated tile would let a caller run a tool whose precondition is unmet, so this never reports one.
ToolTileOutcome InscribeToolTileGrid(const SvgIconRegistry*    Icons,
                                     const ToolBandDescriptor& Band,
                                     unsigned int              StratumBit,
                                     ImVec2                    Origin,
                                     float                     ColumnWidth,
                                     const ToolCardPalette&    Palette,
                                     const ToolCardMetrics&    Metrics);

} // namespace Frontier

#endif
