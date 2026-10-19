/*============================================================================================================================================
                                                            VECTORAUTHORINGCONTROLS.CPP
============================================================================================================================================*/
// 🧩 Reuse the themed ControlKit switch/slider while matching the approved rounded vector field and disclosure surfaces.

#include "VectorAuthoringControls.h"
#include "GlyphSpace.h"
#include "DropdownControls.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <algorithm>

namespace Frontier
{
namespace
{
ImVec4 ConvertSpectrum(ColorQuad Spectrum)
{
    return {Spectrum.Red, Spectrum.Green, Spectrum.Blue, Spectrum.Alpha};
}
}

void VectorAuthoringControlSurface::Register(const char *Caption)
{
    if (!RegisterExtent) return;
    const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
    RegisterExtent(Caption, {Minimum.x, Minimum.y, Maximum.x, Maximum.y});
}

bool VectorAuthoringControlSurface::Button(
    const char       *Caption,
    float             Width,
    bool              Selected,
    std::string_view  Path,
    bool              Capsule)
{
    const auto &Palette = ControlKit::Palette();
    const float Height = ControlHeight * Scale;
    if (Width == 0) Width = std::max(Height, ImGui::CalcTextSize(Caption, ImGui::FindRenderedTextEnd(Caption)).x + 24 * Scale);
    const float Rounding = Capsule || !Path.empty() ? Height * .5f : std::min(6 * Scale, Height * .25f);
    const auto Position = ImGui::GetCursorScreenPos();
    const bool Pressed = ImGui::InvisibleButton(Caption, ImVec2(Width, Height));
    Register(Caption);
    const auto Colour = Selected ? Palette.Selected : ImGui::IsItemHovered() ? Palette.Raised : Palette.Inset;
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Position, ImVec2(Position.x + Width, Position.y + Height), ImGui::GetColorU32(ConvertSpectrum(Colour)), Rounding);
    Ink->AddRect(Position, ImVec2(Position.x + Width, Position.y + Height), ImGui::GetColorU32(ConvertSpectrum(Selected ? Palette.TextFaint : Palette.Stroke)), Rounding);
    if (!Path.empty())
    {
        PixelSpace Surface;
        (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
        GlyphSpace::Stroke(Surface, Path, {Position.x + (Width - 17 * Scale) * .5f, Position.y + 6 * Scale, 17 * Scale, 1.5f, Palette.TextDim});
    }
    else
    {
        const auto Extent = ImGui::CalcTextSize(Caption, ImGui::FindRenderedTextEnd(Caption));
        Ink->AddText(ImVec2(Position.x + (Width - Extent.x) * .5f, Position.y + (Height - Extent.y) * .5f), ImGui::GetColorU32(ConvertSpectrum(Palette.Text)), Caption, ImGui::FindRenderedTextEnd(Caption));
    }
    return Pressed;
}

void VectorAuthoringControlSurface::Tooltip(const char *Caption, const char *Description)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !ImGui::IsItemFocused()) return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * Scale, 10 * Scale));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ConvertSpectrum(ControlKit::Palette().Raised));
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240 * Scale);
    ImGui::TextUnformatted(Caption);
    ImGui::Spacing();
    ImGui::TextDisabled("%s", Description);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

bool VectorAuthoringControlSurface::Toggle(const char *Caption, bool &Enabled)
{
    const auto Position = ImGui::GetCursorScreenPos();
    const float Width = std::max(ImGui::GetContentRegionAvail().x, ImGui::CalcTextSize(Caption, nullptr, true).x + 47 * Scale);
    const bool Changed = ImGui::InvisibleButton(Caption, ImVec2(Width, 28 * Scale));
    Register(Caption);
    if (Changed) Enabled = !Enabled;
    PixelSpace Surface;
    const float SwitchScale = .66f * Scale;
    (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, SwitchScale);
    (void)ControlKit::Switch(Surface, Position.x / SwitchScale, (Position.y + 5 * Scale) / SwitchScale, Enabled, ControlPointer{}, ImGui::GetStyle().Alpha);
    ImGui::GetWindowDrawList()->AddText(ImVec2(Position.x + 47 * Scale, Position.y + 6 * Scale), ImGui::GetColorU32(ImGuiCol_Text), Caption, ImGui::FindRenderedTextEnd(Caption));
    return Changed;
}

bool VectorAuthoringControlSurface::Number(
    const char *Caption,
    float      &Quantity,
    float       Minimum,
    float       Maximum)
{
    ImGui::PushID(Caption);
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", Caption);
    ImGui::SetNextItemWidth(-1);
    const bool Changed = ImGui::InputFloat("##Quantity", &Quantity, 0, 0, "%.3g");
    Register(Caption);
    if (Changed) Quantity = std::isfinite(Quantity) ? std::clamp(Quantity, Minimum, Maximum) : Minimum;
    ImGui::EndGroup();
    ImGui::PopID();
    return Changed;
}

bool VectorAuthoringControlSurface::Range(
    const char *Caption,
    float      &Quantity,
    float       Minimum,
    float       Maximum)
{
    bool Changed = Number(Caption, Quantity, Minimum, Maximum);
    ImGui::PushID(Caption);
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = std::max(48.f, ImGui::GetContentRegionAvail().x);
    ImGui::InvisibleButton("Range", ImVec2(Width, 28 * Scale));
    const auto &Input = ImGui::GetIO();
    const bool Active = ImGui::IsItemActive();
    const ControlPointer Pointer{Input.MousePos.x, Input.MousePos.y, Input.MouseDown[0], ImGui::IsItemActivated(), ImGui::IsMouseReleased(0), Active || ImGui::IsItemHovered(), Input.MouseWheel};
    PixelSpace Surface;
    (void)Surface.Begin(SurfaceLayer::Window, Input.DisplaySize.x, Input.DisplaySize.y, 1);
    float Replacement = Quantity;
    (void)ControlKit::Slider(Surface, {Origin.x, Origin.y, Origin.x + Width, Origin.y + 28 * Scale}, Minimum, Maximum, Quantity, Active, Pointer, Replacement, ImGui::GetStyle().Alpha, 28 * Scale, 26 * Scale);
    if (Active && Replacement != Quantity) { Quantity = Replacement; Changed = true; }
    ImGui::PopID();
    return Changed;
}

