/*==========================================================================================================================================
                                                         EVERESTWORKSPACE.H
==========================================================================================================================================*/
// 🧩 Project Everest workspace: EverestApplication runtime + GTX-first Option A Vulkan 1.3 ReSTIR live viewport,
//    WASD + RMB flight rig, #if FRONTIER_EDITOR PreviewRaster & IlluminationTelemetryJournal, and ThemeStructure UI.

#pragma once

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/GeometricRaster/ViewportFlightRig.h"
#include "Engine/Geometry/GeometryProcessing/OctetVolumeAtlas.h"
#include "Engine/Geometry/GeometryProcessing/RangeDistanceAtlas.h"
#include "Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.h"
#include "Engine/Render/SurfaceRasterization.h"
#include "Engine/Rendering/LuminousFacetAtlas.h"
#include "Engine/Rendering/SpatiotemporalIllumination.h"
#include "Engine/Rendering/SurfaceRadiosityAtlas.h"
#include "Engine/Rendering/SurfaceScatteringAtlas.h"
#include "EverestApplication.h"
#include "EverestSimulationRecords.h"

#if FRONTIER_EDITOR
#include "Engine/Diagnostics/IlluminationTelemetryJournal.h"
#include "Engine/GeometricRaster/PreviewRaster.h"
#include "Engine/Geometry/GeometryProcessing/ScreenSpaceClusterRefinement.h"
#endif

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/LogStreamPanel.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Interface/SceneInspectorPanel.h"
#include "UI/Editor/ControlCentrePanel.h"
#include <imgui.h>
#endif

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Everest
{

class EverestWorkspace
{
  public:
#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
    explicit EverestWorkspace(Frontier::ControlCentrePanel& ControlCentre,
                              std::filesystem::path         ProjectRootPath = "Projects/Everest");
#else
    explicit EverestWorkspace(std::filesystem::path ProjectRootPath = "Projects/Everest");
#endif
    ~EverestWorkspace();

    EverestWorkspace(const EverestWorkspace&)            = delete;
    EverestWorkspace& operator=(const EverestWorkspace&) = delete;

    void IntegrateSimulation(double DeltaSeconds);

    // Live Vulkan swapchain presentation hooks (zero-copy 2D RGBA8_UNORM viewport + external command buffer recording).
    bool AttachVulkanPresentation(const Frontier::VulkanRasterAccess&  Access,
                                  std::function<uint64_t(VkImageView)> DescribeTextureView,
                                  std::function<void(uint64_t)>        ReleaseTextureDescriptor = {});
    void ConstructGpuCommands(VkCommandBuffer CommandBuffer, uint64_t CompletedSerial, uint64_t UpcomingSerial);
    void DetachVulkanPresentation() noexcept;

    // Headless offscreen Vulkan compute render (used by standalone Shipping tests or screenshot export).
    bool RenderOffscreenFrame(uint32_t WidthPx, uint32_t HeightPx, std::vector<uint8_t>& OutSrgbRgbaBytes);

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
    bool ConfigurePlatform() noexcept;
    void ConstructInterface(double DeltaSeconds);

    std::function<void()> EvaluateReturnToBrowser;

    [[nodiscard]] Frontier::SceneDirectoryPanel& QueryOutliner() noexcept { return OutlinerPanel; }
    [[nodiscard]] Frontier::SceneInspectorPanel& QueryInspector() noexcept { return InspectorPanel; }
    [[nodiscard]] Frontier::LogStreamPanel&      QueryLogStream() noexcept { return LogPanel; }
    [[nodiscard]] Frontier::PanelDockspace&      QueryDockspace() noexcept { return Dockspace; }
#endif

    [[nodiscard]] EverestApplication&                            AccessApplication() noexcept { return Application; }
    [[nodiscard]] const EverestApplication&                      QueryApplication() const noexcept { return Application; }
    [[nodiscard]] Frontier::SceneWorld&                          QuerySceneWorld() noexcept { return Application.AccessWorld(); }
    [[nodiscard]] const Frontier::SceneWorld&                    QuerySceneWorld() const noexcept { return Application.QueryWorld(); }
    [[nodiscard]] Frontier::ViewportFlightRig&                   AccessFlightRig() noexcept { return FlightRig; }
    [[nodiscard]] const Frontier::ViewportFlightRig&             QueryFlightRig() noexcept { return FlightRig; }
    [[nodiscard]] const Frontier::SpatiotemporalFrameTelemetry&  QueryFrameTelemetry() const noexcept { return LatestTelemetry; }
    [[nodiscard]] const std::filesystem::path&                   QueryProjectRoot() const noexcept { return ProjectRoot; }
    [[nodiscard]] const std::filesystem::path&                   QueryActiveLevelPath() const noexcept { return ActiveLevelPath; }
    [[nodiscard]] const std::string&                             QueryDiagnostic() const noexcept { return Diagnostic; }
    [[nodiscard]] double                                         QueryElapsedSeconds() const noexcept { return ElapsedSeconds; }

  private:
    void                  InitializeWorld();
    std::filesystem::path ResolveShaderArchiveDirectory() const;
    std::filesystem::path ResolveVehicleArchiveDirectory() const;
    std::filesystem::path ResolveMaterialRootDirectory() const;

    bool EnsureSceneGeometryBuilt();
    bool InitializeRendererOnExchange();
    void SyncDynamicWorldToRenderer(const Frontier::SpatiotemporalCamera&        OutCamera,
                                    std::vector<Frontier::AnalyticalLuminaire>&  OutLuminaires,
                                    Frontier::SpatiotemporalRenderConfig&        OutConfig);

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
    void ServiceDynamicSceneRequests();
    void ConstructTopToolbar(float Scale);
    void ConstructViewportRibbon(const ImVec2& BarMin, const ImVec2& BarMax, float Scale);
    void ConstructViewportSurface();
    void RefreshHeadlessViewportTexture(uint32_t TargetW, uint32_t TargetH);

    Frontier::ControlCentrePanel& Centre;
    Frontier::PanelDockspace      Dockspace;
    Frontier::SceneDirectoryPanel OutlinerPanel;
    Frontier::SceneInspectorPanel InspectorPanel;
    Frontier::LogStreamPanel      LogPanel;
    uint64_t                      ObservedLayoutRevision = 0;

    std::unique_ptr<ImTextureData> HeadlessTexture{};
    uint64_t                       HeadlessSceneSignature = 0u;
    bool                           HeadlessVulkanChecked  = false;
    bool                           HeadlessVulkanReady    = false;
#endif

    std::filesystem::path ProjectRoot;
    std::filesystem::path ActiveLevelPath;
    EverestApplication    Application;
    std::string           Diagnostic;
    double                ElapsedSeconds     = 0.0;
    float                 LastDeltaSeconds   = 1.0f / 60.0f;

    // Live GTX-first Option A GPU Rendering State
    Frontier::ViewportFlightRig          FlightRig{};
    Frontier::VulkanComputeExchange      Exchange{};
    Frontier::SpatiotemporalIllumination Illumination{};
    Frontier::SurfaceScatteringAtlas     Materials{};
    Frontier::OctetVolumeAtlas           TwoLevelAtlas{};
    Frontier::RangeDistanceAtlas         RangeClipmap{};
    Frontier::SurfaceRadiosityAtlas      RadiosityCards{};
    Frontier::LuminousFacetAtlas         LuminousFacets{};
    Frontier::VolumeDistanceField        VehicleDistanceField{};

    uint32_t ProvingGroundArchiveId = 0u;
    uint32_t UnitBoxArchiveId       = 0u;
    uint32_t UnitCylinderArchiveId  = 0u;
    uint32_t UnitSphereArchiveId    = 0u;
    bool     GeometryBuilt          = false;
    bool     RendererReady          = false;
    bool     BorrowedPresentation   = false;

    std::array<Frontier::DeviceViewportImageSlot, 2> ViewportSlots_{};
    std::array<uint64_t, 2>                          ViewportDescriptors_{0u, 0u};
    uint32_t                                         ActiveViewportSlot_  = 0u;
    uint32_t                                         DesiredRenderWidth_  = 960u;
    uint32_t                                         DesiredRenderHeight_ = 540u;
    int                                              ResolutionScalePct_  = 75; // 50, 75, 100
    std::array<float, 3>                             ClipmapAnchorPos_{1.0e5f, 1.0e5f, 1.0e5f};
    float                                            ClipmapCascadeZeroM_ = -1.0f;

    std::function<uint64_t(VkImageView)> DescribeViewFn_{};
    std::function<void(uint64_t)>        ReleaseDescriptorFn_{};

    Frontier::SpatiotemporalFrameTelemetry LatestTelemetry{};

#if FRONTIER_EDITOR
    Frontier::IlluminationTelemetryJournal TelemetryJournal{};
    Frontier::ScreenSpaceClusterRefinement ClusterRefinement{};
    Frontier::PreviewRaster                FastPreview{};
    bool                                   UseFastPreviewRaster_ = false;
    int                                    PreviewViewIndex_     = 0;
    int                                    PreviewShadowIndex_   = 4; // HybridOrthoAndDistance
#endif
};

} // namespace Everest
