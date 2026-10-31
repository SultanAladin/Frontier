/*==========================================================================================================================================
                                                   ENVIRONMENTCONTROLS.CPP
==========================================================================================================================================*/
// 🧩 Reference inputs using the same pill slider as the Material Workspace.
#include "Experimental/EnvironmentInspectors/EnvironmentControls.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/PixelSpace.h"
#include "Engine/Interface/InspectorControls.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace Frontier::Experimental
{
DiagramKeys ReadDiagramKeys()
{
    DiagramKeys Result;
    if (!ImGui::IsItemFocused()) return Result;
    const ImGuiID Owner = ImGui::GetItemID();
    const ImGuiKey Keys[] = {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow,
                             ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End};
    for (ImGuiKey Key : Keys) ImGui::SetKeyOwner(Key, Owner, ImGuiInputFlags_LockThisFrame);
    auto Pressed = [Owner](ImGuiKey Key) { return ImGui::IsKeyPressed(Key, ImGuiInputFlags_Repeat, Owner); };
    Result.Horizontal = int(Pressed(ImGuiKey_RightArrow)) - int(Pressed(ImGuiKey_LeftArrow));
    Result.Vertical = int(Pressed(ImGuiKey_UpArrow)) - int(Pressed(ImGuiKey_DownArrow));
    Result.Home = Pressed(ImGuiKey_Home);
    Result.End = Pressed(ImGuiKey_End);
    Result.Step = ImGui::GetIO().KeyShift ? 10.f : 1.f;
    return Result;
}
bool AuthoringSlider(const char* Id, const char* Label, float& Value, float Minimum, float Maximum,
                     float Step, const char* Unit, const char* Format, float RequestedWidth)
{
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const float Width = std::max(1.0f, RequestedWidth > 0.0f ? RequestedWidth : ImGui::GetContentRegionAvail().x);
    const EditorTypefaces Faces = EditorTypefaces::Resolve();
    ImDrawList* Ink = ImGui::GetWindowDrawList();
    Ink->AddText(Faces.Regular, 11.0f, Origin, IM_COL32(179, 165, 138, 255), Label);
    ImGui::PushID(Id);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 76.0f, Origin.y - 3.0f));
    ImGui::SetNextItemWidth(54.0f);
    ImGui::PushFont(Faces.Light, 17.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    float Edited = Value;
    bool Changed = ImGui::InputFloat("##Value", &Edited, 0.0f, 0.0f, Format);
    ImGui::PopStyleColor(3); ImGui::PopStyleVar(); ImGui::PopFont();
    if (Changed && std::isfinite(Edited)) Value = std::clamp(Edited, Minimum, Maximum);
    else Changed = false;
    if (Unit && Unit[0]) Ink->AddText(Faces.Regular, 9.0f, ImVec2(Origin.x + Width - 16.0f, Origin.y + 4.0f),
                                      IM_COL32(160, 148, 120, 255), Unit);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 28.0f));
    ImGui::InvisibleButton("##Track", ImVec2(Width, 24.0f), ImGuiButtonFlags_EnableNav);
    const DiagramKeys Keys = ReadDiagramKeys();
    const ImGuiIO& Io = ImGui::GetIO();
    const bool Active = ImGui::IsItemActive();
    ControlPointer Pointer{Io.MousePos.x, Io.MousePos.y, Io.MouseDown[0], ImGui::IsItemActivated(),
                           ImGui::IsMouseReleased(0), Active || ImGui::IsItemHovered(), Io.MouseWheel};
    PixelSpace Surface;
    (void)Surface.Begin(SurfaceLayer::Window, Io.DisplaySize.x, Io.DisplaySize.y);
    const auto Saved = ControlKit::Palette();
    auto Palette = Saved;
    Palette.SliderTrack = {23.f / 255.f, 23.f / 255.f, 23.f / 255.f, 1.f};
    Palette.SliderFill = {69.f / 255.f, 69.f / 255.f, 69.f / 255.f, 1.f};
    Palette.SliderThumb = {215.f / 255.f, 215.f / 255.f, 206.f / 255.f, 1.f};
    ControlKit::ConfigurePalette(Palette);
    float Replacement = Value;
    (void)ControlKit::Slider(Surface, {Origin.x, Origin.y + 28.f, Origin.x + Width, Origin.y + 52.f},
                             Minimum, Maximum, Value, Active, Pointer, Replacement, ImGui::GetStyle().Alpha, 24.f, 22.f);
    ControlKit::ConfigurePalette(Saved);
    if (Active && Io.MouseDown[0])
    {
        if (Step > 0.f) Replacement = Minimum + std::round((Replacement - Minimum) / Step) * Step;
        Replacement = std::clamp(Replacement, Minimum, Maximum);
        if (std::fabs(Replacement - Value) > 1e-6f) { Value = Replacement; Changed = true; }
    }
    if (Keys.Any())
    {
        const float Increment = Step > 0.f ? Step : (Maximum - Minimum) / 100.f;
        const float Replacement = Keys.Home ? Minimum : Keys.End ? Maximum :
            std::clamp(Value + (Keys.Horizontal + Keys.Vertical) * Increment * Keys.Step, Minimum, Maximum);
        if (std::fabs(Replacement - Value) > 1e-6f) { Value = Replacement; Changed = true; }
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 60.f));
    ImGui::Dummy(ImVec2(Width, 0.f));
    return Changed;
}
}
