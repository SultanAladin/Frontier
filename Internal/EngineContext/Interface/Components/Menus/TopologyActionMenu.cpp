/*==============================================================================================================================================
                                                        TOPOLOGYACTIONMENU.CPP
==============================================================================================================================================*/
// 🧩 The selection-driven context menu card: header badge, band captions, and one row per resolved operation with its glyph, label, and either a
//    keystroke chip or a shortfall chip. Drawn entirely through ImDrawList over invisible hit-test buttons rather than through ImGui::Selectable,
//    because a row carries a vector glyph, a two-tone label, and a right-aligned chip whose tint depends on the gate — none of which a Selectable
//    can express without fighting its own padding.
//    Every metric below is the prototype's, divided by its ×1.35 presentation scale. The tones are the prototype's `:root` block rather than the
//    shared palette — see the ⚠️ note on the constant block for why this one component owns its own token set.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "TopologyActionMenu.h"
#include "PolygonMutation/PolygonGlyphTable.h"
#include "PolygonMutation/StratumBadgeTable.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Card geometry, in unscaled pixels — every use multiplies by Metrics.UiScale so one config knob resizes the whole menu.
    //    Each value is the prototype's CSS pixel divided by the ×1.35 it uses to read on a desktop page, so the two stay comparable:
    //    card 362→268, row 35→26, glyph tile 26→19, card radius 20→15, row radius 12→9.
    constexpr float CardWidthDefault  = 268.0f;   // [px] - Card width when the descriptor leaves it 0 (.ctx width 362)
    constexpr float CardPadding       = 8.0f;     // [px] - Inner margin of the card body (.ctx-body padding 11)
    constexpr float HeaderHeight      = 40.0f;    // [px] - Header strip height (.ctx-head 12px padding around a 26px tile)
    constexpr float RowHeightDefault  = 26.0f;    // [px] - One operation row (.tool height 35)
    constexpr float CaptionHeight     = 20.0f;    // [px] - Band caption strip (.grp-cap 10+5 padding over a 10px cap)
    constexpr float GlyphTileSide     = 19.0f;    // [px] - The black rounded tile behind a row's glyph (.t-ic 26)
    constexpr float GlyphTileRounding = 6.0f;     // [px] - That tile's corner radius (.t-ic radius 8)
    constexpr float GlyphBoxSide      = 11.0f;    // [px] - The glyph drawn inside the tile (.t-ic .icon 15)
    constexpr float GlyphGutter       = 34.0f;    // [px] - Row left edge to the label: 6 padding + 19 tile + 9 gap (.tool gap 10)
    constexpr float CardRounding      = 15.0f;    // [px] - Card corner radius (--panel-radius 20)
    constexpr float RowRounding       = 9.0f;     // [px] - Row hover-fill corner radius (--item-radius 12)
    constexpr float ChipPadding       = 4.5f;     // [px] - Horizontal padding inside a keystroke / reason chip (.t-key 6)
    constexpr float ChipRounding      = 4.5f;     // [px] - Chip corner radius (.t-key radius 6)
    constexpr float BranchArrowWidth  = 8.0f;     // [px] - Space reserved for a branch's disclosure triangle
    constexpr float GlyphStrokeWidth  = 1.2f;     // [px] - Glyph line weight, deliberately scale-independent
    constexpr float HeaderBadgeSide   = 19.0f;    // [px] - The stratum badge tile in the header (.h-ic 26)
    constexpr float SeparatorMargin   = 7.0f;     // [px] - Horizontal inset of the pre-Removal rule (.sep margin 6px 10px)
    constexpr float SeparatorHeight   = 5.0f;     // [px] - Vertical space that rule occupies

    // 📝 Scroll feel. WheelStep is given in ROWS rather than pixels so one notch always advances the same number of rows whatever
    //    the row height or UI scale. ScrollEaseRate is the exponential decay constant: ~18 settles a long throw in about a fifth
    //    of a second — fast enough not to feel sluggish, slow enough that the eye can follow the rows past.
    constexpr float WheelStep         = RowHeightDefault * 2.5f;   // [px] - Travel per wheel notch
    constexpr float ScrollEaseRate    = 18.0f;    // [1/s]- Ease-out rate; higher is snappier

    // 📝 ⚠️ The menu carries its OWN tones rather than reading ColorPaletteDescriptor, and that is deliberate. The active dark
    //    palette is lifted from the UVeditor prototype (--panel-2 #0e0e0e, a NEAR-WHITE AccentPrimary #e8e8e8), whereas this
    //    component is a port of ControlsPreview/TopologyActionMenu.html, whose tokens are a different set: a #1b1b1e header that
    //    reads against a #131315 body, and a BLUE #4a90e2 accent. Reading the shared palette gave an invisible header and white
    //    "emphasised" rows. Overwriting the shared palette would have restyled every other panel in the engine to match this one
    //    menu. So the mockup's `:root` block lives here, one constant per token, until the engine adopts that token set wholesale.
    constexpr ImU32 CardGround        = IM_COL32( 19,  19,  21, 255);   // --panel-bg    #131315
    constexpr ImU32 HeaderGround      = IM_COL32( 27,  27,  30, 255);   // --panel-head  #1b1b1e
    constexpr ImU32 RowHoverGround    = IM_COL32( 35,  35,  38, 255);   // --value-bg    #232326
    constexpr ImU32 TileGround        = IM_COL32( 10,  10,  11, 255);   // --value-black #0a0a0b
    constexpr ImU32 BranchHoverGround = IM_COL32( 51,  51,  58, 255);   // --value-unit  #33333a
    constexpr ImU32 HairLine          = IM_COL32(255, 255, 255,  18);   // --hair        rgba(255,255,255,.07)
    constexpr ImU32 AccentTone        = IM_COL32( 74, 144, 226, 255);   // --accent      #4a90e2
    constexpr ImU32 TextPrimaryTone   = IM_COL32(233, 233, 236, 255);   // --text-primary #e9e9ec
    constexpr ImU32 TextMutedTone     = IM_COL32(123, 123, 130, 255);   // --text-muted  #7b7b82
    constexpr ImU32 TextFaintTone     = IM_COL32( 85,  85,  91, 255);   // --text-faint  #55555b
    constexpr ImU32 TextOnAccentTone  = IM_COL32(255, 255, 255, 255);   // .tool.hot:hover .t-ic colour #fff

    constexpr ImU32 DangerTint        = IM_COL32(214,  92,  84, 255);   // --danger #d65c54
    constexpr ImU32 DangerRowWash     = IM_COL32(214,  92,  84,  31);   // .tool.warn:hover background rgba(214,92,84,.12)
    constexpr ImU32 DangerLabelHover  = IM_COL32(255, 143, 143, 255);   // .tool.warn:hover colour #ff8f8f
    constexpr ImU32 ShortfallText     = IM_COL32(201, 139, 134, 255);   // .tool.gated .t-key colour #c98b86
    constexpr ImU32 ShortfallFill     = IM_COL32(214,  92,  84,  33);   // .tool.gated .t-key background rgba(214,92,84,.13)
    constexpr ImU32 CountShortText    = IM_COL32(201, 169, 107, 255);   // .tool.gated.count .t-key colour #c9a96b
    constexpr ImU32 CountShortFill    = IM_COL32(201, 169, 107,  33);   // .tool.gated.count .t-key background rgba(201,169,107,.13)
    constexpr float GatedGlyphAlpha   = 0.38f;                          // .tool.gated .t-ic opacity
    constexpr float GatedLabelAlpha   = 0.75f;                          // .tool.gated .t-name rgba(123,123,130,.75)

    // 📝 A greyed row keeps its glyph and label legible but clearly inert, by thinning the tone it would otherwise draw in rather
    //    than by naming a second "disabled" colour for every one above — which is what the mockup's `opacity` rules do.
    ImU32 BlendTowardTransparent(ImU32 Colour, float Alpha)
    {
        ImVec4 Channels = ImGui::ColorConvertU32ToFloat4(Colour);
        Channels.w *= Alpha;
        return ImGui::ColorConvertFloat4ToU32(Channels);
    }

    // 📝 The severity tint for a row's glyph and label. Emphasised rows take the accent (the mockup's `.hot`), destructive rows
    //    a warm red, everything else the primary text tone.
    ImU32 ResolveRowTint(const ThemeConfiguration& Theme, OperationSeverity Severity, bool Available)
    {
        ImU32 Tint = TextPrimaryTone;
        if (Severity == OperationSeverity::Emphasised)
        {
            Tint = AccentTone;
        }
        else if (Severity == OperationSeverity::Destructive)
        {
            // ⚠️ The palette carries no Danger field, so the destructive tone is composed here. Kept as one named constant
            //    rather than sprinkled inline, so promoting it to the palette later is a single edit.
            Tint = IM_COL32(214, 92, 84, 255);
        }

        return Available ? Tint : BlendTowardTransparent(Tint, 0.38f);
    }

    // 📝 Draw a small rounded chip with right-aligned text, returning the width it consumed. Serves both the keystroke chip and
    //    the shortfall chip — they differ only in tint, which is the caller's decision.
    float InscribeChip(ImDrawList* DrawList, const char* Text, ImVec2 RowTopRight, float RowHeight, ImU32 TextTint, ImU32 FillTint, float Scale)
    {
        if (Text == nullptr || Text[0] == '\0')
        {
            return 0.0f;
        }

        const ImVec2 TextSize  = ImGui::CalcTextSize(Text);
        const float  ChipWidth = TextSize.x + ChipPadding * 2.0f * Scale;
        const float  ChipHeight = TextSize.y + 2.0f * Scale;
        const ImVec2 ChipMin(RowTopRight.x - ChipWidth, RowTopRight.y + (RowHeight - ChipHeight) * 0.5f);
        const ImVec2 ChipMax(RowTopRight.x, ChipMin.y + ChipHeight);

        if ((FillTint & IM_COL32_A_MASK) != 0u)
        {
            DrawList->AddRectFilled(ChipMin, ChipMax, FillTint, ChipRounding * Scale);
        }
        DrawList->AddText(ImVec2(ChipMin.x + ChipPadding * Scale, ChipMin.y + 1.0f * Scale), TextTint, Text);
        return ChipWidth;
    }

    // 📝 A right-pointing disclosure chevron for a branch row — two strokes, not a filled triangle. The mockup draws `›`, and a
    //    solid head at this size reads as a severity marker competing with the glyph tile rather than as "opens further".
    void InscribeBranchArrow(ImDrawList* DrawList, ImVec2 Centre, float Size, ImU32 Tint, float StrokeWidth)
    {
        const float Reach = Size * 0.30f;
        DrawList->PathLineTo(ImVec2(Centre.x - Reach, Centre.y - Size * 0.42f));
        DrawList->PathLineTo(ImVec2(Centre.x + Reach, Centre.y));
        DrawList->PathLineTo(ImVec2(Centre.x - Reach, Centre.y + Size * 0.42f));
        DrawList->PathStroke(Tint, ImDrawFlags_None, StrokeWidth);
    }

    // 📝 Record a branch's option card and report the option clicked, or -1. Options adopt the Dropdown `.dd-item` behaviour from
    //    ControlsPreview rather than ImGui::Selectable: on hover the row squares off, fills with the side-segment tone, gains a
    //    full-height accent bar down its left edge and shifts its label clear of that bar. A Selectable can express none of it,
    //    and the branch is the one place in the menu where the Controls vocabulary is directly visible.
    int ConstructOperationBranch(const ThemeConfiguration&   Theme,
                                 const MenuBranchDescriptor& Branch,
                                 int                         RowIndex,
                                 ImVec2                      CardOrigin,
                                 ImGuiWindowFlags            CardFlags,
                                 bool&                       PointerWithinBranch)
    {
        const float Scale        = Theme.Metrics.UiScale;
        const float OptionHeight = 22.0f * Scale;      // .b-item — 9px padding around a 13px label
        const float CaptionBlock = CaptionHeight * Scale;
        const float MarkerSide   = 12.0f * Scale;      // .b-item .radio 17
        const float AccentBar    = 3.0f * Scale;       // .b-item:hover::before width 4
        const float LabelShift   = 5.0f * Scale;       // .b-item:hover padding-left 12 → 18
        const float BranchWidth  = 157.0f * Scale;     // .sub width 212

        int ChosenOption = -1;

        ImGui::SetNextWindowPos(CardOrigin);
        ImGui::SetNextWindowSize(ImVec2(BranchWidth, CaptionBlock + OptionHeight * static_cast<float>(Branch.OptionCount)
                                                     + CardPadding * 2.0f * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, CardPadding * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, RowRounding * Scale);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, HeaderGround);

        char BranchId[64] = {};
        std::snprintf(BranchId, sizeof(BranchId), "##branch-%d", RowIndex);
        if (ImGui::Begin(BranchId, nullptr, CardFlags | ImGuiWindowFlags_NoFocusOnAppearing))
        {
            ImDrawList*  DrawList = ImGui::GetWindowDrawList();
            const ImVec2 Origin   = ImGui::GetWindowPos();

            // The branch repeats its own caption: a card floating clear of its row has no other way to say what it is choosing.
            float CursorY = ImGui::GetCursorScreenPos().y;
            DrawList->AddText(ImVec2(Origin.x + CardPadding * 1.5f * Scale, CursorY + 4.0f * Scale),
                              TextMutedTone, Branch.Caption);
            CursorY += CaptionBlock;

            for (int OptionIndex = 0; OptionIndex < Branch.OptionCount; ++OptionIndex)
            {
                const char*  Option = Branch.Options[OptionIndex];
                const ImVec2 OptionMin(Origin.x, CursorY);
                const ImVec2 OptionMax(Origin.x + BranchWidth, CursorY + OptionHeight);

                ImGui::SetCursorScreenPos(OptionMin);
                ImGui::PushID(OptionIndex);
                const bool Pressed = ImGui::InvisibleButton("##option", ImVec2(BranchWidth, OptionHeight));
                const bool Hovered = ImGui::IsItemHovered();
                ImGui::PopID();

                float LabelX = OptionMin.x + CardPadding * 1.5f * Scale;
                if (Hovered)
                {
                    // Square corners and a full-height bar, not a rounded pill — the Dropdown signature.
                    DrawList->AddRectFilled(OptionMin, OptionMax, BranchHoverGround, 0.0f);
                    DrawList->AddRectFilled(OptionMin, ImVec2(OptionMin.x + AccentBar, OptionMax.y),
                                            AccentTone, 0.0f);
                    LabelX += LabelShift;
                }

                const ImVec2 LabelSize = ImGui::CalcTextSize(Option);
                DrawList->AddText(ImVec2(LabelX, OptionMin.y + (OptionHeight - LabelSize.y) * 0.5f),
                                  TextPrimaryTone, Option);

                // The marked option carries a filled radio, so the branch states what is already in effect.
                if (OptionIndex == Branch.MarkedIndex)
                {
                    const ImVec2 MarkerCentre(OptionMax.x - CardPadding * 1.5f * Scale - MarkerSide * 0.5f,
                                              OptionMin.y + OptionHeight * 0.5f);
                    DrawList->AddCircle(MarkerCentre, MarkerSide * 0.5f, AccentTone, 0, 1.5f * Scale);
                    DrawList->AddCircleFilled(MarkerCentre, MarkerSide * 0.5f - 2.5f * Scale, AccentTone);
                }

                if (Pressed)
                {
                    ChosenOption = OptionIndex;
                }
                CursorY += OptionHeight;
            }

            // Reported so the caller can keep the latch closed while the pointer is travelling across this card — the gap
            // between the row and its options is where a hover-tracked submenu would otherwise vanish.
            PointerWithinBranch = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        return ChosenOption;
    }

    // 📝 Locate the branch payload whose caption matches a row's BranchCaption. Compared by content rather than by pointer:
    //    the caption constants live in the catalogue's translation unit, and a caller assembling branches elsewhere would
    //    otherwise have to share those exact pointers.
    const MenuBranchDescriptor* FindBranch(const TopologyActionMenuDescriptor& Descriptor, const char* Caption)
    {
        if (Caption == nullptr || Descriptor.Branches == nullptr)
        {
            return nullptr;
        }
        for (int Index = 0; Index < Descriptor.BranchCount; ++Index)
        {
            const MenuBranchDescriptor& Branch = Descriptor.Branches[Index];
            if (Branch.Caption != nullptr && std::strcmp(Branch.Caption, Caption) == 0)
            {
                return &Branch;
            }
        }
        return nullptr;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float ResolveTopologyActionMenuHeight(const ThemeConfiguration&     Theme,
                                      const ResolvedOperationEntry* Entries,
                                      int                           EntryCount)
{
    if (Entries == nullptr || EntryCount <= 0)
    {
        return 0.0f;
    }

    const float Scale = Theme.Metrics.UiScale;
    float Height = HeaderHeight * Scale + CardPadding * 2.0f * Scale;

    // Band captions and the pre-Removal rule are what make the tall menus tall, so they are counted rather than approximated:
    // exactly what the row loop emits, in the same order, so a scrolling card's content height is never a guess.
    OperationBand PreviousBand = OperationBand::BandCount;
    for (int Index = 0; Index < EntryCount; ++Index)
    {
        if (Entries[Index].Descriptor == nullptr)
        {
            continue;
        }
        const OperationBand Band = Entries[Index].Descriptor->Band;
        if (Band != PreviousBand)
        {
            if (PreviousBand != OperationBand::BandCount && Band == OperationBand::Removal)
            {
                Height += SeparatorHeight * Scale;
            }
            Height += CaptionHeight * Scale;
            PreviousBand = Band;
        }
        Height += RowHeightDefault * Scale;
    }
    return Height;
}

MenuGrowth ResolveMenuPlacement(ImVec2 AnchorPosition, ImVec2 CardSize, ImVec2 BandTopLeft, ImVec2 BandBottomRight)
{
    // Flip per axis independently: a card near the right edge flips horizontally but still opens downward, and only a corner
    // flips both. Flipping both together (the naive version) makes a menu jump diagonally away from the click.
    const bool OverflowsRight  = (AnchorPosition.x + CardSize.x) > BandBottomRight.x;
    const bool OverflowsBottom = (AnchorPosition.y + CardSize.y) > BandBottomRight.y;

    // A flip is only an improvement if the flipped side actually has room; otherwise keep the original side and let the caller's
    // confinement clamp handle it, which at least keeps the anchor visible.
    const bool RoomLeft = (AnchorPosition.x - CardSize.x) >= BandTopLeft.x;
    const bool RoomAbove = (AnchorPosition.y - CardSize.y) >= BandTopLeft.y;

    const bool FlipHorizontal = OverflowsRight && RoomLeft;
    const bool FlipVertical   = OverflowsBottom && RoomAbove;

    if (FlipVertical)
    {
        return FlipHorizontal ? MenuGrowth::UpLeft : MenuGrowth::UpRight;
    }
    return FlipHorizontal ? MenuGrowth::DownLeft : MenuGrowth::DownRight;
}

TopologyActionMenuResult ConstructTopologyActionMenu(const ThemeConfiguration& Theme, const TopologyActionMenuDescriptor& Descriptor)
{
    TopologyActionMenuResult Result{ -1, -1, -1, false };

    // An empty menu is a caller bug (nothing selected), not an empty box to render.
    if (Descriptor.Entries == nullptr || Descriptor.EntryCount <= 0)
    {
        return Result;
    }

    const float Scale       = Theme.Metrics.UiScale;
    const float CardWidth   = (Descriptor.CardWidth > 0.0f ? Descriptor.CardWidth : CardWidthDefault) * Scale;
    const float ContentHeight = ResolveTopologyActionMenuHeight(Theme, Descriptor.Entries, Descriptor.EntryCount);

    // A full catalogue is taller than any screen, so the body scrolls past HeightLimit exactly as the mockup's `max-height:68vh`
    // does. Clamped rather than left to ImGui's own viewport clamp, which would slide the card up off its anchor instead.
    const bool  Scrolling  = (Descriptor.HeightLimit > 0.0f) && (ContentHeight > Descriptor.HeightLimit);
    const float CardHeight = Scrolling ? Descriptor.HeightLimit : ContentHeight;

    // Resolve the card origin from the anchor and the growth direction.
    ImVec2 CardOrigin = Descriptor.AnchorPosition;
    if (Descriptor.Growth == MenuGrowth::DownLeft || Descriptor.Growth == MenuGrowth::UpLeft)
    {
        CardOrigin.x -= CardWidth;
    }
    if (Descriptor.Growth == MenuGrowth::UpRight || Descriptor.Growth == MenuGrowth::UpLeft)
    {
        CardOrigin.y -= CardHeight;
    }

    ImGui::SetNextWindowPos(CardOrigin);
    ImGui::SetNextWindowSize(ImVec2(CardWidth, CardHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CardRounding * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, Theme.Metrics.BorderThickness);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, CardGround);
    ImGui::PushStyleColor(ImGuiCol_Border, HairLine);

    const ImGuiWindowFlags CardFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin(Descriptor.Identifier, nullptr, CardFlags))
    {
        // Rebound to the body's own draw list once the scroll region opens, so rows clip to it rather than to the card.
        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        const ImVec2 Origin  = ImGui::GetWindowPos();

        //---------------------------------------------------------- HEADER ----------------------------------------------------------
        // [ badge | stratum name over "N of M operations" | "N selected" pill ] — the menu answers "why these rows?" itself.
        const ImVec2 HeaderMin = Origin;
        const ImVec2 HeaderMax(Origin.x + CardWidth, Origin.y + HeaderHeight * Scale);
        DrawList->AddRectFilled(HeaderMin, HeaderMax, HeaderGround, CardRounding * Scale, ImDrawFlags_RoundCornersTop);

        const float  HeaderMidY   = (HeaderMin.y + HeaderMax.y) * 0.5f;
        const ImVec2 BadgeOrigin(HeaderMin.x + CardPadding * 1.5f * Scale, HeaderMidY - HeaderBadgeSide * Scale * 0.5f);
        InscribeStratumBadge(DrawList, Descriptor.ActiveStratum, BadgeOrigin, HeaderBadgeSide * Scale);

        // The "N selected" pill is measured first: the title beside it is ellipsised against whatever room the pill leaves, so a
        // long stratum name can never overrun the count.
        char CountText[32] = {};
        std::snprintf(CountText, sizeof(CountText), "%d selected", Descriptor.SelectedCount);
        const ImVec2 CountSize  = ImGui::CalcTextSize(CountText);
        const float  PillPadX   = 6.0f * Scale;
        const float  PillPadY   = 3.0f * Scale;
        const ImVec2 PillMax(HeaderMax.x - CardPadding * 1.5f * Scale, HeaderMidY + CountSize.y * 0.5f + PillPadY);
        const ImVec2 PillMin(PillMax.x - CountSize.x - PillPadX * 2.0f, HeaderMidY - CountSize.y * 0.5f - PillPadY);
        DrawList->AddRectFilled(PillMin, PillMax, RowHoverGround, (PillMax.y - PillMin.y) * 0.5f);
        DrawList->AddText(ImVec2(PillMin.x + PillPadX, PillMin.y + PillPadY), TextMutedTone, CountText);

        // Title over subtitle. The subtitle states the FILTERED count against the catalogue total, which is what tells a reader
        // the menu is a selection of 94 rather than all there is.
        const float TitleX = BadgeOrigin.x + HeaderBadgeSide * Scale + CardPadding * Scale;
        const char* StratumName = DescribeTopologyStratum(Descriptor.ActiveStratum);

        if (Descriptor.CatalogueCount > 0)
        {
            char SubtitleText[48] = {};
            std::snprintf(SubtitleText, sizeof(SubtitleText), "%d of %d operations", Descriptor.EntryCount, Descriptor.CatalogueCount);
            const float LineHeight = ImGui::GetTextLineHeight();
            DrawList->AddText(ImVec2(TitleX, HeaderMidY - LineHeight - 1.0f * Scale), TextPrimaryTone, StratumName);
            DrawList->AddText(ImVec2(TitleX, HeaderMidY + 1.0f * Scale), TextMutedTone, SubtitleText);
        }
        else
        {
            DrawList->AddText(ImVec2(TitleX, HeaderMidY - ImGui::GetTextLineHeight() * 0.5f),
                              TextPrimaryTone, StratumName);
        }

        DrawList->AddLine(ImVec2(HeaderMin.x, HeaderMax.y), ImVec2(HeaderMax.x, HeaderMax.y),
                          HairLine, Theme.Metrics.BorderThickness);

        //----------------------------------------------------------- ROWS -----------------------------------------------------------
        // Which row currently owns the open branch. Read from the CARD's storage before the body region opens, since a child
        // window carries its own storage and the latch has to outlive the region it is set from.
        ImGuiStorage* const CardState = ImGui::GetStateStorage();
        const ImGuiID       LatchKey  = ImGui::GetID("##branch-latch");
        int   LatchedBranchRow        = CardState->GetInt(LatchKey, -1);
        const int   BranchLatchRow    = LatchedBranchRow;    // The latch as it stood on entry, before this frame's hovers move it
        const char* BranchOwnerCaption = nullptr;
        ImVec2      BranchOrigin(0.0f, 0.0f);

        // The body is a child region so a full catalogue scrolls inside the card instead of growing past the screen. Its own
        // background is transparent — the card already painted it — and it keeps the header pinned above.
        // 📝 NoScrollbar unconditionally: the card stays a compact rectangle and the wheel still reaches every row, but no gutter
        //    is spent on a bar. A context menu is a transient card, and a visible bar reads as a scrolling PANEL — the mockup
        //    shows none. The eased scroll below is what tells a reader there is more, in place of the bar.
        const ImVec2 BodyOrigin(Origin.x, HeaderMax.y);
        const ImVec2 BodySize(CardWidth, CardHeight - HeaderHeight * Scale);
        ImGui::SetCursorScreenPos(BodyOrigin);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        // NoScrollWithMouse hands the wheel to the ease below: left on, ImGui writes ScrollY directly the same frame and the two
        // fight, which shows up as a jitter on every notch. The region still scrolls — just only through the target.
        ImGui::BeginChild("##body", BodySize, false,
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);

        DrawList = ImGui::GetWindowDrawList();

        // 🔴 Eased scrolling. ImGui applies the wheel to ScrollY immediately, which on a 20-row jump reads as a teleport with no
        //    sense of travel. The wheel is intercepted here into a TARGET held in the child's own storage, and the live ScrollY
        //    is stepped a fraction of the remaining distance each frame — exponential ease-out, so it moves fastest at the start
        //    and settles without overshoot. Frame-rate corrected via DeltaTime, or the menu would scroll at a different speed on
        //    a 144 Hz display than on a 60 Hz one.
        if (Scrolling)
        {
            ImGuiStorage* const BodyState = ImGui::GetStateStorage();
            const ImGuiID       TargetKey = ImGui::GetID("##scroll-target");
            const float         ScrollMax = ImGui::GetScrollMaxY();

            float ScrollTarget = BodyState->GetFloat(TargetKey, ImGui::GetScrollY());

            // The wheel is read at the START of the region, before ImGui has applied its own scroll for the frame, so the target
            // advances from where the ease is actually heading rather than from a position ImGui already jumped.
            if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
            {
                ScrollTarget -= ImGui::GetIO().MouseWheel * WheelStep * Scale;
            }
            // Clamped against last frame's extent, which is all ImGui can report. Skipped while that extent is still 0 — on the
            // frame the region first opens nothing has declared its content yet, and clamping there would zero a target the user
            // had just aimed with the wheel.
            if (ScrollTarget < 0.0f)                        { ScrollTarget = 0.0f; }
            if (ScrollMax > 0.0f && ScrollTarget > ScrollMax) { ScrollTarget = ScrollMax; }

            const float Remaining = ScrollTarget - ImGui::GetScrollY();
            if (Remaining < -0.5f || Remaining > 0.5f)
            {
                // 1 - exp(-k·dt) is the frame-rate-independent form of a per-frame lerp; a bare `Remaining * 0.25f` would ease
                // four times faster at 240 fps than at 60.
                const float Blend = 1.0f - std::exp(-ScrollEaseRate * ImGui::GetIO().DeltaTime);
                ImGui::SetScrollY(ImGui::GetScrollY() + Remaining * Blend);
            }
            else
            {
                ImGui::SetScrollY(ScrollTarget);      // Snap the last half pixel, or the row grid sits fractionally off forever.
            }

            BodyState->SetFloat(TargetKey, ScrollTarget);
        }

        float CursorY = ImGui::GetCursorScreenPos().y + CardPadding * Scale;
        OperationBand PreviousBand = OperationBand::BandCount;

        for (int Index = 0; Index < Descriptor.EntryCount; ++Index)
        {
            const ResolvedOperationEntry& Entry = Descriptor.Entries[Index];
            if (Entry.Descriptor == nullptr)
            {
                continue;
            }
            const TopologyOperationDescriptor& Operation = *Entry.Descriptor;

            // Band caption on every band transition, with a rule before REMOVAL — the destructive band is the one place the
            // mockup breaks the run of captions, so a mis-click there costs a deliberate crossing rather than a slip.
            if (Operation.Band != PreviousBand)
            {
                if (PreviousBand != OperationBand::BandCount && Operation.Band == OperationBand::Removal)
                {
                    const float RuleY = CursorY + SeparatorHeight * 0.5f * Scale;
                    DrawList->AddLine(ImVec2(Origin.x + SeparatorMargin * Scale, RuleY),
                                      ImVec2(Origin.x + CardWidth - SeparatorMargin * Scale, RuleY),
                                      HairLine, Theme.Metrics.BorderThickness);
                    CursorY += SeparatorHeight * Scale;
                }

                DrawList->AddText(ImVec2(Origin.x + CardPadding * 1.75f * Scale, CursorY + 5.0f * Scale),
                                  TextFaintTone, DescribeOperationBand(Operation.Band));
                CursorY += CaptionHeight * Scale;
                PreviousBand = Operation.Band;
            }

            const bool   Available = (Entry.Condition == AvailabilityCondition::Available);
            const float  RowHeight = RowHeightDefault * Scale;
            const ImVec2 RowMin(Origin.x + CardPadding * Scale, CursorY);
            const ImVec2 RowMax(Origin.x + CardWidth - CardPadding * Scale, CursorY + RowHeight);

            // An invisible button carries the hit test so the row's own painting stays free-form. Gated rows still get a button
            // (so the pointer does not fall through to the card behind) but never report activation.
            ImGui::SetCursorScreenPos(RowMin);
            ImGui::PushID(Index);
            const bool Pressed = ImGui::InvisibleButton("##row", ImVec2(RowMax.x - RowMin.x, RowHeight));
            const bool Hovered = ImGui::IsItemHovered();
            ImGui::PopID();

            const bool Destructive = (Operation.Severity == OperationSeverity::Destructive);
            const bool Emphasised  = (Operation.Severity == OperationSeverity::Emphasised);

            // A row whose branch is open keeps drawing as hovered even though the pointer has moved onto the branch card, or the
            // menu would show an open submenu belonging to a row that looks inert.
            const bool ActiveHover = Available && (Hovered || BranchLatchRow == Index);

            if (Hovered)
            {
                Result.HoveredIndex = Index;
            }
            if (ActiveHover)
            {
                // A destructive row hovers into a danger WASH rather than the neutral control tone, so the row that deletes
                // never looks like the row that translates a moment before the click lands.
                DrawList->AddRectFilled(RowMin, RowMax, Destructive ? DangerRowWash : RowHoverGround,
                                        RowRounding * Scale);
            }
            if (Pressed && Available)
            {
                Result.ActivatedIndex = Index;
            }

            const ImU32 RowTint   = ResolveRowTint(Theme, Operation.Severity, Available);
            const ImU32 LabelTint = Available ? TextPrimaryTone
                                              : BlendTowardTransparent(TextMutedTone, GatedLabelAlpha);

            //--- Glyph tile. The black rounded tile is what gives a 94-row menu a scannable left column, and it is also the
            //    surface severity uses: an emphasised row fills it with the accent on hover, exactly as `.tool.hot:hover` does.
            const ImVec2 TileMin(RowMin.x + 3.0f * Scale, RowMin.y + (RowHeight - GlyphTileSide * Scale) * 0.5f);
            const ImVec2 TileMax(TileMin.x + GlyphTileSide * Scale, TileMin.y + GlyphTileSide * Scale);

            const bool  AccentTile = ActiveHover && Emphasised;
            const ImU32 TileFill   = AccentTile ? AccentTone
                                                : (Available ? TileGround
                                                             : BlendTowardTransparent(TileGround, GatedGlyphAlpha));
            DrawList->AddRectFilled(TileMin, TileMax, TileFill, GlyphTileRounding * Scale);

            // The glyph over that tile. Its tint inverts to TextOnAccent once the tile is filled, or the accent-on-accent
            // would erase it — the one case where the row's severity tint is not the glyph's tint.
            ImU32 GlyphTint = RowTint;
            if (AccentTile)
            {
                GlyphTint = TextOnAccentTone;
            }
            else if (ActiveHover && Destructive)
            {
                GlyphTint = DangerLabelHover;
            }
            else if (!Available)
            {
                GlyphTint = BlendTowardTransparent(RowTint, GatedGlyphAlpha);
            }

            const ImVec2 GlyphOrigin(TileMin.x + (GlyphTileSide - GlyphBoxSide) * 0.5f * Scale,
                                     TileMin.y + (GlyphTileSide - GlyphBoxSide) * 0.5f * Scale);
            InscribePolygonGlyph(DrawList, static_cast<PolygonGlyph>(Operation.Glyph), GlyphOrigin,
                                 GlyphBoxSide * Scale, GlyphTint, GlyphStrokeWidth);

            //--- Label. Neutral rows read in the plain text tone; a destructive row brightens on hover, which is the mockup's
            //    `.tool.warn:hover` colour shift and the second half of the "this one is different" signal.
            ImU32 LabelColour = LabelTint;
            if (Destructive)
            {
                LabelColour = ActiveHover ? DangerLabelHover : RowTint;
            }
            else if (Emphasised && !AccentTile)
            {
                LabelColour = Available ? TextPrimaryTone : LabelTint;
            }

            const ImVec2 LabelSize = ImGui::CalcTextSize(Operation.Label);
            DrawList->AddText(ImVec2(RowMin.x + GlyphGutter * Scale, RowMin.y + (RowHeight - LabelSize.y) * 0.5f),
                              LabelColour, Operation.Label);

            // Right side: a branch chevron, then either the shortfall chip (gated) or the keystroke chip (available).
            float RightEdge = RowMax.x - ChipPadding * Scale;
            if (Operation.BranchCaption != nullptr)
            {
                InscribeBranchArrow(DrawList, ImVec2(RightEdge - BranchArrowWidth * 0.5f * Scale, RowMin.y + RowHeight * 0.5f),
                                    BranchArrowWidth * Scale, LabelTint, GlyphStrokeWidth * 1.2f);
                RightEdge -= BranchArrowWidth * 1.4f * Scale;
            }

            if (!Available && Entry.ShortfallText != nullptr)
            {
                // 🔴 The shortfall chip is the whole point of the gate layer: it names the missing precondition in the slot
                //    where the keystroke would otherwise sit, so the row explains itself without a tooltip. The two shortfall
                //    classes are tinted apart — amber for a COUNT the caller can fix by selecting more, red for a TOPOLOGY fact
                //    it cannot — because "select one more face" and "this ring is not quads" are not the same instruction.
                const bool  CountFault = (Entry.Condition == AvailabilityCondition::CountShortfall);
                InscribeChip(DrawList, Entry.ShortfallText, ImVec2(RightEdge, RowMin.y), RowHeight,
                             CountFault ? CountShortText : ShortfallText,
                             CountFault ? CountShortFill : ShortfallFill, Scale);
            }
            else if (Operation.KeyHint != nullptr)
            {
                InscribeChip(DrawList, Operation.KeyHint, ImVec2(RightEdge, RowMin.y), RowHeight,
                             TextMutedTone, TileGround, Scale);
            }

            // Hovering a branch row LATCHES it open; hovering any OTHER row closes it. Tracking it per-frame instead would be
            // unusable — the row's own hover goes false the moment the pointer crosses onto the branch card, so the card would
            // close under the cursor before an option could be clicked.
            const bool BranchRow = Available && (Operation.BranchCaption != nullptr);
            if (Hovered)
            {
                LatchedBranchRow = BranchRow ? Index : -1;
            }

            // Capture the geometry of whichever row the latch named ON ENTRY. Recording the card itself has to wait until the
            // scroll region closes — a child window would clip it, and overhanging the menu is the whole point of a branch.
            if (BranchRow && BranchLatchRow == Index)
            {
                BranchOwnerCaption = Operation.BranchCaption;
                BranchOrigin       = ImVec2(RowMax.x + 2.0f * Scale, RowMin.y - CardPadding * Scale);
            }

            CursorY += RowHeight;
        }

        // A scrolling body needs its content extent declared, or the region has nothing to scroll over: the rows were painted
        // through the draw list, which advances no ImGui cursor of its own.
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, CursorY + CardPadding * Scale));
        ImGui::Dummy(ImVec2(1.0f, 1.0f));

        ImGui::EndChild();
        ImGui::PopStyleColor();

        //---------------------------------------------------------- BRANCH ----------------------------------------------------------
        // Outside the scroll region, so the card overhangs the menu instead of being clipped by it.
        // Declared out here because the dismissal test below needs it too: a click on the branch is a click on the menu.
        bool PointerWithinBranch = false;
        if (BranchOwnerCaption != nullptr)
        {
            const MenuBranchDescriptor* Branch = FindBranch(Descriptor, BranchOwnerCaption);
            if (Branch != nullptr && Branch->Options != nullptr && Branch->OptionCount > 0)
            {
                const int BranchOption = ConstructOperationBranch(Theme, *Branch, LatchedBranchRow, BranchOrigin,
                                                                  CardFlags, PointerWithinBranch);
                if (BranchOption >= 0)
                {
                    Result.ActivatedIndex    = LatchedBranchRow;
                    Result.BranchOptionIndex = BranchOption;
                    LatchedBranchRow         = -1;      // Chosen: the branch has served its purpose and closes.
                }
                else if (PointerWithinBranch)
                {
                    // The pointer is on the branch, so a row-hover elsewhere this frame must not steal the latch back.
                    LatchedBranchRow = BranchLatchRow;
                }
            }
            else
            {
                // A row declaring a branch the caller supplied no payload for must not latch forever.
                LatchedBranchRow = -1;
            }
        }

        CardState->SetInt(LatchKey, LatchedBranchRow);

        // Dismissal: a press outside the card, or Escape. Reported rather than acted on, because the caller owns whether the
        // menu is open — a component that closed itself could not be reused by a caller that keeps it pinned.
        // ⚠️ Tested against the card's OWN rect, plus the branch child's, rather than IsWindowHovered(AnyWindow): inside a
        //    Begin/End pair that flag asks "is any window hovered", so a click over the caller's panel counted as INSIDE and
        //    the menu could never be dismissed. Either mouse button dismisses, so the right-click that re-anchors a menu
        //    elsewhere does not leave the previous card behind.
        const bool PointerOnCard   = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        const bool PointerDown     = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if ((PointerDown && !PointerOnCard && !PointerWithinBranch) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            Result.DismissRequested = true;
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    return Result;
}

}   // namespace Frontier
