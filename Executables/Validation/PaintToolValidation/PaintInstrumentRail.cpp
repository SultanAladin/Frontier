/*==============================================================================================================================================
                                                        PAINTINSTRUMENTRAIL.CPP
==============================================================================================================================================*/
// 🧩 Ten family rows, their colour dots, and the accent marker on the active one.

#include "PaintInstrumentRail.h"

#include <cstdio>

namespace Frontier
{

static_assert(PaintFamilyCount <= PaintRailRowLimit,
              "PaintRailRowLimit must cover the catalogue's family count — the dot phases are indexed by family.");

namespace
{
    // .r-dot transition — cubic-bezier(.34,1.56,.64,1), an overshoot: y1 = 1.56 puts the peak at ~1.098 near t = 0.57.
    // 📝 The overshoot is worth ~0.2 px on an 8 px dot, so what actually reads on screen is the curve's FRONT LOADING —
    //    56% of the growth is done by t = 0.15 and it has all but arrived by t = 0.35. That snap is the character; the
    //    overshoot is the reason the snap does not look like a jump.
    float EaseRailDot(float Progress) { return SolvePaintCubicBezier(Progress, 0.34f, 1.56f, 0.64f, 1.0f); }

    bool IsPointerInside(ImVec2 Pointer, ImVec2 Minimum, ImVec2 Maximum)
    {
        return (Pointer.x >= Minimum.x && Pointer.x < Maximum.x && Pointer.y >= Minimum.y && Pointer.y < Maximum.y);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void AdvancePaintRail(PaintRailState& State, const PaintCardMetrics& Metrics, int ActiveFamilyIndex, float DeltaSeconds)
{
    if (DeltaSeconds <= 0.0f) { return; }

    for (int RowIndex = 0; RowIndex < PaintRailRowLimit; ++RowIndex)
    {
        const float Target = (RowIndex == ActiveFamilyIndex) ? 1.0f : 0.0f;
        float&      Phase  = State.DotPhase[RowIndex];

        if (Metrics.RailDotSeconds <= 0.0f) { Phase = Target; continue; }

        const float Step = DeltaSeconds / Metrics.RailDotSeconds;
        if (Phase < Target)      { Phase = (Phase + Step > Target) ? Target : Phase + Step; }
        else if (Phase > Target) { Phase = (Phase - Step < Target) ? Target : Phase - Step; }
    }
}


int ConstructPaintInstrumentRail(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                 const PaintRailState& State, const PaintPaneRegion& Region,
                                 const PaintFamilyDescriptor* Families, int FamilyCount,
                                 int ActiveFamilyIndex)
{
    // 📝 A pane translated fully off the card draws nothing AND tests nothing. Leaving the hit-test in would let the
    //    hidden rail swallow clicks meant for the options column sitting on top of it.
    if (!Region.IsVisible || Families == nullptr || FamilyCount <= 0) { return -1; }

    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    // .rail{ background:var(--menu); border-right:1px solid var(--hair) }
    DrawList->AddRectFilled(Region.BodyMinimum, Region.BodyMaximum, Palette.CardFill);
    DrawList->AddLine(ImVec2(Region.BodyMaximum.x - 0.5f, Region.BodyMinimum.y),
                      ImVec2(Region.BodyMaximum.x - 0.5f, Region.BodyMaximum.y), Palette.Hairline, 1.0f);

    const ImVec2 Pointer = ImGui::GetIO().MousePos;
    const ImVec2 ClipMin = DrawList->GetClipRectMin();
    const ImVec2 ClipMax = DrawList->GetClipRectMax();
    const bool   PointerInClip = IsPointerInside(Pointer, ClipMin, ClipMax);

    // .rail-body{ padding:7px; gap:1px }
    const float RowWidth = (Region.BodyMaximum.x - Region.BodyMinimum.x) - (Metrics.RailPadding * 2.0f);
    float       RowTop   = Region.BodyMinimum.y + Metrics.RailPadding;

    int ClickedIndex = -1;

    for (int RowIndex = 0; RowIndex < FamilyCount && RowIndex < PaintRailRowLimit; ++RowIndex)
    {
        const PaintFamilyDescriptor& Family = Families[RowIndex];

        const ImVec2 RowMinimum(Region.BodyMinimum.x + Metrics.RailPadding, RowTop);
        const ImVec2 RowMaximum(RowMinimum.x + RowWidth, RowTop + Metrics.RailRowHeight);
        RowTop += Metrics.RailRowHeight + Metrics.RailRowGap;

        const bool IsActive  = (RowIndex == ActiveFamilyIndex);
        // 🔴 Hover requires the pointer to be inside the CLIP as well as the row. Mid-carousel the rail is partly
        //    off-card, and a row whose rectangle has slid past the card edge would otherwise still light up.
        const bool IsHovered = PointerInClip && IsPointerInside(Pointer, RowMinimum, RowMaximum);

        if (IsHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            // 📝 Reported even when it is already active. The prototype's handler early-returns on the active row, but
            //    that is the CALLER's rule there too — it also skips the grid's fade restart, which the rail cannot see.
            ClickedIndex = RowIndex;
        }

        // .rail-item.active{ background:var(--rail-sel) }  — hover alone changes only the text colour, not the fill.
        if (IsActive)
        {
            DrawList->AddRectFilled(RowMinimum, RowMaximum, Palette.RailSelectedFill, Metrics.RailRowRounding);

            // 🔴 `.rail-item.active::before{ left:-7px }` — NEGATIVE, so the marker sits outside the row, inside the
            //    body's 7 px padding, landing flush on the pane's own left edge. Drawing it at the row's left edge
            //    instead would float it 7 px inboard, detached from the pane border it is meant to grow out of.
            const float  MarkerLeft   = RowMinimum.x - Metrics.RailPadding;
            const float  MarkerCentre = (RowMinimum.y + RowMaximum.y) * 0.5f;
            const ImVec2 MarkerMinimum(MarkerLeft, MarkerCentre - (Metrics.RailMarkerHeight * 0.5f));
            const ImVec2 MarkerMaximum(MarkerLeft + Metrics.RailMarkerWidth, MarkerCentre + (Metrics.RailMarkerHeight * 0.5f));
            // border-radius:0 3px 3px 0 — the right corners only, so it reads as emerging from the edge.
            DrawList->AddRectFilled(MarkerMinimum, MarkerMaximum, Palette.Accent, Metrics.RailMarkerWidth,
                                    ImDrawFlags_RoundCornersRight);
        }

        // padding:0 9px 0 8px — asymmetric, 8 on the left and 9 on the right.
        const float ContentLeft  = RowMinimum.x + 8.0f;
        const float ContentRight = RowMaximum.x - 9.0f;
        const float ContentMidY  = (RowMinimum.y + RowMaximum.y) * 0.5f;

        // The dot, scaled about its OWN centre — `transform:scale()` on the dot, so the row's layout does not reflow.
        // The eased value may exceed 1 on this curve, which is the overshoot and must survive to the radius.
        const float DotScale  = 1.0f + ((Metrics.RailDotChosenGrow - 1.0f) * EaseRailDot(State.DotPhase[RowIndex]));
        const float DotRadius = (Metrics.RailDotDiameter * 0.5f) * DotScale;
        const ImU32 DotColour = ResolveAuthoredColour(Family.DotColour, Palette.Muted);
        // opacity:.9 on the dot itself.
        const ImU32 DotInk    = (DotColour & ~IM_COL32_A_MASK) | ((ImU32)(0.9f * 255.0f + 0.5f) << IM_COL32_A_SHIFT);
        const float DotMidX   = ContentLeft + (Metrics.RailDotDiameter * 0.5f);
        DrawList->AddCircleFilled(ImVec2(DotMidX, ContentMidY), DotRadius, DotInk, 16);

        // gap:9px between the dot and the caption. The dot's LAYOUT box stays 8 px wide however far it has grown —
        // `flex:0 0 auto` plus a transform, so a growing dot never pushes the caption sideways.
        float CaptionLeft = ContentLeft + Metrics.RailDotDiameter + 9.0f;

        // .r-n — the tally, right-aligned and reserved before the caption so a long family name clips against it.
        char TallyText[16];
        std::snprintf(TallyText, sizeof(TallyText), "%d", Family.Tally);
        const ImVec2 TallySize = ImGui::CalcTextSize(TallyText);
        const float  TallyLeft = ContentRight - TallySize.x;
        DrawList->AddText(ImVec2(TallyLeft, ContentMidY - (TallySize.y * 0.5f)), Palette.Faint, TallyText);

        // .rail-item:hover{ color:var(--ink) } — hover and active share the same ink; only the fill distinguishes them.
        const ImU32 CaptionInk = (IsActive || IsHovered) ? Palette.Ink : Palette.Muted;
        const float CaptionRight = TallyLeft - 9.0f;
        if (Family.Caption != nullptr && CaptionRight > CaptionLeft)
        {
            const ImVec2 CaptionSize = ImGui::CalcTextSize(Family.Caption);
            DrawList->PushClipRect(ImVec2(CaptionLeft, RowMinimum.y), ImVec2(CaptionRight, RowMaximum.y), true);
            DrawList->AddText(ImVec2(CaptionLeft, ContentMidY - (CaptionSize.y * 0.5f)), CaptionInk, Family.Caption);
            DrawList->PopClipRect();
        }
    }

    return ClickedIndex;
}


void ConstructPaintRailHeader(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                              const char* ActiveDotColour, int FamilyCount, int InstrumentTally,
                              ImVec2 PaneMinimum, float PaneWidth)
{
    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    const ImVec2 BandMinimum = PaneMinimum;
    const ImVec2 BandMaximum(PaneMinimum.x + PaneWidth, PaneMinimum.y + Metrics.HeaderHeight);

    // Same band as the shell's headers: --rail-sel fill under a bottom hairline. Inert — the rail header is not clickable.
    DrawList->AddRectFilled(BandMinimum, BandMaximum, Palette.RailSelectedFill);
    DrawList->AddLine(ImVec2(BandMinimum.x, BandMaximum.y - 0.5f), ImVec2(BandMaximum.x, BandMaximum.y - 0.5f),
                      Palette.Hairline, 1.0f);

    const float PaddingX = 13.0f * (Metrics.HeaderHeight / 53.0f);
    const float CentreY  = (BandMinimum.y + BandMaximum.y) * 0.5f;
    float       CursorX  = BandMinimum.x + PaddingX;

    // 🔴 The band mark, drawn from primitives because the prototype authors it INLINE from the family's dot colour
    //    rather than pulling a pack glyph — `<rect rx=5 fill=#000>` + `<circle r=5.5>` + `<circle r=8.5 stroke-opacity=.35
    //    stroke-width=1.4>`, all in a 24-box. Rasterizing it would need one texture per family for three primitives.
    const float  TileEdge = Metrics.HeaderIconEdge;
    const float  Unit     = TileEdge / 24.0f;          // the authored viewBox is 24 wide, so every radius scales by this
    const ImVec2 TileMinimum(CursorX, CentreY - (TileEdge * 0.5f));
    const ImVec2 TileMaximum(CursorX + TileEdge, CentreY + (TileEdge * 0.5f));
    const ImVec2 TileMid((TileMinimum.x + TileMaximum.x) * 0.5f, CentreY);

    const ImU32 DotColour = ResolveAuthoredColour(ActiveDotColour, Palette.Muted);
    // stroke-opacity:.35 — an alpha on the ring only, not on the filled circle.
    const ImU32 RingInk   = (DotColour & ~IM_COL32_A_MASK) | ((ImU32)(0.35f * 255.0f + 0.5f) << IM_COL32_A_SHIFT);

    // 📝 Rounded at 6 px, the `.h-ic` CSS radius, NOT the inline `<rect rx="5">`. The svg is clipped by its container's
    //    `border-radius:6px`, so the container's corner is the one that shows and the authored rx never appears.
    DrawList->AddRectFilled(TileMinimum, TileMaximum, IM_COL32(0, 0, 0, 0xFF), 6.0f);
    DrawList->AddCircleFilled(TileMid, 5.5f * Unit, DotColour, 20);
    DrawList->AddCircle(TileMid, 8.5f * Unit, RingInk, 24, 1.4f * Unit);

    CursorX += TileEdge + 10.0f;   // .pane-head{ gap:10px }

    // The total instrument tally, muted rather than accent — the rail's pill counts the whole catalogue, while the grid's
    // counts the live family, and the prototype inks only the live one.
    char TallyText[16];
    std::snprintf(TallyText, sizeof(TallyText), "%d", InstrumentTally);
    const ImVec2 PillText   = ImGui::CalcTextSize(TallyText);
    const float  PillWidth  = PillText.x + (9.0f * 2.0f);
    const float  PillHeight = PillText.y + (4.0f * 2.0f);
    const float  PillRight  = BandMaximum.x - PaddingX;
    const ImVec2 PillMinimum(PillRight - PillWidth, CentreY - (PillHeight * 0.5f));
    DrawList->AddRectFilled(PillMinimum, ImVec2(PillRight, CentreY + (PillHeight * 0.5f)),
                            Palette.PaneFill, PillHeight * 0.5f);
    DrawList->AddText(ImVec2(PillMinimum.x + 9.0f, PillMinimum.y + 4.0f), Palette.Muted, TallyText);

    // 🔴 The title is the LITERAL "Instruments", not the active family's caption. Only `bandSub` and `bandIcon` are
    //    rewritten as the rail changes (prototype line 2906 authors the word inline, and RenderRail sets only the other
    //    two), so the active family reaches this band solely as the dot colour in the mark above. Substituting the
    //    caption here would make the left header echo the right one, which names the family already.
    char SubtitleText[32];
    std::snprintf(SubtitleText, sizeof(SubtitleText), "%d families", FamilyCount);

    const float TitleHeight = ImGui::GetTextLineHeight();
    const float BlockHeight = TitleHeight + 3.0f + TitleHeight;
    const float TextY       = CentreY - (BlockHeight * 0.5f);
    const float TextRight   = PillMinimum.x - 8.0f;

    if (TextRight > CursorX)
    {
        DrawList->PushClipRect(ImVec2(CursorX, BandMinimum.y), ImVec2(TextRight, BandMaximum.y), true);
        DrawList->AddText(ImVec2(CursorX, TextY), Palette.Ink, "Instruments");
        DrawList->AddText(ImVec2(CursorX, TextY + TitleHeight + 3.0f), Palette.Faint, SubtitleText);
        DrawList->PopClipRect();
    }
}

} // namespace Frontier
