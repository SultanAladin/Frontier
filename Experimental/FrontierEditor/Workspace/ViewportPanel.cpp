/*==========================================================================================================================================
                                                             VIEWPORTPANEL.CPP
============================================================================================================================================*/
// 🧩 The scene canvas's docking home: fully blank for now except its header row; renderer arrives later.

#include "Workspace/ViewportPanel.h"

#include "Engine/Interface/StackRailControls.h"

#include <imgui.h>

#include <cstdio>

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                                    INTERFACE RECORDING
//----------------------------------------------------------------------------------------------------------------------

void ViewportPanel::ConstructInterface()
{
    const KitFaces Faces  = ResolveKitFaces();
    ImDrawList*    Ink    = ImGui::GetWindowDrawList();
    const ImVec2   Origin = ImGui::GetCursorScreenPos();
    const ImVec2   Avail  = ImGui::GetContentRegionAvail();

    // Blank surface in the Appearance theme; nothing else is drawn below the header row.
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Avail.x, Origin.y + Avail.y), Palette.Surface);

    // Header: record summary (left) + live input context (right) -------------------------------------
    char Stats[192];
    std::snprintf(Stats, sizeof(Stats), "%zu records · %s", RecordCount,
                  SelectedName.empty() ? "no selection" : SelectedName.c_str());
    KitDrawText(Ink, Faces.Regular, 11.0f, ImVec2(Origin.x + 12.0f, Origin.y + 10.0f), Palette.Muted, Stats);

    Ink->AddCircleFilled(ImVec2(Origin.x + Avail.x - 18.0f, Origin.y + 16.0f), 4.0f,
                         SimulationCaptured ? Palette.Accent : Palette.Faint);
    const char* Mode = SimulationCaptured ? "SIMULATION" : "EDITOR";
    KitDrawTracked(Ink, Faces.Medium, 10.0f, ImVec2(Origin.x + Avail.x - 118.0f, Origin.y + 10.0f),
                   SimulationCaptured ? Palette.Accent : Palette.Faint, Mode, 10.0f * 0.13f);

    // The whole blank surface is the (invisible) Simulation capture target.
    ImGui::SetCursorScreenPos(Origin);
    if (ImGui::InvisibleButton("##SceneCanvas", Avail) && RequestCaptureToggle)
    {
        RequestCaptureToggle();
    }
}

} // namespace Frontier
