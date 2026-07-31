/*==============================================================================================================================================
                                                                MENUPILL.CPP
==============================================================================================================================================*/
// 🧩 Paints the two-tone chrome pill and hosts its grouped menu in an ImGui popup, so the overlay ordering and click-outside-to-close come for
//    free. The pill's two halves are drawn as individually-rounded rectangles (the mockup rounds each half rather than clipping the parent, so
//    the menu can overhang) and the caret rotates 180° while open. The menu card reproduces `.gp-menu` > `.gp-head` > `.gp-card` > `.gp-item`:
//    an uppercase group caption, a rounded checkbox that fills with the accent when marked, a right-pinned keystroke chip, and hairline rules.

#include "MenuPill.h"

#include <cstddef>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Pill geometry, transcribed from the mockup CSS so the chrome matches the reference without re-deriving numbers.
    const float StandardPillHeight  = 30.0f;   // [px] - `.vp-gearpill` height
    const float CompactPillHeight   = 22.0f;   // [px] - `.vp-pill-sm` height
    const float StandardPillRadius  =  9.0f;   // [px] - `.vp-gearpill` border-radius
    const float CompactPillRadius   =  7.0f;   // [px] - `.vp-pill-sm` border-radius
    const float StandardCaretWidth  = 26.0f;   // [px] - `.gp-caret` width
    const float CompactCaretWidth   = 22.0f;   // [px] - `.vp-pill-sm .gp-caret` width
    const float StandardTextPadding = 11.0f;   // [px] - `.gp-main` horizontal padding
    const float CompactTextPadding  =  9.0f;   // [px] - `.vp-pill-sm .gp-main` padding
    const float PillContentGap      =  7.0f;   // [px] - `.gp-main` gap between icon/dot and text
    const float StatusDotRadius     =  3.0f;   // [px] - `.vdot` is 6px across
    const float MenuOffset          =  6.0f;   // [px] - `calc(100% + 6px)` menu standoff
    const float MenuMinimumWidth    = 190.0f;  // [px] - `.gp-menu` min-width
    const float MenuPadding         =  5.0f;   // [px] - `.gp-menu` padding
    const float MenuRounding        = 10.0f;   // [px] - `.gp-menu` border-radius
    const float CardRounding        =  9.0f;   // [px] - `.gp-card` border-radius
    const float ItemRounding        =  7.0f;   // [px] - `.gp-item` border-radius
    const float ItemHeight          = 28.0f;   // [px] - `.gp-item` padding 7px + 12.5px line
    const float ItemTextPadding     =  9.0f;   // [px] - `.gp-item` horizontal padding
    const float CheckBoxEdge        = 14.0f;   // [px] - `.gp-chk` is 14x14
    const float CheckBoxGap         =  9.0f;   // [px] - `.gp-item` gap
    const float GroupCaptionHeight  = 18.0f;   // [px] - `.gp-group` band
    const float SeparatorHeight     = 11.0f;   // [px] - `.gp-sep` 1px + 5px margins
    const float HeaderHeight        = 32.0f;   // [px] - `.gp-head` band
    const float KeyChipPaddingX     =  6.0f;   // [px] - `.gp-key` horizontal padding

    // 📝 The mockup's `.vdot` green (#4fd18b) — a literal because the shared palette carries no vitality tone.
    const ImU32 StatusDotTone = IM_COL32(79, 209, 139, 255);

    // 📝 Draw a chevron pointing down (or up while the menu is open, mirroring `.open .gp-caret svg{rotate(180deg)}`).
    void InscribeCaretGlyph(ImDrawList* Draw, ImVec2 Centre, float HalfSpan, ImU32 Tone, bool Inverted)
    {
        const float Rise = Inverted ? -HalfSpan * 0.55f : HalfSpan * 0.55f;
        const ImVec2 Left (Centre.x - HalfSpan, Centre.y - Rise * 0.5f);
        const ImVec2 Apex (Centre.x,            Centre.y + Rise * 0.5f);
        const ImVec2 Right(Centre.x + HalfSpan, Centre.y - Rise * 0.5f);
        Draw->AddLine(Left, Apex,  Tone, 1.5f);
        Draw->AddLine(Apex, Right, Tone, 1.5f);
    }

    // 📝 Resolve the widest row so the menu card sizes to its content, never narrower than the mockup's min-width.
    float ResolveMenuWidth(const MenuPillDescriptor& Descriptor)
    {
        float Widest = MenuMinimumWidth;
        if (Descriptor.MenuTitle != nullptr)
        {
            const float TitleSpan = ImGui::CalcTextSize(Descriptor.MenuTitle).x + ItemTextPadding * 4.0f;
            if (TitleSpan > Widest) { Widest = TitleSpan; }
        }

        for (int Index = 0; Index < Descriptor.ItemCount; ++Index)
        {
            const MenuPillItemDescriptor& Item = Descriptor.Items[Index];
            float RowSpan = ItemTextPadding * 2.0f + CheckBoxEdge + CheckBoxGap;
            if (Item.Label != nullptr) { RowSpan += ImGui::CalcTextSize(Item.Label).x; }
            if (Item.KeyHint != nullptr)
            {
                RowSpan += CheckBoxGap + ImGui::CalcTextSize(Item.KeyHint).x + KeyChipPaddingX * 2.0f;
            }
            RowSpan += MenuPadding * 4.0f;   // card + menu padding on both sides
            if (RowSpan > Widest) { Widest = RowSpan; }
        }
        return Widest;
    }

    // 📝 Total menu height from the row metrics, so the anchor fit test below is exact rather than guessed.
    float ResolveMenuHeight(const MenuPillDescriptor& Descriptor)
    {
        float Height = MenuPadding * 2.0f;                       // `.gp-menu` padding
        if (Descriptor.MenuTitle != nullptr) { Height += HeaderHeight; }

        Height += MenuPadding * 2.0f;                            // `.gp-card` padding
        for (int Index = 0; Index < Descriptor.ItemCount; ++Index)
        {
            const MenuPillItemDescriptor& Item = Descriptor.Items[Index];
            if (Item.GroupCaption != nullptr)       { Height += GroupCaptionHeight; }
            if (Item.SeparatorPreceding)            { Height += SeparatorHeight; }
            Height += ItemHeight;
        }
        return Height;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float ResolveMenuPillHeight(bool CompactEnabled)
{
    return CompactEnabled ? CompactPillHeight : StandardPillHeight;
}


MenuPillResult ConstructMenuPill(const ThemeConfiguration& Theme, const MenuPillDescriptor& Descriptor)
{
    MenuPillResult Result = {};
    Result.ActivatedIndex = -1;

    if (Descriptor.Identifier == nullptr)
    {
        return Result;
    }

    const bool  Compact     = Descriptor.CompactEnabled;
    const float Height      = ResolveMenuPillHeight(Compact);
    const float Radius      = Compact ? CompactPillRadius  : StandardPillRadius;
    const float CaretWidth  = Compact ? CompactCaretWidth  : StandardCaretWidth;
    const float TextPadding = Compact ? CompactTextPadding : StandardTextPadding;
    const float IconEdge    = Height - 14.0f;

    // -- Measure the label half so the pill sizes to its content -------------------------------------------------------
    float LabelSpan = TextPadding * 2.0f;
    if (Descriptor.StatusDotEnabled)     { LabelSpan += StatusDotRadius * 2.0f + PillContentGap; }
    if (Descriptor.IconTexture != 0)     { LabelSpan += IconEdge + PillContentGap; }
    if (Descriptor.PrefixText != nullptr){ LabelSpan += ImGui::CalcTextSize(Descriptor.PrefixText).x + 4.0f; }
    if (Descriptor.MainText != nullptr)  { LabelSpan += ImGui::CalcTextSize(Descriptor.MainText).x; }

    const ImVec2 Origin  = ImGui::GetCursorScreenPos();
    const float  PillSpan = LabelSpan + CaretWidth;

    ImGui::PushID(Descriptor.Identifier);

    // 📝 One button spans the whole pill: the mockup opens the menu from either half, so a single hit area is faithful AND
    //    avoids two ids fighting over the same rect.
    const bool Pressed = ImGui::InvisibleButton("##pill", ImVec2(PillSpan, Height));
    Result.Hovered     = ImGui::IsItemHovered();

    const char* PopupIdentifier = "##menu";
    if (Pressed)
    {
        ImGui::OpenPopup(PopupIdentifier);
    }
    const bool MenuOpen = ImGui::IsPopupOpen(PopupIdentifier);
    Result.MenuOpen     = MenuOpen;

    // -- Paint the two halves ------------------------------------------------------------------------------------------
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const ImVec2 PillMin = Origin;
    const ImVec2 PillMax(Origin.x + PillSpan, Origin.y + Height);
    const ImVec2 SplitTop(Origin.x + LabelSpan, Origin.y);
    const ImVec2 SplitBottom(Origin.x + LabelSpan, PillMax.y);

    // 📝 Each half is rounded on its own outer corners only — the mockup's trick for a two-tone pill that does not clip the menu.
    //    No outline and no divider hairline: the two tones alone separate the halves, so the pill reads flat against the band.
    const ImU32 LabelTone = (Result.Hovered || MenuOpen) ? Theme.Palette.ControlActive : Theme.Palette.PanelHeader;
    Draw->AddRectFilled(PillMin, SplitBottom, LabelTone, Radius,
                        ImDrawFlags_RoundCornersLeft);
    Draw->AddRectFilled(SplitTop, PillMax, Theme.Palette.ValueNumberSegment, Radius,
                        ImDrawFlags_RoundCornersRight);

    // -- Label half contents (dot, icon, dim prefix, main text) --------------------------------------------------------
    float CursorX = Origin.x + TextPadding;
    const float MidY = (PillMin.y + PillMax.y) * 0.5f;

    if (Descriptor.StatusDotEnabled)
    {
        Draw->AddCircleFilled(ImVec2(CursorX + StatusDotRadius, MidY), StatusDotRadius, StatusDotTone, 12);
        CursorX += StatusDotRadius * 2.0f + PillContentGap;
    }
    if (Descriptor.IconTexture != 0)
    {
        const ImVec2 IconMin(CursorX, MidY - IconEdge * 0.5f);
        Draw->AddImage(Descriptor.IconTexture, IconMin, ImVec2(IconMin.x + IconEdge, IconMin.y + IconEdge));
        CursorX += IconEdge + PillContentGap;
    }
    if (Descriptor.PrefixText != nullptr)
    {
        const ImVec2 PrefixSize = ImGui::CalcTextSize(Descriptor.PrefixText);
        Draw->AddText(ImVec2(CursorX, MidY - PrefixSize.y * 0.5f), Theme.Palette.TextMuted, Descriptor.PrefixText);
        CursorX += PrefixSize.x + 4.0f;
    }
    if (Descriptor.MainText != nullptr)
    {
        const ImVec2 MainSize = ImGui::CalcTextSize(Descriptor.MainText);
        const ImU32  MainTone = (Result.Hovered || MenuOpen) ? Theme.Palette.TextPrimary : Theme.Palette.TextMuted;
        Draw->AddText(ImVec2(CursorX, MidY - MainSize.y * 0.5f), MainTone, Descriptor.MainText);
    }

    // -- Caret half ----------------------------------------------------------------------------------------------------
    InscribeCaretGlyph(Draw, ImVec2(SplitTop.x + CaretWidth * 0.5f, MidY), Compact ? 4.0f : 5.0f,
                       Result.Hovered ? Theme.Palette.TextPrimary : Theme.Palette.TextMuted, MenuOpen);

    // -- The floating menu ---------------------------------------------------------------------------------------------
    if (Descriptor.Items != nullptr && Descriptor.ItemCount > 0)
    {
        const float MenuWidth  = ResolveMenuWidth(Descriptor);
        const float MenuHeight = ResolveMenuHeight(Descriptor);

        const bool OpensAbove  = (Descriptor.Anchor == MenuPillAnchor::AboveLeadingEdge)
                              || (Descriptor.Anchor == MenuPillAnchor::AboveTrailingEdge);
        const bool TrailingEdge = (Descriptor.Anchor == MenuPillAnchor::BelowTrailingEdge)
                               || (Descriptor.Anchor == MenuPillAnchor::AboveTrailingEdge);

        float MenuX = TrailingEdge ? (PillMax.x - MenuWidth) : PillMin.x;
        float MenuY = OpensAbove ? (PillMin.y - MenuOffset - MenuHeight) : (PillMax.y + MenuOffset);

        // 📝 Clamp into the viewport so a band pill near an edge still shows its whole card.
        const ImGuiViewport* Viewport = ImGui::GetMainViewport();
        const float LeftBound   = Viewport->WorkPos.x;
        const float RightBound  = Viewport->WorkPos.x + Viewport->WorkSize.x;
        const float TopBound    = Viewport->WorkPos.y;
        const float BottomBound = Viewport->WorkPos.y + Viewport->WorkSize.y;
        if (MenuX + MenuWidth > RightBound)  { MenuX = RightBound - MenuWidth; }
        if (MenuX < LeftBound)               { MenuX = LeftBound; }
        if (MenuY + MenuHeight > BottomBound){ MenuY = BottomBound - MenuHeight; }
        if (MenuY < TopBound)                { MenuY = TopBound; }

        ImGui::SetNextWindowPos(ImVec2(MenuX, MenuY));
        ImGui::SetNextWindowSize(ImVec2(MenuWidth, 0.0f));
        // 📝 Borderless menu: zero border size + a transparent border colour, so no hairline rings the card.
        ImGui::PushStyleColor(ImGuiCol_PopupBg,     Theme.Palette.PanelBackground);
        ImGui::PushStyleColor(ImGuiCol_Border,      IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(MenuPadding, MenuPadding));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  MenuRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,     ImVec2(0.0f, 0.0f));

        if (ImGui::BeginPopup(PopupIdentifier))
        {
            ImDrawList* MenuDraw = ImGui::GetWindowDrawList();
            const float AvailableSpan = ImGui::GetContentRegionAvail().x;

            // -- Menu header band (`.gp-head`) -------------------------------------------------------------------------
            if (Descriptor.MenuTitle != nullptr)
            {
                const ImVec2 HeadMin = ImGui::GetCursorScreenPos();
                const ImVec2 TitleSize = ImGui::CalcTextSize(Descriptor.MenuTitle);
                MenuDraw->AddText(ImVec2(HeadMin.x + ItemTextPadding, HeadMin.y + (HeaderHeight - TitleSize.y) * 0.5f),
                                  Theme.Palette.TextPrimary, Descriptor.MenuTitle);
                ImGui::Dummy(ImVec2(AvailableSpan, HeaderHeight));
            }

            // -- The non-collapsing card the rows live in (`.gp-card`) -------------------------------------------------
            const ImVec2 CardMin = ImGui::GetCursorScreenPos();
            float CardHeight = MenuPadding * 2.0f;
            for (int Index = 0; Index < Descriptor.ItemCount; ++Index)
            {
                if (Descriptor.Items[Index].GroupCaption != nullptr) { CardHeight += GroupCaptionHeight; }
                if (Descriptor.Items[Index].SeparatorPreceding)      { CardHeight += SeparatorHeight; }
                CardHeight += ItemHeight;
            }
            MenuDraw->AddRectFilled(CardMin, ImVec2(CardMin.x + AvailableSpan, CardMin.y + CardHeight),
                                    Theme.Palette.DeskBackground, CardRounding);

            ImGui::Dummy(ImVec2(AvailableSpan, MenuPadding));

            const float RowSpan = AvailableSpan - MenuPadding * 2.0f;
            for (int Index = 0; Index < Descriptor.ItemCount; ++Index)
            {
                const MenuPillItemDescriptor& Item = Descriptor.Items[Index];

                // 📝 Group caption (`.gp-group`) — small uppercase run introducing the rows beneath it.
                if (Item.GroupCaption != nullptr)
                {
                    const ImVec2 CaptionMin = ImGui::GetCursorScreenPos();
                    const ImVec2 CaptionSize = ImGui::CalcTextSize(Item.GroupCaption);
                    MenuDraw->AddText(ImVec2(CaptionMin.x + MenuPadding + ItemTextPadding,
                                             CaptionMin.y + (GroupCaptionHeight - CaptionSize.y) * 0.5f),
                                      Theme.Palette.TextMuted, Item.GroupCaption);
                    ImGui::Dummy(ImVec2(AvailableSpan, GroupCaptionHeight));
                }

                // 📝 Hairline rule (`.gp-sep`) between logical blocks of rows.
                if (Item.SeparatorPreceding)
                {
                    const ImVec2 RuleMin = ImGui::GetCursorScreenPos();
                    const float  RuleY   = RuleMin.y + SeparatorHeight * 0.5f;
                    MenuDraw->AddLine(ImVec2(RuleMin.x + MenuPadding * 2.0f, RuleY),
                                      ImVec2(RuleMin.x + AvailableSpan - MenuPadding * 2.0f, RuleY),
                                      Theme.Palette.PanelBorder);
                    ImGui::Dummy(ImVec2(AvailableSpan, SeparatorHeight));
                }

                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + MenuPadding);
                const ImVec2 RowMin = ImGui::GetCursorScreenPos();

                ImGui::PushID(Index);
                const bool RowClicked = ImGui::InvisibleButton("##row", ImVec2(RowSpan, ItemHeight));
                const bool RowHovered = ImGui::IsItemHovered();
                ImGui::PopID();

                const ImVec2 RowMax(RowMin.x + RowSpan, RowMin.y + ItemHeight);
                if (RowHovered)
                {
                    MenuDraw->AddRectFilled(RowMin, RowMax, Theme.Palette.ControlHovered, ItemRounding);
                }

                // 📝 Checkbox (`.gp-chk`): an outlined square that fills with the accent when marked, carrying a small
                //    knob-toned core. An action row instead reserves the same indent with no box (`.gp-noindent`).
                const ImVec2 BoxMin(RowMin.x + ItemTextPadding, (RowMin.y + RowMax.y) * 0.5f - CheckBoxEdge * 0.5f);
                const ImVec2 BoxMax(BoxMin.x + CheckBoxEdge, BoxMin.y + CheckBoxEdge);
                if (Item.CheckmarkEnabled)
                {
                    if (Item.Marked)
                    {
                        MenuDraw->AddRectFilled(BoxMin, BoxMax, Theme.Palette.SelectionMarker, 4.0f);
                        MenuDraw->AddRectFilled(ImVec2(BoxMin.x + 4.0f, BoxMin.y + 4.0f),
                                                ImVec2(BoxMax.x - 4.0f, BoxMax.y - 4.0f),
                                                Theme.Palette.SliderKnob, 1.0f);
                    }
                    else
                    {
                        MenuDraw->AddRect(BoxMin, BoxMax, Theme.Palette.PanelBorder, 4.0f, 0, 1.5f);
                    }
                }

                if (Item.Label != nullptr)
                {
                    const ImVec2 LabelSize = ImGui::CalcTextSize(Item.Label);
                    const ImU32  LabelTint = RowHovered ? Theme.Palette.TextPrimary : Theme.Palette.TextMuted;
                    MenuDraw->AddText(ImVec2(BoxMax.x + CheckBoxGap, (RowMin.y + RowMax.y) * 0.5f - LabelSize.y * 0.5f),
                                      LabelTint, Item.Label);
                }

                // 📝 Keystroke chip (`.gp-key`) pinned to the right edge of the row.
                if (Item.KeyHint != nullptr)
                {
                    const ImVec2 HintSize = ImGui::CalcTextSize(Item.KeyHint);
                    const ImVec2 ChipMax(RowMax.x - ItemTextPadding, (RowMin.y + RowMax.y) * 0.5f + HintSize.y * 0.5f + 2.0f);
                    const ImVec2 ChipMin(ChipMax.x - HintSize.x - KeyChipPaddingX * 2.0f, ChipMax.y - HintSize.y - 4.0f);
                    MenuDraw->AddRectFilled(ChipMin, ChipMax, Theme.Palette.ValueNumberSegment, 5.0f);
                    MenuDraw->AddText(ImVec2(ChipMin.x + KeyChipPaddingX, ChipMin.y + 2.0f),
                                      Theme.Palette.TextMuted, Item.KeyHint);
                }

                if (RowClicked)
                {
                    Result.ActivatedIndex = Index;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(2);
    }

    // 📝 Leave the cursor just past the pill so a band can keep laying widgets out on the same line.
    ImGui::SetCursorScreenPos(ImVec2(PillMax.x, Origin.y));
    ImGui::PopID();

    return Result;
}

}   // namespace Frontier
