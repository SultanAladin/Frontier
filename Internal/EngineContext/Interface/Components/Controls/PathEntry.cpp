/*==============================================================================================================================================
                                                                PATHENTRY.CPP
==============================================================================================================================================*/
// 🧩 A path row in the ControlsPreview.html style: a black rounded field pill hosting a REAL editable text input (double-click-select, text
//    cursor, mouse selection all work) plus a square browse button to its right. Platform-free — the caller opens the file dialogue when
//    BrowseRequested comes back true. Stateless: the buffer is caller-owned.

#include "PathEntry.h"

#include "ControlLayout.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

PathEntryResult ConstructPathEntry(const ThemeConfiguration& Theme, const PathEntryDescriptor& Descriptor)
{
    PathEntryResult Result = {};

    if (Descriptor.Buffer == nullptr || Descriptor.BufferCapacity <= 0)
    {
        return Result;
    }

    ImGui::PushID(Descriptor.Label);
    if (!Descriptor.Enabled)
    {
        ImGui::BeginDisabled();
    }

    const float FieldWidth = BeginControlRow(Theme, Descriptor.Label);
    const float Height     = ResolvePillHeight(Theme);
    const float Gap        = Theme.Metrics.ControlSpacing;
    const float ButtonW    = Height;                       // square browse cap
    const float TextW      = FieldWidth - ButtonW - Gap;

    const ImVec2 Origin = ImGui::GetCursorScreenPos();

    // -- Field pill hosting an editable text input ---------------------------------------------------------------------
    const ValuePillLayout Pill = DrawValuePill(Theme, Origin, ImVec2(TextW, Height), nullptr, nullptr, Descriptor.Enabled);

    ImGui::SetCursorScreenPos(Pill.NumberMin);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text,           Theme.Palette.ValueText);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, (Height - ImGui::GetFontSize()) * 0.5f));
    ImGui::SetNextItemWidth(TextW > 1.0f ? TextW : 1.0f);
    Result.TextChanged = ImGui::InputText("##path", Descriptor.Buffer, static_cast<size_t>(Descriptor.BufferCapacity));
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);

    // -- Browse cap ----------------------------------------------------------------------------------------------------
    const ImVec2 BtnMin(Origin.x + TextW + Gap, Origin.y);
    const ImVec2 BtnMax(BtnMin.x + ButtonW, BtnMin.y + Height);
    ImDrawList*  Draw = ImGui::GetWindowDrawList();
    const float  Rnd  = ImGui::GetStyle().FrameRounding + 3.0f;

    ImGui::SetCursorScreenPos(BtnMin);
    const bool Pressed = ImGui::InvisibleButton("##browse", ImVec2(ButtonW, Height));
    const ImU32 Fill   = ImGui::IsItemHovered() ? Theme.Palette.ControlActive : Theme.Palette.ControlBackground;
    Draw->AddRectFilled(BtnMin, BtnMax, Fill, Rnd);

    const ImVec2 Dots = ImGui::CalcTextSize("...");
    Draw->AddText(ImVec2(BtnMin.x + (ButtonW - Dots.x) * 0.5f, BtnMin.y + (Height - Dots.y) * 0.5f),
                  Theme.Palette.TextPrimary, "...");
    Result.BrowseRequested = Pressed;

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + Height));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    EndControlRow(Theme);

    if (!Descriptor.Enabled)
    {
        ImGui::EndDisabled();
        Result.TextChanged     = false;
        Result.BrowseRequested = false;
    }
    ImGui::PopID();

    return Result;
}

}   // namespace Frontier
