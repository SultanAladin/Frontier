/*==========================================================================================================================================
                                                          CONTENTBROWSERPANEL.H
==========================================================================================================================================*/
// 🧩 Read-only editor content browsing through the shared directory catalogue and expansion tab.

#pragma once

#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/PanelExpansionTab.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include <array>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     CONTENT BROWSER
//------------------------------------------------------------------------------------------------------------------------

class ContentBrowserPanel final : public ActivationSequence
{
  public:
    [[nodiscard]] PlaneExtent QuerySizeExtent() const noexcept
    {
        return SizeExtent;
    }
    explicit ContentBrowserPanel(std::filesystem::path Root = "EngineContent");
    void ConstructInterface(float Scale, bool Enabled) noexcept;
    void SuspendInteraction() noexcept
    {
        Search.Active = CategoryMenu = Focused = false;
        PressedPath.clear();
    }
    void               ConstructHostedInterface(float Scale, bool Enabled) noexcept;
    void               ConfigureOpen(bool Open) noexcept;
    [[nodiscard]] bool QuerySearchActive() const noexcept
    {
        return Search.Active;
    }
    [[nodiscard]] bool QueryGrid() const noexcept
    {
        return Grid;
    }
    [[nodiscard]] ContentDirectory &QueryDirectory() noexcept
    {
        return Directory;
    }
    [[nodiscard]] const PanelExpansionTab &QueryExpansionTab() const noexcept
    {
        return Expansion;
    }
    [[nodiscard]] PlaneExtent QuerySearchExtent() const noexcept
    {
        return SearchExtent;
    }
    [[nodiscard]] PlaneExtent QueryFilterExtent() const noexcept
    {
        return FilterExtent;
    }
    [[nodiscard]] PlaneExtent QueryViewExtent() const noexcept
    {
        return ViewExtent;
    }
    [[nodiscard]] PlaneExtent QueryNavigationExtent(size_t Index) const noexcept
    {
        return Index < NavigationExtents.size() ? NavigationExtents[Index] : PlaneExtent{};
    }
    [[nodiscard]] PlaneExtent QueryCategoryExtent(size_t Index) const noexcept
    {
        return Index < CategoryExtents.size() ? CategoryExtents[Index] : PlaneExtent{};
    }
    [[nodiscard]] PlaneExtent QueryEntryExtent(size_t Index) const noexcept;
    [[nodiscard]] PlaneExtent QueryBodyExtent() const noexcept
    {
        return BodyExtent;
    }

  protected:
    bool InitializeContent() noexcept override;
    void IntegrateContent(double Δτ) noexcept override;
    void FinalizeContent() noexcept override;

  private:
    void ConstructLayout(PixelSpace &Surface, const PlaneExtent &Pane, const ControlPointer &Pointer);
    void ConstructEntries(PixelSpace &Surface, const PlaneExtent &View, const ControlPointer &Pointer);
    void ConstructSelection(PixelSpace &Surface, const PlaneExtent &Extent);
    void IntegrateKeyboard();
    void IntegrateSearch();
    void ResetLocation();
    [[nodiscard]] PlaneExtent PhysicalExtent(PlaneExtent Extent) const noexcept;

    std::filesystem::path      Root;
    ContentDirectory           Directory;
    PanelExpansionTab          Expansion{PanelExpansionDirectionCategory::Upward};
    TextEntryState             Search;
    ContentCategory            Category = ContentCategory::All;
    PlanePoint                 Origin{};
    float                      Scale              = 1;
    float                      Scroll             = 0;
    float                      CellWidth          = 132;
    float                      CellHeight         = 116;
    uint32_t                   Columns            = 1;
    bool                       Grid               = true;
    bool                       Maximized          = false;
    bool                       CategoryMenu       = false;
    bool                       Focused            = false;
    bool                       HeaderContact      = false;
    bool                       HostedPresentation = false;
    std::filesystem::path      PressedPath;
    PlaneExtent                SearchExtent, ViewExtent, BodyExtent, FilterExtent, SizeExtent;
    std::array<PlaneExtent, 5> NavigationExtents{};
    std::array<PlaneExtent, 7> CategoryExtents{};
    std::vector<PlaneExtent>   EntryExtents;
};
} // namespace Frontier
