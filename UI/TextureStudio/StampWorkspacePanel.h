/*============================================================================================================================================
                                                          STAMPWORKSPACEPANEL.H
============================================================================================================================================*/
// 🧩 Native Stamp (decal) workspace presentation and gestures: the same toolbar/canvas feel as Pattern, a simpler
//    single-decal purpose — Text/Vector/Image components over a bounded 24-layer stack, no pattern tiling.

#pragma once
#include "Engine/Interface/VectorAuthoringControls.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/ContentInterchange/VectorAuthoringDocument.h"
#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include <cstdint>
#include <map>
#include "Engine/Lifecycle/ActivationSequence.h"

namespace Frontier
{
class StampWorkspacePanel final : public ActivationSequence
{
public:
    StampWorkspacePanel();
    void ConstructInterface();
    void ConfigureInterfaceScale(float Scale);
    void ConfigureTypeface(void *Regular, void *Light = nullptr);
    void ConfigureDocument(VectorAuthoringDocument Document);
    // 📝 One small GPU atlas carries the open Stamp's own layer row-thumbnails (the Texture-workspace-style
    // preview inside each layer row), rebaked whenever the document edits — mirrors Pattern's layer atlas,
    // Stamp just has no library-wide gallery atlas since there is no preset library to browse.
    static constexpr uint32_t LayerAtlasTile = 64;
    static constexpr uint32_t LayerAtlasColumns = 12;
    static constexpr uint32_t LayerAtlasExtent = (LayerAtlasTile + 2) * LayerAtlasColumns;
    static constexpr size_t LayerAtlasCapacity = size_t(LayerAtlasColumns) * LayerAtlasColumns;
    struct StampAtlasImage
    {
        uint64_t Texture = 0;
        float MinimumU = 0, MinimumV = 0, MaximumU = 1, MaximumV = 1;
    };
    std::function<StampAtlasImage()> QueryLayerThumbnailAtlas;
    [[nodiscard]] uint64_t QueryLayerAtlasRevision();
    [[nodiscard]] const std::vector<uint8_t> &QueryLayerAtlasPixels();
    [[nodiscard]] static uint32_t QueryLayerAtlasExtent() noexcept { return LayerAtlasExtent; }
    [[nodiscard]] const VectorAuthoringDocument &QueryDocument() const;
    [[nodiscard]] const std::vector<uint32_t> &QuerySelection() const noexcept { return Selection; }
    [[nodiscard]] uint64_t QueryRevision() const noexcept { return History.QueryRevision(); }
    [[nodiscard]] PanelDockspace &QueryPanels() noexcept { return Panels; }
    [[nodiscard]] PlaneExtent QueryControlExtent(const std::string &Caption) const;
    [[nodiscard]] PlaneExtent QueryPanelExtent(const std::string &Caption) const;
    [[nodiscard]] PlaneExtent QueryBoardExtent() const noexcept { return BoardExtent; }
    [[nodiscard]] bool QueryModified() const { return QueryDocument() != SavedDocument; }
    [[nodiscard]] bool QueryAuthoringSettled() const noexcept { return Gesture == 0; }
    [[nodiscard]] const std::string &QueryDiagnostic() const noexcept { return Diagnostic; }
    bool RequestClose();
    bool ConsumeMaterialApplyRequest() noexcept;
    // 📝 Second argument is the device-pixel edge the returned image will actually be displayed at
    // (the zoomed board, framebuffer-scaled): lets the GPU bake at that resolution instead of a
    // fixed-size raster stretched by the zoom transform.
    std::function<uint64_t(const VectorCoverageProgram &, uint32_t)> QueryCoverage;
    std::function<bool()> QueryCoverageReady;
    std::function<std::optional<uint32_t>(const VectorAuthoringDocument &, PlanePoint)> QuerySelectionToken;
    std::function<bool(std::string_view)> SaveSource;
    std::function<std::optional<std::string>()> LoadSource;
    std::function<void()> ActivateMaterial;
    std::function<void()> ActivatePattern;
    std::function<void()> CancelClose;
    std::function<PlanePoint(PlanePoint)> EvaluateNavigationDelta;
private:
    enum class VectorAuthoringTool { Select, Rectangle, Ellipse, Diamond, Triangle, Polygon, Star, Pan };
    VectorAuthoringHistory History;
    VectorAuthoringDocument SavedDocument;
    VectorCoverageProgram CachedCoverageProgram;
    uint32_t CachedCoverageMask = UINT32_MAX;
    uint64_t CachedHistoryRevision = UINT64_MAX;
    uint64_t CachedGestureRevision = UINT64_MAX;
    uint64_t GestureRevision = 0;
    uint32_t GesturePrimitiveToken = 0;
    bool CachedCoverageValid = false;
    bool ApplyToMaterialRequested = false;
    std::optional<VectorAuthoringDocument> SelectionClipboard;
    void TransferSelection(bool Remove);
    void InsertSelection(bool InPlace);
    void ReplicateSelection();
    void SelectEditableVectors();

