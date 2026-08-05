/*============================================================================================================================================
                                                         RIGIDBODYCONTROLWINDOW.CPP
============================================================================================================================================*/
// 🧩 The control window, recorded from the shared Interface components exactly as ControlsGallery and the lighting-tuning window do:
//    BeginPropertyPanel -> BeginPropertyCard -> ConstructValueSlider / ConstructBooleanEntry. The gallery look does NOT come from the global ImGui
//    style, it comes from those components pushing Theme.Palette tones, which is why nothing here styles a widget itself.

#include "RigidBodyControlWindow.h"

#include "EngineContext/Interface/Components/PropertyPanelBase.h"
#include "EngineContext/Interface/Components/Controls/ValueSlider.h"
#include "EngineContext/Interface/Components/Controls/BooleanEntry.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CARD COLLAPSE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Card collapse is caller-owned so it survives across cycles. This window is a singleton within the process, so file-local persistence is the
//    honest scope — there is no second instance whose collapse could alias this one.
static bool SolverCardExpanded  = true;
static bool ContactCardExpanded = true;
static bool ReadoutCardExpanded = true;

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

RigidBodyWindowOutcome DrawRigidBodyControlWindow(const ThemeConfiguration& Theme, RigidBodyTuning& Tuning, const RigidBodyReadout& Readout)
{
    RigidBodyWindowOutcome Outcome = {};

    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Rigid body — drop validation"))
    {
        ImGui::End();   // collapsed: Begin returned false, but End must still balance the Begin
        return Outcome;
    }

    BeginPropertyPanel(Theme, "##rigid-body-drop");

    // --- Solver ------------------------------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Solver", &SolverCardExpanded))
    {
        BooleanEntryDescriptor AdvanceRow = {};
        AdvanceRow.Label   = "Advancing";
        AdvanceRow.Value   = &Tuning.Advancing;
        AdvanceRow.Enabled = true;
        ConstructBooleanEntry(Theme, AdvanceRow);

        ValueSliderDescriptor GravityRow = {};
        GravityRow.Label   = "Gravity";
        GravityRow.Value   = &Tuning.GravityStrength;
        GravityRow.Minimum = -20.0f;   // negative lifts the scene, which is the fastest way to see the solve is live
        GravityRow.Maximum = 40.0f;
        GravityRow.Format  = "%.2f";
        GravityRow.Unit    = "m/s2";
        GravityRow.Enabled = true;
        ConstructValueSlider(Theme, GravityRow);

        ValueSliderDescriptor StepRow = {};
        StepRow.Label   = "Step";
        StepRow.Value   = &Tuning.StepSeconds;
        StepRow.Minimum = 1.0f / 240.0f;
        StepRow.Maximum = 1.0f / 20.0f;
        StepRow.Format  = "%.4f";
        StepRow.Unit    = "s";
        StepRow.Enabled = true;
        ConstructValueSlider(Theme, StepRow);

        // Substeps is an integer knob, but the shared gallery has no integer row — so it rides a float slider and is narrowed on read. Held as a
        // float mirror rather than casting the live int in place, which would quantise the slider's own drag and make it feel sticky.
        static float SubstepMirror = 1.0f;
        SubstepMirror = (float)Tuning.Substeps;
        ValueSliderDescriptor SubstepRow = {};
        SubstepRow.Label   = "Substeps";
        SubstepRow.Value   = &SubstepMirror;
        SubstepRow.Minimum = 1.0f;
        SubstepRow.Maximum = 8.0f;
        SubstepRow.Format  = "%.0f";
        SubstepRow.Unit    = nullptr;
        SubstepRow.Enabled = true;
        if (ConstructValueSlider(Theme, SubstepRow))
            Tuning.Substeps = (int32_t)(SubstepMirror + 0.5f);

        // Step is only meaningful while held still; advancing already consumes intervals every frame.
        if (ImGui::Button("Step once", ImVec2(-1.0f, 0.0f)) && !Tuning.Advancing)
            Outcome.SingleAdvance = true;
        if (ImGui::Button("Reset to authored scene", ImVec2(-1.0f, 0.0f)))
            Outcome.ReseedRequested = true;

        EndPropertyCard(Theme);
    }

    // --- Contact -----------------------------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Contact", &ContactCardExpanded))
    {
        ValueSliderDescriptor FrictionRow = {};
        FrictionRow.Label   = "Friction";
        FrictionRow.Value   = &Tuning.Friction;
        FrictionRow.Minimum = 0.0f;
        FrictionRow.Maximum = 1.5f;
        FrictionRow.Format  = "%.2f";
        FrictionRow.Unit    = nullptr;
        FrictionRow.Enabled = true;
        ConstructValueSlider(Theme, FrictionRow);

        ValueSliderDescriptor RestitutionRow = {};
        RestitutionRow.Label   = "Restitution";
        RestitutionRow.Value   = &Tuning.Restitution;
        RestitutionRow.Minimum = 0.0f;
        RestitutionRow.Maximum = 0.95f;
        RestitutionRow.Format  = "%.2f";
        RestitutionRow.Unit    = nullptr;
        RestitutionRow.Enabled = true;
        ConstructValueSlider(Theme, RestitutionRow);

        // ⚠️ Both are body properties, applied at construction and at reseed — a live edit does NOT reach bodies already in the world.
        ImGui::TextDisabled("Applied on Reset.");

        EndPropertyCard(Theme);
    }

    // --- Readout -----------------------------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Readout", &ReadoutCardExpanded))
    {
        ImGui::Text("Bodies          %u", (unsigned)Readout.BodyCount);
        ImGui::Text("Awake           %u", (unsigned)Readout.AwakeCount);
        ImGui::Text("Kinetic energy  %.2f J", (double)Readout.KineticEnergy);
        // 📝 An empty world publishes a quiet NaN for the tallest body (there is no tallest one), which %.3f would render as a bare "nan". Draw a dash
        //    instead: "no bodies" and "a body resting at 0.000 m" are genuinely different readings and must not print alike.
        if (Readout.HighestCrate == Readout.HighestCrate)   // false only for NaN
            ImGui::Text("Highest crate   %.3f m", (double)Readout.HighestCrate);
        else
            ImGui::Text("Highest crate   \xE2\x80\x94");   // —
        ImGui::Text("Simulated       %.2f s", (double)Readout.ElapsedSeconds);
        ImGui::Text("Steps           %u", (unsigned)Readout.AdvanceOrdinal);

        // The settle verdict, which is what this validation is for: every body asleep means the collapse resolved into a stable pile rather than
        // jittering forever, which is the failure a stacking solve actually exhibits.
        if (Readout.BodyCount > 0u && Readout.AwakeCount == 0u)
            ImGui::TextColored(ImVec4(0.42f, 0.78f, 0.45f, 1.0f), "settled");
        else
            ImGui::TextColored(ImVec4(0.85f, 0.72f, 0.36f, 1.0f), "in motion");

        EndPropertyCard(Theme);
    }

    EndPropertyPanel(Theme);
    ImGui::End();
    return Outcome;
}

} // namespace Frontier
