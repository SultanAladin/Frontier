/*==============================================================================================================================================
                                                          LIGHTINGTUNINGWINDOW.H
==============================================================================================================================================*/
// 🧩 The live lighting-tuning debug window state + its one draw call. Holds the sun-source knobs (elevation, azimuth, intensity, colour) that drive
//    AtmosphereProfile::AssignSolarDirection and the deferred shade's premultiplied SunRadiance, plus the primary sun-shadow knobs (enable,
//    angular radius, tap count) the area-sampled BVH ray in SurfaceShade.frag reads. DrawLightingTuningWindow renders one collapsible ImGui window
//    over the shared theme — pure state edit, NO engine / Vulkan calls, so it is host-agnostic and testable. The renderer threads the resulting
//    fields into the shade push block each frame.
//
//    📝 Split out of the retired SurfelTuningWindow when the webgiya surfel substrate was stripped: the world-scale cell/radius/per-cell-cap knobs
//       belonged to that grid and died with it, but the sun and shadow knobs never did — the SKY, the atmosphere profile, and the direct shade all
//       follow this ONE sun, none of which is surfel work.
//
//    📝 The GI block is the W298 surfel port's parameter home. It lives here rather than in its own window because it belongs to the same question the
//       sun cards answer — how is this frame lit — and a second floating window would split one decision across two places.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDEREXTENSION_LIGHTINGTUNINGWINDOW_H
#define FRONTIER_GRAPHICS_RENDEREXTENSION_LIGHTINGTUNINGWINDOW_H

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The renderer's debug view flags, surfaced in the panel as toggles. BORROWED POINTERS into the RenderExtension's own members — the window edits the
//    live flags in place, so a click and its keyboard shortcut drive the exact same state with no mirror to sync. A null pointer greys that row (the
//    field does not exist in this build, e.g. selection behind FRONTIER_POLYGON_AUTHORING), and an *Available flag false greys it too.
//
// 🔴 The *Available flags are NOT cosmetic. Software raster needs int64 shader atomics, and the shade / clipmap need their pipelines to have built; the
//    renderer FORCES those flags back to false when unavailable. Without greying, a toggle would flip, snap back, and read as a broken control — so the
//    panel must refuse the click rather than let the renderer silently undo it.
struct DebugViewBinding
{
    bool* VisibilityResolve   = nullptr;   // [-] - F2: composite the id buffer over the forward view
    bool* ObjectSelection     = nullptr;   // [-] - F3: hover + pick ring (absent when polygon authoring is compiled out)
    bool* TopologyWireframe   = nullptr;   // [-] - Numpad-0: authored ngon/quad/tri edges instead of triangulation edges
    bool* VisibilityScaling   = nullptr;   // [-] - Numpad-2: GPU cull -> indirect draw instead of the plain instanced draw
    bool* SoftwareRaster      = nullptr;   // [-] - Numpad-3: compute micro-raster instead of the hardware raster
    bool* SurfaceShade        = nullptr;   // [-] - F4: the deferred material BRDF instead of the forward view
    bool* ClipmapInspection   = nullptr;   // [-] - Numpad-4: clipmap cell lattice + probe markers

    bool  SoftwareRasterAvailable    = false;  // [-] - int64 shader atomics present AND the micro-raster pipeline built
    bool  SurfaceShadeAvailable      = false;  // [-] - the shade pipeline built (shaders staged)
    bool  ClipmapInspectionAvailable = false;  // [-] - the clipmap visualization pipelines built
    bool  TopologyWireframeAvailable = false;  // [-] - needs the resolve on to have any effect
};

// 📝 The live-editable lighting state. Every field is read by the renderer each frame and pushed into the shade / sky; nothing here is surfel state.
struct LightingTuningState
{
    bool  WindowOpen        = false;   // [-] - the F-key toggles this; the window is collapsible via its ImGui header when open
    bool  FKeyLatch         = false;   // [-] - edge latch so one F-key press flips WindowOpen once

    // 📝 Primary sun-shadow knobs (area-sampled BVH ray in SurfaceShade.frag). ShadowEnabled gates the whole per-pixel trace; SunAngularRadius widens
    //    the penumbra (0 = hard, ~0.0047 = the real sun, larger = artistically softer); ShadowSampleCount is the tap count per pixel (higher = smoother
    //    but costlier + wants a temporal/denoise pass at low counts). The renderer threads these into the shade push block each frame.
    bool  ShadowEnabled      = true;   // [-] - trace the direct-sun visibility gate; false leaves LightEnergy unshadowed
    float SunAngularRadius   = 0.03f;  // [rad] - half-angle of the sun disc the shadow rays spread across (penumbra width)
    int   ShadowSampleCount  = 8;      // [-] - jittered rays per pixel across the disc (1 = hard edge, more = smoother penumbra)

