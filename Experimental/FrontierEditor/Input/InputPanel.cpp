/*==========================================================================================================================================
                                                               INPUTPANEL.CPP
==========================================================================================================================================*/
// 🧩 Input workspace panel: live action/axis readings, press-to-assign capture, conflicts, TOML persistence.

#include "Input/InputPanel.h"

#include "Engine/Interface/ConfigurationStructure.h"
#include "Engine/Interface/StackRailControls.h"
#include "Input/InputMapCodec.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

namespace
{


void SectionCaption(const KitFaces& Faces, const EditorTheme& Palette, const char* Text)
{
    ImDrawList*  Ink = ImGui::GetWindowDrawList();
    const ImVec2 At  = ImGui::GetCursorScreenPos();
    KitDrawTracked(Ink, Faces.Medium, 10.5f, ImVec2(At.x + 2.0f, At.y + 6.0f), Palette.Faint, Text, 10.5f * 0.13f);
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 24.0f));
}

void Lamp(const EditorTheme& Palette, bool Lit, const char* Label)
{
    ImDrawList*  Ink = ImGui::GetWindowDrawList();
    const ImVec2 At  = ImGui::GetCursorScreenPos();
    Ink->AddCircleFilled(ImVec2(At.x + 5.0f, At.y + 7.0f), 5.0f, Lit ? Palette.Accent : Palette.Surface3);
    ImGui::Dummy(ImVec2(14.0f, 14.0f));
    ImGui::SameLine();
    ImGui::TextUnformatted(Label);
}

} // namespace

//----------------------------------------------------------------------------------------------------------------------
//                                                    INTERFACE RECORDING
//----------------------------------------------------------------------------------------------------------------------

