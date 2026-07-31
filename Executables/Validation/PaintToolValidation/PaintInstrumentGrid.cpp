/*==============================================================================================================================================
                                                        PAINTINSTRUMENTGRID.CPP
==============================================================================================================================================*/
// 🧩 A family's instruments as 4-column tiles, each a round well onto the nib crop, and the pinned tally foot.

#include "PaintInstrumentGrid.h"
#include "PaintIconPack.h"
#include "PaintIconStore.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
    // .tile .t-lbl{ font-size:9.5px; line-height:1.25; -webkit-line-clamp:2 }
    constexpr int   GridLabelLineLimit  = 2;
    constexpr float GridLabelLineFactor = 1.25f;

    // .tile{ padding:8px 4px 7px; gap:6px } — asymmetric vertically, 8 above and 7 below.
    constexpr float TilePaddingTop    = 8.0f;
    constexpr float TilePaddingBottom = 7.0f;
    constexpr float TilePaddingSide   = 4.0f;
    constexpr float TileInnerGap      = 6.0f;

    // @keyframes fade{ from{ opacity:0; transform:translateX(4px) } to{ opacity:1; transform:none } }
    constexpr float GridFadeDriftX = 4.0f;

    bool IsPointerInside(ImVec2 Pointer, ImVec2 Minimum, ImVec2 Maximum)
    {
        return (Pointer.x >= Minimum.x && Pointer.x < Maximum.x && Pointer.y >= Minimum.y && Pointer.y < Maximum.y);
    }

    float Saturate(float Value) { return (Value < 0.0f) ? 0.0f : ((Value > 1.0f) ? 1.0f : Value); }

    ImU32 ScaleAlpha(ImU32 Colour, float Alpha)
    {
        const float Existing = (float)((Colour >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
        const ImU32 Channels = Colour & ~IM_COL32_A_MASK;
        const ImU32 Scaled   = (ImU32)(Saturate(Existing * Alpha) * 255.0f + 0.5f);
        return Channels | (Scaled << IM_COL32_A_SHIFT);
    }

    // 📝 `-webkit-line-clamp:2` wraps on words and then clips, so a two-word label splits and a long single word is cut.
    //    Reproduced by measuring rather than by counting characters: the labels are proportional-font display names
    //    ("Classic Gold-Nib Fountain") and a character count would break at the wrong place on every one of them.
    //    🔴 Returns the byte range per line, not a copy, so no allocation happens inside the per-tile loop.
    struct GridLabelLine { const char* Begin; const char* End; };

    int WrapGridLabel(const char* Label, float AvailableWidth, GridLabelLine* Lines, int LineLimit)
    {
        if (Label == nullptr || Label[0] == '\0' || LineLimit <= 0) { return 0; }

        int         LineCount = 0;
        const char* LineStart = Label;
        const char* Cursor    = Label;
        const char* LastBreak = nullptr;   // the most recent space that still fitted

        while (*Cursor != '\0')
        {
            // Advance one word.
            const char* WordEnd = Cursor;
            while (*WordEnd != '\0' && *WordEnd != ' ') { ++WordEnd; }

            const float Width = ImGui::CalcTextSize(LineStart, WordEnd).x;
            if (Width > AvailableWidth && LastBreak != nullptr)
            {
                // The line overflows: break at the previous space and restart from just after it.
                Lines[LineCount].Begin = LineStart;
                Lines[LineCount].End   = LastBreak;
                ++LineCount;
                if (LineCount >= LineLimit) { return LineCount; }

                LineStart = LastBreak + 1;
                Cursor    = LineStart;
                LastBreak = nullptr;
                continue;
            }

            LastBreak = (*WordEnd == ' ') ? WordEnd : nullptr;
            Cursor    = (*WordEnd == ' ') ? (WordEnd + 1) : WordEnd;
        }

        if (LineStart < Cursor || LineCount == 0)
        {
            Lines[LineCount].Begin = LineStart;
            Lines[LineCount].End   = Cursor;
            ++LineCount;
        }
        return LineCount;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void AdvancePaintGrid(PaintGridState& State, const PaintCardMetrics& Metrics, float DeltaSeconds)
{
    if (DeltaSeconds <= 0.0f) { return; }

    if (Metrics.GridSwapSeconds <= 0.0f) { State.SwapPhase = 1.0f; return; }

    State.SwapPhase = Saturate(State.SwapPhase + (DeltaSeconds / Metrics.GridSwapSeconds));
}


void RestartPaintGridFade(PaintGridState& State, int FamilyIndex)
{
    // 🔴 Guarded on the family, which is what makes this safe to call from a per-frame path: an unguarded reset called
    //    every frame would pin SwapPhase at 0 and the grid would never become opaque. The prototype restarts the fade
    //    at the CLICK only (`RenderGrid(true)`), so the family is the signal that a restart is genuinely new.
    //    📝 A re-click on the ALREADY-ACTIVE family therefore does NOT replay the fade here, where the prototype's does.
    //       Deliberate: the prototype re-runs its whole grid build on every click and the animation rides along, whereas
    //       this state persists. Replaying on same-family clicks would need a separate explicit call, and the caller has
    //       one — RequestPaintGridReplay below — rather than making the common case a re-render hazard.
    if (State.FadedFamilyIndex == FamilyIndex) { return; }

    State.SwapPhase        = 0.0f;
    State.FadedFamilyIndex = FamilyIndex;
}


void RequestPaintGridReplay(PaintGridState& State, int FamilyIndex)
{
    // 📝 The unguarded form, for the one site that genuinely means "replay it now" — a click on the active family, which
    //    the prototype animates too. Kept separate from the guarded restart so a render path cannot reach it by accident.
    State.SwapPhase        = 0.0f;
    State.FadedFamilyIndex = FamilyIndex;
}


float ResolvePaintGridContentHeight(const PaintCardMetrics& Metrics, int FamilyIndex)
{
    int FirstIndex = 0, LastIndex = 0;
    ResolvePaintFamilyRange(FamilyIndex, FirstIndex, LastIndex);

    const int Stock = LastIndex - FirstIndex;
    if (Stock <= 0) { return Metrics.GridPadding * 2.0f; }

    const int   Columns = (int)Metrics.GridColumnCount;
    const int   Rows    = (Stock + Columns - 1) / Columns;

    // A tile's height is its own content, not a fixed number: well + gap + two label lines + asymmetric padding.
    const float LabelHeight = ImGui::GetTextLineHeight() * GridLabelLineFactor * (float)GridLabelLineLimit;
    const float TileHeight  = TilePaddingTop + Metrics.WellDiameter + TileInnerGap + LabelHeight + TilePaddingBottom;

    return (Metrics.GridPadding * 2.0f) + ((float)Rows * TileHeight) + ((float)(Rows - 1) * Metrics.TileGap);
}


int ConstructPaintInstrumentGrid(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                                 const PaintGridState& State, const PaintPaneRegion& Region,
                                 PaintGridArtSources& Art,
                                 int FamilyIndex, int SelectedInstrumentIndex, float ScrollOffset)
{
    // 📝 Same reason the rail bails: a pane translated off the card must neither draw nor hit-test, or the hidden grid
    //    swallows clicks meant for whatever slid on top of it.
    if (!Region.IsVisible) { return -1; }

    int FirstIndex = 0, LastIndex = 0;
    ResolvePaintFamilyRange(FamilyIndex, FirstIndex, LastIndex);
    if (LastIndex <= FirstIndex) { return -1; }

    int InstrumentCount = 0;
    const PaintInstrumentDescriptor* const Instruments = ResolvePaintInstruments(InstrumentCount);
    if (Instruments == nullptr) { return -1; }

    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    // .grid-pane{ background:var(--menu-2) }
    DrawList->AddRectFilled(Region.BodyMinimum, Region.BodyMaximum, Palette.PaneFill);

    // 🔴 The fade is opacity AND a 4 px horizontal drift, and both must be applied or the swap reads as a plain
    //    cross-fade. The drift is what makes the new family appear to arrive from the right.
    const float FadeEased = Saturate(State.SwapPhase);
    const float FadeDrift = GridFadeDriftX * (1.0f - FadeEased);

    // The body clips its own scroll; intersected with the shell's clip so a mid-carousel pane stays bounded by the card.
    DrawList->PushClipRect(Region.BodyMinimum, Region.BodyMaximum, true);

    const ImVec2 Pointer       = ImGui::GetIO().MousePos;
    const ImVec2 ClipMin       = DrawList->GetClipRectMin();
    const ImVec2 ClipMax       = DrawList->GetClipRectMax();
    const bool   PointerInClip = IsPointerInside(Pointer, ClipMin, ClipMax);

    const int   Columns    = (int)Metrics.GridColumnCount;
    const float BodyWidth  = (Region.BodyMaximum.x - Region.BodyMinimum.x) - (Metrics.GridPadding * 2.0f);
    // repeat(4, 1fr) with a 6 px gap: the gaps come out of the track, so a column is what remains divided four ways.
    const float TileWidth  = (BodyWidth - (Metrics.TileGap * (float)(Columns - 1))) / (float)Columns;

    const float LabelLine   = ImGui::GetTextLineHeight() * GridLabelLineFactor;
    const float LabelHeight = LabelLine * (float)GridLabelLineLimit;
    const float TileHeight  = TilePaddingTop + Metrics.WellDiameter + TileInnerGap + LabelHeight + TilePaddingBottom;

    const float OriginX = Region.BodyMinimum.x + Metrics.GridPadding + FadeDrift;
    const float OriginY = Region.BodyMinimum.y + Metrics.GridPadding - ScrollOffset;

    int ClickedIndex = -1;

    for (int Index = FirstIndex; Index < LastIndex && Index < InstrumentCount; ++Index)
    {
        const PaintInstrumentDescriptor& Instrument = Instruments[Index];

        const int   Slot   = Index - FirstIndex;
        const int   Column = Slot % Columns;
        const int   Row    = Slot / Columns;

        const ImVec2 TileMinimum(OriginX + ((TileWidth + Metrics.TileGap) * (float)Column),
                                 OriginY + ((TileHeight + Metrics.TileGap) * (float)Row));
        const ImVec2 TileMaximum(TileMinimum.x + TileWidth, TileMinimum.y + TileHeight);

        // 📝 Rows scrolled entirely out of the body are skipped rather than drawn and clipped. At 23 instruments in six
        //    rows this is not a throughput concern — it is that an off-body tile must not hit-test, same as an off-card pane.
        if (TileMaximum.y < Region.BodyMinimum.y || TileMinimum.y > Region.BodyMaximum.y) { continue; }

        const bool IsChosen  = (Index == SelectedInstrumentIndex);
        const bool IsHovered = PointerInClip && IsPointerInside(Pointer, TileMinimum, TileMaximum);

        if (IsHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { ClickedIndex = Index; }

        // .tile:active{ transform:translateY(1px) scale(.98) } — a press offset, applied to the whole tile.
        const bool  IsPressed = IsHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        ImVec2      DrawMinimum = TileMinimum;
        ImVec2      DrawMaximum = TileMaximum;
        if (IsPressed)
        {
            const float ShrinkX = (TileWidth  * (1.0f - 0.98f)) * 0.5f;
            const float ShrinkY = (TileHeight * (1.0f - 0.98f)) * 0.5f;
            DrawMinimum = ImVec2(TileMinimum.x + ShrinkX, TileMinimum.y + ShrinkY + 1.0f);
            DrawMaximum = ImVec2(TileMaximum.x - ShrinkX, TileMaximum.y - ShrinkY + 1.0f);
        }

        // .tile rest / :hover / .on — the fill and the border move together, and the border is transparent at rest rather
        // than absent, so a hovered tile does not shift by a pixel when its border appears.
        const ImU32 TileFill   = IsChosen ? Palette.TileChosenFill : (IsHovered ? Palette.TileHoverFill : Palette.TileFill);
        const ImU32 TileBorder = IsChosen ? Palette.Accent : (IsHovered ? Palette.HairlineStrong : IM_COL32(0, 0, 0, 0));

        DrawList->AddRectFilled(DrawMinimum, DrawMaximum, ScaleAlpha(TileFill, FadeEased), Metrics.TileRounding);
        DrawList->AddRect(DrawMinimum, DrawMaximum, ScaleAlpha(TileBorder, FadeEased), Metrics.TileRounding, 0, 1.0f);

        // ── the well ──
        const float  WellRadius = Metrics.WellDiameter * 0.5f;
        const ImVec2 WellCentre((DrawMinimum.x + DrawMaximum.x) * 0.5f,
                                DrawMinimum.y + TilePaddingTop + WellRadius);

        // border-radius:999px on a 46px box — a circle, and `overflow:hidden` on it is what crops the art.
        DrawList->AddCircleFilled(WellCentre, WellRadius, ScaleAlpha(Palette.PaneFill, FadeEased), 32);

        // 🔴 The art is clipped to the WELL, not to the tile. The nib crop is drawn at 46 px into a 46 px circle so the
        //    rectangle would fit either way — but the FULL strip is 5:1 and would run straight across the neighbouring
        //    tiles without this.
        ImTextureID ArtTexture = 0;
        float       ArtWidth   = Metrics.WellDiameter;
        float       ArtHeight  = Metrics.WellDiameter;

        if (Art.ArtMode == PaintWellArtMode::FullStrip && Art.StripStore != nullptr)
        {
            const PaintStripTexture* const Strip = ResolvePaintStrip(*Art.StripStore, Index);
            if (Strip != nullptr && Strip->PixelHeight > 0u)
            {
                ArtTexture = Strip->StripTextureId;
                // Letterboxed: the 5:1 strip fits the well's WIDTH and keeps its aspect, so the whole instrument reads.
                const float Aspect = (float)Strip->PixelWidth / (float)Strip->PixelHeight;
                ArtWidth  = Metrics.WellDiameter;
                ArtHeight = Metrics.WellDiameter / Aspect;
            }
        }

        // 📝 The nib crop is also the FALLBACK when a strip has not uploaded, rather than an empty well: the store's own
        //    contract says a null strip should fall back to the square art, and a blank circle beside populated ones
        //    reads as a load failure — the same reasoning the prototype gives for its header stand-in.
        if (ArtTexture == 0 && Art.Registry != nullptr)
        {
            ArtTexture = ResolveIconTexture(*Art.Registry, ResolvePaintNibKey(Index));
            ArtWidth   = Metrics.NibArtEdge;
            ArtHeight  = Metrics.NibArtEdge;
        }

        if (ArtTexture != 0)
        {
            const ImVec2 ArtMinimum(WellCentre.x - (ArtWidth * 0.5f), WellCentre.y - (ArtHeight * 0.5f));
            const ImVec2 ArtMaximum(ArtMinimum.x + ArtWidth, ArtMinimum.y + ArtHeight);

            // 🔴 `overflow:hidden` on a border-radius:999px box is a CIRCULAR crop, and PushClipRect is axis-aligned — it can only
            //    ever crop to the well's bounding SQUARE, so the art kept filling the four corners and the ring below then landed
            //    on top of it. That is why the well read as "background, then border over it" rather than as a round window.
            //    Two paths, because neither one covers both cases:
            //      · the square nib crop fills the well exactly, so AddImageRounded at the full radius IS the circle;
            //      · the 5:1 strip is letterboxed to a short wide band whose own corners are nowhere near the circle, so rounding
            //        its rectangle would crop nothing — it still needs the rect clip, with the corners painted back afterwards.
            const bool ArtFillsWell = (ArtWidth >= (WellRadius * 2.0f) - 0.5f) && (ArtHeight >= (WellRadius * 2.0f) - 0.5f);

            if (ArtFillsWell)
            {
                DrawList->AddImageRounded(ArtTexture, ArtMinimum, ArtMaximum, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                                          ScaleAlpha(IM_COL32_WHITE, FadeEased), WellRadius);
            }
            else
            {
                // 📝 The band is centred and shorter than the well, so the art that escapes the circle is at the band's two ENDS,
                //    not at the corners of a square. Clipping to the circle's own CHORD at the band's height crops exactly that:
                //    at half-height h the circle spans ±sqrt(r² - h²), so a clip that wide is the circle's true width there.
                //    Exact for the whole band because the widest chord over its span is the one at its outermost edge.
                const float BandHalfHeight = (ArtHeight * 0.5f);
                const float ChordHalfWidth = (BandHalfHeight < WellRadius)
                                           ? std::sqrt((WellRadius * WellRadius) - (BandHalfHeight * BandHalfHeight))
                                           : 0.0f;

                DrawList->PushClipRect(ImVec2(WellCentre.x - ChordHalfWidth, WellCentre.y - WellRadius),
                                       ImVec2(WellCentre.x + ChordHalfWidth, WellCentre.y + WellRadius), true);
                DrawList->AddImage(ArtTexture, ArtMinimum, ArtMaximum,
                                   ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ScaleAlpha(IM_COL32_WHITE, FadeEased));
                DrawList->PopClipRect();
            }
        }

        // The ring, drawn AFTER the art so the art cannot paint over it — `border` on the well sits above its overflow.
        const ImU32 RingInk = IsChosen ? Palette.Accent : (IsHovered ? Palette.WellRingHover : Palette.WellRing);
        DrawList->AddCircle(WellCentre, WellRadius, ScaleAlpha(RingInk, FadeEased), 32, 1.0f);
        if (IsChosen)
        {
            // .tile.on .well{ box-shadow:0 0 0 2px rgba(91,140,255,.2) } — a 2 px halo outside the ring.
            DrawList->AddCircle(WellCentre, WellRadius + 1.0f, ScaleAlpha(ScaleAlpha(Palette.Accent, 0.2f), FadeEased),
                                32, 2.0f);
        }

        // ── the label ──
        const float LabelTop   = WellCentre.y + WellRadius + TileInnerGap;
        const float LabelWidth = (DrawMaximum.x - DrawMinimum.x) - (TilePaddingSide * 2.0f);
        const ImU32 LabelInk   = (IsChosen || IsHovered) ? Palette.Ink : Palette.Muted;

        GridLabelLine Lines[GridLabelLineLimit] = {};
        const int     LineCount = WrapGridLabel(Instrument.Label, LabelWidth, Lines, GridLabelLineLimit);

        DrawList->PushClipRect(ImVec2(DrawMinimum.x + TilePaddingSide, LabelTop),
                              ImVec2(DrawMaximum.x - TilePaddingSide, LabelTop + LabelHeight), true);
        for (int LineIndex = 0; LineIndex < LineCount; ++LineIndex)
        {
            // text-align:center — each line centres independently, so a two-line label is not left-ragged.
            const float LineWidth = ImGui::CalcTextSize(Lines[LineIndex].Begin, Lines[LineIndex].End).x;
            const float LineX     = ((DrawMinimum.x + DrawMaximum.x) * 0.5f) - (LineWidth * 0.5f);
            DrawList->AddText(ImVec2(LineX, LabelTop + (LabelLine * (float)LineIndex)),
                              ScaleAlpha(LabelInk, FadeEased), Lines[LineIndex].Begin, Lines[LineIndex].End);
        }
        DrawList->PopClipRect();
    }

    DrawList->PopClipRect();
    return ClickedIndex;
}


void ConstructPaintGridFoot(const PaintCardPalette& Palette, const PaintCardMetrics& Metrics,
                            const char* ActiveInstrumentLabel, int CatalogueTotal,
                            ImVec2 FootMinimum, float PaneWidth)
{
    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    const ImVec2 FootMaximum(FootMinimum.x + PaneWidth, FootMinimum.y + Metrics.GridFootHeight);

    // .grid-foot{ border-top:1px solid var(--hair) } over the pane's own fill — no separate background.
    DrawList->AddRectFilled(FootMinimum, FootMaximum, Palette.PaneFill);
    DrawList->AddLine(ImVec2(FootMinimum.x, FootMinimum.y + 0.5f), ImVec2(FootMaximum.x, FootMinimum.y + 0.5f),
                      Palette.Hairline, 1.0f);

    const float CentreY = (FootMinimum.y + FootMaximum.y) * 0.5f;
    float       CursorX = FootMinimum.x + 13.0f;          // padding:0 13px
    const float TextY   = CentreY - (ImGui::GetTextLineHeight() * 0.5f);

    // 🔴 Three runs, not one formatted string: `Library <b>102</b> instruments` inks only the NUMBER at the accent, and
    //    the words around it muted. Printing the whole sentence in one colour loses the emphasis the markup is carrying.
    char Digits[16];
    std::snprintf(Digits, sizeof(Digits), "%d", CatalogueTotal);

    DrawList->AddText(ImVec2(CursorX, TextY), Palette.Muted, "Library ");
    CursorX += ImGui::CalcTextSize("Library ").x;
    DrawList->AddText(ImVec2(CursorX, TextY), Palette.Accent, Digits);
    CursorX += ImGui::CalcTextSize(Digits).x;
    DrawList->AddText(ImVec2(CursorX, TextY), Palette.Muted, " instruments");
    CursorX += ImGui::CalcTextSize(" instruments").x + 11.0f;   // .grid-foot{ gap:11px }

    // The second span: the active instrument, or the prototype's own empty-state words.
    char BandText[96];
    if (ActiveInstrumentLabel != nullptr && ActiveInstrumentLabel[0] != '\0')
    {
        std::snprintf(BandText, sizeof(BandText), "Active \xC2\xB7 %s", ActiveInstrumentLabel);
    }
    else
    {
        std::snprintf(BandText, sizeof(BandText), "No instrument selected");
    }

    DrawList->PushClipRect(ImVec2(CursorX, FootMinimum.y), ImVec2(FootMaximum.x - 13.0f, FootMaximum.y), true);
    DrawList->AddText(ImVec2(CursorX, TextY), Palette.Faint, BandText);
    DrawList->PopClipRect();
}

} // namespace Frontier
