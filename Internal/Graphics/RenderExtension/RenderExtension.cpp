/*==============================================================================================================================================
                                                             RENDEREXTENSION.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the modular render coordinator. Initialize brings up the shared WindowSubstrate then inspects the GPU; Synthesize drives
//    the present sequence (a thin pass-through to the substrate loop at 0.1); Finalize tears the substrate down. InspectHardwareFeatures is the
//    one piece of new machinery this step adds: it chains the int64-atomic feature structs onto vkGetPhysicalDeviceFeatures2 and derives the
//    Pascal / GTX-1060 baseline verdict the from-scratch renderer refuses to fall below.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/RenderExtension/RenderExtension.h"
#include "Graphics/RenderExtension/Diagnostics/DiagnosticArchive.h"

#include "EngineContext/Input/InputPacket.h"

#include <algorithm>
#include <string>
#include <vector>

#include <cstring>
#include <cmath>
#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 Resolve the look yaw / pitch increment (radians) to feed OrbitViewportCamera this frame, applying the active jitter
//    strategy. Called for BOTH the fly mouse-look and the Alt-orbit tumble, so the fix is identical in both. Sensitivity is
//    folded in here; the return is a ready-to-apply angular increment.
//
//      • Raw              — increment = frame-count · sens. This is the quantized signal that jitters; kept as the A/B baseline.
//      • AnchorAccumulate — Blender's viewrotate. While the drag is held we keep the TOTAL offset from the press anchor
//                           (LookOffset), and the camera is turned so its cumulative rotation equals total · sens. The
//                           increment we return is (total · sens − already-applied). Integer quantization of any single frame
//                           only changes WHEN the offset reaches a value, never smears into a false rate — so it cannot jitter,
//                           and because there is no filter it has zero lag and adds nothing on a zero-count frame.
//      • VelocityLowpass  — UE5-style. We form the instantaneous look velocity (counts / second), low-pass it against a
//                           wall-clock time constant (framerate independent), and integrate the SMOOTHED velocity over the
//                           frame. On a zero-count frame the velocity decays toward zero rather than snapping, so bursts are
//                           spread across frames without the phantom motion an EMA-on-delta injected.
void ResolveLookIncrement(RenderExtension& Extension,
                          bool             DragActive,
                          float            FrameCountX,
                          float            FrameCountY,
                          float            Sensitivity,
                          float            DeltaSeconds,
                          float&           OutYawIncrement,
                          float&           OutPitchIncrement)
{
    OutYawIncrement   = 0.0f;
    OutPitchIncrement = 0.0f;

    switch (Extension.LookMode)
    {
        case LookFilterMode::AnchorAccumulate:
        {
            if (!DragActive)
            {
                // Drag released — reset so the next press starts a fresh anchor.
                Extension.LookDragActive = false;
                Extension.LookOffsetX = Extension.LookOffsetY = 0.0f;
                Extension.LookAppliedX = Extension.LookAppliedY = 0.0f;
                return;
            }
            if (!Extension.LookDragActive)
            {
                // First frame of a new drag: seed the anchor, apply nothing yet.
                Extension.LookDragActive = true;
                Extension.LookOffsetX = Extension.LookOffsetY = 0.0f;
                Extension.LookAppliedX = Extension.LookAppliedY = 0.0f;
            }
            Extension.LookOffsetX += FrameCountX;
            Extension.LookOffsetY += FrameCountY;
            const float TargetYaw   = Extension.LookOffsetX * Sensitivity;
            const float TargetPitch = Extension.LookOffsetY * Sensitivity;
            OutYawIncrement   = TargetYaw   - Extension.LookAppliedX;
            OutPitchIncrement = TargetPitch - Extension.LookAppliedY;
            Extension.LookAppliedX = TargetYaw;
            Extension.LookAppliedY = TargetPitch;
            return;
        }

        case LookFilterMode::VelocityLowpass:
        {
            const float TimeConstant = 0.035f;   // [s] - Low-pass horizon; larger = smoother + laggier (UE5-ish default)
            const float SafeDelta    = (DeltaSeconds > 1e-5f) ? DeltaSeconds : 1e-5f;
            const float Alpha        = 1.0f - std::exp(-SafeDelta / TimeConstant);
            const float TargetVelX   = FrameCountX / SafeDelta;   // [px/s] - instantaneous rate this frame
            const float TargetVelY   = FrameCountY / SafeDelta;
            Extension.LookVelocityX += (TargetVelX - Extension.LookVelocityX) * Alpha;
            Extension.LookVelocityY += (TargetVelY - Extension.LookVelocityY) * Alpha;
            if (!DragActive)
            {
                // Not dragging — bleed the velocity out fast so a stale tail never leaks into the next drag.
                Extension.LookVelocityX = Extension.LookVelocityY = 0.0f;
                return;
            }
            OutYawIncrement   = Extension.LookVelocityX * SafeDelta * Sensitivity;
            OutPitchIncrement = Extension.LookVelocityY * SafeDelta * Sensitivity;
            return;
        }

        case LookFilterMode::Raw:
        default:
        {
            OutYawIncrement   = FrameCountX * Sensitivity;
            OutPitchIncrement = FrameCountY * Sensitivity;
            return;
        }
    }
}

// 📝 The Unreal-editor navigation gesture map, read straight off the window's InputPacket. Two coexisting modes, matching a
//    modern DCC / game-editor viewport:
//
//      • RMB held  → FLY. The pointer delta turns the look direction (mouse-look), WASD walks the camera through the world along
//        the view basis, Q / E drop / raise it, Shift boosts, and the scroll wheel raises / lowers the persistent fly SPEED
//        (the Unreal convention — scroll while flying does not dolly). This is a free flight through the scene.
//      • Alt held  → ORBIT / PAN about the target (the DCC tumble). Alt + LMB-drag orbits, Alt + MMB-drag pans in the view plane.
//      • Neither    → scroll dollies the camera toward / away from the target (the idle-inspect zoom).
//
//    The look rotation is routed through ResolveLookIncrement, which applies whichever jitter strategy F1 has selected. Pan and
//    dolly stay raw — pan is distance-scaled and dolly is per-notch, neither exhibits the slow-drag quantization jitter.
void DriveViewportCamera(RenderExtension& Extension, float DeltaSeconds)
{
    ViewportCamera&    Camera = Extension.ViewCamera;
    const InputPacket& Input  = Extension.Substrate.Window.Input;

    const float OrbitSensitivity = 0.004f;   // [rad/px] - Pointer delta → orbit / look angle (tuned for raw device counts on a high-DPI mouse)
    const float PanScaleFactor   = 0.001f;   // [-]      - Distance-scaled pan gain
    const float DollyStep        = 0.12f;    // [-]      - Fraction of current distance per scroll notch (idle zoom)
    const float FlySpeedBase      = 6.0f;    // [m/s]    - WASD walk speed at FlySpeedScale = 1
    const float BoostFactor       = 4.0f;    // [-]      - Walk-speed multiplier while Shift held
    const float FlySpeedStep       = 1.15f;  // [-]      - Per-scroll-notch multiplier on the persistent fly speed

    const bool  FlyMode   = PacketButtonHeld(Input, PointerButton::Right);
    const bool  OrbitMode = PacketKeyHeld(Input, KeyIdentity::LeftAlt) || PacketKeyHeld(Input, KeyIdentity::RightAlt);
    const bool  BoostHeld = PacketKeyHeld(Input, KeyIdentity::LeftShift) || PacketKeyHeld(Input, KeyIdentity::RightShift);

    const float LookX  = (float)Input.PointerDeltaX;  // [px] - summed pointer delta this frame (device counts, undivided)
    const float LookY  = (float)Input.PointerDeltaY;  // [px]
    const float Scroll = (float)Input.ScrollDelta;    // [notch]

    // Is a look-drag (as opposed to pan) engaged this frame? Fly = RMB look; orbit = Alt + LMB. Pan (Alt+MMB) is NOT a look.
    const bool  OrbitLookDrag = OrbitMode && PacketButtonHeld(Input, PointerButton::Left) && !PacketButtonHeld(Input, PointerButton::Middle);
    const bool  LookDrag      = FlyMode || OrbitLookDrag;

    if (FlyMode)
    {
        // -- Unreal fly. Mouse-look turns the view; scroll tunes the persistent walk speed; WASD/QE walk the camera. ------
        float YawIncrement = 0.0f, PitchIncrement = 0.0f;
        ResolveLookIncrement(Extension, LookDrag, LookX, LookY, OrbitSensitivity, DeltaSeconds, YawIncrement, PitchIncrement);
        OrbitViewportCamera(Camera, -YawIncrement, -PitchIncrement);   // -pitch: drag up looks up (ResolveOrbitRotation negates pitch so -Pitch lifts the eye)

        if (std::fabs(Scroll) > 1e-5f)
        {
            Extension.FlySpeedScale *= std::pow(FlySpeedStep, Scroll);
            if (Extension.FlySpeedScale < 0.05f) Extension.FlySpeedScale = 0.05f;
            if (Extension.FlySpeedScale > 40.0f) Extension.FlySpeedScale = 40.0f;
        }

        const float Forward = (PacketKeyHeld(Input, KeyIdentity::W) ? 1.0f : 0.0f) - (PacketKeyHeld(Input, KeyIdentity::S) ? 1.0f : 0.0f);
        const float Right   = (PacketKeyHeld(Input, KeyIdentity::D) ? 1.0f : 0.0f) - (PacketKeyHeld(Input, KeyIdentity::A) ? 1.0f : 0.0f);
        const float Up      = (PacketKeyHeld(Input, KeyIdentity::E) ? 1.0f : 0.0f) - (PacketKeyHeld(Input, KeyIdentity::Q) ? 1.0f : 0.0f);
        if (std::fabs(Forward) > 1e-5f || std::fabs(Right) > 1e-5f || std::fabs(Up) > 1e-5f)
        {
            const float Speed = FlySpeedBase * Extension.FlySpeedScale * (BoostHeld ? BoostFactor : 1.0f) * DeltaSeconds;
            FlyViewportCamera(Camera, Forward * Speed, Right * Speed, Up * Speed);
        }
    }
    else if (OrbitMode)
    {
        // -- DCC tumble. Alt + LMB orbits about the target; Alt + MMB pans in the view plane, distance-scaled. ------------
        if (PacketButtonHeld(Input, PointerButton::Middle))
        {
            const float PanScale = Camera.Distance * PanScaleFactor;
            PanViewportCamera(Camera, -LookX * PanScale, LookY * PanScale);
            // Pan is not a look-drag; the trailing !LookDrag guard settles the anchor/velocity state so the next orbit is clean.
        }
        else if (PacketButtonHeld(Input, PointerButton::Left))
        {
            float YawIncrement = 0.0f, PitchIncrement = 0.0f;
            ResolveLookIncrement(Extension, LookDrag, LookX, LookY, OrbitSensitivity, DeltaSeconds, YawIncrement, PitchIncrement);
            OrbitViewportCamera(Camera, -YawIncrement, -PitchIncrement);   // -pitch: drag up looks up (ResolveOrbitRotation negates pitch so -Pitch lifts the eye)
        }
    }
    else
    {
        // -- Idle. Scroll dollies toward / away from the target, proportional to distance so a notch feels the same near or far.
        if (std::fabs(Scroll) > 1e-5f)
            DollyViewportCamera(Camera, -Scroll * DollyStep * Camera.Distance);
    }

    // Any frame with no active look-drag settles the filter state (resets anchor / bleeds velocity) so the next drag is clean.
    if (!LookDrag)
    {
        float DiscardA = 0.0f, DiscardB = 0.0f;
        ResolveLookIncrement(Extension, false, 0.0f, 0.0f, OrbitSensitivity, DeltaSeconds, DiscardA, DiscardB);
    }
}

// 📝 The baseline verdict. The renderer's floor is GTX-1060-class Pascal: it needs int64 image atomics for the software
//    micro-raster and a discrete or integrated GPU (CPU / virtual devices cannot sustain the visibility pipeline). We treat a
//    device with the atomics and a real GPU type as at-or-above baseline; the atomics are the hard gate.
bool DeriveArchitectureBaseline(const VkPhysicalDeviceProperties& Properties, bool Int64AtomicsEnabled)
{
    bool RealDeviceCondition = Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
                            || Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    return Int64AtomicsEnabled && RealDeviceCondition;
}

// Fill the grid push data from the derived frames: the inverse view-projection (so the shader unprojects each pixel to the
// ground plane), the world-space eye, the active lens, and the view centre. The orbit spec stores no matrices — the view +
// projection frames are derived here.
void AssembleGridConstants(const ViewportCamera& Subject, GroundGridConstants& Constants)
{
    const FocalOrientation Frame          = SolveOrbitOrientation(Subject);
    const Matrix4f         Projection     = EvaluateProjectionFrame(Subject);
    const Matrix4f         ViewProjection = MultiplyMatrix(Projection, Frame.ViewMatrix);
    Constants.InverseViewProjection = InvertMatrix(ViewProjection);
    Constants.CameraPosition[0] = Frame.EyePosition.XCoord;
    Constants.CameraPosition[1] = Frame.EyePosition.YCoord;
    Constants.CameraPosition[2] = Frame.EyePosition.ZCoord;
    Constants.CameraPosition[3] = 0.0f;

    // 📝 The lens flag selects the shader's ray reconstruction (parallel near→far under ortho, eye-fan under perspective), and
    //    FocalCentre anchors the radial fade to the orbit Target rather than an eye that ortho may place arbitrarily far out.
    //    See the GroundGridConstants note — omitting these stretches the ortho grid.
    Constants.OrthographicEnabled = (Subject.Projection == ProjectionMode::Orthographic) ? 1.0f : 0.0f;
    Constants.FocalCentre[0] = Subject.Target.XCoord;
    Constants.FocalCentre[1] = Subject.Target.YCoord;
    Constants.FocalCentre[2] = Subject.Target.ZCoord;
    Constants.FocalCentre[3] = 0.0f;
}

// Fill the sky-dome push data from the same derived frames as the grid: the sky shader unprojects each pixel to a world view
// ray through this inverse view-projection, so sky and grid share one camera. The sun / exposure knobs keep their struct defaults.
void AssembleSkyConstants(const ViewportCamera& Subject, SkyDomeConstants& Constants)
{
    const FocalOrientation Frame          = SolveOrbitOrientation(Subject);
    const Matrix4f         Projection     = EvaluateProjectionFrame(Subject);
    const Matrix4f         ViewProjection = MultiplyMatrix(Projection, Frame.ViewMatrix);
    const Matrix4f         Inverse        = InvertMatrix(ViewProjection);
    // Matrix4f is column-major float[4][4]; SkyDomeConstants.InverseViewProjection is a flat column-major float[16].
    for (int Column = 0; Column < 4; Column++)
        for (int Row = 0; Row < 4; Row++)
            Constants.InverseViewProjection[Column * 4 + Row] = Inverse.Column[Column][Row];
    Constants.CameraPosition[0] = Frame.EyePosition.XCoord;
    Constants.CameraPosition[1] = Frame.EyePosition.YCoord;
    Constants.CameraPosition[2] = Frame.EyePosition.ZCoord;
    Constants.CameraPosition[3] = 0.0f;
}

// Fill the deferred shade's push data. Same derivation as the sky above — the shade unprojects each pixel to a world-space view RAY through this
// inverse view-projection, then intersects that ray with the unpacked triangle to recover the barycentric weights the raster never stored, so shade
// and sky must share one camera or the reconstruction lands on the wrong surface point. CameraPosition is both the ray origin and the BRDF view vector.
// LightDirection is a fixed world-space key light (normalized, pointing TOWARD the light) — a real sun hook-up arrives with the shadow clipmap.
// CompositeMask overrides the Composite record's own mask; 0 means "use the record's".
void AssembleSurfaceShadeConstants(const ViewportCamera& Subject, uint32_t CompositeMask, SurfaceShadeConstants& Constants)
{
    const FocalOrientation Frame          = SolveOrbitOrientation(Subject);
    const Matrix4f         Projection     = EvaluateProjectionFrame(Subject);
    const Matrix4f         ViewProjection = MultiplyMatrix(Projection, Frame.ViewMatrix);
    const Matrix4f         Inverse        = InvertMatrix(ViewProjection);
    for (int Column = 0; Column < 4; Column++)
        for (int Row = 0; Row < 4; Row++)
            Constants.InverseViewProjection[Column * 4 + Row] = Inverse.Column[Column][Row];

    Constants.CameraPosition[0] = Frame.EyePosition.XCoord;
    Constants.CameraPosition[1] = Frame.EyePosition.YCoord;
    Constants.CameraPosition[2] = Frame.EyePosition.ZCoord;
    Constants.CameraPosition[3] = 1.0f;

    // A high, slightly-off-axis key so the rings read three-dimensionally: every head catches both a lit side and a terminator, which is what makes
    // roughness legible (a head-on light flattens Chrome and Plastic into the same disc). Normalized because the BRDF assumes a unit L.
    const float LightX = 0.40f;
    const float LightY = -0.55f;
    const float LightZ = 0.73f;
    const float LightLength = std::sqrt(LightX * LightX + LightY * LightY + LightZ * LightZ);
    Constants.LightDirection[0] = LightX / LightLength;
    Constants.LightDirection[1] = LightY / LightLength;
    Constants.LightDirection[2] = LightZ / LightLength;
    Constants.LightDirection[3] = 0.0f;

    Constants.CompositeFeatureMask = CompositeMask;
    Constants.FloorPartitionBase   = FloorPartitionBase;
}

#ifdef FRONTIER_POLYGON_AUTHORING
// Fill the component overlay's push data. Shares the camera derivation with the shade above, but takes the FORWARD view-projection rather than its
// inverse: the overlay only ever transforms world -> screen (projecting a reconstructed triangle's three corners to measure pixel distances), never the
// reverse. Passing the forward matrix is what lets the fragment stage skip a per-pixel mat4 inverse.
// ⚠️ The camera must be the SAME one the frame was rasterized with, or every handle lands offset from the surface it belongs to — the id buffer names a
//    triangle, and this matrix decides where that triangle's corners are on screen. Deriving it here from Extension.ViewCamera (as the shade and sky do)
//    is what keeps the three in lockstep.
void AssembleComponentOverlayConstants(const ViewportCamera&        Subject,
                                       ComponentSelectionMode       Mode,
                                       uint32_t                     SelectedPartition,
                                       uint32_t                     SelectedPrimitive,
                                       uint32_t                     SelectedComponent,
                                       int32_t                      CursorX,
                                       int32_t                      CursorY,
                                       ComponentOverlayConstants&   Constants)
{
    const FocalOrientation Frame          = SolveOrbitOrientation(Subject);
    const Matrix4f         Projection     = EvaluateProjectionFrame(Subject);
    const Matrix4f         ViewProjection = MultiplyMatrix(Projection, Frame.ViewMatrix);
    for (int Column = 0; Column < 4; Column++)
        for (int Row = 0; Row < 4; Row++)
            Constants.ViewProjection[Column * 4 + Row] = ViewProjection.Column[Column][Row];

    Constants.ComponentMode      = (uint32_t)Mode;
    Constants.SelectedPartition  = SelectedPartition;
    Constants.SelectedPrimitive  = SelectedPrimitive;
    Constants.SelectedComponent  = SelectedComponent;
    Constants.FloorPartitionBase = FloorPartitionBase;

    // 📝 The raw cursor, not a resolved hover — the shader re-runs its own reconstruction there to decide what is hovered. Negative (pointer outside the
    //    window) is passed through unchanged and read as "nothing hovered".
    Constants.CursorX = CursorX;
    Constants.CursorY = CursorY;
}
#endif // FRONTIER_POLYGON_AUTHORING

// 📝 How much of the clipmap the debug viz shows and how much of it the stub fill keeps resident, in cells either side of the camera cell. The
//    fill radius is deliberately SMALLER than the display radius so the outer ring of every level reads vacant on screen — that ring is what makes
//    the scroll visible: it slides against the camera and the cells it uncovers flip to vacant, then relight as the fill catches them.
constexpr int32_t ClipmapDisplayShellRadius   = 2;   // [cells] - half-extent of the SMALL fine-level camera grid (occupied cells are drawn directly, not gated by this shell — see ImportantNotes 2026-07-29)
constexpr int32_t ClipmapResidencyFillRadius  = 3;   // [cells] - half-extent the stub fill marks resident, per level (kept ≥ the display radius so the whole camera grid reads resident + relights)
constexpr float   ClipmapRelightRampRate      = 0.04f; // [-]   - per-frame ramp climb (≈0.4 s from scroll-in to fully relit at 60 fps)

// Advance the clipmap spine to the camera: scroll every level (which invalidates only the newly-exposed L-slabs), then run the stub fill that marks
// a shell around the camera resident, then climb the relight ramp. This is the whole P5c streaming path — the scroll/invalidate/refill cycle both
// the sun-shadow clipmap (P6) and the GI probe clipmap (P7b) will drive, with their real payload standing where the ramp stands today.
void IntegrateClipmapField(ToroidalClipmapField& Field, Vector3f CameraPosition)
{
    for (uint32_t LevelIndex = 0; LevelIndex < Field.LevelCount; ++LevelIndex)
    {
        const ClipmapScrollResult ScrollOutcome = EvaluateClipmapScroll(Field, LevelIndex, CameraPosition);
        IntegrateScrollResidency(Field, ScrollOutcome);

        // Stub fill: whatever the scroll vacated inside the fill shell is re-marked resident with its ramp at zero, so it relights over the next
        // handful of frames. A real consumer would stream/rasterize the cell here instead of merely flipping the residency bit.
        const CellCoordinate CameraCell = ResolveCameraCell(Field, LevelIndex, CameraPosition);
        for (int32_t ZOffset = -ClipmapResidencyFillRadius; ZOffset <= ClipmapResidencyFillRadius; ++ZOffset)
            for (int32_t YOffset = -ClipmapResidencyFillRadius; YOffset <= ClipmapResidencyFillRadius; ++YOffset)
                for (int32_t XOffset = -ClipmapResidencyFillRadius; XOffset <= ClipmapResidencyFillRadius; ++XOffset)
                {
                    ActivateWorldCell(Field, LevelIndex,
                                      CellCoordinate{ CameraCell.XCell + XOffset,
                                                      CameraCell.YCell + YOffset,
                                                      CameraCell.ZCell + ZOffset });
                }
    }

    AdvanceRelightRamp(Field, ClipmapRelightRampRate);
}

#ifdef FRONTIER_DEVELOPMENT_PROFILE
// Fill the clipmap visualization's push data. Unlike the flat-array constants above, ClipmapInspectionConstants carries a Matrix4f directly, so the
// world -> clip matrix is assigned whole (the shader's mat4 reads Matrix4f's column-major storage as-is). The opacity / marker-size knobs keep their
// struct defaults — they are tuning, not per-frame state.
void AssembleClipmapInspectionConstants(const ViewportCamera& Subject, ClipmapInspectionConstants& Constants)
{
    const FocalOrientation Frame      = SolveOrbitOrientation(Subject);
    const Matrix4f         Projection = EvaluateProjectionFrame(Subject);
    Constants.ViewProjection = MultiplyMatrix(Projection, Frame.ViewMatrix);
}
#endif

// Fill the visibility raster's push data: the world -> clip matrix for the current camera, flattened column-major (Matrix4f is Column[c][r]; the
// shader's mat4 reads it as col*4 + row, so no transpose). The raster needs only the transform — identity + tint travel in the instance buffer.
void AssembleVisibilityConstants(const ViewportCamera& Subject, VisibilityRasterConstants& Constants)
{
    const FocalOrientation Frame          = SolveOrbitOrientation(Subject);
    const Matrix4f         Projection     = EvaluateProjectionFrame(Subject);
    const Matrix4f         ViewProjection = MultiplyMatrix(Projection, Frame.ViewMatrix);
    for (int Column = 0; Column < 4; ++Column)
        for (int Row = 0; Row < 4; ++Row)
            Constants.ViewProjection[Column * 4 + Row] = ViewProjection.Column[Column][Row];
}

// Fill the GPU cull's push data from the same camera the raster uses: the world -> clip matrix, the six INWARD frustum planes extracted from it, the
// world-space camera origin (the cone view direction), the HiZ extent + level count, and the record count / pass selector (filled by the caller). The
// plane extraction is Gribb-Hartmann for a column-major matrix with the point on the right (clip = M * vec4(world,1)): each plane's (a,b,c,d) is a
// combination of the matrix ROWS, where row i = (Column[0][i], Column[1][i], Column[2][i], Column[3][i]). The planes are normalized so the shader's
// signed distance centre.n + d is a true world-space distance (its radius test relies on that). ViewportExtent + PyramidLevelCount come from the pyramid.
void AssembleCullConstants(const ViewportCamera&          Subject,
                           const HierarchicalDepthPyramid& Pyramid,
                           uint32_t                        RecordCount,
                           uint32_t                        LatePassEnabled,
                           InstanceCullConstants&          Constants)
{
    const FocalOrientation Frame          = SolveOrbitOrientation(Subject);
    const Matrix4f         Projection     = EvaluateProjectionFrame(Subject);
    const Matrix4f         ViewProjection = MultiplyMatrix(Projection, Frame.ViewMatrix);

    for (int Column = 0; Column < 4; ++Column)
        for (int Row = 0; Row < 4; ++Row)
            Constants.ViewProjection[Column * 4 + Row] = ViewProjection.Column[Column][Row];

    // Matrix rows (Column[c][r] indexing): Row[r] = (Column[0][r], Column[1][r], Column[2][r], Column[3][r]).
    const float Row0[4] = { ViewProjection.Column[0][0], ViewProjection.Column[1][0], ViewProjection.Column[2][0], ViewProjection.Column[3][0] };
    const float Row1[4] = { ViewProjection.Column[0][1], ViewProjection.Column[1][1], ViewProjection.Column[2][1], ViewProjection.Column[3][1] };
    const float Row2[4] = { ViewProjection.Column[0][2], ViewProjection.Column[1][2], ViewProjection.Column[2][2], ViewProjection.Column[3][2] };
    const float Row3[4] = { ViewProjection.Column[0][3], ViewProjection.Column[1][3], ViewProjection.Column[2][3], ViewProjection.Column[3][3] };

    // Six inward planes: left = w+x, right = w-x, bottom = w+y, top = w-y, near = w+z (standard-Z / GL clip -w..w in z here is w+z), far = w-z.
    // (Gribb-Hartmann; each row-sum yields an inward-pointing plane before normalization.)
    float Planes[6][4];
    for (int Component = 0; Component < 4; ++Component)
    {
        Planes[0][Component] = Row3[Component] + Row0[Component];   // left
        Planes[1][Component] = Row3[Component] - Row0[Component];   // right
        Planes[2][Component] = Row3[Component] + Row1[Component];   // bottom
        Planes[3][Component] = Row3[Component] - Row1[Component];   // top
        Planes[4][Component] = Row3[Component] + Row2[Component];   // near
        Planes[5][Component] = Row3[Component] - Row2[Component];   // far
    }

    for (int Plane = 0; Plane < 6; ++Plane)
    {
        const float NormalLength = std::sqrt(Planes[Plane][0] * Planes[Plane][0] +
                                             Planes[Plane][1] * Planes[Plane][1] +
                                             Planes[Plane][2] * Planes[Plane][2]);
        const float Inverse = (NormalLength > 1e-8f) ? (1.0f / NormalLength) : 0.0f;
        Constants.FrustumPlanes[Plane * 4 + 0] = Planes[Plane][0] * Inverse;
        Constants.FrustumPlanes[Plane * 4 + 1] = Planes[Plane][1] * Inverse;
        Constants.FrustumPlanes[Plane * 4 + 2] = Planes[Plane][2] * Inverse;
        Constants.FrustumPlanes[Plane * 4 + 3] = Planes[Plane][3] * Inverse;
    }

    Constants.CameraOrigin[0] = Frame.EyePosition.XCoord;
    Constants.CameraOrigin[1] = Frame.EyePosition.YCoord;
    Constants.CameraOrigin[2] = Frame.EyePosition.ZCoord;
    Constants.CameraOrigin[3] = 0.0f;

    Constants.ViewportExtentX   = (float)Pyramid.Width;
    Constants.ViewportExtentY   = (float)Pyramid.Height;
    Constants.PyramidLevelCount = (int32_t)Pyramid.LevelCount;
    Constants.RecordCount       = RecordCount;
    Constants.LatePassEnabled   = LatePassEnabled;
}

// Fit the shared mesh's local bounding sphere over its CPU vertex positions (centroid centre + farthest-point radius — loose but always enclosing,
// which is all the cull needs) into LocalSphere = { cx, cy, cz, radius }. The normal cone is left non-coneable (LocalCone.w = -1): the Suzanne heads are
// closed solids whose face normals span every direction, so a whole-mesh cone can never be uniformly back-facing — backface rejection belongs to P4's
// per-partition cones, not this per-instance record. A degenerate (empty) stream yields a zero sphere, which the cull treats as always-visible.
void FitMeshLocalBounds(const RenderVertexStream& Stream, float LocalSphere[4], float LocalCone[4])
{
    LocalSphere[0] = LocalSphere[1] = LocalSphere[2] = LocalSphere[3] = 0.0f;
    LocalCone[0] = LocalCone[1] = LocalCone[2] = 0.0f;
    LocalCone[3] = -1.0f;   // non-coneable

    const size_t Count = Stream.Vertices.size();
    if (Count == 0)
        return;

    double SumX = 0.0, SumY = 0.0, SumZ = 0.0;
    for (const RenderVertex& Vertex : Stream.Vertices)
    {
        SumX += Vertex.Position[0];
        SumY += Vertex.Position[1];
        SumZ += Vertex.Position[2];
    }
    const float CentreX = (float)(SumX / (double)Count);
    const float CentreY = (float)(SumY / (double)Count);
    const float CentreZ = (float)(SumZ / (double)Count);

    float RadiusSquared = 0.0f;
    for (const RenderVertex& Vertex : Stream.Vertices)
    {
        const float OffsetX = Vertex.Position[0] - CentreX;
        const float OffsetY = Vertex.Position[1] - CentreY;
        const float OffsetZ = Vertex.Position[2] - CentreZ;
        const float DistanceSquared = OffsetX * OffsetX + OffsetY * OffsetY + OffsetZ * OffsetZ;
        if (DistanceSquared > RadiusSquared)
            RadiusSquared = DistanceSquared;
    }

    LocalSphere[0] = CentreX;
    LocalSphere[1] = CentreY;
    LocalSphere[2] = CentreZ;
    LocalSphere[3] = std::sqrt(RadiusSquared);
}

// 📝 Upper bound on candidate cell tests one voxelization sweep may spend. A triangle's candidate box is its own bounds in cells, so a
//    scene-spanning triangle (the floor slab is exactly one) alone proposes the whole field — published measurements put a single such triangle
//    at ~170 ms against a 512³ grid. This cap keeps a pathological mesh from stalling load, and the sweep REPORTS what it refused so a truncated
//    marking is never mistaken for full coverage.
constexpr uint32_t ClipmapOccupancyCellBudget = 4000000;

// Voxelize one loaded mesh's instances into Extension.OccupiedWorldCells at the occupancy level, appending to whatever is already there (so the
// heads and the floor accumulate into one set). Runs the exact per-triangle predicate — see TriangleCellOverlap.h for why a bounding box is not
// an acceptable substitute. Reports the marked / tested counts, and CAUTIONS when the budget truncated the sweep.
void VoxelizeSceneOccupancy(RenderExtension&                         Extension,
                            const RenderVertexStream&                Stream,
                            const std::vector<SuzanneSceneInstance>& Instances,
                            const char*                              StreamLabel)
{
    if (Stream.Vertices.empty() || Stream.Indices.empty() || Instances.empty())
        return;

    // RenderVertex is interleaved position/normal/uv, so the position stride is the struct's float count and the stream base is its first field.
    const float*   PositionStream = &Stream.Vertices.front().Position[0];
    const uint32_t PositionStride = (uint32_t)(sizeof(RenderVertex) / sizeof(float));

    uint32_t TotalTested  = 0;
    uint32_t TotalRefused = 0;
    for (const SuzanneSceneInstance& Placement : Instances)
    {
        const CellOverlapOutcome Outcome =
            VoxelizeTriangleStream(PositionStream, PositionStride, (uint32_t)Stream.Vertices.size(),
                                   Stream.Indices.data(), (uint32_t)Stream.Indices.size(),
                                   Placement.Model, Extension.ClipmapField, Extension.ClipmapOccupancyLevel,
                                   CellOverlapSeparation::Supercover26, ClipmapOccupancyCellBudget,
                                   Extension.OccupiedWorldCells);
        TotalTested  += Outcome.TestedCellCount;
        TotalRefused += Outcome.RefusedCellCount;
    }

    ISSUE_NOTICE("render-extension", "clipmap occupancy voxelized ('%s'): %u cells from %u instances x %u triangles at level %u (%u cell tests)",
                 StreamLabel, (unsigned)Extension.OccupiedWorldCells.size(), (unsigned)Instances.size(),
                 (unsigned)(Stream.Indices.size() / 3), (unsigned)Extension.ClipmapOccupancyLevel, (unsigned)TotalTested);

    if (TotalRefused > 0)
    {
        ISSUE_CAUTION("render-extension", "clipmap occupancy INCOMPLETE ('%s'): the %u-test budget refused %u candidate cells — marked cells "
                                          "under-cover the scene", StreamLabel, (unsigned)ClipmapOccupancyCellBudget, (unsigned)TotalRefused);
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InspectHardwareFeatures(const VulkanHost& Host, HardwareFeatureProfile& Profile)
{
    if (Host.PhysicalDevice == VK_NULL_HANDLE)
        return;

    VkPhysicalDeviceProperties Properties = {};
    vkGetPhysicalDeviceProperties(Host.PhysicalDevice, &Properties);
    std::strncpy(Profile.DeviceName, Properties.deviceName, sizeof(Profile.DeviceName) - 1);
    Profile.DriverApiVersion = Properties.apiVersion;

    // ① Chain the int64-atomic feature structs onto the base features query so a single call reports both.
    VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT ImageAtomicFeatures = {};
    ImageAtomicFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT;

    VkPhysicalDeviceShaderAtomicInt64Features BufferAtomicFeatures = {};
    BufferAtomicFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES;
    BufferAtomicFeatures.pNext = &ImageAtomicFeatures;

    VkPhysicalDeviceFeatures2 Features = {};
    Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    Features.pNext = &BufferAtomicFeatures;
    vkGetPhysicalDeviceFeatures2(Host.PhysicalDevice, &Features);

    // ② The micro-raster packs depth+id via image atomicMax; buffer int64 atomics are the fallback packing path.
    Profile.Int64AtomicsEnabled = ImageAtomicFeatures.shaderImageInt64Atomics == VK_TRUE
                               || BufferAtomicFeatures.shaderBufferInt64Atomics == VK_TRUE;
    Profile.ArchitectureBaselineCondition = DeriveArchitectureBaseline(Properties, Profile.Int64AtomicsEnabled);
}

bool InitializeRenderExtension(RenderExtension& Extension,
                               const char*      TitleText,
                               uint32_t         RequestedWidth,
                               uint32_t         RequestedHeight)
{
    if (!InitializeWindowSubstrate(Extension.Substrate, TitleText, RequestedWidth, RequestedHeight))
    {
        ISSUE_FAULT("render-extension", "window substrate bring-up failed");
        return false;
    }

    InspectHardwareFeatures(Extension.Substrate.Host, Extension.FeatureProfile);
    const HardwareFeatureProfile& FeatureProfile = Extension.FeatureProfile;
    ISSUE_NOTICE("render-extension", "GPU: %s (API %u.%u.%u)",
                 FeatureProfile.DeviceName,
                 VK_API_VERSION_MAJOR(FeatureProfile.DriverApiVersion),
                 VK_API_VERSION_MINOR(FeatureProfile.DriverApiVersion),
                 VK_API_VERSION_PATCH(FeatureProfile.DriverApiVersion));
    ISSUE_NOTICE("render-extension", "int64 shader atomics: %s", FeatureProfile.Int64AtomicsEnabled ? "yes" : "no");
    ISSUE_NOTICE("render-extension", "architecture baseline (Pascal / GTX-1060 floor): %s",
                 FeatureProfile.ArchitectureBaselineCondition ? "met" : "BELOW FLOOR");

    // ⚠️ Below-floor devices still present a clear window at 0.1; the micro-raster (Phase 3) is where the gate becomes fatal.
    if (!FeatureProfile.ArchitectureBaselineCondition)
        ISSUE_CAUTION("render-extension", "device below baseline — visibility passes will be unavailable in later phases");

    // -- Camera: input is read directly off the window's InputPacket each frame; no separate input pipeline stands up. The
    //    camera is Unreal-style (RMB fly + WASD, Alt-drag orbit / pan, scroll dolly / fly-speed). -------------------------
    Extension.FlySpeedScale = 1.0f;
    Extension.ViewCamera = ResolveDefaultPerspectiveCamera();
    ConformCameraAspect(Extension.ViewCamera, Extension.Substrate.Extent.width, Extension.Substrate.Extent.height);

    // -- Ground-grid pass: reads the swapchain colour format; skipped gracefully if dynamic rendering is absent. ---------
#ifndef FRONTIER_GRID_SHADER_DIR
#define FRONTIER_GRID_SHADER_DIR "Shaders"
#endif
    InitializeGroundGridPass(Extension.GridPass, Extension.Substrate.Host, Extension.Substrate.SurfaceFormat, FRONTIER_GRID_SHADER_DIR);

    // -- Sky/atmosphere pass: baked once, drawn behind the grid each frame. Skipped gracefully if it cannot build. --------
#ifndef FRONTIER_SKY_SHADER_DIR
#define FRONTIER_SKY_SHADER_DIR "Shaders"
#endif
    if (InitializeSkyAtmospherePass(Extension.SkyPass, Extension.Substrate.Host,
                                    Extension.Substrate.SurfaceFormat, FRONTIER_SKY_SHADER_DIR))
    {
        BakeSkyAtmosphereConstants(Extension.SkyPass, Extension.Substrate.Host);
    }

    // -- Render schedule (Phase 0): assemble the ordered spine with the two forward steps that exist today, in the same order
    //    the legacy inline recorder walks them (sky fills the background, grid alpha-blends over it). The schedule is left
    //    default-OFF: the recorder takes the legacy path until EnabledCondition is flipped, which is the pixel-identity A/B. Both
    //    step bodies re-derive their push constants from the CURRENT camera each frame, so they track the same live view the
    //    inline path does. Later phases append their steps after these two rather than growing the recorder lambda. --------
    // -- Scene depth target (Phase 2): the renderer-owned D32 offscreen depth the HiZ pyramid will reduce. Sized to the current
    //    swapchain extent; rebuilt on resize inside the preamble. Skipped gracefully (ReadyCondition stays false) if it cannot
    //    allocate — the depth preamble then records nothing and the colour path is unaffected. ------------------------------
    InitializeVisibilityDepth(Extension.DepthTarget, Extension.Substrate.Host,
                              Extension.Substrate.Extent.width, Extension.Substrate.Extent.height);

    // -- HiZ pyramid (Phase 1): the max-reduce mip chain over the depth target. Sized to the same extent; rebuilt on resize in the
    //    preamble alongside the depth target. Skipped gracefully (ReadyCondition stays false) if it cannot build — the reduce then
    //    records nothing. No cull consumer exists yet: the pyramid is produced and its apex is the P1 validation gate. --------------
#ifndef FRONTIER_HIZ_SHADER_DIR
#define FRONTIER_HIZ_SHADER_DIR "Shaders"
#endif
    InitializeHierarchicalDepthPyramid(Extension.DepthPyramid, Extension.Substrate.Host,
                                       Extension.Substrate.Extent.width, Extension.Substrate.Extent.height,
                                       FRONTIER_HIZ_SHADER_DIR);

    // -- Visibility buffer (Phase 2b): the R32_UINT id target the hardware raster writes, paired with the depth target above. Sized to the
    //    current extent; rebuilt on resize in the preamble alongside depth. Skipped gracefully (ReadyCondition stays false) if it cannot allocate.
    InitializeVisibilityImage(Extension.VisibilityTarget, Extension.Substrate.Host,
                              Extension.Substrate.Extent.width, Extension.Substrate.Extent.height);

    // -- Visibility raster (Phase 2b): the instanced hardware draw of the Suzanne scene into the visibility buffer + depth. Built against the
    //    R32_UINT colour + D32 depth formats. Capacity covers the pyramid-stress worst case (10 pyramids x 91 heads = 910). Skipped gracefully.
#ifndef FRONTIER_VISIBILITY_SHADER_DIR
#define FRONTIER_VISIBILITY_SHADER_DIR "Shaders"
#endif
    InitializeVisibilityRasterization(Extension.VisibilityRaster, Extension.Substrate.Host,
                                      VisibilityImageFormat, VisibilityDepthFormat,
                                      1024, FRONTIER_VISIBILITY_SHADER_DIR);

    // -- Floor raster (checkered floor): a SECOND visibility raster whose own pipeline + instance set draw the floor slab into the SAME visibility
    //    buffer + depth as the heads. Byte-identical pipeline; a separate object only so the floor carries its own one-instance storage buffer + set.
    //    Capacity 1 (the floor doc holds one object). Drawn inside the shared Begin/End scope in the preamble so the heads occlude / rest on it.
    InitializeVisibilityRasterization(Extension.FloorRaster, Extension.Substrate.Host,
                                      VisibilityImageFormat, VisibilityDepthFormat,
                                      1, FRONTIER_VISIBILITY_SHADER_DIR);

    // -- Instance cull (Phase 3, P3): the GPU-driven per-instance two-pass cull whose survivor list + indirect argument drive the raster when the
    //    VisibilityScaling toggle is on. Same MaxInstances capacity as the raster. Built once (pipeline size-independent); the record SSBO is uploaded
    //    with the scene below, and the raster's survivor binding is re-pointed at this cull's SurvivorBuffer once both are live. Skipped gracefully —
    //    a failed build leaves ReadyCondition false, so the preamble falls back to the plain instanced draw regardless of the toggle.
    InitializeInstanceCullSubmission(Extension.InstanceCull, Extension.Substrate.Host, 1024, FRONTIER_VISIBILITY_SHADER_DIR);

    // -- Software micro-raster (Phase 3, P4): the compute raster that atomicMaxes a packed (depth|id) word into the R64 target VisibilityImage owns,
    //    then a fullscreen resolve unpacks it into the SAME R32_UINT id buffer + D32 depth the hardware raster fills — so Numpad-3 is an id-identical
    //    A/B against the hardware path. Built against the same colour + depth formats. The route (image / buffer) is chosen from the host's int64-atomics
    //    features; a device without them leaves ReadyCondition false and the Numpad-3 toggle inert (the preamble keeps the hardware raster). Skipped
    //    gracefully. Its scene + packed-target bindings are wired below once the geometry + packed target are live.
    InitializeSoftwareRasterization(Extension.SoftwareRaster, Extension.Substrate.Host,
                                    VisibilityImageFormat, VisibilityDepthFormat, FRONTIER_VISIBILITY_SHADER_DIR);

    // -- Visibility resolve (Phase 2b): the fullscreen composite that reads the id buffer and writes a debug colour over sky + grid — the on-screen
    //    A/B for the raster. Built against the SWAPCHAIN colour format (it composites into the presented image, not the R32_UINT target). Default
    //    OFF (VisibilityResolveEnabled = false); F2 flips it live. Its descriptor is pointed at the visibility image's view once here and re-pointed
    //    on every resize (the reconfigure rebuilds the view). Skipped gracefully — a failed build leaves the toggle inert.
    InitializeVisibilityInscription(Extension.VisibilityResolve, Extension.Substrate.Host,
                                    Extension.Substrate.SurfaceFormat, FRONTIER_VISIBILITY_SHADER_DIR);
    RefreshVisibilityInscription(Extension.VisibilityResolve, Extension.VisibilityTarget);
    Extension.VisibilityResolveExtentWidth  = Extension.VisibilityTarget.Width;
    Extension.VisibilityResolveExtentHeight = Extension.VisibilityTarget.Height;

    // -- Deferred surface shade (the material slice): the fullscreen pass that turns the same id buffer into genuinely lit materials. Built against the
    //    SWAPCHAIN colour format (it composites into the presented image, and its alpha-over blend is what makes the Glass preset read as transparent).
    //    The material table is resolved on the host and uploaded ONCE here — it is immutable, because runtime lobe toggling rides the push constant
    //    instead of a re-upload. Its descriptor also needs the MESH and INSTANCE buffers, which do not exist until the scene loads below, so Refresh is
    //    deferred to that point. Best-effort: a failed build leaves ReadyCondition false and the F4 toggle inert.
    InitializeSurfaceShadeInscription(Extension.SurfaceShade, Extension.Substrate.Host,
                                      Extension.Substrate.SurfaceFormat, FRONTIER_VISIBILITY_SHADER_DIR);
    if (Extension.SurfaceShade.ReadyCondition)
    {
        SurfacePresetParameters PresetTable[SurfacePresetCount];
        BuildSurfacePresetTable(PresetTable);
        UploadSurfaceShadeMaterials(Extension.SurfaceShade, PresetTable);
    }

    // -- Object selection (P3a): the one-texel pick readback + the outline composite. Both stand on the id buffer the raster already writes, so
    //    neither adds a geometry pass. The outline is built against the SWAPCHAIN colour format (it composites into the presented image) and its
    //    descriptor is pointed at the visibility image's view here, re-pointed on every resize alongside the resolve's. Both are best-effort: a
    //    failed build leaves ReadyCondition false, the F3 toggle inert, and the colour path untouched.
#ifdef FRONTIER_POLYGON_AUTHORING
    InitializeObjectPickReadback(Extension.ObjectPick, Extension.Substrate.Host);
    InitializeSelectionOutlineInscription(Extension.SelectionOutline, Extension.Substrate.Host,
                                         Extension.Substrate.SurfaceFormat, FRONTIER_VISIBILITY_SHADER_DIR);
    RefreshSelectionOutlineInscription(Extension.SelectionOutline, Extension.VisibilityTarget, Extension.DepthTarget);

    // 📝 The component overlay is built here beside its sibling but NOT refreshed here: it additionally indexes the geometry SSBOs, which do not exist
    //    until the scene document is uploaded further down. Its Refresh therefore lives beside the shade pass's, at the earliest point all three
    //    buffers are live. Same best-effort contract — a failed build leaves ReadyCondition false and Numpad-7 inert.
    InitializeComponentOverlayInscription(Extension.ComponentOverlay, Extension.Substrate.Host,
                                          Extension.Substrate.SurfaceFormat, FRONTIER_VISIBILITY_SHADER_DIR);
#endif

    // -- Clipmap spine (P5c): the camera-tracked 3D voxel clipmap both the sun-shadow clipmap (P6) and the GI probe clipmap (P7b) will stand on.
    //    Configured with the Unreal/Blender radius-doubling defaults (1 m base cell, 4 levels doubling to 8 m, 32 cells per cubic edge — level 0
    //    spans 32 m, level 3 spans 256 m). Pure host-side math; the field is advanced from the camera in the frame recorder below regardless of
    //    whether anything visualizes it, so its scroll/invalidate/refill path runs live from here on.
    // 📝 Configured BEFORE the scene load below, because the load voxelizes each instance against a level's cell size — the field has to exist
    //    (and know its cell metres) before there is anything to voxelize into. --------------------------------------------------------------
    ConfigureClipmapField(Extension.ClipmapField, ClipmapDefaultLevelCount, ClipmapDefaultResolution, ClipmapDefaultBaseCellMetres);
    ISSUE_NOTICE("render-extension", "clipmap field: %u levels, %u^3 cells, base cell %.2f m (level 0 spans %.0f m, level %u spans %.0f m)",
                 (unsigned)Extension.ClipmapField.LevelCount, (unsigned)ClipmapDefaultResolution, (double)ClipmapDefaultBaseCellMetres,
                 (double)(ClipmapDefaultResolution * Extension.ClipmapField.Levels.front().CellMetres),
                 (unsigned)(Extension.ClipmapField.LevelCount - 1),
                 (double)(ClipmapDefaultResolution * Extension.ClipmapField.Levels.back().CellMetres));

    // 📝 Which level occupancy is voxelized and displayed at. Level 0's 1 m cells over a 13-head scene would bury the view in cages; level 2's
    //    4 m cells read as a legible shell. The prebake below and the per-frame replay both key off this one value.
    Extension.ClipmapOccupancyLevel = std::min(2u, Extension.ClipmapField.LevelCount - 1u);

    // -- Scene geometry (Phase 2b): a one-shot transfer pool, then LOAD the saved scene document (.wsdoc) the chosen scene names — decode it, derive
    //    the shared geometry block's GPU stream through the engine's own ConstructRenderVertexStream, upload it device-local, recompose the placed
    //    objects into raster instances, and REGISTER the document into the scene directory (one outliner row per placed head). The renderer no longer
    //    constructs the scene in C++ (no BuildSuzanneScene / reference-JSON) — it loads it. All best-effort: a missing / malformed document leaves an
    //    empty geometry and the raster records nothing (ReadyCondition/InstanceCount gate it), so the colour path is unaffected.
    if (Extension.VisibilityRaster.ReadyCondition)
    {
        VkCommandPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        PoolInformation.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        PoolInformation.queueFamilyIndex = Extension.Substrate.Host.GraphicsQueueFamily;
        if (vkCreateCommandPool(Extension.Substrate.Host.Device, &PoolInformation, Extension.Substrate.Host.Allocator, &Extension.UploadPool) == VK_SUCCESS)
        {
#ifndef FRONTIER_SCENE_ASSET_DIR
#define FRONTIER_SCENE_ASSET_DIR "Assets"
#endif
            // One document per scene choice. MaterialRings is the material-slice scene: 13 heads in two rings, each placement carrying its own
            // SurfacePresetTable ordinal in the document's per-object material key, which is what gives the deferred shade pass something to shade.
            const char* DocumentName = "SuzanneRadial.wsdoc";
            switch (Extension.SceneChoice)
            {
                case SuzanneSceneChoice::PyramidStress: DocumentName = "SuzannePyramid.wsdoc";       break;
                case SuzanneSceneChoice::MaterialRings: DocumentName = "SuzanneMaterialRings.wsdoc"; break;
                case SuzanneSceneChoice::RadialArray:   break;
            }
            const std::string DocumentPath = std::string(FRONTIER_SCENE_ASSET_DIR) + "/" + DocumentName;

            RenderVertexStream Stream;
            std::vector<SuzanneSceneInstance> Instances;
            std::vector<uint32_t> TriangleSourceFace;
            WorkspaceDocument LoadedDocument;
            if (LoadWorkspaceScene(DocumentPath.c_str(), Stream, Instances, &LoadedDocument, &TriangleSourceFace) &&
                ConstructPolygonBufferAllocation(Extension.Substrate.Host, Extension.UploadPool, Stream, Extension.SceneGeometry))
            {
                UploadVisibilityScene(Extension.VisibilityRaster, Instances);

                // Clipmap occupancy (P5c): voxelize every placed head's TRIANGLES into world cells, once, here at load. The exact triangle-cell
                // overlap predicate marks only the cells a surface actually crosses, so a concave object claims no empty interior cells — the
                // failure a bounding box cannot avoid, and one a GI / shadow consumer would read as solid geometry that is not there.
                Extension.OccupiedWorldCells.clear();
                VoxelizeSceneOccupancy(Extension, Stream, Instances, "heads");

                // GPU cull (P3): fit the shared mesh's local bounding sphere / cone once, upload one per-instance cull record (the sphere transformed
                // by each instance's Model), then point the raster's survivor binding at this cull's SurvivorBuffer. UploadInstanceCullRecords stages
                // the records device-local through the same one-shot pool. InstanceCullRecordCount is the early-pass lane bound the preamble pushes.
                // A no-op (leaves the count 0 / survivor binding at its placeholder) when the cull did not build — the preamble then draws plainly.
                if (Extension.InstanceCull.ReadyCondition)
                {
                    float LocalSphere[4];
                    float LocalCone[4];
                    FitMeshLocalBounds(Stream, LocalSphere, LocalCone);
                    UploadInstanceCullRecords(Extension.InstanceCull, Extension.UploadPool, Instances,
                                              LocalSphere, LocalCone, Extension.SceneGeometry.IndexCount);
                    Extension.InstanceCullRecordCount = Extension.InstanceCull.RecordCount;
                    BindVisibilitySurvivorBuffer(Extension.VisibilityRaster, Extension.InstanceCull.SurvivorBuffer,
                                                 (VkDeviceSize)Extension.InstanceCull.RecordCapacity * sizeof(uint32_t));
                }

                // Software micro-raster (P4): point its raster set at the SAME borrowed scene the hardware path draws — the head instances (from the
                // hardware raster's instance SSBO), the cull survivor list (so Numpad-3 respects the same cull), and the shared mesh vertex/index SSBOs
                // (BufferAllocation added STORAGE usage for exactly this). Then point both software sets at VisibilityImage's packed R64 target. A no-op
                // when the software path did not build (no int64 atomics); the packed bind additionally no-ops unless the image owns a matching route.
                if (Extension.SoftwareRaster.ReadyCondition)
                {
                    BindSoftwareRasterScene(Extension.SoftwareRaster, Extension.VisibilityRaster.InstanceBuffer,
                                            Extension.InstanceCull.SurvivorBuffer, Extension.SceneGeometry.VertexBuffer,
                                            Extension.SceneGeometry.IndexBuffer);
                    BindSoftwarePackedTarget(Extension.SoftwareRaster, Extension.VisibilityTarget);
                }

                // Deferred shade: point its set at the id image plus the three buffers the reconstruction reads — the shared mesh vertex/index SSBOs (to
                // fetch the unpacked triangle's three corners) and the raster's instance SSBO (to reach the model transform, normal basis, and
                // MaterialId behind a partition ordinal). This is the earliest point all three exist, which is why the Refresh lives here rather than
                // beside Initialize. Re-run on resize alongside the resolve's, since the reconfigure rebuilds the image view.
                if (Extension.SurfaceShade.ReadyCondition)
                {
                    RefreshSurfaceShadeInscription(Extension.SurfaceShade, Extension.VisibilityTarget,
                                                   Extension.SceneGeometry.VertexBuffer, Extension.SceneGeometry.VertexByteCapacity,
                                                   Extension.SceneGeometry.IndexBuffer,  Extension.SceneGeometry.IndexByteCapacity,
                                                   Extension.VisibilityRaster.InstanceBuffer,
                                                   (VkDeviceSize)Instances.size() * sizeof(SuzanneSceneInstance));
                }

                // The component overlay reads the SAME three buffers as the shade pass above (it reconstructs the same triangle, then measures screen
                // distances to its corners), plus the depth target. Refreshed at the same point and for the same reason: this is the earliest moment all
                // of them exist. Re-run on resize alongside the others, since the reconfigure rebuilds both image views.
#ifdef FRONTIER_POLYGON_AUTHORING
                RefreshComponentOverlayInscription(Extension.ComponentOverlay, Extension.VisibilityTarget, Extension.DepthTarget,
                                                   Extension.SceneGeometry.VertexBuffer, Extension.SceneGeometry.VertexByteCapacity,
                                                   Extension.SceneGeometry.IndexBuffer,  Extension.SceneGeometry.IndexByteCapacity,
                                                   Extension.VisibilityRaster.InstanceBuffer,
                                                   (VkDeviceSize)Instances.size() * sizeof(SuzanneSceneInstance));
#endif

                // Hand the resolve the per-triangle → authored-source-face table so the topology wireframe (Numpad-0) can collapse
                // internal triangulation diagonals. One entry per emitted triangle, parallel to the primitive ordinals the raster
                // packs into the id buffer. A no-op when the table is empty (topology mode then reads as the per-triangle wireframe).
                UploadInscriptionSourceFaces(Extension.VisibilityResolve, TriangleSourceFace);

                // Register the loaded document into the scene directory so every placed head becomes an outliner row carrying its title +
                // placement. The registry is a plain data store here (no per-frame advance yet); classification 0 == unclassified until a
                // PolygonComplex classification is registered.
                InitializeScene(Extension.SceneRegistry, (uint32_t)LoadedDocument.Objects.size());
                const WorkspaceRegistration Registration =
                    RegisterWorkspaceDocument(Extension.SceneRegistry, LoadedDocument, NullRecordToken, 0u);

                ISSUE_NOTICE("render-extension", "visibility scene loaded: %u instances, %u triangles/head, %u outliner rows (from '%s')",
                             (unsigned)Instances.size(), (unsigned)(Extension.SceneGeometry.IndexCount / 3),
                             (unsigned)Registration.SpawnedCount, DocumentName);
            }
            else
            {
                ISSUE_CAUTION("render-extension", "visibility scene document unavailable ('%s') — raster idle", DocumentPath.c_str());
            }

            // Checkered floor: load its standalone document (one slab block + one grey object), upload the slab into its own device-local geometry, and
            // upload the single floor instance into the floor raster. Drawn as a second mesh into the shared visibility buffer in the preamble. All
            // best-effort — a missing CheckerFloor.wsdoc leaves FloorGeometry empty and the floor draw a no-op (the heads-only scene is unaffected).
            const std::string FloorPath = std::string(FRONTIER_SCENE_ASSET_DIR) + "/CheckerFloor.wsdoc";
            RenderVertexStream FloorStream;
            std::vector<SuzanneSceneInstance> FloorInstances;
            if (LoadFloorDocument(FloorPath.c_str(), FloorStream, FloorInstances) &&
                ConstructPolygonBufferAllocation(Extension.Substrate.Host, Extension.UploadPool, FloorStream, Extension.FloorGeometry))
            {
                UploadVisibilityScene(Extension.FloorRaster, FloorInstances);
                ISSUE_NOTICE("render-extension", "checkered floor loaded: %u instances, %u triangles",
                             (unsigned)FloorInstances.size(), (unsigned)(Extension.FloorGeometry.IndexCount / 3));

                // The floor is scene geometry too, so it belongs in the clipmap occupancy alongside the heads — previously it was loaded into
                // its own local stream and never voxelized, which is why the slab showed no occupied cells at all. Its triangles are large
                // enough to each propose a wide candidate box, so this is the sweep the cell budget above exists for.
                VoxelizeSceneOccupancy(Extension, FloorStream, FloorInstances, "floor");
            }
            else
            {
                ISSUE_CAUTION("render-extension", "checkered floor document unavailable ('%s') — floor not drawn", FloorPath.c_str());
            }
        }
    }


#ifdef FRONTIER_DEVELOPMENT_PROFILE
    // -- Clipmap visualization (development only): the instanced wire-cube lattice + probe markers over the field, built against the SWAPCHAIN
    //    colour format (it composites into the presented image inside the same colour scope the grid draws in). Default OFF; Numpad-4 flips it.
    //    Skipped gracefully — a failed build leaves ReadyCondition false and the toggle inert. Absent entirely from a shipping build.
#ifndef FRONTIER_CLIPMAP_SHADER_DIR
#define FRONTIER_CLIPMAP_SHADER_DIR "Shaders"
#endif
    InitializeClipmapFieldInspection(Extension.ClipmapInspection, Extension.Substrate.Host,
                                     Extension.Substrate.SurfaceFormat, FRONTIER_CLIPMAP_SHADER_DIR);
#endif

    InitializeRenderSchedule(Extension.Schedule);
    AppendRenderScheduleStep(Extension.Schedule, "sky",
        [&Extension](VkCommandBuffer CommandBuffer, VkExtent2D Extent)
        {
            SkyDomeConstants SkyConstants;
            AssembleSkyConstants(Extension.ViewCamera, SkyConstants);
            RecordSkyAtmospherePass(Extension.SkyPass, Extension.Substrate.Host, CommandBuffer, Extent, SkyConstants);
        });
    AppendRenderScheduleStep(Extension.Schedule, "grid",
        [&Extension](VkCommandBuffer CommandBuffer, VkExtent2D Extent)
        {
            GroundGridConstants Constants;
            AssembleGridConstants(Extension.ViewCamera, Constants);
            RecordGroundGridPass(Extension.GridPass, CommandBuffer, Extent, Constants);
        });

    return true;
}

void SynthesizeOutputSequence(RenderExtension& Extension)
{
    // 📝 One recorder per frame drives the whole sequence: derive the frame delta from the clock, advance input, refresh the
    //    projection aspect, drive the orbit camera off the resolved intents, assemble the grid constants from the updated
    //    camera, and record the ground-grid pass. The substrate owns clear/present around this.
    Extension.PreviousTimestamp = QueryMonotonicSeconds();

    // 📝 Offscreen preamble: runs before the colour scope opens (substrate seam). Keep the depth target sized to the live extent
    //    (a resized swapchain reconfigures it — the device is idle at that point in the loop), then record the depth clear into its
    //    OWN depth-only scope. Later phases append the visibility raster + HiZ reduce here. The preamble is independent of the
    //    schedule's default-OFF gate: depth is always produced so the pyramid always has a source, but it is offscreen and never
    //    touches the swapchain, so it cannot change the presented colour (the Phase-0 pixel-identity gate still holds).
    Extension.Substrate.RecordPreamble =
        [&Extension](VkCommandBuffer CommandBuffer, VkExtent2D Extent)
        {
            // 🔴 Resize fail-safe. All three offscreen render targets (depth, id image, HiZ pyramid) are extent-sized, so a window resize must
            //    rebuild every one of them. Reconfiguring destroys the old images/views/descriptors IMMEDIATELY (no deferred free), yet this preamble
            //    runs INSIDE the current frame's command recording and a PRIOR frame may still be reading the old resources on the GPU — and worse, this
            //    frame's own cull dispatch (below) samples the pyramid, so the pyramid MUST be rebuilt here, before anything records against it, never
            //    afterwards. Do all the size-dependent rebuilds together, once, under a single device-idle that fires ONLY on a genuine extent change
            //    (the Reconfigure* calls no-op when the extent already matches). The idle stalls just the rare resize frame; steady state never waits.
            //    Without this the pyramid was reconfigured AFTER the cull recorded a sampler read of it, destroying images the submitted command buffer
            //    still referenced — a use-after-free that crashed on resize.
            const bool ExtentChanged = !Extension.DepthPyramid.ReadyCondition
                                    || Extension.DepthPyramid.Width  != Extent.width
                                    || Extension.DepthPyramid.Height != Extent.height;
            if (ExtentChanged)
                vkDeviceWaitIdle(Extension.Substrate.Host.Device);

            ReconfigureVisibilityDepth(Extension.DepthTarget, Extent.width, Extent.height);
            ReconfigureVisibilityImage(Extension.VisibilityTarget, Extent.width, Extent.height);
            ReconfigureHierarchicalDepthPyramid(Extension.DepthPyramid, Extent.width, Extent.height);

            // A resize rebuilt the visibility image's view, so the resolve's descriptor now points at a stale handle — re-point it. The device is
            // already idle above on a genuine extent change, so this rewrite of the (possibly in-flight) resolve set is safe.
            if (Extension.VisibilityResolveExtentWidth  != Extension.VisibilityTarget.Width ||
                Extension.VisibilityResolveExtentHeight != Extension.VisibilityTarget.Height)
            {
                RefreshVisibilityInscription(Extension.VisibilityResolve, Extension.VisibilityTarget);
                Extension.VisibilityResolveExtentWidth  = Extension.VisibilityTarget.Width;
                Extension.VisibilityResolveExtentHeight = Extension.VisibilityTarget.Height;

                // The same reconfigure rebuilt the packed R64 target's view/buffer, so the software raster's b4 (write) + resolve b0 (read) bindings now
                // point at stale handles — re-point them under the same extent-change device-idle. A no-op when the software path did not build.
                BindSoftwarePackedTarget(Extension.SoftwareRaster, Extension.VisibilityTarget);

                // The shade samples that same rebuilt view, so its set is equally stale. Only b0 actually changed — the mesh / instance buffers are
                // extent-independent — so the already-bound handles are handed straight back from the unit's own Bound* fields, and its idempotence
                // guard reduces this to the one write that matters.
                RefreshSurfaceShadeInscription(Extension.SurfaceShade, Extension.VisibilityTarget,
                                               Extension.SurfaceShade.BoundVertexBuffer,   Extension.SceneGeometry.VertexByteCapacity,
                                               Extension.SurfaceShade.BoundIndexBuffer,    Extension.SceneGeometry.IndexByteCapacity,
                                               Extension.SurfaceShade.BoundInstanceBuffer,
                                               (VkDeviceSize)Extension.VisibilityRaster.InstanceCount * sizeof(SuzanneSceneInstance));

#ifdef FRONTIER_POLYGON_AUTHORING
                // The outline samples the same rebuilt view, so its descriptor is equally stale — re-point it under the same device-idle. Refresh
                // self-guards on the view handle, so this is a no-op when nothing actually changed.
                RefreshSelectionOutlineInscription(Extension.SelectionOutline, Extension.VisibilityTarget, Extension.DepthTarget);

                // The component overlay samples both rebuilt views too. Its geometry buffers are extent-independent, so they are handed straight back
                // from its own Bound* fields and the idempotence guard reduces this to the two image writes that actually changed.
                RefreshComponentOverlayInscription(Extension.ComponentOverlay, Extension.VisibilityTarget, Extension.DepthTarget,
                                                   Extension.ComponentOverlay.BoundVertexBuffer,   Extension.SceneGeometry.VertexByteCapacity,
                                                   Extension.ComponentOverlay.BoundIndexBuffer,    Extension.SceneGeometry.IndexByteCapacity,
                                                   Extension.ComponentOverlay.BoundInstanceBuffer,
                                                   (VkDeviceSize)Extension.VisibilityRaster.InstanceCount * sizeof(SuzanneSceneInstance));

                // ⚠️ A stale selection outlives a resize (the ordinals are scene identities, not screen state), but the HOVER does not: the pick ring
                //    holds texels copied from the OLD extent, so the cursor's partition must be re-derived before it is trusted again.
                Extension.HoveredPartition       = NoSelectionSentinel;
                Extension.ReportedHoverPartition = NoSelectionSentinel;   // the log latch mirrors the hover it reports
#endif
            }

            // Hardware visibility raster: draw the Suzanne scene into the R32_UINT id buffer with depth testing against the D32 target. This
            // both fills the visibility buffer AND writes real varying scene depth — which is what makes the HiZ reduce below meaningful (the P1
            // gate now sees geometry, not a flat cleared plane). When the raster is idle (no geometry / not ready) it records nothing and the
            // depth target is left UNDEFINED; the depth clear then supplies a far-plane depth so the reduce still has a defined source.
            const bool RasterRan = Extension.VisibilityRaster.ReadyCondition
                                && Extension.VisibilityRaster.InstanceCount > 0
                                && Extension.SceneGeometry.IndexCount > 0;

            // GPU-driven cull path (P3, early-only first cut): active when the VisibilityScaling toggle is on, the cull is built with records, AND the
            // HiZ pyramid has been reduced at least once (CurrentLayout SHADER_READ_ONLY) so the occlusion test has a legal sampled source. On the very
            // first frame the pyramid is still UNDEFINED, so the cull stands down and the plain instanced draw fills the buffer + seeds the pyramid; from
            // the next frame the cull samples LAST frame's pyramid (the design's stale-depth early pass) and the raster draws only the survivors indirectly.
            const bool CullActive = RasterRan
                                 && Extension.VisibilityScalingEnabled
                                 && Extension.InstanceCull.ReadyCondition
                                 && Extension.InstanceCullRecordCount > 0
                                 && Extension.DepthPyramid.ReadyCondition
                                 && Extension.DepthPyramid.CurrentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            // The floor is a distinct mesh drawn into the SAME visibility buffer as the heads: one cleared buffer, N meshes, each depth-testing against
            // what the others wrote (the modern one-clear / N-mesh path via BeginVisibilityScope → DrawVisibilityMesh… → EndVisibilityScope). The floor
            // is always a plain single-instance draw (no cull — it is one object); the heads draw indirect (cull) or plain per the toggle. The floor push
            // constants share the same camera; CullActive stays 0 for the floor so its gl_InstanceIndex is direct.
            const bool FloorReady = Extension.FloorRaster.ReadyCondition
                                 && Extension.FloorRaster.InstanceCount > 0
                                 && Extension.FloorGeometry.IndexCount > 0;

            // Software micro-raster path (P4, Numpad-3): when toggled on AND the compute path built (int64 atomics present) AND heads are present, the
            // heads are rasterized by the compute edge-function walk instead of the hardware raster. RecordSoftwareRasterization owns the whole fill —
            // it clears the packed R64 target, atomicMaxes (depth|id) per triangle, then resolves into the SAME R32 id buffer + D32 depth the hardware
            // path writes, leaving both in their attachment layouts. It respects the cull the same way: CullActive remaps the dispatch through the
            // survivor list when the scaling toggle is on and the pyramid is warm. After the heads resolve, the floor is composited on top through a
            // preserving (LOAD) scope so the software path renders the WHOLE view — heads + ground — making Numpad-3 a like-for-like A/B of the scene.
            const bool SoftwareActive = RasterRan
                                     && Extension.SoftwareRasterEnabled
                                     && Extension.SoftwareRaster.ReadyCondition;

            if (SoftwareActive)
            {
                // The dispatch spans the survivors when the cull is warm (CullActive 1 → the compute raster remaps its instance slot through the survivor
                // list), else every head instance directly. TriangleCount is the shared mesh's per-instance triangle count (index count / 3).
                SoftwareRasterConstants SoftwareConstants;
                VisibilityRasterConstants CameraConstants;
                AssembleVisibilityConstants(Extension.ViewCamera, CameraConstants);
                for (int Slot = 0; Slot < 16; ++Slot)
                    SoftwareConstants.ViewProjection[Slot] = CameraConstants.ViewProjection[Slot];
                SoftwareConstants.ViewportExtentX = (float)Extension.VisibilityTarget.Width;
                SoftwareConstants.ViewportExtentY = (float)Extension.VisibilityTarget.Height;
                SoftwareConstants.InstanceCount   = Extension.VisibilityRaster.InstanceCount;
                SoftwareConstants.TriangleCount   = Extension.SceneGeometry.IndexCount / 3u;
                SoftwareConstants.CullActive      = CullActive ? 1u : 0u;

                if (CullActive)
                {
                    // Same early cull as the hardware path so the survivor list the software dispatch remaps through is this frame's: reset, dispatch
                    // against last frame's pyramid, leaving the survivor + argument buffers barriered. The software raster reads the survivor SSBO (b1).
                    InstanceCullConstants CullConstants;
                    AssembleCullConstants(Extension.ViewCamera, Extension.DepthPyramid, Extension.InstanceCullRecordCount, 0u, CullConstants);
                    ResetInstanceCullFrame(Extension.InstanceCull, CommandBuffer);
                    RecordInstanceCullPass(Extension.InstanceCull, Extension.DepthPyramid, CullConstants, CommandBuffer);
                }

                RecordSoftwareRasterization(Extension.SoftwareRaster, Extension.VisibilityTarget, Extension.DepthTarget,
                                            SoftwareConstants, CommandBuffer);

                // The software path resolved the HEADS into the shared id + depth and left both in their attachment layouts. The floor is a hardware-only
                // single-instance draw, so it is composited on top here through a PRESERVING scope: LOAD (not clear) the id + depth the compute resolve
                // just wrote, then draw the floor so it depth-tests against the software-written head depth. Heads still occlude the floor and the floor
                // still occludes nothing above it — the same one-buffer / N-mesh result the hardware path produces, so Numpad-3 stays a like-for-like A/B
                // of the whole view rather than heads-on-empty.
                if (FloorReady)
                {
                    VisibilityRasterConstants FloorConstants;
                    AssembleVisibilityConstants(Extension.ViewCamera, FloorConstants);
                    FloorConstants.CullActive = 0u;
                    BeginVisibilityScope(Extension.FloorRaster, Extension.VisibilityTarget, Extension.DepthTarget, CommandBuffer, true);
                    DrawVisibilityMesh(Extension.FloorRaster, Extension.FloorRaster.InstanceSet, Extension.FloorGeometry,
                                       Extension.FloorRaster.InstanceCount, FloorConstants, false, VK_NULL_HANDLE, CommandBuffer);
                    EndVisibilityScope(Extension.FloorRaster, Extension.VisibilityTarget, Extension.DepthTarget, CommandBuffer);
                }
            }
            else if (CullActive)
            {
                // Early cull runs OUTSIDE the rendering scope: reset the counters + argument, then dispatch one lane per record against last frame's
                // pyramid. The pass leaves the survivor list + indirect argument barriered for the raster (indirect + vertex read).
                InstanceCullConstants CullConstants;
                AssembleCullConstants(Extension.ViewCamera, Extension.DepthPyramid, Extension.InstanceCullRecordCount, 0u, CullConstants);
                ResetInstanceCullFrame(Extension.InstanceCull, CommandBuffer);
                RecordInstanceCullPass(Extension.InstanceCull, Extension.DepthPyramid, CullConstants, CommandBuffer);

                VisibilityRasterConstants RasterConstants;
                AssembleVisibilityConstants(Extension.ViewCamera, RasterConstants);

                // One shared scope: clear once, indirect-draw the head survivors (CullActive 1 remaps gl_InstanceIndex through the survivor list), then
                // plain-draw the floor. Both write depth so the heads occlude / rest on the floor.
                BeginVisibilityScope(Extension.VisibilityRaster, Extension.VisibilityTarget, Extension.DepthTarget, CommandBuffer);
                RasterConstants.CullActive = 1u;
                DrawVisibilityMesh(Extension.VisibilityRaster, Extension.VisibilityRaster.InstanceSet, Extension.SceneGeometry,
                                   Extension.VisibilityRaster.InstanceCount, RasterConstants, true, Extension.InstanceCull.ArgumentBuffer, CommandBuffer);
                if (FloorReady)
                {
                    VisibilityRasterConstants FloorConstants = RasterConstants;
                    FloorConstants.CullActive = 0u;
                    DrawVisibilityMesh(Extension.FloorRaster, Extension.FloorRaster.InstanceSet, Extension.FloorGeometry,
                                       Extension.FloorRaster.InstanceCount, FloorConstants, false, VK_NULL_HANDLE, CommandBuffer);
                }
                EndVisibilityScope(Extension.VisibilityRaster, Extension.VisibilityTarget, Extension.DepthTarget, CommandBuffer);
            }
            else if (RasterRan)
            {
                VisibilityRasterConstants RasterConstants;
                AssembleVisibilityConstants(Extension.ViewCamera, RasterConstants);

                // One shared scope: clear once, plain-draw every head instance, then plain-draw the floor.
                BeginVisibilityScope(Extension.VisibilityRaster, Extension.VisibilityTarget, Extension.DepthTarget, CommandBuffer);
                DrawVisibilityMesh(Extension.VisibilityRaster, Extension.VisibilityRaster.InstanceSet, Extension.SceneGeometry,
                                   Extension.VisibilityRaster.InstanceCount, RasterConstants, false, VK_NULL_HANDLE, CommandBuffer);
                if (FloorReady)
                    DrawVisibilityMesh(Extension.FloorRaster, Extension.FloorRaster.InstanceSet, Extension.FloorGeometry,
                                       Extension.FloorRaster.InstanceCount, RasterConstants, false, VK_NULL_HANDLE, CommandBuffer);
                EndVisibilityScope(Extension.VisibilityRaster, Extension.VisibilityTarget, Extension.DepthTarget, CommandBuffer);
            }
            else if (FloorReady)
            {
                // No heads this frame, but the floor is a real mesh: draw it alone into the cleared buffer so the ground still shows + seeds the pyramid.
                VisibilityRasterConstants FloorConstants;
                AssembleVisibilityConstants(Extension.ViewCamera, FloorConstants);
                BeginVisibilityScope(Extension.FloorRaster, Extension.VisibilityTarget, Extension.DepthTarget, CommandBuffer);
                DrawVisibilityMesh(Extension.FloorRaster, Extension.FloorRaster.InstanceSet, Extension.FloorGeometry,
                                   Extension.FloorRaster.InstanceCount, FloorConstants, false, VK_NULL_HANDLE, CommandBuffer);
                EndVisibilityScope(Extension.FloorRaster, Extension.VisibilityTarget, Extension.DepthTarget, CommandBuffer);
            }
            else
            {
                RecordVisibilityDepthClear(Extension.DepthTarget, CommandBuffer);
            }

            // The id buffer was written (and left in COLOR_ATTACHMENT) whenever any mesh drew this frame — heads OR the floor. The depth-clear-only
            // branch leaves it untouched. This gates the sample-transition + the resolve read below so neither samples an undefined image.
            const bool VisibilityWritten = SoftwareActive || CullActive || RasterRan || FloorReady;

            // Hand depth to the HiZ compute reduce as a sampled source: it leaves the depth in SHADER_READ_ONLY, which the reduce requires.
            TransitionVisibilityDepthForSampling(Extension.DepthTarget, CommandBuffer);
            // Reduce the depth into the HiZ pyramid (max = conservative-farthest, standard-Z). The pyramid was already refit to the live
            // extent at the top of the preamble (before the cull sampled it), so here it is just one compute dispatch per mip to fill the chain.
            ReduceHierarchicalDepthPyramid(Extension.DepthPyramid, Extension.DepthTarget, CommandBuffer);

            // ─── P3 LATE PASS: same-frame disocclusion recovery. ──────────────────────────────────────────────────────────────────────────────
            // The early cull tested every head against LAST frame's pyramid and deferred its occlusion-rejects to the re-test list. Now that THIS
            // frame's pyramid is built (the reduce just above), re-test only that list against it: an instance the stale depth wrongly hid but the
            // fresh depth reveals is recovered into the SHARED survivor list, growing the SHARED indirect argument (InstanceCount E -> E+L). Then a
            // PRESERVING (LOAD) re-raster appends those recovered heads onto the early id + depth buffer, depth-testing against what the early pass
            // wrote so they slot in at the right depth. Without this, a head that becomes visible pops in one frame late.
            //   NOTE (Option A, see ImportantNotes 2026-07-28): the re-raster draws indirect over the WHOLE grown argument, so the E early survivors
            //   rasterize a second time. It is id- + depth-exact (same identity, same depth, LEQUAL re-write), so this is wasted fragments, not a
            //   wrong result; the tail-only fix (a second late-argument buffer) is deferred.
            const bool LatePassActive = CullActive && !SoftwareActive;
            if (LatePassActive)
            {
                InstanceCullConstants LateConstants;
                AssembleCullConstants(Extension.ViewCamera, Extension.DepthPyramid, Extension.InstanceCullRecordCount, 1u, LateConstants);
                RecordInstanceCullPass(Extension.InstanceCull, Extension.DepthPyramid, LateConstants, CommandBuffer);

                VisibilityRasterConstants LateRasterConstants;
                AssembleVisibilityConstants(Extension.ViewCamera, LateRasterConstants);
                LateRasterConstants.CullActive = 1u;

                // PreserveContents = true: LOAD the early id + depth, append the recovered survivors. Depth was just handed to the reduce as a
                // sampled source, so the preserve-scope transitions it SHADER_READ_ONLY -> DEPTH_STENCIL_ATTACHMENT (contents preserved) and the id
                // buffer COLOR_ATTACHMENT -> COLOR_ATTACHMENT (a WAW fence against the early draw). The floor already rests in the buffer from the
                // early scope, so it is not redrawn here.
                BeginVisibilityScope(Extension.VisibilityRaster, Extension.VisibilityTarget, Extension.DepthTarget, CommandBuffer, true);
                DrawVisibilityMesh(Extension.VisibilityRaster, Extension.VisibilityRaster.InstanceSet, Extension.SceneGeometry,
                                   Extension.VisibilityRaster.InstanceCount, LateRasterConstants, true, Extension.InstanceCull.ArgumentBuffer, CommandBuffer);
                EndVisibilityScope(Extension.VisibilityRaster, Extension.VisibilityTarget, Extension.DepthTarget, CommandBuffer);

                // Rebuild the pyramid from the completed depth (early + late) so NEXT frame's early cull tests against a pyramid that already holds
                // the late survivors — otherwise they would be re-deferred every frame. One extra reduce; the depth re-enters SHADER_READ_ONLY.
                TransitionVisibilityDepthForSampling(Extension.DepthTarget, CommandBuffer);
                ReduceHierarchicalDepthPyramid(Extension.DepthPyramid, Extension.DepthTarget, CommandBuffer);
            }

            // Object-selection pick copy (P3a). Ordered BEFORE the sampling transition on purpose: the copy needs TRANSFER_SRC_OPTIMAL and the
            // composites need SHADER_READ_ONLY_OPTIMAL, so doing the copy first lets the single transition below carry the image to its final
            // read-only layout — copying afterwards would need a second round trip out of and back into SHADER_READ_ONLY every frame.
            // Gated on VisibilityWritten for the same reason the resolve is: an unwritten buffer holds undefined bytes, and picking them would
            // manufacture selections out of stale memory.
#ifdef FRONTIER_POLYGON_AUTHORING
            if (VisibilityWritten && Extension.ObjectSelectionEnabled && Extension.ObjectPick.ReadyCondition)
            {
                TransitionVisibilityImageForPickCopy(Extension.VisibilityTarget, CommandBuffer);
                RecordObjectPickCopy(Extension.ObjectPick, Extension.VisibilityTarget,
                                     Extension.PickCursorX, Extension.PickCursorY, CommandBuffer);
            }
#endif

            // Hand the visibility id buffer to the resolve as a sampled source (COLOR_ATTACHMENT → SHADER_READ_ONLY), so the inscription in the
            // colour scope can texelFetch it. Only when the raster actually wrote it this frame — the idle path never transitions it to
            // COLOR_ATTACHMENT, so sampling it would read undefined contents; the resolve's own readiness guard skips it then.
            if (VisibilityWritten)
                TransitionVisibilityImageForSampling(Extension.VisibilityTarget, CommandBuffer);
        };

    Extension.Substrate.RecordSequence =
        [&Extension](VkCommandBuffer CommandBuffer, VkExtent2D Extent)
        {
            const double Now   = QueryMonotonicSeconds();
            float        Delta = (float)(Now - Extension.PreviousTimestamp);
            Extension.PreviousTimestamp = Now;
            if (Delta < 0.0f)      Delta = 0.0f;
            if (Delta > 0.1f)      Delta = 0.1f;   // clamp a stalled frame so the fly walk never lurches

            // 📝 Input comes straight off the window's packet, already refilled by PollPlatformEvents this frame (the poll
            //    runs at the top of the substrate loop, before this recorder). The cursor stays visible; the pointer delta is
            //    the OS-summed sub-frame motion, routed through the active look-jitter strategy inside DriveViewportCamera.
            ConformCameraAspect(Extension.ViewCamera,
                                Extension.Substrate.Extent.width,
                                Extension.Substrate.Extent.height);

            // F1 cycles the look-jitter strategy live (edge-latched so one press advances once). Prints the active mode so a
            // side-by-side comparison is unambiguous: drive the same slow drag under each and see which reads smooth.
            const bool ModeKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::F1);
            if (ModeKeyDown && !Extension.LookModeKeyLatch)
            {
                const int Next = ((int)Extension.LookMode + 1) % (int)LookFilterMode::ModeCount;
                Extension.LookMode = (LookFilterMode)Next;
                const char* ModeName = Extension.LookMode == LookFilterMode::Raw              ? "RAW (no filter)"
                                     : Extension.LookMode == LookFilterMode::AnchorAccumulate ? "ANCHOR-ACCUMULATE (Blender)"
                                                                                              : "VELOCITY-LOWPASS (UE5)";
                printf("[camera] look filter -> %s\n", ModeName);
                fflush(stdout);
            }
            Extension.LookModeKeyLatch = ModeKeyDown;

            // F2 toggles the visibility resolve live (edge-latched). OFF = the plain forward view (sky + grid); ON = the id buffer composited over
            // it as coloured Suzanne silhouettes — the on-screen A/B that the hardware raster wrote correct ids.
            const bool ResolveKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::F2);
            if (ResolveKeyDown && !Extension.VisibilityResolveKeyLatch)
            {
                Extension.VisibilityResolveEnabled = !Extension.VisibilityResolveEnabled;
                printf("[visibility] resolve -> %s\n", Extension.VisibilityResolveEnabled ? "ON (id buffer)" : "OFF (forward view)");
                fflush(stdout);
            }
            Extension.VisibilityResolveKeyLatch = ResolveKeyDown;

            //------------------------------------------------------------------------------------------------------------------
            //  Object selection (P3a): resolve last frame's pick, then act on this frame's input.
            //------------------------------------------------------------------------------------------------------------------
#ifdef FRONTIER_POLYGON_AUTHORING
            // 📝 ORDER IS LOAD-BEARING. Resolve FIRST — it reads the oldest ring slot, which is the one the preamble is about to overwrite. Reading
            //    after the preamble recorded would read a slot whose copy is still in flight and whose bytes are undefined.
            if (Extension.ObjectSelectionEnabled && Extension.ObjectPick.ReadyCondition)
            {
                Extension.HoveredPartition = ResolveObjectPickIdentity(Extension.ObjectPick);

                // The cursor for THIS frame's copy, in framebuffer space — exactly the space the id buffer is addressed in, so no rescale.
                Extension.PickCursorX = (int32_t)Extension.Substrate.Window.Input.PointerPositionX;
                Extension.PickCursorY = (int32_t)Extension.Substrate.Window.Input.PointerPositionY;

                // 💡 Left-click COMMITS the hovered partition. Edge-latched, so holding the button selects once rather than re-selecting every frame.
                //    The committed value is whatever the readback resolved — clicking empty space resolves to the sentinel and therefore CLEARS the
                //    selection, which is the DCC convention (click-off deselects) and needs no separate code path.
                const bool SelectPointerDown = PacketButtonHeld(Extension.Substrate.Window.Input, PointerButton::Left);
                if (SelectPointerDown && !Extension.SelectPointerLatch)
                {
                    // ⚠️ Alt+LMB is the orbit gesture in this renderer's DCC-style input map, so a click carrying Alt is a camera drag, not a pick.
                    //    Without this guard every orbit would silently reassign the selection on button-down.
                    const bool AltHeld = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::LeftAlt)
                                      || PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::RightAlt);
                    if (!AltHeld)
                    {
                        Extension.SelectedPartition = Extension.HoveredPartition;

                        // 📝 The primitive rides along in the same copied word, so a click commits it too. In Face mode that pair IS the selection (the
                        //    primitive is the face). In Vertex / Edge mode the sub-key cannot be committed from here — resolving WHICH corner is nearest
                        //    needs the projected triangle, which only the shader has — so the component stays the sentinel, which the shader reads as
                        //    "the whole primitive". The visible effect is that clicking in vertex/edge mode selects the face the handle sits on; the
                        //    per-vertex commit needs the key routed back out of the shader and is the follow-up (see Backlog).
                        Extension.SelectedPrimitive = (Extension.SelectedPartition == NoSelectionSentinel)
                                                    ? NoSelectionSentinel
                                                    : Extension.ObjectPick.ResolvedPrimitive;
                        Extension.SelectedComponent = NoSelectionSentinel;

                        if (Extension.SelectedPartition == NoSelectionSentinel)
                            ISSUE_NOTICE("selection", "cleared (clicked empty space)");
                        else
                            ISSUE_NOTICE("selection", "object %u primitive %u selected at (%d, %d)",
                                         Extension.SelectedPartition, Extension.SelectedPrimitive,
                                         Extension.PickCursorX, Extension.PickCursorY);
                    }
                }
                Extension.SelectPointerLatch = SelectPointerDown;

                // Escape clears the selection outright (edge-latched), for when the cursor has nowhere empty to click.
                const bool ClearKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::Escape);
                if (ClearKeyDown && !Extension.SelectClearKeyLatch && Extension.SelectedPartition != NoSelectionSentinel)
                {
                    Extension.SelectedPartition = NoSelectionSentinel;
                    // ⚠️ Clear the component pair too. Leaving a live primitive behind a cleared partition would let the overlay's
                    //    "SelectedComponent == sentinel means the whole primitive" rule match a partition that is itself the sentinel, painting a
                    //    phantom blue face on whatever the id buffer happens to hold there.
                    Extension.SelectedPrimitive = NoSelectionSentinel;
                    Extension.SelectedComponent = NoSelectionSentinel;
                    ISSUE_NOTICE("selection", "cleared (escape)");
                }
                Extension.SelectClearKeyLatch = ClearKeyDown;

                // 🔍 Hover trace, latched on CHANGE. An unconditional trace here fires every frame — ~60 identical lines a second with the cursor
                //    parked, which buries the selection commits that actually matter. Logging only when the ordinal differs from the last reported
                //    one turns it into one line per object crossing, which is the event worth seeing.
                if (Extension.HoveredPartition != Extension.ReportedHoverPartition)
                {
                    ISSUE_TRACE("selection", "hover %u selected %u cursor (%d, %d)",
                                Extension.HoveredPartition, Extension.SelectedPartition, Extension.PickCursorX, Extension.PickCursorY);
                    Extension.ReportedHoverPartition = Extension.HoveredPartition;
                }
            }

            // F3 toggles object selection live (edge-latched). OFF also parks the hover + the pick cursor so no stale ring is composited and the
            // preamble stops recording the copy — the whole path goes quiet rather than merely invisible.
            const bool SelectionKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::F3);
            if (SelectionKeyDown && !Extension.ObjectSelectionKeyLatch)
            {
                Extension.ObjectSelectionEnabled = !Extension.ObjectSelectionEnabled;
                if (!Extension.ObjectSelectionEnabled)
                {
                    Extension.HoveredPartition = NoSelectionSentinel;
                    Extension.PickCursorX      = -1;
                    Extension.PickCursorY      = -1;
                    // 📝 Park the log latch alongside the hover it mirrors: leaving it set would make the FIRST hover after re-enabling compare equal
                    //    to a stale ordinal and go unlogged.
                    Extension.ReportedHoverPartition = NoSelectionSentinel;
                }
                ISSUE_NOTICE("selection", "object selection -> %s", Extension.ObjectSelectionEnabled ? "ON" : "OFF");
            }
            Extension.ObjectSelectionKeyLatch = SelectionKeyDown;

            // Numpad-7 cycles the component mode Object -> Vertex -> Edge -> Face -> Object (edge-latched, one press per step). Object is the resting
            // state: the overlay records nothing and the object ring owns the frame, so the authoring handles never compete with it.
            const bool ComponentModeKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::Numpad7);
            if (ComponentModeKeyDown && !Extension.ComponentModeKeyLatch)
            {
                const uint32_t NextMode = ((uint32_t)Extension.ComponentMode + 1u) % (uint32_t)ComponentSelectionMode::ModeCount;
                Extension.ComponentMode = (ComponentSelectionMode)NextMode;

                // ⚠️ Drop the committed component when the mode changes. The keys are mode-specific — a mesh vertex index and an edge key are both bare
                //    uints drawn from overlapping ranges — so carrying one into another mode would match an unrelated component and light the wrong
                //    handle. The partition/primitive pair survives, since those mean the same thing in every mode.
                Extension.SelectedComponent = NoSelectionSentinel;

                static const char* const ComponentModeNames[] = { "Object", "Vertex", "Edge", "Face" };
                ISSUE_NOTICE("selection", "component mode -> %s", ComponentModeNames[NextMode]);
            }
            Extension.ComponentModeKeyLatch = ComponentModeKeyDown;
#endif // FRONTIER_POLYGON_AUTHORING

            // Numpad-0 toggles the resolve wireframe between per-triangle (every triangulation edge) and per-source-face topology
            // (only the authored ngon/quad/tri boundaries — internal fan diagonals collapse away). Edge-latched like F2; takes
            // effect the moment the resolve is on.
            const bool WireframeKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::Numpad0);
            if (WireframeKeyDown && !Extension.WireframeModeKeyLatch)
            {
                Extension.TopologyWireframeEnabled = !Extension.TopologyWireframeEnabled;
                printf("[visibility] wireframe -> %s\n", Extension.TopologyWireframeEnabled ? "TOPOLOGY (authored ngons/quads/tris)" : "TRIANGLES (triangulation)");
                fflush(stdout);
            }
            Extension.WireframeModeKeyLatch = WireframeKeyDown;

            // Numpad-2 toggles the GPU-driven visibility-scaling path (the two-pass cull -> indirect raster). Default ON: the raster draws only the
            // survivors the cull kept. OFF: the plain instanced draw of every instance. Edge-latched; either path writes the same id buffer, so the
            // resolve / wireframe are unaffected — this only changes HOW the buffer is filled (all instances vs cull survivors).
            const bool ScalingKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::Numpad2);
            if (ScalingKeyDown && !Extension.VisibilityScalingKeyLatch)
            {
                Extension.VisibilityScalingEnabled = !Extension.VisibilityScalingEnabled;
                printf("[visibility] scaling (GPU cull) -> %s\n", Extension.VisibilityScalingEnabled ? "ON (cull -> indirect draw)" : "OFF (plain instanced draw)");
                fflush(stdout);
            }
            Extension.VisibilityScalingKeyLatch = ScalingKeyDown;

            // Numpad-3 toggles the software micro-raster (P4). ON: the heads are filled by the compute edge-function walk (atomicMax packed depth|id →
            // resolve) instead of the hardware raster; OFF: the hardware raster. Both write the identical id + depth, so this is a pixel/id-identical A/B.
            // Gated on int64 atomics: when the compute path did not build (no ReadyCondition) the toggle refuses to arm and prints why, so the request
            // never silently no-ops. Edge-latched like the others.
            const bool SoftwareKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::Numpad3);
            if (SoftwareKeyDown && !Extension.SoftwareRasterKeyLatch)
            {
                if (!Extension.SoftwareRaster.ReadyCondition)
                {
                    Extension.SoftwareRasterEnabled = false;
                    printf("[visibility] software raster unavailable — this GPU exposes no int64 shader atomics (hardware raster kept)\n");
                }
                else
                {
                    Extension.SoftwareRasterEnabled = !Extension.SoftwareRasterEnabled;
                    printf("[visibility] software raster -> %s\n", Extension.SoftwareRasterEnabled ? "ON (compute micro-raster)" : "OFF (hardware raster)");
                }
                fflush(stdout);
            }
            Extension.SoftwareRasterKeyLatch = SoftwareKeyDown;

            // F4 toggles the deferred surface shade. ON: the id buffer is resolved into genuinely lit materials over sky + grid. OFF: the forward
            // view stands alone, which is the A/B that shows what the shade is actually contributing. Gated on the pass having built.
            const bool ShadeKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::F4);
            if (ShadeKeyDown && !Extension.SurfaceShadeKeyLatch)
            {
                if (!Extension.SurfaceShade.ReadyCondition)
                {
                    Extension.SurfaceShadeEnabled = false;
                    printf("[shade] surface shade unavailable — pipeline did not build (shaders staged?)\n");
                }
                else
                {
                    Extension.SurfaceShadeEnabled = !Extension.SurfaceShadeEnabled;
                    printf("[shade] surface shade -> %s\n", Extension.SurfaceShadeEnabled ? "ON (material BRDF)" : "OFF (forward view)");
                }
                fflush(stdout);
            }
            Extension.SurfaceShadeKeyLatch = ShadeKeyDown;

            // Numpad-5 walks the cursor across the Composite record's lobes; Numpad-6 flips the lobe under it. This is what makes the feature mask
            // load-bearing rather than decorative — the Composite head re-shades live with no table re-upload, because the mask rides a push constant.
            // The cursor starts on the FULL mask (every lobe live), so the first Numpad-6 press subtracts rather than adding to an empty surface.
            static const uint32_t CompositeLobes[] =
            {
                SurfaceFeatureDiffuse, SurfaceFeatureSpecular, SurfaceFeatureCoat, SurfaceFeatureSheen,
                SurfaceFeatureIridescence, SurfaceFeatureEmissive, SurfaceFeatureSubsurface, SurfaceFeatureTransmission,
            };
            constexpr uint32_t CompositeLobeCount = (uint32_t)(sizeof(CompositeLobes) / sizeof(CompositeLobes[0]));

            const bool LobeCursorKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::Numpad5);
            if (LobeCursorKeyDown && !Extension.CompositeLobeKeyLatch)
            {
                Extension.CompositeLobeCursor = (Extension.CompositeLobeCursor + 1u) % CompositeLobeCount;
                printf("[shade] composite lobe cursor -> %s\n", SurfaceFeatureName(CompositeLobes[Extension.CompositeLobeCursor]));
                fflush(stdout);
            }
            Extension.CompositeLobeKeyLatch = LobeCursorKeyDown;

            const bool LobeToggleKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::Numpad6);
            if (LobeToggleKeyDown && !Extension.CompositeToggleKeyLatch)
            {
                // First press materializes the live mask from "every lobe on", so toggling always starts from the record's fully-featured state.
                if (Extension.CompositeFeatureMask == 0u)
                    for (uint32_t LobeIterator = 0; LobeIterator < CompositeLobeCount; ++LobeIterator)
                        Extension.CompositeFeatureMask |= CompositeLobes[LobeIterator];

                const uint32_t SelectedLobe = CompositeLobes[Extension.CompositeLobeCursor];
                Extension.CompositeFeatureMask ^= SelectedLobe;
                printf("[shade] composite %s -> %s\n", SurfaceFeatureName(SelectedLobe),
                       (Extension.CompositeFeatureMask & SelectedLobe) ? "ON" : "OFF");
                fflush(stdout);
            }
            Extension.CompositeToggleKeyLatch = LobeToggleKeyDown;

#ifdef FRONTIER_DEVELOPMENT_PROFILE
            // Numpad-4 toggles the clipmap visualization (P5c, development builds only). ON: the clipmap cell lattice + probe markers composite over
            // the view as an X-ray overlay (the colour scope carries no depth), so residency and the scroll-in relight read directly on screen.
            // Gated on the visualization having built; edge-latched like the others. The FIELD advances either way — only its display is toggled.
            const bool ClipmapKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::Numpad4);
            if (ClipmapKeyDown && !Extension.ClipmapInspectionKeyLatch)
            {
                if (!Extension.ClipmapInspection.ReadyCondition)
                {
                    Extension.ClipmapInspectionEnabled = false;
                    printf("[clipmap] visualization unavailable — pipelines did not build (shaders staged?)\n");
                }
                else
                {
                    Extension.ClipmapInspectionEnabled = !Extension.ClipmapInspectionEnabled;
                    printf("[clipmap] visualization -> %s\n", Extension.ClipmapInspectionEnabled ? "ON (cell lattice + probes)" : "OFF");
                }
                fflush(stdout);
            }
            Extension.ClipmapInspectionKeyLatch = ClipmapKeyDown;
#endif

            DriveViewportCamera(Extension, Delta);

            // 📝 Clipmap spine (P5c): advance every level to the camera the moment the camera has moved for this frame — scroll (invalidating only the
            //    newly-exposed L-slabs), refill the shell around the camera, climb the relight ramp. Unconditional and cheap (host-side integer work on
            //    a 32³ residency table); it runs whether or not anything visualizes it, so P6/P7b inherit a spine that has been exercised every frame.
            const Vector3f ObserverPosition = EvaluateObserverPosition(Extension.ViewCamera);
            IntegrateClipmapField(Extension.ClipmapField, ObserverPosition);

            // 📝 Pass recording, two equivalent paths. The schedule path (EnabledCondition, default-OFF at Phase 0) walks the
            //    ordered spine assembled in Initialize; the legacy inline path records the same sky→grid order directly. Both
            //    must produce identical pixels — that equivalence IS the Phase 0 gate. Sky first fills the whole framebuffer as
            //    the background; the grid alpha-blends over it.
            if (Extension.Schedule.EnabledCondition)
            {
                RecordRenderSchedule(Extension.Schedule, CommandBuffer, Extent);
            }
            else
            {
                SkyDomeConstants SkyConstants;
                AssembleSkyConstants(Extension.ViewCamera, SkyConstants);
                RecordSkyAtmospherePass(Extension.SkyPass, Extension.Substrate.Host, CommandBuffer, Extent, SkyConstants);

                GroundGridConstants Constants;
                AssembleGridConstants(Extension.ViewCamera, Constants);
                RecordGroundGridPass(Extension.GridPass, CommandBuffer, Extent, Constants);
            }

            // Visibility resolve (Phase 2b A/B): composite the id buffer over the forward view when toggled on. Records inside this same colour
            // scope (over sky + grid), reading the visibility image the preamble transitioned to SHADER_READ_ONLY. Gated on the raster having run
            // this frame (InstanceCount > 0 + geometry present) so it never samples an undefined image. Default OFF — the presented pixels are then
            // exactly the forward view, holding the phase gate until the user flips F2.
            const bool HeadsPresent = Extension.VisibilityRaster.InstanceCount > 0 && Extension.SceneGeometry.IndexCount > 0;
            const bool FloorPresent = Extension.FloorRaster.InstanceCount > 0 && Extension.FloorGeometry.IndexCount > 0;
            if (Extension.VisibilityResolveEnabled
                && Extension.VisibilityResolve.ReadyCondition
                && (HeadsPresent || FloorPresent))
            {
                VisibilityInscriptionConstants ResolveConstants;
                ResolveConstants.WireframeMode = Extension.TopologyWireframeEnabled ? 1u : 0u;
                RecordVisibilityInscription(Extension.VisibilityResolve, Extent, ResolveConstants, CommandBuffer);
            }

            // Deferred surface shade (the material slice): composite the SHADED materials over the forward view. Records after the id-hash resolve so
            // that when both are on the real shading wins the pixel, and before the selection outline so the ring still reads on top. Gated on the
            // HEADS specifically — the pass binds only the head mesh's vertex/index buffers, and it discards floor partitions for exactly that reason.
            if (Extension.SurfaceShadeEnabled
                && Extension.SurfaceShade.ReadyCondition
                && HeadsPresent)
            {
                SurfaceShadeConstants ShadeConstants;
                AssembleSurfaceShadeConstants(Extension.ViewCamera, Extension.CompositeFeatureMask, ShadeConstants);
                RecordSurfaceShadeInscription(Extension.SurfaceShade, Extent, ShadeConstants, CommandBuffer);
            }

            // Selection outline (P3a): the ring over the selected object's silhouette, composited AFTER the visibility resolve so it reads on top of
            // either presentation (plain forward view or the id-buffer A/B). Gated on the raster having run this frame for the same reason the resolve
            // is — an unwritten id buffer holds undefined contents. The record call self-skips when nothing is selected or hovered, so an idle frame
            // costs no fullscreen pass.
#ifdef FRONTIER_POLYGON_AUTHORING
            if (Extension.ObjectSelectionEnabled
                && Extension.SelectionOutline.ReadyCondition
                && (HeadsPresent || FloorPresent))
            {
                SelectionOutlineConstants OutlineConstants;
                OutlineConstants.SelectedPartition = Extension.SelectedPartition;
                OutlineConstants.HoveredPartition  = Extension.HoveredPartition;
                // ⚠️ The shader unprojects sampled window depth into linear metres as near/(1 − z_win), so this must track the SAME near plane the
                //    frame's projection matrix was built from. Hard-coding it would leave the occlusion compare silently mis-scaled the moment the
                //    lens changes — and a mis-scaled compare degrades to the original bug (contours drawn as solid rings), not to a visible error.
                OutlineConstants.DepthNearPlane = Extension.ViewCamera.NearPlane;
                RecordSelectionOutlineInscription(Extension.SelectionOutline, Extent, OutlineConstants, CommandBuffer);
            }

            // Component overlay (vertex / edge / face handles): LAST of the selection composites, so handles read on top of the object ring rather than
            // being buried by it. Gated on the HEADS specifically — like the shade pass, it binds only the head mesh's buffers and discards floor
            // partitions, so a floor-only frame has nothing it can reconstruct. Record self-skips in Object mode, so the default costs no pass.
            if (Extension.ObjectSelectionEnabled
                && Extension.ComponentOverlay.ReadyCondition
                && Extension.ComponentMode != ComponentSelectionMode::Object
                && HeadsPresent)
            {
                ComponentOverlayConstants OverlayConstants;
                AssembleComponentOverlayConstants(Extension.ViewCamera, Extension.ComponentMode,
                                                  Extension.SelectedPartition, Extension.SelectedPrimitive,
                                                  Extension.SelectedComponent,
                                                  Extension.PickCursorX, Extension.PickCursorY,
                                                  OverlayConstants);
                RecordComponentOverlayInscription(Extension.ComponentOverlay, Extent, OverlayConstants, CommandBuffer);
            }
#endif

#ifdef FRONTIER_DEVELOPMENT_PROFILE
            // Clipmap visualization (P5c, development only): LAST inside the colour scope so the lattice reads over everything, including the resolve.
            // Refresh walks the field and refills the host-visible coherent record buffer in place. ⚠️ That single buffer is not per-slot, and the
            // substrate keeps 2 frames in flight, so the previous frame's draw may still be reading it — a cell can therefore be drawn with the
            // adjacent frame's category or ramp. Deliberately accepted: every record stays in bounds (fixed capacity, zeroed at init), so the only
            // consequence is a one-frame colour flicker on a debug overlay. A real consumer needs a per-slot region or a fence-guarded upload.
            // Occupied cells come from the load-time triangle voxelization, so the highlight marks the cells the scene's surfaces actually cross
            // rather than a box around each object — see TriangleCellOverlap.h for why the bounding-box shortcut is not acceptable here.
            if (Extension.ClipmapInspectionEnabled && Extension.ClipmapInspection.ReadyCondition)
            {
                // Occupancy is REPLAYED, not recomputed: the cells were voxelized once at load from the scene's actual triangles, so this frame
                // only hands the prebaked set over. Per-triangle voxelization is far too slow to repeat per frame, and nothing here moves.
                RefreshClipmapInspection(Extension.ClipmapInspection, Extension.ClipmapField, ObserverPosition,
                                         Extension.OccupiedWorldCells, Extension.ClipmapOccupancyLevel, ClipmapDisplayShellRadius);

                ClipmapInspectionConstants ClipmapConstants;
                AssembleClipmapInspectionConstants(Extension.ViewCamera, ClipmapConstants);
                RecordClipmapInspection(Extension.ClipmapInspection, CommandBuffer, Extent, ClipmapConstants);
            }
#endif
        };

    RunWindowSubstrate(Extension.Substrate);
}

void FinalizeRenderExtension(RenderExtension& Extension)
{
    // The GPU must be idle before destroying pass resources, and the grid pipeline must go before the device does. Wait once
    // here, drop the recorder (it captures the extension by reference), then tear the grid + input down ahead of the substrate.
    if (Extension.Substrate.Host.Device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(Extension.Substrate.Host.Device);
    Extension.Substrate.RecordSequence = nullptr;
    Extension.Substrate.RecordPreamble = nullptr;
    // Drop the schedule's step callbacks (they capture Extension by reference) before the passes they record into are torn down.
    FinalizeRenderSchedule(Extension.Schedule);
#ifdef FRONTIER_DEVELOPMENT_PROFILE
    FinalizeClipmapFieldInspection(Extension.ClipmapInspection);
#endif
    FinalizeHierarchicalDepthPyramid(Extension.DepthPyramid);
    // Object selection torn down before the resolve, mirroring the reverse-of-creation order it was built in (readback + outline came after it).
#ifdef FRONTIER_POLYGON_AUTHORING
    FinalizeComponentOverlayInscription(Extension.ComponentOverlay);
    FinalizeSelectionOutlineInscription(Extension.SelectionOutline);
    FinalizeObjectPickReadback(Extension.ObjectPick);
#endif

    FinalizeSurfaceShadeInscription(Extension.SurfaceShade);
    FinalizeVisibilityInscription(Extension.VisibilityResolve);
    FinalizeSoftwareRasterization(Extension.SoftwareRaster);
    FinalizeInstanceCullSubmission(Extension.InstanceCull);
    FinalizeVisibilityRasterization(Extension.VisibilityRaster);
    FinalizeVisibilityRasterization(Extension.FloorRaster);
    FinalizeScene(Extension.SceneRegistry);
    ReleasePolygonBufferAllocation(Extension.Substrate.Host, Extension.SceneGeometry);
    ReleasePolygonBufferAllocation(Extension.Substrate.Host, Extension.FloorGeometry);
    if (Extension.UploadPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(Extension.Substrate.Host.Device, Extension.UploadPool, Extension.Substrate.Host.Allocator);
        Extension.UploadPool = VK_NULL_HANDLE;
    }
    FinalizeVisibilityImage(Extension.VisibilityTarget);
    FinalizeVisibilityDepth(Extension.DepthTarget);
    FinalizeSkyAtmospherePass(Extension.SkyPass, Extension.Substrate.Host);
    FinalizeGroundGridPass(Extension.GridPass, Extension.Substrate.Host);
    FinalizeWindowSubstrate(Extension.Substrate);
}

} // namespace Frontier
