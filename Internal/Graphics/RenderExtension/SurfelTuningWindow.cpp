/*==============================================================================================================================================
                                                            SURFELTUNINGWINDOW.CPP
==============================================================================================================================================*/
// 🧩 Draws the live surfel-tuning window using the SAME real Interface controls ControlsGallery uses — BeginPropertyPanel + BeginPropertyCard
//    scaffolding hosting ConstructValueSlider / ConstructSelectionEntry / ConstructBooleanEntry rows. The distinctive ControlsGallery look does
//    NOT come from the global ImGui style; it comes from each of these components pushing Theme.Palette colours per-widget. So the window is styled
//    byte-identically to ControlsGallery.exe by construction, not by approximating its palette. The renderer owns the ImGui context + frame bracket
//    and reads the SurfelTuningState fields this edits; this file makes NO Vulkan / engine-spine calls, so it stays a testable leaf.

#include "Graphics/RenderExtension/SurfelTuningWindow.h"

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
    // 📝 The fixed per-cell cap ladder. The window edits an INDEX into this (ConstructSelectionEntry is index-based); the renderer's
    //    SurfelTuningState stores the VALUE, so we map value<->index at the edges of the draw.
    const int         CapValues[]  = { 32, 64, 128, 256 };
    const char* const CapLabels[]  = { "32", "64", "128", "256" };
    constexpr int     CapCount     = 4;

    // Value -> index (default to 64's index if the stored value is off the ladder).
    int CapIndexForValue(int Value)
    {
        for (int Index = 0; Index < CapCount; ++Index)
            if (CapValues[Index] == Value)
                return Index;
        return 1;
    }

    // 📝 The sun-shadow sample-count ladder. Same index<->value pattern as the per-cell cap: SelectionEntry edits an INDEX, the tuning state stores the
    //    tap count. 1 is a hard shadow; 8 is the default soft penumbra; 32 is the smooth-but-costly end.
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
}


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void DrawSurfelTuningWindow(const ThemeConfiguration& Theme, SurfelTuningState& State)
{
    // 📝 The caller gates on WindowOpen, but pass &WindowOpen as p_open so the window's own [x] also clears it. WindowBg is pushed to the
    //    theme's desk background — the same wrap ControlsGalleryHost puts around its host window — so the surface behind the cards reads right.
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme.Palette.DeskBackground);
    const bool Visible = ImGui::Begin("Surfel GI — Tuning", &State.WindowOpen);
    if (!Visible)
    {
        ImGui::End();            // collapsed: Begin returned false, but End must still balance the Begin
        ImGui::PopStyleColor();  // and the pushed WindowBg must be popped on every path
        return;
    }

    // -- World-scale knobs: the same ValueSlider control ControlsGallery draws, so the pill + track + drag styling match exactly ------------
    BeginPropertyPanel(Theme, "##surfel-tuning");

    if (BeginPropertyCard(Theme, "World scale (live, whole pipeline)", &State.WorldScaleExpanded))
    {
        // 📝 Ranges chosen so the diagnosed fix (cell ≈ 0.34 m, radius ≈ 0.40 m) sits comfortably inside; ValueSlider is a linear bounded band,
        //    so keep the ceiling modest (2 m) to give the sub-metre end real travel — the coarse end is rarely wanted for this flat, wide scene.
        ValueSliderDescriptor Cell = {};
        Cell.Label = "Cell diameter"; Cell.Value = &State.CellDiameter;
        Cell.Minimum = 0.05f; Cell.Maximum = 2.00f; Cell.Format = "%.3f"; Cell.Unit = "m"; Cell.Enabled = true;
        ConstructValueSlider(Theme, Cell);

        ValueSliderDescriptor Radius = {};
        Radius.Label = "Base radius"; Radius.Value = &State.BaseRadius;
        Radius.Minimum = 0.05f; Radius.Maximum = 2.00f; Radius.Format = "%.3f"; Radius.Unit = "m"; Radius.Enabled = true;
        ConstructValueSlider(Theme, Radius);

        ValueSliderDescriptor Bias = {};
        Bias.Label = "Near-field bias"; Bias.Value = &State.NearFieldBias;
        Bias.Minimum = 0.25f; Bias.Maximum = 4.00f; Bias.Format = "%.2f"; Bias.Unit = "\xC3\x97"; Bias.Enabled = true;   // × multiplier; 1.0 == inert
        ConstructValueSlider(Theme, Bias);

        EndPropertyCard(Theme);
    }

    // -- Per-cell cap: a SelectionEntry (segmented enum) matching ControlsGallery's SelectionEntry look; applies on the toggle below ---------
    if (BeginPropertyCard(Theme, "Per-cell cap (apply on toggle)", &State.PerCellCapExpanded))
    {
        int CapIndex = CapIndexForValue(State.PerCellCapChoice);
        SelectionEntryDescriptor Cap = {};
        Cap.Label = "Cap"; Cap.SelectedIndex = &CapIndex;
        Cap.Options = CapLabels; Cap.OptionCount = CapCount; Cap.Enabled = true;
        if (ConstructSelectionEntry(Theme, Cap))
            State.PerCellCapChoice = CapValues[CapIndex];   // selection edits an index; write the VALUE back into the tuning state

        // 📝 The List buffer is allocated once at the 256 max, so Apply is a pure uniform commit (choice -> applied), not a reallocation — no
        //    device stall. Grey the button until the pending choice differs from what the shaders are currently using.
        const bool CapDirty = (State.PerCellCapChoice != State.PerCellCapApplied);
        if (!CapDirty)
            ImGui::BeginDisabled();
        if (ImGui::Button("Apply cap"))
            State.ApplyCapRequested = true;
        if (!CapDirty)
            ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("applied: %d", State.PerCellCapApplied);

        EndPropertyCard(Theme);
    }

    // -- Sun source (tuning-window override). Elevation/azimuth rewrite the atmosphere profile's solar vector each frame, so the SKY, the surfel
    //    integrate, and the direct shade all follow ONE sun; intensity x colour is premultiplied into the shade's key radiance. Degrees at the UI,
    //    radians in the state — the ×π/180 conversion sits at the slider edges so SunElevation/SunAzimuth stay the shader-native unit. ----------------
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

        // Discrete tap count (1/4/8/16/32), same index<->value pattern as the per-cell cap. More taps = smoother penumbra at higher per-pixel cost.
        int ShadowIndex = ShadowSampleIndexForValue(State.ShadowSampleCount);
        SelectionEntryDescriptor Samples = {};
        Samples.Label = "Samples"; Samples.SelectedIndex = &ShadowIndex;
        Samples.Options = ShadowSampleLabels; Samples.OptionCount = ShadowSampleCount; Samples.Enabled = State.ShadowEnabled;
        if (ConstructSelectionEntry(Theme, Samples))
            State.ShadowSampleCount = ShadowSampleValues[ShadowIndex];

        EndPropertyCard(Theme);
    }

    EndPropertyPanel(Theme);

    ImGui::End();
    ImGui::PopStyleColor();
}

}   // namespace Frontier
