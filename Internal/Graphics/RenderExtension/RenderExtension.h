/*==============================================================================================================================================
                                                              RENDEREXTENSION.H
==============================================================================================================================================*/
// 🧩 The modular render coordinator. Stands on the shared WindowSubstrate (window + Vulkan host + swapchain + present) and adds the two things
//    a from-scratch visibility-buffer renderer needs from step one: a HardwareFeatureProfile probe (does this GPU meet the Pascal / GTX-1060
//    floor and expose the int64 image atomics the software micro-raster will need) and the single Initialize / Synthesize / Finalize seam any
//    host application drives it through. Phase 0.1 only clears and presents; later phases record their passes into SynthesizeOutputSequence.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDEREXTENSION_RENDEREXTENSION_H
#define FRONTIER_GRAPHICS_RENDEREXTENSION_RENDEREXTENSION_H

#include "Graphics/RenderExtension/Device/WindowSubstrate.h"
#include "Graphics/Grid/GroundGridPass.h"
#include "Graphics/Atmosphere/SkyAtmosphere.h"
#include "EngineContext/Navigation/Camera/CameraConfiguration.h"
#include "EngineContext/Navigation/Camera/CameraNavigation/CameraNavigation.h"
#include "EngineContext/Navigation/Camera/CameraProjection/CameraViewMatrixSolver.h"
#include "EngineContext/Navigation/Camera/CameraProjection/ProjectionEvaluator.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The look-jitter mitigation strategy, cycled at run time (F1) so the three can be compared live against the same hand
//    motion. See the RenderExtension fields for what each one does and the state it keeps.
enum class LookFilterMode : uint8_t
{
    Raw = 0,           // No filter — the quantized count→angle baseline
    AnchorAccumulate,  // Blender-style: rotate toward the total drag offset from the press anchor
    VelocityLowpass,   // UE5-style: wall-clock exponential low-pass on look velocity

    ModeCount
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 What the selected GPU can do, inspected once after the Vulkan host is up. The two booleans gate the software micro-raster
//    (Phase 3): without int64 image atomics the atomicMax depth+id packing has no hardware path, and the baseline condition is
//    the overall "this GPU is at or above the GTX-1060 / Pascal floor" verdict the renderer refuses to fall below.
struct HardwareFeatureProfile
{
    char        DeviceName[256]      = { 0 };      // [-] - Reported physical-device name
    uint32_t    DriverApiVersion     = 0;          // [-] - Vulkan API version the device advertises
    bool        Int64AtomicsEnabled  = false;      // [-] - Shader int64 image/buffer atomics present (micro-raster path)
    bool        ArchitectureBaselineCondition = false; // [-] - Device meets the Pascal / GTX-1060 minimum tier
};

// 📝 The render coordinator itself. Owns the shared present machinery by reference-of-composition (the WindowSubstrate member)
//    plus the capability verdict. Deliberately thin at 0.1 — it holds nothing that identifies a single host, so any application
//    can construct one. Input is read straight off the window's InputPacket each frame (no separate input pipeline); the camera
//    is driven Unreal-style (RMB + WASD free-fly, scroll = fly speed) with a coexisting Alt-drag orbit / pan for DCC work.
struct RenderExtension
{
    WindowSubstrate         Substrate;             // [-] - Window + Vulkan host + swapchain + present loop
    HardwareFeatureProfile  FeatureProfile;        // [-] - GPU capability verdict from InspectHardwareFeatures

    GroundGridPass          GridPass;              // [-] - GPU ground-grid draw (lines + dots), built once
    SkyAtmospherePass       SkyPass;               // [-] - Hillaire 2020 sky/atmosphere, drawn behind the grid
    ViewportCamera          ViewCamera;            // [-] - Orbit / fly camera spec the grid is rendered through
    double                  PreviousTimestamp = 0.0; // [s] - Last frame's clock reading, for the per-frame delta
    float                   FlySpeedScale     = 1.0f; // [-] - Scroll-adjusted fly-speed multiplier (Unreal-style)

    // 📝 Look-jitter fix, three switchable strategies (F1 cycles). The jitter is proven (JitterProbe) to be integer-count
    //    quantization of a 1000 Hz mouse sampled into a 60 fps loop: at slow drag speed each frame carries only 0–3 whole
    //    device counts, delivered in uneven bursts (some frames 2, some 0), so a raw count→angle map lurches-and-stalls.
    //      • Raw              — no filter; the count→angle baseline (this IS the jittery signal, kept for A/B).
    //      • AnchorAccumulate — Blender's viewrotate: rotate toward the total drag offset from the press anchor, not the
    //                           per-frame delta. Quantization cannot accumulate into jitter; zero lag, no phantom motion.
    //      • VelocityLowpass  — UE5-style: wall-clock exponential low-pass on the look VELOCITY (counts/s); framerate
    //                           independent, decays to zero when input stops (no phantom motion on the zero-count frames).
    LookFilterMode          LookMode          = LookFilterMode::AnchorAccumulate; // [-] - Active strategy (F1 cycles)

    // AnchorAccumulate state. On the drag's first frame the total offset is seeded; thereafter the summed pointer delta is
    // added and the camera is turned by (total·sens − already-applied), so it always tracks the exact hand offset losslessly.
    bool                    LookDragActive    = false; // [-] - True while an orbit/look drag is in progress
    float                   LookOffsetX       = 0.0f;  // [px] - Total summed drag offset since press (X)
    float                   LookOffsetY       = 0.0f;  // [px] - Total summed drag offset since press (Y)
    float                   LookAppliedX      = 0.0f;  // [rad] - Yaw already applied this drag (so we feed only the increment)
    float                   LookAppliedY      = 0.0f;  // [rad] - Pitch already applied this drag

    // VelocityLowpass state. Smoothed look velocity in device counts / second, low-passed with a wall-clock time constant.
    float                   LookVelocityX     = 0.0f;  // [px/s] - Low-passed pointer X velocity
    float                   LookVelocityY     = 0.0f;  // [px/s] - Low-passed pointer Y velocity

    bool                    LookModeKeyLatch  = false; // [-] - Edge latch so one F1 press cycles the mode once
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Stand up the window + Vulkan host + swapchain, then inspect the GPU's features. Returns false with the extension left safely
// finalizable on any failure (including a device below the architecture baseline).
bool InitializeRenderExtension(RenderExtension& Extension,
                               const char*      TitleText,
                               uint32_t         RequestedWidth,
                               uint32_t         RequestedHeight);

// Drive the frame sequence until the window is closed. A frame is one temporal sequence: at 0.1 this is acquire -> clear ->
// present via the substrate; later phases record the visibility, shade, and lighting passes into the same sequence.
void SynthesizeOutputSequence(RenderExtension& Extension);

// Destroy everything InitializeRenderExtension created. Safe on partially-initialized state.
void FinalizeRenderExtension(RenderExtension& Extension);

// Inspect the selected physical device and fill the HardwareFeatureProfile. Standalone so a host can query the verdict without
// driving a sequence. Requires the Vulkan host inside the substrate to already be initialized.
void InspectHardwareFeatures(const VulkanHost& Host, HardwareFeatureProfile& Profile);

} // namespace Frontier

#endif
