/*============================================================================================================================================
                                                           UVWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVWorkspaceHost registering either focused StandaloneWorkspace panes or multi-domain FeatureSet docked tabs

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVWorkspaceHost.h"

#include <imgui.h>

namespace Frontier::Workspaces
{

UVWorkspaceHost::UVWorkspaceHost()
{
    Outliner.AttachSession(&Session);
    ViewportHost.AttachSession(&Session);
    Inspector.AttachSession(&Session);
}

void UVWorkspaceHost::ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept
{
    ActiveEngineConfig = Config;
}

bool UVWorkspaceHost::AttachVulkanRasterizer(GeometricRaster::UVWorkspaceRasterizer* BoundRasterizer)
{
    return Session.AttachVulkanRasterizer(BoundRasterizer);
}

void UVWorkspaceHost::RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer)
{
    using Components::DockPlacementDirection;
    using Components::PaneDirectiveMask;
    using Components::WorkspaceDockPaneDescriptor;

    RegisteredTabCount = 0u;

    // Left Pane: UV Space Outliner
    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "UV Space",
        DockPlacementDirection::SplitLeft,
        0.20f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { Outliner.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });

    // Center Pane: UV Parameterization Viewport (Split 2D UVTileCanvas + 3D ProjectionViewport)
    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "UV",
        DockPlacementDirection::CenterTabGroup,
        0.58f,
        PaneDirectiveMask::CentralViewportDirectives,
        [this]() { ViewportHost.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
    ++RegisteredTabCount;

    // In HostLaunchMode::FeatureSet, register peer domain workspace tabs in the central dock group
    if (ActiveEngineConfig.QueryLaunchMode() == Configuration::HostLaunchMode::FeatureSet)
    {
        const char* PeerWorkspaces[4] = {
            "Geometry",
            "Texture",
            "Material",
            "Simulate",
        };
        for (const char* PeerTitle : PeerWorkspaces)
        {
            (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
                PeerTitle,
                DockPlacementDirection::CenterTabGroup,
                0.58f,
                PaneDirectiveMask::CentralViewportDirectives,
                [PeerTitle]()
                {
                    ImGui::TextUnformatted(PeerTitle);
                },
            });
            ++RegisteredTabCount;
        }
    }

    // Right Pane: 5-Tab UV Inspector
    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "UV Inspector",
        DockPlacementDirection::SplitRight,
        0.22f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { Inspector.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
}

} // namespace Frontier::Workspaces
