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
    const float Scale = ImGui::GetStyle().FontScaleDpi;
    const auto &Entries = Directory.QueryEntries();
    size_t ProjectCount = 0;
    for (const auto &Entry : Entries)
        if (Entry.Category != ContentCategory::Directory && Entry.Path.extension() == ".projectspace") ++ProjectCount;
    ImGui::TextUnformatted(PresetsOnly ? "Project presets" : "Your workspace");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu project files", ProjectCount);
    if (PresetsOnly)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("One native creation preset");
    }
    else
    {
        ImGui::Spacing();
        ImGui::InputTextWithHint("##Folder", "Folder path", DirectoryText.data(), DirectoryText.size());
        ImGui::SameLine();
        if (ImGui::Button("Browse folder")) ConfigureDirectory(DecodeContentPath(DirectoryText.data()));
        ImGui::InputTextWithHint("##Search", "Search projects", SearchText.data(), SearchText.size());
        ImGui::Separator();
    }
    const auto Fold = [](std::string Text)
    {
        for (auto &Character : Text)
            Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
        return Text;
    };
    const auto Search = Fold(SearchText.data());
    std::filesystem::path Navigation, Selection;
    const float CardHeight = 164 * Scale;
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16 * Scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.075f,.075f,.078f,1));
    if (ImGui::BeginTable("Project browser cards", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableNextColumn();
        ImGui::PushID("BlankProjectCard");
        ImGui::BeginChild("Card", ImVec2(0, CardHeight), ImGuiChildFlags_Borders);
        ImGui::TextDisabled("CREATE");
        ImGui::Spacing();
        if (ImGui::Button("Blank Project", ImVec2(-1, 48 * Scale))) Creating = true;
        ImGui::TextWrapped("Start from an empty project container. No sample scene content is inserted.");
        ImGui::EndChild();
        ImGui::PopID();
        for (const auto &Entry : Entries)
        {
            if (PresetsOnly) break;
            const bool Folder = Entry.Category == ContentCategory::Directory;
            if (!Folder && Fold(EncodeContentPath(Entry.Path.extension())) != ".projectspace") continue;
            if (!Search.empty() && Fold(Entry.Name).find(Search) == std::string::npos) continue;
            ImGui::TableNextColumn();
            ImGui::PushID(EncodeContentPath(Entry.Path).c_str());
            ImGui::BeginChild("Project card", ImVec2(0, CardHeight), ImGuiChildFlags_Borders);
            ImGui::TextDisabled("%s", Folder ? "FOLDER" : "PROJECT CONTAINER");
            ImGui::Spacing();
            if (ImGui::Selectable(Entry.Name.c_str(), Selection == Entry.Path || SelectedPath == Entry.Path,
                ImGuiSelectableFlags_None, ImVec2(0, 48 * Scale)))
            {
                if (Folder) Navigation = Entry.Path;
                else Selection = Entry.Path;
            }
            ImGui::TextWrapped("%s", Folder ? "Browse this folder for project containers." : "Native project settings and identity.");
            ImGui::EndChild();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!Navigation.empty()) ConfigureDirectory(Navigation);
    if (!Selection.empty()) SelectProject(Selection);
    if (ProjectCount == 0 && Entries.empty()) ImGui::TextWrapped("No project containers here. Open a project from disk or create a Blank Project.");
}

void ProjectBrowserPanel::ConstructDetails()
{
    if (SelectedPath.empty())
    {
        ImGui::TextWrapped("Select a project to inspect its settings.");
        return;
    }
    const float Scale = ImGui::GetStyle().FontScaleDpi;
    const float LabelColumn = 118 * Scale;
    const auto Row = [&](const char *Label, const std::string &Value)
    {
        ImGui::TextDisabled("%s", Label);
        ImGui::SameLine(LabelColumn);
        ImGui::TextWrapped("%s", Value.empty() ? "—" : Value.c_str());
    };
    const auto Count = [&](const char *Key)
    {
        const auto *Rows = SelectedProject.Sections[Key].as_array();
        return Rows ? Rows->size() : 0;
    };
    const auto Name = SelectedProject.Sections["project"]["name"].value_or(std::string("Project"));
    ImGui::TextWrapped("%s", Name.c_str());
    ImGui::TextDisabled("Editable projectspace · read without modification");
    ImGui::Spacing();
    ImGui::SeparatorText("Identity");
    Row("File", EncodeContentPath(SelectedPath.filename()));
    Row("Identity", SelectedProject.Sections["project"]["identity"].value_or(std::string{}));
    Row("Template rev", std::to_string(SelectedProject.Sections["project"]["template_revision"].value_or<int64_t>(0)));
    Row("Format", SelectedProject.Sections["format"].value_or(std::string{}));
    Row("Version", std::to_string(SelectedProject.Sections["version"].value_or<int64_t>(0)));
    ImGui::Spacing();
    ImGui::SeparatorText("Contents");
    if (const auto *Roots = SelectedProject.Sections["content_roots"].as_array())
    {
        ImGui::TextDisabled("Content roots");
        for (const auto &Root : *Roots)
        {
            const auto Locator = Root.value<std::string>().value_or(std::string{});
            ImGui::BulletText("%s", Locator.empty() ? "—" : Locator.c_str());
        }
    }
    Row("Levels", std::to_string(Count("levels")));
    Row("Catalogue", std::to_string(Count("catalogue")));
    Row("Dependency pins", std::to_string(Count("dependency_pins")));
    ImGui::Spacing();
    ImGui::SeparatorText("Engine");
    Row("Startup level", SelectedProject.Sections["engine"]["startup_level"].value_or(std::string{}));
    Row("Config version", std::to_string(SelectedProject.Sections["engine"]["configuration_version"].value_or<int64_t>(0)));
    if (SelectedProject.AppearancePresent)
    {
        static constexpr const char *Themes[] = {"Oled", "Dark", "Dim", "Light", "Sepia", "Dracula", "Nord", "GitHub"};
        static constexpr const char *Accents[] = {"White", "Orange", "Amber", "Lime", "Emerald", "Cyan", "Blue", "Violet", "Fuchsia", "Rose"};
        const auto &Applied = SelectedProject.Appearance.Preferences;
        const auto Theme = static_cast<size_t>(Applied.Theme), Accent = static_cast<size_t>(Applied.Accent);
        ImGui::Spacing();
        ImGui::SeparatorText("Workspace");
        Row("Theme", Theme < 8 ? Themes[Theme] : "Custom");
        Row("Accent", Accent < 10 ? Accents[Accent] : "Custom");
        Row("Font", SelectedProject.Appearance.RequestedFamily);
        Row("Interface scale", std::to_string(int(Applied.InterfaceScale)) + "%");
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Open in Frontier", ImVec2(-1, 0))) RequestOpenProject();
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
