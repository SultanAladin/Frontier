/*==========================================================================================================================================
                                                         PROJECTBROWSERPANEL.CPP
==========================================================================================================================================*/
// 🧩 Native three-column startup browser with real project selection and a single blank-project creation route.

#include "ProjectBrowserPanel.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <imgui.h>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     PROJECT OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

ProjectBrowserPanel::ProjectBrowserPanel(ProjectPathSelection Selection) : PickProjectPath(std::move(Selection))
{
    std::snprintf(NameText.data(), NameText.size(), "%s", "Untitled");
    std::error_code Failure;
    const auto Current = std::filesystem::current_path(Failure);
    if (!Failure) ConfigureDirectory(Current);
}

bool ProjectBrowserPanel::ConfigureDirectory(const std::filesystem::path &Requested)
{
    if (!Directory.ConfigureRoot(Requested))
    {
        Diagnostic = Directory.QueryDiagnostic();
        return false;
    }
    const auto Text = EncodeContentPath(Directory.QueryDirectory());
    std::snprintf(DirectoryText.data(), DirectoryText.size(), "%s", Text.c_str());
    Diagnostic.clear();
    return true;
}

bool ProjectBrowserPanel::SelectProject(const std::filesystem::path &Path)
{
    ProjectSpaceDocument Candidate;
    if (!ProjectSpaceCodec::Decode(Path, Candidate, Diagnostic)) return false;
    SelectedPath = Path;
    SelectedProject = std::move(Candidate);
    Diagnostic.clear();
    return true;
}

bool ProjectBrowserPanel::CreateProject(const std::filesystem::path &Destination, const std::string &Name)
{
    const auto Template = ResolveContentDirectory("EngineContent/Defaults") / "BlankProject/Blank.projectspace";
    if (!ProjectSpaceCodec::CreateBlank(Template, Destination, Name, Diagnostic)) return false;
    const auto Folder = Destination.has_parent_path() ? Destination.parent_path() : std::filesystem::path(".");
    (void)ConfigureDirectory(Folder);
    return SelectProject(Destination);
}

