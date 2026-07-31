/*==============================================================================================================================================
                                                        MODELLINGTOOLMENU.CPP
==============================================================================================================================================*/
// 🧩 The two-pane modelling-tool card: a band rail beside a grid of tool tiles, each pane a fixed-height header over its own independently
//    scrolling body. Drawn through ImDrawList over invisible hit-test buttons for the same reason TopologyActionMenu is — a tile carries a vector
//    glyph, a wrapped caption, a corner keystroke, and a shortfall that REPLACES the caption under the pointer, none of which a Selectable can
//    express without fighting its own padding.
//    Every metric is Documentation/Prototypes/ModellingToolMenu.html's CSS pixel divided by the ×1.35 that prototype uses to read on a desktop
//    page, so the two stay directly comparable: header 53→39, rail row 32→24, tile glyph 27→20, card cap 396→293.
//    ⚠️ The tones are this prototype's `:root` block, a THIRD token set distinct from both the shared palette and TopologyActionMenu's — see the
//    note on the constant block.

#include <cmath>
#include <cstdio>

#include "ModellingToolMenu.h"
#include "PolygonMutation/PolygonGlyphTable.h"
#include "PolygonMutation/StratumBadgeTable.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Card geometry, unscaled — every use multiplies by Metrics.UiScale so one config knob resizes the whole card. Prototype CSS
    //    pixels ÷1.35: header 53→39, rail row 32→24, rail width 196→145, grid width 326→242, tile glyph 27→20.
    constexpr float HeaderHeight       = 39.0f;    // [px] - Both pane headers, pinned identical (.pane-head height 53)
    constexpr float FooterHeight       = 20.0f;    // [px] - The pinned tally strip under the grid (.grid-foot height 27)
    constexpr float RailWidth          = 145.0f;   // [px] - Rail column (.rail-body min-width 196)
    constexpr float GridWidth          = 242.0f;   // [px] - Grid column (.grid-pane min-width 326)
    constexpr float RailRowHeight      = 24.0f;    // [px] - One rail row (.rail-item height 32)
    constexpr float RailBodyPadding    = 5.0f;     // [px] - Rail body inset (.rail-body padding 7)
    constexpr float RailRowRounding    = 6.5f;     // [px] - Rail row fill radius (.rail-item radius 9)
    constexpr float RailGlyphSide      = 13.0f;    // [px] - Rail row glyph (.r-ic 18)
    constexpr float RailGlyphGutter    = 20.0f;    // [px] - Row left edge to the caption: 6 padding + 13 glyph (.rail-item gap 9)
    constexpr float RailMarkerWidth    = 2.0f;     // [px] - Active-row accent bar (.rail-item.active::before width 3)
    constexpr float RailMarkerHeight   = 11.0f;    // [px] - That bar's length (height 15)

    constexpr float GridBodyPadding    = 7.0f;     // [px] - Grid body inset (.grid-body padding 9)
    constexpr float TileGap            = 4.5f;     // [px] - Gap between tiles (.grid gap 6)
    constexpr float TileRounding       = 6.5f;     // [px] - Tile corner radius (.tile radius 9)
    constexpr float TileGlyphSide      = 20.0f;    // [px] - Glyph inside a tile (.tile svg 27)
    constexpr float TilePaddingTop     = 7.5f;     // [px] - Tile top inset before the glyph (.tile padding-top 10)
    constexpr float TileGlyphCaptionGap = 4.5f;    // [px] - Glyph to caption (.tile gap 6)
    constexpr float TilePaddingBottom  = 6.0f;     // [px] - Caption baseline to tile bottom (.tile padding-bottom 8)
    constexpr int   TileColumnCount    = 4;        // [idx]- Tiles across (.grid 4 columns)

    constexpr float CardRounding       = 11.0f;    // [px] - Card corner radius (--menu-radius 15)
    constexpr float PanePadding        = 9.5f;     // [px] - Header / footer horizontal inset (.pane-head padding 13)
    constexpr float HeaderGlyphSide    = 18.0f;    // [px] - The black tile in a pane header (.h-ic 24)
    constexpr float HeaderGlyphInset   = 12.5f;    // [px] - The tool glyph drawn inside that tile (.grid-pane .h-ic svg 17)
    constexpr float GlyphStrokeWidth   = 1.2f;     // [px] - Glyph line weight, deliberately scale-independent
    constexpr float PillPaddingX       = 6.5f;     // [px] - Header pill horizontal padding (.h-n padding 4px 9px)
    constexpr float PillPaddingY       = 3.0f;     // [px] - Header pill vertical padding
    constexpr float SeparatorHeight    = 7.0f;     // [px] - Vertical space the rail's dividing rule occupies (.rail-sep)
    constexpr float CardHeightDefault  = 293.0f;   // [px] - Card cap when the descriptor leaves it 0 (max-height 396)

    // 📝 Scroll feel, matched to TopologyActionMenu so the two cards behave identically under the wheel. WheelStep is given in ROWS
    //    rather than pixels so one notch always advances the same number of rows whatever the row height or UI scale.
    constexpr float RailWheelStep      = RailRowHeight * 2.5f;   // [px] - Rail travel per wheel notch
    constexpr float GridWheelStep      = 46.0f;                  // [px] - Grid travel per notch, about one tile row
    constexpr float ScrollEaseRate     = 18.0f;                  // [1/s]- Ease-out rate; higher is snappier

    // 📝 ⚠️ A THIRD token set. The live palette derives from the UVeditor prototype (near-white accent), TopologyActionMenu carries
    //    the ControlsPreview tokens (#131315 body, #4a90e2 blue), and this card is a port of ModellingToolMenu.html, whose `:root`
    //    is neither: a #17171a rail against a #101012 grid with a #5b8cff accent. Reading the shared palette gave a card with no
    //    visible seam between its panes; overwriting it would restyle every other panel to match this one menu. So the mockup's
    //    tokens live here, one constant per token, until the engine adopts a single set wholesale.
    constexpr ImU32 RailGround         = IM_COL32( 23,  23,  26, 255);   // --menu       #17171a
    constexpr ImU32 GridGround         = IM_COL32( 16,  16,  18, 255);   // --menu-2     #101012
    constexpr ImU32 RailSelectedGround = IM_COL32( 32,  32,  37, 255);   // --rail-sel   #202025
    constexpr ImU32 TileGround         = IM_COL32( 26,  26,  30, 255);   // --tile       #1a1a1e
    constexpr ImU32 TileHoverGround    = IM_COL32( 37,  37,  43, 255);   // .tile:hover  #25252b
    constexpr ImU32 GatedTileGround    = IM_COL32( 22,  22,  26, 255);   // .tile.gated  #16161a
    constexpr ImU32 HairLine           = IM_COL32(255, 255, 255,  18);   // --hair       rgba(255,255,255,.07)
    constexpr ImU32 AccentTone         = IM_COL32( 91, 140, 255, 255);   // --accent     #5b8cff
    constexpr ImU32 TextPrimaryTone    = IM_COL32(232, 232, 240, 255);   // --ink        #e8e8f0
    constexpr ImU32 TextMutedTone      = IM_COL32(138, 138, 153, 255);   // --muted      #8a8a99
    constexpr ImU32 TextFaintTone      = IM_COL32( 90,  90, 102, 255);   // --faint      #5a5a66
    constexpr ImU32 ShortfallTone      = IM_COL32(201, 162,  39, 255);   // .t-why       #c9a227
    constexpr float GatedGlyphAlpha    = 0.50f;                          // .tile.gated svg opacity
    // ⚠️ The mockup's `.rail-item.empty` opacity (0.34) has no counterpart here on purpose: a band with no applicable tile is
    //    omitted from the rail rather than dimmed, so there is no empty row left to tint. See the omission rule in the rail loop.

    // 📝 A greyed tile keeps its glyph legible but clearly inert, by thinning the tone it would otherwise draw in rather than by
    //    naming a second "disabled" colour for every one above — which is what the mockup's `opacity` rules do.
    ImU32 BlendTowardTransparent(ImU32 Colour, float Alpha)
    {
        ImVec4 Channels = ImGui::ColorConvertU32ToFloat4(Colour);
        Channels.w *= Alpha;
        return ImGui::ColorConvertFloat4ToU32(Channels);
    }

    // 📝 The rounded count pill that closes both pane headers, right-aligned within the strip it is given.
    void InscribeHeaderPill(ImDrawList* DrawList, const char* Text, float RightEdge, float MidY, ImU32 TextTint, float Scale)
    {
        const ImVec2 TextSize = ImGui::CalcTextSize(Text);
        const ImVec2 PillMax(RightEdge, MidY + TextSize.y * 0.5f + PillPaddingY * Scale);
        const ImVec2 PillMin(PillMax.x - TextSize.x - PillPaddingX * 2.0f * Scale,
                             MidY - TextSize.y * 0.5f - PillPaddingY * Scale);
        DrawList->AddRectFilled(PillMin, PillMax, GridGround, (PillMax.y - PillMin.y) * 0.5f);
        DrawList->AddText(ImVec2(PillMin.x + PillPaddingX * Scale, PillMin.y + PillPaddingY * Scale), TextTint, Text);
    }

    // 📝 Title over subtitle, the shape both pane headers share. Clipped to the room the pill leaves so a long caption can never
    //    overrun the count beside it.
    void InscribeHeaderText(ImDrawList* DrawList, const char* Title, const char* Subtitle, float LeftEdge, float RightLimit, float MidY, float Scale)
    {
        DrawList->PushClipRect(ImVec2(LeftEdge, MidY - HeaderHeight * Scale), ImVec2(RightLimit, MidY + HeaderHeight * Scale), true);
        const float LineHeight = ImGui::GetTextLineHeight();
        DrawList->AddText(ImVec2(LeftEdge, MidY - LineHeight - 1.0f * Scale), TextPrimaryTone, Title);
        DrawList->AddText(ImVec2(LeftEdge, MidY + 1.0f * Scale), TextFaintTone, Subtitle);
        DrawList->PopClipRect();
    }

    // 📝 One pane header's ground plus its bottom hairline. Both panes call this with the SAME height, so the two headers land on
    //    one continuous band across the card — a taller header beside a shorter one reads as two stacked cards rather than one
    //    menu with two panes.
    void InscribeHeaderGround(ImDrawList* DrawList, ImVec2 Minimum, ImVec2 Maximum, float Rounding, ImDrawFlags Corners, const ThemeConfiguration& Theme)
    {
        DrawList->AddRectFilled(Minimum, Maximum, RailSelectedGround, Rounding, Corners);
        DrawList->AddLine(ImVec2(Minimum.x, Maximum.y), ImVec2(Maximum.x, Maximum.y), HairLine, Theme.Metrics.BorderThickness);
    }

    // 🔴 Eased scrolling, shared by both pane bodies. ImGui applies the wheel to ScrollY immediately, which on a long jump reads as
    //    a teleport with no sense of travel. The wheel is intercepted into a TARGET held in the child's own storage and the live
    //    ScrollY is stepped a fraction of the remaining distance each frame — exponential ease-out, fastest at the start, settling
    //    without overshoot. Frame-rate corrected via DeltaTime, or the card would scroll at a different speed on a 144 Hz display
    //    than on a 60 Hz one.
    void ApplyEasedScroll(float WheelTravel)
    {
        ImGuiStorage* const BodyState = ImGui::GetStateStorage();
        const ImGuiID       TargetKey = ImGui::GetID("##scroll-target");
        const float         ScrollMax = ImGui::GetScrollMaxY();

        float ScrollTarget = BodyState->GetFloat(TargetKey, ImGui::GetScrollY());

        // Read at the START of the region, before ImGui has applied its own scroll for the frame, so the target advances from where
        // the ease is actually heading rather than from a position ImGui already jumped.
        if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
        {
            ScrollTarget -= ImGui::GetIO().MouseWheel * WheelTravel;
        }
        if (ScrollTarget < 0.0f)                          { ScrollTarget = 0.0f; }
        if (ScrollMax > 0.0f && ScrollTarget > ScrollMax)  { ScrollTarget = ScrollMax; }

        const float Remaining = ScrollTarget - ImGui::GetScrollY();
        if (Remaining < -0.5f || Remaining > 0.5f)
        {
            // 1 - exp(-k·dt) is the frame-rate-independent form of a per-frame lerp; a bare `Remaining * 0.25f` would ease four
            // times faster at 240 fps than at 60.
            const float Blend = 1.0f - std::exp(-ScrollEaseRate * ImGui::GetIO().DeltaTime);
            ImGui::SetScrollY(ImGui::GetScrollY() + Remaining * Blend);
        }
        else
        {
            ImGui::SetScrollY(ScrollTarget);      // Snap the last half pixel, or the row grid sits fractionally off forever.
        }

        BodyState->SetFloat(TargetKey, ScrollTarget);
    }

    // 📝 Whether the pointer is inside a scrolling pane that MOVED this frame. Hover-to-swap has to be muted while the rail scrolls:
    //    the pointer stays put but the rows travel under it, so every row that passes would fire and the grid would thrash through
    //    five bands on one wheel throw. The prototype does this with a 130 ms timer; here the live scroll delta is available
    //    directly, which is both simpler and exact.
    bool ResolveScrollSuppression(float PreviousScroll)
    {
        const float Delta = ImGui::GetScrollY() - PreviousScroll;
        return Delta < -0.5f || Delta > 0.5f;
    }

    // 📝 A tile caption, centred and wrapped to at most two lines. Wrapping is done by hand rather than through AddText's wrap
    //    width because each line has to be CENTRED independently, which the wrapped overload cannot do.
    void InscribeTileCaption(ImDrawList* DrawList, const char* Text, float CentreX, float TopY, float Available, ImU32 Tint)
    {
        const ImVec2 WholeSize = ImGui::CalcTextSize(Text);
        if (WholeSize.x <= Available)
        {
            DrawList->AddText(ImVec2(CentreX - WholeSize.x * 0.5f, TopY), Tint, Text);
            return;
        }

        // Break at the last space that still fits. A caption with no space ("Un-Subdivide") simply overruns its tile and is clipped
        // by the grid body, which is what the prototype's `text-overflow` does.
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
            DrawList->AddText(ImVec2(CentreX - WholeSize.x * 0.5f, TopY), Tint, Text);
            return;
        }

        const ImVec2 FirstSize  = ImGui::CalcTextSize(Text, Break);
        const ImVec2 SecondSize = ImGui::CalcTextSize(Break + 1);
        DrawList->AddText(ImVec2(CentreX - FirstSize.x * 0.5f, TopY), Tint, Text, Break);
        DrawList->AddText(ImVec2(CentreX - SecondSize.x * 0.5f, TopY + ImGui::GetTextLineHeight()), Tint, Break + 1);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ResolveToolApplicability(const ModellingToolDescriptor& Tool, TopologyStratum Stratum)
{
    return StratumAdmitted(Tool.StrataMask, Stratum);
}


