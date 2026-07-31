/*==============================================================================================================================================
                                                        PAINTCARDSPECIFICATION.CPP
==============================================================================================================================================*/
// 🧩 Resolves the paint card's palette and metrics from the shared theme, and parses the authored CSS colours the catalogue stores as hex strings.
//    Every literal here is Documentation/Prototypes/PaintToolMenu.html's own, extracted by _ClaudeScratch/tmp/ProbePaintMetrics.js.

#include "PaintCardSpecification.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// Blend Overlay onto Base at Alpha. The prototype expresses .tile.on as `rgba(accent, .08)` over the pane, and an ImGui fill is
// opaque — so the composite is done here rather than drawing two quads and hoping the order holds.
ImU32 BlendOver(ImU32 BaseColour, ImU32 OverlayColour, float OverlayAlpha)
{
    const ImVec4 Base    = ImGui::ColorConvertU32ToFloat4(BaseColour);
    const ImVec4 Overlay = ImGui::ColorConvertU32ToFloat4(OverlayColour);

    const float Weight = (OverlayAlpha < 0.0f) ? 0.0f : ((OverlayAlpha > 1.0f) ? 1.0f : OverlayAlpha);

    ImVec4 Mixed;
    Mixed.x = Base.x + ((Overlay.x - Base.x) * Weight);
    Mixed.y = Base.y + ((Overlay.y - Base.y) * Weight);
    Mixed.z = Base.z + ((Overlay.z - Base.z) * Weight);
    Mixed.w = 1.0f;   // the composite sits on an opaque pane, so it is opaque

    return ImGui::ColorConvertFloat4ToU32(Mixed);
}


// One hex digit's value, or -1 when the character is not a hex digit.
int HexDigitValue(char Character)
{
    if (Character >= '0' && Character <= '9') { return Character - '0'; }
    if (Character >= 'a' && Character <= 'f') { return (Character - 'a') + 10; }
    if (Character >= 'A' && Character <= 'F') { return (Character - 'A') + 10; }
    return -1;
}

} // namespace


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

PaintCardPalette ResolvePaintCardPalette(const ThemeConfiguration& Theme)
{
    PaintCardPalette Resolved = {};

    // 📝 Drawn from the shared theme wherever the theme has the concept, so a palette change flows through. The paint card's
    //    :root tokens were compared against the modelling card's and are the same values for every one of these.
    Resolved.CardFill       = Theme.Palette.PanelBackground;
    Resolved.Hairline       = Theme.Palette.PanelBorder;
    Resolved.Ink            = Theme.Palette.TextPrimary;
    Resolved.Muted          = Theme.Palette.TextMuted;
    Resolved.ValueBlack     = Theme.Palette.ValueNumberSegment;
    Resolved.ValueUnitFill  = Theme.Palette.ValueSideSegment;
    Resolved.ValueInk       = Theme.Palette.ValueText;
    Resolved.TrackFill      = Theme.Palette.ControlBackground;
    Resolved.TrackTravelled = Theme.Palette.SliderFill;
    Resolved.KnobFill       = Theme.Palette.SliderKnob;
    Resolved.KnobInk        = Theme.Palette.KnobText;

    // 📝 Card-specific tokens the shared theme has no field for, stated as the prototype's own literals so this stays the single
    //    place they exist. The grid/options pane deliberately sits one notch darker than the rail.
    Resolved.PaneFill         = IM_COL32(0x10, 0x10, 0x12, 0xFF);   // --menu-2
    Resolved.RailSelectedFill = IM_COL32(0x23, 0x23, 0x27, 0xFF);   // .rail-item.active
    Resolved.TileFill         = IM_COL32(0x1D, 0x1D, 0x21, 0xFF);   // --tile
    Resolved.TileHoverFill    = IM_COL32(0x26, 0x26, 0x2B, 0xFF);   // --tile-hi
    Resolved.HairlineStrong   = IM_COL32(0xFF, 0xFF, 0xFF, 0x1A);   // --hair-strong, .10 alpha
    Resolved.Faint            = IM_COL32(0x55, 0x55, 0x5D, 0xFF);   // --faint

    // 🔴 The accent is a LITERAL, not Theme.Palette.AccentPrimary, and that is a measured decision
    //    rather than an oversight. The shared dark theme's AccentPrimary is near-WHITE (232,232,232)
    //    because there it means "active switch / active pill", and its only blue — SelectionMarker,
    //    #4a90e2 — is a different blue from this card's. The paint accent drives four visible things
    //    (the rail's active marker, the chosen tile's tint, the live tally, the primary button), so
    //    binding it to the theme would render the port in grey and misstate the prototype.
    Resolved.Accent           = IM_COL32(0x5B, 0x8C, 0xFF, 0xFF);   // --accent

    // The three with no counterpart anywhere in the shared palette.
    Resolved.WellRing         = IM_COL32(0x26, 0x26, 0x29, 0xFF);   // --well-hair
    Resolved.WellRingHover    = IM_COL32(0x46, 0x46, 0x4C, 0xFF);   // .tile:hover .well
    Resolved.SliderOutline    = IM_COL32(0xFF, 0xFF, 0xFF, 0x38);   // --outline, .22 alpha

    // 🔴 The pale paper the two preview surfaces invert to. See the header: pigment needs paper, or a stroke reads as a hole.
    Resolved.PaperFill        = IM_COL32(0xF4, 0xF1, 0xEA, 0xFF);   // .pv-stroke / .pv-dot-wrap
    Resolved.PaperInk         = IM_COL32(0x1B, 0x1B, 0x1E, 0xFF);   // legible over that paper

    // 📝 Composited rather than drawn as a second translucent quad: `rgba(91,140,255,.08)` over the pane. Derived from the
    //    RESOLVED accent and pane, not from the prototype's literal, so a theme accent change carries into the chosen tile too.
    Resolved.TileChosenFill   = BlendOver(Resolved.PaneFill, Resolved.Accent, 0.08f);

    return Resolved;
}


