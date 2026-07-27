/*==============================================================================================================================================
                                                              VIEWPORTGRID.CPP
==============================================================================================================================================*/
// 🧩 Records a screen-space reference grid onto the viewport draw list. This is the placeholder floor until the 3D renderer lands — the grid
//    pans + zooms with the camera so a blank viewport still communicates space + origin. When the renderer arrives this stays as the editor
//    overlay grid; nothing here needs to change.

#include "ViewportGrid.h"

#include <math.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Blend two packed colours by t (0..1). Used to fade minor lines against the panel fill.
    ImU32 BlendColor(ImU32 A, ImU32 B, float T)
    {
        const ImVec4 Fa = ImGui::ColorConvertU32ToFloat4(A);
        const ImVec4 Fb = ImGui::ColorConvertU32ToFloat4(B);
        const ImVec4 Mix(Fa.x + (Fb.x - Fa.x) * T, Fa.y + (Fb.y - Fa.y) * T,
                         Fa.z + (Fb.z - Fa.z) * T, Fa.w + (Fb.w - Fa.w) * T);
        return ImGui::ColorConvertFloat4ToU32(Mix);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructViewportGrid(const ThemeConfiguration& Theme, const PanelViewportCamera& Camera, const ViewportGridDescriptor& Descriptor)
{
    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    DrawList->PushClipRect(Descriptor.SurfaceMin, Descriptor.SurfaceMax, true);

    // 📝 Zoom scales cell size inversely with camera distance so the grid breathes with dolly; pan offsets the origin.
    const float ZoomScale = 400.0f / (Camera.Distance > 1.0f ? Camera.Distance : 1.0f);
    const float Cell      = (Descriptor.CellPixels > 1.0f ? Descriptor.CellPixels : 16.0f) * ZoomScale;

    const ImVec2 Center(
        (Descriptor.SurfaceMin.x + Descriptor.SurfaceMax.x) * 0.5f - Camera.TargetX * ZoomScale,
        (Descriptor.SurfaceMin.y + Descriptor.SurfaceMax.y) * 0.5f + Camera.TargetZ * ZoomScale);

    const ImU32 MinorColor = BlendColor(Theme.Palette.PanelBackground, Theme.Palette.PanelBorder, 0.6f);
    const ImU32 MajorColor = BlendColor(Theme.Palette.PanelBackground, Theme.Palette.TextMuted, 0.5f);
    const int   MajorEvery = Descriptor.MajorEvery > 0 ? Descriptor.MajorEvery : 10;

    if (Cell >= 2.0f)
    {
        // -- Vertical lines --------------------------------------------------------------------------------------------
        const int FirstX = static_cast<int>(floorf((Descriptor.SurfaceMin.x - Center.x) / Cell));
        const int LastX  = static_cast<int>(ceilf ((Descriptor.SurfaceMax.x - Center.x) / Cell));
        for (int Index = FirstX; Index <= LastX; ++Index)
        {
            const float X = Center.x + Index * Cell;
            const bool  Major = (Index % MajorEvery) == 0;
            DrawList->AddLine(ImVec2(X, Descriptor.SurfaceMin.y), ImVec2(X, Descriptor.SurfaceMax.y),
                              Major ? MajorColor : MinorColor, 1.0f);
        }

        // -- Horizontal lines ------------------------------------------------------------------------------------------
        const int FirstY = static_cast<int>(floorf((Descriptor.SurfaceMin.y - Center.y) / Cell));
        const int LastY  = static_cast<int>(ceilf ((Descriptor.SurfaceMax.y - Center.y) / Cell));
        for (int Index = FirstY; Index <= LastY; ++Index)
        {
            const float Y = Center.y + Index * Cell;
            const bool  Major = (Index % MajorEvery) == 0;
            DrawList->AddLine(ImVec2(Descriptor.SurfaceMin.x, Y), ImVec2(Descriptor.SurfaceMax.x, Y),
                              Major ? MajorColor : MinorColor, 1.0f);
        }
    }

    // -- Origin axis cross ---------------------------------------------------------------------------------------------
    if (Descriptor.AxisReference)
    {
        const ImU32 AxisX = IM_COL32(196, 82, 82, 220);    // red   - world X
        const ImU32 AxisZ = IM_COL32(82, 130, 196, 220);   // blue  - world Z
        DrawList->AddLine(ImVec2(Descriptor.SurfaceMin.x, Center.y), ImVec2(Descriptor.SurfaceMax.x, Center.y), AxisX, 1.5f);
        DrawList->AddLine(ImVec2(Center.x, Descriptor.SurfaceMin.y), ImVec2(Center.x, Descriptor.SurfaceMax.y), AxisZ, 1.5f);
    }

    DrawList->PopClipRect();
}

}   // namespace Frontier
