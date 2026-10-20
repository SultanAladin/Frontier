/*============================================================================================================================================
                                                          PATTERNWORKSPACEPANEL.H
============================================================================================================================================*/
// 🧩 Native Pattern workspace presentation and gestures over shared authoring contracts, dock hosting and caller-owned GPU coverage.

#pragma once
#include "Engine/Interface/VectorAuthoringControls.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/ContentInterchange/CubicPathEditing.h"
#include <filesystem>
#include <map>
#include "Engine/Lifecycle/ActivationSequence.h"

namespace Frontier
{
class PatternWorkspacePanel final : public ActivationSequence
{
public:
    PatternWorkspacePanel();
    void ConstructInterface();
    void ConfigureInterfaceScale(float Scale);
    void ConfigureTypeface(void *Regular, void *Light = nullptr);
    void ConfigureDocument(VectorAuthoringDocument Document);
    bool ConfigurePresetDirectory(const std::filesystem::path &Directory);
    struct PatternLibraryFamilyEntry
    {
        std::string Name, Group, Description, Study;
        std::map<std::string, std::string> Files;
    };
    struct PatternThumbnailShape
    {
        std::vector<std::array<float, 2>> Triangles;
        std::vector<std::array<float, 2>> Outline;
        uint32_t FillColour = 0, LineColour = 0;
        bool Filled = false, Stroked = false, ClosedOutline = false;
        float StrokeWidth = 0;
    };
    struct PatternPresetCacheEntry
    {
        VectorAuthoringDocument Document;
        std::vector<PatternThumbnailShape> Shapes;
        bool Failed = false;
    };
    void ConfigureLibraryFilter(const std::string &Group, const std::string &Palette, bool ClearSearch);
    void ConfigureLibrarySearch(const std::string &Text);
    [[nodiscard]] std::vector<size_t> QueryLibraryMatches() const;
    [[nodiscard]] const PatternPresetCacheEntry &QueryPresetEntry(const PatternLibraryFamilyEntry &Family);
    [[nodiscard]] const std::vector<PatternLibraryFamilyEntry> &QueryLibraryFamilies() const noexcept { return LibraryFamilies; }
    [[nodiscard]] static std::vector<PatternThumbnailShape> TessellatePresetDocument(const VectorAuthoringDocument &Document);
    [[nodiscard]] const VectorAuthoringDocument &QueryDocument() const;
    [[nodiscard]] const std::vector<uint32_t> &QuerySelection() const noexcept { return Selection; }
    [[nodiscard]] const std::vector<size_t> &QueryAnchorSelection() const noexcept { return SelectedAnchors; }
    [[nodiscard]] uint64_t QueryRevision() const noexcept { return History.QueryRevision(); }
    [[nodiscard]] PanelDockspace &QueryPanels() noexcept { return Panels; }
    [[nodiscard]] PlaneExtent QueryControlExtent(const std::string &Caption) const;
    [[nodiscard]] PlaneExtent QueryPanelExtent(const std::string &Caption) const;
    [[nodiscard]] PlaneExtent QueryBoardExtent() const noexcept { return BoardExtent; }
    [[nodiscard]] bool QueryModified() const { return QueryDocument() != SavedDocument; }
    [[nodiscard]] bool QueryAuthoringSettled() const noexcept { return Gesture == 0 && !PenSource; }
    [[nodiscard]] const std::string &QueryDiagnostic() const noexcept { return Diagnostic; }
    bool RequestClose();
    bool ConsumeMaterialApplyRequest() noexcept;
    std::function<uint64_t(const VectorCoverageProgram &)> QueryCoverage;
    std::function<std::optional<uint32_t>(const VectorAuthoringDocument &, PlanePoint)> QuerySelectionToken;
    std::function<bool(std::string_view)> SaveSource;
    std::function<std::optional<std::string>()> LoadSource;
    std::function<void()> ActivateMaterial;
    std::function<void()> CancelClose;
    std::function<PlanePoint(PlanePoint)> EvaluateNavigationDelta;
private:
    enum class VectorAuthoringTool { Select, Rectangle, Ellipse, Diamond, Triangle, Polygon, Star, Pan, Points, Pen };
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
    std::vector<PatternLibraryFamilyEntry> LibraryFamilies;
    std::vector<std::string> LibraryGroups, LibraryPalettes;
    std::string LibraryDefaultPalette = "Indigo";
    std::filesystem::path LibraryDirectory;
    std::string LibraryGroup = "All patterns";
    std::string LibraryPalette;
    std::array<char, 129> LibrarySearchText{};
    size_t LibraryPage = 0;
    std::string LibrarySelected;
    std::map<std::string, PatternPresetCacheEntry> PresetCache;
    std::vector<std::string> PresetCacheOrder;
    std::optional<VectorAuthoringDocument> GestureSource, GestureDraft;
    std::optional<VectorAuthoringDocument> PenSource, PenPreview;
    CubicPath PendingPath;
    uint32_t PenToken = 0;
    size_t PenAnchorFloor = 0;
    bool PenReversed = false;
    bool IndependentPenContinuation = false;
    bool AutomaticPen = false;
    void ResumePen(uint32_t Token, bool FromStart);
    std::vector<uint32_t> PenSelection;
    void RefreshPenPreview();
    bool FinishPen(bool Closed);
    void CancelPen();
    bool ConstructPenInteraction(PlaneExtent Canvas, PlaneExtent Board, bool Hovered);
    CubicPath PointDragSource;
    std::vector<size_t> SelectedAnchors;
    CubicSegmentProjection SegmentProjection;
    bool SegmentBending = false;
    uint32_t PointToken = 0;
    size_t ActiveAnchor = SIZE_MAX;
    CubicControl ActiveControl = CubicControl::Anchor;
    bool ConstructPointInteraction(PlaneExtent Canvas, PlaneExtent Board, bool Hovered);
    VectorAuthoringArraySpecification ArraySpecification;
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
    bool GroupsOpen = false, ArraysOpen = false, ChannelsOpen = false, CloseRequested = false;
    bool TransparentCanvas = false, GridEnabled = false, SnappingEnabled = false;
    void *RegularTypeface = nullptr, *LightTypeface = nullptr;
    std::array<char, 81> NameText{};
    std::array<char, 129> SearchText{};
    std::string Diagnostic;
    void ConstructHeader();
    void ConstructLibrary();
    void ConstructLayers();
    void ConstructCanvas();
    void ConstructInspector();
    void ConstructCloseDialogue();
    void ConstructLibraryCard(const PatternLibraryFamilyEntry &Family, const PatternPresetCacheEntry &Entry, float Width, bool Selected);
    void ConstructPrimitive(uint32_t Category, const char *Caption);
    void ApplyLibraryPreset(size_t Index);
    void ApplyPresetDocument(VectorAuthoringDocument Document);
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
