/*==========================================================================================================================================
                                                          PROJECTBROWSERPANEL.H
==========================================================================================================================================*/
// 🧩 Browse real project containers and create the sole Blank Project preset before entering the editor.

#pragma once
#include "Engine/ContentInterchange/ProjectSpaceCodec.h"
#include "Engine/Interface/ContentDirectory.h"
#include <array>
#include <functional>
#include <utility>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     PROJECT BROWSER
//------------------------------------------------------------------------------------------------------------------------

class ProjectBrowserPanel
{
  public:
    using ProjectPathSelection = std::function<std::filesystem::path(bool)>;
    explicit ProjectBrowserPanel(ProjectPathSelection Selection = {});
    bool ConfigureDirectory(const std::filesystem::path &Directory);
    bool SelectProject(const std::filesystem::path &Path);
    bool CreateProject(const std::filesystem::path &Destination, const std::string &Name);
    bool RequestOpenProject();
    void ResetSelection() noexcept { SelectedPath.clear(); Diagnostic.clear(); }
    void ConstructInterface();
    void ConfigurePalette(const ControlKitPalette &Palette) noexcept { Colours = Palette; }
    [[nodiscard]] std::filesystem::path ConsumeLaunchPath() { return std::exchange(LaunchPath, {}); }
    [[nodiscard]] const std::string &QueryDiagnostic() const noexcept { return Diagnostic; }
    [[nodiscard]] const std::filesystem::path &QuerySelection() const noexcept { return SelectedPath; }
    [[nodiscard]] static constexpr std::array<const char *, 1> QueryPresets() noexcept { return {"Blank Project"}; }

  private:
    ContentDirectory Directory;
    ControlKitPalette Colours;
    ProjectPathSelection PickProjectPath;
    ProjectSpaceDocument SelectedProject;
    std::filesystem::path SelectedPath, LaunchPath;
    std::string Diagnostic;
    std::array<char, 4096> DirectoryText{}, DestinationText{};
    std::array<char, 257> NameText{};
    std::array<char, 257> SearchText{};
    bool LeftCollapsed = false;
    bool RightCollapsed = false;
    bool Creating = false;
    bool ResponsiveConfigured = false;
    bool WasCompact = false;
    bool PresetsOnly = false;
    void ConstructCatalogue();
    void ConstructDetails();
    void ConstructCreation();
};
} // namespace Frontier
