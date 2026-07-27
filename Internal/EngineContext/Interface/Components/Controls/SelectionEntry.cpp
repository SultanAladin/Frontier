/*==============================================================================================================================================
                                                              SELECTIONENTRY.CPP
==============================================================================================================================================*/
// 🧩 An enumerated choice drawn as a row of segmented pills (ControlsPreview.html): each option is a rounded chip — the selected one filled
//    white with dark text, the rest grey with muted text. Chips flow left-to-right and wrap onto new lines when the field is narrow. Stateless;
//    the selected index lives in the caller's struct. (For a compact popup instead of inline chips, use the Dropdown control.)

#include "SelectionEntry.h"

#include "ControlLayout.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ConstructSelectionEntry(const ThemeConfiguration& Theme, const SelectionEntryDescriptor& Descriptor)
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
    const float ChipH      = ResolvePillHeight(Theme) * 0.9f;
    const float ChipPadX   = 16.0f;
    const float ChipGap    = Theme.Metrics.ControlSpacing + 4.0f;
    const float Rounding   = ImGui::GetStyle().FrameRounding + 4.0f;

    const ImVec2 Origin  = ImGui::GetCursorScreenPos();
    ImDrawList*  Draw    = ImGui::GetWindowDrawList();

    float PenX     = Origin.x;
    float PenY     = Origin.y;
    float MaxRight = Origin.x;
    bool  Changed  = false;

    for (int Index = 0; Index < Descriptor.OptionCount; ++Index)
    {
        const char*  Text     = Descriptor.Options[Index];
        const ImVec2 TextSize = ImGui::CalcTextSize(Text);
        const float  ChipW    = TextSize.x + ChipPadX * 2.0f;

        // 📝 Wrap to a new line when this chip would overflow the field width.
        if (PenX + ChipW > Origin.x + FieldWidth + 0.5f && PenX > Origin.x + 0.5f)
        {
            PenX  = Origin.x;
            PenY += ChipH + ChipGap;
        }

        const ImVec2 ChipMin(PenX, PenY);
        const ImVec2 ChipMax(PenX + ChipW, PenY + ChipH);
        const bool   Selected = (Index == Current);

        const ImU32 Fill = Selected ? Theme.Palette.SliderKnob : Theme.Palette.ControlBackground;
        const ImU32 Ink  = Selected ? Theme.Palette.KnobText   : Theme.Palette.TextMuted;
        Draw->AddRectFilled(ChipMin, ChipMax, Fill, Rounding);

        const ImVec2 TextPos(ChipMin.x + (ChipW - TextSize.x) * 0.5f,
                             ChipMin.y + (ChipH - TextSize.y) * 0.5f);
        Draw->AddText(TextPos, Ink, Text);

        // 📝 Hit-test with an invisible button parked over the chip.
        ImGui::SetCursorScreenPos(ChipMin);
        ImGui::PushID(Index);
        if (ImGui::InvisibleButton("##chip", ImVec2(ChipW, ChipH)) && Descriptor.Enabled)
        {
            Descriptor.SelectedIndex[0] = Index;
            Changed = true;
        }
        if (!Selected && ImGui::IsItemHovered())
        {
            Draw->AddRect(ChipMin, ChipMax, Theme.Palette.ValueOutline, Rounding, ImDrawFlags_None, 1.0f);
        }
        ImGui::PopID();

        PenX    += ChipW + ChipGap;
        MaxRight = MaxRight > ChipMax.x ? MaxRight : ChipMax.x;
    }

    // 📝 Advance the cursor past the (possibly multi-line) chip block.
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, PenY + ChipH));
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
