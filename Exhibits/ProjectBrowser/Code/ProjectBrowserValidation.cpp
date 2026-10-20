/*==========================================================================================================================================
                                                       PROJECTBROWSERVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Exercise real blank-project creation, validated hand-off and native browser draw commands without GPU claims.

#include "UI/Editor/ProjectBrowserPanel.h"
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool Condition, const char *Description)
{
    if (!Condition) throw std::runtime_error(Description);
}

std::string ReadText(const std::filesystem::path &Path)
{
    std::ifstream Stream(Path, std::ios::binary);
    return {std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>()};
}
} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount == 3, "Expected blank template and scratch output directory");
        const std::filesystem::path Template(Arguments[1]);
        const auto Run = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto Output = std::filesystem::path(Arguments[2]) / ("Run-" + Run);
        Require(!std::filesystem::exists(Output), "Use a fresh output directory; validation never deletes existing files");
        std::filesystem::create_directories(Output);
        const auto OriginalTemplate = ReadText(Template);
        ProjectBrowserPanel Browser;
        const auto Presets = Browser.QueryPresets();
        Require(Presets.size() == 1 && std::string(Presets[0]) == "Blank Project", "Only Blank Project is permitted");
        Require(!Browser.RequestOpenProject(), "Empty selection must not launch");
        Require(Browser.ConfigureDirectory(Output), "Arbitrary folder must open");
        const auto Created = Output / "Unicode-\xc3\xa9" / "MyProject.projectspace";
        Require(Browser.CreateProject(Created, "My Project"), "Blank project creation failed");
        Require(std::filesystem::is_directory(Created.parent_path() / "Content"), "Content root missing");
        ProjectSpaceDocument Project;
        std::string Diagnostic;
        Require(ProjectSpaceCodec::Decode(Created, Project, Diagnostic), "Created project must decode");
        Require(Project.Sections["project"]["name"].value_or(std::string{}) == "My Project", "Project name lost");
        Require(Project.Sections["project"]["identity"].value_or(std::string{}) != "engine-blank-template-v1", "Identity must be fresh");
        Require(Project.Sections["levels"].as_array()->empty(), "Blank preset must not insert sample levels");
        Require(Project.Sections["catalogue"].as_array()->empty(), "Blank preset must not insert sample content");
        Require(Project.AppearancePresent && Project.InputPresent && Project.NotificationsPresent, "Preset defaults not preserved");
        const auto OriginalProject = ReadText(Created);
        Require(!Browser.CreateProject(Created, "Overwrite"), "Existing projects must not be overwritten");
        Require(ReadText(Created) == OriginalProject, "Overwrite refusal changed project bytes");
        Require(!Browser.CreateProject(Output / "Wrong.project", "Wrong"), "Unimplemented project format must be refused");
        Require(!Browser.SelectProject(Output / "Missing.projectspace"), "Missing project must be refused");
        const auto Corrupt = Output / "Corrupt.projectspace";
        std::ofstream(Corrupt) << "not a project container";
        Require(!Browser.SelectProject(Corrupt), "Corrupt project must be refused");
        Require(Browser.SelectProject(Created) && Browser.RequestOpenProject(), "Valid selection must hand off");
        Require(Browser.ConsumeLaunchPath() == Created && Browser.ConsumeLaunchPath().empty(), "Hand-off must be consumed once");
        std::ofstream(Created, std::ios::trunc | std::ios::binary) << "invalid after selection";
        Require(!Browser.RequestOpenProject(), "Changed project must be revalidated before launch");
        std::ofstream(Created, std::ios::trunc | std::ios::binary) << OriginalProject;
        Require(Browser.SelectProject(Created), "Restored project must select");
        ImGui::CreateContext();
        auto &IO = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.DeltaTime = 1.0f / 60.0f;
        IO.Fonts->AddFontDefault();
        unsigned char *Pixels;
        int AtlasWidth, AtlasHeight;
        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
        for (const auto Size : {ImVec2(1280, 900), ImVec2(390, 844)})
        {
            IO.DisplaySize = Size;
            for (int Iteration = 0; Iteration < 3; ++Iteration)
            {
                ImGui::NewFrame();
                Browser.ConstructInterface();
                ImGui::Render();
                Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Native browser generated no draw geometry");
                std::vector<ImGuiWindow *> Columns;
                const auto *Root = ImGui::FindWindowByName("Project browser");
                for (auto *Window : ImGui::GetCurrentContext()->Windows)
                    if (Window->ParentWindow == Root && (Window->Flags & ImGuiWindowFlags_ChildWindow))
                        Columns.push_back(Window);
                Require(Columns.size() == 3, "Expected exactly three native columns");
                std::sort(Columns.begin(), Columns.end(), [](auto *First, auto *Second) { return First->Pos.x < Second->Pos.x; });
                Require(std::abs(Columns[0]->Size.x - (Size.x < 760 ? 44 : 204)) < 1, "Left column width changed");
                Require(std::abs(Columns[2]->Size.x - (Size.x < 760 ? 44 : 302)) < 1, "Right column width changed");
                Require(Columns[2]->Pos.x + Columns[2]->Size.x <= Size.x + 1, "Native columns overflow viewport");
                for (auto *Column : Columns)
                {
                    Require(Column->DC.ChildWindows.Size == 2, "Each column requires one header and one content region");
                    Require(Column->DC.ChildWindows[0]->Size.y == 64, "Native column headers must align at 64px");
                }
            }
        }
        Browser.ResetSelection();
        IO.DisplaySize = ImVec2(1280, 900);
        ImGui::NewFrame();
        Browser.ConstructInterface();
        ImGui::Render();
        Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Empty catalogue and Blank Project must draw");
        ImGui::DestroyContext();
        Require(ReadText(Template) == OriginalTemplate, "Installed blank template must remain unchanged");
        Require(ReadText(Created) == OriginalProject, "Browsing must not rewrite project bytes");
        std::ofstream(Output / "NativeBrowserChecks.json")
            << "{\n  \"result\": \"passed\",\n  \"presets\": [\"Blank Project\"],\n"
            << "  \"checks\": [\"Real creation and fresh identity\", \"Empty content and preserved defaults\", "
            << "\"Unicode directory\", \"Overwrite and unsupported format refusal\", "
            << "\"Missing/corrupt/changed project refusal\", \"Single-consumption hand-off\", "
            << "\"Desktop/narrow ImGui draw commands and measured aligned columns\", \"Read-only browsing\"],\n"
            << "  \"execution\": \"Host CPU checks only; no Windows, OS dialog or GPU execution\"\n}\n";
        std::cout << "PASS: Blank-only preset, real creation, safe refusal, revalidated hand-off, desktop/narrow native draw commands\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
