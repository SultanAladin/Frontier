/*==============================================================================================================================================
                                                        PANEHEADERRASTERIZATION.H
==============================================================================================================================================*/
// 🧩 The one pane-header band every slide shares: the fixed-height row at the top of a pane carrying a title, an optional subtitle, an optional
//    tally pill, and either a leading glyph tile or a back chevron. Both slides use ONE rule for the band so the seam never steps across the
//    carousel — letting each pane pass its own height is how that continuity gets broken, so height comes from the metrics, never from a parameter.
//    Reconciled from PaintCardShell's ConstructPaintPaneHeader and the header logic inside ToolCardShell; the paint prototype's is the fuller of
//    the two (it has the back-row chevron and the nib-well icon variant), so the shared band is authored to its shape and modelling simply omits
//    the fields it does not use.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_RASTERIZATION_PANEHEADERRASTERIZATION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_RASTERIZATION_PANEHEADERRASTERIZATION_H

#include "imgui.h"

#include "../Token/PaletteSpecification.h"
#include "../Token/MetricsSpecification.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Everything one pane header shows. Assembled by the caller because each pane's header reads different sources — the action grid's icon is the
//    open cluster's glyph, the options grid's is the selected action's. A back header replaces the icon+pill: the options-slide header is the one
//    clickable band on the console, and reports its click.
struct ConsolePaneHeader
{
    const char* Title       = nullptr;   // [-] - the bold line
    const char* Subtitle    = nullptr;   // [-] - the faint second line; null draws a single-line header
    const char* TallyText   = nullptr;   // [-] - the pill on the right; null omits the pill
    bool        TallyAccent = false;     // [-] - accent-inked vs muted tally
    bool        IsBackRow   = false;     // [-] - draws the chevron and hover fill, and reports its click
    ImTextureID IconTexture = 0;         // [-] - optional leading glyph; 0 draws the empty black tile
    bool        IconIsWell  = false;     // [-] - pale well fill + ring, art overflows the tile (paint's nib crop)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw one pane's header band into a pane rectangle and report whether a back row was clicked this frame. Height comes from Metrics.HeaderHeight,
// not a parameter, so the band is continuous across the console.
bool InscribeConsolePaneHeader(const PaletteSpecification&  Palette,
                               const MetricsSpecification& Metrics,
                               const ConsolePaneHeader&    Header,
                               ImVec2                      PaneMinimum,
                               float                       PaneWidth);

} // namespace Frontier

#endif
