/*============================================================================================================================================
                                                       VIEWPORTPROJECTIONPANE.CPP
============================================================================================================================================*/
// 🧩 Implementation of reusable ViewportProjectionPane presenting WorkspaceDocument 2D/3D GPU views via PixelSpace::FillTexturedRectangle

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/ViewportProjectionPane.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace Frontier
{

ViewportProjectionPane::ViewportProjectionPane() = default;

ViewportProjectionPane::~ViewportProjectionPane()
{
    PlanarTexture.DestroyPixels();
    SpatialTexture.DestroyPixels();
}

void ViewportProjectionPane::AttachDocument(Workspaces::WorkspaceDocument* TargetDocument) noexcept
{
    BoundDocument = TargetDocument;
}

void ViewportProjectionPane::ConfigurePaneBadges(std::string_view PlanarBadge, std::string_view SpatialBadge)
{
    PlanarBadgeTitle  = std::string(PlanarBadge);
    SpatialBadgeTitle = std::string(SpatialBadge);
}

void ViewportProjectionPane::ConfigureToolbarChips(std::vector<ViewportToolbarChip> Chips)
{
    DomainChips = std::move(Chips);
}

void ViewportProjectionPane::ConfigureStatusBanner(std::string_view BannerText)
{
    StatusBanner = std::string(BannerText);
}

void ViewportProjectionPane::AssignFramebufferProvider(FramebufferSyncCallback Provider)
{
    FramebufferProvider = std::move(Provider);
}

void ViewportProjectionPane::AssignSplitModeCallback(SplitModeChangedCallback Callback)
{
    SplitChangedNotifier = std::move(Callback);
}

void ViewportProjectionPane::AssignInteractionCallback(ViewportInteractionCallback Callback)
{
    InteractionCallback = std::move(Callback);
}

Editor::ViewportProjection& ViewportProjectionPane::AccessProjection() noexcept
{
    return BoundDocument != nullptr ? BoundDocument->AccessViewportProjection() : FallbackProjection;
}

const Editor::ViewportProjection& ViewportProjectionPane::InspectProjection() const noexcept
{
    return BoundDocument != nullptr ? BoundDocument->InspectViewportProjection() : FallbackProjection;
}

void ViewportProjectionPane::SyncTextureData(ImTextureData& TargetTex, const ViewportFramebufferSlice& Source)
{
    if (Source.RgbaPixels == nullptr || Source.WidthPx == 0u || Source.HeightPx == 0u)
    {
        return;
    }
    if (TargetTex.Width != static_cast<int>(Source.WidthPx) ||
        TargetTex.Height != static_cast<int>(Source.HeightPx) ||
        TargetTex.Pixels == nullptr)
    {
        TargetTex.DestroyPixels();
        TargetTex.SetStatus(ImTextureStatus_Destroyed);
        TargetTex.Create(ImTextureFormat_RGBA32, static_cast<int>(Source.WidthPx), static_cast<int>(Source.HeightPx));
    }
    std::memcpy(TargetTex.Pixels, Source.RgbaPixels, static_cast<std::size_t>(Source.WidthPx) * Source.HeightPx * 4u);
    TargetTex.SetStatus(ImTextureStatus_OK);
}

void ViewportProjectionPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    (void)Δt;
    constexpr ColorRgba HostBg     { 15, 16, 19, 255 };
    constexpr ColorRgba BarBg      { 23, 25, 30, 255 };
    constexpr ColorRgba ChipBg     { 31, 34, 42, 255 };
    constexpr ColorRgba ChipActive { 46, 82, 144, 255 };
    constexpr ColorRgba BorderCol  { 48, 52, 62, 255 };
    constexpr ColorRgba IvoryText  { 239, 244, 207, 255 };
    constexpr ColorRgba CrispText  { 228, 232, 240, 255 };
    constexpr ColorRgba EmeraldOk  { 94, 212, 148, 255 };

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, HostBg);

    auto&         Projection    = AccessProjection();
    const bool    Clicked       = Input.PrimaryPointer.Pressed;
    const Point2D Cursor        { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    bool          ToolbarCaught = false;

    // 1. Reusable Top Projection & Domain Action Strip
    const Rectangle2D ToolbarRect { Bounds.X + 8.0f, Bounds.Y + 8.0f, std::max(80.0f, Bounds.Width - 16.0f), 36.0f };
    Surface.FillRectangle(ToolbarRect, BarBg, 6.0f);
    Surface.StrokeRectangle(ToolbarRect, BorderCol, 6.0f, 1.0f);

    float       CursorX = ToolbarRect.X + 8.0f;
    const float ChipY   = ToolbarRect.Y + 5.0f;

    constexpr std::array<const char*, 3u> SplitLabels {{ "Split 2D+3D", "2D Planar", "3D Viewport" }};
    for (uint32_t Idx = 0u; Idx < 3u; ++Idx)
    {
        const Rectangle2D LRect  { CursorX, ChipY, 84.0f, 26.0f };
        const auto        Mode   = static_cast<Editor::ViewportSplitMode>(Idx);
        const bool        Active = (Projection.QuerySplitMode() == Mode);
        Surface.FillRectangle(LRect, Active ? ChipActive : ChipBg, 5.0f);
        Surface.StrokeRectangle(LRect, Active ? IvoryText : BorderCol, 5.0f, 1.0f);
        Surface.TextCentered(LRect, 11.0f, Active ? IvoryText : CrispText, SplitLabels[Idx]);
        if (Clicked && LRect.Contains(Cursor))
        {
            ToolbarCaught = true;
            Projection.ConfigureSplitMode(Mode);
            if (SplitChangedNotifier)
            {
                SplitChangedNotifier(Mode);
            }
        }
        CursorX += 88.0f;
    }

    if (!DomainChips.empty())
    {
        CursorX += 10.0f;
        for (const auto& Action : DomainChips)
        {
            const Rectangle2D QRect { CursorX, ChipY, 78.0f, 26.0f };
            Surface.FillRectangle(QRect, Action.PrimaryStyle ? ChipActive : ChipBg, 13.0f);
            Surface.StrokeRectangle(QRect, BorderCol, 13.0f, 1.0f);
            Surface.TextCentered(QRect, 11.0f, IvoryText, Action.Caption.c_str());
            if (Clicked && QRect.Contains(Cursor))
            {
                ToolbarCaught = true;
                if (Action.OnActivated)
                {
                    Action.OnActivated();
                }
            }
            CursorX += 84.0f;
        }
    }

    // 2. Partition Single or Dual Viewport Regions
    const float ContentTop = ToolbarRect.Bottom() + 8.0f;
    const float ContentH   = std::max(64.0f, Bounds.Bottom() - ContentTop - 8.0f);
    const float ContentW   = std::max(64.0f, Bounds.Width - 16.0f);
    const float ContentX   = Bounds.X + 8.0f;

    Rectangle2D Rect2D {};
    Rectangle2D Rect3D {};

    switch (Projection.QuerySplitMode())
    {
        case Editor::ViewportSplitMode::SplitPlanarAndSpatial:
        {
            const float LeftW = std::floor((ContentW - 8.0f) * Projection.QuerySplitRatio());
            Rect2D = { ContentX, ContentTop, LeftW, ContentH };
            Rect3D = { ContentX + LeftW + 8.0f, ContentTop, ContentW - LeftW - 8.0f, ContentH };
            break;
        }
        case Editor::ViewportSplitMode::Planar2DOnly:
            Rect2D = { ContentX, ContentTop, ContentW, ContentH };
            break;
        case Editor::ViewportSplitMode::Spatial3DOnly:
            Rect3D = { ContentX, ContentTop, ContentW, ContentH };
            break;
    }

    // 3. Dispatch Interactive Camera, GPU Pick & TransformGizmo Input Before Framebuffer Refresh
    if (!ToolbarCaught && InteractionCallback)
    {
        InteractionCallback(ViewportInteractionFrame{ Rect2D, Rect3D, Input });
    }

    if (!StatusBanner.empty() && ToolbarRect.Right() - CursorX > 140.0f)
    {
        Surface.TextRightAligned(
            { CursorX, ToolbarRect.Y + 9.0f, ToolbarRect.Right() - CursorX - 12.0f, 18.0f },
            11.0f,
            EmeraldOk,
            StatusBanner.c_str());
    }

    DualViewportFramebuffers Slices {};
    if (FramebufferProvider)
    {
        Slices = FramebufferProvider(
            static_cast<uint32_t>(std::max(0.0f, Rect3D.Width)),
            static_cast<uint32_t>(std::max(0.0f, Rect3D.Height)),
            static_cast<uint32_t>(std::max(0.0f, Rect2D.Width)),
            static_cast<uint32_t>(std::max(0.0f, Rect2D.Height)));
    }

    // 4. Present GPU Framebuffers via upstream PixelSpace::FillTexturedRectangle
    if (Rect2D.Width >= 16.0f && Rect2D.Height >= 16.0f && Slices.Planar2D.RgbaPixels != nullptr)
    {
        SyncTextureData(PlanarTexture, Slices.Planar2D);
        Surface.FillTexturedRectangle(Rect2D, &PlanarTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 5.0f);
        Surface.StrokeRectangle(Rect2D, BorderCol, 5.0f, 1.2f);
        const float BadgeW = std::max(140.0f, Surface.MeasureTextWidth(10.5f, PlanarBadgeTitle.c_str()) + 22.0f);
        Surface.FillRectangle({ Rect2D.X + 10.0f, Rect2D.Y + 10.0f, BadgeW, 22.0f }, { 18, 20, 25, 215 }, 4.0f);
        Surface.Text({ Rect2D.X + 18.0f, Rect2D.Y + 14.0f }, 10.5f, IvoryText, PlanarBadgeTitle.c_str());
    }

    if (Rect3D.Width >= 16.0f && Rect3D.Height >= 16.0f && Slices.Spatial3D.RgbaPixels != nullptr)
    {
        SyncTextureData(SpatialTexture, Slices.Spatial3D);
        Surface.FillTexturedRectangle(Rect3D, &SpatialTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 5.0f);
        Surface.StrokeRectangle(Rect3D, BorderCol, 5.0f, 1.2f);
        const float BadgeW = std::max(160.0f, Surface.MeasureTextWidth(10.5f, SpatialBadgeTitle.c_str()) + 22.0f);
        Surface.FillRectangle({ Rect3D.X + 10.0f, Rect3D.Y + 10.0f, BadgeW, 22.0f }, { 18, 20, 25, 215 }, 4.0f);
        Surface.Text({ Rect3D.X + 18.0f, Rect3D.Y + 14.0f }, 10.5f, IvoryText, SpatialBadgeTitle.c_str());
    }

    Surface.PopClip();
}

void ViewportProjectionPane::PresentInImGuiPane(double Δt, float ScaleFactor)
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##ViewportProjectionPaneCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)));

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace             WindowSurface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    const PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    Render(WindowSurface, LogicalBounds, Input, Δt);
}

} // namespace Frontier
