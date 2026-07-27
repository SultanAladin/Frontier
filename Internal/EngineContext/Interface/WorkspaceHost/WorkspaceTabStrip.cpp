/*==============================================================================================================================================
                                                              WORKSPACETABSTRIP.CPP
==============================================================================================================================================*/
// 🧩 Records one accent-tinted tab per workspace from the dock state. The active tab wears the accent fill; a click requests the switch. No
//    concrete workspace is named — the strip is pure iteration over WorkspaceDockState.

#include "WorkspaceTabStrip.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

int ConstructWorkspaceTabStrip(const ThemeConfiguration& Theme, WorkspaceDockState& State)
{
    int Clicked = -1;

    ImGui::PushID("##workspace-tabs");
    for (int Index = 0; Index < State.WorkspaceCount; ++Index)
    {
        const WorkspaceDescriptor& Workspace = State.Workspaces[Index];
        const bool Active = (Index == State.ActiveIndex);

        if (Index > 0)
        {
            ImGui::SameLine(0.0f, Theme.Metrics.ControlSpacing);
        }

        // 📝 Active tab wears the accent; inactive tabs sit on the control fill.
        if (Active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme.Palette.AccentPrimary);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme.Palette.TextOnAccent);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme.Palette.ControlBackground);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme.Palette.TextPrimary);
        }

        ImGui::PushID(Index);
        const char* Caption = Workspace.Title ? Workspace.Title : (Workspace.Identifier ? Workspace.Identifier : "?");
        if (ImGui::Button(Caption))
        {
            Clicked = Index;
            RequestWorkspaceActivation(State, Index);
        }
        ImGui::PopID();

        ImGui::PopStyleColor(2);
    }
    ImGui::PopID();

    return Clicked;
}

}   // namespace Frontier
