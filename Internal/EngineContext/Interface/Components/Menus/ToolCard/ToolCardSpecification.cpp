/*==============================================================================================================================================
                                                        TOOLCARDSPECIFICATION.CPP
==============================================================================================================================================*/
// 🧩 Resolves the tool card's palette and metrics from the shared theme, and answers the availability questions the rail and grid are built from.
//    The three-state availability rule lives here rather than in either component because both ask it and they must agree: the rail counts what
//    the grid will draw, so a divergence would state a tally the grid then contradicts.

#include "ToolCardSpecification.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ToolCardPalette ResolveToolCardPalette(const ThemeConfiguration& Theme)
{
    ToolCardPalette Resolved = {};

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

    // 📝 Card-specific tokens. The prototype's CSS carries these and the shared theme has no field for any of them — the grid
    //    pane deliberately sits one notch darker than the rail, and a gated tile is a distinct fill rather than the tile fill
    //    at lower opacity. Stated as the prototype's own literals so this remains the single place they exist.
    Resolved.GridFill         = IM_COL32(0x10, 0x10, 0x12, 0xFF);   // --menu-2
    Resolved.RailSelectedFill = IM_COL32(0x23, 0x23, 0x27, 0xFF);   // --rail-sel
    Resolved.TileFill         = IM_COL32(0x1D, 0x1D, 0x21, 0xFF);   // --tile
    Resolved.TileHoverFill    = IM_COL32(0x26, 0x26, 0x2B, 0xFF);   // --tile-hi
    Resolved.GatedTileFill    = IM_COL32(0x16, 0x16, 0x1A, 0xFF);   // .tile.gated
    Resolved.HairlineStrong   = IM_COL32(0xFF, 0xFF, 0xFF, 0x1A);   // --hair-strong, .10 alpha
    Resolved.Faint            = IM_COL32(0x55, 0x55, 0x5D, 0xFF);   // --faint
    Resolved.ShortfallInk     = IM_COL32(0xC9, 0xA2, 0x27, 0xFF);   // .tile.gated .t-why
    Resolved.BreachInk        = IM_COL32(0xE0, 0x60, 0x3F, 0xFF);   // .probe-row.breach

    return Resolved;
}


ToolCardMetrics ResolveToolCardMetrics(const ThemeConfiguration& Theme)
{
    ToolCardMetrics Resolved;   // defaults are the prototype's CSS numbers

    // 📝 One knob scales the whole card. The column widths and the card width are scaled together and the width is then
    //    RE-DERIVED from them rather than scaled independently: scaling three related numbers separately lets rounding put the
    //    card a pixel wide of its own columns, which is exactly the seam-stepping the pinned columns exist to prevent.
    const float Scale = (Theme.Metrics.UiScale > 0.0f) ? Theme.Metrics.UiScale : 1.0f;
    if (Scale != 1.0f)
    {
        Resolved.LeftColumnWidth   *= Scale;
        Resolved.RightColumnWidth  *= Scale;
        Resolved.CardHeight        *= Scale;
        Resolved.HeaderHeight      *= Scale;
        Resolved.RailRowHeight     *= Scale;
        Resolved.GridFootHeight    *= Scale;
        Resolved.OptionsFootHeight *= Scale;
        Resolved.CardRounding      *= Scale;
        Resolved.TileRounding      *= Scale;
        Resolved.TileGap           *= Scale;
        Resolved.CardWidth          = Resolved.LeftColumnWidth + Resolved.RightColumnWidth + 1.0f;
    }

    return Resolved;
}


ToolAvailability ResolveTileAvailability(const ToolTileDescriptor& Tile, unsigned int StratumBit)
{
    if ((Tile.StrataMask & StratumBit) == 0u)
    {
        return ToolAvailability::Absent;
    }
    return (Tile.Shortfall != nullptr) ? ToolAvailability::Gated : ToolAvailability::Live;
}


int TallyApplicableTiles(const ToolBandDescriptor& Band, unsigned int StratumBit)
{
    if (Band.Tiles == nullptr)
    {
        return 0;
    }

    int Applicable = 0;
    for (int TileIndex = 0; TileIndex < Band.TileCount; ++TileIndex)
    {
        if (ResolveTileAvailability(Band.Tiles[TileIndex], StratumBit) != ToolAvailability::Absent)
        {
            ++Applicable;
        }
    }
    return Applicable;
}


int TallyLiveTiles(const ToolBandDescriptor& Band, unsigned int StratumBit)
{
    if (Band.Tiles == nullptr)
    {
        return 0;
    }

    int Live = 0;
    for (int TileIndex = 0; TileIndex < Band.TileCount; ++TileIndex)
    {
        if (ResolveTileAvailability(Band.Tiles[TileIndex], StratumBit) == ToolAvailability::Live)
        {
            ++Live;
        }
    }
    return Live;
}

} // namespace Frontier