int ResolveFirstPopulatedBand(const ModellingBandDescriptor* Bands, int BandCount, TopologyStratum Stratum)
{
    if (Bands == nullptr)
    {
        return -1;
    }

    for (int Index = 0; Index < BandCount; ++Index)
    {
        const ModellingBandDescriptor& Band = Bands[Index];
        if (Band.Tools == nullptr)
        {
            continue;
        }
        for (int ToolIndex = 0; ToolIndex < Band.ToolCount; ++ToolIndex)
        {
            if (ResolveToolApplicability(Band.Tools[ToolIndex], Stratum))
            {
                return Index;
            }
        }
    }
    return -1;
}


ModellingToolMenuResult ConstructModellingToolMenu(const ThemeConfiguration&          Theme,
                                                   const ModellingToolMenuDescriptor& Descriptor,
                                                   int&                               OpenBand)
{
    ModellingToolMenuResult Result = {};
    Result.ActivatedBand   = -1;
    Result.ActivatedTool    = -1;
    Result.OpenBand         = OpenBand;
    Result.DismissRequested = false;

    if (Descriptor.Bands == nullptr || Descriptor.BandCount <= 0)
    {
        return Result;
    }

    const float Scale      = Theme.Metrics.UiScale;
    const float CardWidth  = (RailWidth + GridWidth) * Scale;
    const float CardHeight = (Descriptor.HeightLimit > 0.0f ? Descriptor.HeightLimit : CardHeightDefault * Scale);

    // The open band is validated against THIS stratum before anything draws: a band that was showing when the selection changed can
    // have lost every tile, and opening onto an empty grid reads as a broken menu rather than as an empty band.
    {
        bool OpenBandPopulated = false;
        if (OpenBand >= 0 && OpenBand < Descriptor.BandCount && Descriptor.Bands[OpenBand].Tools != nullptr)
        {
            for (int Index = 0; Index < Descriptor.Bands[OpenBand].ToolCount; ++Index)
            {
                if (ResolveToolApplicability(Descriptor.Bands[OpenBand].Tools[Index], Descriptor.ActiveStratum))
                {
                    OpenBandPopulated = true;
                    break;
                }
            }
        }
        if (!OpenBandPopulated)
        {
            OpenBand = ResolveFirstPopulatedBand(Descriptor.Bands, Descriptor.BandCount, Descriptor.ActiveStratum);
        }
    }

    ImGui::SetNextWindowPos(Descriptor.AnchorPosition);
    ImGui::SetNextWindowSize(ImVec2(CardWidth, CardHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CardRounding * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, Theme.Metrics.BorderThickness);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, RailGround);
    ImGui::PushStyleColor(ImGuiCol_Border, HairLine);

    const ImGuiWindowFlags CardFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin(Descriptor.Identifier, nullptr, CardFlags))
    {
        ImDrawList*  DrawList   = ImGui::GetWindowDrawList();
        const ImVec2 Origin     = ImGui::GetWindowPos();
        const float  RailRight  = Origin.x + RailWidth * Scale;
        const float  HeaderBase = Origin.y + HeaderHeight * Scale;

        // The grid ground is painted before either pane, so the seam between the two columns is a change of ground rather than a
        // drawn line — which is what makes the card read as one surface split in two.
        DrawList->AddRectFilled(ImVec2(RailRight, Origin.y), ImVec2(Origin.x + CardWidth, Origin.y + CardHeight),
                                GridGround, CardRounding * Scale, ImDrawFlags_RoundCornersRight);

        //-------------------------------------------------------- RAIL HEADER --------------------------------------------------------
        // [ stratum badge | stratum name over "N of M tools" | "N selected" pill ] — the selection profile, stated by the menu
        // itself, so a reader never has to ask why these bands and not others.
        InscribeHeaderGround(DrawList, Origin, ImVec2(RailRight, HeaderBase), CardRounding * Scale,
                             ImDrawFlags_RoundCornersTopLeft, Theme);

        const float  HeaderMidY = (Origin.y + HeaderBase) * 0.5f;
        const ImVec2 BadgeOrigin(Origin.x + PanePadding * Scale, HeaderMidY - HeaderGlyphSide * Scale * 0.5f);
        InscribeStratumBadge(DrawList, Descriptor.ActiveStratum, BadgeOrigin, HeaderGlyphSide * Scale);

        // Live tools are tallied across the whole table, not per band: the subtitle answers "how much of this menu applies to my
        // selection", which is a property of the catalogue rather than of whichever band happens to be open.
        int LiveToolTally  = 0;
        int TotalToolTally = 0;
        for (int Index = 0; Index < Descriptor.BandCount; ++Index)
        {
            const ModellingBandDescriptor& Band = Descriptor.Bands[Index];
            if (Band.Tools == nullptr)
            {
                continue;
            }
            TotalToolTally += Band.ToolCount;
            for (int ToolIndex = 0; ToolIndex < Band.ToolCount; ++ToolIndex)
            {
                const ModellingToolDescriptor& Tool = Band.Tools[ToolIndex];
                if (ResolveToolApplicability(Tool, Descriptor.ActiveStratum) && Tool.Shortfall == nullptr)
                {
                    ++LiveToolTally;
                }
            }
        }

        char SelectedText[32] = {};
        std::snprintf(SelectedText, sizeof(SelectedText), "%d selected", Descriptor.SelectedCount);
        const float RailPillLeft = RailRight - PanePadding * Scale - ImGui::CalcTextSize(SelectedText).x
                                 - PillPaddingX * 2.0f * Scale;
        InscribeHeaderPill(DrawList, SelectedText, RailRight - PanePadding * Scale, HeaderMidY, AccentTone, Scale);

        char RailSubtitle[48] = {};
        std::snprintf(RailSubtitle, sizeof(RailSubtitle), "%d of %d tools", LiveToolTally, TotalToolTally);
        InscribeHeaderText(DrawList, DescribeTopologyStratum(Descriptor.ActiveStratum), RailSubtitle,
                           BadgeOrigin.x + HeaderGlyphSide * Scale + PanePadding * 0.75f * Scale,
                           RailPillLeft - 4.0f * Scale, HeaderMidY, Scale);

        //-------------------------------------------------------- RAIL BODY ----------------------------------------------------------
        // A child region so a long band table scrolls inside the card instead of growing past the screen. NoScrollbar
        // unconditionally: the card stays a compact rectangle and the wheel still reaches every row, but no gutter is spent on a
        // bar — a visible bar reads as a scrolling PANEL, and the mockup shows none. The eased scroll is what tells a reader there
        // is more, in place of the bar.
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, HeaderBase));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        // NoScrollWithMouse hands the wheel to the ease: left on, ImGui writes ScrollY directly the same frame and the two fight,
        // which shows up as a jitter on every notch.
        ImGui::BeginChild("##rail", ImVec2(RailWidth * Scale, CardHeight - HeaderHeight * Scale), false,
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
        {
            ImDrawList* const RailDrawList  = ImGui::GetWindowDrawList();
            const float       ScrollOnEntry = ImGui::GetScrollY();
            ApplyEasedScroll(RailWheelStep * Scale);
            const bool RailScrolling = ResolveScrollSuppression(ScrollOnEntry);

            float CursorY = ImGui::GetCursorScreenPos().y + RailBodyPadding * Scale;

            // Tracks whether any row has been laid down yet, so a separator carried by the first SURVIVING band is suppressed. With
            // nothing selected the creation bands are the only rows left, and their divider would otherwise draw a rule against the
            // top edge of the rail with nothing above it to divide.
            bool RowInscribed = false;

            for (int Index = 0; Index < Descriptor.BandCount; ++Index)
            {
                const ModellingBandDescriptor& Band = Descriptor.Bands[Index];

                // How many of this band's tiles the current selection can actually run.
                const bool SingleShot = (Band.Tools == nullptr);
                int        BandLive    = 0;
                int        BandVisible = 0;
                if (SingleShot)
                {
                    BandVisible = StratumAdmitted(Band.StrataMask, Descriptor.ActiveStratum) ? 1 : 0;
                    BandLive    = BandVisible;
                }
                else
                {
                    for (int ToolIndex = 0; ToolIndex < Band.ToolCount; ++ToolIndex)
                    {
                        const ModellingToolDescriptor& Tool = Band.Tools[ToolIndex];
                        if (ResolveToolApplicability(Tool, Descriptor.ActiveStratum))
                        {
                            ++BandVisible;
                            if (Tool.Shortfall == nullptr)
                            {
                                ++BandLive;
                            }
                        }
                    }
                }

                // 🔴 A band with no applicable tile is OMITTED from the rail, not dimmed. This is the one place the hide-vs-grey rule
                //    inverts relative to the tiles, and the empty selection is why: with nothing selected, thirteen modelling bands
                //    have zero applicable tools, and dimming them all would bury the four creation bands under thirteen dead rows.
                //    A tile greys because its shortfall is actionable information; a whole family that cannot apply at this stratum
                //    carries none, so it earns no row. Computed BEFORE the separator so an omitted band takes its rule with it and
                //    the divider never strands itself above nothing.
                if (BandVisible == 0)
                {
                    continue;
                }

                // The rule dividing the creation bands from the modelling families, and those from the single-shot commands below.
                // A mis-click into Delete costs a deliberate crossing rather than a slip.
                if (Band.SeparatorAbove && RowInscribed)
                {
                    const float RuleY = CursorY + SeparatorHeight * Scale * 0.5f;
                    RailDrawList->AddLine(ImVec2(Origin.x + RailBodyPadding * 2.0f * Scale, RuleY),
                                          ImVec2(RailRight - RailBodyPadding * 2.0f * Scale, RuleY),
                                          HairLine, Theme.Metrics.BorderThickness);
                    CursorY += SeparatorHeight * Scale;
                }

                const bool   Active = (!SingleShot && Index == OpenBand);
                const ImVec2 RowMin(Origin.x + RailBodyPadding * Scale, CursorY);
                const ImVec2 RowMax(RailRight - RailBodyPadding * Scale, CursorY + RailRowHeight * Scale);

                ImGui::SetCursorScreenPos(RowMin);
                ImGui::PushID(Index);
                const bool Pressed  = ImGui::InvisibleButton("##band", ImVec2(RowMax.x - RowMin.x, RowMax.y - RowMin.y));
                const bool Hovered  = ImGui::IsItemHovered();
                ImGui::PopID();

                // ⚠️ Hover-to-swap is muted while the rail scrolls. The pointer stays put but the rows travel under it, so every
                //    row that passes would fire and the grid would thrash through five bands on one wheel throw. A click always
                //    swaps, scrolling or not — that one is deliberate.
                if (Hovered && !RailScrolling && !SingleShot)
                {
                    OpenBand = Index;
                }
                if (Pressed)
                {
                    if (SingleShot)
                    {
                        Result.ActivatedBand = Index;
                        Result.ActivatedTool  = -1;
                    }
                    else
                    {
                        OpenBand = Index;
                    }
                }

                if (Active || (Hovered && SingleShot))
                {
                    RailDrawList->AddRectFilled(RowMin, RowMax, RailSelectedGround, RailRowRounding * Scale);
                }
                if (Active)
                {
                    // The accent bar that marks the open band, flush to the rail's left edge rather than inside the row fill, so
                    // the eye reads it as an index marker on the column rather than as part of the row.
                    const float MarkerMidY = (RowMin.y + RowMax.y) * 0.5f;
                    RailDrawList->AddRectFilled(ImVec2(Origin.x, MarkerMidY - RailMarkerHeight * Scale * 0.5f),
                                                ImVec2(Origin.x + RailMarkerWidth * Scale, MarkerMidY + RailMarkerHeight * Scale * 0.5f),
                                                AccentTone, RailMarkerWidth * Scale);
                }

                const float RowMidY     = (RowMin.y + RowMax.y) * 0.5f;
                const ImU32 CaptionTint = Active ? TextPrimaryTone : TextMutedTone;
                const ImU32 GlyphTint   = TextPrimaryTone;

                InscribePolygonGlyph(RailDrawList, Band.Glyph,
                                     ImVec2(RowMin.x + 4.0f * Scale, RowMidY - RailGlyphSide * Scale * 0.5f),
                                     RailGlyphSide * Scale, GlyphTint, GlyphStrokeWidth);

                // The trailing figure is the band's LIVE count, not its total: a reader picking a family wants to know what will
                // work on this selection, and the total is already implied by the tiles they are about to see.
                char TrailingText[16] = {};
                if (SingleShot)
                {
                    std::snprintf(TrailingText, sizeof(TrailingText), "%s", Band.Keystroke != nullptr ? Band.Keystroke : "");
                }
                else
                {
                    std::snprintf(TrailingText, sizeof(TrailingText), "%d", BandLive);
                }

                float TrailingWidth = 0.0f;
                if (TrailingText[0] != '\0')
                {
                    const ImVec2 TrailingSize = ImGui::CalcTextSize(TrailingText);
                    TrailingWidth = TrailingSize.x;
                    RailDrawList->AddText(ImVec2(RowMax.x - 5.0f * Scale - TrailingSize.x, RowMidY - TrailingSize.y * 0.5f),
                                          TextFaintTone, TrailingText);
                }

                const float CaptionX = RowMin.x + RailGlyphGutter * Scale;
                RailDrawList->PushClipRect(ImVec2(CaptionX, RowMin.y),
                                           ImVec2(RowMax.x - TrailingWidth - 9.0f * Scale, RowMax.y), true);
                RailDrawList->AddText(ImVec2(CaptionX, RowMidY - ImGui::GetTextLineHeight() * 0.5f), CaptionTint, Band.Caption);
                RailDrawList->PopClipRect();

                CursorY      = RowMax.y + 1.0f * Scale;
                RowInscribed = true;
            }

            // Declare the run's full height so ImGui can report a scroll extent; without it the wheel has nothing to travel over.
            ImGui::SetCursorScreenPos(ImVec2(Origin.x, CursorY + RailBodyPadding * Scale));
            ImGui::Dummy(ImVec2(1.0f, 1.0f));
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        //-------------------------------------------------------- GRID HEADER --------------------------------------------------------
        const ModellingBandDescriptor* OpenDescriptor = nullptr;
        if (OpenBand >= 0 && OpenBand < Descriptor.BandCount && Descriptor.Bands[OpenBand].Tools != nullptr)
        {
            OpenDescriptor = &Descriptor.Bands[OpenBand];
        }

        InscribeHeaderGround(DrawList, ImVec2(RailRight, Origin.y), ImVec2(Origin.x + CardWidth, HeaderBase),
                             CardRounding * Scale, ImDrawFlags_RoundCornersTopRight, Theme);
        // The seam. Drawn after both header grounds so it crosses the band rather than being painted over by it.
        DrawList->AddLine(ImVec2(RailRight, Origin.y), ImVec2(RailRight, Origin.y + CardHeight),
                          HairLine, Theme.Metrics.BorderThickness);

        if (OpenDescriptor != nullptr)
        {
            // Same three-part shape as the rail header: band glyph on its own black tile, name over the live/gated split, pill.
            // The tile is supplied here because this glyph is monochrome tool art where the rail header's is a coloured badge that
            // carries its own backing — without it the two headers would sit at visibly different artwork weight.
            const ImVec2 TileMin(RailRight + PanePadding * Scale, HeaderMidY - HeaderGlyphSide * Scale * 0.5f);
            const ImVec2 TileMax(TileMin.x + HeaderGlyphSide * Scale, TileMin.y + HeaderGlyphSide * Scale);
            DrawList->AddRectFilled(TileMin, TileMax, IM_COL32(0, 0, 0, 255), 4.5f * Scale);
            InscribePolygonGlyph(DrawList, OpenDescriptor->Glyph,
                                 ImVec2((TileMin.x + TileMax.x) * 0.5f - HeaderGlyphInset * Scale * 0.5f,
                                        (TileMin.y + TileMax.y) * 0.5f - HeaderGlyphInset * Scale * 0.5f),
                                 HeaderGlyphInset * Scale, TextPrimaryTone, GlyphStrokeWidth);

            int ShownTally = 0;
            int LiveTally  = 0;
            for (int Index = 0; Index < OpenDescriptor->ToolCount; ++Index)
            {
                const ModellingToolDescriptor& Tool = OpenDescriptor->Tools[Index];
                if (ResolveToolApplicability(Tool, Descriptor.ActiveStratum))
                {
                    ++ShownTally;
                    if (Tool.Shortfall == nullptr)
                    {
                        ++LiveTally;
                    }
                }
            }

            char GridPillText[24] = {};
            std::snprintf(GridPillText, sizeof(GridPillText), "%d tools", ShownTally);
            const float GridPillLeft = Origin.x + CardWidth - PanePadding * Scale - ImGui::CalcTextSize(GridPillText).x
                                     - PillPaddingX * 2.0f * Scale;
            InscribeHeaderPill(DrawList, GridPillText, Origin.x + CardWidth - PanePadding * Scale, HeaderMidY,
                               TextMutedTone, Scale);

            char GridSubtitle[64] = {};
            if (ShownTally - LiveTally > 0)
            {
                std::snprintf(GridSubtitle, sizeof(GridSubtitle), "%d of %d available  ·  %d gated",
                              LiveTally, ShownTally, ShownTally - LiveTally);
            }
            else
            {
                std::snprintf(GridSubtitle, sizeof(GridSubtitle), "%d of %d available", LiveTally, ShownTally);
            }
            InscribeHeaderText(DrawList, OpenDescriptor->Caption, GridSubtitle,
                               TileMax.x + PanePadding * 0.75f * Scale, GridPillLeft - 4.0f * Scale, HeaderMidY, Scale);
        }

        //--------------------------------------------------------- GRID BODY ---------------------------------------------------------
        const float FooterTop  = Origin.y + CardHeight - FooterHeight * Scale;
        const float GridBodyTop = HeaderBase;

        ImGui::SetCursorScreenPos(ImVec2(RailRight, GridBodyTop));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        ImGui::BeginChild("##grid", ImVec2(GridWidth * Scale, FooterTop - GridBodyTop), false,
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
        {
            ImDrawList* const GridDrawList = ImGui::GetWindowDrawList();
            ApplyEasedScroll(GridWheelStep * Scale);

            // 🔴 A new band starts at the top. Carried-over scroll would open the grid part-way down a set the reader has not seen
            //    the start of — and because the bands differ in length, the same offset means a different thing in each.
            ImGuiStorage* const GridState = ImGui::GetStateStorage();
            const ImGuiID       BandKey   = ImGui::GetID("##shown-band");
            if (GridState->GetInt(BandKey, -1) != OpenBand)
            {
                GridState->SetInt(BandKey, OpenBand);
                ImGui::SetScrollY(0.0f);
                ImGui::GetStateStorage()->SetFloat(ImGui::GetID("##scroll-target"), 0.0f);
            }

            if (OpenDescriptor != nullptr)
            {
                const float TileSpan   = (GridWidth - GridBodyPadding * 2.0f) * Scale;
                const float TileWidth  = (TileSpan - TileGap * Scale * static_cast<float>(TileColumnCount - 1))
                                       / static_cast<float>(TileColumnCount);
                // Two caption lines are always budgeted, so a one-line tile and a two-line tile in the same row keep the same
                // height — a grid whose rows jog by a text line reads as broken alignment rather than as tighter labels.
                const float TileHeight = (TilePaddingTop + TileGlyphSide + TileGlyphCaptionGap + TilePaddingBottom) * Scale
                                       + ImGui::GetTextLineHeight() * 2.0f;

                float CursorY = GridBodyTop + GridBodyPadding * Scale - ImGui::GetScrollY();
                int   Column  = 0;

                for (int Index = 0; Index < OpenDescriptor->ToolCount; ++Index)
                {
                    const ModellingToolDescriptor& Tool = OpenDescriptor->Tools[Index];

                    // 🔴 The three-state model. A tool whose stratum does not apply is ABSENT — not greyed — because a greyed tile
                    //    for a tool this selection can never run would fill the grid with noise; a tool that applies but cannot run
                    //    is GATED, kept present with its shortfall, because that shortfall is the one thing the reader needs.
                    if (!ResolveToolApplicability(Tool, Descriptor.ActiveStratum))
                    {
                        continue;
                    }
                    const bool Gated = (Tool.Shortfall != nullptr);

                    const ImVec2 TileMin(RailRight + GridBodyPadding * Scale
                                         + static_cast<float>(Column) * (TileWidth + TileGap * Scale), CursorY);
                    const ImVec2 TileMax(TileMin.x + TileWidth, TileMin.y + TileHeight);

                    ImGui::SetCursorScreenPos(TileMin);
                    ImGui::PushID(Index);
                    const bool Pressed = ImGui::InvisibleButton("##tool", ImVec2(TileWidth, TileHeight)) && !Gated;
                    const bool Hovered = ImGui::IsItemHovered();
                    ImGui::PopID();

                    if (Pressed)
                    {
                        Result.ActivatedBand = OpenBand;
                        Result.ActivatedTool  = Index;
                    }

                    const ImU32 Ground = Gated ? GatedTileGround : (Hovered ? TileHoverGround : TileGround);
                    GridDrawList->AddRectFilled(TileMin, TileMax, Ground, TileRounding * Scale);
                    if (Hovered && !Gated)
                    {
                        GridDrawList->AddRect(TileMin, TileMax, HairLine, TileRounding * Scale, 0, Theme.Metrics.BorderThickness);
                    }

                    const float GlyphTop = TileMin.y + TilePaddingTop * Scale;
                    InscribePolygonGlyph(GridDrawList, Tool.Glyph,
                                         ImVec2((TileMin.x + TileMax.x) * 0.5f - TileGlyphSide * Scale * 0.5f, GlyphTop),
                                         TileGlyphSide * Scale,
                                         Gated ? BlendTowardTransparent(TextFaintTone, GatedGlyphAlpha) : TextPrimaryTone,
                                         GlyphStrokeWidth);

                    // The shortfall REPLACES the caption under the pointer rather than sitting beside it: at this tile size there is
                    // room for one line of text, and the reason a tool cannot run outranks its name once the reader has asked.
                    const float CaptionTop = GlyphTop + TileGlyphSide * Scale + TileGlyphCaptionGap * Scale;
                    if (Gated && Hovered)
                    {
                        InscribeTileCaption(GridDrawList, Tool.Shortfall, (TileMin.x + TileMax.x) * 0.5f, CaptionTop,
                                            TileWidth - 4.0f * Scale, ShortfallTone);
                    }
                    else
                    {
                        InscribeTileCaption(GridDrawList, Tool.Label, (TileMin.x + TileMax.x) * 0.5f, CaptionTop,
                                            TileWidth - 4.0f * Scale,
                                            Gated ? BlendTowardTransparent(TextMutedTone, 0.7f) : TextMutedTone);
                    }

                    if (Tool.Keystroke != nullptr && !Gated)
                    {
                        const ImVec2 KeySize = ImGui::CalcTextSize(Tool.Keystroke);
                        GridDrawList->AddText(ImVec2(TileMax.x - 4.5f * Scale - KeySize.x, TileMin.y + 3.5f * Scale),
                                              TextFaintTone, Tool.Keystroke);
                    }

                    ++Column;
                    if (Column == TileColumnCount)
                    {
                        Column   = 0;
                        CursorY += TileHeight + TileGap * Scale;
                    }
                }

                // Declare the grid's full height for the scroll extent, counting the row in progress.
                const float ConsumedHeight = (CursorY + (Column > 0 ? TileHeight + TileGap * Scale : 0.0f))
                                           - (GridBodyTop - ImGui::GetScrollY()) + GridBodyPadding * Scale;
                ImGui::SetCursorScreenPos(ImVec2(RailRight, GridBodyTop));
                ImGui::Dummy(ImVec2(1.0f, ConsumedHeight));
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        //---------------------------------------------------------- FOOTER -----------------------------------------------------------
        // Pinned below the scroll, not inside it: a tally that scrolls out of view is a tally nobody reads. The header owns the
        // live/gated split, so the footer carries what the header cannot — the selection that split was resolved against, and how to
        // find out why a tile is greyed.
        DrawList->AddRectFilled(ImVec2(RailRight, FooterTop), ImVec2(Origin.x + CardWidth, Origin.y + CardHeight),
                                GridGround, CardRounding * Scale, ImDrawFlags_RoundCornersBottomRight);
        DrawList->AddLine(ImVec2(RailRight, FooterTop), ImVec2(Origin.x + CardWidth, FooterTop),
                          HairLine, Theme.Metrics.BorderThickness);

        const char* StratumWord = DescribeTopologyStratum(Descriptor.ActiveStratum);
        char FooterText[64] = {};
        std::snprintf(FooterText, sizeof(FooterText), "on %d %s%s", Descriptor.SelectedCount, StratumWord,
                      Descriptor.SelectedCount == 1 ? "" : "s");
        const float FooterMidY = FooterTop + FooterHeight * Scale * 0.5f;
        DrawList->AddText(ImVec2(RailRight + PanePadding * Scale, FooterMidY - ImGui::GetTextLineHeight() * 0.5f),
                          TextFaintTone, FooterText);

        const char* HintText = "hover a gated tool";
        const ImVec2 HintSize = ImGui::CalcTextSize(HintText);
        DrawList->AddText(ImVec2(Origin.x + CardWidth - PanePadding * Scale - HintSize.x,
                                 FooterMidY - HintSize.y * 0.5f), TextFaintTone, HintText);

        // Dismissal: a press outside the card, or Escape. Reported rather than acted on, because the caller owns whether the card is
        // open — the same split the shipped menu uses.
        const bool PointerOnCard = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        const bool PointerDown   = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if ((PointerDown && !PointerOnCard) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            Result.DismissRequested = true;
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    Result.OpenBand = OpenBand;
    return Result;
}

}   // namespace Frontier
