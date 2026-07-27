/*==============================================================================================================================================
                                                          COLORPALETTEDESCRIPTOR.CPP
==============================================================================================================================================*/
// 🧩 The one place raw colour literals are permitted. Editing a channel here re-tints every panel and control that reads the theme.

#include "ColorPaletteDescriptor.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Pack a straight-alpha RGBA (0-255) into ImGui's packed ImU32. Keeps palette literals readable as (r, g, b, a).
    ImU32 PackColor(int R, int G, int B, int A)
    {
        return IM_COL32(R, G, B, A);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ColorPaletteDescriptor ResolveDarkPalette()
{
    ColorPaletteDescriptor Palette = {};

    // 📝 Tokens lifted from the UVeditor prototype (css/theme.css :root) so the properties panel shares that dark palette exactly:
    //    --bg #000, --panel-2 #0e0e0e (card fill), --panel-3 #141414 (track fill), --row-hover #1a1a1a, --row-selected #1f1f1f,
    //    --border #1c1c1c, --text #ededed, --text-dim #8a8a8a, and the near-white accent (.pill.active #fff / .switch.on #e8e8e8).
    Palette.DeskBackground    = PackColor(  0,   0,   0, 255);   // --bg
    Palette.PanelBackground   = PackColor( 14,  14,  14, 255);   // --panel-2 (card body)
    Palette.PanelHeader       = PackColor( 14,  14,  14, 255);   // --panel-2 (header shares the card fill)
    Palette.PanelBorder       = PackColor( 28,  28,  28, 255);   // --border
    Palette.ControlBackground = PackColor( 20,  20,  20, 255);   // --panel-3
    Palette.ControlHovered    = PackColor( 26,  26,  26, 255);   // --row-hover
    Palette.ControlActive     = PackColor( 31,  31,  31, 255);   // --row-selected
    Palette.AccentPrimary     = PackColor(232, 232, 232, 255);   // near-white active (.switch.on / .pill.active)
    Palette.AccentSubtle      = PackColor(255, 255, 255,  31);   // --accent-soft
    Palette.SelectionMarker   = PackColor( 74, 144, 226, 255);   // ControlsPreview --accent #4a90e2 (dropdown radio + hover bar)
    Palette.TextPrimary       = PackColor(237, 237, 237, 255);   // --text
    Palette.TextMuted         = PackColor(138, 138, 138, 255);   // --text-dim
    Palette.TextOnAccent      = PackColor( 17,  17,  17, 255);   // text over white (.pill.active color #111)

    // 📝 Pill / value-box tones, aligned to the UVeditor props.css: .num-field is black (#000) with grey unit text (--text-faint),
    //    .slider track is --panel-3 (#141414) with a #5a5a5a fill and a white handle.
    Palette.ValueNumberSegment = PackColor(  0,   0,   0, 255);   // .num-field background #000
    Palette.ValueSideSegment   = PackColor( 20,  20,  20, 255);   // grey cap ~ --panel-3
    Palette.ValueOutline       = PackColor(255, 255, 255,  56);   // (unused: pill outline removed) kept for other painters
    Palette.ValueText          = PackColor(255, 255, 255, 255);   // .num-field input color #fff
    Palette.SliderTrack        = PackColor( 20,  20,  20, 255);   // .slider background --panel-3
    Palette.SliderFill         = PackColor( 90,  90,  90, 255);   // .slider .fill #5a5a5a
    Palette.SliderKnob         = PackColor(255, 255, 255, 255);   // .slider .handle #fff
    Palette.KnobText           = PackColor( 17,  17,  17, 255);   // text over white pill

    return Palette;
}

}   // namespace Frontier
