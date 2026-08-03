/*==============================================================================================================================================
                                                              PROBESTRIP.H
==============================================================================================================================================*/
// 🧩 The console's left column in its SECOND slide: the readout for the current context — a single selection's titled sections, or a multi-pick's
//    aggregate rows instead, because a sum over twelve faces is a different statement from one face's own measurements. Ported from ToolCard's
//    ToolProbeColumn. It reads a ProbeReadoutDescriptor the workspace supplies and draws it; it holds no state and asks no gate.
//
//    📝 A null readout draws nothing, not an empty pane: a workspace whose current selection measures nothing (or that has no probe at all) leaves
//       this slide's left column blank, and the options column beside it fills the slide.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PANE_PROBESTRIP_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PANE_PROBESTRIP_H

#include "imgui.h"

#include "../Descriptor/ProbeReadoutDescriptor.h"
#include "../Token/PaletteSpecification.h"
#include "../Token/MetricsSpecification.h"

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The height the readout occupies, for the scroll range. Measured without drawing so the pane can size its body before painting. Zero when Readout
// is null.
[[nodiscard]] float MeasureProbeStripHeight(const ProbeReadoutDescriptor* Readout,
                                            float                         ColumnWidth,
                                            const MetricsSpecification&   Metrics);

// Draw the readout into the region at Origin. Draws nothing when Readout is null. Which shape each row takes is decided by the descriptor's set
// members, not by a kind tag, exactly as the probe descriptor is authored.
void InscribeProbeStrip(const SvgIconRegistry*        Icons,
                        const ProbeReadoutDescriptor* Readout,
                        ImVec2                        Origin,
                        float                         ColumnWidth,
                        const PaletteSpecification&   Palette,
                        const MetricsSpecification&   Metrics);

} // namespace Frontier

#endif
