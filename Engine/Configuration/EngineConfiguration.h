/*============================================================================================================================================
                                                           ENGINECONFIGURATION.H
============================================================================================================================================*/
// 🧩 Engine startup and host-launch configuration supporting unified FeatureSet, StandaloneWorkspace (e.g. FrontierUV), and HeadlessBatch

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/BuildConfiguration.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Frontier::Configuration
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class HostLaunchMode : uint8_t
{
    FeatureSet          = 0u, // [-] - Full multi-workspace Frontier suite docked in WorkspaceDockContainer
    StandaloneWorkspace = 1u, // [-] - Dedicated single-domain executable (e.g. FrontierUV) booting directly into one workspace
    HeadlessBatch       = 2u, // [-] - Non-interactive CLI / CTest execution on WorkspaceThread + ExecutionChannels
};

enum class WorkspaceDomainPreset : uint8_t
{
    AllFeatureWorkspaces = 0u, // [-] - Enables Geometry, UV, Texture, Material, Image, Vector & Simulation tabs
    UVParameterization   = 1u, // [-] - Standalone or focused UV Space, 2D UVTileCanvas, 3D Viewport & Pack Inspector
    GeometryAuthoring    = 2u, // [-] - PolygonNetwork & parametric primitive authoring workspace
    TextureAuthoring     = 3u, // [-] - Multi-UDIM channel-packed texture workspace
    MaterialAuthoring    = 4u, // [-] - Microfacet BSDF & closure network workspace
    VectorAuthoring      = 5u, // [-] - Analytic contour & stencil workspace
    SimulationAuthoring  = 6u, // [-] - Rigid, fluid & particulate dynamics workspace
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct EngineConfigurationDescriptor
{
    HostLaunchMode        LaunchMode              = HostLaunchMode::FeatureSet;                  // [-]     - FeatureSet vs Standalone
    WorkspaceDomainPreset ActiveDomainPreset      = WorkspaceDomainPreset::AllFeatureWorkspaces; // [-]     - Initial or standalone domain
    std::string           ApplicationTitle        { "Frontier" };                                // [-]     - Window & dock title
    uint32_t              PrimaryWindowWidthPx    = 1600u;                                       // [px]    - Initial window width
    uint32_t              PrimaryWindowHeightPx   = 980u;                                        // [px]    - Initial window height
    uint32_t              RequestedChannelLanes   = 0u;                                          // [count] - 0 = derive from hardware threads
    uint32_t              ReservedDomainThreads   = 3u;                                          // [count] - OS threads reserved for domains
    bool                  GPUComputeEnabled       = true;                                        // [-]     - Enables .slang compute & readback
    bool                  GPUPickingEnabled       = true;                                        // [-]     - Enables R32G32B32A32_UINT GPU pick
};

class EngineConfiguration
{
public:
    EngineConfiguration() noexcept;
    explicit EngineConfiguration(EngineConfigurationDescriptor InitialDescriptor) noexcept;

    void Configure(EngineConfigurationDescriptor NewDescriptor) noexcept;
    void ConfigureFromCommandLine(std::span<const std::string_view> Arguments) noexcept;

    [[nodiscard]] static EngineConfigurationDescriptor ConstructFeatureSetDefault() noexcept;
    [[nodiscard]] static EngineConfigurationDescriptor ConstructStandaloneUVDefault() noexcept;
    [[nodiscard]] static EngineConfigurationDescriptor ConstructHeadlessBatchDefault(
        WorkspaceDomainPreset TargetDomain = WorkspaceDomainPreset::UVParameterization) noexcept;

    [[nodiscard]] static uint32_t DeriveOptimalChannelLanes(
        uint32_t HardwareThreadCount,
        uint32_t ReservedDomainThreads = 3u) noexcept;

    [[nodiscard]] const EngineConfigurationDescriptor& InspectDescriptor() const noexcept;
    [[nodiscard]] HostLaunchMode                       QueryLaunchMode() const noexcept;
    [[nodiscard]] WorkspaceDomainPreset                QueryDomainPreset() const noexcept;
    [[nodiscard]] uint32_t                             QueryResolvedChannelLanes() const noexcept;
    [[nodiscard]] bool                                 QueryInteractiveUIEnabled() const noexcept;
    [[nodiscard]] bool                                 QueryDomainIncluded(WorkspaceDomainPreset CandidateDomain) const noexcept;
    [[nodiscard]] std::string_view                     QueryLaunchModeLabel() const noexcept;

private:
    EngineConfigurationDescriptor Descriptor           {}; // [-]     - Active launch & hardware settings
    uint32_t                      ResolvedChannelLanes = 2u; // [count] - Clamped ExecutionChannels lane count
};

} // namespace Frontier::Configuration