    std::vector<uint32_t> Selection, GestureSelection;
    std::vector<std::vector<uint32_t>> UndoSelections, RedoSelections;
    PanelDockspace Panels;
    VectorAuthoringControlSurface Controls;
    std::map<std::string, PlaneExtent> ControlExtents, PanelExtents;
    uint64_t LayerAtlasRevision = 0;
    uint64_t LayerAtlasBakedRevision = UINT64_MAX;
    std::vector<uint8_t> LayerAtlasPixels;
    void RefreshLayerAtlas();
    std::optional<VectorAuthoringDocument> GestureSource, GestureDraft;
    VectorAuthoringTool Tool = VectorAuthoringTool::Select;
    PlaneExtent BoardExtent;
    PlanePoint PointerOrigin{}, Pan{}, PanOrigin{}, SelectionPointer{};
    float SelectionTravel = 0;
    bool SelectionReleased = false;
    float Scale = 1, Zoom = 1;
    int Gesture = 0, Filter = 0;
    std::array<int, 2> ResizeDirection{};
    bool MarqueeAdditive = false, SelectionExtend = false;
    uint32_t MaskSelection = 0;
    bool ChannelsOpen = false, CloseRequested = false;
    bool TransparentCanvas = false, GridEnabled = false, SnappingEnabled = false;
    void *RegularTypeface = nullptr, *LightTypeface = nullptr;
    std::array<char, 81> NameText{};
    std::array<char, 129> SearchText{};
    std::string Diagnostic;
    // 📝 Add-component popups: Text/Vector/Image share one small staging buffer set rather than the
    // library-gallery machinery Pattern uses, since Stamp has no preset library to browse — only a
    // bounded 24-layer stack of hand-authored or imported decal components.
    std::array<char, 2049> ComponentTextBuffer{};
    float ComponentTextSize = 64;
    std::string ComponentSvgBuffer;
    std::string ComponentImageBuffer;
    uint32_t ComponentEditToken = 0; // 0 creates a new component; nonzero replaces that layer's content in place.
    void ConstructAddComponentMenu();
    void ConstructTextPopup();
    void ConstructVectorPopup();
    void ConstructImagePopup();
    void ConstructHeader();
    void ConstructLayers();
    void ConstructCanvas();
    void ConstructInspector();
    void ConstructCloseDialogue();
    void ConstructPrimitive(uint32_t Category, const char *Caption);
    void Finalize(VectorAuthoringDocument Document, std::vector<uint32_t> Tokens);
    void EvaluateEdit(const std::function<void(VectorAuthoringDocument &)> &Edit);
    void RestoreHistory(bool Redo);
    void Select(uint32_t Token, bool Extend = false);
    void CapturePanel(const char *Caption);
    void ApplySourcePatch(const VectorAuthoringSourcePatch &Patch);
    bool Save();
    void SuspendContent() noexcept override;
    void FinalizeContent() noexcept override;
};
}
