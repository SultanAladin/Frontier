/*============================================================================================================================================
                                                            UVOUTLINERPANEL.H
============================================================================================================================================*/
// 🧩 Frontier-styled UV Space Outliner pane rendering Studio Asset presets, O(1) PriorityAllocationLayers, Remainder & UDIM tiles

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

namespace Frontier::Workspaces
{

class UVOutlinerPanel
{
public:
    UVOutlinerPanel() noexcept = default;

    void AttachSession(UVWorkspaceSession* TargetSession) noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

private:
    UVWorkspaceSession* Session           = nullptr;
    uint32_t            CreatedLayerCount = 3u;
};

} // namespace Frontier::Workspaces
