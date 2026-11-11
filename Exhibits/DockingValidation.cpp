/*============================================================================================================================================
                                                     DOCKINGVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end validation and CPU ImDrawData visual proof generator for shared Trapezoidal Docking Panels

#include "Engine/Components/Docking/DockingPanel.h"
#include "Exhibits/VisualProofRasterizer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <filesystem>

namespace
{

using Frontier::Components::ApplicationDockDomainToken;
using Frontier::Components::ConfigureImGuiTrapezoidalStyle;
using Frontier::Components::ConstructProjectZeroTabMetrics;
using Frontier::Components::ConstructTrapezoidalContour;
using Frontier::Components::DockingPanelDescriptor;
using Frontier::Components::DockingPanelToken;
using Frontier::Components::DockPlacementDirection;
using Frontier::Components::DockTransferVerdict;
using Frontier::Components::FoldDomainToImGuiClassId;
using Frontier::Components::HeadlessTrapezoidalDockCoordinator;
using Frontier::Components::HostWindowToken;
using Frontier::Components::IssueApplicationDockDomain;
using Frontier::Components::PanelDirectiveMask;
using Frontier::Components::QueryPointInsideContour;
using Frontier::Components::RgbaColor8;
using Frontier::Components::ScreenPoint2D;
using Frontier::Components::TrapezoidalTabMetrics;
using Frontier::Exhibits::LoadProjectZeroDmSansFont;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareProofCanvas;
using Frontier::Windowing::PlatformWindowSurface;
using Frontier::Windowing::WindowConfigurationDescriptor;

bool VerifyAssertion(bool Condition, const char* Description) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[DockingValidation] FAILED: %s\n", Description);
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       PROJECT-ZERO PANEL INTERIOR PRESENTERS
//------------------------------------------------------------------------------------------------------------------------

void PresentOutlinerInterior()
{
    ImGui::Spacing();
    ImGui::Columns(2, "##OutlinerCounters", false);
    ImGui::TextColored(ImVec4(0.56f, 0.56f, 0.60f, 1.0f), "Visible");
    ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.95f, 1.0f), "11");
    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.56f, 0.56f, 0.60f, 1.0f), "Hidden");
    ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.95f, 1.0f), "0");
    ImGui::Columns(1);

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.62f, 0.62f, 0.67f, 1.0f), "Search entities...");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.93f, 0.93f, 0.96f, 1.0f), "v  Showcase                    4");
    ImGui::Indent(14.0f);
    ImGui::TextColored(ImVec4(0.84f, 0.84f, 0.88f, 1.0f), "Hero Cube");
    ImGui::TextColored(ImVec4(0.84f, 0.84f, 0.88f, 1.0f), "Reflective Sphere");
    ImGui::TextColored(ImVec4(0.84f, 0.84f, 0.88f, 1.0f), "Plinth");
    ImGui::TextColored(ImVec4(0.84f, 0.84f, 0.88f, 1.0f), "Ground");
    ImGui::Unindent(14.0f);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.93f, 0.93f, 0.96f, 1.0f), "v  Cameras                     2");
    ImGui::Indent(14.0f);
    ImGui::TextColored(ImVec4(0.84f, 0.84f, 0.88f, 1.0f), "Main Camera");
    ImGui::TextColored(ImVec4(0.84f, 0.84f, 0.88f, 1.0f), "Detail Camera");
    ImGui::Unindent(14.0f);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.93f, 0.93f, 0.96f, 1.0f), "v  World                       5");
    ImGui::Indent(14.0f);
    ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.04f, 1.0f), "* Sun  [Selected]");
    ImGui::TextColored(ImVec4(0.84f, 0.84f, 0.88f, 1.0f), "Environment");
    ImGui::TextColored(ImVec4(0.84f, 0.84f, 0.88f, 1.0f), "Key Rim");
    ImGui::TextColored(ImVec4(0.84f, 0.84f, 0.88f, 1.0f), "Ground Fog");
    ImGui::TextColored(ImVec4(0.84f, 0.84f, 0.88f, 1.0f), "Prevailing Wind");
    ImGui::Unindent(14.0f);
}

void PresentViewportInterior()
{
    ImDrawList*   DrawList = ImGui::GetWindowDrawList();
    const ImVec2  Origin   = ImGui::GetCursorScreenPos();
    const ImVec2  Avail    = ImGui::GetContentRegionAvail();
    const ImVec2  LowerMax(Origin.x + Avail.x, Origin.y + Avail.y);

    // Subtle dark viewport backdrop
    DrawList->AddRectFilledMultiColor(
        Origin,
        LowerMax,
        IM_COL32(18, 22, 30, 255),
        IM_COL32(18, 22, 30, 255),
        IM_COL32(11, 11, 13, 255),
        IM_COL32(11, 11, 13, 255));

    // Top trapezoidal Project-Zero Control Centre notch (inverted trapezoid!)
    const auto Metrics    = ConstructProjectZeroTabMetrics(1.0f);
    const float NotchW    = 148.0f;
    const float NotchH    = 24.0f;
    const float NotchX    = Origin.x + (Avail.x - NotchW) * 0.5f;
    const auto TopNotch   = ConstructTrapezoidalContour(NotchX, Origin.y, NotchW, NotchH, Metrics, true);
    if (TopNotch.VertexCount >= 4)
    {
        std::vector<ImVec2> Poly;
        Poly.reserve(TopNotch.VertexCount);
        for (std::size_t Idx = 0; Idx < TopNotch.VertexCount; ++Idx)
        {
            Poly.emplace_back(TopNotch.Vertices[Idx].X, TopNotch.Vertices[Idx].Y);
        }
        DrawList->AddConvexPolyFilled(Poly.data(), static_cast<int>(Poly.size()), IM_COL32(24, 24, 28, 255));
        DrawList->AddPolyline(Poly.data(), static_cast<int>(Poly.size()), IM_COL32(46, 46, 52, 255), ImDrawFlags_Closed, 1.0f);
        DrawList->AddText(ImVec2(NotchX + 32.0f, Origin.y + 3.0f), IM_COL32(215, 215, 222, 255), "Project-Zero");
    }

    // Bottom trapezoidal Asset Browser pull-up tab (upright trapezoid!)
    const float DrawerTabW = 156.0f;
    const float DrawerTabH = 24.0f;
    const float DrawerTabX = Origin.x + (Avail.x - DrawerTabW) * 0.5f;
    const float DrawerTabY = LowerMax.y - DrawerTabH;
    const auto BottomTab   = ConstructTrapezoidalContour(DrawerTabX, DrawerTabY, DrawerTabW, DrawerTabH, Metrics, false);
    if (BottomTab.VertexCount >= 4)
    {
        std::vector<ImVec2> Poly;
        Poly.reserve(BottomTab.VertexCount);
        for (std::size_t Idx = 0; Idx < BottomTab.VertexCount; ++Idx)
        {
            Poly.emplace_back(BottomTab.Vertices[Idx].X, BottomTab.Vertices[Idx].Y);
        }
        DrawList->AddConvexPolyFilled(Poly.data(), static_cast<int>(Poly.size()), IM_COL32(24, 24, 28, 255));
        DrawList->AddPolyline(Poly.data(), static_cast<int>(Poly.size()), IM_COL32(46, 46, 52, 255), ImDrawFlags_Closed, 1.0f);
        DrawList->AddText(ImVec2(DrawerTabX + 30.0f, DrawerTabY + 3.0f), IM_COL32(215, 215, 222, 255), "Asset Browser");
    }

    // Center scene preview label
    DrawList->AddText(
        ImVec2(Origin.x + 18.0f, Origin.y + 36.0f),
        IM_COL32(138, 142, 152, 255),
        "GPU .slang Viewport Surface  |  Perspective 50 mm  |  16.67 ms (60 FPS)");
}

void PresentInspectorInterior()
{
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.96f, 0.78f, 0.32f, 1.0f), "Sun");
    ImGui::TextColored(ImVec4(0.56f, 0.56f, 0.60f, 1.0f), "World  |  Directional Solar Emitter");
    ImGui::Separator();
    ImGui::Spacing();

    ImDrawList*  DrawList  = ImGui::GetWindowDrawList();
    const ImVec2 BannerMin = ImGui::GetCursorScreenPos();
    const float  BannerW   = std::max(120.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 BannerMax(BannerMin.x + BannerW, BannerMin.y + 84.0f);

    DrawList->AddRectFilledMultiColor(
        BannerMin,
        BannerMax,
        IM_COL32(44, 58, 84, 255),
        IM_COL32(68, 72, 92, 255),
        IM_COL32(168, 108, 54, 255),
        IM_COL32(118, 78, 48, 255));
    DrawList->AddCircleFilled(ImVec2(BannerMin.x + BannerW * 0.72f, BannerMin.y + 52.0f), 11.0f, IM_COL32(255, 214, 130, 255));
    DrawList->AddText(ImVec2(BannerMin.x + 10.0f, BannerMin.y + 8.0f), IM_COL32(240, 240, 245, 255), "Low sun");
    ImGui::Dummy(ImVec2(BannerW, 88.0f));

    ImGui::Columns(2, "##SolarMetrics", true);
    ImGui::TextColored(ImVec4(0.56f, 0.56f, 0.60f, 1.0f), "Solar time");
    ImGui::Text("07:52");
    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.56f, 0.56f, 0.60f, 1.0f), "Elevation");
    ImGui::Text("+14 deg");
    ImGui::NextColumn();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.56f, 0.56f, 0.60f, 1.0f), "Azimuth");
    ImGui::Text("ESE 118 deg");
    ImGui::NextColumn();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.56f, 0.56f, 0.60f, 1.0f), "Colour");
    ImGui::Text("5.4 kK");
    ImGui::Columns(1);

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.13f, 0.77f, 0.37f, 1.0f), "[ON]  Enabled");
    ImGui::TextColored(ImVec4(0.56f, 0.56f, 0.60f, 1.0f), "[OFF] Day cycle");
}

void PresentAssetBrowserInterior()
{
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.96f, 1.0f), "EngineContent  /  Showcase  /  Materials");
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Columns(3, "##AssetGrid", false);
    ImGui::Text("BrushedSteel.mat");
    ImGui::NextColumn();
    ImGui::Text("AnodizedSlate.mat");
    ImGui::NextColumn();
    ImGui::Text("SolarAtmosphere.slang");
    ImGui::Columns(1);
}

//------------------------------------------------------------------------------------------------------------------------
//                                    SINGLE-WINDOW IMGUI RASTERIZATION HELPER
//------------------------------------------------------------------------------------------------------------------------

template <PlatformWindowSurface SurfaceType>
SoftwareProofCanvas RenderHostWindowToCanvas(
    Frontier::Components::TrapezoidalDockCoordinator<SurfaceType>& Coordinator,
    HostWindowToken                                                WindowToken)
{
    const auto WindowInfo = Coordinator.InspectHostWindow(WindowToken);
    const uint32_t Width  = std::max<uint32_t>(64u, WindowInfo.SurfaceExtent.PixelWidth);
    const uint32_t Height = std::max<uint32_t>(64u, WindowInfo.SurfaceExtent.PixelHeight);

    ImGuiContext* LocalImGui = ImGui::CreateContext();
    ImGui::SetCurrentContext(LocalImGui);

    ImGuiIO& IoInstance      = ImGui::GetIO();
    IoInstance.IniFilename   = nullptr;
    IoInstance.LogFilename   = nullptr;
    IoInstance.ConfigFlags  |= ImGuiConfigFlags_DockingEnable;
    IoInstance.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    IoInstance.DisplaySize   = ImVec2(static_cast<float>(Width), static_cast<float>(Height));
    IoInstance.DeltaTime     = 1.0f / 60.0f;

    (void)LoadProjectZeroDmSansFont(IoInstance, 12.5f);
    ConfigureImGuiTrapezoidalStyle(Coordinator.ActiveTabMetrics());

    // Run 4 ticks so DockBuilder split proportions, full tab label widths, and PatchD '+' disc settle cleanly
    for (int WarmupPass = 0; WarmupPass < 4; ++WarmupPass)
    {
        ImGui::NewFrame();
        Coordinator.PresentWindowImGuiPanels(WindowToken);
        ImGui::Render();
    }

    SoftwareProofCanvas WindowCanvas(Width, Height, RgbaColor8{ 11, 11, 12, 255 });
    WindowCanvas.RasterizeImGuiDrawData(ImGui::GetDrawData(), 0, 0);

    ImGui::DestroyContext(LocalImGui);
    return WindowCanvas;
}

void StampOperatingTitleBar(
    SoftwareProofCanvas& Canvas,
    int32_t              WinX,
    int32_t              WinY,
    int32_t              WinW,
    const char*          CaptionLabel,
    RgbaColor8           AccentDot)
{
    Canvas.FillRectangle(WinX, WinY - 26, WinW, 26, RgbaColor8{ 22, 22, 26, 255 });
    Canvas.OutlineRectangle(WinX - 1, WinY - 27, WinW + 2, 27, 1, RgbaColor8{ 56, 56, 64, 255 });
    Canvas.FillRectangle(WinX + 10, WinY - 17, 8, 8, AccentDot);

    ImGuiContext* OverlayCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(OverlayCtx);
    ImGuiIO& IoInstance      = ImGui::GetIO();
    IoInstance.IniFilename   = nullptr;
    IoInstance.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    IoInstance.DisplaySize   = ImVec2(static_cast<float>(Canvas.Width()), static_cast<float>(Canvas.Height()));
    (void)LoadProjectZeroDmSansFont(IoInstance, 11.5f);

    ImGui::NewFrame();
    ImGui::GetForegroundDrawList()->AddText(
        ImVec2(static_cast<float>(WinX + 26), static_cast<float>(WinY - 22)),
        IM_COL32(220, 220, 228, 255),
        CaptionLabel);
    ImGui::Render();
    Canvas.RasterizeImGuiDrawData(ImGui::GetDrawData(), 0, 0);
    ImGui::DestroyContext(OverlayCtx);
}

//------------------------------------------------------------------------------------------------------------------------
//                                      TEMPLATE INVARIANT SUITE (HEADLESS + GLFW)
//------------------------------------------------------------------------------------------------------------------------

template <PlatformWindowSurface SurfaceType>
bool ValidateDockCoordinatorOnSurface(const char* SurfaceLabel)
{
    Frontier::Components::TrapezoidalDockCoordinator<SurfaceType> AppCoordinator{};
    Frontier::Components::TrapezoidalDockCoordinator<SurfaceType> ForeignCoordinator{};

    if (!VerifyAssertion(
            AppCoordinator.Initialize(WindowConfigurationDescriptor::ConstructApplicationWindow("Project-Zero", 1280, 760)),
            "Primary application dock coordinator initialization") ||
        !VerifyAssertion(
            ForeignCoordinator.Initialize(WindowConfigurationDescriptor::ConstructApplicationWindow("Foreign Tool", 960, 600)),
            "Foreign application dock coordinator initialization"))
    {
        return false;
    }

    if (!VerifyAssertion(
            AppCoordinator.DomainToken() != ForeignCoordinator.DomainToken(),
            "Distinct application instances receive distinct ApplicationDockDomainTokens") ||
        !VerifyAssertion(
            FoldDomainToImGuiClassId(AppCoordinator.DomainToken()) != FoldDomainToImGuiClassId(ForeignCoordinator.DomainToken()),
            "Distinct application domains map to distinct ImGuiWindowClass IDs"))
    {
        return false;
    }

    const DockingPanelToken OutlinerToken = AppCoordinator.RegisterPanel(DockingPanelDescriptor{
        .PanelTitle        = "Outliner",
        .InitialDirection  = DockPlacementDirection::SplitLeft,
        .InitialSplitRatio = 0.21f,
        .Directives        = PanelDirectiveMask::StandardPanelDirectives,
        .ContentPresenter  = PresentOutlinerInterior,
    });
    const DockingPanelToken ViewportToken = AppCoordinator.RegisterPanel(DockingPanelDescriptor{
        .PanelTitle        = "Viewport",
        .InitialDirection  = DockPlacementDirection::CenterTabGroup,
        .InitialSplitRatio = 1.0f,
        .Directives        = PanelDirectiveMask::CentralViewportDirectives,
        .ContentPresenter  = PresentViewportInterior,
    });
    const DockingPanelToken InspectorToken = AppCoordinator.RegisterPanel(DockingPanelDescriptor{
        .PanelTitle        = "Inspector",
        .InitialDirection  = DockPlacementDirection::SplitRight,
        .InitialSplitRatio = 0.26f,
        .Directives        = PanelDirectiveMask::StandardPanelDirectives,
        .ContentPresenter  = PresentInspectorInterior,
    });
    const DockingPanelToken BrowserToken = AppCoordinator.RegisterPanel(DockingPanelDescriptor{
        .PanelTitle        = "Asset Browser",
        .InitialDirection  = DockPlacementDirection::SplitBottom,
        .InitialSplitRatio = 0.24f,
        .Directives        = PanelDirectiveMask::StandardPanelDirectives,
        .ContentPresenter  = PresentAssetBrowserInterior,
    });

    if (!VerifyAssertion(
            OutlinerToken.Valid() && ViewportToken.Valid() && InspectorToken.Valid() && BrowserToken.Valid(),
            "All 4 Project-Zero panels registered") ||
        !VerifyAssertion(
            AppCoordinator.ActiveOperatingWindowCount() == 1u,
            "Starts with 1 primary OS window"))
    {
        return false;
    }

    // Invariant (2): Undocking Inspector tears it off into a standalone OS PlatformWindow!
    const HostWindowToken DetachedInspectorWindow = AppCoordinator.DetachPanelToOperatingWindow(
        InspectorToken,
        980,
        140,
        390,
        590);
    if (!VerifyAssertion(DetachedInspectorWindow.SlotIndex == 1u, "Undocking Inspector allocates OS window slot 1") ||
        !VerifyAssertion(AppCoordinator.ActiveOperatingWindowCount() == 2u, "Two live OS PlatformWindows active after tear-off") ||
        !VerifyAssertion(AppCoordinator.InspectPanel(InspectorToken).DetachedWindow, "Inspector marked as hosted in detached OS window"))
    {
        return false;
    }

    // Invariant (1): Any panel in the application can dock into ANY window in the application (including the detached OS window!)
    const DockTransferVerdict IntraAppToDetached = AppCoordinator.DockPanelToWindow(
        BrowserToken,
        DetachedInspectorWindow,
        DockPlacementDirection::CenterTabGroup,
        1.0f);
    if (!VerifyAssertion(
            IntraAppToDetached == DockTransferVerdict::Permitted &&
            AppCoordinator.InspectHostWindow(DetachedInspectorWindow).DockedPanelCount == 2u,
            "Asset Browser docked into detached OS window alongside Inspector"))
    {
        return false;
    }

    // Invariant (3): Docking into a foreign application domain or null external OS window MUST be strictly rejected!
    const DockTransferVerdict ForeignAttempt = AppCoordinator.DockPanelToApplicationWindow(
        InspectorToken,
        ForeignCoordinator.DomainToken(),
        HostWindowToken{ 0u },
        DockPlacementDirection::CenterTabGroup,
        1.0f);
    const DockTransferVerdict NullDomainAttempt = AppCoordinator.DockPanelToApplicationWindow(
        InspectorToken,
        ApplicationDockDomainToken{ 0u },
        HostWindowToken{ 0u },
        DockPlacementDirection::CenterTabGroup,
        1.0f);

    if (!VerifyAssertion(
            ForeignAttempt == DockTransferVerdict::RejectedForeignApplication,
            "Cross-application docking strictly rejected with RejectedForeignApplication") ||
        !VerifyAssertion(
            NullDomainAttempt == DockTransferVerdict::RejectedNullDomain,
            "Unclassed external OS window docking strictly rejected with RejectedNullDomain") ||
        !VerifyAssertion(
            AppCoordinator.RejectedTransferCount() == 2u,
            "Both cross-application rejection attempts recorded"))
    {
        return false;
    }

    // Re-docking all panels out of the detached OS window back into Primary Window #0 automatically closes & reclaims the detached OS window!
    if (!VerifyAssertion(
            AppCoordinator.DockPanelToWindow(InspectorToken, HostWindowToken{ 0u }, DockPlacementDirection::SplitRight, 0.26f) ==
                DockTransferVerdict::Permitted &&
            AppCoordinator.DockPanelToWindow(BrowserToken, HostWindowToken{ 0u }, DockPlacementDirection::SplitBottom, 0.24f) ==
                DockTransferVerdict::Permitted &&
            AppCoordinator.ActiveOperatingWindowCount() == 1u,
            "Re-docking last panel back to primary window automatically reclaims transient OS window"))
    {
        return false;
    }

    std::printf(
        "[DockingValidation] %-24s | TearOffs: %u | DockMoves: %u | ForeignBlocked: %u\n",
        SurfaceLabel,
        AppCoordinator.TearOffOperatingCount(),
        AppCoordinator.CompletedDockMoveCount(),
        AppCoordinator.RejectedTransferCount());

    AppCoordinator.Finalize();
    ForeignCoordinator.Finalize();
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                    CPU VISUAL PROOF GENERATION (4 PNG EXHIBITS)
//------------------------------------------------------------------------------------------------------------------------

bool GenerateVisualProofSuite()
{
    const auto OutputDir = ResolveRepositoryRootPath() / "Exhibits/Images/Docking";

    HeadlessTrapezoidalDockCoordinator Coordinator{};
    if (!Coordinator.Initialize(WindowConfigurationDescriptor::ConstructApplicationWindow("Project-Zero", 1360, 760)))
    {
        return false;
    }

    const DockingPanelToken OutlinerToken = Coordinator.RegisterPanel(DockingPanelDescriptor{
        .PanelTitle        = "Outliner",
        .InitialDirection  = DockPlacementDirection::SplitLeft,
        .InitialSplitRatio = 0.20f,
        .Directives        = PanelDirectiveMask::StandardPanelDirectives,
        .ContentPresenter  = PresentOutlinerInterior,
    });
    const DockingPanelToken ViewportToken = Coordinator.RegisterPanel(DockingPanelDescriptor{
        .PanelTitle        = "Viewport",
        .InitialDirection  = DockPlacementDirection::CenterTabGroup,
        .InitialSplitRatio = 1.0f,
        .Directives        = PanelDirectiveMask::CentralViewportDirectives,
        .ContentPresenter  = PresentViewportInterior,
    });
    const DockingPanelToken InspectorToken = Coordinator.RegisterPanel(DockingPanelDescriptor{
        .PanelTitle        = "Inspector",
        .InitialDirection  = DockPlacementDirection::SplitRight,
        .InitialSplitRatio = 0.24f,
        .Directives        = PanelDirectiveMask::StandardPanelDirectives,
        .ContentPresenter  = PresentInspectorInterior,
    });
    const DockingPanelToken BrowserToken = Coordinator.RegisterPanel(DockingPanelDescriptor{
        .PanelTitle        = "Asset Browser",
        .InitialDirection  = DockPlacementDirection::CenterTabGroup,
        .InitialSplitRatio = 0.24f,
        .Directives        = PanelDirectiveMask::EnableCloseButton |
                             PanelDirectiveMask::EnableTrailingAddButton |
                             PanelDirectiveMask::EnableOperatingTearOff |
                             PanelDirectiveMask::EnableCrossWindowDocking,
        .ContentPresenter  = PresentAssetBrowserInterior,
    });
    (void)OutlinerToken;
    (void)ViewportToken;

    // Proof 1: Primary Docked Project-Zero Workspace (Outliner Left | Viewport Center | Inspector Right)
    {
        const SoftwareProofCanvas PrimaryWindowCanvas = RenderHostWindowToCanvas(Coordinator, HostWindowToken{ 0u });
        SoftwareProofCanvas       DesktopSheet(1440, 840, RgbaColor8{ 16, 18, 22, 255 });
        DesktopSheet.BlitCanvasWithDropShadow(PrimaryWindowCanvas, 40, 52, RgbaColor8{ 52, 52, 60, 255 });
        StampOperatingTitleBar(
            DesktopSheet,
            40,
            52,
            1360,
            "OS Window #0  —  Project-Zero Workspace  (Trapezoidal Dock Panels: Outliner | Viewport | Inspector)",
            RgbaColor8{ 34, 197, 94, 255 });

        if (!VerifyAssertion(
                DesktopSheet.EncodePngToFile(OutputDir / "01-PrimaryDockedWorkspace.png"),
                "Write 01-PrimaryDockedWorkspace.png"))
        {
            return false;
        }
    }

    // Proof 2: Invariant (2) — Inspector torn off into a standalone OS PlatformWindow (#1) beside Primary OS Window (#0)
    Coordinator.AccessOperatingSurface(HostWindowToken{ 0u })->ResizeSurface(960, 720, 1.0f);
    const HostWindowToken DetachedWindow = Coordinator.DetachPanelToOperatingWindow(InspectorToken, 1030, 80, 420, 660);
    {
        const SoftwareProofCanvas PrimaryCanvas  = RenderHostWindowToCanvas(Coordinator, HostWindowToken{ 0u });
        const SoftwareProofCanvas DetachedCanvas = RenderHostWindowToCanvas(Coordinator, DetachedWindow);

        SoftwareProofCanvas DesktopSheet(1500, 820, RgbaColor8{ 16, 18, 22, 255 });
        DesktopSheet.BlitCanvasWithDropShadow(PrimaryCanvas,  36,   60, RgbaColor8{ 52, 52, 60, 255 });
        DesktopSheet.BlitCanvasWithDropShadow(DetachedCanvas, 1036, 94, RgbaColor8{ 34, 197, 94, 255 });

        StampOperatingTitleBar(
            DesktopSheet,
            36,
            60,
            960,
            "OS Window #0  —  Project-Zero Primary Window  (Outliner | Viewport)",
            RgbaColor8{ 34, 197, 94, 255 });
        StampOperatingTitleBar(
            DesktopSheet,
            1036,
            94,
            420,
            "OS Window #1  —  Torn-Off Native PlatformWindow (Inspector)",
            RgbaColor8{ 245, 158, 11, 255 });

        if (!VerifyAssertion(
                DesktopSheet.EncodePngToFile(OutputDir / "02-DetachedOperatingWindow.png"),
                "Write 02-DetachedOperatingWindow.png"))
        {
            return false;
        }
    }

    // Proof 3: Invariant (1) — Console docked as interlocking trapezoidal tab beside Inspector + Asset Browser split below in OS Window #1!
    const DockingPanelToken ConsoleToken = Coordinator.RegisterPanel(DockingPanelDescriptor{
        .PanelTitle        = "Console",
        .InitialDirection  = DockPlacementDirection::CenterTabGroup,
        .InitialSplitRatio = 1.0f,
        .Directives        = PanelDirectiveMask::StandardPanelDirectives,
        .ContentPresenter  = []() {
            ImGui::TextColored(ImVec4(0.56f, 0.56f, 0.60f, 1.0f), "[00:00.01] Slang shader toolchain ready.");
        },
    });
    Coordinator.TogglePanelVisibility(BrowserToken, true);
    (void)Coordinator.DockPanelToWindow(ConsoleToken, DetachedWindow, DockPlacementDirection::CenterTabGroup, 1.0f);
    (void)Coordinator.DockPanelToWindow(BrowserToken, DetachedWindow, DockPlacementDirection::SplitBottom,    0.34f);
    {
        const SoftwareProofCanvas PrimaryCanvas  = RenderHostWindowToCanvas(Coordinator, HostWindowToken{ 0u });
        const SoftwareProofCanvas DetachedCanvas = RenderHostWindowToCanvas(Coordinator, DetachedWindow);

        SoftwareProofCanvas DesktopSheet(1500, 820, RgbaColor8{ 16, 18, 22, 255 });
        DesktopSheet.BlitCanvasWithDropShadow(PrimaryCanvas,  36,   60, RgbaColor8{ 52, 52, 60, 255 });
        DesktopSheet.BlitCanvasWithDropShadow(DetachedCanvas, 1036, 94, RgbaColor8{ 34, 197, 94, 255 });

        StampOperatingTitleBar(
            DesktopSheet,
            36,
            60,
            960,
            "OS Window #0  —  Project-Zero Primary Window",
            RgbaColor8{ 34, 197, 94, 255 });
        StampOperatingTitleBar(
            DesktopSheet,
            1036,
            94,
            420,
            "OS Window #1  —  Multi-Panel Intra-App Dock (Inspector + Asset Browser)",
            RgbaColor8{ 34, 197, 94, 255 });

        if (!VerifyAssertion(
                DesktopSheet.EncodePngToFile(OutputDir / "03-MultiWindowIntraAppDocking.png"),
                "Write 03-MultiWindowIntraAppDocking.png"))
        {
            return false;
        }
    }

    // Proof 4: Invariant (3) — Application-Domain Boundary Guard (Intra-App PERMITTED vs Foreign App BLOCKED)
    {
        HeadlessTrapezoidalDockCoordinator ForeignApp{};
        auto ForeignDesc         = WindowConfigurationDescriptor::ConstructApplicationWindow("External App", 560, 620);
        ForeignDesc.MinimumWidth = 320;
        (void)ForeignApp.Initialize(ForeignDesc);
        (void)ForeignApp.RegisterPanel(DockingPanelDescriptor{
            .PanelTitle        = "External Tool",
            .InitialDirection  = DockPlacementDirection::CenterTabGroup,
            .InitialSplitRatio = 1.0f,
            .Directives        = PanelDirectiveMask::StandardPanelDirectives,
            .ContentPresenter  = []() {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.94f, 0.27f, 0.27f, 1.0f), "Foreign Process / Separate Domain");
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextWrapped(
                    "ApplicationDockDomainToken mismatch: panels from Project-Zero are strictly blocked "
                    "from docking into this window (RejectedForeignApplication).");
            },
        });

        Coordinator.TogglePanelVisibility(ConsoleToken, false);
        Coordinator.AccessOperatingSurface(HostWindowToken{ 0u })->ApplySizeLimits(320, 240);
        Coordinator.AccessOperatingSurface(HostWindowToken{ 0u })->ResizeSurface(880, 620, 1.0f);
        (void)Coordinator.DockPanelToWindow(OutlinerToken,  HostWindowToken{ 0u }, DockPlacementDirection::SplitLeft,   0.23f);
        (void)Coordinator.DockPanelToWindow(InspectorToken, HostWindowToken{ 0u }, DockPlacementDirection::SplitRight,  0.31f);
        (void)Coordinator.DockPanelToWindow(BrowserToken,   HostWindowToken{ 0u }, DockPlacementDirection::SplitBottom, 0.30f);

        const SoftwareProofCanvas OwnCanvas     = RenderHostWindowToCanvas(Coordinator, HostWindowToken{ 0u });
        const SoftwareProofCanvas ForeignCanvas = RenderHostWindowToCanvas(ForeignApp,  HostWindowToken{ 0u });

        SoftwareProofCanvas GuardSheet(1540, 760, RgbaColor8{ 16, 18, 22, 255 });
        GuardSheet.BlitCanvasWithDropShadow(OwnCanvas,     36,  86, RgbaColor8{ 34, 197, 94, 255 });
        GuardSheet.BlitCanvasWithDropShadow(ForeignCanvas, 944, 86, RgbaColor8{ 239, 68, 68, 255 });

        StampOperatingTitleBar(
            GuardSheet,
            36,
            86,
            880,
            "Frontier Instance A  [Domain 0x01]  —  Intra-App Docking PERMITTED",
            RgbaColor8{ 34, 197, 94, 255 });
        StampOperatingTitleBar(
            GuardSheet,
            944,
            86,
            560,
            "External App / Instance B  [Domain 0x02]  —  Cross-App Docking BLOCKED",
            RgbaColor8{ 239, 68, 68, 255 });

        if (!VerifyAssertion(
                GuardSheet.EncodePngToFile(OutputDir / "04-ApplicationBoundaryEnforcement.png"),
                "Write 04-ApplicationBoundaryEnforcement.png"))
        {
            return false;
        }
        ForeignApp.Finalize();
    }

    Coordinator.Finalize();
    return true;
}

bool ValidateTrapezoidalContourGeometry()
{
    const TrapezoidalTabMetrics Metrics = ConstructProjectZeroTabMetrics(1.0f);
    const auto UprightTab = ConstructTrapezoidalContour(100.0f, 50.0f, 140.0f, 24.0f, Metrics, false);
    const auto TopNotch   = ConstructTrapezoidalContour(100.0f, 50.0f, 140.0f, 24.0f, Metrics, true);

    return VerifyAssertion(UprightTab.VertexCount >= 12u, "Upright trapezoid samples quadratic rounded upper corners") &&
           VerifyAssertion(std::fabs(UprightTab.EffectiveSlant - 14.0f) < 1e-3f, "140 px tab uses full 14 px bevel slant") &&
           VerifyAssertion(QueryPointInsideContour(UprightTab, ScreenPoint2D{ 170.0f, 62.0f }), "Tab center lies inside trapezoid") &&
           VerifyAssertion(!QueryPointInsideContour(UprightTab, ScreenPoint2D{ 102.0f, 52.0f }), "Upper-left outside bevel slope excluded") &&
           VerifyAssertion(QueryPointInsideContour(UprightTab, ScreenPoint2D{ 102.0f, 72.0f }), "Lower-left flared base included") &&
           VerifyAssertion(QueryPointInsideContour(TopNotch, ScreenPoint2D{ 102.0f, 52.0f }), "Inverted top notch includes upper-left base") &&
           VerifyAssertion(!QueryPointInsideContour(TopNotch, ScreenPoint2D{ 102.0f, 72.0f }), "Inverted top notch excludes lower-left bevel");
}

} // namespace

int main()
{
    if (!ValidateTrapezoidalContourGeometry())
    {
        return 1;
    }

    if (!ValidateDockCoordinatorOnSurface<Frontier::Windowing::HeadlessPlatformWindow>("HeadlessPlatformWindow"))
    {
        return 1;
    }

#if !defined(__ANDROID__)
    if (!ValidateDockCoordinatorOnSurface<Frontier::Windowing::DesktopPlatformWindow>("DesktopPlatformWindow[GLFW-3.4]"))
    {
        return 1;
    }
#endif

    if (!GenerateVisualProofSuite())
    {
        return 1;
    }

    std::printf("[DockingValidation] All trapezoidal docking invariants + 4 visual proof PNGs verified.\n");
    return 0;
}
