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

    // 🔴 Occlusion test (the grid-over-objects fix). The near plane MUST come from the same camera the matrices above were built
    //    from: the shader linearizes the sampled depth with it AND scales its own ray parameter by it, so a stale or hardcoded value
    //    mis-scales both sides of the compare and degrades silently back to "grid draws through everything".
    Constants.DepthNearPlane = Subject.NearPlane;

    // ⚠️ Perspective only. The shader derives the ground hit distance from the ray parameter times the near plane, an identity that
    //    holds only because the perspective vertex path builds the ray as eye → near-plane point. Under a PARALLEL projection the
    //    vertex stage spans near → far instead, so that scale is simply wrong — and the compare would hide the grid at essentially
    //    every pixel. Disabling it there is also the honest behaviour for a lens whose purpose is seeing through the model.
    Constants.DepthTestEnabled = (Subject.Projection == ProjectionMode::Orthographic) ? 0.0f : 1.0f;
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
// LightDirection is the world-space sun (normalized, pointing TOWARD the light), passed in by the caller.
// CompositeMask overrides the Composite record's own mask; 0 means "use the record's".
//
// 🔴 THE SUN IS A PARAMETER RATHER THAN A CONSTANT BECAUSE SHADING AND SHADOWING MUST SHARE ONE DIRECTION (P6.5). This used to hardcode a tuned key
//    light (0.40, -0.55, 0.73) chosen to flatter the head rings, which was harmless while nothing cast shadows. It stops being harmless the moment the
//    tracer runs: the atlas is rasterized from the clipmap's basis, which comes from the SUN, so a different shading direction would light one side of
//    a surface while the shadow fell on another. That reads as broken shadows, not as a mismatched key. One source, two consumers.
void AssembleSurfaceShadeConstants(const ViewportCamera& Subject, uint32_t CompositeMask, Vector3f SolarDirection,
                                   SurfaceShadeConstants& Constants)
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

    // ⚠️ Re-normalized rather than trusted: the BRDF assumes a unit L, and a caller passing a profile vector that drifted off unit length would show up
    //    as a subtly wrong intensity rather than as a bad direction. Falls back to straight up when handed a degenerate vector, so a zeroed profile
    //    lights the scene from above instead of producing NaN across every lit pixel.
    const float LightLength = std::sqrt(SolarDirection.XCoord * SolarDirection.XCoord
                                      + SolarDirection.YCoord * SolarDirection.YCoord
                                      + SolarDirection.ZCoord * SolarDirection.ZCoord);
    if (LightLength > 1e-6f)
    {
        Constants.LightDirection[0] = SolarDirection.XCoord / LightLength;
        Constants.LightDirection[1] = SolarDirection.YCoord / LightLength;
        Constants.LightDirection[2] = SolarDirection.ZCoord / LightLength;
    }
    else
    {
        Constants.LightDirection[0] = 0.0f;
        Constants.LightDirection[1] = 0.0f;
        Constants.LightDirection[2] = 1.0f;
    }
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
void AssembleComponentOverlayConstants(const ViewportCamera&              Subject,
                                       ComponentSelectionMode             Mode,
                                       uint32_t                           SelectedPartition,
                                       uint32_t                           SelectedPrimitive,
                                       uint32_t                           SelectedComponent,
                                       int32_t                            CursorX,
                                       int32_t                            CursorY,
                                       const ComponentOverlayInscription& Overlay,
                                       ComponentOverlayConstants&         Constants)
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

    // 📝 The authored-provenance gate. Zero means the tables never uploaded, and the shader answers by drawing no handles at all rather than falling back
    //    to triangle keys — the fallback would silently re-introduce the triangulated-selection bug this data exists to fix.
    // ⚠️ Must come from the inscription, NOT from a buffer length: when the tables are absent the descriptor set aliases bindings 5-7 onto the index buffer
    //    (an unbound descriptor is undefined memory), so those buffers report a healthy non-zero length while holding index data.
    Constants.AuthoredTriangleCount = Overlay.AuthoredTriangleCount;

    // 📝 The raw cursor, not a resolved hover — the shader re-runs its own reconstruction there to decide what is hovered. Negative (pointer outside the
    //    window) is passed through unchanged and read as "nothing hovered".
    Constants.CursorX = CursorX;
    Constants.CursorY = CursorY;
}
#endif // FRONTIER_POLYGON_AUTHORING

// 📝 How much of the clipmap the debug viz shows and how much of it the stub fill keeps resident, in cells either side of the camera cell. The
//    fill radius is deliberately SMALLER than the display radius so the outer ring of every level reads vacant on screen — that ring is what makes
//    the scroll visible: it slides against the camera and the cells it uncovers flip to vacant, then relight as the fill catches them.
constexpr int32_t ClipmapDisplayShellRadius   = 2;   // [cells] - half-extent of the SMALL fine-level camera grid
constexpr int32_t ClipmapResidencyFillRadius  = 3;   // [cells] - half-extent the stub fill marks resident, per level (kept ≥ the display radius so the whole camera grid reads resident + relights)

// 📝 How far from the camera an OCCUPIED cell is still cage-drawn, in occupancy-level cells. This bounds the overlay's frame cost against scene
//    size rather than letting it scale with total surface area: the lattice spends 72 vertices per cell and composites with no depth attachment, so
//    every cage overdraws whatever sits in front of it. A ground slab voxelizes to well over a thousand cells and, seen edge-on, was alone enough to
//    make camera motion visibly stutter. Raise it to inspect further out, at a directly proportional vertex cost.
// ⚠️ This is in CELLS, so its metre reach follows ClipmapOccupancyLevel — the two must be retuned together. At level 0 (1 m cells) 24 cells reaches
//    ±24 m, which covers the head cluster and a wide patch of floor; the same 24 at level 2 would reach ±96 m and pull the whole slab back in.
// ⚠️ Measured 2026-07-29: a radius-24 window drew up to 4,999 cells once the camera moved (not the ~2,400 a static-camera estimate predicts), and
//    widening to 96 or beyond CLAMPS at ClipmapInspectionCellCapacity (20,000) — DroppedCellCount starts reporting and the overlay under-draws. Frame
//    time held ~16.7 ms mean at every radius including unbounded, so on this GPU the gate is a legibility and truncation control, not a frame-rate
//    rescue. Under FIFO vsync that mean cannot reveal remaining headroom; a real per-draw cost needs timestamp queries around RecordClipmapInspection.
constexpr int32_t ClipmapOccupiedDisplayRadius = 24; // [cells] - half-extent of the occupied-cell cage display, at ClipmapOccupancyLevel
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

