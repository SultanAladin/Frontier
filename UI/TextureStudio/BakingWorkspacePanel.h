/*============================================================================================================================================
                                                          BAKINGWORKSPACEPANEL.H
============================================================================================================================================*/
// 🧩 Native 1:1 port of Experimental/TextureStudio/src/BakingStudio.jsx's "Mesh maps" workflow: built-in study
//    meshes (Teapot/Cube/Sphere), the 27-tile mesh-map grid, a CPU orbit-camera 3D preview, a 2D map viewer and
//    the resolution/projection inspector. "Material patch" and "Volume SDF" modes are present as the same
//    mode-switch buttons the web app has, but their content panels are not yet native — see ConstructInspector.

#pragma once
#include "Engine/ContentInterchange/BakeMesh.h"
#include "Engine/Interface/IconArt.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Interface/PixelSpace.h"
#include "Engine/Interface/VectorAuthoringControls.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include "Engine/Render/MaterialPreviewConfiguration.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Frontier
{
class BakingWorkspacePanel final : public ActivationSequence
{
  public:
    BakingWorkspacePanel();
    void ConstructInterface();
    void ConfigureInterfaceScale(float Scale) noexcept { Scale = Scale > 0.1f ? Scale : 1.f; InterfaceScale = Scale; Controls.Scale = Scale; }
    [[nodiscard]] float QueryInterfaceScale() const noexcept { return InterfaceScale; }
    // 📝 Same contract as Pattern/Stamp's ConfigureTypeface: the app hands this the shared "DM Sans" regular/light
    // handles once at startup so the "Baking" pane heading and card titles read with the same type family as
    // every other native workspace instead of ImGui's bundled default font.
    void ConfigureTypeface(void *Regular, void *Light = nullptr) noexcept { RegularTypeface = Regular; LightTypeface = Light ? Light : Regular; }

    // 📝 Cross-workspace navigation, wired exactly like Stamp/Pattern's ActivateMaterial/ActivatePattern/ActivateStamp.
    std::function<void()> ActivateMaterial;
    std::function<void()> ActivatePattern;
    std::function<void()> ActivateStamp;
    // 📝 Same dependency-injection contract as MaterialLibraryPanel::PresentIcon (TextureStudioApplication.cpp
    // binds both to the identical IconPresentation::Draw lambda) - the inspector's section icons and fold
    // chevrons are real shared editor artwork reused from the Material workspace's inspector, not bespoke
    // hand-drawn draw-list shapes the way this panel's identity glyph used to be.
    std::function<bool(IconSymbol, PlanePoint, float, ColorQuad)> PresentIcon;

    // 📝 Where ConstructInterface last wrote exported PNG/manifest files (QueryExportDirectory to configure it);
    // empty until a bake has been exported at least once this session.
    [[nodiscard]] const std::string &QueryLastExportDirectory() const noexcept { return LastExportDirectory; }
    void ConfigureExportDirectory(std::filesystem::path Directory) { ExportDirectory = std::move(Directory); }

    // 📝 Screen-space rect of a tracked interactive control, last frame it was drawn — mirrors Pattern/Stamp's
    // QueryControlExtent convention so headless evidence/validation can drive real clicks instead of poking
    // private state. Only the handful of controls automated evidence actually needs are tracked (see .cpp).
    [[nodiscard]] PlaneExtent QueryControlExtent(const std::string &Caption) const;
    // 📝 Read-only access to the live recipe (headless evidence/validation needs this to assert, e.g., that
    // the "All" preset really did select every implemented mesh map, not just open a dialog that looked right).
    [[nodiscard]] const BakeSettings &QuerySettings() const noexcept { return Settings; }

    // 📝 Same host contract Pattern/StampWorkspacePanel expose (TextureStudioApplication.cpp calls these on
    // every registered workspace's Panels instance): layout persistence, detached-window platform windows and
    // recovery all go through this one shared dockspace instead of Baking inventing its own.
    [[nodiscard]] PanelDockspace &QueryPanels() noexcept { return Panels; }
    void ConfigurePlatform() { (void)Panels.ConfigurePlatform(); }

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
    // 📝 3D-preview texture choice: empty ("Shaded") keeps the plain Lambert-lit surface; any other value is a
    // baked map key, and ConstructViewport3D paints the mesh with that map's actual baked texels (sampled at
    // each triangle's real UVs, not a GPU texture - same "genuine CPU representation of the real data" approach
    // as ConstructMapPreview's 2D block raster) tinted by the same per-face Lambert shade. Native-only addition
    // requested directly by the user; the web reference always hardwires the bevel/tangent normal in 3D with no
    // picker, so there is no studioTheme.css/BakingStudio.jsx counterpart to port for this dropdown.
    std::string Preview3DMapKey;

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
    // 📝 1:1 with BakingStudio.jsx's <FolderOpen>/saveRecipe() header buttons - SaveRecipe() writes a genuine
    // JSON settings snapshot (no vendored JSON library in this tree, so it is hand-serialized; every field is
    // real, not a stub). OpenRecipe reports that a native file-picker isn't wired into this build yet rather
    // than silently doing nothing, since there is no dialog library in this tree to back a real file pick.
    void SaveRecipe();
    void OpenRecipe();

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
    // 📝 Reuses the real Material-workspace orbit camera (Engine/Render/MaterialPreviewConfiguration.h) -
    // the same Yaw/Pitch/Zoom/Pan fields and the same ConstructOrbitProjection() math
    // MaterialLibraryPanel's inspector preview drags - instead of a separately hand-rolled camera struct.
    // Baking has no fixed per-shape bounding table like Material's five catalogue shapes, so it supplies its
    // own live-measured demo-mesh radius/pivot to ConstructOrbitProjection() rather than reusing
    // ConstructMaterialPreviewProjection()'s Shape-indexed lookup, which is specific to Material's fixed
    // ShaderBall/Sphere/Plane/Cube/Cloth catalogue (and its thumbnail-atlas 16-unit shape spacing).
    // 📝 Pitch is 0.16 rather than the pre-port 0.35 because ConstructOrbitProjection already adds its own
    // .1908 rad "look slightly down" base elevation (the same Alloy capture-rig default Material's preview
    // starts at) - added together the two land at roughly the same starting elevation as before.
    MaterialPreviewConfiguration Camera{.Yaw = .7f, .Pitch = .16f};
    // 📝 Same orbit lag as MaterialLibraryPanel's inspector preview (see its SynchronizePreviewCamera): Camera
    // is the raw drag/wheel target, SmoothedCamera eases toward it every frame so the viewport glides instead
    // of snapping, using the identical exponential blend. Reset view therefore also glides, not snaps - same
    // feel as Material, since ResetCamera() only resets the target (see below).
    MaterialPreviewConfiguration SmoothedCamera;
    bool SmoothedCameraReady = false;
    void SynchronizeCamera();
    bool Overlay = false, Wireframe = false;
    void ConstructViewport3D(PlaneExtent Region);
    void ResetCamera() noexcept { ResetMaterialPreviewView(Camera); }

    //--------------------------------------------------------------------------------------------------------------
    //                                                 LAYOUT SECTIONS
    //--------------------------------------------------------------------------------------------------------------
    float InterfaceScale = 1.f;
    void ConstructHeader();
    void ConstructAssetsDock();
    void ConstructPreviewDock();
    void ConstructInspector();
    void ConstructMeshMapGrid(float Width);
    void ApplyPreset(const std::string &Preset);
    // 📝 The reusable dockable-panel host every other TextureWorkspace panel (Material/Pattern/Stamp)
    // registers its own docks with - registered once in the constructor (see the .cpp), replacing Baking's
    // previous bespoke fixed-position SetNextWindowPos/Size layout for the Assets/Preview/Inspector docks.
    PanelDockspace Panels;

    std::map<std::string, PlaneExtent> ControlExtents;
    void Track(const std::string &Caption);

    // 📝 Same shared rounded-control surface Pattern/Stamp draw their cards, pill buttons and field rows
    // with (Engine/Interface/VectorAuthoringControls.h) - reused here instead of raw ImGui widgets so the
    // Baking workspace actually matches the rest of the app's "Alloy" design language, not a bespoke look.
    VectorAuthoringControlSurface Controls;
    void *RegularTypeface = nullptr, *LightTypeface = nullptr;

    // 📝 Inspector redesign (reuses MaterialLibraryPanel's inspector components): PresentIcon draws real
    // shared editor artwork (ConstructIcon is just its thin positional wrapper, same shape as
    // MaterialLibraryPanel::ConstructIcon), and FoldedInspectorCards drives the exact same collapsible-card
    // affordance as Material's per-layer property cards via Engine/Interface/InspectorFold.h.
    void ConstructIcon(IconSymbol Symbol, float X, float Y, float Size, uint32_t Rgb = 0xa3a393);
    std::set<std::string> FoldedInspectorCards;
};
} // namespace Frontier
