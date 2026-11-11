/*============================================================================================================================================
                                                           UVVIEWPORTHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVViewportHost presenting UVOperationToolbar + live Vulkan 2D UVTileCanvas & 3D ProjectionViewport

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVViewportHost.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace Frontier::Workspaces
{

UVViewportHost::UVViewportHost()  = default;
UVViewportHost::~UVViewportHost()
{
    Texture3D.DestroyPixels();
    Texture2D.DestroyPixels();
}

void UVViewportHost::AttachSession(UVWorkspaceSession* TargetSession) noexcept
{
    Session = TargetSession;
}

void UVViewportHost::SyncImGuiTexture(
    ImTextureData&              TexData,
    const std::vector<uint8_t>& RgbaBytes,
    uint32_t                    WidthPx,
    uint32_t                    HeightPx,
    uint64_t                    TexIdTag)
{
    if (WidthPx == 0u || HeightPx == 0u || RgbaBytes.size() < static_cast<std::size_t>(WidthPx) * HeightPx * 4u)
    {
        return;
    }
    if (TexData.Width != static_cast<int>(WidthPx) || TexData.Height != static_cast<int>(HeightPx) || TexData.Pixels == nullptr)
    {
        TexData.DestroyPixels();
        TexData.SetStatus(ImTextureStatus_Destroyed);
        TexData.Create(ImTextureFormat_RGBA32, static_cast<int>(WidthPx), static_cast<int>(HeightPx));
    }
    std::memcpy(TexData.Pixels, RgbaBytes.data(), static_cast<std::size_t>(WidthPx) * HeightPx * 4u);
    TexData.SetStatus(ImTextureStatus_OK);
    (void)TexIdTag;
}

void UVViewportHost::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    (void)Δt;
    const ColorRgba HostBg     { 15, 16, 19, 255 };
    const ColorRgba BarBg      { 23, 25, 30, 255 };
    const ColorRgba ChipBg     { 31, 34, 42, 255 };
    const ColorRgba ChipActive { 46, 82, 144, 255 };
    const ColorRgba BorderCol  { 48, 52, 62, 255 };
    const ColorRgba IvoryText  { 239, 244, 207, 255 };
    const ColorRgba CrispText  { 228, 232, 240, 255 };
    const ColorRgba EmeraldOk  { 94, 212, 148, 255 };

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, HostBg);

    if (Session == nullptr)
    {
        Surface.PopClip();
        return;
    }

    auto&       Prefs   = Session->WorkspaceConfig.AccessUVPreferences();
    const bool    Clicked = Input.PrimaryPointer.Pressed;
    const Point2D Cur     { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    // 1. Top UVOperationToolbar (38 px)
    const Rectangle2D ToolbarRect { Bounds.X + 8.0f, Bounds.Y + 8.0f, std::max(80.0f, Bounds.Width - 16.0f), 36.0f };
    Surface.FillRectangle(ToolbarRect, BarBg, 6.0f);
    Surface.StrokeRectangle(ToolbarRect, BorderCol, 6.0f, 1.0f);

    float CursorX = ToolbarRect.X + 8.0f;
    const float ChipY = ToolbarRect.Y + 5.0f;

    // Layout Mode Switcher: Split 2D+3D | 2D UV | 3D View
    const std::array<const char*, 3u> LayoutNames {{ "Split 2D+3D", "2D UVTile", "3D Viewport" }};
    for (uint32_t Idx = 0u; Idx < 3u; ++Idx)
    {
        const Rectangle2D LRect { CursorX, ChipY, 84.0f, 26.0f };
        const bool Active = (static_cast<uint32_t>(Prefs.InitialViewportLayout) == Idx);
        Surface.FillRectangle(LRect, Active ? ChipActive : ChipBg, 5.0f);
        Surface.StrokeRectangle(LRect, Active ? IvoryText : BorderCol, 5.0f, 1.0f);
        Surface.TextCentered(LRect, 11.0f, Active ? IvoryText : CrispText, LayoutNames[Idx]);
        if (Clicked && LRect.Contains(Cur))
        {
            Prefs.InitialViewportLayout = static_cast<Configuration::UVViewportLayoutMode>(Idx);
            Session->FramebuffersDirty  = true;
        }
        CursorX += 88.0f;
    }

    CursorX += 10.0f;
    const std::array<const char*, 3u> QuickOps {{ "Unwrap", "Relax GPU", "Pack UVs" }};
    const std::array<UVCommandDirective, 3u> QuickCmds {{
        UVCommandDirective::UnwrapConformal,
        UVCommandDirective::RelaxStretchGPU,
        UVCommandDirective::PackUVsConcaveSkyline,
    }};
    for (uint32_t Idx = 0u; Idx < 3u; ++Idx)
    {
        const Rectangle2D QRect { CursorX, ChipY, 76.0f, 26.0f };
        Surface.FillRectangle(QRect, Idx == 2u ? ChipActive : ChipBg, 13.0f);
        Surface.StrokeRectangle(QRect, BorderCol, 13.0f, 1.0f);
        Surface.TextCentered(QRect, 11.0f, IvoryText, QuickOps[Idx]);
        if (Clicked && QRect.Contains(Cur))
        {
            Session->QueueDirective(QuickCmds[Idx]);
        }
        CursorX += 82.0f;
    }

    // Status Telemetry Readout on Right Side of Toolbar
    if (ToolbarRect.Right() - CursorX > 140.0f)
    {
        Surface.TextRightAligned(
            { CursorX, ToolbarRect.Y + 9.0f, ToolbarRect.Right() - CursorX - 12.0f, 18.0f },
            11.0f,
            EmeraldOk,
            Session->StatusBannerText.c_str());
    }

    // 2. Central Viewport Split Region
    const float ContentTop = ToolbarRect.Bottom() + 8.0f;
    const float ContentH   = std::max(64.0f, Bounds.Bottom() - ContentTop - 8.0f);
    const float ContentW   = std::max(64.0f, Bounds.Width - 16.0f);
    const float ContentX   = Bounds.X + 8.0f;

    Rectangle2D Rect2D {};
    Rectangle2D Rect3D {};

    if (Prefs.InitialViewportLayout == Configuration::UVViewportLayoutMode::Split2DAnd3D)
    {
        const float HalfW = std::floor((ContentW - 8.0f) * Prefs.SplitRatioNormalized);
        Rect2D = { ContentX, ContentTop, HalfW, ContentH };
        Rect3D = { ContentX + HalfW + 8.0f, ContentTop, ContentW - HalfW - 8.0f, ContentH };
    }
    else if (Prefs.InitialViewportLayout == Configuration::UVViewportLayoutMode::UVTile2DOnly)
    {
        Rect2D = { ContentX, ContentTop, ContentW, ContentH };
    }
    else
    {
        Rect3D = { ContentX, ContentTop, ContentW, ContentH };
    }

    Session->RefreshViewportFramebuffers(
        static_cast<uint32_t>(std::max(0.0f, Rect3D.Width)),
        static_cast<uint32_t>(std::max(0.0f, Rect3D.Height)),
        static_cast<uint32_t>(std::max(0.0f, Rect2D.Width)),
        static_cast<uint32_t>(std::max(0.0f, Rect2D.Height)));

    ImDrawList* RawList = Surface.QueryCanvasBuffer();

    if (Rect2D.Width >= 16.0f && Rect2D.Height >= 16.0f)
    {
        SyncImGuiTexture(Texture2D, Session->Framebuffer2DRGBA, Session->Framebuffer2DWidth, Session->Framebuffer2DHeight, 0xF202D001ull);
        const Rectangle2D Phys2D = Surface.ToPhysical(Rect2D);
        if (RawList != nullptr && Texture2D.Pixels != nullptr)
        {
            ImTextureRef Ref2D {};
            Ref2D._TexData = &Texture2D;
            RawList->AddImage(
                Ref2D,
                ImVec2(Phys2D.X, Phys2D.Y),
                ImVec2(Phys2D.Right(), Phys2D.Bottom()));
        }
        Surface.StrokeRectangle(Rect2D, BorderCol, 5.0f, 1.2f);
        Surface.FillRectangle({ Rect2D.X + 10.0f, Rect2D.Y + 10.0f, 178.0f, 22.0f }, { 18, 20, 25, 215 }, 4.0f);
        Surface.Text({ Rect2D.X + 18.0f, Rect2D.Y + 14.0f }, 10.5f, IvoryText, "2D UVTILE CANVAS + 2D GIZMO");
    }

    if (Rect3D.Width >= 16.0f && Rect3D.Height >= 16.0f)
    {
        SyncImGuiTexture(Texture3D, Session->Framebuffer3DRGBA, Session->Framebuffer3DWidth, Session->Framebuffer3DHeight, 0xF303D002ull);
        const Rectangle2D Phys3D = Surface.ToPhysical(Rect3D);
        if (RawList != nullptr && Texture3D.Pixels != nullptr)
        {
            ImTextureRef Ref3D {};
            Ref3D._TexData = &Texture3D;
            RawList->AddImage(
                Ref3D,
                ImVec2(Phys3D.X, Phys3D.Y),
                ImVec2(Phys3D.Right(), Phys3D.Bottom()));
        }
        Surface.StrokeRectangle(Rect3D, BorderCol, 5.0f, 1.2f);
        Surface.FillRectangle({ Rect3D.X + 10.0f, Rect3D.Y + 10.0f, 214.0f, 22.0f }, { 18, 20, 25, 215 }, 4.0f);
        Surface.Text({ Rect3D.X + 18.0f, Rect3D.Y + 14.0f }, 10.5f, IvoryText, "3D PROJECTION + 16 MATCAP + GIZMO");
    }

    Surface.PopClip();
}

void UVViewportHost::PresentInImGuiPane(double Δt, float ScaleFactor)
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

    ImGui::InvisibleButton("##UVViewportHostCanvas", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)));

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace             WindowSurface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    const PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    Render(WindowSurface, LogicalBounds, Input, Δt);
}

} // namespace Frontier::Workspaces
