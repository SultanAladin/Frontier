/*============================================================================================================================================
                                                  TEXTUREPAINTWORKSPACEPANEL.H
============================================================================================================================================*/
// 🧩 Native port of Experimental/TextureStudio/src/TexturePaintStudio.jsx's Texture Paint workspace: the same
//    three-dock composition (layer stack LEFT, Teapot viewport CENTER, inspector/history RIGHT), the same header,
//    authoring bar, mask toolbar, content-browser launcher and status bar, built from the shared native surfaces
//    every other TextureStudio workspace already uses (PanelDockspace, VectorAuthoringControlSurface, GlyphSpace
//    icons and the Material workspace's orbit camera in MaterialPreviewConfiguration.h).
//
// 📝 Scope, stated plainly: the layer document below is real local state (add/duplicate/delete/reorder/folder/
//    mask/channel/opacity/blend edits, snapshot undo/redo history, JSON project save), and the viewport is a real
//    CPU draw-list rasterizer over the same built-in Teapot geometry the Baking workspace bakes (BakeMesh.h) -
//    orbit/zoom/pick, wireframe, ground grid, part targeting, an authoring-UDIM UV view derived from the mesh's
//    actual chart ids, and preview-only digital dabs/fills evaluated per-triangle for the on-screen composite.
//    It is NOT Alloy's WebGL surface compositor: no GPU paint slices, no baked PBR map export, no decal/stamp
//    projection engine and no material-snapshot shading are claimed here. Controls whose engine is not native
//    yet (Stamp/Transform placement, project file picking) stay disabled or say so in the status line.

