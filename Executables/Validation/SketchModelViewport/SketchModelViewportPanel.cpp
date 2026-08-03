/*==============================================================================================================================================
                                                        SKETCHMODELVIEWPORTPANEL.CPP
==============================================================================================================================================*/
// 🧩 App-local composition body for the SketchModelViewport validation host. Initializes one perspective ViewportPanelState (the shared init
//    already enables grid, axis, and the spatial compass) and records ConstructViewportPanel each cycle. The grid push constants are derived
//    straight from Viewport.Camera — the one render-canonical Frontier::ViewportCamera the navigation + compass also read — exactly the way
//    RenderExtension's AssembleGridConstants does (inverse view-projection + world eye). No bridge, no second camera: the grid frames precisely
//    what the panel navigates and the cube tilts with it. The Settings line/dot toggles ride into the constants. RenderedTexture is set by the host.

#include "SketchModelViewportPanel.h"

#include "SketchModelWorkplaneOverlay.h"
#include "SketchModelViewportInput.h"

#include "EngineContext/Interface/Components/Bands/ViewportBandTop.h"
#include "EngineContext/Interface/Components/Bands/ViewportBandBottom.h"

#include "EngineContext/Navigation/Camera/CameraProjection/CameraViewMatrixSolver.h"
#include "EngineContext/Navigation/Camera/CameraProjection/ProjectionEvaluator.h"
#include "EngineContext/Math/LinearAlgebra_Float32.h"

#include <cstdio>

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeSketchModelViewportSample(SketchModelViewportState& State)
{
    // 📝 Perspective init already enables grid + axis + the spatial compass, builds the compass patches once, and sets the one
    //    camera to the default perspective orbit pose. The chrome carries its own defaults (dotted grid, metres).
    Frontier::InitializeViewportPanelState(State.Viewport, Frontier::ViewportProjection::Perspective);
    State.Chrome = SketchModelChromeState{};

    // 📝 Both summoned surfaces open blank: an empty directory with an empty history, and the action console closed until a right-click.
    InitializeSketchModelSummonedSurfaces(State.Summoned);
}


void ConformSketchModelAspect(SketchModelViewportState& State, uint32_t SurfaceWidth, uint32_t SurfaceHeight)
{
    // 📝 Aspect from the surface the grid renders into, so the analytic lines meet the panel edges square. Guards a zero height.
    Frontier::ConformCameraAspect(State.Viewport.Camera, SurfaceWidth, SurfaceHeight);
}


Frontier::GroundGridConstants AssembleSketchModelGridConstants(const SketchModelViewportState& State)
{
    // 📝 Mirrors RenderExtension::AssembleGridConstants — the orbit spec stores no matrices, so the view + projection frames are
    //    derived here from the ONE viewport camera and combined into the inverse view-projection the shader unprojects each pixel through.
    Frontier::GroundGridConstants Constants;

    const Frontier::FocalOrientation Frame          = Frontier::SolveOrbitOrientation(State.Viewport.Camera);
    const Frontier::Matrix4f         Projection     = Frontier::EvaluateProjectionFrame(State.Viewport.Camera);
    const Frontier::Matrix4f         ViewProjection = Frontier::MultiplyMatrix(Projection, Frame.ViewMatrix);

    Constants.InverseViewProjection = Frontier::InvertMatrix(ViewProjection);
    Constants.CameraPosition[0]     = Frame.EyePosition.XCoord;
    Constants.CameraPosition[1]     = Frame.EyePosition.YCoord;
    Constants.CameraPosition[2]     = Frame.EyePosition.ZCoord;
    Constants.CameraPosition[3]     = 0.0f;

    // 📝 The lens the shader must reconstruct rays for, plus the view centre the radial fade keys off. Under a parallel
    //    projection the eye is not a convergence point and can sit arbitrarily far out, so the shader needs both (see the
    //    GroundGridConstants note): the flag selects near→far parallel rays, and FocalCentre keeps the fade on the orbit Target.
    Constants.OrthographicEnabled = (State.Viewport.Camera.Projection == Frontier::ProjectionMode::Orthographic) ? 1.0f : 0.0f;
    Constants.FocalCentre[0]      = State.Viewport.Camera.Target.XCoord;
    Constants.FocalCentre[1]      = State.Viewport.Camera.Target.YCoord;
    Constants.FocalCentre[2]      = State.Viewport.Camera.Target.ZCoord;
    Constants.FocalCentre[3]      = 0.0f;

    // 📝 The Settings menu offers the three layer states as one exclusive choice, so expand that selection into the two layer
    //    floats the shader reads. The origin axes stay lit in every state (the mockup's "None" suppresses the grid, not the axes).
    Constants.LineLayerEnabled = (State.Chrome.GridLayers == GridLayerSelection::LineGrid) ? 1.0f : 0.0f;
    Constants.DotLayerEnabled  = (State.Chrome.GridLayers == GridLayerSelection::DotGrid)  ? 1.0f : 0.0f;
    Constants.AxisLayerEnabled = 1.0f;

    // 📝 The Settings menu's reach choice drives the radial dissolve. Extent and sharpness travel TOGETHER — the shader's fade is
    //    (1 - Radius/Extent) raised to the sharpness, so a longer extent on a steep curve would still thin out early.
    Constants.GridExtent    = ResolveGridReachExtent(State.Chrome.GridReach);
    Constants.FadeSharpness = ResolveGridReachSharpness(State.Chrome.GridReach);

    return Constants;
}


