/*==============================================================================================================================================
                                                           PAINTCARDSHELL.CPP
==============================================================================================================================================*/
// 🧩 The card box, the carousel translate, and the shared pane header band.

#include "PaintCardShell.h"

#include <cmath>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            EASING
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The prototype's transitions are named cubic-beziers, not "ease" — `cubic-bezier(.5,.05,.2,1)` for the carousel and
//    `cubic-bezier(.16,1,.3,1)` for the pop. Substituting a smoothstep gets the duration right and the FEEL wrong: the
//    carousel curve holds still at the start and coasts out long, and the pop curve overshoots toward its end. Both are
//    load bearing at 340 ms and 140 ms, which is slow enough to see. So the actual curve is solved here.
// A CSS cubic-bezier is a parametric curve through (0,0),(X1,Y1),(X2,Y2),(1,1); `Progress` is the X axis (time) and the
// return is the Y axis (eased value). X must be inverted numerically because the curve is not a function of time
// directly — the standard approach, and the one browsers use.
float SolvePaintCubicBezier(float Progress, float X1, float Y1, float X2, float Y2)
{
    // 📝 Only the ENDPOINTS are shortcut, and they are exact for any control points: x(0)=y(0)=0 and x(1)=y(1)=1 by
    //    construction. This is not a clamp of the interior — an overshoot curve still returns >1 between them.
    if (Progress <= 0.0f) { return 0.0f; }
    if (Progress >= 1.0f) { return 1.0f; }

    const auto CurveAt = [](float T, float A, float B) -> float
    {
        const float OneMinusT = 1.0f - T;
        // 3(1-t)²t·A + 3(1-t)t²·B + t³
        return (3.0f * OneMinusT * OneMinusT * T * A) + (3.0f * OneMinusT * T * T * B) + (T * T * T);
    };

    // 📝 Newton would need the derivative and can leave the interval on these control points; bisection over 24 steps
    //    lands within ~6e-8 on a curve that only ever drives pixels, and cannot diverge.
    //    🔴 Bisection is valid here only because x(t) is monotonic, which CSS guarantees by requiring x1,x2 in [0,1].
    //       The Y control points carry no such restriction, which is why an overshoot lives in Y and never in X.
    float Low = 0.0f, High = 1.0f, Parameter = Progress;
    for (int Iteration = 0; Iteration < 24; ++Iteration)
    {
        const float X = CurveAt(Parameter, X1, X2);
        if (X < Progress) { Low = Parameter; } else { High = Parameter; }
        Parameter = (Low + High) * 0.5f;
    }

    return CurveAt(Parameter, Y1, Y2);
}


namespace
{
    // .tool-track — cubic-bezier(.5,.05,.2,1)
    float EaseCarousel(float Progress) { return SolvePaintCubicBezier(Progress, 0.5f, 0.05f, 0.2f, 1.0f); }

    // @keyframes pop — cubic-bezier(.16,1,.3,1)
    float EaseOpen(float Progress) { return SolvePaintCubicBezier(Progress, 0.16f, 1.0f, 0.3f, 1.0f); }

    float Saturate(float Value) { return (Value < 0.0f) ? 0.0f : ((Value > 1.0f) ? 1.0f : Value); }

