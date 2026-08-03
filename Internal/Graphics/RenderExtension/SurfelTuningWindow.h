/*==============================================================================================================================================
                                                            SURFELTUNINGWINDOW.H
==============================================================================================================================================*/
// 🧩 The live surfel-tuning debug window state + its one draw call. Holds the world-scale knobs (cell diameter, base radius, near-field spawn
//    bias) and the per-cell cap selector the user dials at run time to fix near-camera coverage, plus the window-open flag and its F-key latch.
//    DrawSurfelTuningWindow renders one collapsible ImGui window over the shared theme — pure state edit, NO engine / Vulkan calls, so it is
//    host-agnostic and testable. The renderer threads the resulting fields into every surfel shader's push block (whole-pipeline reach) so
//    coverage AND the GI gather track the sliders together; the cap only takes effect on the explicit Apply button (a commit point the renderer
//    consumes at a device-idle seam), never per click.

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDEREXTENSION_SURFELTUNINGWINDOW_H
#define FRONTIER_GRAPHICS_RENDEREXTENSION_SURFELTUNINGWINDOW_H

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The live-editable tuning state, seeded from the baked SurfelGrid.glsl constants so opening the window mid-run shows the values the shaders
//    are currently using. CellDiameter / BaseRadius / NearFieldBias feed every surfel shader each frame (whole-pipeline). PerCellCapChoice is the
//    pending combo selection; PerCellCapApplied is what the grid buffers are sized/looped against right now — Apply copies choice into applied.
struct SurfelTuningState
{
    bool  WindowOpen        = false;   // [-] - the F-key toggles this; the window is collapsible via its ImGui header when open
    bool  FKeyLatch         = false;   // [-] - edge latch so one F-key press flips WindowOpen once (mirrors the F1/F6/F7 latches)

    float CellDiameter      = 1.0f;    // [m] - base (cascade-0) cell edge; seeds from SURFEL_GRID_CELL_DIAMETER
    float BaseRadius        = 1.2f;    // [m] - surfel disc radius at cascade 0; seeds from SURFEL_BASE_RADIUS
    float NearFieldBias     = 1.0f;    // [-] - lifts near-field spawn probability (1.0 = current behaviour, inert; >1 spawns more where the camera looks)

    int   PerCellCapChoice  = 64;      // [-] - 32/64/128/256 selection, PENDING until Apply
    int   PerCellCapApplied = 64;      // [-] - the cap the grid buffers are currently sized/looped for (what the shaders actually use)
    bool  ApplyCapRequested = false;   // [-] - set by the Apply button, consumed + cleared by the renderer's cap-commit seam

    // 📝 Primary sun-shadow knobs (area-sampled BVH ray in SurfaceShade.frag). ShadowEnabled gates the whole per-pixel trace; SunAngularRadius widens
    //    the penumbra (0 = hard, ~0.0047 = the real sun, larger = artistically softer); ShadowSampleCount is the tap count per pixel (higher = smoother
    //    but costlier + wants a temporal/denoise pass at low counts). The renderer threads these into the shade push block each frame.
    bool  ShadowEnabled      = true;   // [-] - trace the direct-sun visibility gate; false leaves LightEnergy unshadowed
    float SunAngularRadius   = 0.03f;  // [rad] - half-angle of the sun disc the shadow rays spread across (penumbra width)
    int   ShadowSampleCount  = 8;      // [-] - jittered rays per pixel across the disc (1 = hard edge, more = smoother penumbra)

    // 📝 Sun source (F10 Sun card, tuning-window override). Elevation/Azimuth drive AtmosphereProfile::AssignSolarDirection each frame, so the SKY,
    //    the surfel integrate, and the direct shade all follow ONE sun. Intensity x Colour is premultiplied into the shade's SunRadiance push each
    //    frame (the sky keeps its own SolarIlluminance calibration). Defaults mirror the shipped 45° profile + the retired hardcoded 3.0*(1,0.98,0.95).
    float SunElevation       = 0.7853982f;         // [rad] - 0 = horizon, +pi/2 = zenith; default 45°
    float SunAzimuth         = 0.0f;               // [rad] - around the up axis
    float SunIntensity       = 3.0f;               // [-] - key-light multiplier folded into SunRadiance
    float SunColour[3]       = { 1.0f, 0.98f, 0.95f }; // [-] - key-light tint (warm white default)

    // 📝 Card collapse flags — caller-owned so BeginPropertyCard's expand state survives across frames (ControlsGallery keeps them in its State too).
    bool  WorldScaleExpanded  = true;  // [-] - the world-scale (cell/radius/bias) card starts open
    bool  PerCellCapExpanded  = true;  // [-] - the per-cell-cap card starts open
    bool  SunSourceExpanded   = true;  // [-] - the sun-source (elevation/azimuth/intensity/colour) card starts open
    bool  SunShadowExpanded   = true;  // [-] - the sun-shadow card starts open
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw one collapsible ImGui window built from the REAL Interface controls (ConstructValueSlider / ConstructSelectionEntry /
// ConstructBooleanEntry inside a BeginPropertyPanel/BeginPropertyCard column) so it is styled BYTE-IDENTICALLY to ControlsGallery — the
// look comes from those components pushing Theme.Palette per-widget, not from the global ImGui style. Theme is the shared theme the renderer
// resolved at init; State holds the knobs. Call once per frame between ImGui::NewFrame and ImGui::Render, gated on State.WindowOpen by the caller.
// 🔴 No Vulkan / engine-spine calls — it only draws components + edits State, so the renderer reads the fields afterwards and does the GPU work at its own safe seams.
void DrawSurfelTuningWindow(const ThemeConfiguration& Theme, SurfelTuningState& State);

}   // namespace Frontier

#endif
