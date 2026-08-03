/*==============================================================================================================================================
                                                        PANEHEADERRASTERIZATION.CPP
==============================================================================================================================================*/
// 🧩 Draws the one header band every slide shares. Reconciled from ToolCardShell's inline header helpers (ground+hairline, count pill, title/subtitle
//    block, back chevron) and PaintCardShell's fuller header (the back row and the nib-well icon variant). The band's height is ALWAYS Metrics.
//    HeaderHeight, never a parameter — that is what keeps the seam from stepping across the carousel. The caller assembles the ConsolePaneHeader from
//    whatever its pane reads; this function only paints it and reports a back-row click.

#include "PaneHeaderRasterization.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float PanePadding       = 13.0f;   // [px] - .pane-head padding
    constexpr float HeaderGap         = 10.0f;   // [px] - .pane-head gap
    constexpr float HeaderTileEdge    = 24.0f;   // [px] - .pane-head .h-ic
    constexpr float HeaderTileRound   =  6.0f;   // [px] - .h-ic radius
    constexpr float HeaderGlyphInset  = 17.0f;   // [px] - .h-ic svg, inset from the badge's full bleed
    constexpr float HeaderSubtitleGap =  3.0f;   // [px] - .h-txt .fn margin-top
    constexpr float PillPaddingX      =  9.0f;   // [px] - .h-n padding
    constexpr float PillPaddingY      =  4.0f;   // [px]
    constexpr float BackArrowEdge     = 16.0f;   // [px] - .pane-head .b-arrow

    constexpr ImU32 HeaderBackHover = IM_COL32(0x29, 0x29, 0x30, 0xFF);   // .pane-head.back:hover
    constexpr ImU32 WellFill        = IM_COL32(0x1B, 0x1B, 0x1F, 0xFF);   // paint's nib-well ground
    constexpr ImU32 WellRing        = IM_COL32(0xFF, 0xFF, 0xFF, 0x1A);   // the ring around it

    // 📝 The rounded count pill that closes a header, right-aligned in the strip it is given. Returns its left edge so the title beside it can be
    //    clipped to the room left over.
    float InscribeHeaderPill(ImDrawList* Canvas, const char* Text, float RightEdge, float MidY, ImU32 Ground, ImU32 TextInk)
    {
        const ImVec2 TextSize = ImGui::CalcTextSize(Text);
        const ImVec2 PillMax(RightEdge, MidY + TextSize.y * 0.5f + PillPaddingY);
        const ImVec2 PillMin(PillMax.x - TextSize.x - PillPaddingX * 2.0f, MidY - TextSize.y * 0.5f - PillPaddingY);
        Canvas->AddRectFilled(PillMin, PillMax, Ground, (PillMax.y - PillMin.y) * 0.5f);
        Canvas->AddText(ImVec2(PillMin.x + PillPaddingX, PillMin.y + PillPaddingY), TextInk, Text);
        return PillMin.x;
    }

    // 📝 Title over subtitle, clipped to the room the pill leaves. A null subtitle centres the title on the band as a single line.
    void InscribeHeaderText(ImDrawList* Canvas, const char* Title, const char* Subtitle,
                            float LeftEdge, float RightLimit, float MidY, const PaletteSpecification& Palette)
    {
        const float LineHeight = ImGui::GetTextLineHeight();

        Canvas->PushClipRect(ImVec2(LeftEdge, MidY - LineHeight * 2.0f), ImVec2(RightLimit, MidY + LineHeight * 2.0f), true);
        if (Subtitle != nullptr)
        {
            const float BlockHigh = LineHeight * 2.0f + HeaderSubtitleGap;
            Canvas->AddText(ImVec2(LeftEdge, MidY - BlockHigh * 0.5f), Palette.Ink, (Title != nullptr) ? Title : "");
            Canvas->AddText(ImVec2(LeftEdge, MidY - BlockHigh * 0.5f + LineHeight + HeaderSubtitleGap), Palette.Faint, Subtitle);
        }
        else
        {
            Canvas->AddText(ImVec2(LeftEdge, MidY - LineHeight * 0.5f), Palette.Ink, (Title != nullptr) ? Title : "");
        }
        Canvas->PopClipRect();
    }

    // 📝 The back chevron the options slide's left header carries, drawn as two strokes rather than registered as a glyph — it is shell chrome, not
    //    catalogue artwork, and registering it would put a shell concern in a workspace's icon pack.
    void InscribeBackChevron(ImDrawList* Canvas, ImVec2 Centre, float Edge, ImU32 Tint)
    {
        const float Reach = Edge * 0.28f;
        Canvas->AddLine(ImVec2(Centre.x + Reach * 0.6f, Centre.y - Reach), ImVec2(Centre.x - Reach * 0.5f, Centre.y), Tint, 1.6f);
        Canvas->AddLine(ImVec2(Centre.x - Reach * 0.5f, Centre.y), ImVec2(Centre.x + Reach * 0.6f, Centre.y + Reach), Tint, 1.6f);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InscribeConsolePaneHeader(const PaletteSpecification&  Palette,
                               const MetricsSpecification& Metrics,
                               const ConsolePaneHeader&    Header,
                               ImVec2                      PaneMinimum,
                               float                       PaneWidth)
{
    ImDrawList* const Canvas   = ImGui::GetWindowDrawList();
    const float       Height   = Metrics.HeaderHeight;
    const float       MidY     = PaneMinimum.y + Height * 0.5f;
    const float       RightPad = PaneMinimum.x + PaneWidth - PanePadding;
    const ImVec2      HeaderMax(PaneMinimum.x + PaneWidth, PaneMinimum.y + Height);

    // 📝 A back header is the one clickable band on the console, so its hit-test is taken FIRST — the hover it reads picks the ground tone the band
    //    then draws in, and its press is what this function reports.
    bool BackPressed = false;
    bool BackHovered = false;
    if (Header.IsBackRow)
    {
        ImGui::SetCursorScreenPos(PaneMinimum);
        ImGui::PushID("##console-back");
        BackPressed = ImGui::InvisibleButton("##back", ImVec2(PaneWidth, Height));
        BackHovered = ImGui::IsItemHovered();
        ImGui::PopID();
    }

    // ---- ground + bottom hairline (shared height keeps the two panes on one continuous band) ----
    const ImU32 Ground = (Header.IsBackRow && BackHovered) ? HeaderBackHover : Palette.RailSelectedFill;
    Canvas->AddRectFilled(PaneMinimum, HeaderMax, Ground);
    Canvas->AddLine(ImVec2(PaneMinimum.x, HeaderMax.y), ImVec2(HeaderMax.x, HeaderMax.y), Palette.Hairline, 1.0f);

    // ---- leading mark: a back chevron, OR an icon tile (empty black by default, pale well for paint's nib crop) ----
    float TextLeft = PaneMinimum.x + PanePadding;
    if (Header.IsBackRow)
    {
        InscribeBackChevron(Canvas, ImVec2(PaneMinimum.x + PanePadding + BackArrowEdge * 0.5f, MidY),
                            BackArrowEdge, BackHovered ? Palette.Ink : Palette.Muted);
        TextLeft = PaneMinimum.x + PanePadding + BackArrowEdge + HeaderGap;
    }
    else
    {
        const ImVec2 TileMin(PaneMinimum.x + PanePadding, MidY - HeaderTileEdge * 0.5f);
        const ImVec2 TileMax(TileMin.x + HeaderTileEdge, TileMin.y + HeaderTileEdge);
        if (Header.IconIsWell)
        {
            Canvas->AddRectFilled(TileMin, TileMax, WellFill, HeaderTileRound);
            Canvas->AddRect(TileMin, TileMax, WellRing, HeaderTileRound, 0, 1.0f);
        }
        else
        {
            Canvas->AddRectFilled(TileMin, TileMax, IM_COL32(0, 0, 0, 255), HeaderTileRound);
        }
        if (Header.IconTexture != 0)
        {
            // A well icon overflows its tile (the nib crop bleeds to the tile edge); a plain glyph is inset within the black tile.
            const float Inset = Header.IconIsWell ? 0.0f : (HeaderTileEdge - HeaderGlyphInset) * 0.5f;
            Canvas->AddImage(Header.IconTexture, ImVec2(TileMin.x + Inset, TileMin.y + Inset),
                             ImVec2(TileMax.x - Inset, TileMax.y - Inset));
        }
        TextLeft = TileMax.x + HeaderGap;
    }

    // ---- tally pill (optional), then the title/subtitle block clipped to whatever the pill leaves ----
    float TitleRight = RightPad;
    if (Header.TallyText != nullptr)
    {
        const ImU32 PillInk = Header.TallyAccent ? Palette.Accent : Palette.Muted;
        TitleRight = InscribeHeaderPill(Canvas, Header.TallyText, RightPad, MidY, Palette.PaneFill, PillInk) - 4.0f;
    }
    InscribeHeaderText(Canvas, Header.Title, Header.Subtitle, TextLeft, TitleRight, MidY, Palette);

    return BackPressed;
}

} // namespace Frontier