    // 📝 Sun source. Elevation/Azimuth drive AtmosphereProfile::AssignSolarDirection each frame, so the SKY and the direct shade follow ONE sun.
    //    Intensity x Colour is premultiplied into the shade's SunRadiance push each frame (the sky keeps its own SolarIlluminance calibration).
    //    Defaults mirror the shipped 45° profile + the retired hardcoded 3.0*(1,0.98,0.95).
    float SunElevation       = 0.7853982f;         // [rad] - 0 = horizon, +pi/2 = zenith; default 45°
    float SunAzimuth         = 0.0f;               // [rad] - around the up axis
    float SunIntensity       = 3.0f;               // [-] - key-light multiplier folded into SunRadiance
    float SunColour[3]       = { 1.0f, 0.98f, 0.95f }; // [-] - key-light tint (warm white default)

    // 📝 Global illumination (W298 surfel port). GlobalIlluminationEnabled is the MASTER switch and it is not a display filter: the renderer skips
    //    RECORDING the whole surfel chain when it is false, so switching GI off costs nothing rather than computing a field nobody reads. The shade
    //    then falls back to the flat AmbientColour fill, which is exactly the pre-port look — so the toggle is a true A/B against the old renderer.
    //
    //    🔴 Every knob below is INERT until the phase that owns it lands (the fields exist now so the panel is built once, not re-cut per phase). A
    //       slider that moves but changes nothing on screen is indistinguishable from a broken feature, so the card greys the not-yet-wired rows and
    //       each phase flips its own row live as it binds the matching W298 RuntimeParams field.
    bool  GlobalIlluminationEnabled = false;  // [-] - MASTER: false skips the surfel chain entirely and the shade uses the flat ambient fill
    float IndirectIntensity         = 1.0f;   // [-] - multiplier on the gathered indirect radiance before it reaches LightEnergy
    float SkyOcclusionStrength      = 1.0f;   // [-] - how strongly surfel coverage darkens the flat sky term (0 = never occlude)
    int   RayCountPerSurfel         = 8;      // [-] - rays cast per live surfel per frame; the dominant trace cost
    int   RayBounceLimit            = 1;      // [-] - path depth (1 = single bounce, the GIBS default)
    float SurfelCellExtent          = 0.25f;  // [m] - world edge of one grid cell; sets surfel density and the field's reach
    bool  SurfelDebugOverlay        = false;  // [-] - draw the surfel discs / cell bounds over the shaded image

    // 📝 Card collapse flags — caller-owned so BeginPropertyCard's expand state survives across frames.
    bool  SunSourceExpanded   = true;  // [-] - the sun-source (elevation/azimuth/intensity/colour) card starts open
    bool  SunShadowExpanded    = true;  // [-] - the sun-shadow card starts open
    bool  GlobalIlluminationExpanded = true;  // [-] - the GI card starts open
    bool  DebugViewExpanded          = false; // [-] - the debug-view card starts CLOSED: it is diagnostic, not a lighting knob
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw one collapsible ImGui window built from the REAL Interface controls (ConstructValueSlider / ConstructBooleanEntry inside a
// BeginPropertyPanel/BeginPropertyCard column) so it is styled BYTE-IDENTICALLY to ControlsGallery — the look comes from those components pushing
// Theme.Palette per-widget, not from the global ImGui style. Theme is the shared theme the renderer resolved at init; State holds the knobs. Call
// once per frame between ImGui::NewFrame and ImGui::Render, gated on State.WindowOpen by the caller.
// 🔴 No Vulkan / engine-spine calls — it only draws components + edits State, so the renderer reads the fields afterwards and does the GPU work at its own safe seams.
//
// Debug carries borrowed pointers to the renderer's own view flags so the toggles edit them in place (a click and the keyboard shortcut share one flag,
// with no mirror to drift). Pass a default-constructed DebugViewBinding to draw the card fully greyed.
void DrawLightingTuningWindow(const ThemeConfiguration& Theme, LightingTuningState& State, const DebugViewBinding& Debug);

}   // namespace Frontier

#endif