bool VectorAuthoringControlSurface::Dropdown(const char *Caption, int &Selection, std::span<const char *const> Captions)
{
    ImGui::PushID(Caption);
    ImGui::TextDisabled("%s", Caption);
    ImGui::SetNextItemWidth(-1);
    bool Changed = false;
    if (Captions.empty()) { ImGui::PopID(); return false; }
    if (DropdownControlSurface::BeginSelection("##Selection", Captions[size_t(std::clamp(Selection, 0, int(Captions.size()) - 1))], Scale))
    {
        for (size_t Index = 0; Index < Captions.size(); ++Index)
        {
            if (DropdownControlSurface::Option(Captions[Index], Selection == int(Index), Scale))
            {
                Selection = int(Index);
                Changed = true;
            }
        }
        DropdownControlSurface::EndSelection();
    }
    Register(Caption);
    ImGui::PopID();
    return Changed;
}

bool VectorAuthoringControlSurface::Colour(const char *Caption, std::array<float, 4> &Spectrum)
{
    ImGui::TextDisabled("%s", Caption);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 18 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * Scale, 14 * Scale));
    const bool Changed = ImGui::ColorEdit4(Caption, Spectrum.data(), ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_PickerHueWheel);
    Register(Caption);
    ImGui::PopStyleVar(2);
    return Changed;
}

void VectorAuthoringControlSurface::BeginCard(const char *Caption, bool Raised, bool Subordinate)
{
    ImGui::PushID(Caption);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * Scale, 14 * Scale));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ConvertSpectrum(Raised ? ControlKit::Palette().Raised : Subordinate ? ControlKit::Palette().Panel : ControlKit::Palette().Inset));
    ImGui::BeginChild("Card", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextUnformatted(Caption);
    ImGui::Spacing();
}

void VectorAuthoringControlSurface::EndCard()
{
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, 10 * Scale));
}

bool VectorAuthoringControlSurface::Array(
    VectorAuthoringArraySpecification &Specification,
    size_t                     LayerCount,
    size_t                     SelectionCount,
    size_t                     Limit,
    bool                       Protected)
{
    ImGui::BeginDisabled(Protected);
    int Category = int(Specification.Category), Profile = int(Specification.ScaleProfile);
    const char *Layouts[]{"Linear", "Grid", "Circular / arc", "Radial rings"};
    if (Dropdown("Array layout", Category, Layouts))
    {
        Specification.Category = VectorAuthoringArrayCategory(Category);
        if (Category == 1 && Specification.StepY == 0) Specification.StepY = 80;
    }
    const auto Integral = [&](const char *Caption, uint32_t &Quantity, float Minimum, float Maximum)
    {
        float Replacement = float(Quantity);
        if (Number(Caption, Replacement, Minimum, Maximum)) Quantity = uint32_t(Replacement);
    };
    if (Category == 1)
    {
        Integral("Columns", Specification.Columns, 1, 8);
        Integral("Rows", Specification.Rows, 1, 8);
    }
    else Integral(Category >= 2 ? "Copies per ring" : "Copies", Specification.Count, 2, 64);
    if (Category >= 2)
    {
        Number("Radius", Specification.Radius, 0, 512);
        if (Category == 3)
        {
            Integral("Rings", Specification.Rings, 1, 8);
            Number("Ring spacing", Specification.RadiusStep, 1, 512);
        }
        Number("Centre X", Specification.CentreX, -512, 1024);
        Number("Centre Y", Specification.CentreY, -512, 1024);
        Number("Start angle", Specification.StartAngle, -360, 360);
        Number("Arc sweep", Specification.Sweep, -360, 360);
        Toggle("Rotate copies around centre", Specification.RotateCopies);
    }
    else
    {
        Number("Step X", Specification.StepX, -512, 512);
        Number("Step Y", Specification.StepY, -512, 512);
    }
    const char *Profiles[]{"Uniform", "Linear progression", "Circular wave", "Radial distance", "Seeded noise"};
    if (Dropdown("Scale profile", Profile, Profiles)) Specification.ScaleProfile = VectorAuthoringScaleProfile(Profile);
    Number("Start scale %", Specification.StartScale, 1, 400);
    if (Profile != 0) Number("End scale %", Specification.EndScale, 1, 400);
    if (Profile == 4) Integral("Noise seed", Specification.Seed, 0, 999999);
    ImGui::TextWrapped("Replaces the selection with editable copies, not a live modifier. Undo restores originals.");
    const auto Population = Category == 1 ? Specification.Columns * Specification.Rows : Specification.Count * (Category == 3 ? Specification.Rings : 1);
    const auto Total = LayerCount - SelectionCount + Population * SelectionCount;
    ImGui::TextDisabled("%zu / %zu document entries after generation", Total, Limit);
    ImGui::BeginDisabled(Total > Limit || Population < 2);
    const bool Generate = Button("Generate array", ImGui::GetContentRegionAvail().x);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    return Generate;
}
}
