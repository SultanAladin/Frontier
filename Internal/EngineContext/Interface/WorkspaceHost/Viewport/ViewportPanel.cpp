/*==============================================================================================================================================
                                                              VIEWPORTPANEL.CPP
==============================================================================================================================================*/
// 🧩 The shared viewport body. Reserves the full region as an interactive surface, blits the renderer's texture when one exists (else a themed
//    placeholder + reference grid), and turns pointer drags into camera orbit/pan/dolly. The exact same code drives the 3D modeling view and
//    the 2D UV/sketch view — only the projection + camera differ. When the renderer arrives, only the RenderedTexture branch changes.

#include "ViewportPanel.h"

#include "ViewportGrid.h"

#include "../../SpatialCompass/SpatialCompass.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Apply the standard viewport navigation binds to the camera from ImGui input while the surface is hovered/active.
    //    Left-drag orbits (3D) / does nothing planar; middle-drag pans; wheel dollies. Returns whether the camera moved.
    bool ApplyViewportNavigation(ViewportPanelState& State, bool Hovered, bool Active)
    {
        bool Changed = false;
        ImGuiIO& Io = ImGui::GetIO();

        if (Active)
        {
            const ImVec2 Drag = Io.MouseDelta;
            if (Io.MouseDown[2] || (Io.MouseDown[0] && Io.KeyShift))
            {
                if (Drag.x != 0.0f || Drag.y != 0.0f)
                {
                    PanPanelViewportCamera(State.Camera, Drag.x, Drag.y);
                    Changed = true;
                }
            }
            else if (Io.MouseDown[0] && State.Projection == ViewportProjection::Perspective)
            {
                if (Drag.x != 0.0f || Drag.y != 0.0f)
                {
                    OrbitPanelViewportCamera(State.Camera, Drag.x, Drag.y);
                    Changed = true;
                }
            }
        }

        if (Hovered && Io.MouseWheel != 0.0f)
        {
            DollyPanelViewportCamera(State.Camera, Io.MouseWheel);
            Changed = true;
        }

        return Changed;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeViewportPanelState(ViewportPanelState& State, ViewportProjection Projection)
{
    State.Projection      = Projection;
    State.Camera          = (Projection == ViewportProjection::Perspective)
                                ? ResolveDefaultPerspectivePanelCamera()
                                : ResolveDefaultOrthographicPanelCamera();
    State.RenderedTexture = 0;
    State.GridEnabled     = true;
    State.AxisEnabled     = true;

    // 📝 The spatial compass is a 3D-only aid (the mockup is a perspective orbit widget); build its patches once.
    State.SpatialCompassEnabled = (Projection == ViewportProjection::Perspective);
    InitializeSpatialCompass(State.SpatialCompass);
}


ViewportPanelResult ConstructViewportPanel(const ThemeConfiguration& Theme, ViewportPanelState& State)
{
    ViewportPanelResult Result = {};

    const ImVec2 SurfaceMin = ImGui::GetCursorScreenPos();
    ImVec2 SurfaceSize      = ImGui::GetContentRegionAvail();
    if (SurfaceSize.x < 1.0f) { SurfaceSize.x = 1.0f; }
    if (SurfaceSize.y < 1.0f) { SurfaceSize.y = 1.0f; }
    const ImVec2 SurfaceMax(SurfaceMin.x + SurfaceSize.x, SurfaceMin.y + SurfaceSize.y);

    // 📝 One invisible button captures the whole surface for hover + drag without stealing keyboard focus.
    ImGui::InvisibleButton("##viewport-surface", SurfaceSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool Hovered = ImGui::IsItemHovered();
    const bool Active  = ImGui::IsItemActive();

    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    if (State.RenderedTexture != 0)
    {
        // 📝 Renderer path: blit its output over the surface. (Wired once the render extension lands.)
        const ImTextureID Texture = static_cast<ImTextureID>(static_cast<intptr_t>(State.RenderedTexture));
        DrawList->AddImage(Texture, SurfaceMin, SurfaceMax);
    }
    else
    {
        // 📝 Placeholder path: a slightly-darker sunk surface + the reference grid, so a blank viewport still reads as space.
        DrawList->AddRectFilled(SurfaceMin, SurfaceMax, Theme.Palette.DeskBackground);

        if (State.GridEnabled)
        {
            ViewportGridDescriptor Grid = {};
            Grid.SurfaceMin    = SurfaceMin;
            Grid.SurfaceMax    = SurfaceMax;
            Grid.CellPixels    = 24.0f;
            Grid.MajorEvery    = 10;
            Grid.AxisReference = State.AxisEnabled;
            ConstructViewportGrid(Theme, State.Camera, Grid);
        }
    }

    // 📝 A hairline border frames the surface regardless of path.
    DrawList->AddRect(SurfaceMin, SurfaceMax, Theme.Palette.PanelBorder);

    // 📝 The CAD spatial compass — drawn ON TOP of the scene (after the surface + grid), reading + writing the same camera.
    //    When the cursor is over the widget (or a snap is running) it owns the interaction, so the viewport's own orbit is
    //    suppressed this cycle to avoid a double-drag. 3D viewports only.
    bool SpatialCompassChanged  = false;
    bool SpatialCompassCaptured = false;
    if (State.SpatialCompassEnabled)
    {
        SpatialCompassContext CompassContext = {};
        CompassContext.DrawList       = DrawList;
        CompassContext.SurfaceMin     = SurfaceMin;
        CompassContext.SurfaceMax     = SurfaceMax;
        CompassContext.Camera         = &State.Camera;
        CompassContext.Theme          = &Theme;
        CompassContext.DeltaSeconds   = ImGui::GetIO().DeltaTime;
        CompassContext.SurfaceHovered = Hovered;

        const IntersectionResult CompassResult = ConstructSpatialCompass(CompassContext, State.SpatialCompass);
        SpatialCompassChanged  = CompassResult.CameraChanged;
        // The widget captures pointer input while hovered, being dragged, or animating a snap.
        SpatialCompassCaptured = (CompassResult.HoverZone != CompassZone::None)
                              || State.SpatialCompass.PointerHeld
                              || State.SpatialCompass.Transition.Engaged
                              || State.SpatialCompass.ResetTrigger.Hovered
                              || State.SpatialCompass.ProjectionToggle.Hovered;
    }

    // 📝 Suppress the viewport's own orbit/pan/dolly while the compass owns the pointer (else a drag on the compass also
    //    orbits the free camera). The compass already moved the camera as needed.
    const bool CameraChanged = SpatialCompassCaptured
                                   ? SpatialCompassChanged
                                   : (ApplyViewportNavigation(State, Hovered, Active) || SpatialCompassChanged);

    Result.Hovered       = Hovered;
    Result.CameraChanged = CameraChanged;
    Result.SurfaceMin    = SurfaceMin;
    Result.SurfaceMax    = SurfaceMax;
    const ImVec2 Pointer = ImGui::GetIO().MousePos;
    Result.LocalPointer  = ImVec2(Pointer.x - SurfaceMin.x, Pointer.y - SurfaceMin.y);

    return Result;
}

}   // namespace Frontier
