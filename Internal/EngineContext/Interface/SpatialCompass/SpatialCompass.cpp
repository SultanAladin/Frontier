/*==============================================================================================================================================
                                                              SPATIALCOMPASS.CPP
==============================================================================================================================================*/
// 🧩 The orchestration of the spatial compass overlay — projects the six faces from the live camera, depth-orders them, draws the translucent
//    glass + captions + roll arrows + the shared-background control row, resolves the cursor to a face, and applies every interaction the mockup
//    exposed (face-click eased snap, drag-orbit with the >4px capture defer + release-snap-to-nearest, four 90deg rolls, Home to isometric, and
//    the Perspective ⇄ Orthographic toggle), writing camera changes back into the live ViewportCamera. A faithful port of ViewOrientationCube.html:
//    the rig math, the .42s ease, the translucent (never-culled) faces, and the persistent selection all match the mockup 1:1.

#include "SpatialCompass.h"

#include "Projection/CoordinateProjection.h"
#include "Projection/LabelAlignment.h"
#include "Intersection/DepthEvaluation.h"
#include "Intersection/RayIntersectionSolver.h"

#include <math.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    const float PitchLimitRadians = 1.55334f;   // [rad] - ~89deg, matches the viewport camera's pole clamp
    const float DegToRad          = 0.01745329252f;
    const float RadToDeg          = 57.2957795f;

    float ClampValue(float Value, float Low, float High)
    {
        if (Value < Low)  { return Low; }
        if (Value > High) { return High; }
        return Value;
    }

    // 📝 The cube widget's screen rectangle: a WidgetPixels square inset from the surface's top-right by the configured margins.
    void ResolveWidgetRect(const SpatialCompassContext& Context,
                           const CompassConfiguration&  Configuration,
                           ImVec2&                      WidgetMin,
                           ImVec2&                      WidgetCentre)
    {
        const float Right = Context.SurfaceMax.x - Configuration.MarginRight;
        const float Top   = Context.SurfaceMin.y + Configuration.MarginTop;
        WidgetMin    = ImVec2(Right - Configuration.WidgetPixels, Top);
        WidgetCentre = ImVec2(WidgetMin.x + Configuration.WidgetPixels * 0.5f,
                              WidgetMin.y + Configuration.WidgetPixels * 0.5f);
    }

    // 📝 The four roll-arrow centres (up/down/left/right) hugging the cube stage edges — the mockup's `.roll.up/.down/.left/.right`.
    void ResolveRollCentres(const ImVec2& WidgetMin, float WidgetPixels, ImVec2 OutCentre[4])
    {
        const float Mid = WidgetPixels * 0.5f;
        const float Inset = 11.0f;                                   // .roll radius ~11px, hugging the edge
        OutCentre[0] = ImVec2(WidgetMin.x + Mid,             WidgetMin.y + Inset);                 // up
        OutCentre[1] = ImVec2(WidgetMin.x + Mid,             WidgetMin.y + WidgetPixels - Inset);  // down
        OutCentre[2] = ImVec2(WidgetMin.x + Inset,           WidgetMin.y + Mid);                   // left
        OutCentre[3] = ImVec2(WidgetMin.x + WidgetPixels - Inset, WidgetMin.y + Mid);              // right
    }

    // 📝 Free-orbit release snap-to-nearest — ports the mockup's snapNearest(): find the named view whose (Elevation,Azimuth)
    //    is closest to the current display angles (azimuth compared modulo 360), and snap if within SnapTolerance.
    bool ResolveNearestSelectablePreset(float ElevationDeg,
                                        float AzimuthDeg,
                                        float ToleranceDeg,
                                        AlignmentPreset& OutPreset)
    {
        float NormalizedAzimuth = fmodf(fmodf(AzimuthDeg, 360.0f) + 360.0f, 360.0f);

        float Best = 1e30f;
        AlignmentPreset BestPreset = AlignmentPreset::Count;
        for (int Index = 0; Index < static_cast<int>(AlignmentPreset::Count); ++Index)
        {
            const AlignmentEntry& Entry = ResolveAlignmentEntry(static_cast<AlignmentPreset>(Index));
            float EntryAzimuth = fmodf(fmodf(Entry.AzimuthDeg, 360.0f) + 360.0f, 360.0f);
            float AzimuthDelta = fabsf(NormalizedAzimuth - EntryAzimuth);
            AzimuthDelta = (AzimuthDelta < 360.0f - AzimuthDelta) ? AzimuthDelta : 360.0f - AzimuthDelta;
            float ElevationDelta = fabsf(ElevationDeg - Entry.ElevationDeg);
            float Distance = AzimuthDelta + ElevationDelta;
            if (Distance < Best)
            {
                Best       = Distance;
                BestPreset = static_cast<AlignmentPreset>(Index);
            }
        }

        if (Best < ToleranceDeg)
        {
            OutPreset = BestPreset;
            return true;
        }
        return false;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeSpatialCompass(SpatialCompassState& State)
{
    InitializeCompassPartition(State.Partition, State.Configuration);
    State.SelectedPreset        = AlignmentPreset::Count;
    State.PointerHeld           = false;
    State.DragEngaged           = false;
    State.Transition.Engaged    = false;
    State.ProjectionToggle.OrthographicEnabled = false;
    State.Initialized           = true;
}


IntersectionResult ConstructSpatialCompass(const SpatialCompassContext& Context,
                                           SpatialCompassState& State)
{
    IntersectionResult Result = {};

    if (Context.DrawList == nullptr || Context.Camera == nullptr || Context.Theme == nullptr)
    {
        return Result;
    }
    if (!State.Initialized)
    {
        InitializeSpatialCompass(State);
    }

    const CompassConfiguration&   Configuration = State.Configuration;
    PanelViewportCamera&          Camera        = *Context.Camera;
    const ColorPaletteDescriptor& Palette       = Context.Theme->Palette;
    ImDrawList*                   DrawList      = Context.DrawList;
    ImGuiIO&                      Io            = ImGui::GetIO();
    const ImVec2                  Cursor        = Io.MousePos;

    //--------------------------------------------------------------------------------------------------------------------
    //  1. ADVANCE THE EASED SNAP (mockup .rig transition .42s) — writes Yaw/Pitch into the live camera.
    //--------------------------------------------------------------------------------------------------------------------
    if (State.Transition.Engaged)
    {
        float SnapYaw = Camera.Yaw, SnapPitch = Camera.Pitch;
        EvaluateOrientationSnap(State.Transition, Context.DeltaSeconds, SnapYaw, SnapPitch);
        Camera.Yaw    = SnapYaw;
        Camera.Pitch  = ClampValue(SnapPitch, -PitchLimitRadians, PitchLimitRadians);
        Result.CameraChanged = true;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //  2. PROJECT THE CUBE FROM THE LIVE CAMERA (mockup applyRig — the cube is the inverse of the camera view).
    //--------------------------------------------------------------------------------------------------------------------
    ImVec2 WidgetMin, WidgetCentre;
    ResolveWidgetRect(Context, Configuration, WidgetMin, WidgetCentre);
    const ImVec2 WidgetMax(WidgetMin.x + Configuration.WidgetPixels, WidgetMin.y + Configuration.WidgetPixels);

    ProjectionContext Projection = {};
    ResolveDisplayAngles(Camera.Yaw, Camera.Pitch, Projection.ElevationDeg, Projection.AzimuthDeg);
    Projection.WidgetCentre = WidgetCentre;
    Projection.RigPushBack  = Configuration.RigPushBack;
    Projection.FocalLength  = State.ProjectionToggle.OrthographicEnabled ? Configuration.FocalOrthographic
                                                                         : Configuration.FocalPerspective;

    for (int Index = 0; Index < CompassPartition::FaceCount; ++Index)
    {
        ProjectSurfacePatch(State.Partition.Face[Index], Projection);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //  3. RESOLVE THE HOVERED FACE (front-facing, nearest under the cursor) — only when the surface is hovered.
    //--------------------------------------------------------------------------------------------------------------------
    const bool WidgetHovered = Context.SurfaceHovered &&
                               Cursor.x >= WidgetMin.x && Cursor.x <= WidgetMax.x &&
                               Cursor.y >= WidgetMin.y && Cursor.y <= WidgetMax.y;

    CompassZone     HoverZone   = CompassZone::None;
    AlignmentPreset HoverPreset = AlignmentPreset::Count;
    if (WidgetHovered && !State.DragEngaged)
    {
        SolveCompassHit(State.Partition, Cursor, HoverZone, HoverPreset);
    }
    Result.HoverZone   = HoverZone;
    Result.HoverPreset = HoverPreset;

    //--------------------------------------------------------------------------------------------------------------------
    //  4. CONTROL-ROW LAYOUT + HIT (Home + projection toggle share one pill, below the cube) — mockup .ctlRow.
    //--------------------------------------------------------------------------------------------------------------------
    const float ButtonRadius = Configuration.ControlButton * 0.5f;
    const float RowInnerWidth = Configuration.ControlButton * 2.0f + Configuration.ControlSeparator
                              + Configuration.ControlPadding * 2.0f;
    const float RowHeight     = Configuration.ControlButton + Configuration.ControlPadding * 2.0f;
    const float RowLeft       = WidgetCentre.x - RowInnerWidth * 0.5f;
    const float RowTop        = WidgetMax.y + Configuration.ControlRowGap;
    const ImVec2 RowMin(RowLeft, RowTop);
    const ImVec2 RowMax(RowLeft + RowInnerWidth, RowTop + RowHeight);

    State.ResetTrigger.Centre     = ImVec2(RowLeft + Configuration.ControlPadding + ButtonRadius,
                                           RowTop + RowHeight * 0.5f);
    State.ResetTrigger.Radius     = ButtonRadius;
    State.ProjectionToggle.Centre = ImVec2(RowMax.x - Configuration.ControlPadding - ButtonRadius,
                                           RowTop + RowHeight * 0.5f);
    State.ProjectionToggle.Radius = ButtonRadius;

    State.ResetTrigger.Hovered     = Context.SurfaceHovered &&
                                     ResolveTriggerHovered(State.ResetTrigger.Centre, ButtonRadius, Cursor);
    State.ProjectionToggle.Hovered = Context.SurfaceHovered &&
                                     ResolveTriggerHovered(State.ProjectionToggle.Centre, ButtonRadius, Cursor);

    //--------------------------------------------------------------------------------------------------------------------
    //  5. ROLL ARROWS — four 90deg nudges, shown only when the widget is hovered (mockup .stage:hover .roll).
    //--------------------------------------------------------------------------------------------------------------------
    ImVec2 RollCentre[4];
    ResolveRollCentres(WidgetMin, Configuration.WidgetPixels, RollCentre);
    const float RollRadius = 11.0f;
    int RollHovered = -1;
    if (WidgetHovered)
    {
        for (int Index = 0; Index < 4; ++Index)
        {
            if (ResolveTriggerHovered(RollCentre[Index], RollRadius, Cursor))
            {
                RollHovered = Index;
                break;
            }
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //  6. INPUT — click priority: control row, then roll, then face, else begin a drag orbit. (Mockup event routing.)
    //--------------------------------------------------------------------------------------------------------------------
    const bool PressStarted  = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool PressReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const bool ButtonDown    = Io.MouseDown[0];

    // Whether this press should be swallowed by the widget (so the viewport's own orbit does not also consume it).
    bool WidgetConsumesPress = false;

    if (PressStarted)
    {
        if (State.ResetTrigger.Hovered)
        {
            // Home -> isometric (mockup #home)
            ArmOrientationSnap(State.Transition, Camera.Yaw, Camera.Pitch,
                               AlignmentPreset::Isometric, Configuration.TransitionDuration);
            State.SelectedPreset  = AlignmentPreset::Count;
            Result.Committed      = true;
            Result.CommittedPreset= AlignmentPreset::Isometric;
            Result.CameraChanged  = true;
            WidgetConsumesPress   = true;
        }
        else if (State.ProjectionToggle.Hovered)
        {
            // Perspective <-> Orthographic (mockup #projToggle): flip both the overlay focal length and the live camera.
            State.ProjectionToggle.OrthographicEnabled = !State.ProjectionToggle.OrthographicEnabled;
            Camera.Orthographic = State.ProjectionToggle.OrthographicEnabled;
            Result.CameraChanged = true;
            WidgetConsumesPress  = true;
        }
        else if (RollHovered >= 0)
        {
            // 90deg rolls (mockup roll arrows): up/down change elevation (Pitch), left/right change azimuth (Yaw).
            // Display Elevation = -Pitch, Azimuth = -Yaw, so an up-arrow (ax-=90) is Pitch+=90deg, etc.
            const float Ninety = 90.0f * DegToRad;
            if (RollHovered == 0) { Camera.Pitch += Ninety; }   // up:    ax -= 90 -> Pitch += 90
            if (RollHovered == 1) { Camera.Pitch -= Ninety; }   // down:  ax += 90 -> Pitch -= 90
            if (RollHovered == 2) { Camera.Yaw   -= Ninety; }   // left:  ay += 90 -> Yaw   -= 90
            if (RollHovered == 3) { Camera.Yaw   += Ninety; }   // right: ay -= 90 -> Yaw   += 90
            Camera.Pitch = ClampValue(Camera.Pitch, -PitchLimitRadians, PitchLimitRadians);
            State.SelectedPreset = AlignmentPreset::Count;
            State.Transition.Engaged = false;
            Result.CameraChanged = true;
            WidgetConsumesPress  = true;
        }
        else if (HoverZone == CompassZone::Face)
        {
            // Begin a potential face click OR drag orbit: remember the press; the click commits on release if travel < threshold.
            State.PointerHeld = true;
            State.DragEngaged = false;
            State.PressPoint  = Cursor;
            State.PressYaw    = Camera.Yaw;
            State.PressPitch  = Camera.Pitch;
            WidgetConsumesPress = true;
        }
        else if (WidgetHovered)
        {
            // Press on the cube but off a face (a corner gap) — still an orbit handle.
            State.PointerHeld = true;
            State.DragEngaged = false;
            State.PressPoint  = Cursor;
            State.PressYaw    = Camera.Yaw;
            State.PressPitch  = Camera.Pitch;
            WidgetConsumesPress = true;
        }
    }

    // Drag orbit (mockup stage pointermove): engage once travel passes the threshold, then orbit the live camera.
    if (State.PointerHeld && ButtonDown)
    {
        const float TravelX = Cursor.x - State.PressPoint.x;
        const float TravelY = Cursor.y - State.PressPoint.y;
        if (!State.DragEngaged && (fabsf(TravelX) + fabsf(TravelY) > Configuration.DragThreshold))
        {
            State.DragEngaged = true;
            State.Transition.Engaged = false;
        }
        if (State.DragEngaged)
        {
            // Mockup: ay = say + dx*0.6 ; ax = clamp(sax + dy*0.6). Display Azimuth = -Yaw, Elevation = -Pitch, so:
            //   Yaw   = PressYaw   - dx*0.6deg ;  Pitch = PressPitch - dy*0.6deg (clamped to the poles).
            const float NewYaw   = State.PressYaw   - TravelX * Configuration.OrbitSpeed * DegToRad;
            const float NewPitch = State.PressPitch - TravelY * Configuration.OrbitSpeed * DegToRad;
            Camera.Yaw   = NewYaw;
            Camera.Pitch = ClampValue(NewPitch, -PitchLimitRadians, PitchLimitRadians);
            State.SelectedPreset = AlignmentPreset::Count;
            Result.CameraChanged = true;
        }
    }

    // Release (mockup pointerup): a click (no drag) snaps + selects the face; a real drag settles to the nearest named view.
    if (PressReleased && State.PointerHeld)
    {
        if (!State.DragEngaged && HoverZone == CompassZone::Face)
        {
            ArmOrientationSnap(State.Transition, Camera.Yaw, Camera.Pitch,
                               HoverPreset, Configuration.TransitionDuration);
            State.SelectedPreset   = HoverPreset;
            Result.Committed       = true;
            Result.CommittedPreset = HoverPreset;
            Result.CameraChanged   = true;
        }
        else if (State.DragEngaged)
        {
            float ElevationDeg, AzimuthDeg;
            ResolveDisplayAngles(Camera.Yaw, Camera.Pitch, ElevationDeg, AzimuthDeg);
            AlignmentPreset Nearest = AlignmentPreset::Count;
            if (ResolveNearestSelectablePreset(ElevationDeg, AzimuthDeg, Configuration.SnapTolerance, Nearest))
            {
                ArmOrientationSnap(State.Transition, Camera.Yaw, Camera.Pitch,
                                   Nearest, Configuration.TransitionDuration);
                if (ResolveAlignmentEntry(Nearest).Selectable)
                {
                    State.SelectedPreset = Nearest;
                }
                Result.CameraChanged = true;
            }
        }
        State.PointerHeld = false;
        State.DragEngaged = false;
    }

    Result.CameraChanged = Result.CameraChanged || WidgetConsumesPress;

    //--------------------------------------------------------------------------------------------------------------------
    //  7. DRAW — re-project (the camera may have moved this cycle), depth-order, then paint back-to-front.
    //--------------------------------------------------------------------------------------------------------------------
    ResolveDisplayAngles(Camera.Yaw, Camera.Pitch, Projection.ElevationDeg, Projection.AzimuthDeg);
    Projection.FocalLength = State.ProjectionToggle.OrthographicEnabled ? Configuration.FocalOrthographic
                                                                        : Configuration.FocalPerspective;
    for (int Index = 0; Index < CompassPartition::FaceCount; ++Index)
    {
        ProjectSurfacePatch(State.Partition.Face[Index], Projection);
    }

    int Order[CompassPartition::FaceCount];
    OrderFacesBackToFront(State.Partition, Order);

    // Ported face palette (mockup .face glass): semi-transparent fill, hover tint, live/selected accents.
    const ImU32 GlassFill    = IM_COL32( 44,  52,  64, 100);   // rgba(52..34, .42..) averaged
    const ImU32 GlassBorder  = IM_COL32( 80,  92, 108, 140);   // rgba(80,92,108,.55)
    const ImU32 HoverFill    = IM_COL32( 74,  92, 118, 150);   // .face:hover
    const ImU32 SelectedFill = IM_COL32( 79, 140, 255, 140);   // .face.selected
    const ImU32 AccentBorder = Palette.AccentPrimary;
    const ImU32 LabelColour  = IM_COL32(199, 205, 214, 235);   // --face-tx
    const ImU32 LabelLive    = IM_COL32(255, 255, 255, 255);

    for (int Slot = 0; Slot < CompassPartition::FaceCount; ++Slot)
    {
        const SurfacePatchDefinition& Patch = State.Partition.Face[Order[Slot]];

        // Translucent faces are NEVER culled — the far faces read THROUGH the glass, exactly as the mockup (no
        // backface-visibility:hidden). Only the fill/accent differs by facing + state.
        const bool IsHover    = (HoverZone == CompassZone::Face) && (HoverPreset == Patch.Preset);
        const bool IsSelected = (State.SelectedPreset == Patch.Preset);

        ImU32 Fill = GlassFill;
        if (Patch.FacingViewer)
        {
            if (IsSelected)   { Fill = SelectedFill; }
            else if (IsHover) { Fill = HoverFill; }
        }
        else
        {
            // Far faces read fainter through the glass.
            Fill = IM_COL32(40, 47, 58, 55);
        }

        const ImVec2 Poly[4] = { Patch.ScreenCorner[0], Patch.ScreenCorner[1],
                                 Patch.ScreenCorner[2], Patch.ScreenCorner[3] };
        DrawList->AddConvexPolyFilled(Poly, 4, Fill);
        const ImU32 Border = (Patch.FacingViewer && (IsSelected || IsHover)) ? AccentBorder : GlassBorder;
        DrawList->AddPolyline(Poly, 4, Border, ImDrawFlags_Closed, Patch.FacingViewer ? 1.4f : 1.0f);

        // Caption — projected INTO the face plane so it foreshortens with the cube (locked flat to the face). Only on the
        // front-facing faces (a back caption would read mirrored through the glass).
        if (Patch.FacingViewer)
        {
            const AlignmentEntry& Entry = ResolveAlignmentEntry(Patch.Preset);
            ConstructInPlaneFaceLabel(DrawList, Patch, Projection, Entry.Label,
                                      Configuration.CubeHalfExtent,
                                      (IsSelected || IsHover) ? LabelLive : LabelColour);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //  8. ROLL ARROWS — chevrons on the four edges, brighter on hover (mockup .roll, revealed on stage hover).
    //--------------------------------------------------------------------------------------------------------------------
    if (WidgetHovered)
    {
        const ImU32 RollDim  = IM_COL32(138, 138, 138, 200);
        const ImU32 RollLive = IM_COL32(237, 237, 237, 255);
        for (int Index = 0; Index < 4; ++Index)
        {
            const ImVec2 C = RollCentre[Index];
            const ImU32 Tint = (RollHovered == Index) ? RollLive : RollDim;
            DrawList->AddCircleFilled(C, RollRadius, IM_COL32(22, 22, 22, 210), 16);
            DrawList->AddCircle(C, RollRadius, IM_COL32(38, 38, 38, 220), 16, 1.0f);
            // A small chevron pointing outward in the arrow's direction.
            const float S = 4.0f;
            ImVec2 A, B, D;
            if (Index == 0)      { A = ImVec2(C.x - S, C.y + S*0.5f); B = ImVec2(C.x, C.y - S*0.6f); D = ImVec2(C.x + S, C.y + S*0.5f); }
            else if (Index == 1) { A = ImVec2(C.x - S, C.y - S*0.5f); B = ImVec2(C.x, C.y + S*0.6f); D = ImVec2(C.x + S, C.y - S*0.5f); }
            else if (Index == 2) { A = ImVec2(C.x + S*0.5f, C.y - S); B = ImVec2(C.x - S*0.6f, C.y); D = ImVec2(C.x + S*0.5f, C.y + S); }
            else                 { A = ImVec2(C.x - S*0.5f, C.y - S); B = ImVec2(C.x + S*0.6f, C.y); D = ImVec2(C.x - S*0.5f, C.y + S); }
            DrawList->AddLine(A, B, Tint, 1.6f);
            DrawList->AddLine(B, D, Tint, 1.6f);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //  9. CONTROL ROW — Home + projection toggle share ONE rounded background (mockup .ctlRow).
    //--------------------------------------------------------------------------------------------------------------------
    {
        DrawList->AddRectFilled(RowMin, RowMax, IM_COL32(14, 15, 17, 230), Configuration.ControlRounding);
        DrawList->AddRect(RowMin, RowMax, IM_COL32(28, 28, 28, 255), Configuration.ControlRounding, 0, 1.0f);

        // divider between the two icons (mockup .ctlSep)
        const float SepX = RowMin.x + RowInnerWidth * 0.5f;
        DrawList->AddLine(ImVec2(SepX, RowMin.y + 8.0f), ImVec2(SepX, RowMax.y - 8.0f), IM_COL32(28, 28, 28, 255), 1.0f);

        // Home button — a house glyph (mockup #home svg).
        {
            const ImVec2 C = State.ResetTrigger.Centre;
            if (State.ResetTrigger.Hovered)
            {
                DrawList->AddCircleFilled(C, ButtonRadius, IM_COL32(34, 34, 34, 255), 20);
            }
            const ImU32 Ink = State.ResetTrigger.Hovered ? IM_COL32(237, 237, 237, 255) : IM_COL32(138, 138, 138, 255);
            const float H = 7.0f;
            // roof
            DrawList->AddLine(ImVec2(C.x - H, C.y - 1.0f), ImVec2(C.x, C.y - H), Ink, 1.7f);
            DrawList->AddLine(ImVec2(C.x, C.y - H), ImVec2(C.x + H, C.y - 1.0f), Ink, 1.7f);
            // walls
            DrawList->AddRect(ImVec2(C.x - H*0.72f, C.y - 1.0f), ImVec2(C.x + H*0.72f, C.y + H), Ink, 0.0f, 0, 1.7f);
            // door
            DrawList->AddRectFilled(ImVec2(C.x - 1.6f, C.y + 2.0f), ImVec2(C.x + 1.6f, C.y + H), Ink);
        }

        // Projection toggle — cube glyph (perspective) or square-with-cross (orthographic); filled pill when orthographic.
        {
            const ImVec2 C = State.ProjectionToggle.Centre;
            const bool Ortho = State.ProjectionToggle.OrthographicEnabled;
            if (Ortho)
            {
                DrawList->AddCircleFilled(C, ButtonRadius, Palette.AccentPrimary, 20);
            }
            else if (State.ProjectionToggle.Hovered)
            {
                DrawList->AddCircleFilled(C, ButtonRadius, IM_COL32(34, 34, 34, 255), 20);
            }
            const ImU32 Ink = Ortho ? IM_COL32(0, 0, 0, 255)
                                    : (State.ProjectionToggle.Hovered ? IM_COL32(237, 237, 237, 255)
                                                                      : IM_COL32(138, 138, 138, 255));
            const float G = 6.5f;
            if (Ortho)
            {
                // square with a centred cross (parallel projection)
                DrawList->AddRect(ImVec2(C.x - G, C.y - G), ImVec2(C.x + G, C.y + G), Ink, 1.5f, 0, 1.7f);
                DrawList->AddLine(ImVec2(C.x - G, C.y), ImVec2(C.x + G, C.y), Ink, 1.4f);
                DrawList->AddLine(ImVec2(C.x, C.y - G), ImVec2(C.x, C.y + G), Ink, 1.4f);
            }
            else
            {
                // a small perspective cube (a diamond top with a body)
                const float T = G * 0.7f;
                ImVec2 TopA(C.x, C.y - G), TopB(C.x + G, C.y - T*0.4f), TopC(C.x, C.y + T*0.2f), TopD(C.x - G, C.y - T*0.4f);
                DrawList->AddLine(TopA, TopB, Ink, 1.5f);
                DrawList->AddLine(TopB, TopC, Ink, 1.5f);
                DrawList->AddLine(TopC, TopD, Ink, 1.5f);
                DrawList->AddLine(TopD, TopA, Ink, 1.5f);
                DrawList->AddLine(TopB, ImVec2(TopB.x, TopB.y + G), Ink, 1.5f);
                DrawList->AddLine(TopC, ImVec2(TopC.x, TopC.y + G), Ink, 1.5f);
                DrawList->AddLine(TopD, ImVec2(TopD.x, TopD.y + G), Ink, 1.5f);
                DrawList->AddLine(ImVec2(TopB.x, TopB.y + G), ImVec2(TopC.x, TopC.y + G), Ink, 1.5f);
                DrawList->AddLine(ImVec2(TopD.x, TopD.y + G), ImVec2(TopC.x, TopC.y + G), Ink, 1.5f);
            }
        }
    }

    return Result;
}

}   // namespace Frontier
