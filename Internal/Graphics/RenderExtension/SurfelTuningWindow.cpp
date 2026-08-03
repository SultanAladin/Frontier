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

#include "imgui.h"

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

    EndPropertyPanel(Theme);

    ImGui::End();
    ImGui::PopStyleColor();
}

}   // namespace Frontier
