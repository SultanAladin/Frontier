/*==============================================================================================================================================
                                                         PALETTESPECIFICATION.CPP
==============================================================================================================================================*/
// 🧩 Resolves the console's shared palette from the theme. Only the tokens EVERY workspace shares live here; a workspace's own tokens (paint's
//    paper inversion, construction's CAD gate tints) are resolved by the workspace, never in this function. Ported from ToolCardSpecification's
//    ResolveToolCardPalette, minus the gate-specific ShortfallInk/GatedTileFill/BreachInk that were modelling-and-construction concepts.

#include "PaletteSpecification.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

PaletteSpecification ResolveConsolePalette(const ThemeConfiguration& Theme)
{
    PaletteSpecification Resolved = {};

    // 📝 Drawn from the shared theme wherever the theme has the concept, so a palette change flows through.
    Resolved.CardFill         = Theme.Palette.PanelBackground;
    Resolved.Hairline         = Theme.Palette.PanelBorder;
    Resolved.Accent           = Theme.Palette.AccentPrimary;
    Resolved.Ink              = Theme.Palette.TextPrimary;
    Resolved.Muted            = Theme.Palette.TextMuted;
    Resolved.ValueBlack       = Theme.Palette.ValueNumberSegment;
    Resolved.ValueUnitFill    = Theme.Palette.ValueSideSegment;
    Resolved.TrackFill        = Theme.Palette.ControlBackground;
    Resolved.TrackTravelled   = Theme.Palette.SliderFill;
    Resolved.KnobFill         = Theme.Palette.SliderKnob;
    Resolved.KnobInk          = Theme.Palette.KnobText;

    // 📝 The selection indicator is a flat BLUE accent wash (like the outliner selection), no marker bar, no corners. Sourced from the console's own
    //    blue accent (0x3B82F6) at a readable alpha rather than the near-invisible white AccentSubtle, so the highlight actually shows over the dark card.
    Resolved.SelectionFill    = IM_COL32(0x3B, 0x82, 0xF6, 0x40);   // blue accent @ ~.25 — the selected rail row / hovered tile ground
    Resolved.SelectionMarker  = IM_COL32(0x3B, 0x82, 0xF6, 0xFF);   // solid blue — the left-edge indicator bar on the selected rail row

    // 📝 Console tokens the shared theme has no field for, stated as the prototype's own literals so this is their single home. The pane
    //    deliberately sits one notch darker than the rail; a gated tile is a distinct fill rather than the tile fill at lower opacity; the
    //    chosen tile is an accent wash. These are the INTERSECTION both card prototypes agreed on.
    Resolved.PaneFill         = IM_COL32(0x10, 0x10, 0x12, 0xFF);   // --menu-2
    Resolved.RailSelectedFill = IM_COL32(0x23, 0x23, 0x27, 0xFF);   // --rail-sel
    Resolved.TileFill         = IM_COL32(0x1D, 0x1D, 0x21, 0xFF);   // --tile
    Resolved.TileHoverFill    = IM_COL32(0x26, 0x26, 0x2B, 0xFF);   // --tile-hi
    Resolved.TileChosenFill   = IM_COL32(0x3B, 0x82, 0xF6, 0x14);   // .tile.on — accent at ~.08 over the pane
    Resolved.GatedTileFill    = IM_COL32(0x16, 0x16, 0x1A, 0xFF);   // .tile.gated
    Resolved.HairlineStrong   = IM_COL32(0xFF, 0xFF, 0xFF, 0x1A);   // --hair-strong, .10 alpha
    Resolved.Faint            = IM_COL32(0x55, 0x55, 0x5D, 0xFF);   // --faint

    return Resolved;
}

} // namespace Frontier
