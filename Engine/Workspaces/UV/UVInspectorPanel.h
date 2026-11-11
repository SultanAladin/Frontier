/*============================================================================================================================================
                                                           UVINSPECTORPANEL.H
============================================================================================================================================*/
// 🧩 5-tab Frontier UV Inspector pane (Packing, Unwrap, UDIM, Shading, Check) using ScalarSlider, ExclusiveSelector & 16-MatCap grid

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

namespace Frontier::Workspaces
{

class UVInspectorPanel
{
public:
    UVInspectorPanel() noexcept = default;

    void AttachSession(UVWorkspaceSession* TargetSession) noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

private:
    void RenderPackingSection(PixelSpace& Surface, Rectangle2D Area, const PaneInputReading& Input);
    void RenderUnwrapSection(PixelSpace& Surface, Rectangle2D Area, const PaneInputReading& Input);
    void RenderUDIMSection(PixelSpace& Surface, Rectangle2D Area, const PaneInputReading& Input);
    void RenderShadingSection(PixelSpace& Surface, Rectangle2D Area, const PaneInputReading& Input);
    void RenderCheckSection(PixelSpace& Surface, Rectangle2D Area, const PaneInputReading& Input);

    UVWorkspaceSession* Session       = nullptr;
    uint32_t            DraggingField = 0u;
};

} // namespace Frontier::Workspaces
