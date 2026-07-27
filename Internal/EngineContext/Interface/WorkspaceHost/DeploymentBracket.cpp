/*==============================================================================================================================================
                                                              DEPLOYMENTBRACKET.CPP
==============================================================================================================================================*/
// 🧩 Stands up the full-viewport shell window and records the tab strip over the dock. This is the outermost UI seam: an application's cycle
//    resolves the theme, then calls this once. Everything visible traces down from here through the registered workspaces + panels.

#include "DeploymentBracket.h"

#include "WorkspaceTabStrip.h"

#include "../Theme/ThemeResolver.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructDeploymentBracket(const ThemeConfiguration& Theme, const DeploymentBracketDescriptor& Descriptor,
                                WorkspaceDockState& DockState)
{
    // 📝 Mirror the theme into ImGui's style so nested raw widgets inherit it, every cycle (cheap + keeps live theme edits honest).
    EnforceThemeStyle(Theme);

    // 📝 One full-viewport, chromeless window is the desk. It never moves, resizes, or scrolls — the dock owns all interior layout.
    const ImGuiViewport* Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(Viewport->WorkSize);

    const ImGuiWindowFlags ShellFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme.Palette.DeskBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##deployment-bracket", nullptr, ShellFlags))
    {
        // -- Title + tab strip on one line -------------------------------------------------------------------------------
        if (Descriptor.ApplicationTitle != nullptr)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme.Palette.TextPrimary);
            ImGui::TextUnformatted(Descriptor.ApplicationTitle);
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, Theme.Metrics.PanelPadding * 2.0f);
        }

        ConstructWorkspaceTabStrip(Theme, DockState);
        ImGui::Separator();

        // -- The four-region workspace dock ------------------------------------------------------------------------------
        ConstructWorkspaceDock(Theme, DockState);
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

}   // namespace Frontier
