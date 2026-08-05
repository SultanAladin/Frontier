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

// 📝 ImGui — headers only; the backend impl objects link in via the exe's Build.bat. The tuning window uses the Vulkan backend and the shared
//    theme, rendered into the substrate's own command buffer (see the F10 window in RecordSequence). NO VulkanImguiInterface (second swapchain).
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "EngineContext/Interface/Theme/ThemeResolver.h"
#include "EngineContext/Interface/WorkspaceHost/ImguiPlatformRelay.h"

#include <algorithm>
#include <string>
#include <vector>

#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>   // getenv / strtol — the census auto-arm (FRONTIER_SURFEL_CENSUS)

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

    // Phase 3 surfel GI: the gather's grid origin MUST be the SAME camera-relative origin the slotting/integrate used this frame — the raw eye
    // (AssembleSurfelSlottingConstants uses Frame.EyePosition verbatim, no snap). Reusing Frame here keeps host and shader on the same cell lattice;
    // a different origin would hash the shade point into a different bucket than the surfels were slotted into and the gather would find nothing.
    Constants.GridOrigin[0] = Frame.EyePosition.XCoord;
    Constants.GridOrigin[1] = Frame.EyePosition.YCoord;
    Constants.GridOrigin[2] = Frame.EyePosition.ZCoord;
    Constants.GridOrigin[3] = 0.0f;

    // The radial-depth occlusion tunables, matching the integrate's defaults (SurfelRadialDepth.cpp: 1.2, 0.2, 0.25, 0.15). Left here so the shade's
    // gate and the integrate's learning agree; the SurfelReadOffsetElements / SurfelGiEnabled / SurfelCapacity fields are runtime state set at the
    // call site (they depend on the post-swap pool parity and the GI toggle, which this camera-only assemble cannot see).
    Constants.OcclusionParams[0] = 1.2f;
    Constants.OcclusionParams[1] = 0.2f;
    Constants.OcclusionParams[2] = 0.25f;
    Constants.OcclusionParams[3] = 0.15f;
}

// Fill the surfel slotting push block. GridOrigin is the eye position: the grid is CAMERA-RELATIVE, so the eye is the origin every cell coordinate is
// measured against (matching the SurfelValidation oracle, which feeds the raw camera position as the grid origin it slots and looks up against).
// CameraPosition is the raw eye that drives the eye-distance surfel radius (SurfelGrid.glsl reads it separately from the origin). ListCount is the slot
// pass's bounds guard.
void AssembleSurfelSlottingConstants(const ViewportCamera& Subject, const SurfelTuningState& Tuning, SurfelSlottingConstants& Constants)
{
    const FocalOrientation Frame = SolveOrbitOrientation(Subject);
    Constants.CameraPosition[0] = Frame.EyePosition.XCoord;
    Constants.CameraPosition[1] = Frame.EyePosition.YCoord;
    Constants.CameraPosition[2] = Frame.EyePosition.ZCoord;
    Constants.CameraPosition[3] = 0.0f;
    Constants.GridOrigin[0]     = Frame.EyePosition.XCoord;
    Constants.GridOrigin[1]     = Frame.EyePosition.YCoord;
    Constants.GridOrigin[2]     = Frame.EyePosition.ZCoord;
    Constants.GridOrigin[3]     = 0.0f;
    Constants.ListCount         = (int32_t)SurfelGridListCount;

    // Live world-scale knobs (F10 window) — the count/slot grid box + intersection must match the spawn/gather cell diameter + radius exactly.
    Constants.TuneCellDiameter  = Tuning.CellDiameter;
    Constants.TuneBaseRadius    = Tuning.BaseRadius;
    Constants.TuneNearFieldBias = Tuning.NearFieldBias;
}

// Fill the surfel spawn push block. Shares the shade's clip->world reconstruction (the spawn reads the SAME visibility id buffer and reconstructs world
// pos/normal exactly as SurfaceShade does), the same camera-relative grid origin as the slotting above, and the same floor rebase as the shade
// (FloorPartitionBase / FloorIndexBase). ScreenAndTiles packs (width, height, tiles-x, frame index) for the per-tile spawn dispatch.
void AssembleSurfelSpawnConstants(const ViewportCamera& Subject, VkExtent2D Extent, uint32_t FrameIndex,
                                  bool FloorResident, uint32_t FloorIndexBase, float DensityScale,
                                  const SurfelTuningState& Tuning, SurfelSpawnConstants& Constants)
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
    Constants.GridOrigin[0]     = Frame.EyePosition.XCoord;
    Constants.GridOrigin[1]     = Frame.EyePosition.YCoord;
    Constants.GridOrigin[2]     = Frame.EyePosition.ZCoord;
    Constants.GridOrigin[3]     = 0.0f;

    Constants.ScreenAndTiles[0] = (int32_t)Extent.width;
    Constants.ScreenAndTiles[1] = (int32_t)Extent.height;
    Constants.ScreenAndTiles[2] = (int32_t)SurfelSpawnTilesAcross(Extent.width);
    Constants.ScreenAndTiles[3] = (int32_t)FrameIndex;

    Constants.FloorPartitionBase = FloorPartitionBase;
    Constants.FloorShadeEnabled  = FloorResident ? 1u : 0u;
    Constants.FloorIndexBase     = FloorResident ? FloorIndexBase : 0u;
    Constants.SpawnDensityScale  = DensityScale;

    // Live world-scale knobs (F10 tuning window). Passed straight into the spawn push block; the shader seats them into SurfelGrid.glsl's globals.
    Constants.TuneCellDiameter  = Tuning.CellDiameter;
    Constants.TuneBaseRadius    = Tuning.BaseRadius;
    Constants.TuneNearFieldBias = Tuning.NearFieldBias;
    // The spawn gate ceiling. Uses the APPLIED cap (the value the List buffer is sized for), NOT the pending combo choice — a click that has not
    // been Applied must not let a cell pack past what the current allocation covers. Apply commits Choice -> Applied at the RecordPreamble seam.
    Constants.PerCellCap        = Tuning.PerCellCapApplied;
}

