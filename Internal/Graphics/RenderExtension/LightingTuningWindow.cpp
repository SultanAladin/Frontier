/*==============================================================================================================================================
                                                        LIGHTINGTUNINGWINDOW.CPP
==============================================================================================================================================*/
// 🧩 Draws the live lighting-tuning window using the SAME real Interface controls ControlsGallery uses — BeginPropertyPanel + BeginPropertyCard
//    scaffolding hosting ConstructValueSlider / ConstructSelectionEntry / ConstructBooleanEntry / ConstructColorEntry rows. The distinctive
//    ControlsGallery look does NOT come from the global ImGui style; it comes from each of these components pushing Theme.Palette colours
//    per-widget. So the window is styled byte-identically to ControlsGallery.exe by construction, not by approximating its palette. The renderer
//    owns the ImGui context + frame bracket and reads the LightingTuningState fields this edits; this file makes NO Vulkan / engine-spine calls,
//    so it stays a testable leaf.

#include "Graphics/RenderExtension/LightingTuningWindow.h"

#include "EngineContext/Interface/Components/PropertyPanelBase.h"

#include "EngineContext/Interface/Components/Controls/ValueSlider.h"

#include "EngineContext/Interface/Components/Controls/SelectionEntry.h"

#include "EngineContext/Interface/Components/Controls/BooleanEntry.h"

#include "EngineContext/Interface/Components/Controls/ColorEntry.h"

