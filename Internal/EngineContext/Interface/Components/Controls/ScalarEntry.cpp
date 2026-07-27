/*==============================================================================================================================================
                                                                SCALARENTRY.CPP
==============================================================================================================================================*/
// 🧩 A numeric row in the ControlsPreview.html style: a "[ black number | grey unit ]" editable value pill on the left and a black/grey/white
//    slider on the right that LOOKS and BEHAVES exactly like ValueSlider's — a real filling track with a positional knob you click or drag to
//    set the value (left = lower, right = higher). When the descriptor is bounded (Minimum != Maximum) that range drives the slider directly.
//    When it is unbounded the slider adopts a STABLE range [0 .. top] anchored once from the first value and only ever grown to keep the value
//    reachable — it never re-centres or shrinks, so the knob stays where you leave it (no snap-back). Stateless to the caller: the true value
//    lives in the caller's struct; only the range top lives in ImGui per-widget storage.

#include "ScalarEntry.h"

#include "ControlLayout.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    float Clamp(float V, float Lo, float Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ConstructScalarEntry(const ThemeConfiguration& Theme, const ScalarEntryDescriptor& Descriptor)
{
    if (Descriptor.Value == nullptr)
    {
        return false;
    }

    const char* Format = Descriptor.Format ? Descriptor.Format : "%.3f";
    const float Step    = Descriptor.Step > 0.0f ? Descriptor.Step : 0.01f;
    const bool  Bounded = Descriptor.Minimum != Descriptor.Maximum;

    ImGui::PushID(Descriptor.Label);
    if (!Descriptor.Enabled)
    {
        ImGui::BeginDisabled();
    }

    // 📝 Field column split: value pill (~56%) then the slider filling the rest — same rhythm as ValueSlider.
    const float FieldWidth = BeginControlRow(Theme, Descriptor.Label);
    const float Height     = ResolvePillHeight(Theme);
    const float Gap        = Theme.Metrics.ControlSpacing * 2.0f;
    const float PillWidth  = FieldWidth * 0.56f;
    const float TrackWidth = FieldWidth - PillWidth - Gap;
    const float TrackSpan  = TrackWidth > 1.0f ? TrackWidth : 1.0f;

    const ImVec2 Origin = ImGui::GetCursorScreenPos();

    // -- Value pill (editable, drag-scrub) -----------------------------------------------------------------------------
    const ValuePillLayout Pill = DrawValuePill(Theme, Origin, ImVec2(PillWidth, Height),
                                               nullptr, Descriptor.Unit, Descriptor.Enabled);
    bool Changed = EditPillNumber(Theme, Pill, "num", Descriptor.Value, Step,
                                  Descriptor.Minimum, Descriptor.Maximum, Format);

    // -- Resolve the slider's working range ----------------------------------------------------------------------------
    // 📝 The slider behaves EXACTLY like ValueSlider: a fixed [RangeLo, RangeHi] maps pointer-X straight to the value (left = lower,
    //    right = higher) and the knob stays where you leave it — it never snaps back. For a bounded descriptor the range is the descriptor's
    //    own Min/Max. For an unbounded one we adopt a STABLE range [0 .. RangeHi] anchored once from the seed value; it only GROWS if the
    //    value is dragged/typed past the current top (so the knob can always reach it), and never shrinks or re-centres.
    ImGuiStorage* Store   = ImGui::GetStateStorage();
    const ImGuiID SeedKey = ImGui::GetID("##seed");
    const ImGuiID HiKey   = ImGui::GetID("##rangehi");

    const float TrackHNow = Height * 0.72f;
    const ImVec2 TrackPos(Origin.x + PillWidth + Gap, Origin.y + (Height - TrackHNow) * 0.5f);

    ImGui::SetCursorScreenPos(TrackPos);
    ImGui::InvisibleButton("##track", ImVec2(TrackSpan, TrackHNow));
    const bool TrackActive = Descriptor.Enabled && ImGui::IsItemActive();

    float RangeLo, RangeHi;
    if (Bounded)
    {
        RangeLo = Descriptor.Minimum;
        RangeHi = Descriptor.Maximum;
    }
    else
    {
        RangeLo = 0.0f;
        // 📝 Seed a sensible top from the initial value (at least a few steps of headroom), then only ever grow it.
        if (Store->GetInt(SeedKey, 0) == 0)
        {
            const float Seed = *Descriptor.Value > 0.0f ? *Descriptor.Value * 2.0f : 1.0f;
            const float Floor = Step * 400.0f > 1.0f ? Step * 400.0f : 1.0f;
            Store->SetFloat(HiKey, Seed > Floor ? Seed : Floor);
            Store->SetInt(SeedKey, 1);
        }
        RangeHi = Store->GetFloat(HiKey, 1.0f);
        if (*Descriptor.Value > RangeHi)   // grow to keep the knob reachable; never shrink (no snap-back)
        {
            RangeHi = *Descriptor.Value;
            Store->SetFloat(HiKey, RangeHi);
        }
    }
    const float RangeSpan = RangeHi - RangeLo > 1e-6f ? RangeHi - RangeLo : 1e-6f;

    // 📝 Drag maps pointer X directly to a value along the fixed range — identical to ValueSlider's track handling.
    if (TrackActive)
    {
        const float RawFrac  = (ImGui::GetIO().MousePos.x - TrackPos.x) / TrackSpan;
        const float NewValue = RangeLo + Clamp(RawFrac, 0.0f, 1.0f) * RangeSpan;
        if (NewValue != *Descriptor.Value)
        {
            *Descriptor.Value = NewValue;
            Changed = true;
        }
    }

    // -- Slider track (filling, positional knob) — identical look to ValueSlider ----------------------------------------
    const float Fraction = Clamp((*Descriptor.Value - RangeLo) / RangeSpan, 0.0f, 1.0f);
    DrawSliderTrack(Theme, TrackPos, ImVec2(TrackWidth, TrackHNow), Fraction, Descriptor.Enabled);

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + Height));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    EndControlRow(Theme);

    if (!Descriptor.Enabled)
    {
        ImGui::EndDisabled();
    }
    ImGui::PopID();

    return Changed && Descriptor.Enabled;
}

}   // namespace Frontier
