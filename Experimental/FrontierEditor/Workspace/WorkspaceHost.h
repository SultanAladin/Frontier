/*==========================================================================================================================================
                                                              WORKSPACEHOST.H
==========================================================================================================================================*/
// 🧩 Editor workspace composition: production dockspace + notch with Viewport · Outliner · Inspector · Log Stream · Input.

#pragma once

#include "Engine/Interface/LogStreamPanel.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Interface/ThemeStructure.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Input/InputMap.h"
#include "Input/InputPanel.h"
#include "Workspace/RecordInspectorPanel.h"
#include "Workspace/VehicleSceneFixture.h"
#include "Workspace/ViewportPanel.h"

#include <cstdint>
#include <filesystem>
#include <functional>

namespace Frontier
{

class ControlCentrePanel;

//----------------------------------------------------------------------------------------------------------------------
//                                                       WORKSPACE HOST
//----------------------------------------------------------------------------------------------------------------------

// 📝 Composes the production units the way FrontierMain's project window and the Log Stream workspace
//    captures do — a host-owned PanelDockspace under the ControlCentrePanel notch — and completes the
//    VehicleHMI editor triad with the new Inspector. The host owns no drawing of its own: it registers
//    panels, synchronizes view-models once per cycle, integrates the shared input map and applies the
//    consequences (panel toggles, simulation capture). All pixels come from the production panels.
class WorkspaceHost
{
  public:
    WorkspaceHost(ControlCentrePanel& Centre, InputSubstrate& Substrate, std::filesystem::path LayoutPath,
                  std::filesystem::path InputPath);
    ~WorkspaceHost();

    WorkspaceHost(const WorkspaceHost&)            = delete;
    WorkspaceHost& operator=(const WorkspaceHost&) = delete;

    bool ConfigurePlatform();              // Detached-window policy + dockspace platform windows
    void Synchronize(double Δτ);           // [s] - View-model sync + input fold + consequences
    void ConstructInterface(float Scale);  // [-] - Interface scale from the appearance preferences
    void Finalize();                       // Persist layout + input assignments
    void ResetLayoutToDefaults();

    void ConfigureSimulationCaptured(bool Captured);
    [[nodiscard]] bool QuerySimulationCaptured() const noexcept { return SimulationCaptured; }
    std::function<void(bool)> NotifyPointerCaptureChanged;   // Windowed host: GLFW cursor policy

    [[nodiscard]] PanelDockspace&      QueryPanels() noexcept { return Panels; }
    [[nodiscard]] LogStreamPanel&      QueryLogPanel() noexcept { return LogPanel; }
    [[nodiscard]] SceneDirectoryPanel& QueryOutlinerPanel() noexcept { return OutlinerPanel; }
    [[nodiscard]] InputMap&            QueryInputMap() noexcept { return Map; }
    [[nodiscard]] InputPanel&          QueryInputPanel() noexcept { return InputsPanel; }
    [[nodiscard]] VehicleSceneFixture& QueryFixture() noexcept { return Fixture; }

  private:
    void ApplyDefaultSeating();
    void SeedLogBurst(bool InputFromFile);
    void TogglePanel(const char* Key, const char* Label);

    ControlCentrePanel&   Centre;
    InputSubstrate&       Substrate;
    std::filesystem::path LayoutFile;
    std::filesystem::path InputFile;

    InputMap              Map;
    PanelDockspace        Panels;
    LogStreamPanel        LogPanel;
    SceneDirectoryPanel   OutlinerPanel;
    RecordInspectorPanel  InspectorPanel;
    ViewportPanel         CanvasPanel;
    InputPanel            InputsPanel;
    VehicleSceneFixture   Fixture;

    ThemeStructure        Theme;                      // Appearance theme resolved for the editor panels
    RecordToken           LastSelected{};             // [-] - Selection-change logging
    uint64_t              LayoutResetRevision = 0;    // [-] - Control Centre layout-reset tracking
    double                HeartbeatAccumulator = 0;   // [s]
    uint32_t              HeartbeatIndex       = 0;   // [idx]
    bool                  SimulationCaptured   = false;
};

} // namespace Frontier