#include "imgui.h"

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The sun-shadow sample-count ladder. ConstructSelectionEntry edits an INDEX, the tuning state stores the tap COUNT, so we map
    //    value<->index at the edges of the draw. 1 is a hard shadow; 8 is the default soft penumbra; 32 is the smooth-but-costly end.
    const int         ShadowSampleValues[] = { 1, 4, 8, 16, 32 };
    const char* const ShadowSampleLabels[] = { "1", "4", "8", "16", "32" };
    constexpr int     ShadowSampleCount    = 5;

    // Value -> index (default to 8's index if off the ladder).
    int ShadowSampleIndexForValue(int Value)
    {
        for (int Index = 0; Index < ShadowSampleCount; ++Index)
            if (ShadowSampleValues[Index] == Value)
                return Index;
        return 2;
    }

    // 📝 The surfel rays-per-surfel ladder. Discrete for a reason: this count multiplies by the live surfel population into the per-frame ray total
    //    through a SOFTWARE BVH, so a free slider invites a value the frame budget cannot pay. 8 is the port's starting point.
    const int         SurfelRayValues[]   = { 1, 2, 4, 8, 16, 32 };
    const char* const SurfelRayLabels[]   = { "1", "2", "4", "8", "16", "32" };
    constexpr int     SurfelRayLadderCount = 6;

    int SurfelRayIndexForValue(int Value)
    {
        for (int Index = 0; Index < SurfelRayLadderCount; ++Index)
            if (SurfelRayValues[Index] == Value)
                return Index;
        return 3;   // 8 — the GIBS starting point
    }

    // 📝 Path depth. 1 is the GIBS default (single bounce); the surfel field itself carries multi-bounce energy across frames by feeding its own gather,
    //    so depth beyond 2 buys little for its cost.
    const int         SurfelBounceValues[]   = { 1, 2, 3 };
    const char* const SurfelBounceLabels[]   = { "1", "2", "3" };
    constexpr int     SurfelBounceLadderCount = 3;

    int SurfelBounceIndexForValue(int Value)
    {
        for (int Index = 0; Index < SurfelBounceLadderCount; ++Index)
            if (SurfelBounceValues[Index] == Value)
                return Index;
        return 0;
    }

    // 📝 One debug-view row. Flag is a BORROWED pointer into the renderer's live member, so the checkbox edits the same byte the keyboard shortcut does.
    //    A null Flag or Available=false draws the row greyed against a local false — ConstructBooleanEntry needs a valid address even when disabled, and
    //    routing a disabled row at the real flag would risk writing through it. The shortcut is in the label because the panel is where a reader looks
    //    to discover these at all: every one of them was keyboard-only before.
    void ConstructDebugViewRow(const ThemeConfiguration& Theme, const char* Label, bool* Flag, bool Available)
    {
        bool Placeholder = (Flag != nullptr) ? *Flag : false;

        BooleanEntryDescriptor Row = {};
        Row.Label   = Label;
        Row.Value   = (Flag != nullptr && Available) ? Flag : &Placeholder;
        Row.Enabled = (Flag != nullptr && Available);
        ConstructBooleanEntry(Theme, Row);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void DrawLightingTuningWindow(const ThemeConfiguration& Theme, LightingTuningState& State, const DebugViewBinding& Debug)
{
    // 📝 The caller gates on WindowOpen, but pass &WindowOpen as p_open so the window's own [x] also clears it. WindowBg is pushed to the
    //    theme's desk background — the same wrap ControlsGalleryHost puts around its host window — so the surface behind the cards reads right.
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme.Palette.DeskBackground);
    const bool Visible = ImGui::Begin("Lighting — Tuning", &State.WindowOpen);
    if (!Visible)
    {
        ImGui::End();            // collapsed: Begin returned false, but End must still balance the Begin
        ImGui::PopStyleColor();  // and the pushed WindowBg must be popped on every path
        return;
    }

    BeginPropertyPanel(Theme, "##lighting-tuning");

    // -- Sun source (tuning-window override). Elevation/azimuth rewrite the atmosphere profile's solar vector each frame, so the SKY and the direct
    //    shade both follow ONE sun; intensity x colour is premultiplied into the shade's key radiance. Degrees at the UI, radians in the state —
    //    the ×π/180 conversion sits at the slider edges so SunElevation/SunAzimuth stay the shader-native unit. --------------------------------------
    if (BeginPropertyCard(Theme, "Sun source (live, whole pipeline)", &State.SunSourceExpanded))
    {
        const float DegToRad = 3.14159265358979323846f / 180.0f;

        float ElevationDegrees = State.SunElevation / DegToRad;
        ValueSliderDescriptor Elevation = {};
        Elevation.Label = "Elevation"; Elevation.Value = &ElevationDegrees;
        Elevation.Minimum = 0.0f; Elevation.Maximum = 90.0f; Elevation.Format = "%.1f"; Elevation.Unit = "\xC2\xB0"; Elevation.Enabled = true;   // °
        if (ConstructValueSlider(Theme, Elevation))
            State.SunElevation = ElevationDegrees * DegToRad;

        float AzimuthDegrees = State.SunAzimuth / DegToRad;
        ValueSliderDescriptor Azimuth = {};
        Azimuth.Label = "Azimuth"; Azimuth.Value = &AzimuthDegrees;
        Azimuth.Minimum = 0.0f; Azimuth.Maximum = 360.0f; Azimuth.Format = "%.1f"; Azimuth.Unit = "\xC2\xB0"; Azimuth.Enabled = true;   // °
        if (ConstructValueSlider(Theme, Azimuth))
            State.SunAzimuth = AzimuthDegrees * DegToRad;

        ValueSliderDescriptor Intensity = {};
        Intensity.Label = "Intensity"; Intensity.Value = &State.SunIntensity;
        Intensity.Minimum = 0.0f; Intensity.Maximum = 12.0f; Intensity.Format = "%.2f"; Intensity.Unit = "\xC3\x97"; Intensity.Enabled = true;   // × multiplier
        ConstructValueSlider(Theme, Intensity);

        ColorEntryDescriptor Colour = {};
        Colour.Label = "Colour"; Colour.Channels = State.SunColour; Colour.IncludeAlpha = false; Colour.Enabled = true;
        ConstructColorEntry(Theme, Colour);

        EndPropertyCard(Theme);
    }

    // -- Primary sun shadow: the area-sampled BVH ray in SurfaceShade.frag. An Enabled toggle, a penumbra-width slider, and a discrete tap-count ladder.
    //    All take effect the next frame the shade records — the renderer threads them into the shade push block with no device stall. -----------------
    if (BeginPropertyCard(Theme, "Sun shadow (area-sampled, live)", &State.SunShadowExpanded))
    {
        BooleanEntryDescriptor ShadowOn = {};
        ShadowOn.Label = "Cast sun shadow"; ShadowOn.Value = &State.ShadowEnabled; ShadowOn.Enabled = true;
        ConstructBooleanEntry(Theme, ShadowOn);

        // 📝 Penumbra width = the sun's angular half-radius the shadow rays spread across. The real sun is ~0.0047 rad; the range runs to an
        //    artistically-soft 0.15. Greyed while shadows are off, since it does nothing then.
        ValueSliderDescriptor Penumbra = {};
        Penumbra.Label = "Penumbra width"; Penumbra.Value = &State.SunAngularRadius;
        Penumbra.Minimum = 0.0f; Penumbra.Maximum = 0.15f; Penumbra.Format = "%.4f"; Penumbra.Unit = "rad"; Penumbra.Enabled = State.ShadowEnabled;
        ConstructValueSlider(Theme, Penumbra);

        // Discrete tap count (1/4/8/16/32). More taps = smoother penumbra at higher per-pixel cost.
        int ShadowIndex = ShadowSampleIndexForValue(State.ShadowSampleCount);
        SelectionEntryDescriptor Samples = {};
        Samples.Label = "Samples"; Samples.SelectedIndex = &ShadowIndex;
        Samples.Options = ShadowSampleLabels; Samples.OptionCount = ShadowSampleCount; Samples.Enabled = State.ShadowEnabled;
        if (ConstructSelectionEntry(Theme, Samples))
            State.ShadowSampleCount = ShadowSampleValues[ShadowIndex];

        EndPropertyCard(Theme);
    }

    // -- Global illumination (W298 surfel port). The first row is the MASTER switch and every row under it is greyed while it is off, because those knobs
    //    genuinely do nothing then — the renderer skips recording the surfel chain outright rather than computing a field the shade ignores.
    //
    //    🔴 Rows whose owning phase has not landed are greyed EVEN WITH GI ON, and say so in their label. A live-looking slider that moves but changes
    //       nothing reads as a broken feature, which is a worse signal than an honestly disabled control. Each phase drops its parenthetical and flips
    //       Enabled to State.GlobalIlluminationEnabled as it wires the matching field.
    //       📝 Live now: the two shade-side knobs (phase 8 pushes them). Still greyed: the trace ladders and cell extent, because nothing in this renderer
    //          records the surfel chain yet, and the overlay, because its splat pass is not in the tree. -------------------------------------------------
    if (BeginPropertyCard(Theme, "Global illumination (surfel)", &State.GlobalIlluminationExpanded))
    {
        BooleanEntryDescriptor GlobalIlluminationOn = {};
        GlobalIlluminationOn.Label = "Enable GI";
        GlobalIlluminationOn.Value = &State.GlobalIlluminationEnabled;
        GlobalIlluminationOn.Enabled = true;
        ConstructBooleanEntry(Theme, GlobalIlluminationOn);

        // 📝 LIVE as of phase 8 — both ride the shade push block into SurfaceShade.frag's gather, so a drag lands on the next recorded frame.
        //    ⚠️ Enabled tracks the master switch ONLY, not whether the surfel set is actually pointed. The renderer's push gate zeroes the gather when the
        //       store is absent; the panel cannot see that, and greying on it would make the rows flicker with descriptor readiness.
        ValueSliderDescriptor Indirect = {};
        Indirect.Label = "Indirect intensity"; Indirect.Value = &State.IndirectIntensity;
        Indirect.Minimum = 0.0f; Indirect.Maximum = 4.0f; Indirect.Format = "%.2f"; Indirect.Unit = "\xC3\x97";   // × multiplier
        Indirect.Enabled = State.GlobalIlluminationEnabled;
        ConstructValueSlider(Theme, Indirect);

        // 🔴 THIS IS THE FADE CEILING, NOT AN OCCLUSION AMOUNT — it caps how far surfel coverage may displace the flat ambient fill. 1.0 = fully-covered
        //    surfaces take pure gathered GI; 0.0 = the fill is never displaced, which makes the gather a no-op indistinguishable from GI being off. The
        //    label says "sky occlusion" because that is upstream's name for the same scalar; read it as trust in the field. See the seam in
        //    SurfaceShade.frag for why the fade exists at all (a sparse field must not black out the surfaces it has not reached yet).
        ValueSliderDescriptor SkyOcclusion = {};
        SkyOcclusion.Label = "Sky occlusion"; SkyOcclusion.Value = &State.SkyOcclusionStrength;
        SkyOcclusion.Minimum = 0.0f; SkyOcclusion.Maximum = 1.0f; SkyOcclusion.Format = "%.2f";
        SkyOcclusion.Enabled = State.GlobalIlluminationEnabled;
        ConstructValueSlider(Theme, SkyOcclusion);

        // 🚧 Phase 6 owns the trace: rays per surfel is the dominant cost through the software BVH, so it stays discrete rather than a free slider that
        //    invites a value the frame budget cannot pay for.
        int RayIndex = SurfelRayIndexForValue(State.RayCountPerSurfel);
        SelectionEntryDescriptor Rays = {};
        Rays.Label = "Rays / surfel (phase 6)"; Rays.SelectedIndex = &RayIndex;
        Rays.Options = SurfelRayLabels; Rays.OptionCount = SurfelRayLadderCount; Rays.Enabled = false;
        if (ConstructSelectionEntry(Theme, Rays))
            State.RayCountPerSurfel = SurfelRayValues[RayIndex];

        int BounceIndex = SurfelBounceIndexForValue(State.RayBounceLimit);
        SelectionEntryDescriptor Bounces = {};
        Bounces.Label = "Bounces (phase 6)"; Bounces.SelectedIndex = &BounceIndex;
        Bounces.Options = SurfelBounceLabels; Bounces.OptionCount = SurfelBounceLadderCount; Bounces.Enabled = false;
        if (ConstructSelectionEntry(Theme, Bounces))
            State.RayBounceLimit = SurfelBounceValues[BounceIndex];

        // 🚧 Phase 4 owns the grid. 🔴 Cell extent is NOT live-editable even once wired: it sizes the grid allocations, so changing it must rebuild the
        //    store on an idle device. Phase 4 gives it an explicit Apply rather than letting a drag resize buffers mid-frame.
        ValueSliderDescriptor CellExtent = {};
        CellExtent.Label = "Cell extent (phase 4)"; CellExtent.Value = &State.SurfelCellExtent;
        CellExtent.Minimum = 0.05f; CellExtent.Maximum = 1.0f; CellExtent.Format = "%.2f"; CellExtent.Unit = "m"; CellExtent.Enabled = false;
        ConstructValueSlider(Theme, CellExtent);

        // 🚧 STAYS GREYED THROUGH PHASE 9, and deliberately so. There is no SurfelDebugInscription in the tree (see FolderStructure.md — the splat pass went
        //    with the strip and has not been re-ported), and the surfel chain is not recorded into the frame yet either, so the overlay has both no pipeline
        //    to draw with and nothing to draw. Whoever re-ports the splat flips this row; wiring the toggle first would give it a control that does nothing.
        BooleanEntryDescriptor DebugOverlay = {};
        DebugOverlay.Label = "Debug overlay (no splat pass)";
        DebugOverlay.Value = &State.SurfelDebugOverlay;
        DebugOverlay.Enabled = false;
        ConstructBooleanEntry(Theme, DebugOverlay);

        EndPropertyCard(Theme);
    }

    // -- Debug views. Every one of these was keyboard-only and therefore undiscoverable: the shortcuts are printf-documented in RenderExtension.cpp and
    //    nowhere a running user can see. The toggles edit the renderer's live flags THROUGH BORROWED POINTERS, so a click and its shortcut are the same
    //    state — not a mirror that can drift. Labels keep the shortcut so the panel teaches the keys rather than replacing them.
    //
    //    🔴 Capability-gated rows are greyed, not merely ineffective. The renderer force-clears SoftwareRaster / SurfaceShade / ClipmapInspection when
    //       their pipelines did not build (or int64 atomics are absent), so an enabled-looking toggle would flip and snap straight back — which reads as
    //       a broken control rather than an unavailable feature. Starts COLLAPSED: diagnostics, not lighting. -----------------------------------------
    if (BeginPropertyCard(Theme, "Debug views", &State.DebugViewExpanded))
    {
        ConstructDebugViewRow(Theme, "Visibility resolve (F2)",   Debug.VisibilityResolve, Debug.VisibilityResolve != nullptr);
        ConstructDebugViewRow(Theme, "Surface shade (F4)",        Debug.SurfaceShade,      Debug.SurfaceShadeAvailable);
        ConstructDebugViewRow(Theme, "Object selection (F3)",     Debug.ObjectSelection,   Debug.ObjectSelection != nullptr);

        // 📝 The wireframe only draws over the resolve, so its availability tracks the resolve being ON — greyed otherwise, since flipping it would
        //    change nothing visible and look like a dead control.
        ConstructDebugViewRow(Theme, "Topology wireframe (Numpad 0)", Debug.TopologyWireframe, Debug.TopologyWireframeAvailable);

        ConstructDebugViewRow(Theme, "GPU cull / indirect (Numpad 2)", Debug.VisibilityScaling, Debug.VisibilityScaling != nullptr);
        ConstructDebugViewRow(Theme, "Software raster (Numpad 3)",     Debug.SoftwareRaster,    Debug.SoftwareRasterAvailable);
        ConstructDebugViewRow(Theme, "Clipmap lattice (Numpad 4)",     Debug.ClipmapInspection, Debug.ClipmapInspectionAvailable);

        EndPropertyCard(Theme);
    }

    EndPropertyPanel(Theme);

    ImGui::End();
    ImGui::PopStyleColor();
}

}   // namespace Frontier
