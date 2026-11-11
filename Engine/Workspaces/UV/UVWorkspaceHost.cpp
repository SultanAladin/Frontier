/*============================================================================================================================================
                                                           UVWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVWorkspaceHost registering StandaloneWorkspace / FeatureSet docked tabs and ContentBrowser WorkspaceCategory::UV

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVWorkspaceHost.h"

#include <array>
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

#if FRONTIER_EDITOR_ENABLED
void UVWorkspaceHost::AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser)
{
    BoundBrowser = TargetBrowser;
    if (BoundBrowser == nullptr)
    {
        return;
    }

    // Enforce WorkspaceCategory::UV filter (exposes Geometries, Materials, Textures, Images, Colours; hides Simulations/Particles/Audio)
    BoundBrowser->ConfigureWorkspaceDomain(WorkspaceCategory::UV);

    BoundBrowser->AccessPanel().RegisterColourSlot(ColourSlotRegistration{
        "uv.priority_layer.tint",
        "Active UV Priority Layer Tint",
        Rectangle2D{ 16.0f, 96.0f, 260.0f, 44.0f },
        nullptr,
        [this](ColorRgba AssignedRgba, const ColourCodexEntry&)
        {
            Session.AssignSelectedLayerTint({
                static_cast<float>(AssignedRgba.Red) / 255.0f,
                static_cast<float>(AssignedRgba.Green) / 255.0f,
                static_cast<float>(AssignedRgba.Blue) / 255.0f,
            });
        },
    });
    BoundBrowser->AccessPanel().FocusColourSlot("uv.priority_layer.tint");
}
#endif

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

    // Center Pane: UV Parameterization Viewport (Reusable ViewportProjectionPane)
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
