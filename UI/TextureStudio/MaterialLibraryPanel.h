/*==========================================================================================================================================
                                                         MATERIALLIBRARYPANEL.H
==========================================================================================================================================*/
// 🧩 Share the native material catalogue and source editor between standalone Texture Studio and a future Frontier workspace.

#pragma once
#include "Engine/ContentInterchange/MaterialLibrary.h"
#include "Engine/Render/MaterialPreviewConfiguration.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/IconArt.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include <array>
#include <set>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     MATERIAL LIBRARY PANEL
//------------------------------------------------------------------------------------------------------------------------

struct MaterialPreviewImage
{
    uint64_t Texture = 0;
    float MinimumU = 0, MinimumV = 0, MaximumU = 1, MaximumV = 1;
};

class MaterialLibraryPanel final : public ActivationSequence
{
  public:
    MaterialLibraryPanel();
    bool ConfigureDirectory(const std::filesystem::path &Directory);
    bool SelectMaterial(const std::filesystem::path &RelativePath);
    void ConstructInterface();
    void ReplaceSource(std::string Source);
    void ConfigureInterfaceScale(float Scale) noexcept;
    void ConfigureTypeface(void *Typeface, void *Light = nullptr) noexcept { ReferenceTypeface = Typeface; HeadingTypeface = Light; }
    void ConfigureSourceView(bool Visible) noexcept { SourceVisible = Visible; }
    void ConfigurePlatform() { (void)Panels.ConfigurePlatform(); }
    [[nodiscard]] PanelDockspace &QueryPanels() noexcept { return Panels; }
    std::function<MaterialPreviewImage(const std::filesystem::path &)> QueryThumbnail;
    std::function<MaterialPreviewImage()> QueryPreview;
    std::function<PlanePoint(PlanePoint)> EvaluateNavigationDelta;
    std::function<void(const MaterialPreviewConfiguration &)> ConfigurePreview;
    [[nodiscard]] MaterialPreviewConfiguration QueryPreviewConfiguration() const noexcept { return Inspection; }
    [[nodiscard]] PlaneExtent QueryPreviewControlExtent(size_t Index) const noexcept
    {
        return Index < InspectionExtents.size() ? InspectionExtents[Index] : PlaneExtent{};
    }
    std::function<bool(IconSymbol, PlanePoint, float, ColorQuad)> PresentIcon;
    std::function<std::string(const std::filesystem::path &)> QueryThumbnailDiagnostic;
    std::function<void()> InvalidateThumbnails;
    std::function<void()> ActivatePattern;
    std::function<void(const std::vector<std::filesystem::path> &)> RequestThumbnails;
    bool RequestClose() noexcept;
    bool ConsumeClose() noexcept;
    [[nodiscard]] PlaneExtent QueryApplyExtent() const noexcept { return ApplyExtent; }
    [[nodiscard]] PlaneExtent QueryFullscreenPreviewExtent() const noexcept { return FullscreenPreviewExtent; }
    [[nodiscard]] bool QueryFullscreenPreview() const noexcept { return FullscreenPreview; }
    [[nodiscard]] PlaneExtent QuerySaveExtent() const noexcept { return SaveExtent; }
    [[nodiscard]] PlaneExtent QueryRoughnessExtent() const noexcept { return RoughnessExtent; }
    [[nodiscard]] PlaneExtent QueryRevertExtent() const noexcept { return RevertExtent; }
    [[nodiscard]] MaterialLibrary &QueryLibrary() noexcept { return Library; }

  private:
    std::array<PlaneExtent, 8> InspectionExtents{};
    MaterialPreviewConfiguration Inspection;
    MaterialLibrary Library;
    PanelDockspace Panels;
    bool SourceVisible = false;
    bool CollectionVisible = false, LayersVisible = false;
    bool FullscreenPreview = false;
    std::string CategoryFilter = "All materials";
    std::set<std::string> Favourites;
    std::set<std::string> FoldedInspectorCards;
    float InterfaceScale = 1;
    void *ReferenceTypeface = nullptr;
    void *HeadingTypeface = nullptr;
    [[nodiscard]] float QueryInterfaceScale() const noexcept;
    bool DirectoryRequested = false;
    PlaneExtent RoughnessExtent;
    void ConstructInspector();
    void ConstructViewport();
    void ConstructFullscreenPreview();
    bool ConstructProperty(const char *Caption, float Minimum, float Maximum, float &Position, const char *Unit = "%", bool Logarithmic = false, float Step = 0, const char *Unavailable = nullptr);
    void ConstructIcon(IconSymbol Symbol, float X, float Y, float Size, uint32_t Rgb = 0xa3a393);
    void ConstructActions();
    std::array<char,4096> DirectoryText{}, DestinationText{};
    std::array<char,257> SearchText{}, NameText{};
    std::array<char,32> ColourText{};
    std::vector<char> SourceText;
    PlaneExtent ApplyExtent, SaveExtent, RevertExtent, FullscreenPreviewExtent;
    bool ClosingRequested = false, CloseAuthorized = false;
    bool Creating = false;
    bool Duplicating = false;
    void SynchronizeSource();
    void ConstructCatalogue();
    void ConstructSourceEditor();
    void ConstructCreation();
    void FinalizeContent() noexcept override;
};
} // namespace Frontier
