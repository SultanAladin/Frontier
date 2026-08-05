/*==============================================================================================================================================
                                                         PALETTESPECIFICATION.H
==============================================================================================================================================*/
// 🧩 The colours the console draws with, resolved once per cycle from the shared ThemeConfiguration so a palette change flows from one place. This
//    is the INTERSECTION every workspace shares — the card fill, the pane fill, the tile fills, the rail selection, the hairlines, the text inks,
//    the slider/switch tokens. Tokens a single workspace needs and no other has (paint's paper inversion + well ring, construction's CAD gate
//    tints) do NOT live here: they stay app-local, resolved by the workspace and passed through the descriptor, so a paint-only paper colour never
//    sits in front of a modelling author. The console draws the shared tokens; a workspace overlays its own where it must.
//
//    🔴 This is the reconciliation of ToolCardPalette and PaintCardPalette down to their common fields. The two agreed on 17 tokens and diverged on
//       ~5 each; the agreement is here, the divergence stays where it is authored.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_TOKEN_PALETTESPECIFICATION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_TOKEN_PALETTESPECIFICATION_H

#include "imgui.h"

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct PaletteSpecification
{
    ImU32 CardFill;          // [-] - --menu        card + rail background
    ImU32 PaneFill;          // [-] - --menu-2      grid + options pane, one notch darker
    ImU32 RailSelectedFill;  // [-] - --rail-sel    active rail row
    ImU32 SelectionFill;     // [-] - blue accent wash behind the selected rail row / hovered tile
    ImU32 SelectionMarker;   // [-] - solid blue left-edge indicator bar on the selected rail row
    ImU32 TileFill;          // [-] - --tile        tile rest
    ImU32 TileHoverFill;     // [-] - --tile-hi     tile under pointer
    ImU32 TileChosenFill;    // [-] - accent wash over the pane on the selected tile
    ImU32 GatedTileFill;     // [-] - the darker fill of a tile whose precondition is short
    ImU32 Hairline;          // [-] - --hair        pane rules
    ImU32 HairlineStrong;    // [-] - --hair-strong card border
    ImU32 Accent;            // [-] - --accent      active marker, live tallies
    ImU32 Ink;               // [-] - --ink         primary text + live glyph
    ImU32 Muted;             // [-] - --muted       secondary text
    ImU32 Faint;             // [-] - --faint       tertiary text + gated glyph
    ImU32 ValueBlack;        // [-] - --value-black numeric pill centre
    ImU32 ValueUnitFill;     // [-] - --value-unit  unit segment
    ImU32 TrackFill;         // [-] - --track-bg    switch track, off
    ImU32 TrackTravelled;    // [-] - --track-fill  slider travelled portion
    ImU32 KnobFill;          // [-] - --knob        slider knob, selected segment, switch nub
    ImU32 KnobInk;           // [-] - text over a knob-filled segment
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Resolve the shared console palette from the theme. Workspace-only tokens are resolved by the workspace itself, not here.
[[nodiscard]] PaletteSpecification ResolveConsolePalette(const ThemeConfiguration& Theme);

} // namespace Frontier

#endif
