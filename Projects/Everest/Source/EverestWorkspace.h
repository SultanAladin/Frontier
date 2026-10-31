/*==========================================================================================================================================
                                                         EVERESTWORKSPACE.H
==========================================================================================================================================*/
// 🧩 Project Everest workspace: wraps EverestApplication runtime + #if FRONTIER_EDITOR docked authoring interface.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "EverestApplication.h"
#include "EverestSimulationRecords.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/LogStreamPanel.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Interface/SceneInspectorPanel.h"
#include "UI/Editor/ControlCentrePanel.h"
#endif

#include <filesystem>
#include <functional>
#include <string>

namespace Everest
{

class EverestWorkspace
{
  public:
#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
    explicit EverestWorkspace(Frontier::ControlCentrePanel& ControlCentre,
                              std::filesystem::path         ProjectRootPath = "Projects/Everest");
#else
    explicit EverestWorkspace(std::filesystem::path ProjectRootPath = "Projects/Everest");
#endif
    ~EverestWorkspace();

    EverestWorkspace(const EverestWorkspace&)            = delete;
    EverestWorkspace& operator=(const EverestWorkspace&) = delete;

    void IntegrateSimulation(double DeltaSeconds);

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
    bool ConfigurePlatform() noexcept;
    void ConstructInterface(double DeltaSeconds);

    std::function<void()> EvaluateReturnToBrowser;

    [[nodiscard]] Frontier::SceneDirectoryPanel& QueryOutliner() noexcept { return OutlinerPanel; }
    [[nodiscard]] Frontier::SceneInspectorPanel& QueryInspector() noexcept { return InspectorPanel; }
    [[nodiscard]] Frontier::LogStreamPanel&      QueryLogStream() noexcept { return LogPanel; }
    [[nodiscard]] Frontier::PanelDockspace&      QueryDockspace() noexcept { return Dockspace; }
#endif

    [[nodiscard]] EverestApplication&          AccessApplication() noexcept { return Application; }
    [[nodiscard]] const EverestApplication&    QueryApplication() const noexcept { return Application; }
    [[nodiscard]] Frontier::SceneWorld&        QuerySceneWorld() noexcept { return Application.AccessWorld(); }
    [[nodiscard]] const Frontier::SceneWorld&  QuerySceneWorld() const noexcept { return Application.QueryWorld(); }
    [[nodiscard]] const std::filesystem::path& QueryProjectRoot() const noexcept { return ProjectRoot; }
    [[nodiscard]] const std::filesystem::path& QueryActiveLevelPath() const noexcept { return ActiveLevelPath; }
    [[nodiscard]] const std::string&           QueryDiagnostic() const noexcept { return Diagnostic; }
    [[nodiscard]] double                       QueryElapsedSeconds() const noexcept { return ElapsedSeconds; }

  private:
    void InitializeWorld();

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
    void ServiceDynamicSceneRequests();
    void ConstructTopToolbar(float Scale);
    void ConstructViewportSurface();

    Frontier::ControlCentrePanel& Centre;
    Frontier::PanelDockspace      Dockspace;
    Frontier::SceneDirectoryPanel OutlinerPanel;
    Frontier::SceneInspectorPanel InspectorPanel;
    Frontier::LogStreamPanel      LogPanel;
    uint64_t                      ObservedLayoutRevision = 0;
#endif

    std::filesystem::path ProjectRoot;
    std::filesystem::path ActiveLevelPath;
    EverestApplication    Application;
    std::string           Diagnostic;
    double                ElapsedSeconds = 0.0;
};

} // namespace Everest