void InputPanel::ConstructInterface()
{
    const KitFaces Faces = ResolveKitFaces();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));

    ServiceCapture();

    // Context -----------------------------------------------------------------------------------------
    SectionCaption(Faces, Palette, "CONTEXT");
    const InputContextCategory Live = Map.QueryContext();
    {
        ImDrawList*  Ink    = ImGui::GetWindowDrawList();
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const bool   Sim    = Live == InputContextCategory::Simulation;
        KitPillSurface(Ink, Origin, ImVec2(Origin.x + 150.0f, Origin.y + 24.0f), Sim ? Palette.AccentSoft : Palette.Surface2,
                       Sim ? Palette.Accent : Palette.Line, 999.0f);
        KitDrawTracked(Ink, Faces.Medium, 10.5f, ImVec2(Origin.x + 14.0f, Origin.y + 6.0f),
                       Sim ? Palette.Accent : Palette.Muted, InputContextName(Live), 10.5f * 0.13f);
        ImGui::Dummy(ImVec2(150.0f, 24.0f));
    }
    ImGui::SameLine();
    if (Live == InputContextCategory::Editor)
    {
        if (ImGui::Button("Capture simulation (F5)") && ConfigureSimulationCaptured)
        {
            ConfigureSimulationCaptured(true);
        }
    }
    else if (ImGui::Button("Release to editor (Esc)") && ConfigureSimulationCaptured)
    {
        ConfigureSimulationCaptured(false);
    }
    Lamp(Palette, Map.QueryPointerCaptured(), "Pointer captured (look axes live)");
    Lamp(Palette, Map.QueryTextEntryActive(), "Text entry active (editor shortcuts suppressed)");

    // Live readings -----------------------------------------------------------------------------------
    SectionCaption(Faces, Palette, "LIVE ACTIONS");
    {
        std::vector<std::string> Shown;
        for (const KeyAssignment& Assignment : Map.QueryAssignments())
        {
            if (std::find(Shown.begin(), Shown.end(), Assignment.Action) != Shown.end())
            {
                continue;
            }
            Shown.push_back(Assignment.Action);
            const ActionReading Reading = Map.QueryAction(Assignment.Action);
            Lamp(Palette, Reading.Down, Assignment.Action.c_str());
            ImGui::SameLine(std::max(150.0f, ImGui::GetWindowContentRegionMax().x * 0.62f));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Palette.Faint), "%s%s",
                               Assignment.Context == Live ? "" : "(inactive context) ",
                               Reading.Pressed ? "pressed" : Reading.Released ? "released" : "");
        }
    }

    SectionCaption(Faces, Palette, "LIVE AXES");
    {
        std::vector<std::string> Shown;
        for (const AxisAssignment& Assignment : Map.QueryAxisAssignments())
        {
            if (std::find(Shown.begin(), Shown.end(), Assignment.Axis) != Shown.end())
            {
                continue;
            }
            Shown.push_back(Assignment.Axis);
            const float  Reading = Map.QueryAxis(Assignment.Axis);
            ImDrawList*  Ink     = ImGui::GetWindowDrawList();
            const ImVec2 At      = ImGui::GetCursorScreenPos();
            const float  BarX    = At.x + 150.0f;
            const float  BarW    = 140.0f;
            Ink->AddRectFilled(ImVec2(BarX, At.y + 3.0f), ImVec2(BarX + BarW, At.y + 13.0f), Palette.Surface3, 3.0f);
            const float CentreX = BarX + BarW * 0.5f;
            const float Edge    = CentreX + std::clamp(Reading, -1.0f, 1.0f) * BarW * 0.5f;
            Ink->AddRectFilled(ImVec2(std::min(CentreX, Edge), At.y + 3.0f), ImVec2(std::max(CentreX, Edge), At.y + 13.0f),
                               Palette.Accent, 3.0f);
            Ink->AddLine(ImVec2(CentreX, At.y + 1.0f), ImVec2(CentreX, At.y + 15.0f), Palette.Line, 1.0f);
            char Value[32];
            std::snprintf(Value, sizeof(Value), "%+.2f", static_cast<double>(Reading));
            Ink->AddText(ImVec2(BarX + BarW + 8.0f, At.y), Palette.Muted, Value);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Palette.Faint), "%s", Assignment.Axis.c_str());
            ImGui::Dummy(ImVec2(1.0f, 2.0f));
        }
    }

    // Assignments -------------------------------------------------------------------------------------
    SectionCaption(Faces, Palette, "ASSIGNMENTS / PRESS-TO-ASSIGN");
    if (!CaptureAction.empty())
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Palette.Accent), "Press a key for '%s' (Esc cancels)...",
                           CaptureAction.c_str());
    }
    int RowIndex = 0;
    for (const KeyAssignment& Assignment : Map.QueryAssignments())
    {
        ImGui::PushID(RowIndex++);
        // Columns resolve against the docked width so the panel stays usable in narrow columns.
        const float RowWidth = ImGui::GetWindowContentRegionMax().x;
        ImGui::TextUnformatted(Assignment.Action.c_str());
        ImGui::SameLine(RowWidth * 0.52f);
        const std::string Source = Assignment.Chord.Key != InputKeyCategory::None
                                       ? InputChordTable::QueryChordLabel(Assignment.Chord)
                                       : std::string("Pointer ") + PointerButtonName(Assignment.PointerButton);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Palette.Muted), "%s", Source.c_str());
        ImGui::SameLine(RowWidth * 0.70f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Palette.Faint), "%s",
                           InputContextName(Assignment.Context));
        ImGui::SameLine(std::max(RowWidth - 66.0f, RowWidth * 0.70f + 56.0f));
        if (Assignment.Chord.Key != InputKeyCategory::None && ImGui::SmallButton("Reassign"))
        {
            CaptureAction  = Assignment.Action;
            CaptureContext = Assignment.Context;
            StatusLine     = "Capturing next key press...";
        }
        ImGui::PopID();
    }

    // Conflicts ---------------------------------------------------------------------------------------
    SectionCaption(Faces, Palette, "CONFLICTS");
    if (Map.QueryConflicts().empty())
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Palette.Faint), "None recorded this session.");
    }
    for (const std::string& Conflict : Map.QueryConflicts())
    {
        ImGui::TextWrapped("%s", Conflict.c_str());
    }

    // Persistence -------------------------------------------------------------------------------------
    SectionCaption(Faces, Palette, "PERSISTENCE / TOML");
    ImGui::TextWrapped("%s", PersistencePath.empty() ? "(no path configured)" : PersistencePath.string().c_str());
    std::string Diagnostic;
    if (ImGui::Button("Save assignments") && !PersistencePath.empty())
    {
        StatusLine = InputMapCodec::Encode(PersistencePath, Map, Diagnostic) ? "Assignments saved." : Diagnostic;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load into fresh map") && !PersistencePath.empty())
    {
        InputMap Restored;
        if (InputMapCodec::Decode(PersistencePath, Restored, Diagnostic))
        {
            const size_t Actions = Restored.QueryAssignments().size();
            const size_t Axes    = Restored.QueryAxisAssignments().size();
            StatusLine           = "Decoded " + std::to_string(Actions) + " action + " + std::to_string(Axes) +
                                   " axis assignments (verification read; live map unchanged).";
        }
        else
        {
            StatusLine = Diagnostic;
        }
    }
    if (!StatusLine.empty())
    {
        ImGui::TextWrapped("%s", StatusLine.c_str());
    }

    ImGui::PopStyleVar();
}

void InputPanel::ServiceCapture()
{
    if (CaptureAction.empty() || Observed == nullptr)
    {
        return;
    }
    const std::optional<InputKeyCategory> Pressed = Observed->QueryFirstPressedKey();
    if (!Pressed.has_value())
    {
        return;
    }
    if (*Pressed == InputKeyCategory::Escape)
    {
        StatusLine = "Reassignment cancelled.";
        CaptureAction.clear();
        return;
    }
    const InputModifiers Modifiers = Observed->QueryModifiers();
    InputKeyChord        Chord;
    Chord.Key     = *Pressed;
    Chord.Control = Modifiers.Control;
    Chord.Alt     = Modifiers.Alt;
    Chord.Shift   = Modifiers.Shift;
    Chord.Super   = Modifiers.Super;
    std::string Diagnostic;
    if (Map.ReassignAction(CaptureAction, CaptureContext, Chord, Diagnostic))
    {
        StatusLine = "Reassigned '" + CaptureAction + "' to " + InputChordTable::QueryChordLabel(Chord) + ".";
    }
    else
    {
        StatusLine = Diagnostic;
    }
    CaptureAction.clear();
}

} // namespace Frontier