// Close the occupancy bake once every mesh has been swept: fold out the cells two instances both claimed, then derive the display set.
//
// 📝 The dedup is REQUIRED, not tidiness. VoxelizeTriangleStream deduplicates within one sweep only, and each instance is its own sweep, so two
//    neighbouring heads that share a boundary cell both contribute it. A duplicate would also defeat the shell reduction below — a cell listed
//    twice reads as its own neighbour's occupant — so the two steps have to happen in this order.
void FinalizeSceneOccupancy(RenderExtension& Extension)
{
    // Sort-and-unique rather than a hash set: the ordering is stable across runs, so the diagnostics below are comparable frame to frame.
    std::sort(Extension.OccupiedWorldCells.begin(), Extension.OccupiedWorldCells.end(),
              [](const CellCoordinate& Left, const CellCoordinate& Right)
              {
                  if (Left.ZCell != Right.ZCell) return Left.ZCell < Right.ZCell;
                  if (Left.YCell != Right.YCell) return Left.YCell < Right.YCell;
                  return Left.XCell < Right.XCell;
              });
    Extension.OccupiedWorldCells.erase(
        std::unique(Extension.OccupiedWorldCells.begin(), Extension.OccupiedWorldCells.end(),
                    [](const CellCoordinate& Left, const CellCoordinate& Right)
                    {
                        return Left.XCell == Right.XCell && Left.YCell == Right.YCell && Left.ZCell == Right.ZCell;
                    }),
        Extension.OccupiedWorldCells.end());

    // The overlay draws from the shell copy; every consumer reads the full set above. See ReduceCellsToOuterShell for why they must not be shared.
    Extension.DisplayedOccupancyCells = Extension.OccupiedWorldCells;
    const uint32_t InteriorCount = ReduceCellsToOuterShell(Extension.DisplayedOccupancyCells);

    ISSUE_NOTICE("render-extension", "clipmap occupancy closed: %u distinct cells at level %u; overlay candidate set is %u cells "
                                     "(%u interior cells culled from display only), further gated to %d cells around the camera at draw time",
                 (unsigned)Extension.OccupiedWorldCells.size(), (unsigned)Extension.ClipmapOccupancyLevel,
                 (unsigned)Extension.DisplayedOccupancyCells.size(), (unsigned)InteriorCount,
                 (int)ClipmapOccupiedDisplayRadius);
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

    // 📝 Reported from the HOST rather than the feature profile: the profile records what the hardware CAN do, whereas this flag records
    //    what vkCreateDevice actually turned ON. Only the latter governs whether a fragment-stage storage write is legal, and the two can
    //    disagree — so reporting the capability here would state a permission the device may not hold.
    ISSUE_NOTICE("render-extension", "fragment stores + atomics (sun shadow prerequisite): %s",
                 Extension.Substrate.Host.FragmentStoresAndAtomicsEnabled ? "enabled" : "UNAVAILABLE");

    // ⚠️ Not fatal here, and deliberately not fatal later either: the shadow chain's tile tagging and page-atlas raster both write from a
    //    fragment shader, so on a device without this bit those passes must bypass rather than build pipelines the driver will reject.
    if (!Extension.Substrate.Host.FragmentStoresAndAtomicsEnabled)
        ISSUE_CAUTION("render-extension", "fragmentStoresAndAtomics unavailable — sun shadow passes will gate themselves off");

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

    // -- Linear HDR scene target (P5.9b): allocated BEFORE the sky, because the sky's pipeline is built against this target's format rather than
    //    the swapchain's. Sized to the current extent; rebuilt on resize in the preamble. If it fails to allocate its ReadyCondition stays false and
    //    the frame falls back to writing the swapchain directly — see the radiance scope in the preamble. -----------------------------------------
    InitializeRadianceTarget(Extension.RadianceScene, Extension.Substrate.Host,
                             Extension.Substrate.Extent.width, Extension.Substrate.Extent.height);

    // 📝 The colour format the SCENE units (sky, surface shade) build their pipelines against. This is the radiance target when it allocated, and
    //    the swapchain only as a degraded fallback — a pipeline's attachment format is fixed at creation, so this single decision is what routes
    //    the scene into linear HDR or leaves it writing display-encoded pixels straight to the screen.
    const VkFormat SceneColourFormat = Extension.RadianceScene.ReadyCondition ? RadianceTargetFormat
                                                                              : Extension.Substrate.SurfaceFormat;

    // -- Sky/atmosphere pass: baked once, drawn behind the grid each frame. Skipped gracefully if it cannot build. --------
#ifndef FRONTIER_SKY_SHADER_DIR
#define FRONTIER_SKY_SHADER_DIR "Shaders"
#endif
    if (InitializeSkyAtmospherePass(Extension.SkyPass, Extension.Substrate.Host,
                                    SceneColourFormat, FRONTIER_SKY_SHADER_DIR))
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
    //    SCENE colour format — the linear HDR radiance target, not the swapchain — because the shade now writes unbounded linear radiance and the
    //    resolve tone maps it later. Its alpha-over blend is what makes the Glass preset read as transparent, and it is precisely that blend which is
    //    only physically correct on linear light, so moving it into the radiance target fixes it rather than merely relocating it.
    //    The material table is resolved on the host and uploaded ONCE here — it is immutable, because runtime lobe toggling rides the push constant
    //    instead of a re-upload. Its descriptor also needs the MESH and INSTANCE buffers, which do not exist until the scene loads below, so Refresh is
    //    deferred to that point. Best-effort: a failed build leaves ReadyCondition false and the F4 toggle inert.
    InitializeSurfaceShadeInscription(Extension.SurfaceShade, Extension.Substrate.Host,
                                      SceneColourFormat, FRONTIER_VISIBILITY_SHADER_DIR);
    if (Extension.SurfaceShade.ReadyCondition)
    {
        SurfacePresetParameters PresetTable[SurfacePresetCount];
        BuildSurfacePresetTable(PresetTable);
        UploadSurfaceShadeMaterials(Extension.SurfaceShade, PresetTable);
    }

    // -- Radiance resolve (P5.9b): the single exit from linear space. Built against the SWAPCHAIN format (it writes the presented image) while
    //    READING the radiance target through its descriptor — the two formats are genuinely different here, which is the whole point. Pointed at the
    //    target's view immediately, since (unlike the shade) it depends on no scene geometry. Skipped when the radiance target did not allocate: the
    //    scene units then wrote the swapchain directly and there is nothing to resolve. ---------------------------------------------------------
#ifndef FRONTIER_RADIANCE_SHADER_DIR
#define FRONTIER_RADIANCE_SHADER_DIR "Shaders"
#endif
    if (Extension.RadianceScene.ReadyCondition)
    {
        InitializeRadianceResolveInscription(Extension.RadianceResolve, Extension.Substrate.Host,
                                             Extension.Substrate.SurfaceFormat, FRONTIER_RADIANCE_SHADER_DIR);
        RefreshRadianceResolveInscription(Extension.RadianceResolve, Extension.RadianceScene);

        // 🔴 The scene pipelines were built against RadianceTargetFormat, so if the resolve failed to build there is no path from the radiance target
        //    to the screen and the viewport would present a black frame with no other symptom. Say so loudly rather than letting it read as "the
        //    scene did not load".
        if (!Extension.RadianceResolve.ReadyCondition)
            ISSUE_FAULT("render-extension", "radiance resolve unavailable — the scene renders to the HDR target but cannot reach the screen "
                                            "(stage RadianceResolve.frag.spv)");
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

    // -- Sun shadows (P6.3b): the light-space tile window + the physical page pool behind it. -----------------------------------------------
    // 📝 The window is pure CPU math and cannot fail, so it is configured unconditionally. The atlas allocates a real 4096² R32_UINT image and
    //    CAN fail (out of device memory, no device-local type) — on failure ReadyCondition stays false and every shadow step downstream no-ops,
    //    which degrades to the pre-P6 image rather than taking the renderer down.
    InitializeSunShadowClipmap(Extension.SunWindow, ShadowTilemapLodCount, ShadowTilemapResolution, ShadowBaseTileMetres);
    if (InitializeShadowPageAtlas(Extension.ShadowAtlas, Extension.Substrate.Host, ShadowTilemapLodCount, ShadowTilemapResolution))
    {
        ISSUE_NOTICE("render-extension",
                     "sun shadows: %u levels x %u^2 tiles (level 0 tile %.2f m); atlas %ux%u R32_UINT = %u MiB, %u pages vs %u tiles (%u:1)",
                     (unsigned)ShadowTilemapLodCount, (unsigned)ShadowTilemapResolution, (double)ShadowBaseTileMetres,
                     (unsigned)ShadowPageAtlasEdge, (unsigned)ShadowPageAtlasEdge,
                     (unsigned)((ShadowPageAtlasEdge * ShadowPageAtlasEdge * 4u) / (1024u * 1024u)),
                     (unsigned)ShadowPageCapacity,
                     (unsigned)(ShadowTilemapResolution * ShadowTilemapResolution * ShadowTilemapLodCount),
                     (unsigned)((ShadowTilemapResolution * ShadowTilemapResolution * ShadowTilemapLodCount) / ShadowPageCapacity));
    }
    else
    {
        ISSUE_NOTICE("render-extension", "sun shadows: page atlas did NOT build — shadow steps bypass, image is pre-P6");
    }

    // -- The marking chain (P6.3c): the virtual tile table, then the three GPU passes that raise demand bits in it. -----------------------
    // 📝 Ordered after the atlas because the marking chain is what will FEED the allocator — but it does not depend on it: marking states what the
    //    image wants, allocation decides what it gets. So the table + pipelines build even when the atlas did not, and the tally still reports.
    // 🔴 The chain writes DEMAND only. Nothing here allocates a page or rasterizes depth into one (S4/S5 are CPU mirrors, S7 is P6.4), so the marked
    //    table cannot change the presented image — the Phase-0 pixel-identity gate still holds through this whole step.
#ifndef FRONTIER_SHADOW_SHADER_DIR
#define FRONTIER_SHADOW_SHADER_DIR "Shaders"
#endif
    if (InitializeShadowTileStore(Extension.TileStore, Extension.Substrate.Host, ShadowTilemapLodCount, ShadowTilemapResolution) &&
        InitializeShadowTileMarkingSubmission(Extension.TileMarking, Extension.Substrate.Host, Extension.TileStore,
                                              FRONTIER_SHADOW_SHADER_DIR))
    {
        ISSUE_NOTICE("render-extension", "sun shadows: marking chain live — %u-word table, S1%s + S3 recorded per image",
                     (unsigned)ShadowTileStoreCapacity,
                     Extension.TileMarking.TagPipelineEnabled ? " + S2" : " (S2 OFF: no fragment SSBO writes)");
    }
    else
    {
        ISSUE_NOTICE("render-extension", "sun shadows: marking chain did NOT build — no tile demand is produced, tally stays zero");
    }

    // -- S6 (P6.4): the page clear pass. ------------------------------------------------------------------------------------------
    // 🔴 Depends on the atlas because its descriptor bakes in the atlas storage view, so it is built only when the atlas did. It still cannot change the
    //    presented image: it writes the identity value into pages nothing samples yet (S7 is the next step, the tracer is P6.5), so the Phase-0
    //    pixel-identity gate holds through this step too.
    if (Extension.ShadowAtlas.ReadyCondition &&
        InitializeShadowPageClearSubmission(Extension.ShadowPageClear, Extension.Substrate.Host, Extension.ShadowAtlas,
                                            FRONTIER_SHADOW_SHADER_DIR))
    {
        ISSUE_NOTICE("render-extension", "sun shadows: S6 page clear live — clears only the pages wanted AND stale (0x%08X identity)",
                     (unsigned)ShadowPageClearIdentity);
    }
    else if (Extension.ShadowAtlas.ReadyCondition)
    {
        ISSUE_NOTICE("render-extension", "sun shadows: S6 page clear did NOT build — pages keep the previous owner's depth, S7 would resolve garbage");
    }

    // -- S7 (P6.4): the caster depth raster. --------------------------------------------------------------------------------------
    // 🔴 Requires fragmentStoresAndAtomics, exactly as S2 does: the whole pass IS a fragment-stage imageAtomicMin, so without the feature the pipeline
    //    would be a validation error rather than merely slow. Degrading to no shadows is the only option, hence the explicit else.
    // 📝 Built after S6 because it rasterizes into the pages S6 primes, and like S6 it still cannot change the presented image — it writes depth into an
    //    atlas nothing samples until the P6.5 tracer lands, so the Phase-0 pixel-identity gate holds through this step too.
    if (Extension.ShadowAtlas.ReadyCondition && Extension.Substrate.Host.FragmentStoresAndAtomicsEnabled &&
        InitializeShadowDepthRasterSubmission(Extension.ShadowDepthRaster, Extension.Substrate.Host, Extension.ShadowAtlas,
                                              FRONTIER_SHADOW_SHADER_DIR))
    {
        ISSUE_NOTICE("render-extension", "sun shadows: S7 caster depth raster live — %.0f m depth span from %.0f m, atomic-min resolve",
                     (double)ShadowDepthRangeMetres, (double)ShadowDepthOriginMetres);
    }
    else if (Extension.ShadowAtlas.ReadyCondition && !Extension.Substrate.Host.FragmentStoresAndAtomicsEnabled)
    {
        ISSUE_NOTICE("render-extension", "sun shadows: S7 depth raster SKIPPED — host lacks fragmentStoresAndAtomics, no caster depth is produced");
    }
    else if (Extension.ShadowAtlas.ReadyCondition)
    {
        ISSUE_NOTICE("render-extension", "sun shadows: S7 depth raster did NOT build — pages stay at the identity, every receiver reads as unshadowed");
    }

    // 📝 Which level occupancy is voxelized and displayed at. This is the knob that decides whether the voxelization is LEGIBLE: at level 2 (4 m
    //    cells) a whole head plus the floor under it collapse into a single cage, so the shell reads as one uniform box and there is no way to see
    //    that the per-triangle predicate is doing anything. Level 0's 1 m cells resolve a head into a cluster of cages that follows its silhouette,
    //    which is what makes the concave/exact behaviour visible. It costs proportionally more cells, which is what the display radius bounds.
    //    The prebake below and the per-frame replay both key off this one value.
    constexpr uint32_t ClipmapOccupancyPreferredLevel = 0;
    Extension.ClipmapOccupancyLevel = std::min(ClipmapOccupancyPreferredLevel, Extension.ClipmapField.LevelCount - 1u);

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

            // 📝 The authored provenance the component modes select by: per-triangle source face, per-corner CLUSTER vertex, per-side loop-edge ordinal.
            //    Filled by the decoder alongside the render stream, because the authored cluster only exists there — it is discarded after triangulation.
            AuthoredTopologyMap AuthoredTopology;
            // ⚠️ PartitionBase is 0 here — this is the PRIMARY scene. Passing FloorPartitionBase would shift every head identity into the floor's reserved
            //    range, which the overlay rejects outright, so the handles would vanish rather than merely misdraw.
            if (LoadWorkspaceScene(DocumentPath.c_str(), Stream, Instances, &LoadedDocument, &TriangleSourceFace,
                                   0u, &AuthoredTopology) &&
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

                // S7 (P6.4): acquire the heads' caster set. 📝 Here rather than beside Initialize because the instance buffer only exists once the scene
                //    has been uploaded. The SAME buffer the hardware raster draws from, so a head cannot be placed differently for its shadow than for
                //    its shading — one transform source, two consumers.
                Extension.ShadowCasterSceneSet =
                    AcquireShadowCasterSet(Extension.ShadowDepthRaster, Extension.VisibilityRaster.InstanceBuffer,
                                           (VkDeviceSize)Extension.VisibilityRaster.InstanceCount * sizeof(SuzanneSceneInstance));

                // Deferred shade: point its set at the id image plus the three buffers the reconstruction reads — the shared mesh vertex/index SSBOs (to
                // fetch the unpacked triangle's three corners) and the raster's instance SSBO (to reach the model transform, normal basis, and
                // MaterialId behind a partition ordinal). This is the earliest point all three exist, which is why the Refresh lives here rather than
                // beside Initialize. Re-run on resize alongside the resolve's, since the reconfigure rebuilds the image view.
                // ⚠️ No floor buffers here: the checkered floor document loads BELOW this point, so its geometry does not exist yet. The floor
                //    bindings are therefore aliased onto the head buffers for now and FloorShadeEnabled stays 0; a second Refresh right after the
                //    floor upload re-points them at the real buffers. Refresh is idempotent, so that second call costs one handle compare when the
                //    floor is absent. Reordering the loads instead would tie this Refresh to a best-effort document that is allowed to be missing.
                if (Extension.SurfaceShade.ReadyCondition)
                {
                    RefreshSurfaceShadeInscription(Extension.SurfaceShade, Extension.VisibilityTarget,
                                                   Extension.SceneGeometry.VertexBuffer, Extension.SceneGeometry.VertexByteCapacity,
                                                   Extension.SceneGeometry.IndexBuffer,  Extension.SceneGeometry.IndexByteCapacity,
                                                   Extension.VisibilityRaster.InstanceBuffer,
                                                   (VkDeviceSize)Instances.size() * sizeof(SuzanneSceneInstance),
                                                   VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 0,
                                                   // 🔴 The shadow pair must be passed at EVERY Refresh, not just the last one. Refresh rewrites the
                                                   //    WHOLE set, so omitting them here would re-alias b8/b9 onto the visibility image even though the
                                                   //    atlas is live — and ShadowAtlasBound would go false, silently disabling shadows rather than
                                                   //    breaking anything visibly.
                                                   Extension.ShadowAtlas.AtlasSampledView,
                                                   Extension.ShadowAtlas.MappingBuffer,
                                                   (VkDeviceSize)Extension.ShadowAtlas.TilePageMapping.size() * sizeof(uint32_t),
                                                   Extension.ShadowAtlas.CoverageBuffer,
                                                   (VkDeviceSize)ShadowPageCapacity * sizeof(uint32_t));
                }

                // The component overlay reads the SAME three buffers as the shade pass above (it reconstructs the same triangle, then measures screen
                // distances to its corners), plus the depth target. Refreshed at the same point and for the same reason: this is the earliest moment all
                // of them exist. Re-run on resize alongside the others, since the reconfigure rebuilds both image views.
#ifdef FRONTIER_POLYGON_AUTHORING
                // ⚠️ ORDER MATTERS: the upload must precede the Refresh. The upload allocates the three authored SSBOs and clears BoundIdView to force the
                //    Refresh past its early-return, so the descriptor set is written ONCE with the real buffers. Refreshing first would bind the aliased
                //    stand-ins, then the early-return would keep them — the tables would be resident but never actually bound.
                UploadComponentOverlayTopology(Extension.ComponentOverlay, AuthoredTopology.SourceFace,
                                               AuthoredTopology.CornerVertex, AuthoredTopology.SideEdge);

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

                // S7 (P6.4): the floor's own caster set. 🔴 The slab is a CASTER as well as the receiver, and giving it its own set rather than reusing
                //    the heads' is what makes that possible — re-pointing one shared set between the two draws would mutate a descriptor the queued head
                //    draw still references. Without this the slab would receive shadows but cast none, losing its own contact shadow.
                // ⚠️ Face culling is off in S7's pipeline precisely for this mesh: the slab is single-sided, so culling from the light's view could
                //    discard its only face and it would stop casting entirely.
                Extension.ShadowCasterFloorSet =
                    AcquireShadowCasterSet(Extension.ShadowDepthRaster, Extension.FloorRaster.InstanceBuffer,
                                           (VkDeviceSize)FloorInstances.size() * sizeof(SuzanneSceneInstance));

                // The floor is scene geometry too, so it belongs in the clipmap occupancy alongside the heads — previously it was loaded into
                // its own local stream and never voxelized, which is why the slab showed no occupied cells at all. Its triangles are large
                // enough to each propose a wide candidate box, so this is the sweep the cell budget above exists for.
                VoxelizeSceneOccupancy(Extension, FloorStream, FloorInstances, "floor");

                // P6.3a: re-point the shade's floor bindings (b5-b7) at the real floor buffers, which only exist as of this branch. Until now they
                // aliased the head buffers, so this is the call that actually lets the floor shade — and therefore the call that gives the sun
                // shadows a lit surface to fall on. Reached only on success, so a missing document leaves the safe alias in place.
                // ⚠️ The head buffers must be passed through unchanged: Refresh rewrites the WHOLE set, so handing it null head handles here would
                //    trip its early-return and the floor would silently never bind.
                if (Extension.SurfaceShade.ReadyCondition)
                {
                    RefreshSurfaceShadeInscription(Extension.SurfaceShade, Extension.VisibilityTarget,
                                                   Extension.SurfaceShade.BoundVertexBuffer,   Extension.SceneGeometry.VertexByteCapacity,
                                                   Extension.SurfaceShade.BoundIndexBuffer,    Extension.SceneGeometry.IndexByteCapacity,
                                                   Extension.SurfaceShade.BoundInstanceBuffer,
                                                   (VkDeviceSize)Extension.VisibilityRaster.InstanceCount * sizeof(SuzanneSceneInstance),
                                                   Extension.FloorGeometry.VertexBuffer, Extension.FloorGeometry.VertexByteCapacity,
                                                   Extension.FloorGeometry.IndexBuffer,  Extension.FloorGeometry.IndexByteCapacity,
                                                   Extension.FloorRaster.InstanceBuffer,
                                                   (VkDeviceSize)FloorInstances.size() * sizeof(SuzanneSceneInstance),
                                                   Extension.ShadowAtlas.AtlasSampledView,   // see the shadow-pair note at the first Refresh
                                                   Extension.ShadowAtlas.MappingBuffer,
                                                   (VkDeviceSize)Extension.ShadowAtlas.TilePageMapping.size() * sizeof(uint32_t),
                                                   Extension.ShadowAtlas.CoverageBuffer,
                                                   (VkDeviceSize)ShadowPageCapacity * sizeof(uint32_t));
                }
            }
            else
            {
                ISSUE_CAUTION("render-extension", "checkered floor document unavailable ('%s') — floor not drawn", FloorPath.c_str());
            }
        }
    }

    // Every mesh that contributes occupancy has now been swept, so close the bake: dedup across instances and derive the overlay's shell copy.
    // Placed outside the load branches on purpose — it must run even when a document failed to load, so the two sets never disagree.
    FinalizeSceneOccupancy(Extension);

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
            // ================================================================================================================================
            //  C1-C4 / C6 — the CPU sun-window advance (P6.3b)
            // ================================================================================================================================
            // 📝 Runs at the very TOP of the preamble, before the early cull, because every GPU shadow step below reads the window this advances:
            //    which tiles are resident, which just went stale, and which pages back them. Host-side integer work on a 32² x 6 tile lattice.
            //
            // 🔴 The observer is CACHED here for RecordSequence to reuse rather than each recomputing it. The preamble runs first and the sequence
            //    second within ONE command buffer, but the sequence drives the camera before it advances the GI field — so recomputing there would
            //    hand the two spines observers a frame of camera motion apart, and the sun window and the GI field would disagree about where the
            //    viewer is. One evaluation, one cache, both read it.
            //
            // ⚠️ Deliberately NOT moving DriveViewportCamera ahead of the preamble (the sketch in PLAN §4.1 suggested it). The visibility raster
            //    below depends on using this frame's NOT-yet-advanced camera — the shade reconstructs geometry from the id buffer that raster
            //    wrote, so it must read the camera that wrote it. Advancing the camera first would invert that invariant and perturb the settled
            //    P5.9b image to solve a problem the shadows do not have: a half-frame of camera lag shifts WHICH TILES are resident, not where any
            //    geometry lands. Tile residency is self-correcting on the next image; the raster/shade camera agreement is not.
            Extension.CachedObserverPosition = EvaluateObserverPosition(Extension.ViewCamera);
            Extension.ObserverCacheSeeded    = true;

            // 🔴 THE SUN WINDOW CENTRES ON THE ORBIT TARGET, NOT THE EYE, AND THAT IS THE FIX FOR THE ROTATION FLICKER. EvaluateObserverPosition
            //    returns Target - Forward * Distance (CameraViewMatrixSolver.cpp:48) — the EYE, which orbits. Centring the clipmap there makes the
            //    lattice origin a function of camera ORIENTATION: at Distance = 18 m a yaw sweep swings the centre around an 18 m circle in light
            //    space, so every receiver's offset-from-centre changes, its analytic level changes, origins scroll, and pages are evicted and
            //    re-rendered — while the geometry and the sun both stood still. Measured as debug-level colours flashing on every left/right/up/down
            //    rotate, and as shadow flicker from pages caught mid-redraw. The orbit TARGET is invariant under both rotation and zoom, so the
            //    lattice moves only when the viewer actually translates, which is the only motion a clipmap should react to.
            // ⚠️ NOT folded into CachedObserverPosition, which the GI clipmap field also reads (IntegrateClipmapField below). That field's
            //    behaviour is settled against the eye and is not what this fixes; widening the change would perturb a working subsystem.
            // 📝 Zoom therefore no longer rebuilds the window either. Distance still drives nothing here — level follows the receiver's own
            //    light-space offset (MarkVisibleShadowPages.comp / SunShadowTrace.glsl), never the viewer's distance.
            Extension.CachedShadowCentre = Extension.ViewCamera.Target;

            {
                // C1-C4: rebuild the light basis, scroll every level to the observer, and commit residency. A sun that ROTATED invalidates whole
                // windows instead of exposing strips — that coarse path lives inside Refresh and is not merged with scrolling.
                // 🔴 CONVERTED OUT OF THE ATMOSPHERE'S Y-UP FRAME, not read raw. The profile stores the sun with .y as elevation; the clipmap's basis
                //    and every other scene consumer are Z-up. Reading the three floats in order — which this did until 2026-07-30 — puts the sun on
                //    the HORIZON at the default 45° profile, because (0.707, 0.707, 0) has zero Z. It produced no error and no warning: the sky drew a
                //    45° sun while the shadow basis pointed sideways, so shadows raked to infinity from a light nothing else agreed with.
                float SunX = 0.0f, SunY = 0.0f, SunZ = 1.0f;
                Atmosphere::ResolveSolarDirectionSceneFrame(Extension.SkyPass.Profile, SunX, SunY, SunZ);
                const Vector3f SolarDirection{ SunX, SunY, SunZ };

                // 🔴 THE PER-LEVEL SCROLL IS DRIVEN HERE RATHER THAN THROUGH RefreshSunShadowClipmap BECAUSE THE ATLAS MUST SEE EACH RESULT. Refresh
                //    evaluates and commits every level internally and returns nothing, so the exposed strips — the only record of which tiles just
                //    changed which ground they address — were discarded before anything could act on them. IntegrateSunShadowResidency zeroes the
                //    clipmap's CPU ResidencyTable for those tiles, but the tracer reads depth through the atlas's TilePageMapping, which kept pointing
                //    at pages holding the PREVIOUS ground's depth. That is why a static camera looked perfect and any camera motion produced offset,
                //    flickering, grid-aligned shadows: standing still exposes no strips, so nothing went stale.
                // ⚠️ ORDER IS LOAD-BEARING THREE WAYS: Evaluate reads the pre-scroll origin, Integrate advances it, and the atlas release must run
                //    AFTER Integrate because a tile's slot is resolved through the CURRENT origin. Releasing first would unmap the slots the window is
                //    about to reuse while sparing the ones actually holding stale depth.
                // 📝 The basis/previous-direction bookkeeping Refresh used to own is reproduced below the loop, unchanged in order: every level is
                //    evaluated against the PREVIOUS image's sun so the rotation test stays meaningful.
                uint32_t ScrollReleasedPages = 0;

                if (Extension.SunWindow.ReadyCondition && !Extension.SunWindow.Levels.empty())
                {
                    for (uint32_t LevelIterator = 0; LevelIterator < (uint32_t)Extension.SunWindow.Levels.size(); ++LevelIterator)
                    {
                        const SunShadowScrollResult Scroll = EvaluateSunShadowScroll(Extension.SunWindow,
                                                                                    LevelIterator,
                                                                                    Extension.CachedShadowCentre,
                                                                                    SolarDirection);
                        IntegrateSunShadowResidency(Extension.SunWindow, Scroll);
                        ScrollReleasedPages += InvalidateScrolledShadowPages(Extension.ShadowAtlas, Extension.SunWindow, Scroll);
                    }

                    Extension.SunWindow.Basis                  = SolveSunShadowBasis(SolarDirection);
                    Extension.SunWindow.SolarDirectionPrevious = NormalizeVector(SolarDirection);
                    Extension.SunWindow.SolarDirectionSeeded   = true;
                }

#ifdef FRONTIER_DEVELOPMENT_PROFILE
                // 📝 Change-triggered: a still camera releases nothing, so an unconditional print would be silent noise and then a flood on motion.
                if (ScrollReleasedPages != 0)
                    printf("[shadow] scroll released %u stale pages\n", ScrollReleasedPages);
#else
                (void)ScrollReleasedPages;
#endif

                // 🔴 The marking chain's per-level origin uniform block is refreshed HERE — immediately after the scroll above and before any of the
                //    three passes is recorded. All of S1/S2/S3 resolve a tile through this ToroidalOrigin, so handing them a window the clipmap has
                //    not yet scrolled would make every pass address the PREVIOUS image's lattice: every mark lands off by the frame's scroll, which
                //    presents as shadows lagging the camera rather than as a stale upload. Cheap (a 96-byte coherent write), so unconditional.
                RefreshShadowTileOrigins(Extension.TileMarking, Extension.SunWindow);

                if (Extension.TileStore.ReadyCondition)
                {
                    // 🔴 RESOLVE THE PREVIOUS IMAGE'S DOWNLOAD FIRST, BEFORE THE RESET AND UPLOAD BELOW. Upload and download share ONE staging
                    //    buffer, so the memcpy inside UploadShadowTileStore overwrites exactly the bytes the previous image's download landed in.
                    //    Resolving afterwards therefore reads back the freshly-RESET mirror and every count reads zero — which looks like "the GPU
                    //    marked nothing", not like a read-ordering mistake. Observed as counts alternating real / zero across the two frame slots.
                    // ⚠️ Still one image stale by design: this reads what the PREVIOUS submission's copy produced. Reading it fresh needs a stall.
                    ResolveShadowTileDownload(Extension.TileStore);
#ifdef FRONTIER_DEVELOPMENT_PROFILE
                    // 📝 P6.3c gate instrumentation. Change-triggered on the Used count, because standing still marks the same tiles every image and
                    //    a per-frame print would bury every other notice.
                    // 🔴 These counts CANNOT see an origin-sign error. `Slot − Origin` and `Origin + Slot` both enumerate all 32 slots bijectively, so
                    //    every number below is byte-identical either way. Only a word-for-word CPU-mirror cross-check pins the convention.
                    RefreshShadowTileTally(Extension.TileStore);
                    if (Extension.TileStore.Tally.TileUsedCount != Extension.ReportedTileUsedCount)
                    {
                        Extension.ReportedTileUsedCount = Extension.TileStore.Tally.TileUsedCount;
                        ISSUE_NOTICE("render-extension",
                                     "shadow tiles: used %u of %u — direct %u, coarse-only %u, update %u, to render %u, masked %u",
                                     (unsigned)Extension.TileStore.Tally.TileUsedCount,
                                     (unsigned)ShadowTileStoreCapacity,
                                     (unsigned)Extension.TileStore.Tally.TileDirectCount,
                                     (unsigned)Extension.TileStore.Tally.TileCoarseCount,
                                     (unsigned)Extension.TileStore.Tally.TileUpdateCount,
                                     (unsigned)Extension.TileStore.Tally.TileRenderCount,
                                     (unsigned)Extension.TileStore.Tally.TileMaskedCount);
                    }
#endif

                }

                // Open the page image: every page claimed last image demotes Used -> Cached so it becomes reclaimable again. Without this the pool
                // leaks into a permanently-Used state and exhausts in blocks that never recover.
                if (Extension.ShadowAtlas.ReadyCondition)
                {
                    OpenShadowPageImage(Extension.ShadowAtlas);

                    // S5 (CPU): claim a page for every tile the marking chain says still wants one, then push the resulting mapping to the device for
                    // S6/S7 to address through.
                    // 🔴 STRICTLY AFTER OpenShadowPageImage. The open demotes every page Used -> Cached; allocating first would have this image's fresh
                    //    claims immediately demoted to reclaimable, so the pool would hand the same pages out twice within one image and two tiles would
                    //    point at one page.
                    // 🔴 AND STRICTLY BEFORE ResetShadowTileDemand, which is why the reset moved BELOW this block. The allocator's only input is
                    //    ShadowTileRequestsPage — i.e. the mirror's Used bit — and the reset clears exactly that bit. Running the reset first (which it
                    //    did until 2026-07-30) left the allocator reading an all-zero demand table, so it requested ZERO pages every image forever. That
                    //    is silent all the way down: S1/S2/S3 keep marking on the GPU and the tile tally keeps reporting real demand (`used 134 of
                    //    6144`), but the page census stays pinned at `used 0 of 256`, S6 clears nothing, S7 draws nothing, and the tracer reads the clear
                    //    identity everywhere — a fully-lit scene with a complete, healthy-looking marking chain in front of it. Nothing between the two
                    //    calls writes TileWords: S1/S2/S3's atomicOr lands in the DEVICE SSBO, and the mirror only ever receives it through
                    //    ResolveShadowTileDownload above.
                    // ⚠️ The demand it reads is ONE IMAGE STALE — Store.TileWords holds what the previous submission's download produced, which is the
                    //    only demand the CPU can see without a device stall. A tile that became visible this image therefore gets its page next image;
                    //    the shadow appears one frame late rather than wrong, and the alternative (stalling to read fresh demand) costs more than the
                    //    frame it saves. 📝 This is also why the allocation cannot simply move to the GPU without porting S5 wholesale.
                    if (Extension.TileStore.ReadyCondition)
                    {
                        const uint32_t AllocationRequestCount =
                            DriveShadowPageAllocation(Extension.ShadowAtlas, Extension.TileStore, Extension.SunWindow);

#ifdef FRONTIER_DEVELOPMENT_PROFILE
                        // 📝 The allocator's return value was DISCARDED here, and that is why the zero-page defect was un-diagnosable from the log: every
                        //    other number in the chain is read either side of it, so "the allocator was never reached", "it returned early on a ready
                        //    condition", and "it requested pages but every request failed" all present identically as a silent census of zero.
                        // 🔴 Latched on the value, not change-triggered on a counter starting at zero. The page/S6/S7 notices below are change-triggered,
                        //    so a PERMANENT zero prints once at boot and then goes silent — absence of a line became the only signal, which is exactly
                        //    how this hid. This prints on every transition INCLUDING the transition into zero.
                        if (AllocationRequestCount != Extension.ReportedAllocationRequestCount)
                        {
                            Extension.ReportedAllocationRequestCount = AllocationRequestCount;
                            ISSUE_NOTICE("render-extension",
                                         "shadow pages: S5 requested %u page(s) from %u tile(s) of standing demand (atlas %s, store %s, window %s)",
                                         (unsigned)AllocationRequestCount,
                                         (unsigned)Extension.TileStore.Tally.TileUsedCount,
                                         Extension.ShadowAtlas.ReadyCondition ? "ready" : "NOT READY",
                                         Extension.TileStore.ReadyCondition   ? "ready" : "NOT READY",
                                         Extension.SunWindow.ReadyCondition   ? "ready" : "NOT READY");
                        }
#endif

                        // Per-image demand reset, then push the cleared mirror to the device so this image's S1/S2/S3 atomicOr into a table carrying
                        // only the page indices + surviving Update bits.
                        // 🔴 Demand (Used/Direct/Coarse/Masked) is THIS image's statement and must not persist; Update is a statement about page CONTENT
                        //    and survives until something redraws it. ResetShadowTileDemand encodes exactly that asymmetry — and it operates on the
                        //    mirror ResolveShadowTileDownload refreshed, so the Update bits it preserves are the GPU's, not a stale copy's.
                        // 🔴 AFTER DriveShadowPageAllocation, NEVER BEFORE IT. The allocator's sole input is the Used bit this clears; running the reset
                        //    first starves it of every request while the tile tally keeps reporting healthy demand. See the note on the allocation above.
                        // ⚠️ The two uploads are ordered allocation-mapping THEN tile-store deliberately: both are transfers into buffers the same
                        //    submission's shaders read, and the mapping must describe the pages this image's S6/S7 address through.
                        ResetShadowTileDemand(Extension.TileStore);

                        UploadShadowPageMapping(Extension.ShadowAtlas, CommandBuffer);
                        UploadShadowTileStore(Extension.TileStore, CommandBuffer);
                    }

                    // C6: read the census and warn ONCE on genuine starvation. Latched because a shortfall persists for as long as the viewer
                    // stands there, and a per-frame print would bury every other notice. Over-subscription is not itself the fault — the pool is
                    // 24:1 over-subscribed by design — a request that got NO page is.
                    RefreshShadowPageCensus(Extension.ShadowAtlas);
                    const bool Starved = ShadowPageOverSubscribed(Extension.ShadowAtlas);
                    if (Starved && !Extension.ReportedPageShortfall)
                    {
                        Extension.ReportedPageShortfall = true;
                        ISSUE_NOTICE("render-extension",
                                     "sun shadows: page pool STARVED — %u requests unserved (used %u, cached %u, free %u). Shadow blocks will be missing",
                                     (unsigned)Extension.ShadowAtlas.Census.PageStarvedCount,
                                     (unsigned)Extension.ShadowAtlas.Census.PageUsedCount,
                                     (unsigned)Extension.ShadowAtlas.Census.PageCachedCount,
                                     (unsigned)Extension.ShadowAtlas.Census.PageFreeCount);
                    }
                    else if (!Starved)
                    {
                        Extension.ReportedPageShortfall = false;   // re-arm, so a later genuine shortfall is reported again
                    }

#ifdef FRONTIER_DEVELOPMENT_PROFILE
                    // 📝 P6.4 gate instrumentation. Change-triggered on the Used count, like the tile tally above — standing still allocates the same
                    //    pages every image. `render` is the number S7 must actually rasterize; it should fall to ~0 once a static scene has been drawn
                    //    once, and that decay is the evidence the cache works rather than an assertion that it does.
                    // 🔴 `starved` IS ON THIS LINE DELIBERATELY, even though the latched notice above also reports it. That notice fires ONCE per
                    //    shortfall episode, so in any log window that does not contain the transition, a pool serving 256 of 759 requests looked
                    //    indistinguishable from a healthy one — `used`/`cached`/`free` alone are all consistent with a pool that is simply busy. The
                    //    unserved count is the only number that separates "working hard" from "dropping shadow blocks on the floor", so it belongs in
                    //    the per-image census and not only in an episode notice.
                    if (Extension.ShadowAtlas.Census.PageUsedCount != Extension.ReportedPageUsedCount)
                    {
                        Extension.ReportedPageUsedCount = Extension.ShadowAtlas.Census.PageUsedCount;
                        ISSUE_NOTICE("render-extension",
                                     "shadow pages: used %u of %u — cached %u, free %u, requests %u, starved %u, evicted %u, stale %u, to render %u",
                                     (unsigned)Extension.ShadowAtlas.Census.PageUsedCount,
                                     (unsigned)ShadowPageCapacity,
                                     (unsigned)Extension.ShadowAtlas.Census.PageCachedCount,
                                     (unsigned)Extension.ShadowAtlas.Census.PageFreeCount,
                                     (unsigned)Extension.ShadowAtlas.Census.PageRequestCount,
                                     (unsigned)Extension.ShadowAtlas.Census.PageStarvedCount,
                                     (unsigned)Extension.ShadowAtlas.Census.PageEvictedCount,
                                     (unsigned)Extension.ShadowAtlas.Census.PageStaleCount,
                                     (unsigned)Extension.ShadowAtlas.Census.PageRenderCount);
                    }
#endif

                    // S6: prime the pages S7 will rasterize into. 🔴 The atlas must be in GENERAL for a storage-image write, and the transition is
                    //    recorded here rather than inside the pass so a later S7 in the same image does not transition twice.
                    // 📝 A no-op once a static scene has been drawn: the clear list is exactly ShadowPageNeedsRender's selection, which decays to empty.
                    //    That decay IS the cache working — a whole-atlas clear would look identical on screen and silently redraw all 1024 pages forever.
                    if (Extension.ShadowPageClear.ReadyCondition)
                    {
                        TransitionShadowPageAtlas(Extension.ShadowAtlas, CommandBuffer, VK_IMAGE_LAYOUT_GENERAL);
                        const uint32_t ClearedPages = RecordShadowPageClear(Extension.ShadowPageClear, Extension.ShadowAtlas, CommandBuffer);

                        // 🔴 IMMEDIATELY AFTER S6 AND OVER THE SAME PAGE SET, which is why it is not folded into the transition above or hoisted to the
                        //    top of the image. Coverage is a statement about what is IN a page, so it must be discarded exactly when that page's depth
                        //    is — see ClearShadowPageCoverage. Both are no-ops together on a static scene.
                        const uint32_t ResetCoveragePages = ClearShadowPageCoverage(Extension.ShadowAtlas, CommandBuffer);

                        // 🔴 THE SAME PAGE SET AGAIN, THIS TIME CARRIED TO S7's FRAGMENT STAGE. S7 rasterizes whole tile windows and cannot know which
                        //    pages S6 primed, so without this mask it writes every MAPPED page while S6 cleared only the STALE ones — measured 1024
                        //    against 12. Those 1012 cached pages then take this image's imageAtomicMin on top of depth they already held, and min never
                        //    releases, so a caster's old silhouette is permanent: a second shadow standing where the object no longer is, spread over
                        //    the allocated span of the atlas. Every count in the chain stays healthy while it happens, which is why it survived six
                        //    diagnoses. The three sets — S6's clear, coverage's reset, this mask — are one predicate and must never diverge.
                        const uint32_t AuthorizedPages = UploadShadowPageRenderMask(Extension.ShadowAtlas, CommandBuffer);

#ifdef FRONTIER_DEVELOPMENT_PROFILE
                        // ⚠️ Change-triggered, and it must AGREE with the census's `to render` above every image — the two are computed from the same
                        //    predicate by different code, so a divergence means one of them is reading a stale record set.
                        if (ClearedPages != Extension.ReportedPageClearCount)
                        {
                            Extension.ReportedPageClearCount = ClearedPages;
                            ISSUE_NOTICE("render-extension", "shadow pages: S6 cleared %u page(s) this image (census said %u to render)",
                                         (unsigned)ClearedPages,
                                         (unsigned)Extension.ShadowAtlas.Census.PageRenderCount);
                        }
                        // 🔴 THE THREE SETS ARE ONE PREDICATE AND A DIVERGENCE IS THE BUG ITSELF, so it is asserted every image rather than sampled. S6's
                        //    clear, coverage's reset and S7's authorization mask are all built from ShadowPageNeedsRender by three separate loops; if
                        //    they ever disagree, either a page was primed that S7 will not draw (a fully-lit hole at the clear identity) or a page S7
                        //    will draw was never primed (this image's casters min'd into retained depth — the duplicate-shadow bug). Neither shows up in
                        //    any census count, which is precisely why it needs its own check.
                        if (ClearedPages != AuthorizedPages || ClearedPages != ResetCoveragePages)
                        {
                            ISSUE_NOTICE("render-extension",
                                         "shadow page set divergence: S6 cleared %u, coverage reset %u, S7 authorized %u — these must be equal",
                                         (unsigned)ClearedPages, (unsigned)ResetCoveragePages, (unsigned)AuthorizedPages);
                        }
#else
                        (void)ClearedPages;
                        (void)ResetCoveragePages;
                        (void)AuthorizedPages;
#endif

                        // S7: rasterize caster depth into the pages S6 just primed. 🔴 STRICTLY AFTER the clear, and in the same GENERAL layout the
                        //    transition above established — rasterizing into an un-primed page resolves imageAtomicMin against uninitialized memory,
                        //    which is garbage depth rather than "no caster". RecordShadowPageClear already left the barrier that orders the two.
                        //
                        // 🔴 BOTH caster meshes, each through its OWN descriptor set. The heads are what the shadow is of; the slab is a caster as well
                        //    as the receiver, so omitting it would lose its contact shadow. Order between them is irrelevant — imageAtomicMin is
                        //    commutative, which is exactly what lets two independent draws share a page with no ordering between them.
                        // 📝 Both are no-ops when their set was never acquired (no scene / no floor document) or the page census says nothing needs
                        //    redrawing, which on a static scene is the steady state.
                        if (Extension.ShadowDepthRaster.ReadyCondition)
                        {
                            const uint32_t SceneLevels =
                                RecordShadowDepthRaster(Extension.ShadowDepthRaster, Extension.ShadowAtlas, Extension.SunWindow,
                                                        Extension.ShadowCasterSceneSet, Extension.SceneGeometry,
                                                        Extension.VisibilityRaster.InstanceCount, CommandBuffer);

                            const uint32_t FloorLevels =
                                RecordShadowDepthRaster(Extension.ShadowDepthRaster, Extension.ShadowAtlas, Extension.SunWindow,
                                                        Extension.ShadowCasterFloorSet, Extension.FloorGeometry,
                                                        Extension.FloorRaster.InstanceCount, CommandBuffer);

#ifdef FRONTIER_DEVELOPMENT_PROFILE
                            // ⚠️ Change-triggered on the PAIR, so a floor that silently stopped casting (its set lost, its instance count zeroed) shows
                            //    up as a changed line rather than as a shadow that merely looks slightly wrong.
                            const uint32_t DrawSignature = (SceneLevels << 8) | FloorLevels;
                            if (DrawSignature != Extension.ReportedDepthRasterSignature)
                            {
                                Extension.ReportedDepthRasterSignature = DrawSignature;
                                ISSUE_NOTICE("render-extension",
                                             "shadow pages: S7 drew %u level(s) of heads + %u level(s) of floor (%u page(s) needed redrawing)",
                                             (unsigned)SceneLevels, (unsigned)FloorLevels,
                                             (unsigned)Extension.ShadowAtlas.Census.PageRenderCount);
                            }
#else
                            (void)SceneLevels;
                            (void)FloorLevels;
#endif

                            // #19: declare the drawn pages clean. 🔴 HERE, after BOTH meshes, and exactly once — this is the call that makes S6's cache
                            //    real. Without it nothing on the device path ever clears staleness, so S6 re-clears and S7 redraws the same pages every
                            //    image forever and the "static scene costs nothing" property is a property of the predicate only, never of the pipeline.
                            // 🔴 Placing it after the FIRST mesh instead would cache a half-drawn page — heads shadowing, floor not — which the cache
                            //    would then never redraw. That is why it sits outside the two record calls rather than inside a helper beside each.
                            // ⚠️ Gated on a draw having actually been recorded. If both meshes drew nothing (no scene, no sets, or the census already
                            //    said zero) then nothing rasterized, and lowering the flags would declare depth valid for pages still holding the clear
                            //    identity — every receiver over them would read as fully lit.
                            if (SceneLevels > 0 || FloorLevels > 0)
                            {
                                const uint32_t MarkedPages =
                                    MarkShadowDepthPagesRendered(Extension.ShadowAtlas, Extension.TileStore, Extension.SunWindow);

#ifdef FRONTIER_DEVELOPMENT_PROFILE
                                // ⚠️ This count MUST equal the S6 clear count traced above — both are the same ShadowPageNeedsRender selection, taken
                                //    before and after the same draws. A divergence means one of them is reading a record set the other already mutated.
                                if (MarkedPages != Extension.ReportedPageMarkedCount)
                                {
                                    Extension.ReportedPageMarkedCount = MarkedPages;
                                    ISSUE_NOTICE("render-extension",
                                                 "shadow pages: S7 marked %u page(s) rendered (S6 cleared %u) — the cache is now live",
                                                 (unsigned)MarkedPages, (unsigned)Extension.ReportedPageClearCount);
                                }
#else
                                (void)MarkedPages;
#endif
                            }
                        }
                    }
                }

                // 🔴 P6.5: hand the atlas to the tracer as a SAMPLED image. S6/S7 wrote it in GENERAL (imageAtomicMin needs a storage image); the shade
                //    texelFetches it through a combined-image-sampler, whose descriptor names SHADER_READ_ONLY_OPTIMAL. Sampling an image that is
                //    actually in GENERAL is undefined — it commonly WORKS, which is what makes the omission a latent defect rather than a visible one,
                //    and only the validation layer would say otherwise.
                // 🔴 HERE, outside every rendering scope and after the last S7 draw is recorded. The transition is a pipeline barrier, which cannot be
                //    recorded inside a dynamic-rendering scope at all — and the radiance scope that the shade records into opens further down, so this
                //    is the last point where a barrier is legal. It also carries the execution dependency that makes S7's writes visible to the shade's
                //    reads; without it the tracer could sample texels whose atomics have not landed.
                // ⚠️ Unconditional rather than gated on a draw having happened. A cleared-but-undrawn atlas still needs the layout change, because the
                //    descriptor was written naming SHADER_READ_ONLY_OPTIMAL either way; and the transition self-guards on the current layout, so an
                //    image already in the target layout costs nothing.
                if (Extension.ShadowAtlas.ReadyCondition)
                {
                    TransitionShadowPageAtlas(Extension.ShadowAtlas, CommandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

                    // ⚠️ The coverage buffer needs its OWN barrier for the same edge: the image transition above orders the atlas image and says nothing
                    //    about this allocation, so without it the tracer can read coverage words whose atomicOrs have not landed — a genuinely drawn page
                    //    reading as never-drawn, which is precisely the artefact the buffer exists to eliminate.
                    BarrierShadowPageCoverageForRead(Extension.ShadowAtlas, CommandBuffer);
                }

#ifdef FRONTIER_DEVELOPMENT_PROFILE
                // 📝 P6.3b gate instrumentation: trace the level-0 toroidal ORIGIN, which is what actually moves when the camera crosses a tile
                //    boundary. Change-triggered, so standing still prints nothing and walking prints one line per tile crossed.
                // 🔴 THE VACANT COUNT IS COUNTED, NOT ASSUMED. Until 2026-07-30 both %u arguments here were the SAME expression, so this line printed
                //    "6144 tiles vacant of 6144" unconditionally without ever inspecting the residency table — a placeholder from before S7 existed
                //    that then read as hard evidence of an empty window and cost a diagnosis session. A tautological log line is worse than no line:
                //    it cannot fail, so it looks like a measurement. If a count here is not walked out of real state, do not print it.
                {
                    const TileCoordinate Origin = Extension.SunWindow.Levels.empty() ? TileCoordinate{}
                                                                                     : Extension.SunWindow.Levels.front().ToroidalOrigin;
                    if (Origin.XTile != Extension.ReportedOriginX || Origin.YTile != Extension.ReportedOriginY)
                    {
                        Extension.ReportedOriginX = Origin.XTile;
                        Extension.ReportedOriginY = Origin.YTile;

                        uint32_t VacantTiles = 0;
                        uint32_t TotalTiles  = 0;
                        for (const SunShadowLevel& Level : Extension.SunWindow.Levels)
                        {
                            TotalTiles += (uint32_t)Level.ResidencyTable.size();
                            for (const uint8_t Resident : Level.ResidencyTable)
                                if (Resident == 0)
                                    ++VacantTiles;
                        }

                        ISSUE_NOTICE("render-extension",
                                     // 📝 Reports the SHADOW CENTRE, which is what SolveCenteredOrigin actually consumed. Printing the observer here
                                     //    would show a position the origin does not follow, making a correct window look wrong under camera rotation.
                                     "sun window: L0 origin (%d, %d), centre (%.2f, %.2f, %.2f), %u tiles vacant of %u",
                                     (int)Origin.XTile, (int)Origin.YTile,
                                     (double)Extension.CachedShadowCentre.XCoord,
                                     (double)Extension.CachedShadowCentre.YCoord,
                                     (double)Extension.CachedShadowCentre.ZCoord,
                                     (unsigned)VacantTiles,
                                     (unsigned)TotalTiles);
                    }
                }
#endif
            }

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

            // The radiance target is extent-sized like the three above, and reconfiguring it rebuilds its view — so the resolve's descriptor goes
            // stale in exactly the same way the id-buffer consumers' do. Both calls are cheap no-ops when the extent already matches, and the
            // device-idle above covers the genuine-change case. Refresh self-guards on the view handle.
            ReconfigureRadianceTarget(Extension.RadianceScene, Extent.width, Extent.height);
            RefreshRadianceResolveInscription(Extension.RadianceResolve, Extension.RadianceScene);

            // The grid samples the scene depth for its occlusion test, and a resize rebuilt that depth view. Called every frame rather than only
            // under ExtentChanged because it is also the FIRST binding: Initialize builds the grid before the depth target exists, so there is no
            // valid view to point at yet at that time. Refresh is idempotent (one handle compare when nothing moved), so the steady-state cost is
            // nil and the resize + first-frame cases are both covered by the same call.
            RefreshGroundGridPass(Extension.GridPass, Extension.Substrate.Host, Extension.DepthTarget);

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
                // ⚠️ The floor triple must be handed back too. This Refresh rewrites the WHOLE set, so omitting it would re-alias b5-b7 onto the head
                //    buffers on the first resize and FloorGeometryBound would fall to false — the floor would stop shading the moment the window was
                //    dragged. Passed from the inscription's own Bound* fields for the same reason the head buffers are: this call exists to re-point
                //    the image view, and every buffer binding should come back out exactly as it went in.
                RefreshSurfaceShadeInscription(Extension.SurfaceShade, Extension.VisibilityTarget,
                                               Extension.SurfaceShade.BoundVertexBuffer,   Extension.SceneGeometry.VertexByteCapacity,
                                               Extension.SurfaceShade.BoundIndexBuffer,    Extension.SceneGeometry.IndexByteCapacity,
                                               Extension.SurfaceShade.BoundInstanceBuffer,
                                               (VkDeviceSize)Extension.VisibilityRaster.InstanceCount * sizeof(SuzanneSceneInstance),
                                               Extension.SurfaceShade.FloorGeometryBound ? Extension.FloorGeometry.VertexBuffer : VK_NULL_HANDLE,
                                               Extension.FloorGeometry.VertexByteCapacity,
                                               Extension.SurfaceShade.FloorGeometryBound ? Extension.FloorGeometry.IndexBuffer : VK_NULL_HANDLE,
                                               Extension.FloorGeometry.IndexByteCapacity,
                                               Extension.SurfaceShade.FloorGeometryBound ? Extension.FloorRaster.InstanceBuffer : VK_NULL_HANDLE,
                                               (VkDeviceSize)Extension.FloorRaster.InstanceCount * sizeof(SuzanneSceneInstance),
                                               // ⚠️ And the shadow pair, for exactly the floor triple's reason: shadows would stop the first time the
                                               //    window was resized. The atlas view is extent-independent (a fixed 4096² image), so this hands back
                                               //    the same handles rather than re-deriving them.
                                               Extension.SurfaceShade.ShadowAtlasBound ? Extension.ShadowAtlas.AtlasSampledView : VK_NULL_HANDLE,
                                               Extension.SurfaceShade.ShadowAtlasBound ? Extension.ShadowAtlas.MappingBuffer    : VK_NULL_HANDLE,
                                               (VkDeviceSize)Extension.ShadowAtlas.TilePageMapping.size() * sizeof(uint32_t),
                                               Extension.SurfaceShade.ShadowAtlasBound ? Extension.ShadowAtlas.CoverageBuffer  : VK_NULL_HANDLE,
                                               (VkDeviceSize)ShadowPageCapacity * sizeof(uint32_t));

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

            // ================================================================================================================================
            //  S1 / S2 / S3 — the GPU marking chain (P6.3c)
            // ================================================================================================================================
            // 📝 Recorded HERE and nowhere else, because this is the one point in the image where both of S1's inputs are simultaneously legal to
            //    sample: the id buffer just reached SHADER_READ_ONLY on the line above, and the depth reached it at the HiZ hand-off (and again
            //    after the late re-raster). Earlier and the id buffer is still a colour attachment; later and the radiance scope has opened, which
            //    forbids the dispatches outright — Vulkan will not nest one dynamic-rendering scope inside another, and S2 opens its own.
            //
            // 🔴 The chain writes DEMAND ONLY. S1 raises Used|Direct where a receiver samples, S2 raises Update where a caster's depth is now
            //    wrong, S3 carries demand fine → coarse as Used|Coarse. NOTHING reads those bits to allocate a page or to rasterize shadow depth
            //    yet, so the presented image is byte-identical with the chain on or off. That is the Phase-0 gate, and it is what makes it safe to
            //    run this live rather than behind a toggle.
            //
            // ⚠️ Gated on VisibilityWritten for the same reason the resolve is: on the depth-clear-only path the id buffer was never written, so
            //    its bytes are undefined and S1 would manufacture demand for whatever tiles that garbage decodes to.
            if (Extension.TileMarking.ReadyCondition && Extension.TileStore.ReadyCondition && VisibilityWritten)
            {
                // Re-point the bindings this unit does not own. Idempotent — the reconfigures above rebuild both views on a resize, and the cull's
                // record buffer only appears once the scene loads, so this is the call that closes both gaps. The caster bounds ARE the cull records
                // (PartitionCullRecord already carries the world-space sphere S2 needs); passing 0 records simply records no tag draw.
                RefreshShadowTileMarkingBindings(Extension.TileMarking,
                                                 Extension.VisibilityTarget.IdView,
                                                 Extension.DepthTarget.DepthView,
                                                 Extension.InstanceCull.RecordBuffer,
                                                 Extension.InstanceCullRecordCount);

                // S1's push data. The inverse view-projection unprojects each id-buffer pixel back to a world receiver point, so it must be derived
                // from the SAME not-yet-advanced camera the raster above used — otherwise the reconstruction lands on the wrong surface and the marked
                // tile belongs to a point nothing was drawn at.
                ShadowMarkConstants MarkConstants;
                {
                    const FocalOrientation Frame          = SolveOrbitOrientation(Extension.ViewCamera);
                    const Matrix4f         Projection     = EvaluateProjectionFrame(Extension.ViewCamera);
                    const Matrix4f         ViewProjection = MultiplyMatrix(Projection, Frame.ViewMatrix);
                    const Matrix4f         Inverse        = InvertMatrix(ViewProjection);
                    for (int Column = 0; Column < 4; Column++)
                        for (int Row = 0; Row < 4; Row++)
                            MarkConstants.InverseViewProjection[Column * 4 + Row] = Inverse.Column[Column][Row];

                    MarkConstants.LightRightAxis[0] = Extension.SunWindow.Basis.RightAxis.XCoord;
                    MarkConstants.LightRightAxis[1] = Extension.SunWindow.Basis.RightAxis.YCoord;
                    MarkConstants.LightRightAxis[2] = Extension.SunWindow.Basis.RightAxis.ZCoord;
                    MarkConstants.LightUpAxis[0]    = Extension.SunWindow.Basis.UpAxis.XCoord;
                    MarkConstants.LightUpAxis[1]    = Extension.SunWindow.Basis.UpAxis.YCoord;
                    MarkConstants.LightUpAxis[2]    = Extension.SunWindow.Basis.UpAxis.ZCoord;

                    // ⚠️ DEAD FIELD, kept only so this struct still mirrors the shader's std140 push block. MarkVisibleShadowPages.comp no longer
                    //    reads ObserverPosition at all: the level now follows the receiver's own light-space offset from the window centre, which the
                    //    shader recovers from the toroidal origin. Fed the SHADOW CENTRE rather than the observer so that if anything ever revives
                    //    this field it lands on the same lattice the scroll solve above used, instead of silently reintroducing the eye.
                    MarkConstants.ObserverPosition[0] = Extension.CachedShadowCentre.XCoord;
                    MarkConstants.ObserverPosition[1] = Extension.CachedShadowCentre.YCoord;
                    MarkConstants.ObserverPosition[2] = Extension.CachedShadowCentre.ZCoord;

                    MarkConstants.ScreenExtentX = (int32_t)Extension.VisibilityTarget.Width;
                    MarkConstants.ScreenExtentY = (int32_t)Extension.VisibilityTarget.Height;
                    MarkConstants.LevelCount    = (uint32_t)Extension.SunWindow.Levels.size();
                }

                RecordShadowReceiverMarking(Extension.TileMarking, MarkConstants, CommandBuffer);
                RecordShadowCasterTagging(Extension.TileMarking, Extension.SunWindow, CommandBuffer);
                RecordShadowTilePropagation(Extension.TileMarking, (uint32_t)Extension.SunWindow.Levels.size(), CommandBuffer);

                // Pull the marked table back into staging for the NEXT image to resolve. ⚠️ The resolve deliberately lives at the TOP of the preamble,
                //    not here: staging is shared with the upload, so reading it after this line but before the copy has executed would return the reset
                //    mirror this image just pushed. One image stale, and that staleness is what makes it free.
                DownloadShadowTileStore(Extension.TileStore, CommandBuffer);
            }

            // ================================================================================================================================
            //  THE RADIANCE SCOPE (P5.9b) — the scene, in linear light
            // ================================================================================================================================
            // 📝 Sky + surface shade record HERE, into the offscreen linear HDR target, rather than into the swapchain scope where they used to
            //    live. This is the whole of the two-scope split: the scene accumulates unbounded linear radiance, the resolve tone maps it ONCE
            //    into the swapchain, and the display-referred overlays then draw on top of that untouched. The sky and the shade can finally
            //    agree at the horizon because only one curve is applied to both, and the Glass preset's alpha-over now composites linear light.
            //
            // 📝 It belongs in the PREAMBLE for a hard structural reason: Vulkan forbids nesting one dynamic-rendering scope inside another, and
            //    the substrate opens the swapchain scope around RecordSequence. The preamble is the seam that runs inside the command buffer but
            //    outside any scope, which is exactly what a second render target needs.
            //
            // 📝 The camera used here is this frame's not-yet-advanced ViewCamera — the same one the visibility raster above just used. That is
            //    deliberate and it FIXES a latent mismatch rather than introducing one: the shade reconstructs its geometry from the id buffer
            //    that raster wrote, so it must read the camera that wrote it. The grid and overlays in RecordSequence run on the advanced camera,
            //    one frame newer; that skew already existed for the raster and is unchanged in magnitude.
            if (Extension.RadianceScene.ReadyCondition && Extension.RadianceResolve.ReadyCondition)
            {
                TransitionRadianceTargetForRendering(Extension.RadianceScene, CommandBuffer);

                // loadOp CLEAR to black: the sky covers every pixel, so nothing depends on the clear value, but a DONT_CARE would leave the
                // half-float target holding the previous frame's radiance wherever a future pass declines to write — an invisible feedback loop.
                VkRenderingAttachmentInfoKHR RadianceAttachment = {};
                RadianceAttachment.sType                       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
                RadianceAttachment.imageView                   = Extension.RadianceScene.ColourView;
                RadianceAttachment.imageLayout                 = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                RadianceAttachment.loadOp                      = VK_ATTACHMENT_LOAD_OP_CLEAR;
                RadianceAttachment.storeOp                     = VK_ATTACHMENT_STORE_OP_STORE;
                RadianceAttachment.clearValue.color.float32[0] = 0.0f;
                RadianceAttachment.clearValue.color.float32[1] = 0.0f;
                RadianceAttachment.clearValue.color.float32[2] = 0.0f;
                RadianceAttachment.clearValue.color.float32[3] = 1.0f;

                VkRenderingInfoKHR RadianceScopeInfo = {};
                RadianceScopeInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
                RadianceScopeInfo.renderArea.extent    = Extent;
                RadianceScopeInfo.layerCount           = 1;
                RadianceScopeInfo.colorAttachmentCount = 1;
                RadianceScopeInfo.pColorAttachments    = &RadianceAttachment;

                Extension.Substrate.Host.CmdBeginRendering(CommandBuffer, &RadianceScopeInfo);

                // Sky first — it fills every pixel as the background the shade composites over.
                SkyDomeConstants SkyConstants;
                AssembleSkyConstants(Extension.ViewCamera, SkyConstants);
                RecordSkyAtmospherePass(Extension.SkyPass, Extension.Substrate.Host, CommandBuffer, Extent, SkyConstants);

                // Surface shade. As of P6.3a the pass binds the FLOOR's vertex/index/instance buffers alongside the heads' and shades both, so the
                // checkered floor is now a genuinely lit surface in the radiance target rather than a discarded partition range. That is what gives
                // the sun shadows a surface to land on — the analytic grid is an overlay drawn after the resolve and can never receive one.
                // ⚠️ VisibilityWritten is part of the gate, not decoration: the shade texelFetches the id buffer, and on an idle frame that buffer
                //    was never transitioned to COLOR_ATTACHMENT or written, so sampling it would reconstruct geometry out of undefined memory.
                // 🔴 EITHER mesh is now enough to make the pass worth recording. Keeping the old heads-only gate would leave a floor-only scene
                //    unshaded even with the floor descriptors correctly bound — the per-pixel range check already routes each identity to its own
                //    buffers, so a mesh that is absent contributes no pixels rather than wrong ones.
                const bool HeadsShadeable = Extension.VisibilityRaster.InstanceCount > 0
                                         && Extension.SceneGeometry.IndexCount > 0;
                const bool FloorShadeable = Extension.SurfaceShade.FloorGeometryBound
                                         && Extension.FloorRaster.InstanceCount > 0
                                         && Extension.FloorGeometry.IndexCount > 0;
                if (Extension.SurfaceShadeEnabled && Extension.SurfaceShade.ReadyCondition && VisibilityWritten
                    && (HeadsShadeable || FloorShadeable))
                {
                    // The same Z-up sun the shadow clipmap was scrolled against this image, so shading and shadowing cannot disagree.
                    float ShadeSunX = 0.0f, ShadeSunY = 0.0f, ShadeSunZ = 1.0f;
                    Atmosphere::ResolveSolarDirectionSceneFrame(Extension.SkyPass.Profile, ShadeSunX, ShadeSunY, ShadeSunZ);

                    SurfaceShadeConstants ShadeConstants;
                    AssembleSurfaceShadeConstants(Extension.ViewCamera, Extension.CompositeFeatureMask,
                                                  Vector3f{ ShadeSunX, ShadeSunY, ShadeSunZ }, ShadeConstants);

                    // 🔴 Sourced from the inscription's own record of what b5-b7 hold, never from whether a floor document loaded: when the floor is
                    //    absent those bindings are aliased onto the HEAD buffers, so enabling the shade would reconstruct floor pixels from head
                    //    triangles — a plausible-looking surface built from the wrong mesh, which is far harder to spot than a missing one.
                    ShadeConstants.FloorShadeEnabled = FloorShadeable ? 1u : 0u;

                    // 🔴 P6.5: gated on ShadowAtlasBound for the same reason FloorShadeEnabled is gated on FloorGeometryBound. When no atlas exists b8 is
                    //    aliased onto the VISIBILITY IMAGE — type-correct (both R32_UINT) and therefore silent — and visibility IDs read as depths hard
                    //    against the sun, so tracing the alias would report almost every surface occluded and black the scene out.
                    // ⚠️ ContentValid, not merely bound: the atlas must also have had depth rasterized into it this image. Tracing a freshly-cleared
                    //    atlas is harmless (every texel is the clear identity, which reads as unoccluded) but tracing one whose layout was never
                    //    transitioned for sampling is undefined, so the transition below is part of this gate's contract.
                    const bool ShadowTraceable = Extension.SunShadowTraceEnabled
                                              && Extension.SurfaceShade.ShadowAtlasBound
                                              && Extension.ShadowAtlas.ReadyCondition
                                              && Extension.SunWindow.ReadyCondition;
                    ShadeConstants.SunShadowEnabled = ShadowTraceable ? 1u : 0u;

                    if (ShadowTraceable)
                    {
                        // Built from the clipmap's CURRENT state (scrolled earlier this image) and the writer's own depth-encoding constants, so the
                        // reader's chain is the exact inverse of what S7 rasterized. 📝 The differential probe proved these two chains agree.
                        const SunShadowTraceBlock TraceBlock =
                            SolveSurfaceShadeTraceBlock(Extension.SunWindow,
                                                        ShadowDepthOriginMetres,
                                                        ShadowDepthRangeMetres,
                                                        SunShadowDepthBiasMetres,
                                                        (SunShadowDebugView)Extension.SunShadowDebugMode);
                        UploadSurfaceShadeTraceBlock(Extension.SurfaceShade, TraceBlock);
                    }

#ifdef FRONTIER_DEVELOPMENT_PROFILE
                    // 📝 THE READ SIDE, which had no instrumentation at all while every write-side stage had its own notice. That asymmetry is why the
                    //    producer could be proven healthy (S5 requests, S6 clears, S7 draws) with the scene still fully lit: ResolveSunVisibility returns
                    //    1.0 unconditionally when SunShadowEnabled is 0, so ONE false conjunct below disables shadowing with no error anywhere.
                    // 🔴 Reports each conjunct separately rather than the conjunction. A single "traceable: no" would restate the symptom; the whole
                    //    diagnostic value is in WHICH of the four is false, since each has a different cause and three of them are set far from here.
                    const int32_t TraceableState = ShadowTraceable ? 1 : 0;
                    if (TraceableState != Extension.ReportedShadowTraceable)
                    {
                        Extension.ReportedShadowTraceable = TraceableState;
                        ISSUE_NOTICE("render-extension",
                                     "sun shadows: TRACE %s — flag %u, atlas bound %u, atlas ready %u, window ready %u (levels %u, tile %.2f m, bias %.3f m)",
                                     ShadowTraceable ? "ON" : "OFF",
                                     (unsigned)Extension.SunShadowTraceEnabled,
                                     (unsigned)Extension.SurfaceShade.ShadowAtlasBound,
                                     (unsigned)Extension.ShadowAtlas.ReadyCondition,
                                     (unsigned)Extension.SunWindow.ReadyCondition,
                                     (unsigned)Extension.SunWindow.Levels.size(),
                                     Extension.SunWindow.Levels.empty() ? 0.0 : (double)Extension.SunWindow.Levels[0].TileMetres,
                                     (double)SunShadowDepthBiasMetres);
                    }
#endif

                    RecordSurfaceShadeInscription(Extension.SurfaceShade, Extent, ShadeConstants, CommandBuffer);
                }

                Extension.Substrate.Host.CmdEndRendering(CommandBuffer);

                // Hand the finished linear scene to the resolve as a sampled source. The resolve records in the swapchain scope, downstream.
                TransitionRadianceTargetForSampling(Extension.RadianceScene, CommandBuffer);
            }
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

            // F5 flips the tone-map operator between Khronos PBR Neutral and a plain linear clamp (P5.9b). This is the A/B that separates "the curve
            // is wrong" from "the radiance feeding the curve is wrong" — the bypass shows the linear buffer as-is, so a blown highlight in bypass is
            // a lighting problem while a blown highlight only under the curve is an exposure problem. Edge-latched; refuses to arm when the resolve
            // did not build, since there is no curve to flip then.
            const bool OperatorKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::F5);
            if (OperatorKeyDown && !Extension.RadianceOperatorKeyLatch)
            {
                if (!Extension.RadianceResolve.ReadyCondition)
                    printf("[radiance] tone-map A/B unavailable — the resolve did not build (shaders staged?)\n");
                else
                {
                    Extension.RadianceOperator = (Extension.RadianceOperator == RadianceOperatorPbrNeutral)
                                               ? RadianceOperatorBypass : RadianceOperatorPbrNeutral;
                    printf("[radiance] operator -> %s\n", (Extension.RadianceOperator == RadianceOperatorBypass)
                                                        ? "BYPASS (linear clamp)" : "Khronos PBR Neutral");
                }
                fflush(stdout);
            }
            Extension.RadianceOperatorKeyLatch = OperatorKeyDown;

            // 🧩 P6.5 — F6 cycles the sun-shadow diagnostic view: off -> resolved level -> depth margin -> occlusion -> off. Each view answers a
            // different question about a scene that renders fully lit, and the ORDER is the order the chain fails in: whether any page resolves at
            // all, then whether a resolved page holds real depth, then whether that depth occludes. Edge-latched like F5.
            // ⚠️ Refuses to arm unless the trace is actually reachable, because every view would otherwise paint the "no level resident" colour for a
            //    reason that has nothing to do with the atlas — the gate being shut — and that reading would send the search in the wrong direction.
            const bool ShadowDebugKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::F6);
            if (ShadowDebugKeyDown && !Extension.SunShadowDebugKeyLatch)
            {
                if (!Extension.SunShadowTraceEnabled || !Extension.SurfaceShade.ShadowAtlasBound)
                    printf("[shadow] debug view unavailable — trace %s, atlas %s\n",
                           Extension.SunShadowTraceEnabled ? "on" : "OFF",
                           Extension.SurfaceShade.ShadowAtlasBound ? "bound" : "NOT BOUND");
                else
                {
                    static const char* const ShadowDebugViewNames[4] =
                        { "OFF (normal shading)",
                          "RESOLVED LEVEL (magenta = no page resident at any level)",
                          "DEPTH MARGIN (white = page holds the clear identity, red = occluded, green = lit)",
                          "OCCLUSION (black = shadowed, white = lit)" };

                    Extension.SunShadowDebugMode = (Extension.SunShadowDebugMode + 1u) % 4u;
                    printf("[shadow] debug view -> %s\n", ShadowDebugViewNames[Extension.SunShadowDebugMode]);
                }
                fflush(stdout);
            }
            Extension.SunShadowDebugKeyLatch = ShadowDebugKeyDown;

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
            // 🔴 Read the observer the PREAMBLE cached, do not re-evaluate. DriveViewportCamera ran just above, so a fresh evaluation here would
            //    be one frame of camera motion ahead of the position the sun window was advanced against — the GI field and the shadow window
            //    would then be tracking two different viewers. The fallback covers only the first frame, before any preamble has run.
            const Vector3f ObserverPosition = Extension.ObserverCacheSeeded ? Extension.CachedObserverPosition
                                                                            : EvaluateObserverPosition(Extension.ViewCamera);
            IntegrateClipmapField(Extension.ClipmapField, ObserverPosition);

            // ================================================================================================================================
            //  THE SWAPCHAIN SCOPE — display-referred, from here down
            // ================================================================================================================================
            // 📝 The scene is already finished and sitting in the radiance target (see the radiance scope at the tail of RecordPreamble). The
            //    resolve tone maps it into this scope as the FIRST draw; everything after composites authored display-referred colour on top and is
            //    deliberately NOT tone mapped.
            // ⚠️ Nothing may be recorded before the resolve. It writes every pixel with blending disabled, so an earlier draw would be erased.
            RadianceResolveConstants ResolveTonemap;
            ResolveTonemap.Exposure      = Extension.SceneExposure;
            ResolveTonemap.OperatorIndex = Extension.RadianceOperator;
            RecordRadianceResolveInscription(Extension.RadianceResolve, Extent, ResolveTonemap, CommandBuffer);

            // 🔴 Fallback for the frame where the radiance target or its resolve did not build. The scene pipelines were then created against the
            //    swapchain format instead (see SceneColourFormat in Initialize), so the sky records DIRECTLY here — display-encoded, double-mapped
            //    at the horizon, exactly the pre-P5.9b behaviour. Degraded but not black. The shade stays with the radiance scope in the preamble;
            //    on this path it simply does not run, because a shade without its sky would composite over an undefined attachment.
            if (!Extension.RadianceResolve.ReadyCondition)
            {
                SkyDomeConstants SkyConstants;
                AssembleSkyConstants(Extension.ViewCamera, SkyConstants);
                RecordSkyAtmospherePass(Extension.SkyPass, Extension.Substrate.Host, CommandBuffer, Extent, SkyConstants);
            }

            // 📝 The ground grid, first of the display-referred overlays. It draws AFTER the resolve — i.e. after the shaded objects — because the
            //    shade moved upstream into the radiance target, and it CANNOT be moved back: the grid is display-referred (authored line colours,
            //    not light), so recording it inside the radiance scope would feed those colours through the tone map curve.
            //
            // 🐞 Drawing later used to mean drawing ON TOP: the grid owns no depth attachment, so it composited unconditionally and an infinite
            //    ground plane painted over the objects standing on it. Fixed by giving the grid a manual depth test instead of a depth attachment —
            //    it samples the scene depth the visibility raster wrote (already in SHADER_READ_ONLY here, unconditionally, since even the idle
            //    branch clears it) and rejects its own hit wherever real geometry stands nearer. So record order is now independent of occlusion:
            //    late enough to skip the tone map, still correctly behind the geometry.
            // ⚠️ The test needs the depth to hold THIS frame's geometry, which is why it may not be hoisted above the preamble's raster.
            //
            //    The schedule path (default-OFF) walked an ordered sky→grid spine assembled in Initialize. Its Phase-0 pixel-identity premise no
            //    longer holds — sky is not in this scope any more — so it records only the grid here and its gate needs re-stating against the
            //    two-scope frame before it is flipped on. See the backlog entry.
            if (Extension.Schedule.EnabledCondition)
            {
                RecordRenderSchedule(Extension.Schedule, CommandBuffer, Extent);
            }
            else
            {
                GroundGridConstants Constants;
                AssembleGridConstants(Extension.ViewCamera, Constants);
                RecordGroundGridPass(Extension.GridPass, CommandBuffer, Extent, Constants);
            }

            // Visibility resolve (Phase 2b A/B): composite the id buffer over the forward view when toggled on. Records inside this same colour
            // scope, reading the visibility image the preamble transitioned to SHADER_READ_ONLY. Gated on the raster having run
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

            // 📝 The deferred surface shade used to record HERE, right after the id-hash resolve, so that with both toggles on the real shading won
            //    the pixel. It now runs upstream in the radiance scope (preamble), which INVERTS that precedence: the id-hash resolve composites
            //    over the already-resolved shaded scene, so with both on the debug hash wins. That is the right way round for a debug A/B — F2 is
            //    asking to see the raw identity, and it should not be silently overpainted by the shading it is being compared against.

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
                                                  Extension.ComponentOverlay, OverlayConstants);
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
                // What bounds the overlay's cost is the RADIUS GATE inside the refresh, not the shell copy handed in: the shell reduction trims only
                // the few hundred cells the curved surfaces bury at level 0 (and none at all at level 2, where the floor is one cell thick), while
                // the gate withholds thousands. The gate is the load-bearing limit — see ClipmapOccupiedDisplayRadius. The full occupancy set stays
                // intact for consumers (see the two vectors in RenderExtension.h).
                RefreshClipmapInspection(Extension.ClipmapInspection, Extension.ClipmapField, ObserverPosition,
                                         Extension.DisplayedOccupancyCells, Extension.ClipmapOccupancyLevel,
                                         ClipmapDisplayShellRadius, ClipmapOccupiedDisplayRadius);

                // Report the drawn/withheld split the first time the overlay runs, and again whenever the drawn count changes materially, so the
                // gate is never silent about what it is holding back. Per-frame logging would flood, so this fires on change only.
                if (Extension.ClipmapInspection.CellCount != Extension.ReportedInspectionCellCount)
                {
                    Extension.ReportedInspectionCellCount = Extension.ClipmapInspection.CellCount;
                    ISSUE_NOTICE("render-extension", "clipmap overlay: drawing %u cells (%u occupied cells withheld beyond the %d-cell display "
                                                     "radius, %u dropped by the capacity cap)",
                                 (unsigned)Extension.ClipmapInspection.CellCount,
                                 (unsigned)Extension.ClipmapInspection.RemoteCellCount,
                                 (int)ClipmapOccupiedDisplayRadius,
                                 (unsigned)Extension.ClipmapInspection.DroppedCellCount);
                }

                // ⚠️ A capacity drop is a DIFFERENT event from the radius gate and needs its own alarm, because the notice above cannot report it
                //    reliably: once the cursor clamps at the capacity the drawn count stops changing, so the change-triggered branch goes quiet at
                //    exactly the moment cells are being discarded. Raising the display radius past ~96 at level 0 reaches this (measured), and the
                //    on-screen result is an overlay that looks complete while silently omitting whatever the cap cut — the one failure that could be
                //    misread as "the voxelizer missed those cells". Latched so it states the problem once per onset rather than flooding.
                const bool CapacityShortfall = (Extension.ClipmapInspection.DroppedCellCount > 0u);
                if (CapacityShortfall != Extension.ReportedInspectionShortfall)
                {
                    Extension.ReportedInspectionShortfall = CapacityShortfall;
                    if (CapacityShortfall)
                        ISSUE_CAUTION("render-extension", "clipmap overlay TRUNCATED: %u cells dropped at the %u-cell capacity — the overlay is "
                                                          "under-drawing, not the voxelization. Lower ClipmapOccupiedDisplayRadius (now %d) or raise "
                                                          "ClipmapInspectionCellCapacity",
                                      (unsigned)Extension.ClipmapInspection.DroppedCellCount,
                                      (unsigned)ClipmapInspectionCellCapacity,
                                      (int)ClipmapOccupiedDisplayRadius);
                }

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
    // Sun shadows: the atlas owns an image + memory + two views, so it tears down with the other device resources under the same idle. The window
    // is host-only and merely releases vectors.
    // ⚠️ Marking before the store: the submission's descriptor set holds a binding pointing at the store's table buffer, so destroying the buffer
    //    first would leave a live set referencing freed memory for the duration of these two calls.
    // ⚠️ Same argument for S6 before the ATLAS: its descriptor set holds a storage-image binding pointing at Atlas.AtlasStorageView, so the view must
    //    outlive the set that references it.
    // 🔴 S7 before the atlas: its descriptor sets hold a storage-image binding on Atlas.AtlasStorageView, so destroying the atlas first would leave the
    //    pool referencing a dead view.
    FinalizeShadowDepthRasterSubmission(Extension.ShadowDepthRaster);
    Extension.ShadowCasterSceneSet = VK_NULL_HANDLE;   // 📝 Freed with S7's pool, not individually.
    Extension.ShadowCasterFloorSet = VK_NULL_HANDLE;
    FinalizeShadowPageClearSubmission(Extension.ShadowPageClear);
    FinalizeShadowTileMarkingSubmission(Extension.TileMarking);
    FinalizeShadowTileStore(Extension.TileStore);
    FinalizeShadowPageAtlas(Extension.ShadowAtlas);
    FinalizeSunShadowClipmap(Extension.SunWindow);
    // Object selection torn down before the resolve, mirroring the reverse-of-creation order it was built in (readback + outline came after it).
#ifdef FRONTIER_POLYGON_AUTHORING
    FinalizeComponentOverlayInscription(Extension.ComponentOverlay);
    FinalizeSelectionOutlineInscription(Extension.SelectionOutline);
    FinalizeObjectPickReadback(Extension.ObjectPick);
#endif

    // The resolve borrows the radiance target's view, so it is torn down first — releasing a sampler's descriptor after the image it points at is
    // destroyed is the wrong order even when both happen under one device-idle.
    FinalizeRadianceResolveInscription(Extension.RadianceResolve);
    FinalizeRadianceTarget(Extension.RadianceScene);
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
