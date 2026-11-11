/*============================================================================================================================================
                                                            UVWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Dual-deployment UV Workspace host mounting StandaloneWorkspace (FrontierUV) or FeatureSet docked panes via EngineConfiguration

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/Workspaces/UV/UVInspectorPanel.h"
#include "Engine/Workspaces/UV/UVOutlinerPanel.h"
#include "Engine/Workspaces/UV/UVViewportHost.h"
#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

namespace Frontier::Workspaces
{

class UVWorkspaceHost
{
public:
    UVWorkspaceHost();

    void ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept;
    [[nodiscard]] bool AttachVulkanRasterizer(GeometricRaster::UVWorkspaceRasterizer* BoundRasterizer);

    void RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer);

    [[nodiscard]] UVWorkspaceSession&       AccessSession() noexcept       { return Session; }
    [[nodiscard]] const UVWorkspaceSession& InspectSession() const noexcept { return Session; }
    [[nodiscard]] UVOutlinerPanel&          AccessOutliner() noexcept      { return Outliner; }
    [[nodiscard]] UVViewportHost&           AccessViewportHost() noexcept  { return ViewportHost; }
    [[nodiscard]] UVInspectorPanel&         AccessInspector() noexcept     { return Inspector; }
    [[nodiscard]] uint32_t                  QueryRegisteredTabCount() const noexcept { return RegisteredTabCount; }

private:
    Configuration::EngineConfiguration ActiveEngineConfig {};
    UVWorkspaceSession                 Session            {};
    UVOutlinerPanel                    Outliner           {};
    UVViewportHost                     ViewportHost       {};
    UVInspectorPanel                   Inspector          {};
    uint32_t                           RegisteredTabCount = 0u;
};

} // namespace Frontier::Workspaces
