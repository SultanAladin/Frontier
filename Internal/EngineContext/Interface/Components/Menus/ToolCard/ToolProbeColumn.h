/*==============================================================================================================================================
                                                        TOOLPROBECOLUMN.H
==============================================================================================================================================*/
// 🧩 The card's left column in its SECOND slide: the measurement readout for what is currently selected, replacing the rail once a tool is open. Pure
//    output — it has no hit targets and reports nothing back, which is why it takes no outcome struct where every other column does.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOOLCARD_TOOLPROBECOLUMN_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_MENUS_TOOLCARD_TOOLPROBECOLUMN_H

#include "imgui.h"

#include "ToolCardSpecification.h"

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The height the readout occupies, for the scroll range.
// 🔴 SelectedCount decides WHICH readout is measured, so it must match the count the draw is then given. A multi-pick reports the
//    aggregate rows instead of the per-component sections, and the two have different heights — measuring one and drawing the other
//    leaves the pane scrolling over the wrong extent.
[[nodiscard]] float MeasureToolProbeHeight(const ToolProbeReadoutDescriptor& Readout,
                                           int                               SelectedCount,
                                           float                             ColumnWidth);

// Draw the readout for the current selection into the region starting at Origin.
void InscribeToolProbeColumn(const SvgIconRegistry*            Icons,
                             const ToolProbeReadoutDescriptor& Readout,
                             int                               SelectedCount,
                             ImVec2                            Origin,
                             float                             ColumnWidth,
                             const ToolCardPalette&            Palette);

} // namespace Frontier

#endif
