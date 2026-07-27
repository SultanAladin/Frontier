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
        OrbitViewportCamera(Camera, -YawIncrement, PitchIncrement);   // +pitch: drag up looks up (Vulkan Y-down clip is flipped at the projection)

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
            OrbitViewportCamera(Camera, -YawIncrement, PitchIncrement);   // +pitch: drag up looks up (Vulkan Y-down clip is flipped at the projection)
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
// ground plane) and the world-space eye. The orbit spec stores no matrices — the view + projection frames are derived here.
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

    return true;
}

void SynthesizeOutputSequence(RenderExtension& Extension)
{
    // 📝 One recorder per frame drives the whole sequence: derive the frame delta from the clock, advance input, refresh the
    //    projection aspect, drive the orbit camera off the resolved intents, assemble the grid constants from the updated
    //    camera, and record the ground-grid pass. The substrate owns clear/present around this.
    Extension.PreviousTimestamp = QueryMonotonicSeconds();
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

            DriveViewportCamera(Extension, Delta);

            // Sky first — it fills the whole framebuffer as the background; the grid alpha-blends over it.
            SkyDomeConstants SkyConstants;
            AssembleSkyConstants(Extension.ViewCamera, SkyConstants);
            RecordSkyAtmospherePass(Extension.SkyPass, Extension.Substrate.Host, CommandBuffer, Extent, SkyConstants);

            GroundGridConstants Constants;
            AssembleGridConstants(Extension.ViewCamera, Constants);
            RecordGroundGridPass(Extension.GridPass, CommandBuffer, Extent, Constants);
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
    FinalizeSkyAtmospherePass(Extension.SkyPass, Extension.Substrate.Host);
    FinalizeGroundGridPass(Extension.GridPass, Extension.Substrate.Host);
    FinalizeWindowSubstrate(Extension.Substrate);
}

} // namespace Frontier