bool ProjectBrowserPanel::RequestOpenProject()
{
    if (SelectedPath.empty())
    {
        Diagnostic = "Select a project first.";
        return false;
    }
    // 📝 Revalidate immediately before hand-off; selection may outlive a file edit or removal.
    if (!SelectProject(SelectedPath)) return false;
    LaunchPath = SelectedPath;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     COLUMN CONTENT
//------------------------------------------------------------------------------------------------------------------------

void ProjectBrowserPanel::ConstructCatalogue()
{
    if (!SelectedPath.empty())
    {
        if (ImGui::Button("Back to files")) ResetSelection();
        ImGui::Spacing();
        const auto Name = SelectedProject.Sections["project"]["name"].value_or(std::string("Project"));
        ImGui::TextUnformatted(Name.c_str());
        ImGui::TextWrapped("Project settings are ready. Native scene and document rendering are not connected yet.");
        ImGui::Spacing();
        if (ImGui::Button("Open in Frontier")) RequestOpenProject();
        return;
    }
    ImGui::TextUnformatted(PresetsOnly ? "Project presets" : "Your workspace");
    if (ImGui::Button("Blank Project", ImVec2(190, 96))) Creating = true;
    if (PresetsOnly) return;
    ImGui::Spacing();
    ImGui::InputTextWithHint("##Folder", "Folder path", DirectoryText.data(), DirectoryText.size());
    ImGui::SameLine();
    if (ImGui::Button("Browse folder")) ConfigureDirectory(DecodeContentPath(DirectoryText.data()));
    ImGui::InputTextWithHint("##Search", "Search projects", SearchText.data(), SearchText.size());
    ImGui::Separator();
    const auto Fold = [](std::string Text)
    {
        for (auto &Character : Text)
            Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
        return Text;
    };
    const auto Search = Fold(SearchText.data());
    std::filesystem::path Navigation, Selection;
    size_t ProjectCount = 0;
    for (const auto &Entry : Directory.QueryEntries())
    {
        const bool Folder = Entry.Category == ContentCategory::Directory;
        if (!Folder && Fold(EncodeContentPath(Entry.Path.extension())) != ".projectspace") continue;
        if (!Search.empty() && Fold(Entry.Name).find(Search) == std::string::npos) continue;
        if (!Folder) ++ProjectCount;
        ImGui::PushID(EncodeContentPath(Entry.Path).c_str());
        const auto Label = (Folder ? "[Folder] " : "[Project] ") + Entry.Name;
        if (ImGui::Selectable(Label.c_str(), false, 0, ImVec2(0, 38)))
        {
            if (Folder) Navigation = Entry.Path;
            else Selection = Entry.Path;
        }
        ImGui::PopID();
    }
    if (!Navigation.empty()) ConfigureDirectory(Navigation);
    if (!Selection.empty()) SelectProject(Selection);
    if (ProjectCount == 0) ImGui::TextWrapped("No project containers here. Open a project from disk or create a Blank Project.");
}

void ProjectBrowserPanel::ConstructDetails()
{
    if (SelectedPath.empty())
    {
        ImGui::TextWrapped("Select a project to inspect its settings.");
        return;
    }
    const auto Name = SelectedProject.Sections["project"]["name"].value_or(std::string("Project"));
    ImGui::TextWrapped("%s", Name.c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("%s", EncodeContentPath(SelectedPath.filename()).c_str());
    ImGui::TextUnformatted("Editable projectspace");
    ImGui::TextWrapped("Existing project files are read without modification. No sample content is inserted.");
}

void ProjectBrowserPanel::ConstructCreation()
{
    if (Creating)
    {
        ImGui::OpenPopup("Create Blank Project");
        Creating = false;
    }
    if (ImGui::BeginPopupModal("Create Blank Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Project name", NameText.data(), NameText.size());
        ImGui::InputText("Destination .projectspace", DestinationText.data(), DestinationText.size());
        if (PickProjectPath && ImGui::Button("Choose destination"))
        {
            const auto Chosen = PickProjectPath(true);
            if (!Chosen.empty())
                std::snprintf(DestinationText.data(), DestinationText.size(), "%s", EncodeContentPath(Chosen).c_str());
        }
        if (ImGui::Button("Create") && CreateProject(DecodeContentPath(DestinationText.data()), NameText.data()))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        if (!Diagnostic.empty()) ImGui::TextWrapped("%s", Diagnostic.c_str());
        ImGui::EndPopup();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ALIGNED PANEL GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

void ProjectBrowserPanel::ConstructInterface()
{
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(Viewport->WorkSize);
    ImGui::SetNextWindowViewport(Viewport->ID);
    const auto Colour = [](ColorQuad Quad) { return ImVec4(Quad.Red, Quad.Green, Quad.Blue, Quad.Alpha); };
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Colour(Colours.Field));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colour(Colours.Panel));
    ImGui::PushStyleColor(ImGuiCol_Text, Colour(Colours.Text));
    ImGui::PushStyleColor(ImGuiCol_Border, Colour(Colours.StrokeStrong));
    ImGui::PushStyleColor(ImGuiCol_Separator, Colour(Colours.Divider));
    ImGui::PushStyleColor(ImGuiCol_Button, Colour(Colours.Raised));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Colour(Colours.Selected));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Colour(Colours.Inset));
    ImGui::PushStyleColor(ImGuiCol_Header, Colour(Colours.Selected));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Colour(Colours.Raised));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    constexpr auto Options = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("Project browser", nullptr, Options);
    const float Scale = ImGui::GetStyle().FontScaleDpi;
    const float Width = ImGui::GetContentRegionAvail().x;
    const bool Compact = Width < 760 * Scale;
    if (!ResponsiveConfigured || (Compact && !WasCompact))
    {
        LeftCollapsed = RightCollapsed = Compact;
        ResponsiveConfigured = true;
    }
    WasCompact = Compact;
    const float Left = (LeftCollapsed ? 44 : (Compact ? 140 : 204)) * Scale;
    const float Right = (RightCollapsed ? 44 : (Compact ? 220 : 302)) * Scale;
    const float Centre = std::max(1.0f, Width - Left - Right);
    const auto Gap = ImGui::GetStyle().ItemSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, Gap.y));
    const auto Column = [&](const char *Token, float Extent, const std::function<void()> &Header,
                            const std::function<void()> &Content)
    {
        ImGui::BeginChild(Token, ImVec2(Extent, 0), ImGuiChildFlags_Borders);
        const float Inset = Extent <= 44 * Scale ? 4 * Scale : 16 * Scale;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Inset, 12 * Scale));
        ImGui::BeginChild("Header", ImVec2(0, 64 * Scale), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        Header();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::Separator();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * Scale, 18 * Scale));
        ImGui::BeginChild("Content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        Content();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndChild();
    };
    Column("Files", Left, [&]()
    {
        if (ImGui::Button(LeftCollapsed ? ">##Left" : "<##Left"))
        {
            LeftCollapsed = !LeftCollapsed;
            if (Compact && !LeftCollapsed) RightCollapsed = true;
        }
        if (!LeftCollapsed) { ImGui::SameLine(); ImGui::TextUnformatted("Files"); }
    }, [&]()
    {
        if (LeftCollapsed) return;
        if (ImGui::Selectable("All files", !PresetsOnly)) PresetsOnly = false;
        if (ImGui::Selectable("Presets", PresetsOnly)) PresetsOnly = true;
    });
    ImGui::SameLine(0, 0);
    Column("Viewer", Centre, [&]()
    {
        ImGui::TextUnformatted("Project browser");
        if (PickProjectPath && ImGui::Button("Open from disk"))
        {
            const auto Chosen = PickProjectPath(false);
            if (!Chosen.empty()) SelectProject(Chosen);
        }
    }, [&]()
    {
        ConstructCatalogue();
        if (!Diagnostic.empty()) ImGui::TextWrapped("%s", Diagnostic.c_str());
    });
    ImGui::SameLine(0, 0);
    Column("Inspector", Right, [&]()
    {
        if (ImGui::Button(RightCollapsed ? "<##Right" : ">##Right"))
        {
            RightCollapsed = !RightCollapsed;
            if (Compact && !RightCollapsed) LeftCollapsed = true;
        }
        if (!RightCollapsed) { ImGui::SameLine(); ImGui::TextUnformatted("Details"); }
    }, [&]() { if (!RightCollapsed) ConstructDetails(); });
    ConstructCreation();
    ImGui::PopStyleVar();
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(10);
}
} // namespace Frontier