// Fill the surfel debug splat push block. ViewProjection projects each live surfel's world position to clip; CameraPosition drives the disc radius; the
// grid origin (== the eye, matching the slotting) lets the cascade / occupancy modes recover the same cell the build used. DebugMode is the
// F6-selected number; Capacity is the pool capacity == the point-list vertex count.
void AssembleSurfelDebugConstants(const ViewportCamera& Subject, VkExtent2D Extent, uint32_t DebugMode,
                                  float RadiusScale, uint32_t ReadOffsetElements, uint32_t Capacity,
                                  const SurfelTuningState& Tuning, SurfelDebugConstants& Constants)
{
    const FocalOrientation Frame          = SolveOrbitOrientation(Subject);
    const Matrix4f         Projection     = EvaluateProjectionFrame(Subject);
    const Matrix4f         ViewProjection = MultiplyMatrix(Projection, Frame.ViewMatrix);
    Constants.ViewProjection = ViewProjection;

    Constants.CameraPosition[0] = Frame.EyePosition.XCoord;
    Constants.CameraPosition[1] = Frame.EyePosition.YCoord;
    Constants.CameraPosition[2] = Frame.EyePosition.ZCoord;
    Constants.CameraPosition[3] = 0.0f;
    Constants.GridOrigin[0]     = Frame.EyePosition.XCoord;
    Constants.GridOrigin[1]     = Frame.EyePosition.YCoord;
    Constants.GridOrigin[2]     = Frame.EyePosition.ZCoord;
    Constants.GridOrigin[3]     = 0.0f;

    Constants.ScreenAndRadius[0] = (float)Extent.width;
    Constants.ScreenAndRadius[1] = (float)Extent.height;
    Constants.ScreenAndRadius[2] = RadiusScale;   // F8/F9 disc-size multiplier atop the grid's own eye-distance radius
    Constants.ScreenAndRadius[3] = 0.0f;

    Constants.DebugMode          = DebugMode;
    Constants.Capacity           = Capacity;
    // 🔴 Post-swap read half for the GI modes (5-7): Moments[i + ReadOffsetElements] over 20-float structs. Same ELEMENT base the shade uses
    //    (MomentsParity*Capacity), NOT the byte offset SurfelMomentsReadOffset returns. Zero for the non-GI modes — they never touch Moments[].
    Constants.ReadOffsetElements = ReadOffsetElements;

    // Live world-scale knobs (F10 window) — the splat disc size + cascade/occupancy modes read the SAME cell/radius globals the field uses.
    Constants.TuneCellDiameter  = Tuning.CellDiameter;
    Constants.TuneBaseRadius    = Tuning.BaseRadius;
    Constants.TuneNearFieldBias = Tuning.NearFieldBias;
    // The occupancy heatmap (mode 4) normalizes cell fill against the APPLIED cap so the ramp matches what the spawn gate is actually enforcing.
    Constants.PerCellCap        = Tuning.PerCellCapApplied;
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
            GeometryTree HeadTree;

            // 🔴 BOTH DOCUMENTS ARE DECODED BEFORE EITHER IS UPLOADED, and the order is the merge. The heads and the floor now share ONE vertex and ONE
            //    index buffer, because a GeometryArenaSlice names a mesh by absolute offsets into a shared stream — a mesh sitting in its own private
            //    buffer cannot be named at all, which is what made the floor invisible to every ray. Sizing that shared claim requires knowing both
            //    meshes, so the floor decode moved ABOVE the upload it used to sit below.
            //    ⚠️ Append order is the ordinal order and is permanent: heads are ordinal 0, floor ordinal 1. Both are appended unconditionally on a
            //       successful decode so a missing floor cannot renumber the heads.
            const std::string FloorPath = std::string(FRONTIER_SCENE_ASSET_DIR) + "/CheckerFloor.wsdoc";
            RenderVertexStream FloorStream;
            std::vector<SuzanneSceneInstance> FloorInstances;
            GeometryTree FloorTree;
            const bool FloorDecoded = LoadFloorDocument(FloorPath.c_str(), FloorStream, FloorInstances, &FloorTree);
            if (!FloorDecoded)
                ISSUE_CAUTION("render-extension", "checkered floor document unavailable ('%s') — floor not drawn", FloorPath.c_str());

            // ⚠️ PartitionBase is 0 here — this is the PRIMARY scene. Passing FloorPartitionBase would shift every head identity into the floor's reserved
            //    range, which the overlay rejects outright, so the handles would vanish rather than merely misdraw.
            const bool HeadsDecoded = LoadWorkspaceScene(DocumentPath.c_str(), Stream, Instances, &LoadedDocument, &TriangleSourceFace,
                                                         0u, &AuthoredTopology, &HeadTree);

            // Fold both meshes into the one run the arena's offsets are measured against, then claim it once. The heads' own placement is recorded
            // even though it is trivially zero, so no consumer has to special-case "the first mesh".
            ResetGeometryStreamConcatenation(Extension.SceneStreams);
            if (HeadsDecoded)
                AppendGeometryStream(Extension.SceneStreams, Stream, Extension.HeadMeshOrdinal);
            if (FloorDecoded)
            {
                AppendGeometryStream(Extension.SceneStreams, FloorStream, Extension.FloorMeshOrdinal);
                Extension.FloorStreamPresent = !FloorStream.Indices.empty();
            }

            if (HeadsDecoded &&
                ConstructPolygonBufferAllocation(Extension.Substrate.Host, Extension.UploadPool,
                                                 Extension.SceneStreams.Merged, Extension.SceneGeometry))
            {
                UploadVisibilityScene(Extension.VisibilityRaster, Instances);

                // 🔴 The heads' own extent within the merged buffer. SceneGeometry.IndexCount is now the WHOLE world (heads + floor), so every
                //    heads-only consumer below reads this placement instead — a cull whose indirect draw named the merged count would draw the floor
                //    slab once per head, with each head's transform.
                const GeometryStreamPlacement HeadPlacement =
                    RetrieveGeometryStreamPlacement(Extension.SceneStreams, Extension.HeadMeshOrdinal);

                // ⚠️ The GPU cull's indirect argument hardcodes FirstIndex 0 (InstanceCullSubmission.cpp:494), so the indirect head draw is only
                //    correct while the heads are the FIRST mesh in the merged run. That holds by construction — they are appended first — but the
                //    dependency is invisible from there, so it is asserted here rather than left to be discovered as a scrambled indirect draw.
                //    Three other consumers ride on the same fact and have no base of their own: the shade's HEAD reconstruction (Indices[Primitive*3],
                //    the b1-b2 branch — the floor's counterpart needed FloorIndexBase precisely because it is NOT at zero), the component overlay's,
                //    and SoftwareRasterization.comp's Indices[Triangle*3]. Give the heads a non-zero offset and all four break at once, each into a
                //    wrong-but-plausible surface rather than a blank one.
                if (HeadPlacement.IndexOffset != 0u)
                    ISSUE_CAUTION("render-extension", "heads are not at merged-stream offset 0 (%u) — the indirect cull draw and every "
                                  "zero-based reconstruction (shade, overlay, software raster) will be wrong",
                                  (unsigned)HeadPlacement.IndexOffset);

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
                                              LocalSphere, LocalCone, HeadPlacement.IndexCount);
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
                                                   (VkDeviceSize)Instances.size() * sizeof(SuzanneSceneInstance));
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
                             (unsigned)Instances.size(), (unsigned)(HeadPlacement.IndexCount / 3),
                             (unsigned)Registration.SpawnedCount, DocumentName);

                // -- The floor half of the merged claim. Its geometry is ALREADY resident (it was folded into the buffer constructed above), so what
                //    remains is the instance upload, the occupancy sweep, and pointing the shade's floor bindings at the merged buffer.
                if (Extension.FloorStreamPresent)
                {
                    const GeometryStreamPlacement FloorPlacement =
                        RetrieveGeometryStreamPlacement(Extension.SceneStreams, Extension.FloorMeshOrdinal);

                    UploadVisibilityScene(Extension.FloorRaster, FloorInstances);

                    // Retain the floor's transform for the surfel micro-raster's floor dispatch (it pushes the matrix rather than binding this
                    // buffer — see RenderExtension.h). FloorInstances is a local that dies with this block, so copy it now or lose it.
                    if (!FloorInstances.empty())
                        std::memcpy(Extension.FloorInstanceModel, FloorInstances[0].Model, sizeof(Extension.FloorInstanceModel));
                    ISSUE_NOTICE("render-extension", "checkered floor merged: %u instances, %u triangles at vertex %u / index %u",
                                 (unsigned)FloorInstances.size(), (unsigned)(FloorPlacement.IndexCount / 3),
                                 (unsigned)FloorPlacement.VertexOffset, (unsigned)FloorPlacement.IndexOffset);

                    // The floor is scene geometry too, so it belongs in the clipmap occupancy alongside the heads — previously it was loaded into
                    // its own local stream and never voxelized, which is why the slab showed no occupied cells at all. Its triangles are large
                    // enough to each propose a wide candidate box, so this is the sweep the cell budget above exists for.
                    VoxelizeSceneOccupancy(Extension, FloorStream, FloorInstances, "floor");

                    // 🔴 b5-b7 STAY WIRED, and they now point at the SAME merged buffer as b1-b2 rather than at a separate floor allocation. That is
                    //    not the aliasing hazard the FloorShadeEnabled gate was written against: back then an absent floor left b5-b7 aliased onto the
                    //    heads' buffers, so a floor pixel would reconstruct from head geometry. Here the floor's triangles genuinely live in this
                    //    buffer, and the shader reaches them because the floor's indices were REBASED at append time — the same absolute indices the
                    //    raster draws with. The gate keeps its original meaning (0 = no floor geometry to reconstruct from) and is still driven by
                    //    FloorGeometryBound below.
                    if (Extension.SurfaceShade.ReadyCondition)
                    {
                        RefreshSurfaceShadeInscription(Extension.SurfaceShade, Extension.VisibilityTarget,
                                                       Extension.SurfaceShade.BoundVertexBuffer,   Extension.SceneGeometry.VertexByteCapacity,
                                                       Extension.SurfaceShade.BoundIndexBuffer,    Extension.SceneGeometry.IndexByteCapacity,
                                                       Extension.SurfaceShade.BoundInstanceBuffer,
                                                       (VkDeviceSize)Extension.VisibilityRaster.InstanceCount * sizeof(SuzanneSceneInstance),
                                                       Extension.SceneGeometry.VertexBuffer, Extension.SceneGeometry.VertexByteCapacity,
                                                       Extension.SceneGeometry.IndexBuffer,  Extension.SceneGeometry.IndexByteCapacity,
                                                       Extension.FloorRaster.InstanceBuffer,
                                                       (VkDeviceSize)FloorInstances.size() * sizeof(SuzanneSceneInstance));
                    }
                }

                // -- The arena: one bottom-level tree per merged mesh, appended in the SAME order as the streams so a mesh ordinal names the same mesh
                //    in both tables. This is the first point every tree and every offset exists together. Best-effort throughout — a scene that cannot
                //    be traced still rasters, which is why a failed append only costs global illumination.
                if (InitializeGeometryArenaSubmission(Extension.GeometryArena, Extension.Substrate.Host))
                {
                    uint32_t AppendedOrdinal = 0;
                    if (HeadTree.ReadyCondition)
                        AppendGeometryTreeToArena(Extension.GeometryArena, HeadTree,
                                                  HeadPlacement.VertexOffset, HeadPlacement.IndexOffset, AppendedOrdinal);

                    if (Extension.FloorStreamPresent && FloorTree.ReadyCondition)
                    {
                        const GeometryStreamPlacement FloorPlacement =
                            RetrieveGeometryStreamPlacement(Extension.SceneStreams, Extension.FloorMeshOrdinal);
                        AppendGeometryTreeToArena(Extension.GeometryArena, FloorTree,
                                                  FloorPlacement.VertexOffset, FloorPlacement.IndexOffset, AppendedOrdinal);
                    }

                    if (UploadGeometryArena(Extension.GeometryArena, Extension.UploadPool))
                        ISSUE_NOTICE("render-extension", "geometry arena uploaded: %u mesh slices",
                                     (unsigned)Extension.GeometryArena.Slices.size());
                    else
                        ISSUE_CAUTION("render-extension", "geometry arena upload failed — scene rasters but does not trace");
                }
            }
            else
            {
                ISSUE_CAUTION("render-extension", "visibility scene document unavailable ('%s') — raster idle", DocumentPath.c_str());
            }
        }
    }

    // Every mesh that contributes occupancy has now been swept, so close the bake: dedup across instances and derive the overlay's shell copy.
    // Placed outside the load branches on purpose — it must run even when a document failed to load, so the two sets never disagree.
    FinalizeSceneOccupancy(Extension);

    // -- Surfel GI, Phase 1 (pool + hash grid + spawn + debug view). Stood up HERE, after the scene load, because the pool/slotting inits submit
    //    one-shot clears on the UploadPool and the lifecycle's first Refresh binds the merged mesh buffers — both of which only exist once the
    //    document has loaded. The debug splat is built against the SCENE colour format (it draws into the radiance scope, like the sky + shade), so
    //    it reuses the SceneColourFormat resolved above. All best-effort: a failed init leaves ReadyCondition false and every surfel record no-ops,
    //    exactly like the clipmap visualization, so the colour path is unaffected. Skipped entirely when the UploadPool never came up (no raster).
