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
#include "SketchModelOperatorPanel.h"
#include "SketchModelBooleanPopup.h"        // the boolean settings card + its live region preview (reconciled off the selection each frame)
#include "SketchModelGroundProjection.h"    // AssembleGroundViewProjection / ProjectWorldPoint — the boolean preview strokes mm loops to pixels

#include "SceneDirectoryInspector.h"   // RecordRevision / RevisionCategory — the corner edit logs a History revision

#include "EngineContext/Interface/Components/Bands/ViewportBandTop.h"
#include "EngineContext/Interface/Components/Bands/ViewportBandBottom.h"

#include "EngineContext/Navigation/Camera/CameraProjection/CameraViewMatrixSolver.h"
#include "EngineContext/Navigation/Camera/CameraProjection/ProjectionEvaluator.h"
#include "EngineContext/Math/LinearAlgebra_Float32.h"

#include <cstdio>
#include <cmath>

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

    // 🔴 Global wheel guard, BEFORE ConstructViewportPanel: the wheel belongs to the draw INSTEAD of the camera ONLY while the polygon tool is
    //    actively being drawn (its center click seated, so a scroll now retunes the side count). For every other tool — and for an armed-but-unclicked
    //    polygon — the wheel stays with the camera so zoom keeps working. The shared panel dollies on any hovered wheel notch, so this captures + zeroes
    //    the notch only under that one predicate; AdvanceShapeDraw below spends the captured value. Returns 0 (leaves the wheel alone) otherwise.
    const Frontier::ParametricSketchShapeStore& Shp = State.Summoned.ShapeStore;
    const bool PolygonTuningWheel =
        Shp.DrawingEnabled &&
        Shp.DrawingCategory == Frontier::ParametricSketchShapeCategory::Polygon &&
        !Shp.PendingPoints.empty();
    const float WheelNotches = HoldWheelFromCamera(PolygonTuningWheel);

    // 🔴 While ANY interactive draw is armed (a sketch primitive OR the workplane sweep), the left button belongs to the DRAW: a left-click-drag must
    //    seat/complete a point. Under the Blender scheme navigation is entirely on the MIDDLE button (orbit MMB, pan Shift+MMB, dolly Ctrl+MMB), so a
    //    plain left-drag no longer orbits — this guard is now defensive only: it zeros the plain-left-drag delta before the shared panel reads it, so no
    //    future left-drag verb can steal a draw gesture. MMB navigation + the wheel stay live. A latched-but-idle tool still counts as armed here.
    const bool DrawArmed = ShapeDrawActive(Shp) || WorkplaneDrawActive(State.Summoned.WorkplaneDraw) || SketchToolLatched(State.Summoned.ToolLatch) ||
                           SketchCommandToolActive(State.Summoned.CommandTools) || State.Summoned.InsetModal.Armed || State.Summoned.InsetPickPending;
    HoldLeftDragFromCamera(DrawArmed);

    Result = Frontier::ConstructViewportPanel(Theme, State.Viewport);

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

    // 🔴 RIGHT-CLICK ON A SHAPE opens that shape's Properties card — and it works WHETHER OR NOT a draw tool is armed. It must run BEFORE AdvanceShapeDraw,
    //    because the draw's right-click branch would otherwise cancel the stroke on the same press. On a HIT it selects the shape, requests the directory
    //    card onto the Properties face, and returns true; the tool (if any) is then FROZEN — not cancelled — for as long as the card stays open, so the user
    //    resumes the same tool the moment they dismiss it. A right-click that MISSES every shape returns false and is left to AdvanceShapeDraw (cancel the
    //    in-progress stroke) or the idle camera. It is gated only on the card not already being open (SummonOpen) — a right-click while the card is up is a
    //    dismissal the card itself handles, not a fresh pick. The console is on Q, so nothing else contends for the press.
    const bool DirectoryCardOpen = State.Summoned.Directory.SummonOpen;
    bool RightClickPickConsumed = false;
    if (!DirectoryCardOpen)
    {
        RightClickPickConsumed =
            AdvanceShapePropertiesInvoke(State, State.Summoned.ShapeStore, State.Summoned.Directory,
                                         State.Summoned.ShapeRowTokens, CanvasOrigin, CanvasSize);
    }

    // 📝 The interactive primitive draw (armed by a Sketch* commit in the console): cast the cursor to the ground, seat defining points, preview the
    //    analytic shape, and on the completing click seal it into the store. SUPPRESSED (frozen, tool still armed) while EITHER summoned surface is open
    //    (the Properties card OR the Q console — a click on either must not also seat a draw point underneath) OR on the frame the right-click pick
    //    consumed the press. Freezing rather than cancelling means the held tool resumes the instant the surface closes. Returns the sealed id on a seal.
    const bool FreezeDraw = DirectoryCardOpen || State.Summoned.ConsoleOpen || RightClickPickConsumed;
    const uint32_t SealedShapeId =
        AdvanceShapeDraw(State, State.Summoned.ShapeStore, State.Summoned.Directory, CanvasOrigin, CanvasSize,
                         WheelNotches, State.Summoned.ToolLatch.CentreRect, FreezeDraw);
    if (SealedShapeId != 0)
    {
        // Mirror the sealed shape into the outliner + History, and RECORD the row token it issued against the store ShapeId so a later
        // right-click PICK can open THAT row's Properties card (AdvanceShapePropertiesInvoke reads this back-map).
        uint32_t SealedRowToken = 0;
        MirrorSketchShapeIntoDirectory(State.Summoned.ShapeStore, State.Summoned.Directory, SealedShapeId, SealedRowToken);
        if (SealedRowToken != 0)
            State.Summoned.ShapeRowTokens.emplace_back(SealedShapeId, SealedRowToken);
    }

    // 🔴 Sticky-tool cycle. RIGHT-CLICK (on empty canvas) and ESCAPE mean the SAME thing: cancel only the IN-PROGRESS STROKE, never the tool. AdvanceShapeDraw
    //    already cleared PendingPoints + dropped DrawingEnabled on that same press; because the latch is LEFT ALONE, SustainSketchToolCycle re-arms the same
    //    category below, so the user is dropped back to a fresh blank stroke of the SAME tool and can keep drawing immediately — no trip back to the context
    //    menu to re-pick it. Neither key clears the latch: the tool is turned off only by re-picking / clearing it from the console, exactly as before.
    SustainSketchToolCycle(State.Summoned.ToolLatch, State.Summoned.ShapeStore);

    // 🔴 WHOLE-SHAPE selection is the idle-tool behaviour: click a committed shape to select it (it strokes green), Shift-click to add. It runs ONLY
    //    when no draw is armed AND no summoned surface owns the press — the console (Q) and the directory card (Tab) both float over this canvas, and a
    //    click that dismisses one of them must not ALSO pick a shape underneath. AdvanceShapeSelection self-gates on DrawingEnabled; the summon guards
    //    are added here since only the panel knows those surfaces are up.
    const bool SummonOwnsPress = State.Summoned.ConsoleOpen || State.Summoned.Directory.SummonOpen;

    // 🔴 BEVEL/CHAMFER MODAL first, ahead of every idle-tool pick. A Fillet/Chamfer commit in the Q console raised FilletPickPending; the modal then
    //    owns the canvas until a corner is picked, dragged, and committed (or cancelled). While it owns the press this cycle (a pick click, a commit,
    //    or a cancel), the whole-shape + sub-element picks below are SKIPPED so the same click is not also read as a selection. Runs only when no
    //    summoned surface owns the press (a click that dismisses the console/card must not also drive the modal). Reads/clears the caller's pending flag.
    bool FilletOwnsPress = false;
    if (!SummonOwnsPress)
    {
        FilletOwnsPress = AdvanceSketchFilletModal(State, State.Summoned.ShapeStore, State.Summoned.FilletModal,
                                                   State.Summoned.FilletPickPending, CanvasOrigin, CanvasSize);
    }

    // 🔴 HISTORY: a fresh-corner Fillet/Chamfer commit (the modal's CommitSerial advanced past the last one we logged) records ONE Sketch revision in
    //    the branching revision store — so the corner edit shows in the History panel exactly like a drawn shape. A redo-box re-adjust leaves the serial
    //    untouched, so it never re-logs. The clock-less validation build stamps a monotonic "HH:MM" (same scheme as MirrorSketchShapeIntoDirectory).
    {
        SketchModelFilletModal& Modal = State.Summoned.FilletModal;
        if (Modal.CommitSerial != State.Summoned.FilletHistorySerial)
        {
            State.Summoned.FilletHistorySerial = Modal.CommitSerial;

            const bool  IsChamfer = (Modal.LastCategory == Frontier::ParametricSketchCornerCategory::Chamfer);
            const char* Verb      = IsChamfer ? "Chamfered" : "Filleted";

            char Title[64];
            std::snprintf(Title, sizeof(Title), "%s corner %d", Verb, Modal.LastCornerIndex);

            char Subtitle[64];
            std::snprintf(Subtitle, sizeof(Subtitle), "%s = %.2f mm", IsChamfer ? "Setback" : "Radius", Modal.LastMagnitude);

            static int FilletMinute = 40;
            char TimeText[8];
            std::snprintf(TimeText, sizeof(TimeText), "%02d:%02d", 9 + (FilletMinute / 60), FilletMinute % 60);
            ++FilletMinute;

            SceneDirectoryInspectorValidation::RecordRevision(State.Summoned.Directory.Revisions,
                                                              SceneDirectoryInspectorValidation::RevisionCategory::Sketch,
                                                              Title, Subtitle, TimeText);
        }
    }

    // 🔴 THE 2D MODIFY COMMAND TOOLS (Trim / Cut / Join / Remove), after the fillet modal and ahead of the idle picks. A Trim/Cut/Join/Remove commit in
    //    the Q console armed the sticky driver; it then owns the canvas until Escape / right-click releases it, hovering a target and applying its verb on
    //    each left click. While it owns the press this cycle, the whole-shape + sub-element picks below are SKIPPED. Runs only when no summoned surface and
    //    no fillet modal owns the press (a click driving those must not also drive a command tool).
    bool CommandOwnsPress = false;
    if (!SummonOwnsPress && !FilletOwnsPress)
    {
        CommandOwnsPress = AdvanceSketchCommandTools(State, State.Summoned.ShapeStore, State.Summoned.CommandTools,
                                                     CanvasOrigin, CanvasSize);
    }

    // 🔴 HISTORY: a fresh command-tool apply (ApplySerial advanced past the last one logged) records ONE Sketch revision — the command-tool twin of the
    //    fillet history hook. A no-op apply (a miss) leaves the serial untouched, so it never logs. Same clock-less monotonic "HH:MM" stamp.
    {
        SketchModelCommandToolState& Tools = State.Summoned.CommandTools;
        if (Tools.ApplySerial != State.Summoned.CommandHistorySerial && Tools.LastLabel != nullptr)
        {
            State.Summoned.CommandHistorySerial = Tools.ApplySerial;

            char Title[64];
            std::snprintf(Title, sizeof(Title), "%s shape %u", Tools.LastLabel, Tools.LastShapeId);

            char Subtitle[64];
            if (Tools.LastOperands > 1)
                std::snprintf(Subtitle, sizeof(Subtitle), "%d operands", Tools.LastOperands);
            else
                Subtitle[0] = '\0';

            static int CommandMinute = 50;
            char TimeText[8];
            std::snprintf(TimeText, sizeof(TimeText), "%02d:%02d", 9 + (CommandMinute / 60), CommandMinute % 60);
            ++CommandMinute;

            SceneDirectoryInspectorValidation::RecordRevision(State.Summoned.Directory.Revisions,
                                                              SceneDirectoryInspectorValidation::RevisionCategory::Sketch,
                                                              Title, Subtitle, TimeText);
        }
    }

    // 🔴 THE OFFSET MODAL (the ported `I` tool), after the command tools and ahead of the idle picks. A SketchOffset commit in the Q console raised
    //    InsetPickPending (the two-phase pick, like Fillet): PHASE 1 hover-highlights the edge / face under the cursor and a click arms the drag on that
    //    shape; PHASE 2 grows a signed distance with the radial drag and a click / Enter commits (or Esc / right-click cancels). While it owns the press
    //    this cycle, the whole-shape + sub-element picks below are SKIPPED. Runs only when no summoned surface / fillet modal / command tool owns the press.
    bool InsetOwnsPress = false;
    if (!SummonOwnsPress && !FilletOwnsPress && !CommandOwnsPress)
    {
        InsetOwnsPress = AdvanceSketchInsetModal(State, State.Summoned.ShapeStore, State.Summoned.InsetModal,
                                                 State.Summoned.InsetPickPending, CanvasOrigin, CanvasSize);
    }

    // 🔴 HISTORY: a fresh Offset commit (InsetModal.CommitSerial advanced past the last one logged) records ONE Sketch revision — the offset twin of the
    //    fillet / command history hooks. A redo-box slide leaves the serial untouched, so it never re-logs. Same clock-less monotonic "HH:MM" stamp.
    {
        SketchModelInsetModal& Modal = State.Summoned.InsetModal;
        if (Modal.CommitSerial != State.Summoned.InsetHistorySerial)
        {
            State.Summoned.InsetHistorySerial = Modal.CommitSerial;

            const int Count = (int)Modal.LastSources.size();

            char Title[64];
            std::snprintf(Title, sizeof(Title), "Offset %d shape%s", Count, (Count == 1) ? "" : "s");

            char Subtitle[64];
            std::snprintf(Subtitle, sizeof(Subtitle), "%s %.2f mm", (Modal.LastMagnitude < 0.0f) ? "In" : "Out",
                          std::fabs(Modal.LastMagnitude));

            static int InsetMinute = 55;
            char TimeText[8];
            std::snprintf(TimeText, sizeof(TimeText), "%02d:%02d", 9 + (InsetMinute / 60), InsetMinute % 60);
            ++InsetMinute;

            SceneDirectoryInspectorValidation::RecordRevision(State.Summoned.Directory.Revisions,
                                                              SceneDirectoryInspectorValidation::RevisionCategory::Sketch,
                                                              Title, Subtitle, TimeText);
        }
    }

    // 🔴 RECONCILE the outliner against the store, AFTER every tool has run this frame. Drawn shapes mirror themselves on seal; but Offset APPENDS
    //    Profiles and Join / Cut mutate the shape set entirely outside that path, so their rows would otherwise never appear / vanish. This pass
    //    mirrors any store shape with no row (into Profiles or Curves by its closedness), drops rows whose shape has left the store, and re-homes a
    //    row whose closedness flipped — the single place that keeps the Profiles/Curves split honest for every operator, not just the draw tools.
    ReconcileSketchDirectory(State.Summoned.ShapeStore, State.Summoned.Directory, State.Summoned.ShapeRowTokens);

    // 🔴 The OPERATOR / REDO box (Blender-style), pinned top-left of the canvas. It draws only while a Fillet/Chamfer commit is retained
    //    (Modal.LastCommitValid) and lets that last edit be re-adjusted — Amount + Fillet↔Chamfer — WITHOUT re-picking the corner. When it owns the
    //    interaction this frame (a drag / swap over the box), the canvas picks below are skipped so the same press is not also read as a selection.
    const bool OperatorBoxOwnsPress =
        ConstructSketchModelOperatorPanel(Theme, State.Summoned.FilletModal, State.Summoned.ShapeStore, CanvasOrigin, CanvasSize);

    // 🔴 The OFFSET operator/redo box — the same Blender-style redo box for the last Offset commit (Distance + Corners). It stacks BELOW the fillet box
    //    when both are retained (both pin top-left): shift it down by a fixed row-height only when the fillet box is showing, else pin it at the corner.
    const float InsetBoxStackBelow = State.Summoned.FilletModal.LastCommitValid ? 132.0f : 0.0f;
    const bool  InsetBoxOwnsPress =
        ConstructSketchModelInsetOperatorPanel(Theme, State.Summoned.InsetModal, State.Summoned.ShapeStore,
                                               CanvasOrigin, CanvasSize, InsetBoxStackBelow);

    // 🔴 THE BOOLEAN POPUP. Alone among the sketch ops it is not armed from the Q console: a boolean's operands ARE the selection, so the popup
    //    reconciles itself against the live selection and opens the moment TWO closed shapes are picked. Reconcile runs unconditionally (it only
    //    reads the selection), but the card is recorded only when no other surface owns the press, so a boolean card can never eat a fillet drag.
    ReconcileSketchModelBooleanPopup(State.Summoned.BooleanPopup, State.Summoned.ShapeStore, ImGui::GetIO().MousePos);

    // -- The live preview: stroke the region Apply would produce, BEFORE committing. Outer boundaries in the accent tint, punched holes dimmer, so
    //    a subtract's void reads as a void. Solved read-only, so nothing here can mutate geometry. --
    if (State.Summoned.BooleanPopup.Open)
    {
        const std::vector<SketchBooleanPreviewLoop> Preview =
            ResolveSketchModelBooleanPreview(State.Summoned.BooleanPopup, State.Summoned.ShapeStore);
        if (!Preview.empty())
        {
            ImDrawList*              Draw           = ImGui::GetWindowDrawList();
            const Frontier::Matrix4f ViewProjection = AssembleGroundViewProjection(State);
            const ImU32              OuterInk       = ImGui::GetColorU32(ImVec4(0.36f, 0.82f, 0.55f, 0.95f));
            const ImU32              HoleInk        = ImGui::GetColorU32(ImVec4(0.36f, 0.82f, 0.55f, 0.45f));

            for (const SketchBooleanPreviewLoop& Loop : Preview)
            {
                const ImU32 Ink = Loop.Hole ? HoleInk : OuterInk;
                for (size_t Index = 0; Index < Loop.Points.size(); ++Index)
                {
                    const ImVec2& A = Loop.Points[Index];
                    const ImVec2& B = Loop.Points[(Index + 1) % Loop.Points.size()];   // closed: wrap the last edge back to the first

                    const ProjectedPoint Start = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize, A.x, A.y, 0.0f);
                    const ProjectedPoint End   = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize, B.x, B.y, 0.0f);
                    if (Start.InFront && End.InFront)
                        Draw->AddLine(Start.Pixel, End.Pixel, Ink, Loop.Hole ? 1.5f : 2.0f);
                }
            }
        }
    }

    // -- Record the card. It reports the commit edge, which the History hook below logs. --
    bool BooleanCommitted = false;
    if (!SummonOwnsPress && !FilletOwnsPress && !OperatorBoxOwnsPress && !CommandOwnsPress && !InsetOwnsPress && !InsetBoxOwnsPress)
        BooleanCommitted = ConstructSketchModelBooleanPopup(Theme, State.Summoned.BooleanPopup, State.Summoned.ShapeStore);

    // 🔴 HISTORY: one Sketch revision per applied boolean, off CommitSerial — the boolean twin of the offset hook above.
    {
        SketchModelBooleanPopup& Popup = State.Summoned.BooleanPopup;
        if (Popup.CommitSerial != State.Summoned.BooleanHistorySerial)
        {
            State.Summoned.BooleanHistorySerial = Popup.CommitSerial;

            char Title[64];
            std::snprintf(Title, sizeof(Title), "%s %d shape%s", (Popup.LastLabel != nullptr) ? Popup.LastLabel : "Boolean",
                          Popup.LastOperands, (Popup.LastOperands == 1) ? "" : "s");

            static int BooleanMinute = 70;
            char TimeText[8];
            std::snprintf(TimeText, sizeof(TimeText), "%02d:%02d", 9 + (BooleanMinute / 60), BooleanMinute % 60);
            ++BooleanMinute;

            SceneDirectoryInspectorValidation::RecordRevision(State.Summoned.Directory.Revisions,
                                                              SceneDirectoryInspectorValidation::RevisionCategory::Sketch,
                                                              Title, "Region boolean", TimeText);
        }
    }

    // 🔴 The boolean popup does NOT lock the canvas. It is a passive offer over the current selection, so the picks below must keep running: a click
    //    on another shape re-selects (and the reconcile re-arms the card on the new pair), a click on empty space clears (and the card closes). Only
    //    the frame it COMMITS is vetoed, because that press already consumed the selection the boolean just replaced. Esc / right-click is claimed
    //    below, ahead of the other Escape handlers, so a dismissal never doubles as ending the fillet / offset tool.
    if (!SummonOwnsPress && !FilletOwnsPress && !OperatorBoxOwnsPress && !CommandOwnsPress && !InsetOwnsPress && !InsetBoxOwnsPress &&
        !BooleanCommitted)
    {
        // 🔴 ESCAPE / right-click DISMISSES the boolean card first, and only that — an open card consumes the gesture so the same press does not also
        //    end the fillet / offset tool below it. Handled here rather than inside the card because the gesture happens over the CANVAS, not the card.
        const bool BooleanDismissEdge = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        const bool BooleanDismissed   = State.Summoned.BooleanPopup.Open && BooleanDismissEdge &&
                                        DismissSketchModelBooleanPopup(State.Summoned.BooleanPopup);

        // 🔴 SELECTION STRATUM hotkeys (Blender / CadWorkspace 1-2-3): 1 = whole-shape, 2 = vertex, 3 = edge. The stratum decides what the next pick
        //    catches AND, through ResolveActiveDimension, the ConstructionDimension the Q console gates against — so pressing 2/3 with a shape selected
        //    is what surfaces the Fillet/Chamfer/Trim ops that were hidden under the pinned Nothing. Gated on no armed draw (a latched tool owns 1-9 as
        //    nothing here, but a future numeric tool binding must not collide) and no summoned surface; only fires on the press edge.
        if (!DrawArmed)
        {
            if      (ImGui::IsKeyPressed(ImGuiKey_1, false)) State.Summoned.Stratum = SelectionStratum::WholeShape;
            else if (ImGui::IsKeyPressed(ImGuiKey_2, false)) State.Summoned.Stratum = SelectionStratum::Vertex;
            else if (ImGui::IsKeyPressed(ImGuiKey_3, false)) State.Summoned.Stratum = SelectionStratum::Edge;
        }

        // 🔴 ESCAPE on the idle canvas ENDS the Fillet/Chamfer tool: hide the redo box (the committed edit stays). This only fires when the modal is
        //    fully idle — a pick-pending / armed modal consumed Esc inside AdvanceSketchFilletModal above (FilletOwnsPress), so we never reach here then.
        if (!BooleanDismissed && State.Summoned.FilletModal.LastCommitValid && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ClearSketchFilletModalHistory(State.Summoned.FilletModal);

        // 🔴 ESCAPE on the idle canvas also ENDS the Offset tool: hide its redo box (the committed offset Profiles stay). Only fires when the inset
        //    modal is fully idle — an armed modal consumed Esc inside AdvanceSketchInsetModal above (InsetOwnsPress), so we never reach here then.
        if (!BooleanDismissed && State.Summoned.InsetModal.LastCommitValid && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ClearSketchInsetModalHistory(State.Summoned.InsetModal);

        // Left-click whole-shape selection (a plain click replaces, Shift toggles) — runs in the WholeShape stratum only, so a vertex/edge click below
        // is not also read as a re-pick of the whole shape. The right-click pick above owns the other button. A boolean dismissal this frame consumed
        // the press, so the pick is skipped: a right-click that closed the card must not also re-pick underneath it.
        if (!BooleanDismissed && State.Summoned.Stratum == SelectionStratum::WholeShape)
            AdvanceShapeSelection(State, State.Summoned.ShapeStore, CanvasOrigin, CanvasSize);

        // Sub-element (vertex / edge) pick against the selected shape, writing the store's shared component-selection slots for the gate to read. A
        // no-op (and it clears the slots) in the WholeShape stratum or with nothing selected.
        AdvanceElementSelection(State, State.Summoned.ShapeStore,
                                State.Summoned.Stratum == SelectionStratum::Vertex,
                                State.Summoned.Stratum == SelectionStratum::Edge,
                                CanvasOrigin, CanvasSize);
    }

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
    FooterBand.NavigationHintText  = "Orbit MMB \xC2\xB7 Pan Shift+MMB \xC2\xB7 Zoom Ctrl+MMB/Wheel \xC2\xB7 Menu Q \xC2\xB7 Cancel RMB/Esc";
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
