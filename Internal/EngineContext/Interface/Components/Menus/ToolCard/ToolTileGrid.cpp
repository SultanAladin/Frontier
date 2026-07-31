/*==============================================================================================================================================
                                                        TOOLTILEGRID.CPP
==============================================================================================================================================*/
// 🧩 Lays out and draws the open band's tiles. The layout arithmetic is shared between the measure and the draw so a scroll range can never disagree
//    with what the grid then paints — both walk the same cell advance over the same filtered set.

#include "ToolTileGrid.h"
#include "ToolGlyphInscription.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float GridBodyPadding     =  9.0f;   // [px] - .grid-body padding
    constexpr float TileGlyphEdge       = 27.0f;   // [px] - .tile svg
    constexpr float TilePaddingTop      = 10.0f;   // [px] - .tile padding-top
    constexpr float TileGlyphCaptionGap =  6.0f;   // [px] - .tile gap
    constexpr float TilePaddingBottom   =  8.0f;   // [px] - .tile padding-bottom
    constexpr float KeystrokeInsetX     =  6.0f;   // [px] - tile right edge to the accelerator
    constexpr float KeystrokeInsetY     =  4.5f;   // [px] - tile top edge to the accelerator
    constexpr float CaptionSideInset    =  4.0f;   // [px] - caption room lost to each side

    // 📝 A greyed tile keeps its glyph legible but clearly inert by thinning the tone it would otherwise draw in, rather than by
    //    naming a second "disabled" colour for every tone above — which is what the prototype's `opacity` rules do.
    ImU32 BlendTowardTransparent(ImU32 Tone, float Alpha)
    {
        ImVec4 Channels = ImGui::ColorConvertU32ToFloat4(Tone);
        Channels.w     *= Alpha;
        return ImGui::ColorConvertFloat4ToU32(Channels);
    }

    // 📝 A centred caption over at most two lines, broken at the last space that still fits. A caption with no space
    //    ("Un-Subdivide") overruns and is clipped by the grid body, which is what the prototype's `text-overflow` does.
    void InscribeTileCaption(ImDrawList* Canvas, const char* Text, float CentreX, float TopY, float Available, ImU32 Tint)
    {
        if (Text == nullptr)
        {
            return;
        }

        const ImVec2 WholeSize = ImGui::CalcTextSize(Text);
        if (WholeSize.x <= Available)
        {
            Canvas->AddText(ImVec2(CentreX - WholeSize.x * 0.5f, TopY), Tint, Text);
            return;
        }

        const char* Break = nullptr;
        for (const char* Cursor = Text; *Cursor != '\0'; ++Cursor)
        {
            if (*Cursor == ' ' && ImGui::CalcTextSize(Text, Cursor).x <= Available)
            {
                Break = Cursor;
            }
        }
        if (Break == nullptr)
        {
            Canvas->AddText(ImVec2(CentreX - WholeSize.x * 0.5f, TopY), Tint, Text);
            return;
        }

        const ImVec2 FirstSize  = ImGui::CalcTextSize(Text, Break);
        const ImVec2 SecondSize = ImGui::CalcTextSize(Break + 1);
        Canvas->AddText(ImVec2(CentreX - FirstSize.x * 0.5f, TopY), Tint, Text, Break);
        Canvas->AddText(ImVec2(CentreX - SecondSize.x * 0.5f, TopY + ImGui::GetTextLineHeight()), Tint, Break + 1);
    }

    // 📝 One cell's footprint. Two caption lines are ALWAYS budgeted so a one-line tile and a two-line tile in the same row keep the
    //    same height — a grid whose rows jog by a text line reads as broken alignment rather than as tighter labels.
    struct CellGeometry
    {
        float TileWidth;
        float TileHeight;
        float Advance;      // [px] - row pitch, height plus gap
        int   ColumnCount;
    };

    CellGeometry ResolveCellGeometry(float ColumnWidth, const ToolCardMetrics& Metrics)
    {
        CellGeometry Geometry = {};
        Geometry.ColumnCount  = static_cast<int>(Metrics.GridColumnCount);
        if (Geometry.ColumnCount < 1)
        {
            Geometry.ColumnCount = 1;
        }

        const float Span = ColumnWidth - GridBodyPadding * 2.0f;
        Geometry.TileWidth  = (Span - Metrics.TileGap * static_cast<float>(Geometry.ColumnCount - 1))
                            / static_cast<float>(Geometry.ColumnCount);
        Geometry.TileHeight = TilePaddingTop + TileGlyphEdge + TileGlyphCaptionGap + TilePaddingBottom
                            + ImGui::GetTextLineHeight() * 2.0f;
        Geometry.Advance    = Geometry.TileHeight + Metrics.TileGap;
        return Geometry;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float MeasureToolTileGridHeight(const ToolBandDescriptor& Band,
                               unsigned int              StratumBit,
                               float                     ColumnWidth,
                               const ToolCardMetrics&    Metrics)
{
    const int Applicable = TallyApplicableTiles(Band, StratumBit);
    if (Applicable == 0)
    {
        return GridBodyPadding * 2.0f;
    }

    const CellGeometry Geometry = ResolveCellGeometry(ColumnWidth, Metrics);
    const int          RowCount = (Applicable + Geometry.ColumnCount - 1) / Geometry.ColumnCount;

    // The trailing gap belongs to the pitch between rows, not after the last one, so the padding closes the body cleanly.
    return GridBodyPadding * 2.0f + static_cast<float>(RowCount) * Geometry.TileHeight
         + static_cast<float>(RowCount - 1) * Metrics.TileGap;
}


ToolTileOutcome InscribeToolTileGrid(const SvgIconRegistry*    Icons,
                                     const ToolBandDescriptor& Band,
                                     unsigned int              StratumBit,
                                     ImVec2                    Origin,
                                     float                     ColumnWidth,
                                     const ToolCardPalette&    Palette,
                                     const ToolCardMetrics&    Metrics)
{
    ToolTileOutcome Outcome = {};
    Outcome.HoveredTile     = -1;
    Outcome.ActivatedTile   = -1;

    if (Band.Tiles == nullptr || Band.TileCount <= 0)
    {
        return Outcome;
    }

    ImDrawList* const  Canvas   = ImGui::GetWindowDrawList();
    const CellGeometry Geometry = ResolveCellGeometry(ColumnWidth, Metrics);

    float CursorY = Origin.y + GridBodyPadding;
    int   Column  = 0;

    for (int TileIndex = 0; TileIndex < Band.TileCount; ++TileIndex)
    {
        const ToolTileDescriptor& Tile = Band.Tiles[TileIndex];

        // 🔴 The three-state model. A tool whose stratum does not apply is ABSENT — not greyed — because a greyed tile for a tool
        //    this selection can never run fills the grid with noise; a tool that applies but cannot run yet is GATED, kept present
        //    with its shortfall, because that shortfall is the one thing the reader actually needs.
        const ToolAvailability Availability = ResolveTileAvailability(Tile, StratumBit);
        if (Availability == ToolAvailability::Absent)
        {
            continue;
        }
        const bool Gated = (Availability == ToolAvailability::Gated);

        const ImVec2 TileMin(Origin.x + GridBodyPadding
                             + static_cast<float>(Column) * (Geometry.TileWidth + Metrics.TileGap), CursorY);
        const ImVec2 TileMax(TileMin.x + Geometry.TileWidth, TileMin.y + Geometry.TileHeight);

        ImGui::SetCursorScreenPos(TileMin);
        ImGui::PushID(TileIndex);
        const bool Pressed = ImGui::InvisibleButton("##tool", ImVec2(Geometry.TileWidth, Geometry.TileHeight));
        const bool Hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        if (Hovered)
        {
            Outcome.HoveredTile = TileIndex;
        }
        // A gated tile reports its hover so the shortfall shows, but never its press — the gate is enforced here rather than left
        // to a caller that would have to re-derive the same availability to know it should ignore the click.
        if (Pressed && !Gated)
        {
            Outcome.ActivatedTile = TileIndex;
        }

        const ImU32 Ground = Gated ? Palette.GatedTileFill : (Hovered ? Palette.TileHoverFill : Palette.TileFill);
        Canvas->AddRectFilled(TileMin, TileMax, Ground, Metrics.TileRounding);
        if (Hovered && !Gated)
        {
            Canvas->AddRect(TileMin, TileMax, Palette.Hairline, Metrics.TileRounding, 0, 1.0f);
        }

        const float GlyphTop = TileMin.y + TilePaddingTop;
        InscribeToolGlyph(Icons, Tile.GlyphName,
                          ImVec2((TileMin.x + TileMax.x) * 0.5f - TileGlyphEdge * 0.5f, GlyphTop),
                          TileGlyphEdge, Gated);

        // The shortfall REPLACES the caption under the pointer rather than sitting beside it: at this tile size there is room for
        // one caption, and the reason a tool cannot run outranks its name once the reader has asked by pointing at it.
        const float CaptionTop   = GlyphTop + TileGlyphEdge + TileGlyphCaptionGap;
        const float CaptionRoom  = Geometry.TileWidth - CaptionSideInset * 2.0f;
        const float CaptionMidX  = (TileMin.x + TileMax.x) * 0.5f;
        if (Gated && Hovered && Tile.Shortfall != nullptr)
        {
            InscribeTileCaption(Canvas, Tile.Shortfall, CaptionMidX, CaptionTop, CaptionRoom, Palette.ShortfallInk);
        }
        else
        {
            InscribeTileCaption(Canvas, Tile.Label, CaptionMidX, CaptionTop, CaptionRoom,
                                Gated ? BlendTowardTransparent(Palette.Muted, 0.7f) : Palette.Muted);
        }

        // The accelerator is suppressed on a gated tile: printing a shortcut for a tool the same tile says cannot run invites the
        // reader to press it.
        if (Tile.Keystroke != nullptr && !Gated)
        {
            const ImVec2 KeySize = ImGui::CalcTextSize(Tile.Keystroke);
            Canvas->AddText(ImVec2(TileMax.x - KeystrokeInsetX - KeySize.x, TileMin.y + KeystrokeInsetY),
                            Palette.Faint, Tile.Keystroke);
        }

        ++Column;
        if (Column == Geometry.ColumnCount)
        {
            Column   = 0;
            CursorY += Geometry.Advance;
        }
    }

    return Outcome;
}

} // namespace Frontier
