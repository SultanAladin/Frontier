/*==============================================================================================================================================
                                                             ACTIONSTRIP.CPP
==============================================================================================================================================*/
// 🧩 Lays out and draws the open cluster's actions. The layout arithmetic is shared between the measure and the draw so a scroll range can never
//    disagree with what the grid then paints — both walk the same cell advance over the same filtered set. Ported from ToolTileGrid, with each tile's
//    standing read from the VerdictBinding instead of a stratum mask.

#include "ActionStrip.h"

#include "../Descriptor/ActionDescriptor.h"
#include "../Rasterization/GlyphRasterization.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float GridBodyPadding     =  9.0f;   // [px] - .grid-body padding
    constexpr float TilePaddingTop      = 10.0f;   // [px] - .tile padding-top
    constexpr float TileGlyphCaptionGap =  6.0f;   // [px] - .tile gap
    constexpr float TilePaddingBottom   =  8.0f;   // [px] - .tile padding-bottom
    constexpr float KeystrokeInsetX     =  6.0f;   // [px] - tile right edge to the accelerator
    constexpr float KeystrokeInsetY     =  4.5f;   // [px] - tile top edge to the accelerator
    constexpr float CaptionSideInset    =  4.0f;   // [px] - caption room lost to each side

    // 📝 The shortfall's ink. The shared palette dropped the gate-specific ShortfallInk (it was a modelling/construction concept), so the console
    //    states its own here — the same amber a gated glyph and the absent-marker draw in, so the whole gated vocabulary reads in one tone.
    constexpr ImU32 ShortfallInk = IM_COL32(0xC9, 0xA2, 0x27, 0xE0);

    // 📝 A greyed tile keeps its glyph legible but clearly inert by thinning the tone it would otherwise draw in, rather than by naming a second
    //    "disabled" colour for every tone above.
    ImU32 BlendTowardTransparent(ImU32 Tone, float Alpha)
    {
        ImVec4 Channels = ImGui::ColorConvertU32ToFloat4(Tone);
        Channels.w     *= Alpha;
        return ImGui::ColorConvertFloat4ToU32(Channels);
    }

    // 📝 A centred caption over at most two lines, broken at the last space that still fits. A caption with no space ("Un-Subdivide") overruns and is
    //    clipped by the grid body.
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

    // 📝 One cell's footprint. Two caption lines are ALWAYS budgeted so a one-line tile and a two-line tile in the same row keep the same height — a
    //    grid whose rows jog by a text line reads as broken alignment rather than as tighter labels.
    struct CellGeometry
    {
        float TileWidth;
        float TileHeight;
        float Advance;      // [px] - row pitch, height plus gap
        int   ColumnCount;
    };

    CellGeometry ResolveCellGeometry(float ColumnWidth, const MetricsSpecification& Metrics)
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
        // The cell GROWS with the artwork rather than cropping it: paint's 46 px well is nearly twice modelling's 27 px mark, and a fixed cell would
        // have overlapped the caption instead of simply making the grid taller.
        Geometry.TileHeight = TilePaddingTop + Metrics.TileArtEdge + TileGlyphCaptionGap + TilePaddingBottom
                            + ImGui::GetTextLineHeight() * 2.0f;
        Geometry.Advance    = Geometry.TileHeight + Metrics.TileGap;
        return Geometry;
    }

    // 📝 How many of a cluster's actions are drawn: everything the gate does not report Omitted. Shared by the measure and the draw so their row
    //    counts never disagree.
    int TallyDrawnActions(const ClusterDescriptor& Cluster, const VerdictBinding& Gate)
    {
        int Drawn = 0;
        for (int ActionIndex = 0; ActionIndex < Cluster.ActionCount; ++ActionIndex)
        {
            if (ResolveActionVerdict(Gate, Cluster.Actions[ActionIndex]).Standing != ActionStanding::Omitted)
            {
                ++Drawn;
            }
        }
        return Drawn;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float MeasureActionStripHeight(const ClusterDescriptor&    Cluster,
                               const VerdictBinding&       Gate,
                               float                       ColumnWidth,
                               const MetricsSpecification& Metrics)
{
    const int Drawn = TallyDrawnActions(Cluster, Gate);
    if (Drawn == 0)
    {
        return GridBodyPadding * 2.0f;
    }

    const CellGeometry Geometry = ResolveCellGeometry(ColumnWidth, Metrics);
    const int          RowCount = (Drawn + Geometry.ColumnCount - 1) / Geometry.ColumnCount;

    // The trailing gap belongs to the pitch between rows, not after the last one, so the padding closes the body cleanly.
    return GridBodyPadding * 2.0f + static_cast<float>(RowCount) * Geometry.TileHeight
         + static_cast<float>(RowCount - 1) * Metrics.TileGap;
}


ConsoleActionOutcome InscribeActionStrip(const SvgIconRegistry*      Icons,
                                         const ClusterDescriptor&    Cluster,
                                         const VerdictBinding&       Gate,
                                         const SurfaceBinding&       Surfaces,
                                         ImVec2                      Origin,
                                         float                       ColumnWidth,
                                         float                       PopScale,
                                         const PaletteSpecification& Palette,
                                         const MetricsSpecification& Metrics)
{
    ConsoleActionOutcome Outcome = {};
    Outcome.HoveredAction        = -1;
    Outcome.ActivatedAction      = -1;

    if (Cluster.Actions == nullptr || Cluster.ActionCount <= 0)
    {
        return Outcome;
    }

    ImDrawList* const  Canvas   = ImGui::GetWindowDrawList();
    const CellGeometry Geometry = ResolveCellGeometry(ColumnWidth, Metrics);

    float CursorY = Origin.y + GridBodyPadding;
    int   Column  = 0;

    for (int ActionIndex = 0; ActionIndex < Cluster.ActionCount; ++ActionIndex)
    {
        const ActionDescriptor& Action = Cluster.Actions[ActionIndex];

        // 🔴 The three-state model, now read from the gate. An action the context does not apply to is OMITTED — not greyed — because a greyed tile
        //    for an action that can never run here fills the grid with noise; an action that applies but cannot run yet is GATED, kept present with
        //    its shortfall, because that shortfall is the one thing the reader actually needs.
        const ActionVerdict Verdict = ResolveActionVerdict(Gate, Action);
        if (Verdict.Standing == ActionStanding::Omitted)
        {
            continue;
        }
        const bool Gated = (Verdict.Standing == ActionStanding::Gated);

        const ImVec2 TileMin(Origin.x + GridBodyPadding
                             + static_cast<float>(Column) * (Geometry.TileWidth + Metrics.TileGap), CursorY);
        const ImVec2 TileMax(TileMin.x + Geometry.TileWidth, TileMin.y + Geometry.TileHeight);

        ImGui::SetCursorScreenPos(TileMin);
        ImGui::PushID(ActionIndex);
        const bool Pressed = ImGui::InvisibleButton("##action", ImVec2(Geometry.TileWidth, Geometry.TileHeight));
        const bool Hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        if (Hovered)
        {
            Outcome.HoveredAction = ActionIndex;
        }
        // A gated tile reports its hover so the shortfall shows, but never its press — the gate is enforced here rather than left to a caller that
        // would have to re-derive the same verdict to know it should ignore the click.
        if (Pressed && !Gated)
        {
            Outcome.ActivatedAction = ActionIndex;
        }

        const ImU32 Ground = Gated ? Palette.GatedTileFill : (Hovered ? Palette.TileHoverFill : Palette.TileFill);
        Canvas->AddRectFilled(TileMin, TileMax, Ground, Metrics.TileRounding);
        if (Hovered && !Gated)
        {
            Canvas->AddRect(TileMin, TileMax, Palette.Hairline, Metrics.TileRounding, 0, 1.0f);
        }

        // The artwork area, offered to the workspace before the console fills it. A painter that declines (or is absent) leaves the glyph path
        // untouched — see SurfacePainter.h on why the fallback is per TILE rather than per workspace.
        const float  ArtEdge   = Metrics.TileArtEdge;
        const float  GlyphTop  = TileMin.y + TilePaddingTop;
        const ImVec2 ArtOrigin((TileMin.x + TileMax.x) * 0.5f - ArtEdge * 0.5f, GlyphTop);

        bool ArtPainted = false;
        if (Surfaces.PaintTile != nullptr)
        {
            SurfaceRegion ArtRegion = {};
            ArtRegion.Minimum  = ArtOrigin;
            ArtRegion.Maximum  = ImVec2(ArtOrigin.x + ArtEdge, ArtOrigin.y + ArtEdge);
            ArtRegion.PopScale = PopScale;
            ArtPainted = Surfaces.PaintTile(Action, ArtRegion, Gated, Surfaces.Context);
        }

        if (!ArtPainted)
        {
            InscribeConsoleGlyph(Icons, Action.GlyphName, ArtOrigin, ArtEdge, Gated);
        }

        // The shortfall REPLACES the caption under the pointer rather than sitting beside it: at this tile size there is room for one caption, and
        // the reason an action cannot run outranks its name once the reader has asked by pointing at it.
        const float CaptionTop  = GlyphTop + ArtEdge + TileGlyphCaptionGap;
        const float CaptionRoom = Geometry.TileWidth - CaptionSideInset * 2.0f;
        const float CaptionMidX = (TileMin.x + TileMax.x) * 0.5f;
        if (Gated && Hovered && Verdict.Shortfall != nullptr)
        {
            InscribeTileCaption(Canvas, Verdict.Shortfall, CaptionMidX, CaptionTop, CaptionRoom, ShortfallInk);
        }
        else
        {
            InscribeTileCaption(Canvas, Action.Label, CaptionMidX, CaptionTop, CaptionRoom,
                                Gated ? BlendTowardTransparent(Palette.Muted, 0.7f) : Palette.Muted);
        }

        // The accelerator is suppressed on a gated tile: printing a shortcut for an action the same tile says cannot run invites the reader to
        // press it.
        if (Action.Keystroke != nullptr && !Gated)
        {
            const ImVec2 KeySize = ImGui::CalcTextSize(Action.Keystroke);
            Canvas->AddText(ImVec2(TileMax.x - KeystrokeInsetX - KeySize.x, TileMin.y + KeystrokeInsetY),
                            Palette.Faint, Action.Keystroke);
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
