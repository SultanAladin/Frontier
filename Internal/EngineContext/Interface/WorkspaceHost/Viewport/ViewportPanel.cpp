/*==============================================================================================================================================
                                                              VIEWPORTPANEL.CPP
==============================================================================================================================================*/
// 🧩 The shared viewport body. Reserves the full region as an interactive surface, blits the renderer's texture when one exists (else a themed
//    placeholder + reference grid), and turns pointer drags into camera orbit/pan/dolly. The exact same code drives the 3D modeling view and
//    the 2D UV/sketch view — only the projection + camera differ. When the renderer arrives, only the RenderedTexture branch changes.

#include "ViewportPanel.h"

#include "ViewportGrid.h"

#include "../../SpatialCompass/SpatialCompass.h"

#include "../../../Navigation/Camera/CameraNavigation/CameraNavigation.h"

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Pixel-delta → camera-verb scaling. The Navigation verbs take angle / world deltas, so the panel converts raw pointer
    //    pixels into them here (the sensitivities the old panel camera carried, folded in at the one call site that needs them).
    const float OrbitRadiansPerPixel = 0.008f;   // [rad/px] - Drag → orbit angle
    const float PanFractionPerPixel  = 0.0015f;  // [-/px]   - Drag → pan (scaled by Distance so the drag tracks the cursor)
    const float DollyFractionPerNotch = 0.10f;   // [-/notch]- Wheel → fraction of Distance

    // 📝 Apply the standard viewport navigation binds to the camera from ImGui input while the surface is hovered/active.
    //    Left-drag orbits (3D) / does nothing planar; middle- (or shift-left-) drag pans; wheel dollies. Returns whether the
    //    camera moved. Drives the render-canonical ViewportCamera through the shared Navigation verbs — one camera, one convention.
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
                    // Pan slides Target in the view plane, scaled so a drag covers the same screen span at any zoom.
                    // 📝 The scale must track whatever actually sets the on-screen world extent, and that differs by lens: a
                    //    perspective frustum widens with Distance, while a parallel one is fixed by OrthographicHalfHeight and
                    //    ignores Distance entirely. Using Distance in ortho made the pan speed unrelated to the visible extent,
                    //    so the scene slid faster or slower than the cursor. Both branches are expressed against the perspective
                    //    reference (HalfHeight = Distance · tan(FOV/2)) so the feel is identical across a lens toggle.
                    const float ExtentReference = (State.Camera.Projection == ProjectionMode::Orthographic)
                                                      ? State.Camera.OrthographicHalfHeight / std::tan(State.Camera.FieldOfView * 0.5f)
                                                      : State.Camera.Distance;
                    const float PanScale = PanFractionPerPixel * ExtentReference;
                    PanViewportCamera(State.Camera, -Drag.x * PanScale, Drag.y * PanScale);
                    Changed = true;
                }
            }
            else if (Io.MouseDown[0] && State.Projection != ViewportProjection::Planar)
            {
                if (Drag.x != 0.0f || Drag.y != 0.0f)
                {
                    // Orbit: drag-right turns the view right (−Yaw), drag-down raises the eye. ConstrainPitch is applied inside.
                    // 💡 Gated on the viewport being a 3D one (not Planar), NOT on the LENS. ViewportProjection is the panel's
                    //    kind — 3D orbit vs 2D UV/sketch — and happens to spell its 3D case "Perspective", so testing it for
                    //    equality silently killed orbit the moment a 3D viewport switched to an orthographic lens. Orbiting in
                    //    ortho is standard DCC behaviour (Blender's "User Ortho"), so only a genuinely planar view opts out.
                    OrbitViewportCamera(State.Camera, -Drag.x * OrbitRadiansPerPixel, -Drag.y * OrbitRadiansPerPixel);
                    Changed = true;
                }
            }
        }

        if (Hovered && Io.MouseWheel != 0.0f)
        {
            // Wheel up pulls the eye in (negative Distance delta). ConstrainDistance is applied inside the verb.
            DollyViewportCamera(State.Camera, -Io.MouseWheel * DollyFractionPerNotch * State.Camera.Distance);
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
                                ? ResolveDefaultPerspectiveCamera()
                                : ResolveDefaultOrthographicCamera();
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
        // 📝 Renderer path: blit its output over the surface. RenderedTexture already IS an ImTextureID (a VkDescriptorSet handle
        //    on the Vulkan backend), so it goes straight to AddImage with no width-losing cast.
        DrawList->AddImage(State.RenderedTexture, SurfaceMin, SurfaceMax);
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

    // 📝 No hairline frames the surface. The mockup's canvas stage is a bare dark rectangle: the tone step between the chrome
    //    bands and the scene is the only separator it needs, and a border here would draw a grey line ACROSS the rendered image
    //    (the blit fills the surface edge to edge), which reads as the viewport being boxed in rather than framed by its bands.

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
