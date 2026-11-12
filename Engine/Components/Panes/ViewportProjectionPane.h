/*============================================================================================================================================
                                                        VIEWPORTPROJECTIONPANE.H
============================================================================================================================================*/
// 🧩 Reusable PixelSpace single/split 2D+3D GPU viewport pane bound to WorkspaceDocument & blitted via PixelSpace::FillTexturedRectangle

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Editor/Viewports/ViewportProjection.h"
#include "Engine/Workspaces/WorkspaceDocument.h"

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ViewportToolbarChip
{
    std::string           Caption      {};            // [-] - Button label rendered in top projection strip
    bool                  PrimaryStyle = false;       // [-] - Highlighted action capsule when true
    std::function<void()> OnActivated  {};            // [-] - Invoked on primary click
};

struct ViewportFramebufferSlice
{
    const uint8_t* RgbaPixels = nullptr; // [-]  - Row-major RGBA8 GPU framebuffer bytes
    uint32_t       WidthPx    = 0u;      // [px] - Framebuffer pixel width
    uint32_t       HeightPx   = 0u;      // [px] - Framebuffer pixel height
};

struct DualViewportFramebuffers
{
    ViewportFramebufferSlice Spatial3D {}; // [-] - 3D spatial framebuffer slice
    ViewportFramebufferSlice Planar2D  {}; // [-] - 2D planar/tile framebuffer slice
};

struct ViewportInteractionFrame
{
    Rectangle2D             PlanarBounds  {}; // [px] - Logical rectangle of 2D planar canvas (0 width when hidden)
    Rectangle2D             SpatialBounds {}; // [px] - Logical rectangle of 3D spatial viewport (0 width when hidden)
    const PaneInputReading& Input;            // [-]  - Current frame pointer, wheel, modifier & keyboard reading
};

class ViewportProjectionPane
{
public:
    using FramebufferSyncCallback = std::function<DualViewportFramebuffers(
        uint32_t SpatialWidthPx,
        uint32_t SpatialHeightPx,
        uint32_t PlanarWidthPx,
        uint32_t PlanarHeightPx)>;

    using SplitModeChangedCallback    = std::function<void(Editor::ViewportSplitMode NewMode)>;
    using ViewportInteractionCallback = std::function<void(const ViewportInteractionFrame& Frame)>;

    ViewportProjectionPane();
    ~ViewportProjectionPane();

    ViewportProjectionPane(const ViewportProjectionPane&)            = delete;
    ViewportProjectionPane& operator=(const ViewportProjectionPane&) = delete;

    void AttachDocument(Workspaces::WorkspaceDocument* TargetDocument) noexcept;
    void ConfigurePaneBadges(std::string_view PlanarBadge, std::string_view SpatialBadge);
    void ConfigureToolbarChips(std::vector<ViewportToolbarChip> Chips);
    void ConfigureStatusBanner(std::string_view BannerText);
    void AssignFramebufferProvider(FramebufferSyncCallback Provider);
    void AssignSplitModeCallback(SplitModeChangedCallback Callback);
    void AssignInteractionCallback(ViewportInteractionCallback Callback);

    [[nodiscard]] Editor::ViewportProjection&       AccessProjection() noexcept;
    [[nodiscard]] const Editor::ViewportProjection& InspectProjection() const noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

private:
    static void SyncTextureData(ImTextureData& TargetTex, const ViewportFramebufferSlice& Source);

    Workspaces::WorkspaceDocument*   BoundDocument        = nullptr;
    Editor::ViewportProjection       FallbackProjection   {};
    std::string                      PlanarBadgeTitle     { "2D PLANAR CANVAS + GIZMO" };
    std::string                      SpatialBadgeTitle    { "3D PROJECTION + MATCAP + GIZMO" };
    std::string                      StatusBanner         {};
    std::vector<ViewportToolbarChip> DomainChips          {};
    FramebufferSyncCallback          FramebufferProvider  {};
    SplitModeChangedCallback         SplitChangedNotifier {};
    ViewportInteractionCallback      InteractionCallback  {};
    ImTextureData                    PlanarTexture        {};
    ImTextureData                    SpatialTexture       {};
};

} // namespace Frontier
