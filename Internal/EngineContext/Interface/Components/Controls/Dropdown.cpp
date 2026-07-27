/*==============================================================================================================================================
                                                                DROPDOWN.CPP
==============================================================================================================================================*/
// 🧩 Draws the "[ current | ▾ ]" head pill and hosts the option list in an ImGui popup so the overlay + click-outside-to-close behaviour is
//    handled for us. Each item is custom-drawn to match ControlsPreview.html: a sharp lighter-grey hover fill with a full-height accent bar on
//    the left, and a filled radio on the currently-selected row. Stateless.

#include "Dropdown.h"

#include "ControlLayout.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ConstructDropdown(const ThemeConfiguration& Theme, const DropdownDescriptor& Descriptor)
{
    if (Descriptor.SelectedIndex == nullptr || Descriptor.Options == nullptr || Descriptor.OptionCount <= 0)
    {
        return false;
    }

    int Current = Descriptor.SelectedIndex[0];
    if (Current < 0 || Current >= Descriptor.OptionCount)
    {
        Current = 0;
    }

    ImGui::PushID(Descriptor.Label);
    if (!Descriptor.Enabled)
    {
        ImGui::BeginDisabled();
    }

    const float FieldWidth = BeginControlRow(Theme, Descriptor.Label);
    const float Height     = ResolvePillHeight(Theme);
    const ImVec2 Origin    = ImGui::GetCursorScreenPos();

    // -- Head pill: black name segment + grey caret cap --------------------------------------------------------------
    const ValuePillLayout Head = DrawValuePill(Theme, Origin, ImVec2(FieldWidth, Height),
                                               nullptr, ">", Descriptor.Enabled);
    // 📝 Current option text, left-aligned in the black segment.
    {
        ImDrawList*  Draw     = ImGui::GetWindowDrawList();
        const char*  Text     = Descriptor.Options[Current];
        const ImVec2 TextSize = ImGui::CalcTextSize(Text);
        const ImVec2 TextPos(Head.NumberMin.x + 14.0f, Head.NumberMin.y + (Height - TextSize.y) * 0.5f);
        Draw->AddText(TextPos, Theme.Palette.ValueText, Text);
    }

    ImGui::SetCursorScreenPos(Origin);
    const bool Pressed = ImGui::InvisibleButton("##head", ImVec2(FieldWidth, Height));
    if (Pressed && Descriptor.Enabled)
    {
        ImGui::OpenPopup("##ddlist");
    }

    // -- Popup list ----------------------------------------------------------------------------------------------------
    // 📝 Anchor the list to the head, but FLIP it above when there is not enough room below before the viewport's bottom edge (otherwise it
    //    would be clipped off the canvas). The height is derived from the item metrics used inside the popup so the fit test is exact.
    bool Changed = false;
    const float ItemH   = Height * 0.86f;
    const float WinPad  = 6.0f;
    const float ItemGap = 2.0f;
    const float PopupH  = WinPad * 2.0f + Descriptor.OptionCount * ItemH + (Descriptor.OptionCount - 1) * ItemGap;

    const ImGuiViewport* Viewport = ImGui::GetMainViewport();
    const float ViewBottom = Viewport->WorkPos.y + Viewport->WorkSize.y;
    const float ViewTop    = Viewport->WorkPos.y;
    const float BelowY     = Origin.y + Height + 6.0f;              // preferred: just under the head
    const float AboveY     = Origin.y - 6.0f - PopupH;             // flipped: just over the head
    const bool  FitsBelow  = (BelowY + PopupH) <= ViewBottom;
    const bool  FitsAbove  = AboveY >= ViewTop;
    // Prefer below; flip above only when below would clip AND above actually fits. If neither fits, clamp below to the viewport bottom.
    float PopupY;
    if (FitsBelow || !FitsAbove)
    {
        PopupY = FitsBelow ? BelowY : (ViewBottom - PopupH);
        if (PopupY < ViewTop) { PopupY = ViewTop; }
    }
    else
    {
        PopupY = AboveY;
    }

    ImGui::SetNextWindowPos(ImVec2(Origin.x, PopupY));
    ImGui::SetNextWindowSize(ImVec2(FieldWidth, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme.Palette.PanelHeader);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(WinPad, WinPad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0.0f, ItemGap));

    if (ImGui::BeginPopup("##ddlist"))
    {
        ImDrawList* Draw    = ImGui::GetWindowDrawList();
        const float AvailW  = ImGui::GetContentRegionAvail().x;

        for (int Index = 0; Index < Descriptor.OptionCount; ++Index)
        {
            const bool   Selected = (Index == Current);
            const ImVec2 ItemMin  = ImGui::GetCursorScreenPos();
            const ImVec2 ItemMax(ItemMin.x + AvailW, ItemMin.y + ItemH);

            ImGui::PushID(Index);
            const bool Clicked = ImGui::InvisibleButton("##item", ImVec2(AvailW, ItemH));
            const bool Hovered = ImGui::IsItemHovered();
            ImGui::PopID();

            // 📝 Hover = sharp lighter-grey fill + a full-height BLUE accent bar on the left (ControlsPreview .dd-item:hover::before,
            //    width:4px, background:var(--accent) #4a90e2). No rounding, per the reference.
            if (Hovered)
            {
                Draw->AddRectFilled(ItemMin, ItemMax, Theme.Palette.ControlActive, 0.0f);
                Draw->AddRectFilled(ItemMin, ImVec2(ItemMin.x + 4.0f, ItemMax.y), Theme.Palette.SelectionMarker, 0.0f);
            }

            const char*  Text     = Descriptor.Options[Index];
            const ImVec2 TextSize = ImGui::CalcTextSize(Text);
            const float  IndentX  = Hovered ? 20.0f : 14.0f;   // .dd-item:hover padding-left:20px (else 14px)
            Draw->AddText(ImVec2(ItemMin.x + IndentX, ItemMin.y + (ItemH - TextSize.y) * 0.5f),
                          Theme.Palette.TextPrimary, Text);

            // 📝 Dot toggle (ControlsPreview .dd-item .radio): a 20px circle on EVERY row. The unselected rows show a faint grey
            //    ring; the CURRENT row shows a blue --accent ring with a filled blue centre (.dd-item.sel .radio + ::after).
            const float  RadioR = 8.0f;   // 20px diameter → ~8px radius (the ::after fill sits at inset:4px → ~4px radius)
            const ImVec2 RadioC(ItemMax.x - 14.0f - RadioR, (ItemMin.y + ItemMax.y) * 0.5f);
            if (Selected)
            {
                Draw->AddCircle(RadioC, RadioR, Theme.Palette.SelectionMarker, 20, 2.0f);
                Draw->AddCircleFilled(RadioC, RadioR - 4.0f, Theme.Palette.SelectionMarker, 20);
            }
            else
            {
                Draw->AddCircle(RadioC, RadioR, Theme.Palette.PanelBorder, 20, 1.5f);
            }

            if (Clicked)
            {
                Descriptor.SelectedIndex[0] = Index;
                Changed = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + Height));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    EndControlRow(Theme);

    if (!Descriptor.Enabled)
    {
        ImGui::EndDisabled();
    }
    ImGui::PopID();

    return Changed && Descriptor.Enabled;
}

}   // namespace Frontier
