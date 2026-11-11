/*============================================================================================================================================
                                                            UVVIEWPORTHOST.H
============================================================================================================================================*/
// 🧩 UV Workspace viewport host binding UVWorkspaceSession's WorkspaceDocument onto reusable ViewportProjectionPane

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/ViewportProjectionPane.h"
#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

namespace Frontier::Workspaces
{

class UVViewportHost
{
public:
    UVViewportHost();
    ~UVViewportHost();

    UVViewportHost(const UVViewportHost&)            = delete;
    UVViewportHost& operator=(const UVViewportHost&) = delete;

    void AttachSession(UVWorkspaceSession* TargetSession) noexcept;

    [[nodiscard]] ViewportProjectionPane&       AccessProjectionPane() noexcept { return ProjectionPane; }
    [[nodiscard]] const ViewportProjectionPane& InspectProjectionPane() const noexcept { return ProjectionPane; }

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

private:
    void SynchronizeProjectionPolicy() noexcept;

    UVWorkspaceSession*    Session        = nullptr;
    ViewportProjectionPane ProjectionPane {};
};

} // namespace Frontier::Workspaces