#ifndef FRONTIER_SURFEL_SHADER_DIR
#define FRONTIER_SURFEL_SHADER_DIR "Shaders"
#endif
    if (Extension.UploadPool != VK_NULL_HANDLE)
    {
        InitializeSurfelPool(Extension.SurfelPoolResource, Extension.Substrate.Host, Extension.UploadPool, SurfelMaxCount);
        InitializeSurfelGridSlotting(Extension.SurfelSlotting, Extension.Substrate.Host, Extension.UploadPool, FRONTIER_SURFEL_SHADER_DIR);

        // The spawn dispatch is one workgroup per 8x8 visibility tile; the per-tile request buffers are sized ONCE for the largest image the window
        // can reach so a resize never re-allocates them. 4K covers every practical swapchain; a smaller live extent simply leaves the tail unused.
        const uint32_t SurfelSpawnMaxTileCount = SurfelSpawnTilesAcross(3840u) * SurfelSpawnTilesAcross(2160u);
        InitializeSurfelLifecycleSubmission(Extension.SurfelLifecycle, Extension.Substrate.Host,
                                            SurfelSpawnMaxTileCount, FRONTIER_SURFEL_SHADER_DIR);
        InitializeSurfelDebugInscription(Extension.SurfelDebug, Extension.Substrate.Host, SceneColourFormat, FRONTIER_SURFEL_SHADER_DIR);

        // 🩺 The census counters buffer must exist BEFORE the Refresh below: Age/Allocate declare pool-set binding 9 unconditionally, and there is no
        //    safe alias for it (every pool atomic is a single int — see SurfelCensusTrace.h), so a missing buffer leaves a declared binding undefined.
        InitializeSurfelCensusTrace(Extension.SurfelCensus, Extension.Substrate.Host);

        // 🩺 FRONTIER_SURFEL_CENSUS=1 auto-arms the census at startup, so the trace can be taken WITHOUT a hand on the K key. This is not a
        //    convenience: the census answers a question about the first few hundred frames of pool life (does the population churn, and what kills
        //    it), and by the time a human has alt-tabbed in and pressed K that window is already gone. Interactive runs are untouched — the variable
        //    is absent, this is inert, and K still toggles.

        // 🩺 FRONTIER_SURFEL_CENSUS=1 auto-arms the census at startup, so the trace can be taken WITHOUT a hand on the K key. This is not a
        //    convenience: the census answers a question about the first few hundred frames of pool life (does the population churn, and what kills
        //    it), and by the time a human has alt-tabbed in and pressed K that window is already gone. Interactive runs are untouched — the variable
        //    is absent, this is inert, and K still toggles.
        if (const char* CensusAutoArm = std::getenv("FRONTIER_SURFEL_CENSUS"))
        {
            const SurfelCensusRunLabel RunLabel = ComposeSurfelCensusRunLabel(Extension.SurfelTuning);

            if (CensusAutoArm[0] == '1' && BeginSurfelCensusRecording(Extension.SurfelCensus, "SurfelDumps", &RunLabel))
            {
                Extension.SurfelCensusAutoFrames = 600u;   // past TTL=500 so an initial cohort can die of old age inside the window
                if (const char* CensusFrameCount = std::getenv("FRONTIER_SURFEL_CENSUS_FRAMES"))
                {
                    const long Requested = std::strtol(CensusFrameCount, nullptr, 10);
                    if (Requested > 0)
                        Extension.SurfelCensusAutoFrames = (uint32_t)Requested;
                }
                printf("[surfel] census auto-armed for %u frames\n", Extension.SurfelCensusAutoFrames);
                fflush(stdout);
            }
        }

        // Point the debug splat at the pool + grid buffers (idempotent; safe every frame later). The lifecycle's spawn set binds the visibility
        // image + the merged mesh buffers exactly as the shade does — heads always, floor when its run is genuinely resident (FloorGeometryBound),
        // else VK_NULL_HANDLE so the three floor bindings alias onto the heads and FloorShadeEnabled stays 0 in the spawn constants below.
        if (Extension.SurfelDebug.ReadyCondition)
            RefreshSurfelDebugInscription(Extension.SurfelDebug, Extension.SurfelPoolResource, Extension.SurfelSlotting,
                                          Extension.DepthTarget.DepthView);

        if (Extension.SurfelLifecycle.ReadyCondition && Extension.VisibilityRaster.ReadyCondition)
        {
            const bool FloorResident = Extension.SurfaceShade.FloorGeometryBound && Extension.FloorRaster.InstanceCount > 0;
            RefreshSurfelLifecycleVisibility(Extension.SurfelLifecycle, Extension.VisibilityTarget.IdView,
                                             Extension.SurfelPoolResource, Extension.SurfelSlotting,
                                             Extension.SceneGeometry.VertexBuffer, Extension.SceneGeometry.IndexBuffer,
                                             Extension.VisibilityRaster.InstanceBuffer,
                                             FloorResident ? Extension.SceneGeometry.VertexBuffer   : VK_NULL_HANDLE,
                                             FloorResident ? Extension.SceneGeometry.IndexBuffer    : VK_NULL_HANDLE,
                                             FloorResident ? Extension.FloorRaster.InstanceBuffer   : VK_NULL_HANDLE,
                                             Extension.SurfelCensus.CountersBuffer);
        }

        // -- Phase 3: point the SHADE's surfel set (set 1) at the SAME seven buffers the integrate writes, so the deferred gather reads the live cache.
        //    Bind ONCE here (the surfel buffers are stable after the pool + slotting init — unlike the id view they do NOT rebuild on resize, so no
        //    resize-path re-Refresh is needed). Best-effort: a no-op until the shade's surfel layout, the pool, and the slotting are all ready, leaving
        //    SurfelSetReady false so the shade record forces the flat-ambient path. This is what the F7 GI toggle A/B-tests against.
        RefreshSurfaceShadeSurfelBindings(Extension.SurfaceShade, Extension.SurfelPoolResource, Extension.SurfelSlotting);

        // -- Top-level acceleration structure (TLAS). Stood up beside the surfels because it, too, needs the loaded scene: it reduces boxes over the
        //    instance array the raster holds and reads the arena's slice + node buffers. The scene is STATIC after load (VisibilityRaster.InstanceCount
        //    is never reassigned), so every buffer handle is stable — we Bind* ONCE here and Record-only per frame. This diverges from the validation
        //    exe (TwoLevelTraceValidation re-binds inside its submit-and-wait loop, trivially safe there); live, with two frames in flight and fresh
        //    per-slot command buffers, re-binding a set already recorded into an in-flight command buffer is UNDEFINED, so the binds MUST stay out of
        //    the per-frame path. SetRadixSortKeyCount sets a scalar (not a descriptor) and the count is fixed, so it is hoisted here too. Best-effort:
        //    a failed init/bind leaves TlasReady false and the per-frame record no-ops, so the colour path is unaffected. Feeds nothing yet — Phase 2.
        constexpr uint32_t TlasInstanceCapacity = 65536; // designed-for-growth; today's scene is a handful of instances, well under RadixSortKeyCeiling
        const uint32_t TlasInstanceCount = Extension.VisibilityRaster.InstanceCount;
        const uint32_t TlasSliceCount    = (uint32_t)Extension.GeometryArena.Slices.size();
        if (Extension.GeometryArena.UploadedCondition && TlasInstanceCount > 0 && TlasInstanceCount <= TlasInstanceCapacity)
        {
            VkBuffer ArenaNode = VK_NULL_HANDLE, ArenaPrimitive = VK_NULL_HANDLE, ArenaSlice = VK_NULL_HANDLE, ArenaParent = VK_NULL_HANDLE;
            RetrieveGeometryArenaBuffers(Extension.GeometryArena, ArenaNode, ArenaPrimitive, ArenaSlice, ArenaParent);

            const bool BoundsOk = InitializeInstanceBoundsSubmission(Extension.TlasBounds, Extension.Substrate.Host, FRONTIER_SURFEL_SHADER_DIR);
            const bool SortOk   = InitializeRadixSortSubmission(Extension.TlasSort, Extension.Substrate.Host, TlasInstanceCapacity, FRONTIER_SURFEL_SHADER_DIR);
            const bool TreeOk   = InitializeInstanceTreeSubmission(Extension.TlasTree, Extension.Substrate.Host, TlasInstanceCapacity, FRONTIER_SURFEL_SHADER_DIR);

            if (BoundsOk && SortOk && TreeOk)
            {
                const VkDeviceSize InstanceBytes = (VkDeviceSize)TlasInstanceCount * sizeof(SuzanneSceneInstance);
                const VkDeviceSize KeyBytes      = (VkDeviceSize)TlasInstanceCount * sizeof(uint32_t);

                VkBuffer MortonKey = VK_NULL_HANDLE, MortonPayload = VK_NULL_HANDLE;
                RetrieveRadixSortInputBuffers(Extension.TlasSort, MortonKey, MortonPayload);

                VkBuffer SortedKey = VK_NULL_HANDLE, SortedPayload = VK_NULL_HANDLE;
                RetrieveRadixSortedBuffers(Extension.TlasSort, SortedKey, SortedPayload); // the RESULT pair — never the primary buffers by name

                // Arena node/slice descriptor ranges use VK_WHOLE_SIZE: the host word arrays are released after UploadGeometryArena, so no host byte
                // count survives — this matches the trace exe's descriptor write. SliceCount is the arena's slice count, NOT the instance count.
                const bool Bound =
                    BindInstanceBoundsScene(Extension.TlasBounds,
                                            Extension.VisibilityRaster.InstanceBuffer, InstanceBytes,
                                            ArenaSlice, VK_WHOLE_SIZE, ArenaNode, VK_WHOLE_SIZE,
                                            TlasInstanceCount, TlasSliceCount)
                  && BindInstanceMortonTarget(Extension.TlasBounds, MortonKey, KeyBytes, MortonPayload, KeyBytes)
                  && SetRadixSortKeyCount(Extension.TlasSort, TlasInstanceCount)
                  && BindInstanceTreeSorted(Extension.TlasTree, SortedKey, KeyBytes, SortedPayload, KeyBytes, TlasInstanceCount)
                  && BindInstanceTreeScene(Extension.TlasTree,
                                           Extension.VisibilityRaster.InstanceBuffer, InstanceBytes,
                                           ArenaSlice, VK_WHOLE_SIZE, ArenaNode, VK_WHOLE_SIZE,
                                           SortedPayload, KeyBytes, TlasSliceCount);

                Extension.TlasReady = Bound;
                if (Bound)
                    ISSUE_NOTICE("render-extension", "TLAS chain wired: %u instances, %u slices", TlasInstanceCount, TlasSliceCount);
                else
                    ISSUE_CAUTION("render-extension", "TLAS bind failed — scene rasters, no top-level built");

                // -- Phase 2: the per-surfel INTEGRATE (trace + MSME). The awaited consumer of the #26 tree-node barrier. Two descriptor sets — set 0 is
                //    the BVH (the exact seven buffers the TLAS just bound: instances + arena slice/node/primitive + tree node + merged index/vertex),
                //    set 1 is the surfel state. Scene is static after load, so — like the TLAS — bind ONCE here and Record-only per frame (re-binding an
                //    in-flight set is undefined). Gated on the whole chain being live; best-effort, so a failure just leaves ReadyCondition false and the
                //    per-frame record no-ops. Feeds nothing on screen yet — Phase 3 gathers it at SurfaceShade.frag.
                InitializeSurfelIntegrateSubmission(Extension.SurfelIntegrate, Extension.Substrate.Host, FRONTIER_SURFEL_SHADER_DIR);
                if (Extension.SurfelIntegrate.ReadyCondition && Extension.TlasReady
                    && Extension.SurfelPoolResource.ReadyCondition && Extension.SurfelSlotting.ReadyCondition)
                {
                    VkBuffer TreeNode = VK_NULL_HANDLE, TreeParent = VK_NULL_HANDLE;
                    RetrieveInstanceTreeBuffers(Extension.TlasTree, TreeNode, TreeParent);

                    RefreshSurfelIntegrateBindings(Extension.SurfelIntegrate,
                                                   Extension.SurfelPoolResource, Extension.SurfelSlotting,
                                                   Extension.VisibilityRaster.InstanceBuffer, ArenaSlice,
                                                   ArenaNode, ArenaPrimitive, TreeNode,
                                                   Extension.SceneGeometry.IndexBuffer, Extension.SceneGeometry.VertexBuffer);
                    ISSUE_NOTICE("render-extension", "surfel integrate wired against the TLAS");

                    // -- Primary sun shadow: point the SHADE's BVH set (set 2) at the SAME four acceleration buffers the integrate trace reads. The
                    //    shade REUSES set 0's instance SSBO + merged vertex/index streams (its Refresh already pointed those), so only Slices / arena
                    //    node / arena primitive / tree node are wired here. Bound ONCE for the same static-scene reason as the integrate. Best-effort: a
                    //    no-op until the shade's set-2 layout exists, leaving ShadowSetReady false so the shade record forces the unshadowed path.
                    RefreshSurfaceShadeBvhBindings(Extension.SurfaceShade,
                                                   ArenaSlice, ArenaNode, ArenaPrimitive, TreeNode);
                    ISSUE_NOTICE("render-extension", "surface-shade sun-shadow BVH wired against the TLAS");
                }
            }
        }
    }

    // -- GPU wall-clock probe (measure-first). Best-effort: a device without graphics-queue timestamps leaves PassTiming.ReadyCondition false and every
    //    Begin/End/Collect no-ops, so the unmeasured path runs byte-identically. Its own ISSUE_NOTICE reports whether timing came up.
    InitializeGpuTimestampScope(Extension.PassTiming, Extension.Substrate.Host);

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

    // -- ImGui debug overlay (surfel tuning window, F10) ----------------------------------------------------------------
    // 📝 Mirror the canonical live ImGui-on-Vulkan bring-up every editor host uses (SketchModelViewportHost.cpp:134-162), with the ONE
    //    difference that the substrate renders with DYNAMIC RENDERING, so the backend gets a PipelineRenderingCreateInfo (colour format =
    //    the swapchain format) instead of a RenderPass. Best-effort: any failure leaves ImguiReady=false and every per-frame ImGui call
    //    no-ops, so the renderer still presents. The window is drawn into the substrate's OWN command buffer (RecordSequence tail).
    {
        const VulkanHost& Host = Extension.Substrate.Host;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& Io = ImGui::GetIO();
        Io.IniFilename = nullptr;   // 🔴 no imgui.ini — a renderer window must not persist layout to disk

        AttachImguiPlatform(Extension.Substrate.Window);

        const uint32_t SwapImageCount = (uint32_t)Extension.Substrate.Images.size();

        // 🔴 Dynamic-rendering path: the backend needs the colour attachment format up front (no RenderPass). Keep the format struct alive
        //    for the duration of the Init call — the backend copies it, so a stack local is safe here.
        VkPipelineRenderingCreateInfoKHR RenderingCreateInfo = {};
        RenderingCreateInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
        RenderingCreateInfo.colorAttachmentCount    = 1;
        RenderingCreateInfo.pColorAttachmentFormats = &Extension.Substrate.SurfaceFormat;

        ImGui_ImplVulkan_InitInfo InitInfo = {};
        InitInfo.ApiVersion                = Host.ApiVersion;
        InitInfo.Instance                  = Host.Instance;
        InitInfo.PhysicalDevice            = Host.PhysicalDevice;
        InitInfo.Device                    = Host.Device;
        InitInfo.QueueFamily               = Host.GraphicsQueueFamily;
        InitInfo.Queue                     = Host.GraphicsQueue;
        InitInfo.DescriptorPool            = Host.ImguiDescriptorPool;
        InitInfo.MinImageCount             = SwapImageCount;
        InitInfo.ImageCount                = SwapImageCount;
        InitInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        // 🔴 This vendored ImGui (post-2025/09/26) carries the dynamic-rendering create-info INSIDE PipelineInfoMain, guarded by
        //    IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING — NOT the old top-level InitInfo.PipelineRenderingCreateInfo the editor hosts used.
        InitInfo.UseDynamicRendering       = true;
        InitInfo.PipelineInfoMain.PipelineRenderingCreateInfo = RenderingCreateInfo;
        InitInfo.Allocator                 = Host.Allocator;

        if (Host.ImguiDescriptorPool != VK_NULL_HANDLE && ImGui_ImplVulkan_Init(&InitInfo))
        {
            // 🔴 Match ControlsGalleryHost EXACTLY: resolve the shared theme and enforce it — NOTHING else. No StyleColorsDark seed, no
            //    ScaleAllSizes. The ControlsGallery look does NOT come from the global ImGui style; it comes from drawing every widget through
            //    the real Interface components (ConstructValueSlider / ConstructSelectionEntry / BeginPropertyCard …), each of which pushes the
            //    Theme.Palette colours per-widget. So the window draws through those components (see SurfelTuningWindow.cpp) rather than raw
            //    ImGui::SliderFloat, and the resolved Theme is cached on the extension to thread into that draw call each frame.
            Extension.ImguiTheme = ResolveActiveTheme();
            EnforceThemeStyle(Extension.ImguiTheme);
            Extension.ImguiReady = true;

            // 📝 Seed the tuning knobs from the baked SurfelGrid.glsl defaults so opening the window shows the values in force right now.
            Extension.SurfelTuning.CellDiameter      = 1.0f;
            Extension.SurfelTuning.BaseRadius        = 1.2f;
            Extension.SurfelTuning.NearFieldBias     = 1.0f;
            Extension.SurfelTuning.PerCellCapChoice  = 64;
            Extension.SurfelTuning.PerCellCapApplied = 64;
            ISSUE_NOTICE("render-extension", "ImGui surfel-tuning overlay ready (F10)");
        }
        else
        {
            // ⚠️ Leave ImguiReady=false and tear the half-built context back down so finalize is a clean no-op.
            DetachImguiPlatform(Extension.Substrate.Window);
            ImGui::DestroyContext();
            ISSUE_CAUTION("render-extension", "ImGui overlay unavailable — surfel-tuning window disabled (renderer runs without it)");
        }
    }

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
            // 📝 Start the ImGui frame at the very top of the preamble — it does NO GPU work (no command-buffer touch), it only opens the UI
            //    frame so widget calls later in RecordSequence have somewhere to land. The platform advance (mouse/focus/cursor) sits between the
            //    backend new-frame and ImGui::NewFrame exactly as the editor hosts order it. Gated on ImguiReady so a failed init is inert.
            if (Extension.ImguiReady)
            {
                ImGui_ImplVulkan_NewFrame();
                AdvanceImguiPlatform();
                ImGui::NewFrame();
            }

            // 📊 GPU-timing frame boundary. Collect FIRST — it reads the ring that trails this frame's (the GPU has retired it), so the numbers are one
            //    frame late but never stall the CPU. BeginFrame THEN advances to this frame's ring and resets it, so the brackets below write into a clean
            //    span. Both no-op when the device can't timestamp the graphics queue, leaving the unmeasured path byte-identical. The console readout is
            //    throttled to once every 120 frames so it narrates the breakdown without flooding the notice log.
            CollectGpuTimestampResults(Extension.PassTiming);
            BeginGpuTimestampFrame(Extension.PassTiming, CommandBuffer);
            if (Extension.PassTiming.ReadyCondition && (++Extension.PassReportFrame % 120u) == 0u)
            {
                const float* Millis = Extension.PassTiming.ResolvedMillis;
                ISSUE_NOTICE("surfel-timing",
                             "GPU ms  slot %.3f  spawn %.3f  age %.3f  integrate %.3f  shade %.3f  splat %.3f",
                             Millis[RenderExtension::SurfelPassSlotSlotting],
                             Millis[RenderExtension::SurfelPassSlotSpawn],
                             Millis[RenderExtension::SurfelPassSlotAge],
                             Millis[RenderExtension::SurfelPassSlotIntegrate],
                             Millis[RenderExtension::SurfelPassSlotShade],
                             Millis[RenderExtension::SurfelPassSlotDebugSplat]);
            }

            // 🩺 Hand the just-collected GPU millis to the census so they land on the SAME CSV row as this frame's population counts.
            // 🔴 THIS MUST SIT BETWEEN THE TWO COLLECTS, AND THAT IS THE WHOLE REASON THE MS COLUMNS MEAN ANYTHING. Both facilities run a 3-deep ring
            //    and both read the slot trailing the one being recorded, so ResolvedMillis and the census row describe the same frame ONLY at this
            //    point — after CollectGpuTimestampResults filled it, before CollectSurfelCensusRow consumes it. Move this above the timing collect and
            //    every ms column lags its counts by a frame; move it below the census collect and it lags by a frame the other way. Neither shows up
            //    as an error, and at steady state neither even looks wrong.
            SupplySurfelCensusTimings(Extension.SurfelCensus, Extension.PassTiming.ResolvedMillis,
                                      RenderExtension::SurfelPassSlotCount);

            // 🩺 Census frame boundary, mirroring the timing collect directly above and for the identical reason: read the ring slot that TRAILS this
            //    frame's, so the row is one frame late but the CPU never waits on the GPU. Appends one CSV row per frame while recording; a no-op otherwise.
            CollectSurfelCensusRow(Extension.SurfelCensus);

            // 🩺 Auto-armed traces close themselves after FRONTIER_SURFEL_CENSUS_FRAMES rows (default 600 — past TTL=500, so a cohort seeded at frame
            //    zero has had time to die of old age and show up in diedTtl) and then request exit. Closing here rather than at process teardown is
            //    what makes the CSV trustworthy: the file is fclosed on a row boundary instead of being truncated mid-write by a kill.
            if (Extension.SurfelCensus.Recording && Extension.SurfelCensusAutoFrames > 0
                && Extension.SurfelCensus.RowsWritten >= Extension.SurfelCensusAutoFrames)
            {
                const std::string ClosedPath = Extension.SurfelCensus.OutputPath;
                const uint32_t    Rows       = EndSurfelCensusRecording(Extension.SurfelCensus);
                printf("[surfel] census complete — %u frames -> %s\n", Rows, ClosedPath.c_str());
                fflush(stdout);
                Extension.Substrate.Window.CloseRequested = true;
            }

            // 🔴 Per-cell cap commit (F10 Apply). Consumed at the TOP of the preamble, before any surfel dispatch this frame, so the spawn gate + the
            //    occupancy heatmap read a stable cap for the whole frame. Because the List SSBO is allocated once at SurfelMaxPerCellAllocationCap (256),
            //    raising the cap can NEVER over-run it — so this is a pure uniform commit (choice -> applied), NOT a reallocation: no vkDeviceWaitIdle, no
            //    descriptor rewrite, no buffer rebuild. The new applied value flows into SurfelSpawnConstants.PerCellCap / SurfelDebugConstants.PerCellCap
            //    below via AssembleSurfel*Constants (both read Tuning.PerCellCapApplied). One-shot: the request is cleared so a held state does not re-fire.
            if (Extension.SurfelTuning.ApplyCapRequested)
            {
                Extension.SurfelTuning.PerCellCapApplied = Extension.SurfelTuning.PerCellCapChoice;
                Extension.SurfelTuning.ApplyCapRequested = false;
                printf("[surfel] per-cell cap applied -> %d\n", Extension.SurfelTuning.PerCellCapApplied);
            }

            // ☀️ Sun source (F10 Sun card override). Rewrite the atmosphere profile's solar vector from the tuning elevation/azimuth, then re-upload ONLY
            //    when it actually moved — UpdateSkyAtmosphereProfile re-writes the UBO mapping + sets SunDirtyCondition (a sky-view re-bake), so a static
            //    sun costs nothing. The sky, the surfel integrate, and the direct shade all read Extension.SkyPass.Profile, so one write drives all three.
            {
                float PriorSolar[3] = { Extension.SkyPass.Profile.SolarDirection[0],
                                        Extension.SkyPass.Profile.SolarDirection[1],
                                        Extension.SkyPass.Profile.SolarDirection[2] };
                AtmosphereUniformBlock TunedProfile = Extension.SkyPass.Profile;
                Atmosphere::AssignSolarDirection(TunedProfile, Extension.SurfelTuning.SunElevation, Extension.SurfelTuning.SunAzimuth);
                const bool SunMoved = TunedProfile.SolarDirection[0] != PriorSolar[0]
                                   || TunedProfile.SolarDirection[1] != PriorSolar[1]
                                   || TunedProfile.SolarDirection[2] != PriorSolar[2];
                if (SunMoved)
                    UpdateSkyAtmosphereProfile(Extension.SkyPass, TunedProfile);
            }

            // 🔴 The observer is CACHED here for RecordSequence to reuse rather than each recomputing it. The preamble runs first and the sequence
            //    second within ONE command buffer, but the sequence drives the camera before it advances the GI field — so recomputing there would
            //    hand the two spines observers a frame of camera motion apart, and the GI field would disagree with itself about where the viewer
            //    is. One evaluation, one cache, both read it.
            //
            // ⚠️ Deliberately NOT moving DriveViewportCamera ahead of the preamble. The visibility raster below depends on using this frame's
            //    NOT-yet-advanced camera — the shade reconstructs geometry from the id buffer that raster wrote, so it must read the camera that
            //    wrote it. Advancing the camera first would invert that invariant and perturb the settled P5.9b image.
            Extension.CachedObserverPosition = EvaluateObserverPosition(Extension.ViewCamera);
            Extension.ObserverCacheSeeded    = true;

            // 🩺 Surfel dump (L key). Consumed HERE, at the very TOP of the preamble — before this frame records any surfel work — precisely because the
            //    surfel + spawn-request buffers now hold the PREVIOUS frame's fully-submitted-and-completed state (the frame the user was looking at when
            //    they pressed L). The facility runs its OWN transient command buffer + submit + fence wait (a one-off stall the keypress pays for), so it
            //    must fire outside the live command-buffer recording that has not begun its surfel dispatches yet. Reading here avoids racing this frame's
            //    not-yet-recorded spawn writes. One-shot: cleared immediately so a held key does not re-fire.
            if (Extension.SurfelDumpRequested)
            {
                Extension.SurfelDumpRequested = false;
                if (Extension.UploadPool == VK_NULL_HANDLE)
                    printf("[surfel] dump skipped — no command pool (scene not loaded?)\n");
                else
                {
                    const FocalOrientation DumpFrame = SolveOrbitOrientation(Extension.ViewCamera);
                    const float DumpEye[3] = { DumpFrame.EyePosition.XCoord, DumpFrame.EyePosition.YCoord, DumpFrame.EyePosition.ZCoord };
                    // Each press names a distinct numbered snapshot set under SurfelDumps/ (beside the exe) so presses accumulate.
                    DumpSurfelStateToDisk(Extension.SurfelPoolResource,
                                          Extension.SurfelLifecycle.TileAllocBuffer,
                                          Extension.SurfelLifecycle.TileCandidateBuffer,
                                          Extension.SurfelLifecycle.TileCapacity,
                                          Extension.UploadPool,
                                          DumpEye,
                                          Extension.SurfelFrameIndex,
                                          Extension.SurfelDumpSequence++,
                                          "SurfelDumps");
                }
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

            // The debug splat's fragment stage depth-rejects against that same scene depth (b3), so the resize that rebuilt the depth view left its
            // descriptor pointing at a stale handle — re-point it here beside the grid, for the same reason: idempotent (a single handle compare when
            // nothing moved) and also the first valid binding after a genuine extent change. Harmless when the inscription never built.
            if (Extension.SurfelDebug.ReadyCondition)
                RefreshSurfelDebugInscription(Extension.SurfelDebug, Extension.SurfelPoolResource, Extension.SurfelSlotting,
                                              Extension.DepthTarget.DepthView);

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
                // 📝 Since the merge, the floor's geometry pair IS the merged SceneGeometry pair, so b5/b6 and b1/b2 carry the same two handles and the
                //    same two capacities. Handing them back from Bound* rather than re-deriving them keeps that an observation about this frame's
                //    state instead of an assumption baked into the resize path.
                RefreshSurfaceShadeInscription(Extension.SurfaceShade, Extension.VisibilityTarget,
                                               Extension.SurfaceShade.BoundVertexBuffer,   Extension.SceneGeometry.VertexByteCapacity,
                                               Extension.SurfaceShade.BoundIndexBuffer,    Extension.SceneGeometry.IndexByteCapacity,
                                               Extension.SurfaceShade.BoundInstanceBuffer,
                                               (VkDeviceSize)Extension.VisibilityRaster.InstanceCount * sizeof(SuzanneSceneInstance),
                                               Extension.SurfaceShade.FloorGeometryBound ? Extension.SurfaceShade.BoundFloorVertexBuffer : VK_NULL_HANDLE,
                                               Extension.SceneGeometry.VertexByteCapacity,
                                               Extension.SurfaceShade.FloorGeometryBound ? Extension.SurfaceShade.BoundFloorIndexBuffer : VK_NULL_HANDLE,
                                               Extension.SceneGeometry.IndexByteCapacity,
                                               Extension.SurfaceShade.FloorGeometryBound ? Extension.SurfaceShade.BoundFloorInstanceBuffer : VK_NULL_HANDLE,
                                               (VkDeviceSize)Extension.FloorRaster.InstanceCount * sizeof(SuzanneSceneInstance));

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
            // 📝 The floor now draws out of the SHARED merged buffer as a sub-range rather than out of its own allocation, so readiness is the presence
            //    of its placement rather than of a second buffer. The range is read once here and handed to each of the four draw sites below.
            const GeometryStreamPlacement FloorDrawPlacement =
                RetrieveGeometryStreamPlacement(Extension.SceneStreams, Extension.FloorMeshOrdinal);
            // 🔴 The heads need a sub-range for the same reason: SceneGeometry.IndexCount is the WHOLE merged run now, so a plain head draw that took
            //    it would render the floor slab once per head instance, each carrying that head's transform.
            const GeometryStreamPlacement HeadDrawPlacement =
                RetrieveGeometryStreamPlacement(Extension.SceneStreams, Extension.HeadMeshOrdinal);
            const bool FloorReady = Extension.FloorRaster.ReadyCondition
                                 && Extension.FloorRaster.InstanceCount > 0
                                 && Extension.FloorStreamPresent
                                 && FloorDrawPlacement.IndexCount > 0;

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
                // 🔴 The HEADS' triangle count, not the merged buffer's. The compute raster walks TriangleCount triangles per instance, so the merged
                //    total would march every head instance across the floor slab's triangles as well.
                SoftwareConstants.TriangleCount   = HeadDrawPlacement.IndexCount / 3u;
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
                    DrawVisibilityMesh(Extension.FloorRaster, Extension.FloorRaster.InstanceSet, Extension.SceneGeometry,
                                       Extension.FloorRaster.InstanceCount, FloorConstants, false, VK_NULL_HANDLE, CommandBuffer,
                                       FloorDrawPlacement.IndexOffset, FloorDrawPlacement.IndexCount);
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
                    DrawVisibilityMesh(Extension.FloorRaster, Extension.FloorRaster.InstanceSet, Extension.SceneGeometry,
                                       Extension.FloorRaster.InstanceCount, FloorConstants, false, VK_NULL_HANDLE, CommandBuffer,
                                       FloorDrawPlacement.IndexOffset, FloorDrawPlacement.IndexCount);
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
                                   Extension.VisibilityRaster.InstanceCount, RasterConstants, false, VK_NULL_HANDLE, CommandBuffer,
                                   HeadDrawPlacement.IndexOffset, HeadDrawPlacement.IndexCount);
                if (FloorReady)
                    DrawVisibilityMesh(Extension.FloorRaster, Extension.FloorRaster.InstanceSet, Extension.SceneGeometry,
                                       Extension.FloorRaster.InstanceCount, RasterConstants, false, VK_NULL_HANDLE, CommandBuffer,
                                       FloorDrawPlacement.IndexOffset, FloorDrawPlacement.IndexCount);
                EndVisibilityScope(Extension.VisibilityRaster, Extension.VisibilityTarget, Extension.DepthTarget, CommandBuffer);
            }
            else if (FloorReady)
            {
                // No heads this frame, but the floor is a real mesh: draw it alone into the cleared buffer so the ground still shows + seeds the pyramid.
                VisibilityRasterConstants FloorConstants;
                AssembleVisibilityConstants(Extension.ViewCamera, FloorConstants);
                BeginVisibilityScope(Extension.FloorRaster, Extension.VisibilityTarget, Extension.DepthTarget, CommandBuffer);
                DrawVisibilityMesh(Extension.FloorRaster, Extension.FloorRaster.InstanceSet, Extension.SceneGeometry,
                                   Extension.FloorRaster.InstanceCount, FloorConstants, false, VK_NULL_HANDLE, CommandBuffer,
                                   FloorDrawPlacement.IndexOffset, FloorDrawPlacement.IndexCount);
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
            //  TOP-LEVEL ACCELERATION STRUCTURE (TLAS) — per-frame GPU build over the scene instances. NO trace yet; this stands up the two-level
            //  BVH the Phase-2 surfel trace will walk, and exposes its node buffer at the seam below.
            // ================================================================================================================================
            // 📝 Recorded in the SAME outside-every-scope compute region as the surfels (compute is illegal inside the radiance scope that opens below).
            //    Independent of the surfel block — it reads the instance array + arena, NOT the visibility image — so its order vs the surfels is free
            //    and it gates on TlasReady, NOT VisibilityWritten (it can rebuild even on an idle frame). The five records go in EXACT order with NO
            //    caller barriers between them: each submission inserts its own inter-dispatch barriers, and the device-side reseeds (bounds accumulator,
            //    tree parent table, refit counters) live inside these Record calls and run every frame — they must NOT be hoisted.
            if (Extension.TlasReady)
            {
                RecordInstanceBoundsReduce(Extension.TlasBounds, CommandBuffer);
                RecordInstanceMortonCode  (Extension.TlasBounds, CommandBuffer);
                RecordRadixSort           (Extension.TlasSort,   CommandBuffer);
                RecordInstanceTreeBuild   (Extension.TlasTree,   CommandBuffer);
                RecordInstanceTreeRefit   (Extension.TlasTree,   CommandBuffer);

                // Fence the refit's node-buffer writes (compute) for the future surfel trace's read (compute). Harmless with no consumer yet — a
                // no-cost fence — but it future-proofs the seam so Phase 2 only adds its descriptor write + dispatch, and makes the live recording
                // structurally identical to the TwoLevelTraceValidation reference. The destination consumer is the Phase-2 surfel trace.
                VkBuffer TlasNodeBuffer = VK_NULL_HANDLE, TlasParentBuffer = VK_NULL_HANDLE;
                RetrieveInstanceTreeBuffers(Extension.TlasTree, TlasNodeBuffer, TlasParentBuffer);
                if (TlasNodeBuffer != VK_NULL_HANDLE)
                {
                    VkBufferMemoryBarrier TlasToConsumer = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
                    TlasToConsumer.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
                    TlasToConsumer.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
                    TlasToConsumer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    TlasToConsumer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    TlasToConsumer.buffer              = TlasNodeBuffer;
                    TlasToConsumer.offset              = 0;
                    TlasToConsumer.size                = VK_WHOLE_SIZE;
                    vkCmdPipelineBarrier(CommandBuffer,
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                         0, 0, nullptr, 1, &TlasToConsumer, 0, nullptr);
                }
            }

            // ================================================================================================================================
            //  SURFEL GI — Phase 1 per-frame compute (pool lifecycle + hash-grid slotting). NO tracing / GI on screen; this maintains the surfel
            //  substrate and the debug splat reads it below.
            // ================================================================================================================================
            // 📝 THE ONE LEGAL SEAM: compute is illegal inside a dynamic-rendering scope, and the radiance scope opens right below. This spot — after
            //    the visibility image is handed to sampling (the spawn texelFetches its id) and before any scope opens — is the only place inside the
            //    command buffer and outside every scope, the same seam the shade's radiance scope needs.
            // 🔴 ORDER IS THE LIFECYCLE CONTRACT: Prepare (one-time seed, F21 — no-ops after frame 0) MUST precede the first slotting; slotting builds
            //    the grid the spawn reads, so spawn is AFTER slotting; Age (+1 / TTL recycle) is after spawn. Each unit inserts its own F3 barriers;
            //    the trailing barrier below fences the finished surfel + offsets writes for the debug splat's vertex-stage read in the radiance scope.
            // ⚠️ Gated on VisibilityWritten for the same reason the shade is: the spawn reconstructs world pos/normal from the id buffer, and on an idle
            //    frame that buffer holds undefined bytes — spawning from them would seed surfels out of stale memory.
            if (VisibilityWritten && Extension.SurfelLifecycle.ReadyCondition && Extension.SurfelSlotting.ReadyCondition
                && Extension.SurfelPoolResource.ReadyCondition)
            {
                RecordSurfelLifecyclePrepare(Extension.SurfelLifecycle, Extension.SurfelPoolResource, CommandBuffer);

                // 🩺 Zero the census tallies BEFORE any pass that counts into them. Spawn/Allocate (births) and Age (deaths) both atomicAdd here, so a
                //    missed clear turns every CSV row into a running total instead of a per-frame flow — and a running total still looks like data.
                BeginSurfelCensusFrame(Extension.SurfelCensus, CommandBuffer);

                SurfelSlottingConstants SlottingConstants;
                AssembleSurfelSlottingConstants(Extension.ViewCamera, Extension.SurfelTuning, SlottingConstants);
                BeginGpuTimestampScope(Extension.PassTiming, CommandBuffer, RenderExtension::SurfelPassSlotSlotting);
                RecordSurfelGridSlotting(Extension.SurfelSlotting, Extension.SurfelPoolResource, SlottingConstants, CommandBuffer);
                EndGpuTimestampScope(Extension.PassTiming, CommandBuffer, RenderExtension::SurfelPassSlotSlotting);

                const bool FloorResident = Extension.SurfaceShade.FloorGeometryBound && FloorDrawPlacement.IndexCount > 0;
                SurfelSpawnConstants SpawnConstants;
                AssembleSurfelSpawnConstants(Extension.ViewCamera, Extension.VisibilityTarget.Width
                                             ? VkExtent2D{ Extension.VisibilityTarget.Width, Extension.VisibilityTarget.Height } : Extent,
                                             Extension.SurfelFrameIndex, FloorResident, FloorDrawPlacement.IndexOffset,
                                             Extension.SurfelSpawnDensityScale, Extension.SurfelTuning, SpawnConstants);
                // The SCREEN-TILE spawn election is the one spawn front-end: <=1 probe per 8x8 pixel tile, reconstructed from the visibility buffer. A
                // surface micro-raster arm (one claim per grid cell, so density followed world area rather than the projection) was built alongside this
                // as an A/B and REMOVED — it never placed better than the election it was meant to beat, and it carried a whole parallel spawn path
                // (claim ledger, request list, indirect commit) to do it.
                BeginGpuTimestampScope(Extension.PassTiming, CommandBuffer, RenderExtension::SurfelPassSlotSpawn);
                RecordSurfelLifecycleSpawn(Extension.SurfelLifecycle, Extension.SurfelPoolResource, SpawnConstants,
                                           VkExtent2D{ Extension.VisibilityTarget.Width, Extension.VisibilityTarget.Height }, CommandBuffer);
                EndGpuTimestampScope(Extension.PassTiming, CommandBuffer, RenderExtension::SurfelPassSlotSpawn);

                // Age reads the touched income mailbox + hashes each surfel's cell for the crowding rent, so it needs THIS frame's grid origin — the
                // same camera-relative eye position the slotting/spawn used above (SlottingConstants.GridOrigin), keeping host and shader on one lattice.
                BeginGpuTimestampScope(Extension.PassTiming, CommandBuffer, RenderExtension::SurfelPassSlotAge);
                RecordSurfelLifecycleAge(Extension.SurfelLifecycle, Extension.SurfelPoolResource, SlottingConstants.GridOrigin,
                                         SpawnConstants, CommandBuffer);
                EndGpuTimestampScope(Extension.PassTiming, CommandBuffer, RenderExtension::SurfelPassSlotAge);

                // 🩺 Snapshot the census into this frame's ring slot. HERE, after Age, because Age is the LAST pass that tallies (Allocate — the birth
                //    side — runs inside RecordSurfelLifecycleSpawn as its stage 2, so it is already complete). Records copies only; the host reads the
                //    trailing ring slot next frame, so nothing here waits on the GPU.
                RecordSurfelCensusCopy(Extension.SurfelCensus, Extension.SurfelPoolResource, CommandBuffer, Extension.SurfelFrameIndex);

                // ── Phase 2: the per-surfel INTEGRATE (trace + MSME). Runs AFTER Age (so this frame's ages are settled) and AFTER the #26 TLAS chain (it
                //    walks the tree the refit just wrote — B2 above already fenced the tree node buffer). B1 fences slotting's grid + the pool/moments-read
                //    writes for integrate's read; B3 fences integrate's moments-write / guiding / depth / touched for next frame's readers; then the swap
                //    flips parity so the write half becomes readable. The swap is LAST (Phase 3 inserts Resolve BEFORE it and changes nothing else).
                //    🔴 Do NOT try to make this frame's Age see this frame's touched — Age ran above, so integrate's touched is a NEXT-frame input (the
                //       deliberate one-frame skew F10); the frame fence carries that, and B3 makes the writes visible within the frame.
                if (Extension.TlasReady && Extension.SurfelIntegrate.ReadyCondition && Extension.SurfelPoolResource.ReadyCondition)
                {
                    // B1 — slotting/pool/moments-read (compute WRITE) → integrate (compute READ). The Phase-1 trailing barrier below is COMPUTE→VERTEX for
                    // the splat; it does NOT cover COMPUTE→COMPUTE, so integrate needs its own.
                    VkMemoryBarrier GridToIntegrate = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
                    GridToIntegrate.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    GridToIntegrate.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    vkCmdPipelineBarrier(CommandBuffer,
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                         0, 1, &GridToIntegrate, 0, nullptr, 0, nullptr);

                    // The eye is the camera-relative grid origin (same as slotting/spawn); the sun is the SAME Z-up solar direction the shade + shadow
                    // clipmap use, its colour the white-calibrated solar illuminance. The sky ground/zenith are a deterministic constant stand-in (§conflict
                    // 2) — no env texture is bound; the clipmap probe replaces this miss path at Phase 5.
                    const FocalOrientation IntegrateFrame = SolveOrbitOrientation(Extension.ViewCamera);
                    const float IntegrateEye[3] = { IntegrateFrame.EyePosition.XCoord, IntegrateFrame.EyePosition.YCoord, IntegrateFrame.EyePosition.ZCoord };

                    float IntegrateSunX = 0.0f, IntegrateSunY = 0.0f, IntegrateSunZ = 1.0f;
                    Atmosphere::ResolveSolarDirectionSceneFrame(Extension.SkyPass.Profile, IntegrateSunX, IntegrateSunY, IntegrateSunZ);
                    const float IntegrateSunDir[3]    = { IntegrateSunX, IntegrateSunY, IntegrateSunZ };
                    const float IntegrateSunColour[3] = { Extension.SkyPass.Profile.SolarIlluminance[0],
                                                          Extension.SkyPass.Profile.SolarIlluminance[1],
                                                          Extension.SkyPass.Profile.SolarIlluminance[2] };
                    const float IntegrateSkyGround[3] = { 0.15f, 0.16f, 0.18f };   // constant sky-ambient stand-in (§conflict 2), replaced at Phase 5
                    const float IntegrateSkyZenith[3] = { 0.30f, 0.42f, 0.60f };
                    const float IntegrateSkyIntensity = 1.0f;

                    SurfelIntegrateConstants IntegrateConstants =
                        AssembleSurfelIntegrateConstants(Extension.SurfelPoolResource,
                                                         IntegrateEye, IntegrateEye,
                                                         IntegrateSunDir, IntegrateSunColour,
                                                         IntegrateSkyGround, IntegrateSkyZenith, IntegrateSkyIntensity,
                                                         Extension.SurfelFrameIndex,
                                                         Extension.VisibilityRaster.InstanceCount,
                                                         (uint32_t)Extension.GeometryArena.Slices.size());
                    // Live world-scale knobs (F10 window) — the integrate's one-bounce gather runs SurfelGather.glsl, so its cell/radius must
                    // match the spawn+slotting build. Stamped here (not in the Surfel-module assembler) to keep SurfelTuningState a RenderExtension type.
                    IntegrateConstants.TuneCellDiameter  = Extension.SurfelTuning.CellDiameter;
                    IntegrateConstants.TuneBaseRadius    = Extension.SurfelTuning.BaseRadius;
                    IntegrateConstants.TuneNearFieldBias = Extension.SurfelTuning.NearFieldBias;
                    BeginGpuTimestampScope(Extension.PassTiming, CommandBuffer, RenderExtension::SurfelPassSlotIntegrate);
                    RecordSurfelIntegrate(Extension.SurfelIntegrate, Extension.SurfelPoolResource, IntegrateConstants, CommandBuffer);
                    EndGpuTimestampScope(Extension.PassTiming, CommandBuffer, RenderExtension::SurfelPassSlotIntegrate);

                    // B3 — integrate (compute WRITE) → next frame's integrate/resolve/Age (compute READ). Fences moments-write / guiding / depth / touched.
                    VkMemoryBarrier IntegrateToReaders = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
                    IntegrateToReaders.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    IntegrateToReaders.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    vkCmdPipelineBarrier(CommandBuffer,
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                         0, 1, &IntegrateToReaders, 0, nullptr, 0, nullptr);

                    // Swap LAST: flip the moments parity so the write half integrate just filled becomes the read half next frame's MSME sees.
                    SwapSurfelMoments(Extension.SurfelPoolResource);
                }

                // Fence the surfel + Offsets + moments writes (compute) for the two graphics-stage readers in the radiance scope: the debug splat's
                // VERTEX-stage storage read AND (Phase 3) the SurfaceShade FRAGMENT-stage GI gather. The units' internal barriers cover COMPUTE->COMPUTE
                // only; both cross-stage visibilities are this pass's to add, so dstStageMask carries VERTEX | FRAGMENT (one barrier serves both).
                VkMemoryBarrier SurfelToSplat = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
                SurfelToSplat.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                SurfelToSplat.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(CommandBuffer,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     0, 1, &SurfelToSplat, 0, nullptr, 0, nullptr);

                Extension.SurfelFrameIndex++;
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
                // 📝 The floor's triangle run is a sub-range of the merged buffer now, so "the floor has geometry" is its placement's IndexCount, not an
                //    allocation's. Reading SceneGeometry.IndexCount here would be true whenever the HEADS loaded and would enable the floor shade over a
                //    scene with no floor in it.
                const bool FloorShadeable = Extension.SurfaceShade.FloorGeometryBound
                                         && Extension.FloorRaster.InstanceCount > 0
                                         && FloorDrawPlacement.IndexCount > 0;
                if (Extension.SurfaceShadeEnabled && Extension.SurfaceShade.ReadyCondition && VisibilityWritten
                    && (HeadsShadeable || FloorShadeable))
                {
                    // The same Z-up sun the shadow clipmap was scrolled against this image, so shading and shadowing cannot disagree.
                    float ShadeSunX = 0.0f, ShadeSunY = 0.0f, ShadeSunZ = 1.0f;
                    Atmosphere::ResolveSolarDirectionSceneFrame(Extension.SkyPass.Profile, ShadeSunX, ShadeSunY, ShadeSunZ);

                    SurfaceShadeConstants ShadeConstants;
                    AssembleSurfaceShadeConstants(Extension.ViewCamera, Extension.CompositeFeatureMask,
                                                  Vector3f{ ShadeSunX, ShadeSunY, ShadeSunZ }, ShadeConstants);

                    // ☀️ Key-light radiance from the F10 Sun card: colour × intensity, PREMULTIPLIED here so the frag reads one vec3 (SunRadiance) with no
                    //    per-pixel multiply. The sky keeps its own SolarIlluminance calibration (independent scale, see AtmosphereProfile.h) — this is only
                    //    the deferred shade's key light, the one the retired hardcoded LightColour*LightIntensity used to carry.
                    ShadeConstants.SunRadiance[0] = Extension.SurfelTuning.SunColour[0] * Extension.SurfelTuning.SunIntensity;
                    ShadeConstants.SunRadiance[1] = Extension.SurfelTuning.SunColour[1] * Extension.SurfelTuning.SunIntensity;
                    ShadeConstants.SunRadiance[2] = Extension.SurfelTuning.SunColour[2] * Extension.SurfelTuning.SunIntensity;
                    ShadeConstants.SunRadiance[3] = 0.0f;

                    // 🔴 Sourced from the inscription's own record of what b5-b7 hold, never from whether a floor document loaded: when the floor is
                    //    absent those bindings are aliased onto the HEAD buffers, so enabling the shade would reconstruct floor pixels from head
                    //    triangles — a plausible-looking surface built from the wrong mesh, which is far harder to spot than a missing one.
                    ShadeConstants.FloorShadeEnabled = FloorShadeable ? 1u : 0u;

                    // 🔴 Paired with the flag above, never set independently: b5-b7 point at the MERGED buffer, so the floor's draw-local primitive
                    //    ordinals need its run's base added before they name the right triangle. Zero when the floor is not shadeable, matching the
                    //    aliased-binding case where no floor run exists to be based.
                    ShadeConstants.FloorIndexBase = FloorShadeable ? FloorDrawPlacement.IndexOffset : 0u;

                    // ---- Phase 3 surfel GI gather (the three runtime fields the camera-only assemble cannot see) ----
                    // 🔴 Post-swap read half: the integrate + SwapSurfelMoments above (~:1800) already flipped MomentsParity, so it now names the
                    //    fresh write half the integrate just filled. The gather indexes Moments[i + ReadOffsetElements] over 20-float structs, so the
                    //    ELEMENT base is MomentsParity*Capacity, NOT the byte offset SurfelMomentsReadOffset returns (the Phase-2 fact-4 trap).
                    ShadeConstants.SurfelReadOffsetElements = Extension.SurfelPoolResource.MomentsParity * Extension.SurfelPoolResource.Capacity;
                    ShadeConstants.SurfelCapacity           = Extension.SurfelPoolResource.Capacity;
                    // GI is ON only when the toggle is set AND the surfel set is actually bound — the record forces the flat-ambient path otherwise, but
                    // gating here too keeps the console A/B honest and avoids pushing a live read-offset the shader would ignore.
                    ShadeConstants.SurfelGiEnabled = (Extension.SurfelGiEnabled && Extension.SurfaceShade.SurfelSetReady) ? 1u : 0u;

                    // Live world-scale knobs (F10 window) — the gather hashes into the SAME cells the spawn+slotting build filled, so cell/radius must track.
                    ShadeConstants.TuneCellDiameter  = Extension.SurfelTuning.CellDiameter;
                    ShadeConstants.TuneBaseRadius    = Extension.SurfelTuning.BaseRadius;
                    ShadeConstants.TuneNearFieldBias = Extension.SurfelTuning.NearFieldBias;

                    // ---- Primary sun shadow (area-sampled BVH; set 2) ----
                    // The trace's TraceInstanceCount / TraceSliceCount, from the SAME sources the TLAS + integrate were counted against (the scene is
                    // static after load, so these are the leaf/slice counts the tree was built with). ShadowFrame rotates the per-pixel jitter so a
                    // temporal pass can average. GI is ON only when the toggle is set AND set 2 is bound — the record forces it off otherwise, but
                    // gating here avoids pushing live counts the shader would ignore and keeps the tuning A/B honest.
                    ShadeConstants.SunAngularRadius   = Extension.SurfelTuning.SunAngularRadius;
                    ShadeConstants.ShadowSampleCount  = (uint32_t)(Extension.SurfelTuning.ShadowSampleCount > 0 ? Extension.SurfelTuning.ShadowSampleCount : 1);
                    ShadeConstants.ShadowEnabled      = (Extension.SurfelTuning.ShadowEnabled && Extension.SurfaceShade.ShadowSetReady) ? 1u : 0u;
                    ShadeConstants.ShadowFrame        = Extension.SurfelFrameIndex;
                    ShadeConstants.ShadowInstanceCount = Extension.VisibilityRaster.InstanceCount;
                    ShadeConstants.ShadowSliceCount    = (uint32_t)Extension.GeometryArena.Slices.size();

                    BeginGpuTimestampScope(Extension.PassTiming, CommandBuffer, RenderExtension::SurfelPassSlotShade);
                    RecordSurfaceShadeInscription(Extension.SurfaceShade, Extent, ShadeConstants, CommandBuffer);
                    EndGpuTimestampScope(Extension.PassTiming, CommandBuffer, RenderExtension::SurfelPassSlotShade);
                }

                // Surfel debug splat (Phase 1, user-requested). Composites every LIVE surfel as a screen-space disc over the shaded scene, INSIDE this
                // radiance scope and after the shade — like GroundGridPass, a composite that records no dispatch of its own. It self-no-ops when the
                // mode is Off (the F6 default), so it costs nothing until asked for; when on it reads the pool + Offsets buffers the compute above just
                // fenced for the vertex stage. Gated on VisibilityWritten because an all-idle frame ran no lifecycle, so the pool holds last frame's
                // (or the seed) state — harmless, but there is nothing new to show and the gate keeps it in lockstep with the compute that feeds it.
                if (VisibilityWritten && Extension.SurfelDebug.ReadyCondition && Extension.SurfelDebugMode != SurfelDebugModeOff)
                {
                    // 🔴 Post-swap read half (mirrors the shade at ~:1913): the integrate + SwapSurfelMoments already flipped parity, so
                    //    MomentsParity*Capacity names the fresh irradiance the GI modes (5-7) read. The non-GI modes ignore it.
                    const uint32_t SurfelDebugReadOffset =
                        Extension.SurfelPoolResource.MomentsParity * Extension.SurfelPoolResource.Capacity;

                    SurfelDebugConstants DebugConstants;
                    AssembleSurfelDebugConstants(Extension.ViewCamera, Extent, Extension.SurfelDebugMode,
                                                 Extension.SurfelDebugRadiusScale, SurfelDebugReadOffset,
                                                 Extension.SurfelPoolResource.Capacity, Extension.SurfelTuning, DebugConstants);
                    BeginGpuTimestampScope(Extension.PassTiming, CommandBuffer, RenderExtension::SurfelPassSlotDebugSplat);
                    RecordSurfelDebugInscription(Extension.SurfelDebug, Extent, DebugConstants, CommandBuffer);
                    EndGpuTimestampScope(Extension.PassTiming, CommandBuffer, RenderExtension::SurfelPassSlotDebugSplat);
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

            // F6 cycles the surfel debug splat through Off -> Age -> Cascade -> Identity -> Occupancy -> Off (Phase-1 visual DoD). The splat draws
            // inside the radiance scope after shade and self-no-ops when the mode is Off, so cycling back to Off costs nothing. Edge-latched like the
            // others; refuses to arm when the inscription did not build (shaders unstaged), and the surfel FIELD advances every frame regardless — only
            // its on-screen display is toggled here.
            const bool SurfelDebugKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::F6);
            if (SurfelDebugKeyDown && !Extension.SurfelDebugModeKeyLatch)
            {
                if (!Extension.SurfelDebug.ReadyCondition)
                    printf("[surfel] debug view unavailable — the inscription did not build (shaders staged?)\n");
                else
                {
                    Extension.SurfelDebugMode = (Extension.SurfelDebugMode + 1) % SurfelDebugModeCount;
                    static const char* const SurfelDebugModeNames[SurfelDebugModeCount] =
                        { "OFF", "AGE", "CASCADE", "IDENTITY", "OCCUPANCY", "IRRADIANCE", "LUMINANCE", "GI-VS-DEAD" };
                    printf("[surfel] debug view -> %s\n", SurfelDebugModeNames[Extension.SurfelDebugMode]);
                }
                fflush(stdout);
            }
            Extension.SurfelDebugModeKeyLatch = SurfelDebugKeyDown;

            // F7 — Phase 3 GI A/B toggle. Flips between the surfel-cache gather and the old flat AmbientColour in the shade, keeping the pre-GI look one
            // press away. Edge-latched like the others; refuses to arm when the shade's surfel set never bound (SurfelSetReady false → the shade is on the
            // flat path anyway), so the console never claims GI is on while it is silently off.
            const bool SurfelGiKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::F7);
            if (SurfelGiKeyDown && !Extension.SurfelGiKeyLatch)
            {
                if (!Extension.SurfaceShade.SurfelSetReady)
                    printf("[surfel] GI unavailable — the shade's surfel set never bound (pool/slotting/shaders ready?)\n");
                else
                {
                    Extension.SurfelGiEnabled = !Extension.SurfelGiEnabled;
                    printf("[surfel] GI -> %s\n", Extension.SurfelGiEnabled ? "on" : "off");
                }
                fflush(stdout);
            }
            Extension.SurfelGiKeyLatch = SurfelGiKeyDown;

            // F8 / F9 shrink / grow the debug splat discs (radius scale), so overlapping surfels stop merging into a flat wash and the field
            // reads as individual dots. Multiplicative steps, clamped to a sane band; edge-latched so one press is one step. Only meaningful with a
            // debug mode on, but harmless off (the field advances regardless and the splat self-no-ops). Prints the new scale for feedback.
            const bool SurfelRadiusDownDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::F8);
            if (SurfelRadiusDownDown && !Extension.SurfelDebugRadiusDownLatch)
            {
                Extension.SurfelDebugRadiusScale = std::clamp(Extension.SurfelDebugRadiusScale * 0.8f, 0.1f, 8.0f);
                printf("[surfel] debug disc scale -> %.2f\n", Extension.SurfelDebugRadiusScale);
                fflush(stdout);
            }
            Extension.SurfelDebugRadiusDownLatch = SurfelRadiusDownDown;

            const bool SurfelRadiusUpDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::F9);
            if (SurfelRadiusUpDown && !Extension.SurfelDebugRadiusUpLatch)
            {
                Extension.SurfelDebugRadiusScale = std::clamp(Extension.SurfelDebugRadiusScale * 1.25f, 0.1f, 8.0f);
                printf("[surfel] debug disc scale -> %.2f\n", Extension.SurfelDebugRadiusScale);
                fflush(stdout);
            }
            Extension.SurfelDebugRadiusUpLatch = SurfelRadiusUpDown;

            // L — one-shot surfel-state dump to disk (the diagnostic instrument for the near-camera mound). Edge-latched: one press sets a one-shot
            // request the PREAMBLE consumes next frame, copying the live surfel records + this frame's per-tile spawn requests off the GPU into
            // SurfelDumps/ beside the exe (surfel-live-NNNN.csv, surfel-spawns-NNNN.csv, surfel-dump-NNNN.json). The copy is fired at the compute seam, NOT here — this is
            // input handling and has no command pool or post-record buffers; the facility does its own submit/wait. Refuses to re-arm while a prior
            // request is still pending so a held key does not queue a burst.
            const bool SurfelDumpKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::L);
            if (SurfelDumpKeyDown && !Extension.SurfelDumpKeyLatch)
            {
                if (Extension.SurfelDumpRequested)
                    printf("[surfel] dump already pending — press ignored\n");
                else if (!Extension.SurfelPoolResource.ReadyCondition)
                    printf("[surfel] dump unavailable — the pool did not build\n");
                else
                {
                    Extension.SurfelDumpRequested = true;
                    printf("[surfel] dump requested\n");
                }
                fflush(stdout);
            }
            Extension.SurfelDumpKeyLatch = SurfelDumpKeyDown;

            // K — TOGGLE the per-frame population census (SurfelCensusTrace.h). Deliberately a separate key and a separate CSV from L above: L is a
            // deep SNAPSHOT of one instant (every live surfel, for the viewer), this is a shallow TIME SERIES of births/deaths across many frames,
            // and only the series can show CHURN — a pool spawning and killing 2000 a frame looks identical to a settled one in any snapshot. Press
            // once to open surfel-census-NNNN.csv under the same SurfelDumps/ folder and start appending a row per frame, press again to close it.
            // Unlike the L dump this needs NO deferred request: recording is a flag the frame's own command recording reads, and the readback is
            // non-blocking (one frame late), so the toggle can take effect here and now.
            const bool SurfelCensusKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::K);
            if (SurfelCensusKeyDown && !Extension.SurfelCensusKeyLatch)
            {
                if (!Extension.SurfelCensus.ReadyCondition)
                    printf("[surfel] census unavailable — the trace buffers did not build\n");
                else if (Extension.SurfelCensus.Recording)
                {
                    const std::string ClosedPath = Extension.SurfelCensus.OutputPath;
                    const uint32_t    Rows       = EndSurfelCensusRecording(Extension.SurfelCensus);
                    printf("[surfel] census stopped — %u frames -> %s\n", Rows, ClosedPath.c_str());
                }
                else
                {
                    // Label from the LIVE tuning state, not from startup defaults: an interactive trace is normally taken right after flipping the F10
                    // toggles, so the whole point is to record what they are set to at the moment K is pressed.
                    const SurfelCensusRunLabel RunLabel = ComposeSurfelCensusRunLabel(Extension.SurfelTuning);

                    if (BeginSurfelCensusRecording(Extension.SurfelCensus, "SurfelDumps", &RunLabel))
                        printf("[surfel] census recording -> %s\n", Extension.SurfelCensus.OutputPath.c_str());
                    else
                        printf("[surfel] census could not open its CSV\n");
                }
                fflush(stdout);
            }
            Extension.SurfelCensusKeyLatch = SurfelCensusKeyDown;

            // Numpad + / - drive the live spawn-density multiplier: it scales the spawn-request throttle in SurfelSpawnRequest.comp, so more (or
            // fewer) surfels seed per frame from the same visibility pixels. Denser coverage is the direct lever on the "no surfel -> no GI"
            // clumpiness of screen-space spawn. Multiplicative steps, clamped to a sane band; edge-latched so one press is one step.
            const bool SurfelDensityUpDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::NumpadAdd);
            if (SurfelDensityUpDown && !Extension.SurfelSpawnDensityUpLatch)
            {
                Extension.SurfelSpawnDensityScale = std::clamp(Extension.SurfelSpawnDensityScale * 1.25f, 0.25f, 16.0f);
                printf("[surfel] spawn density -> %.2f\n", Extension.SurfelSpawnDensityScale);
                fflush(stdout);
            }
            Extension.SurfelSpawnDensityUpLatch = SurfelDensityUpDown;

            const bool SurfelDensityDownDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::NumpadSubtract);
            if (SurfelDensityDownDown && !Extension.SurfelSpawnDensityDownLatch)
            {
                Extension.SurfelSpawnDensityScale = std::clamp(Extension.SurfelSpawnDensityScale * 0.8f, 0.25f, 16.0f);
                printf("[surfel] spawn density -> %.2f\n", Extension.SurfelSpawnDensityScale);
                fflush(stdout);
            }
            Extension.SurfelSpawnDensityDownLatch = SurfelDensityDownDown;


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

            // 🔴 Swallow camera input while ImGui owns the mouse — otherwise dragging a tuning slider ALSO orbits/flies the scene behind the window
            //    (the camera reads the same raw pointer packets ImGui just consumed). WantCaptureMouse is true whenever the pointer is over an ImGui
            //    window OR a widget drag is active, so releasing the drag off-window still hands control back cleanly. Gated on ImguiReady so the
            //    camera is never frozen when the overlay failed to initialize. Keyboard fly (WASD) stays live — only pointer look/orbit/pan is gated.
            const bool ImguiWantsMouse = Extension.ImguiReady && ImGui::GetIO().WantCaptureMouse;
            if (!ImguiWantsMouse)
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
            // 📝 Both predicates ask "did this mesh contribute pixels to the id buffer", which since the merge is a question about each mesh's own
            //    sub-range: SceneGeometry.IndexCount is the whole world's run and would read as non-zero for a mesh that loaded nothing. The draw
            //    placements above are out of scope by here (they live inside the raster scope), so the two runs are re-read from the concatenation.
            const GeometryStreamPlacement HeadResolvePlacement =
                RetrieveGeometryStreamPlacement(Extension.SceneStreams, Extension.HeadMeshOrdinal);
            const GeometryStreamPlacement FloorResolvePlacement =
                RetrieveGeometryStreamPlacement(Extension.SceneStreams, Extension.FloorMeshOrdinal);
            const bool HeadsPresent = Extension.VisibilityRaster.InstanceCount > 0 && HeadResolvePlacement.IndexCount > 0;
            const bool FloorPresent = Extension.FloorRaster.InstanceCount > 0
                                   && Extension.FloorStreamPresent
                                   && FloorResolvePlacement.IndexCount > 0;
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

            // -- ImGui surfel-tuning overlay (F10) — LAST draw inside the colour scope, so the window reads on top of the whole scene ---------
            // 📝 F10 edge-latch toggles the window; the frame was already opened in the preamble (ImGui::NewFrame), so we build the window here,
            //    then Render + RenderDrawData into THIS command buffer — the same dynamic-rendering colour scope the scene drew into, the same
            //    present. NewFrame ran unconditionally in the preamble whenever ImguiReady, so ImGui::Render must run here every frame to balance
            //    it (drawing nothing when the window is closed); skipping Render on a frame that called NewFrame corrupts ImGui's frame state.
            if (Extension.ImguiReady)
            {
                const bool TuningKeyDown = PacketKeyHeld(Extension.Substrate.Window.Input, KeyIdentity::F10);
                if (TuningKeyDown && !Extension.SurfelTuning.FKeyLatch)
                {
                    Extension.SurfelTuning.WindowOpen = !Extension.SurfelTuning.WindowOpen;
                    printf("[surfel] tuning window -> %s\n", Extension.SurfelTuning.WindowOpen ? "on" : "off");
                    fflush(stdout);
                }
                Extension.SurfelTuning.FKeyLatch = TuningKeyDown;

                if (Extension.SurfelTuning.WindowOpen)
                    DrawSurfelTuningWindow(Extension.ImguiTheme, Extension.SurfelTuning);

                ImGui::Render();
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), CommandBuffer);
            }
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

    // ImGui overlay teardown — reverse of the init in InitializeRenderExtension, under the device-idle above (the backend may still hold this
    // frame's descriptor sets). Safe when ImguiReady was never set: the whole block no-ops, leaving nothing to destroy.
    if (Extension.ImguiReady)
    {
        ImGui_ImplVulkan_Shutdown();
        DetachImguiPlatform(Extension.Substrate.Window);
        ImGui::DestroyContext();
        Extension.ImguiReady = false;
    }

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

    // The resolve borrows the radiance target's view, so it is torn down first — releasing a sampler's descriptor after the image it points at is
    // destroyed is the wrong order even when both happen under one device-idle.
    FinalizeRadianceResolveInscription(Extension.RadianceResolve);
    FinalizeRadianceTarget(Extension.RadianceScene);
    // GPU timestamp probe: destroy its query pool. Device is idle at teardown; safe on a never-initialized (unsupported-device) value.
    FinalizeGpuTimestampScope(Extension.PassTiming);
    FinalizeSurfaceShadeInscription(Extension.SurfaceShade);
    // Phase-2 integrate first: it borrows BOTH the surfel state (pool/slotting) AND the BVH (arena + TLAS tree), all released below, so it must go ahead
    // of every one of them. Owns only its layouts/pipeline/pool; safe on never-initialized state.
    FinalizeSurfelIntegrateSubmission(Extension.SurfelIntegrate);
    // Surfel chain torn down in reverse init order (debug -> lifecycle -> slotting -> pool). Each borrows the visibility image + merged mesh buffers,
    // which are released further down, so the borrowers go first. All four are safe on never-initialized state (best-effort init leaves them inert).
    FinalizeSurfelDebugInscription(Extension.SurfelDebug);
    FinalizeSurfelLifecycleSubmission(Extension.SurfelLifecycle);
    FinalizeSurfelGridSlotting(Extension.SurfelSlotting);
    FinalizeSurfelPool(Extension.SurfelPoolResource);
    // 🩺 Census last of the surfel chain: its counters buffer is BOUND into the lifecycle's pool set (binding 9), so it outlives the set that points at
    // it. Also closes any CSV still open because the process exited mid-recording (the K toggle never got its second press).
    FinalizeSurfelCensusTrace(Extension.SurfelCensus);
    // TLAS teardown, reverse of init (tree -> sort -> bounds), and BEFORE the arena release below because the binds borrowed the arena's buffers.
    // All safe on never-initialized state; device already idle at the top of this function.
    FinalizeInstanceTreeSubmission(Extension.TlasTree);
    FinalizeRadixSortSubmission(Extension.TlasSort);
    FinalizeInstanceBoundsSubmission(Extension.TlasBounds);
    FinalizeVisibilityInscription(Extension.VisibilityResolve);
    FinalizeSoftwareRasterization(Extension.SoftwareRaster);
    FinalizeInstanceCullSubmission(Extension.InstanceCull);
    FinalizeVisibilityRasterization(Extension.VisibilityRaster);
    FinalizeVisibilityRasterization(Extension.FloorRaster);
    FinalizeScene(Extension.SceneRegistry);
    // The arena's slices name offsets into SceneGeometry, so it is released before the buffer those offsets address — reverse of the order the load
    // path built them in.
    FinalizeGeometryArenaSubmission(Extension.GeometryArena);
    ResetGeometryStreamConcatenation(Extension.SceneStreams);
    // 📝 ONE release now: the floor's geometry lives inside this same claim, so the second ReleasePolygonBufferAllocation that used to sit here would
    //    be a release of a handle nothing ever allocated.
    ReleasePolygonBufferAllocation(Extension.Substrate.Host, Extension.SceneGeometry);
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
