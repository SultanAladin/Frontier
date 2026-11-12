/*============================================================================================================================================
                                                           UVINSPECTORPANEL.H
============================================================================================================================================*/
// 🧩 5-tab Frontier UV Inspector pane (Packing, Unwrap, UDIM, Shading, Check) with full solvers, per-UDIM res/backdrop & GPU KPI badges

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

    UVWorkspaceSession* Session          = nullptr;
    uint32_t            DraggingField    = 0u;
    float               SurfaceRoughness = 0.32f;
    float               SurfaceMetallic  = 0.08f;
    float               SurfaceMatBlend  = 0.85f;
};

} // namespace Frontier::Workspaces