Frontier::ViewportPanelResult ConstructSketchModelViewportPanel(const Frontier::ThemeConfiguration& Theme,
                                                                const Frontier::SvgIconRegistry&    Icons,
                                                                SketchModelViewportState&            State)
{
    // 🔴 The column is [band][canvas][band] with NOTHING between the three rows — the mockup's `.viewport` is a flex column, so
    //    the two bands and the canvas must sum to exactly the available height. ImGui otherwise inserts ItemSpacing.y after every
    //    row, which pushes the total past the region: the footer then overhangs the canvas AND the host window grows a scrollbar.
    //    Zeroing the spacing for the whole column is what makes the three rows abut, so neither band draws over the viewport.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    // 📝 Measure the column ONCE, before any row is recorded, and give the canvas exactly what the two bands do not take. Reading
    //    the remaining space after the top band would already have lost the spacing arithmetic to whatever the band's child did.
    const float BandTopHeight    = Frontier::ResolveViewportBandTopHeight();
    const float BandBottomHeight = Frontier::ResolveViewportBandBottomHeight();
    float       CanvasHeight     = ImGui::GetContentRegionAvail().y - BandTopHeight - BandBottomHeight;
    if (CanvasHeight < 1.0f) { CanvasHeight = 1.0f; }

    // -- The 52 px band: title cluster on the left, the Views + Settings pills on the right ------------------------------
    //    📝 The Views pill lives in the LEADING cluster in the mockup (it replaced the static "Perspective" label inside
    //       `.vp-title`), so it is recorded first, before the band right-aligns the trailing cluster.
    Frontier::ViewportBandTopDescriptor TopBand = {};
    TopBand.Identifier          = "sketch-model-band-top";
    TopBand.TitleText           = nullptr;   // the Views pill IS the title cluster's label here
    TopBand.SubtitleText        = nullptr;
    TopBand.IconTexture         = Frontier::ResolveIconTexture(Icons, "g-view-volume");
    TopBand.TrailingClusterSpan = ResolveTopClusterSpan(Theme, State.Chrome);

    Frontier::BeginViewportBandTop(Theme, TopBand);
    {
        // 📝 Both pills sit in the band's control row. The leading Views pill is placed by the band right after the glyph;
        //    the trailing Settings pill is what TrailingClusterSpan reserved room for.
        const ImVec2 TrailingOrigin = ImGui::GetCursorScreenPos();

        ImGui::SetCursorScreenPos(Frontier::ResolveViewportBandTopLeadingCursor());
        ConstructCameraViewPill(Theme, State.Viewport, State.Chrome);

        ImGui::SetCursorScreenPos(TrailingOrigin);
        ConstructViewportSettingsPill(Theme, Icons, State.Viewport, State.Chrome);
    }
    Frontier::EndViewportBandTop(Theme);

    // -- The canvas: exactly the height the two bands left over (`.canvas-stage{flex:1}`) --------------------------------
    ImVec2 CanvasSize(ImGui::GetContentRegionAvail().x, CanvasHeight);
    if (CanvasSize.x < 1.0f) { CanvasSize.x = 1.0f; }

    // 📝 Report the canvas extent so the host renders the grid at exactly this size — the offscreen surface must match the rect
    //    the panel blits it into, or the analytic grid's aspect would not agree with the camera the compass reads.
    State.CanvasWidth  = static_cast<uint32_t>(CanvasSize.x);
    State.CanvasHeight = static_cast<uint32_t>(CanvasSize.y);

    Frontier::ViewportPanelResult Result = {};
    const ImVec2 CanvasOrigin = ImGui::GetCursorScreenPos();
    ImGui::BeginChild("##sketch-model-canvas", CanvasSize, false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // 🔴 Global wheel guard, BEFORE ConstructViewportPanel: while a primitive draw is armed the wheel belongs to the draw (a polygon's live side
    //    count), not the camera. The shared panel dollies on any hovered wheel notch, so capture + zero the notch here so the SAME scroll can't
    //    both change the side count AND zoom. The captured notches are spent by AdvanceShapeDraw below; a no-op (returns 0) while no draw is armed.
    const float WheelNotches = HoldWheelFromCamera(ShapeDrawActive(State.Summoned.ShapeStore));

    Result = Frontier::ConstructViewportPanel(Theme, State.Viewport);

    // 🔴 Right-drag orbits the camera (this viewport only): the shared surface button binds only left+middle, so the right press is unclaimed and
    //    reaches here. Layered over the shared left-drag orbit with the same verb + sensitivity, so both buttons look around identically. Applied
    //    after the panel so its hover result is known. Suppressed while a draw is armed — a right-drag then is not a look-around.
    if (!ShapeDrawActive(State.Summoned.ShapeStore) && !WorkplaneDrawActive(State.Summoned.WorkplaneDraw))
        ApplyRightDragOrbit(State.Viewport.Camera, Result.Hovered);

    // 📝 The authored construction planes, drawn OVER the analytic ground grid but under the summoned cards: an ImGui DrawList overlay projected
    //    by the one viewport camera (this .exe wires no ParametricSketch GPU bridge). Reads the summoned directory tree for Workplane records.
    RenderSketchModelWorkplaneOverlay(State, CanvasOrigin, CanvasSize);

    // 📝 The committed 2D sketch primitives, drawn over the sheets: read the store's shapes, flatten + project each, stroke outline + fill. Under
    //    the live rubber band that follows.
    RenderSketchModelShapes(State, State.Summoned.ShapeStore, CanvasOrigin, CanvasSize);

    // 📝 The interactive workplane draw (armed by a References→Workplane commit in the console): cast the cursor to the ground, sweep the
    //    rectangle, and on the confirming click inject the sized plane into the directory. Drawn AFTER the committed sheets so the live rubber
    //    band + crosshair sit on top; a no-op while the draw is Idle.
    AdvanceWorkplaneDraw(State, State.Summoned.Directory, State.Summoned.WorkplaneDraw, CanvasOrigin, CanvasSize);

    // 📝 The interactive primitive draw (armed by a Sketch* commit in the console): cast the cursor to the ground, seat defining points, preview
    //    the analytic shape, and on the completing click seal it into the store (which records the history entry). Returns the sealed shape id so
    //    the panel mirrors it into the outliner + History pane. A no-op while no draw is armed.
    const uint32_t SealedShapeId =
        AdvanceShapeDraw(State, State.Summoned.ShapeStore, State.Summoned.Directory, CanvasOrigin, CanvasSize, WheelNotches);
    if (SealedShapeId != 0)
        MirrorSketchShapeIntoDirectory(State.Summoned.ShapeStore, State.Summoned.Directory, SealedShapeId);

    // 🔴 Sticky-tool cycle. Escape ENDS the cycle: clear the latch FIRST (AdvanceShapeDraw already used the same Escape to cancel any in-progress
    //    shape), so the sustain below does not re-arm an Escaped tool. Then SustainSketchToolCycle re-arms the latched tool whenever a shape has just
    //    sealed (store went idle), so the next click begins a fresh shape of the same kind — the tool stays active click after click. Order is
    //    load-bearing: clear-on-Escape must precede the re-arm, or an Escape and a re-arm would fight in the same frame.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ClearSketchToolLatch(State.Summoned.ToolLatch);
    SustainSketchToolCycle(State.Summoned.ToolLatch, State.Summoned.ShapeStore);

    ImGui::EndChild();

    // 📝 Arm the action console's right-click over exactly this rect, so a press on a band or a pill never summons it. The shared viewport surface
    //    binds only left + middle, so the right press is unclaimed by orbit / pan / dolly and reaches the console untouched.
    ConfineSketchModelSummonField(State.Summoned, CanvasOrigin, CanvasSize);

    // -- The 30 px footer band: navigation hint, live target readout, Units pill ----------------------------------------
    char CoordinateReadout[96];
    std::snprintf(CoordinateReadout, sizeof(CoordinateReadout), "%.2f, %.2f, %.2f %s",
                  State.Viewport.Camera.Target.XCoord,
                  State.Viewport.Camera.Target.YCoord,
                  State.Viewport.Camera.Target.ZCoord,
                  ResolveSceneUnitSuffix(State.Chrome.SceneUnit));

    Frontier::ViewportBandBottomDescriptor FooterBand = {};
    FooterBand.Identifier          = "sketch-model-band-bottom";
    FooterBand.NavigationHintText  = "Orbit LMB/RMB \xC2\xB7 Pan MMB/Shift \xC2\xB7 Zoom Wheel \xC2\xB7 Menu Q";
    FooterBand.CoordinateText      = CoordinateReadout;
    FooterBand.TrailingClusterSpan = ResolveFooterClusterSpan(Theme, State.Chrome);

    Frontier::BeginViewportBandBottom(Theme, FooterBand);
    {
        ConstructSceneUnitPill(Theme, State.Chrome);
    }
    Frontier::EndViewportBandBottom(Theme);

    ImGui::PopStyleVar();   // ItemSpacing

    // -- The two summoned surfaces, recorded LAST so each card draws over the canvas rather than under it ------------------
    //    📝 Both are embedded whole and draw nothing while closed: the directory + inspector card answers Tab, the action console answers a
    //       right-click over the canvas rect reported above.
    ConstructSketchModelSummonedSurfaces(Theme, State.Summoned, &Icons);

    return Result;
}

}   // namespace SketchModelViewportValidation
