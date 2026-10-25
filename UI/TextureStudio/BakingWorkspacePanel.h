/*============================================================================================================================================
                                                          BAKINGWORKSPACEPANEL.H
============================================================================================================================================*/
// 🧩 Native 1:1 port of Experimental/TextureStudio/src/BakingStudio.jsx's "Mesh maps" workflow: built-in study
//    meshes (Teapot/Cube/Sphere), the 27-tile mesh-map grid, a CPU orbit-camera 3D preview, a 2D map viewer and
//    the resolution/projection inspector. "Material patch" and "Volume SDF" modes are present as the same
//    mode-switch buttons the web app has, but their content panels are not yet native — see ConstructInspector.

#pragma once
#include "Engine/ContentInterchange/BakeMesh.h"
#include "Engine/Interface/PixelSpace.h"
#include "Engine/Interface/VectorAuthoringControls.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <string>
#include <vector>

namespace Frontier
{
class BakingWorkspacePanel final : public ActivationSequence
{
  public:
    BakingWorkspacePanel();
    void ConstructInterface();
    void ConfigureInterfaceScale(float Scale) noexcept { Scale = Scale > 0.1f ? Scale : 1.f; InterfaceScale = Scale; }
    [[nodiscard]] float QueryInterfaceScale() const noexcept { return InterfaceScale; }
    // 📝 Same contract as Pattern/Stamp's ConfigureTypeface: the app hands this the shared "DM Sans" regular/light
    // handles once at startup so the "Baking" pane heading and card titles read with the same type family as
    // every other native workspace instead of ImGui's bundled default font.
    void ConfigureTypeface(void *Regular, void *Light = nullptr) noexcept { RegularTypeface = Regular; LightTypeface = Light ? Light : Regular; }

    // 📝 Cross-workspace navigation, wired exactly like Stamp/Pattern's ActivateMaterial/ActivatePattern/ActivateStamp.
    std::function<void()> ActivateMaterial;
    std::function<void()> ActivatePattern;
    std::function<void()> ActivateStamp;

    // 📝 Where ConstructInterface last wrote exported PNG/manifest files (QueryExportDirectory to configure it);
    // empty until a bake has been exported at least once this session.
    [[nodiscard]] const std::string &QueryLastExportDirectory() const noexcept { return LastExportDirectory; }
    void ConfigureExportDirectory(std::filesystem::path Directory) { ExportDirectory = std::move(Directory); }

    // 📝 Screen-space rect of a tracked interactive control, last frame it was drawn — mirrors Pattern/Stamp's
    // QueryControlExtent convention so headless evidence/validation can drive real clicks instead of poking
    // private state. Only the handful of controls automated evidence actually needs are tracked (see .cpp).
    [[nodiscard]] PlaneExtent QueryControlExtent(const std::string &Caption) const;

  private:
    bool InitializeContent() noexcept override;
    void FinalizeContent() noexcept override;
    void IntegrateContent(double DeltaSeconds) noexcept override;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 DOCUMENT STATE
    //--------------------------------------------------------------------------------------------------------------
    enum class StudioMode { Mesh, Material, Volume };
    StudioMode Mode = StudioMode::Mesh;
    enum class ViewMode { Preview3D, Maps2D };
    ViewMode View = ViewMode::Preview3D;

    std::string DemoShape = "Teapot";
    DemoBakeAssets Assets;
    BakeSettings Settings;
    std::vector<std::string> SelectedTargets; // low part names included in the next bake
    std::string InspectMapKey = "normal";
    std::string PreviewSetName, PreviewMapKey = "normal";

    void SelectDemo(const std::string &Shape);
    void ClearResults() noexcept;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 BAKE EXECUTION
    //--------------------------------------------------------------------------------------------------------------
    std::future<std::vector<BakePartResult>> BakeFuture;
    std::atomic<bool> BakeRunning{false};
    std::atomic<uint64_t> BakeDone{0}, BakeTotal{1};
    std::string BakeProgressLabel;
    std::vector<BakePartResult> Results;
    std::string Info, Error;
    // 📝 1:1 with BakingStudio.jsx's `stale` flag: true once the inspector's settings have changed since the
    // snapshot StartBake() captured for the bake that produced Results - drives the "Previous bake · settings
    // changed" subtitle and the 2D map caption's "previous recipe" vs "current bake" wording.
    BakeSettings SettingsAtLastBake;
    [[nodiscard]] bool SettingsStale() const noexcept { return !Results.empty() && !SettingsFieldsEqual(Settings, SettingsAtLastBake); }
    void StartBake();
    void PollBake();

    //--------------------------------------------------------------------------------------------------------------
    //                                                 SETTINGS UNDO/REDO (BakingStudio.jsx's travel())
    //--------------------------------------------------------------------------------------------------------------
    std::vector<BakeSettings> SettingsUndoStack, SettingsRedoStack;
    BakeSettings SettingsAtFrameStart;
    double SettingsCoalesceClockSeconds = 0.0, LastSettingsPushSeconds = -1000.0;
    static bool SettingsFieldsEqual(const BakeSettings &A, const BakeSettings &B) noexcept;
    void CoalesceSettingsUndo();
    void UndoSettings();
    void RedoSettings();


    //--------------------------------------------------------------------------------------------------------------
    //                                                 2D MAP PREVIEW (DRAW-LIST BLOCK RASTER) + PNG EXPORT
    //--------------------------------------------------------------------------------------------------------------
    bool ConstructMapPreview(PlaneExtent Region);
    std::filesystem::path ExportDirectory;
    std::string LastExportDirectory;
    void ExportResults();

    //--------------------------------------------------------------------------------------------------------------
    //                                                 3D ORBIT VIEWPORT (ImGui draw-list rasterization)
    //--------------------------------------------------------------------------------------------------------------
    struct OrbitCamera
    {
        float Yaw = 0.7f, Pitch = 0.35f, Distance = 4.2f;
    } Camera;
    bool Overlay = false, Wireframe = false;
    void ConstructViewport3D(PlaneExtent Region);
    void ResetCamera() noexcept { Camera = OrbitCamera{}; }

    //--------------------------------------------------------------------------------------------------------------
    //                                                 LAYOUT SECTIONS
    //--------------------------------------------------------------------------------------------------------------
    float InterfaceScale = 1.f;
    void ConstructHeader();
    void ConstructAssetsDock(PlaneExtent Region);
    void ConstructPreviewDock(PlaneExtent Region);
    void ConstructInspector(PlaneExtent Region);
    void ConstructMeshMapGrid(float Width);
    void ApplyPreset(const std::string &Preset);

    std::map<std::string, PlaneExtent> ControlExtents;
    void Track(const std::string &Caption);

    // 📝 Same shared rounded-control surface Pattern/Stamp draw their cards, pill buttons and field rows
    // with (Engine/Interface/VectorAuthoringControls.h) - reused here instead of raw ImGui widgets so the
    // Baking workspace actually matches the rest of the app's "Alloy" design language, not a bespoke look.
    VectorAuthoringControlSurface Controls;
    void *RegularTypeface = nullptr, *LightTypeface = nullptr;
};
} // namespace Frontier