    ImU32 ScaleAlpha(ImU32 Colour, float Alpha)
    {
        const float Existing = (float)((Colour >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
        const ImU32 Channels = Colour & ~((ImU32)0xFF << IM_COL32_A_SHIFT);
        const ImU32 Scaled   = (ImU32)(Saturate(Existing * Alpha) * 255.0f + 0.5f);
        return Channels | (Scaled << IM_COL32_A_SHIFT);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void AdvancePaintCardShell(PaintCardShellState& State, const PaintCardMetrics& Metrics, float DeltaSeconds)
{
    if (!State.IsOpen)
    {
        // 📝 Reset rather than freeze, so re-opening replays the pop. The prototype rebuilds the card wholesale on open,
        //    which restarts the animation; keeping the phase would make every open after the first appear instantly.
        State.OpenPhase = 0.0f;
        return;
    }

    if (DeltaSeconds <= 0.0f) { return; }

    if (Metrics.OpenSeconds > 0.0f)
    {
        State.OpenPhase = Saturate(State.OpenPhase + (DeltaSeconds / Metrics.OpenSeconds));
    }
    else
    {
        State.OpenPhase = 1.0f;
    }

    // The carousel runs toward whichever edge the current slide names.
    const float Target = (State.Slide == PaintCardSlide::Options) ? 1.0f : 0.0f;
    if (Metrics.CarouselSeconds > 0.0f)
    {
        const float Step = DeltaSeconds / Metrics.CarouselSeconds;
        if (State.SlidePhase < Target) { State.SlidePhase = (State.SlidePhase + Step > Target) ? Target : State.SlidePhase + Step; }
        else if (State.SlidePhase > Target) { State.SlidePhase = (State.SlidePhase - Step < Target) ? Target : State.SlidePhase - Step; }
    }
    else
    {
        State.SlidePhase = Target;
    }
}


void RequestPaintCardSlide(PaintCardShellState& State, PaintCardSlide Slide)
{
    // 📝 Idempotent because SlidePhase is never touched here — it animates toward whichever edge Slide names, so setting
    //    the same slide repeatedly is a no-op and a held button cannot restart the translate. Writing the phase here
    //    instead (say, zeroing it on request) is what would leave the card stuck partway under a held button.
    //    🔴 Also why a mid-translate reversal is free: flipping Slide re-aims the same phase, so the card turns around from
    //       wherever it had reached rather than snapping to an edge first.
    State.Slide = Slide;
}


bool BeginPaintCardShell(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                         const PaintCardShellState& State, ImVec2 TopLeft)
{
    if (!State.IsOpen) { return false; }

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    const float Eased    = EaseOpen(State.OpenPhase);

    // 🔴 @keyframes pop is THREE simultaneous properties — opacity 0->1, scale .97->1, and translateY -4px->0 — and it
    //    scales about `transform-origin:top left`, not the centre. Scaling about the centre makes the card appear to grow
    //    from its middle, which reads as a different animation entirely.
    const float Scale   = 0.97f + (0.03f * Eased);
    const float DriftY  = -4.0f * (1.0f - Eased);
    const float Alpha   = Eased;

    const ImVec2 Origin(TopLeft.x, TopLeft.y + DriftY);
    const ImVec2 Extent(Origin.x + (Metrics.CardWidth * Scale), Origin.y + (Metrics.CardHeight * Scale));

    // The box: fill, then the hairline border, both faded by the pop.
    DrawList->AddRectFilled(Origin, Extent, ScaleAlpha(Palette.CardFill, Alpha), Metrics.CardRounding);
    DrawList->AddRect(Origin, Extent, ScaleAlpha(Palette.HairlineStrong, Alpha), Metrics.CardRounding, 0, 1.0f);

    // 🔴 The clip is what makes the carousel a carousel: the track is twice the card's width and the half of it that is
    //    off-card must not draw. `.tool-view{ overflow:hidden }`.
    //    📝 Intersected with the existing clip (`true`) rather than replacing it, so a card near a window edge is still
    //       bounded by the window — replacing would let it draw over the window's own chrome.
    ImGui::PushClipRect(Origin, Extent, true);
    return true;
}


void EndPaintCardShell()
{
    ImGui::PopClipRect();
}


PaintPaneRegion ResolvePaintPaneRegion(const PaintCardMetrics& Metrics, const PaintCardShellState& State,
                                       ImVec2 TopLeft, PaintCardSlide Slide, bool IsLeftColumn,
                                       float HeaderHeight, float FootHeight)
{
    PaintPaneRegion Region;

    // 🔴 The track translates by exactly HALF its own width, which is one card width — `translateX(-50%)` of a 200% track.
    //    Reading the -50% as half a CARD would move it half as far and leave both slides half-visible forever.
    const float Eased      = EaseCarousel(State.SlidePhase);
    const float TrackShift = -(Metrics.CardWidth * Eased);

    // Which half of the track this pane lives on, before the shift.
    const float SlideOrigin = (Slide == PaintCardSlide::Options) ? Metrics.CardWidth : 0.0f;
    const float ColumnX     = IsLeftColumn ? 0.0f : (Metrics.LeftColumnWidth + 1.0f);
    const float ColumnWidth = IsLeftColumn ? Metrics.LeftColumnWidth : Metrics.RightColumnWidth;

    const float PaneLeft = TopLeft.x + SlideOrigin + TrackShift + ColumnX;

    Region.BodyMinimum = ImVec2(PaneLeft, TopLeft.y + HeaderHeight);
    Region.BodyMaximum = ImVec2(PaneLeft + ColumnWidth, TopLeft.y + Metrics.CardHeight - FootHeight);

    // 📝 Cheap reject for the slide that is entirely off-card, so a pane can skip its whole body — and, more to the point,
    //    skip hit-testing. A fully translated-away pane that still tests for hover steals clicks from the visible one.
    const float CardLeft  = TopLeft.x;
    const float CardRight = TopLeft.x + Metrics.CardWidth;
    Region.IsVisible = (Region.BodyMaximum.x > CardLeft) && (Region.BodyMinimum.x < CardRight);

    return Region;
}


bool ConstructPaintPaneHeader(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                              const PaintPaneHeader& Header, ImVec2 PaneMinimum, float PaneWidth)
{
    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    const ImVec2 BandMinimum = PaneMinimum;
    const ImVec2 BandMaximum(PaneMinimum.x + PaneWidth, PaneMinimum.y + Metrics.HeaderHeight);

    // The back row is the one interactive header; the other three are inert bands.
    bool WasClicked = false;
    bool IsHovered  = false;
    if (Header.IsBackRow)
    {
        // 📝 Hover is resolved against the CLIP as well as the rectangle. A header on the off-card slide is inside its own
        //    rectangle but outside the clip, and would otherwise light up and take clicks while invisible.
        const ImVec2 Pointer  = ImGui::GetIO().MousePos;
        const ImVec2 ClipMin  = DrawList->GetClipRectMin();   // public accessors, not _ClipRectStack
        const ImVec2 ClipMax  = DrawList->GetClipRectMax();
        IsHovered = (Pointer.x >= BandMinimum.x && Pointer.x < BandMaximum.x &&
                     Pointer.y >= BandMinimum.y && Pointer.y < BandMaximum.y &&
                     Pointer.x >= ClipMin.x && Pointer.x < ClipMax.x &&
                     Pointer.y >= ClipMin.y && Pointer.y < ClipMax.y);
        WasClicked = IsHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    }

    // .pane-head background is --rail-sel; .pane-head.back:hover is #292930.
    const ImU32 BandFill = IsHovered ? IM_COL32(0x29, 0x29, 0x30, 0xFF) : Palette.RailSelectedFill;
    DrawList->AddRectFilled(BandMinimum, BandMaximum, BandFill);
    // border-bottom:1px solid var(--hair)
    DrawList->AddLine(ImVec2(BandMinimum.x, BandMaximum.y - 0.5f), ImVec2(BandMaximum.x, BandMaximum.y - 0.5f),
                      Palette.Hairline, 1.0f);

    const float PaddingX = 13.0f * (Metrics.HeaderHeight / 53.0f);   // padding:0 13px, scaled with the band
    float       CursorX  = BandMinimum.x + PaddingX;
    const float CentreY  = (BandMinimum.y + BandMaximum.y) * 0.5f;

    if (Header.IsBackRow)
    {
        // The chevron: `<polyline points="15 18 9 12 15 6"/>` in a 24-box, drawn at 16px. Two strokes, not an icon lookup —
        // it is the only vector mark on the card that is authored inline rather than as a pack glyph.
        const float ArrowEdge = 16.0f;
        const float Unit      = ArrowEdge / 24.0f;
        const ImU32 ArrowInk  = IsHovered ? Palette.Ink : Palette.Muted;
        const ImVec2 Tip(CursorX + (9.0f * Unit),  CentreY);
        const ImVec2 Upper(CursorX + (15.0f * Unit), CentreY - (6.0f * Unit));
        const ImVec2 Lower(CursorX + (15.0f * Unit), CentreY + (6.0f * Unit));
        DrawList->PathLineTo(Upper);
        DrawList->PathLineTo(Tip);
        DrawList->PathLineTo(Lower);
        DrawList->PathStroke(ArrowInk, 0, 2.0f);
        CursorX += ArrowEdge + 8.0f;   // .pane-head.back{ gap:8px }
    }
    else
    {
        // The icon tile: 24px rounded square, black by default; a nib tile is the pale well with a ring, and its 46px art
        // deliberately OVERFLOWS the tile so the crop reads as a window onto the instrument.
        const float TileEdge = Metrics.HeaderIconEdge;
        const ImVec2 TileMinimum(CursorX, CentreY - (TileEdge * 0.5f));
        const ImVec2 TileMaximum(CursorX + TileEdge, CentreY + (TileEdge * 0.5f));

        if (Header.IconIsNib)
        {
            DrawList->AddRectFilled(TileMinimum, TileMaximum, Palette.PaperFill, 6.0f);
            DrawList->AddRect(TileMinimum, TileMaximum, Palette.WellRing, 6.0f, 0, 1.0f);
        }
        else
        {
            DrawList->AddRectFilled(TileMinimum, TileMaximum, IM_COL32(0, 0, 0, 0xFF), 6.0f);
        }

        if (Header.IconTexture != 0)
        {
            // 🔴 Clipped to the tile before drawing. A nib crop is rasterized at 46px into a 24px tile, so without this it
            //    paints straight over the title text beside it.
            DrawList->PushClipRect(TileMinimum, TileMaximum, true);
            const float  ArtEdge  = Header.IconIsNib ? Metrics.NibArtEdge : TileEdge;
            const float  TileMidX = (TileMinimum.x + TileMaximum.x) * 0.5f;
            const ImVec2 ArtMinimum(TileMidX - (ArtEdge * 0.5f), CentreY - (ArtEdge * 0.5f));
            DrawList->AddImage(Header.IconTexture, ArtMinimum,
                               ImVec2(ArtMinimum.x + ArtEdge, ArtMinimum.y + ArtEdge));
            DrawList->PopClipRect();
        }

        CursorX += TileEdge + 10.0f;   // .pane-head{ gap:10px }
    }

    // The tally pill is measured and reserved BEFORE the title, so a long instrument name ellipsizes against the pill
    // rather than running under it.
    float TextRight = BandMaximum.x - PaddingX;
    if (Header.TallyText != nullptr)
    {
        const ImVec2 PillText  = ImGui::CalcTextSize(Header.TallyText);
        const float  PillWidth = PillText.x + (9.0f * 2.0f);    // padding:4px 9px
        const float  PillHeight = PillText.y + (4.0f * 2.0f);
        const ImVec2 PillMinimum(TextRight - PillWidth, CentreY - (PillHeight * 0.5f));
        const ImVec2 PillMaximum(TextRight, CentreY + (PillHeight * 0.5f));

        DrawList->AddRectFilled(PillMinimum, PillMaximum, Palette.PaneFill, PillHeight * 0.5f);
        DrawList->AddText(ImVec2(PillMinimum.x + 9.0f, PillMinimum.y + 4.0f),
                          Header.TallyAccent ? Palette.Accent : Palette.Muted, Header.TallyText);

        TextRight = PillMinimum.x - 8.0f;
    }

    // 📝 Title and subtitle are drawn as a two-line stack centred on the band, matching `.h-txt` + `.h-txt .fn` with its
    //    3 px top margin. A header with no subtitle centres its single line instead, which is why the block height is
    //    resolved before either line is placed.
    const bool  HasSubtitle = (Header.Subtitle != nullptr && Header.Subtitle[0] != '\0');
    const float TitleHeight = ImGui::GetTextLineHeight();
    const float BlockHeight = HasSubtitle ? (TitleHeight + 3.0f + TitleHeight) : TitleHeight;
    float       TextY       = CentreY - (BlockHeight * 0.5f);

    if (Header.Title != nullptr && TextRight > CursorX)
    {
        // Clipped rather than truncated with an ellipsis: ImGui has no text-overflow, and clipping is what the 560 px card
        // actually needs — every title that overruns does so by a few characters.
        DrawList->PushClipRect(ImVec2(CursorX, BandMinimum.y), ImVec2(TextRight, BandMaximum.y), true);
        DrawList->AddText(ImVec2(CursorX, TextY), Palette.Ink, Header.Title);
        if (HasSubtitle)
        {
            DrawList->AddText(ImVec2(CursorX, TextY + TitleHeight + 3.0f), Palette.Faint, Header.Subtitle);
        }
        DrawList->PopClipRect();
    }

    return WasClicked;
}

} // namespace Frontier