#pragma once
#include "Engine/ContentInterchange/BakeMesh.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Interface/PixelSpace.h"
#include "Engine/Interface/VectorAuthoringControls.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include "Engine/Render/MaterialPreviewConfiguration.h"
#include "UI/TextureStudio/PaintStampEngine.h"
#include "UI/TextureStudio/StampLibrary.h"
#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Frontier
{
class TexturePaintWorkspacePanel final : public ActivationSequence
{
  public:
    TexturePaintWorkspacePanel();
    void ConstructInterface();
    void ConfigureInterfaceScale(float Scale) noexcept { Scale = Scale > 0.1f ? Scale : 1.f; InterfaceScale = Scale; Controls.Scale = Scale; }
    [[nodiscard]] float QueryInterfaceScale() const noexcept { return InterfaceScale; }
    // 📝 Same contract as Pattern/Stamp/Baking's ConfigureTypeface: shared "DM Sans" regular/light handles.
    void ConfigureTypeface(void *Regular, void *Light = nullptr) noexcept { RegularTypeface = Regular; LightTypeface = Light ? Light : Regular; }

    // 📝 Cross-workspace navigation, wired exactly like Baking/Stamp/Pattern's ActivateMaterial/... hooks.
    std::function<void()> ActivateMaterial;
    std::function<void()> ActivatePattern;
    std::function<void()> ActivateStamp;
    std::function<void()> ActivateBaking;

    // 📝 Material catalogue snapshot retained for layer source-asset links (name/category/colour only -
    // assigning one stores an asset link on the selected layer, Alloy's own "Asset link stored on the
    // layer" behaviour; it does not shade the preview with that material's full recipe). The content
    // browser UI that consumed this snapshot was deleted; the data plumbing stays for its return.
    struct MaterialAsset
    {
        std::string Name;
        std::string Category;
        std::string ColourHex;
    };
    void ConfigureMaterialAssets(std::vector<MaterialAsset> Assets) { MaterialAssets = std::move(Assets); }
    [[nodiscard]] size_t QueryMaterialAssetCount() const noexcept { return MaterialAssets.size(); }

    // 📝 Save project / autosaved draft land here (the material library directory the app already owns).
    void ConfigureProjectDirectory(std::filesystem::path Directory) { ProjectDirectory = std::move(Directory); }

    // 🖨️ Shared stamp library folder (<library>/Stamps): the Stamp workspace saves decals here, the
    //    stamp tool's right-click picker reads them (plus built-in presets) from the same place.
    void ConfigureStampLibraryDirectory(std::filesystem::path Directory)
    {
        StampLibraryDirectory = std::move(Directory);
        EnsureStampLibrary(true);
    }

    // 🎨 UV-space preview canvas: the layer stack composited per texel (one 512² tile per UDIM part,
    // 2x2 in a MaterialThumbnailAtlas page) so the 3D/2D views texture-map the mesh per pixel like
    // the HTML paint workshop, instead of flat-shading one colour per face. Host plumbing mirrors
    // the Stamp/Pattern layer atlases exactly: the host supplies the sampled image through
    // QueryPaintCanvasImage and uploads QueryPaintCanvasPixels whenever the revision moves. With no
    // host image the panel keeps its per-triangle centroid preview (headless validation hosts).
    static constexpr uint32_t PaintCanvasTile = 512;
    static constexpr uint32_t PaintCanvasColumns = 2;
    static constexpr uint32_t PaintCanvasExtent = (PaintCanvasTile + 2) * PaintCanvasColumns;
    struct PaintCanvasImage
    {
        uint64_t Texture = 0;
        float MinimumU = 0, MinimumV = 0, MaximumU = 1, MaximumV = 1;
    };
    std::function<PaintCanvasImage()> QueryPaintCanvasImage;
    [[nodiscard]] uint64_t QueryPaintCanvasRevision();
    [[nodiscard]] const std::vector<uint8_t> &QueryPaintCanvasPixels();
    [[nodiscard]] static uint32_t QueryPaintCanvasExtent() noexcept { return PaintCanvasExtent; }

    // 📝 Read-only document facts for headless construction validation (mirrors Baking's QuerySettings).
    [[nodiscard]] const std::string &QueryDocumentName() const noexcept { return DocumentName; }
    [[nodiscard]] size_t QueryLayerCount() const noexcept { return Layers.size(); }
    [[nodiscard]] std::string QuerySelectedLayerName() const;
    [[nodiscard]] size_t QueryHistoryCount() const noexcept { return HistoryEntries.size(); }
    [[nodiscard]] size_t QueryTriangleCount() const noexcept { return Topology.size(); }
    [[nodiscard]] PlaneExtent QueryControlExtent(const std::string &Caption) const;

    // 📝 Same host contract every other workspace panel exposes: one shared dockspace owns layout/platform.
    [[nodiscard]] PanelDockspace &QueryPanels() noexcept { return Panels; }
    void ConfigurePlatform() { (void)Panels.ConfigurePlatform(); }

  private:
    bool InitializeContent() noexcept override;
    void FinalizeContent() noexcept override;
    void IntegrateContent(double DeltaSeconds) noexcept override;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 DOCUMENT MODEL
    //--------------------------------------------------------------------------------------------------------------
    enum class LayerKind { Paint, Fill, Material, Generator, Decal, Folder };
    enum class RegionScope { Face, Tile, Island, Object };

    struct PaintDab
    {
        float U = 0, V = 0;      // authoring-UDIM coordinates (U spans 0..4 across tiles 1001-1004)
        float Radius = 0.03f;    // in UV units of one tile
        std::array<float, 4> Colour{1, 1, 1, 1}; // rgb + dab strength
        int Part = -1;
    };
    struct PaintFill
    {
        RegionScope Scope = RegionScope::Object;
        int Part = -1;
        int Chart = -1;
        int Triangle = -1;
        std::array<float, 4> Colour{1, 1, 1, 1};
    };
    // 🖨️ One placed decal: a StampLibrary entry (by id) centred at an authoring UV, sized/rotated in
    //    UV space like dabs are, tinted at placement time. Evaluated per-triangle in the preview.
    struct StampPlacement
    {
        std::string StampId;
        float U = 0, V = 0;
        float Radius = 0.06f; // half-size in authoring-UV units
        float Rotation = 0;   // radians, counter-clockwise in UV space
        std::array<float, 4> Tint{1, 1, 1, 1};
    };
    struct MaskState
    {
        bool Present = false;
        std::string Kind = "fill"; // fill | paint | color | gradient (Alloy's mask type pills)
        bool Enabled = true;
        bool Inverted = false;
        float Fill = 1.f;          // 1 = white, 0 = black
        float Strength = 100.f;
        std::array<float, 4> Colour{0.72f, 0.45f, 0.2f, 1.f}; // #b87333
        float Tolerance = 0.1f;
        float Softness = 0.1f;
        std::vector<PaintDab> Dabs;
        std::vector<PaintFill> Fills;
    };
    struct LayerState
    {
        std::string Id;
        std::string Name;
        LayerKind Kind = LayerKind::Paint;
        std::string ParentId; // empty = root
        bool Visible = true;
        bool Locked = false;
        bool Expanded = false;
        float Opacity = 100.f;
        std::string Blend = "Normal";
        std::vector<std::string> Channels;
        // 📝 paintChannelModel's channelSettings, flattened: per-channel source configuration (mode/value/
        // generator) stored sparsely - a missing key reads as Alloy's documented default for that channel.
        // These are authoring targets only; the preview composite does not evaluate channel sources.
        std::map<std::string, std::string> ChannelModes;                          // value | texture | generator | gradient
        std::map<std::string, float> ChannelValues;                               // scalar-edit channels
        std::map<std::string, std::array<float, 4>> ChannelColours;               // color-edit channels
        std::map<std::string, std::string> ChannelGenerators;                     // generator id ("" = none)
        std::map<std::string, std::map<std::string, float>> ChannelGeneratorParams;
        MaskState Mask;
        std::vector<PaintDab> Dabs;
        std::vector<PaintFill> Fills;
        std::vector<StampPlacement> Stamps;
        int StrokeCount = 0;
        int DecalCount = 0;
        std::string SourceAsset;       // assigned content-browser asset name, empty = none
        std::string SourceAssetType;   // material | generator | brush
        std::array<float, 4> SourceColour{0, 0, 0, 0};
    };
    // 🖌️ digitalBrush.js's DIGITAL_BRUSH_DEFAULTS, field for field.
    struct BrushState
    {
        std::string Preset = "round";
        std::string Tool = "paint"; // paint | erase | fill
        // 🖌️ PaintWorkshop instrument driving the stamp engine (PaintStampEngine.h). "Digital" preserves the
        // original dab-only behaviour; anything else feeds the raster stamp canvas for the stroke's layer.
        std::string PhysicalTool = "Digital";
        std::array<float, 4> Colour{0.843f, 0.549f, 0.349f, 1.f}; // #d78c59
        float Size = 24.f;
        float Opacity = 1.f;
        float Flow = 1.f;
        float Hardness = 0.92f;
        float Spacing = 0.16f;
        float Stabilization = 0.f;
        float SizeJitter = 0.f;
        float OpacityJitter = 0.f;
        float Scatter = 0.f;
        std::string Shape = "round"; // round | square
        float Roundness = 1.f;
        float Rotation = 0.f;
        bool Follow = false;
        std::string Grain = "none"; // none | noise | lines | checker
        float GrainAmount = 0.f;
        float GrainScale = 4.f;
        float HeightStrength = 0.4f;
        RegionScope FillScope = RegionScope::Object;
    };

    std::string DocumentName = "Teapot study";
    std::vector<LayerState> Layers; // topmost first, everywhere (Alloy's ordering contract)
    std::string SelectedLayerId;
    bool MaskSelected = false;
    uint64_t NextLayerSerial = 1;
    BrushState Brush;

    void ResetDocument();
    [[nodiscard]] LayerState *FindLayer(const std::string &Id) noexcept;
    [[nodiscard]] const LayerState *FindLayer(const std::string &Id) const noexcept;
    [[nodiscard]] LayerState *SelectedLayer() noexcept { return FindLayer(SelectedLayerId); }
    [[nodiscard]] int LayerDepth(const LayerState &Layer) const noexcept;
    [[nodiscard]] bool EffectiveLocked(const LayerState &Layer) const noexcept;
    [[nodiscard]] bool EffectiveVisible(const LayerState &Layer) const noexcept;
    [[nodiscard]] bool HiddenByCollapsedFolder(const LayerState &Layer) const noexcept;
    [[nodiscard]] bool IsDescendantOf(const std::string &Id, const std::string &AncestorId) const noexcept;
    void SelectLayer(const std::string &Id, bool Mask = false);
    void AddLayer(LayerKind Kind);
    void DuplicateSelectedLayer();
    void DeleteSelectedLayer();
    void MoveSelectedLayer(int Direction); // -1 = up (toward top), +1 = down, among siblings
    void SetLayerParent(const std::string &Id, const std::string &ParentId);
    void UngroupSelectedFolder();
    void AddMaskToSelected(bool ColourMask);
    void RemoveSelectedMask();
    void ToggleChannel(LayerState &Layer, const std::string &ChannelId);
    void ClearSelectedPaint();
    [[nodiscard]] std::string MakeLayerId(const char *Prefix);
    [[nodiscard]] std::string UniqueLayerName(const std::string &Base) const;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 HISTORY (snapshot undo/redo)
    //--------------------------------------------------------------------------------------------------------------
    struct Snapshot
    {
        std::vector<LayerState> Layers;
        std::string SelectedId;
        bool MaskSelected = false;
        BrushState Brush;
    };
    struct HistoryEntry
    {
        Snapshot State;
        std::string Label;
        std::string Detail;
        std::string Kind; // project | layer | folder | mask | channel | stroke | tool | source
        std::string TimeText;
    };
    std::vector<HistoryEntry> HistoryEntries;
    int HistoryIndex = -1;
    std::optional<HistoryEntry> PendingHistory;
    double PendingHistoryClock = 0.0;
    [[nodiscard]] Snapshot CaptureSnapshot() const;
    void RestoreSnapshot(const Snapshot &State);
    void CommitHistory(const std::string &Label, const std::string &Detail, const std::string &Kind = "layer");
    void QueueHistory(const std::string &Label, const std::string &Detail, const std::string &Kind = "layer");
    void Undo();
    void Redo();
    void RestoreHistoryEntry(int Index);
    [[nodiscard]] bool CanUndo() const noexcept { return HistoryIndex > 0; }
    [[nodiscard]] bool CanRedo() const noexcept { return HistoryIndex >= 0 && HistoryIndex + 1 < int(HistoryEntries.size()); }

    //--------------------------------------------------------------------------------------------------------------
    //                                                 PROJECT SAVE / DRAFT
    //--------------------------------------------------------------------------------------------------------------
    std::filesystem::path ProjectDirectory;
    bool Dirty = false;
    double ClockSeconds = 0.0;
    double LastEditClock = -100.0;
    [[nodiscard]] std::string SerializeProject() const;
    bool WriteProjectFile(const std::filesystem::path &Path) const;
    void SaveProject();
    void OpenProject();
    void MarkDirty() noexcept { Dirty = true; LastEditClock = ClockSeconds; }

    //--------------------------------------------------------------------------------------------------------------
    //                                                 TEAPOT GEOMETRY + UV TOPOLOGY
    //--------------------------------------------------------------------------------------------------------------
    DemoBakeAssets Assets;
    struct TopologyTriangle
    {
        uint32_t VertexIndex[3]{0, 0, 0};
        int Part = 0;   // 0 body, 1 lid, 2 handle, 3 spout (textureSurfaceModel.js TEXTURE_PARTS order)
        int Chart = 0;  // 0-based teapot patch
        int Index = 0;  // triangle ordinal
        float AuthoringUv[3][2]{}; // UDIM authoring set (U offset by part tile)
        float SourceUv[3][2]{};    // original overlapping patch UV0
        float CentroidUv[2]{0, 0};
    };
    std::vector<TopologyTriangle> Topology;
    void BuildTopology();
    [[nodiscard]] static int PartForPatch(int Patch) noexcept;
    void AuthoringUvFor(const BakeVertex &Vertex, float Out[2]) const noexcept;
    void SourceUvFor(const BakeVertex &Vertex, float Out[2]) const noexcept;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 PREVIEW COMPOSITE + PAINT INPUT
    //--------------------------------------------------------------------------------------------------------------
    [[nodiscard]] static bool FillMatches(const PaintFill &Fill, const TopologyTriangle &Triangle) noexcept;
    [[nodiscard]] float MaskCoverageAt(const LayerState &Layer, const TopologyTriangle &Triangle, const float Uv[2],
        const std::vector<int32_t> *DabCandidates = nullptr) const noexcept;
    [[nodiscard]] std::array<float, 3> EvaluatePreviewColour(const TopologyTriangle &Triangle) const noexcept;
    // One layer's contribution at an explicit authoring-UV point (the per-layer block of
    // EvaluatePreviewColour, shared with the texel compositor). Candidate lists optionally restrict
    // the dab loops to one spatial-grid cell - same result, dabs outside the cell cannot reach here.
    void ApplyPreviewLayer(std::array<float, 3> &Base, const LayerState &Layer, const TopologyTriangle &Triangle,
        const float Uv[2], const std::vector<int32_t> *DabCandidates, const std::vector<int32_t> *MaskDabCandidates,
        bool VisibilityPrechecked = false) const noexcept;
    void EnsurePreviewCanvas();
    void CompositePreviewCanvas();
    void BuildPreviewCanvasOwners();
    [[nodiscard]] uint64_t PreviewCanvasFingerprint() const noexcept;
    [[nodiscard]] bool PreviewCanvasTextured() const;
    [[nodiscard]] std::array<float, 2> PreviewCanvasUv(int Part, float U, float V) const noexcept;
    [[nodiscard]] bool DigitalPaintAvailable() const noexcept;
    void ApplyPaintAt(const float Uv[2], int Part, int Chart, int Triangle, float RadiusUv);
    void FinishStroke();
    // 🖌️ PaintWorkshop stamp engine (HarborPaintWorkshop.html port): a 2048x512 raster canvas over the 4-tile
    // UDIM strip. One layer owns the canvas at a time (the last layer painted with a physical tool); dabs stay
    // the document/undo truth, the canvas is the preview surface for its owning layer.
    PaintStampEngine StampEngine;
    bool StampCanvasConfigured = false;
    std::string StampCanvasLayerId;
    bool StampStrokeActive = false;
    void EnsureStampEngine();
    [[nodiscard]] bool StampToolSelected() const noexcept;
    void FeedStampEngine(const LayerState &Layer, const float Uv[2], int Triangle, float RadiusUv, bool Erasing);
    [[nodiscard]] bool BuildStampHit(const float Uv[2], int Triangle, float RadiusUv, StampHit &Out, float &WorldRadius) const noexcept;
    void SyncStampParams(float WorldRadius) noexcept;
    void ClearStampCanvas() noexcept;
    // 🖨️ Stamp library + decal placement (StampLibrary.h). The picker is a right-click popup in the
    // viewport while the Stamp author tool is active; placements live on layers like dabs/fills do.
    StampLibrary StampLib;
    std::filesystem::path StampLibraryDirectory;
    std::string ActiveStampId;
    bool StampLibraryScanned = false;
    bool StampRightDownValid = false;
    float StampRightDownX = 0, StampRightDownY = 0;
    // Material-catalogue-style picker state: All / Presets / Saved filter + name search, plus a
    // 48-block thumbnail cache so the picker samples each stamp once instead of every frame.
    int StampFilter = 0;
    char StampSearch[64]{};
    std::map<std::string, std::vector<uint32_t>> StampThumbCache;
    void EnsureStampLibrary(bool ForceReload = false);
    [[nodiscard]] const StampLibraryEntry *ActiveStamp() const noexcept;
    void PlaceStampAt(const float Uv[2]);
    void ConstructStampPicker();
    // 🎨 Preview-canvas state (see QueryPaintCanvasImage above). Scratch/Owners/Pixels all share the
    // atlas-page layout (PaintCanvasExtent², tiles inset by the 1px gutter); owners map each texel
    // to the Topology triangle that owns it so region fills/masks keep their per-face identity at
    // texel fidelity. Recomposited only when the content fingerprint moves.
    std::vector<uint8_t> PaintCanvasPixels;
    std::vector<std::array<float, 3>> PaintCanvasScratch;
    std::vector<int32_t> PaintCanvasOwners;
    std::vector<uint8_t> PaintCanvasFilled;
    uint64_t PaintCanvasRevision = 0;
    uint64_t PaintCanvasFingerprintCache = 0;
    uint64_t StampLibraryGeneration = 0;
    bool PaintCanvasComposited = false;
    bool PaintCanvasOwnersBuilt = false;
    size_t PaintCanvasOwnersTopology = 0;
    double PaintCanvasLastComposite = -1.0;
    bool StrokeActive = false;
    bool StrokeChanged = false;
    bool StrokeTargetMask = false;
    std::string StrokeTargetLayerId;
    float LastDabUv[2]{0, 0};
    bool LastDabValid = false;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 VIEWPORT STATE
    //--------------------------------------------------------------------------------------------------------------
    enum class ViewMode { Lit, Wireframe, Uv };
    ViewMode View = ViewMode::Lit;
    int TargetPart = -1; // -1 = All parts
    bool UvSource = false;
    bool Grid = true;
    enum class AuthorTool { Navigate, Stamp, Transform };
    AuthorTool Tool = AuthorTool::Navigate;
    bool DigitalActive = false;
    bool MaskPreview = false; // authoring-bar Material/Mask preview tabs

    // 📝 Same orbit camera + lag the Baking/Material viewports use (MaterialPreviewConfiguration).
    MaterialPreviewConfiguration Camera{.Yaw = .7f, .Pitch = .16f};
    MaterialPreviewConfiguration SmoothedCamera;
    bool SmoothedCameraReady = false;
    void SynchronizeCamera();
    void FrameCamera() noexcept;

    // 🧭 UV camera: Screen = RegionMin + Offset + (U * Scale, (1 - V) * Scale).
    float UvScale = 0.f, UvOffsetX = 0.f, UvOffsetY = 0.f, UvFrameScale = 1.f;
    bool UvFrameRequested = true;
    std::string UvHoverText;
    void FrameUv(float RegionWidth, float RegionHeight);

    // 🎯 Last frame's projected triangles, kept so this frame's pointer can pick/paint the surface.
    struct PickTriangle
    {
        float Screen[3][2]{};
        float Uv[3][2]{};
        float Depth = 0;
        int Part = -1, Chart = -1, Index = -1;
    };
    std::vector<PickTriangle> LastPickTriangles;
    [[nodiscard]] const PickTriangle *PickScene(float X, float Y, float OutUv[2]) const noexcept;

    // 🖱️ Click-vs-orbit discrimination for part picking (a drag orbits, a click selects the hit part).
    bool PointerDownValid = false;
    float PointerDownX = 0, PointerDownY = 0;

    struct MaskEditorState
    {
        enum class Tool { Brush, Select, Navigate };
        Tool ActiveTool = Tool::Brush;
        RegionScope Scope = RegionScope::Object;
        bool InkWhite = true;
        float SizePercent = 4.f;
        float Opacity = 100.f;
        float Hardness = 80.f;
        bool OptionsOpen = true;
        struct Region
        {
            RegionScope Scope = RegionScope::Object;
            int Part = -1, Chart = -1, Triangle = -1;
        };
        std::optional<Region> SelectedRegion;
    };
    MaskEditorState MaskEditor;
    [[nodiscard]] std::string MaskRegionLabel() const;
    void FillMaskRegion();

    void ConstructSceneViewport(PlaneExtent Region);
    void ConstructUvViewport(PlaneExtent Region);
    void HandlePaintPointer(bool Hovered, bool IsUv);

    //--------------------------------------------------------------------------------------------------------------
    //                                                 LAYOUT SECTIONS
    //--------------------------------------------------------------------------------------------------------------
    float InterfaceScale = 1.f;
    enum class RightPanelTab { Inspector, History };
    RightPanelTab RightTab = RightPanelTab::Inspector;
    bool HistoryStrokesOnly = false;
    int HistoryChosen = -1;
    // 📝 ChannelPropertyPanel's picker + per-channel fold state (Alloy starts these six channels folded).
    bool ChannelPickerOpen = false;
    std::set<std::string> ChannelFolded{"height", "normal", "occlusion", "anisotropy", "anisotropyAngle", "clearcoat"};
    char LayerQuery[64]{};
    enum class LayerFilter { All, Visible, Hidden };
    LayerFilter Filter = LayerFilter::All;
    bool AddMenuOpen = false;
    bool SystemBaseOpen = false;
    bool HelpOpen = false;
    bool BrushStudioOpen = false;
    bool BrushStudioChannels = false;
    char LayerNameBuffer[96]{};
    std::string LayerNameSelectionId;
    std::string Status = "Local layer project";
    std::string Error;
    std::string Toast;
    double ToastClock = -100.0;
    void ShowToast(const std::string &Text) { Toast = Text; ToastClock = ClockSeconds; }

    std::vector<MaterialAsset> MaterialAssets;

    void ConstructHeader();
    void ConstructLayersDock();
    void ConstructViewportDock();
    void ConstructInspectorDock();
    void ConstructInspector();
    void ConstructHistory();
    void ConstructBrushStudio();
    void ConstructStatusBar();
    void DrawLayerThumbnail(float MinimumX, float MinimumY, float Size, const LayerState &Layer) const;
    [[nodiscard]] bool PillButton(const char *Id, const char *Label, float Width, bool Selected, const char *IconPath = nullptr, uint32_t Accent = 0);
    [[nodiscard]] bool IconButton(const char *Id, const char *IconPath, float Size, bool Selected = false, uint32_t Colour = 0x9a9a8e);
    void ApplyBrushPreset(const std::string &PresetId);
    [[nodiscard]] const char *BrushPresetName() const noexcept;

    PanelDockspace Panels;
    std::map<std::string, PlaneExtent> ControlExtents;
    void Track(const std::string &Caption);
    VectorAuthoringControlSurface Controls;
    void *RegularTypeface = nullptr, *LightTypeface = nullptr;
};
} // namespace Frontier