PaintCardMetrics ResolvePaintCardMetrics(const ThemeConfiguration& Theme)
{
    PaintCardMetrics Resolved;   // defaults are the prototype's CSS numbers

    const float Scale = (Theme.Metrics.UiScale > 0.0f) ? Theme.Metrics.UiScale : 1.0f;
    if (Scale == 1.0f)
    {
        return Resolved;
    }

    // 🔴 Scaled field by field, NOT by multiplying the whole struct. Three of its members must not be touched:
    //    the five durations (a bigger card does not take longer to slide), GridColumnCount (a count, not a length),
    //    and the two unitless growth factors RailDotChosenGrow / StandArtGrow (a scale times a scale is a different scale).
    //    A blanket multiply gets all eight wrong and every one of them fails silently.
    Resolved.CardHeight        *= Scale;
    Resolved.LeftColumnWidth   *= Scale;
    Resolved.RightColumnWidth  *= Scale;
    Resolved.CardRounding      *= Scale;

    Resolved.HeaderHeight      *= Scale;
    Resolved.GridFootHeight    *= Scale;
    Resolved.OptionsFootHeight *= Scale;

    Resolved.RailPadding       *= Scale;
    Resolved.RailRowHeight     *= Scale;
    Resolved.RailRowRounding   *= Scale;
    Resolved.RailRowGap        *= Scale;
    Resolved.RailDotDiameter   *= Scale;
    Resolved.RailMarkerWidth   *= Scale;
    Resolved.RailMarkerHeight  *= Scale;

    Resolved.GridPadding       *= Scale;
    Resolved.TileGap           *= Scale;
    Resolved.TileRounding      *= Scale;
    Resolved.WellDiameter      *= Scale;
    Resolved.NibArtEdge        *= Scale;
    Resolved.HeaderIconEdge    *= Scale;

    Resolved.StrokeStripHeight *= Scale;
    Resolved.StandHeight       *= Scale;
    Resolved.StandArtWidth     *= Scale;
    Resolved.StandArtHeight    *= Scale;
    Resolved.SwatchDiameter    *= Scale;
    Resolved.SwatchGap         *= Scale;

    Resolved.OptionsPaddingX   *= Scale;
    Resolved.OptionsPaddingY   *= Scale;
    Resolved.OptionsRowGap     *= Scale;
    Resolved.ControlGap        *= Scale;
    Resolved.SliderHeight      *= Scale;
    Resolved.ValueBoxHeight    *= Scale;
    Resolved.SliderKnobEdge    *= Scale;
    Resolved.SliderOutlineWide *= Scale;
    Resolved.SegmentHeight     *= Scale;
    Resolved.SegmentGap        *= Scale;
    Resolved.SegmentRounding   *= Scale;
    Resolved.SwitchWidth       *= Scale;
    Resolved.SwitchHeight      *= Scale;
    Resolved.SwitchNubEdge     *= Scale;
    Resolved.ButtonHeight      *= Scale;
    Resolved.ButtonRounding    *= Scale;

    // 🔴 RE-DERIVED from the two columns rather than scaled on its own. The prototype states the arithmetic itself —
    //    "rail/preview column 196 px + grid/options 363 px + hairline" — and scaling three related numbers independently lets
    //    rounding put the card a pixel wide of its own columns, which is exactly the seam-stepping the pinned column prevents.
    Resolved.CardWidth = Resolved.LeftColumnWidth + Resolved.RightColumnWidth + 1.0f;

    return Resolved;
}


ImU32 ResolveAuthoredColour(const char* CssHex, ImU32 FallbackColour)
{
    if (CssHex == nullptr)
    {
        return FallbackColour;
    }

    const char* Digits = (CssHex[0] == '#') ? (CssHex + 1) : CssHex;

    // Measure the run of hex digits. Both CSS shorthands are accepted because the catalogue carries the prototype's strings
    // verbatim and nothing forces those to one form.
    int DigitCount = 0;
    while (DigitCount < 8 && HexDigitValue(Digits[DigitCount]) >= 0)
    {
        ++DigitCount;
    }
    if (Digits[DigitCount] != '\0')
    {
        return FallbackColour;   // trailing junk: not a plain hex colour
    }

    int Channels[3] = {};

    if (DigitCount == 6)
    {
        for (int Index = 0; Index < 3; ++Index)
        {
            Channels[Index] = (HexDigitValue(Digits[Index * 2]) * 16) + HexDigitValue(Digits[(Index * 2) + 1]);
        }
    }
    else if (DigitCount == 3)
    {
        // 📝 CSS shorthand doubles each digit — #abc is #aabbcc, NOT #a0b0c0. Scaling by 16 instead would darken every
        //    shorthand colour by a channel's worth, which reads as a slightly-off tint rather than as a bug.
        for (int Index = 0; Index < 3; ++Index)
        {
            const int Digit = HexDigitValue(Digits[Index]);
            Channels[Index] = (Digit * 16) + Digit;
        }
    }
    else
    {
        return FallbackColour;
    }

    return IM_COL32(Channels[0], Channels[1], Channels[2], 0xFF);
}

} // namespace Frontier
