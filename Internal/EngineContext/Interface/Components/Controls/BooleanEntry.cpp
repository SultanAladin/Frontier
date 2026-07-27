/*==============================================================================================================================================
                                                              BOOLEANENTRY.CPP
==============================================================================================================================================*/
// 🧩 A true/false toggle drawn as a sliding switch (ControlsPreview.html): a pill track that is grey when off and accent-blue when on, with a
//    white nub that slides between the two ends. Clicking anywhere on the switch flips it. The nub position AND the track colour ease toward
//    the target over a short time-constant, so a flip animates with a subtle micro-slide instead of snapping. Stateless to the caller (the flag
//    lives in the caller's struct); only the eased 0..1 animation phase lives in ImGui per-widget storage.

#include "BooleanEntry.h"

#include "ControlLayout.h"

#include "imgui.h"

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Frame-rate-independent exponential ease: close ~Rate of the gap per second. Stable at any frame time.
    float EaseToward(float Current, float Target, float Rate, float DeltaTime)
    {
        if (DeltaTime <= 0.0f)
        {
            return Target;
        }
        return Current + (Target - Current) * (1.0f - std::exp(-Rate * DeltaTime));
    }

    // 📝 Lerp two packed ImU32 colours channel-wise at T in [0,1].
    ImU32 LerpColor(ImU32 A, ImU32 B, float T)
    {
        const ImVec4 Va = ImGui::ColorConvertU32ToFloat4(A);
        const ImVec4 Vb = ImGui::ColorConvertU32ToFloat4(B);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(Va.x + (Vb.x - Va.x) * T,
                                                     Va.y + (Vb.y - Va.y) * T,
                                                     Va.z + (Vb.z - Va.z) * T,
                                                     Va.w + (Vb.w - Va.w) * T));
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ConstructBooleanEntry(const ThemeConfiguration& Theme, const BooleanEntryDescriptor& Descriptor)
{
    if (Descriptor.Value == nullptr)
    {
        return false;
    }

    ImGui::PushID(Descriptor.Label);
    if (!Descriptor.Enabled)
    {
        ImGui::BeginDisabled();
    }

    BeginControlRow(Theme, Descriptor.Label);

    // 📝 A fixed-size switch left-aligned in the field column; its height tracks the themed row so it lines up with other controls.
    const float  TrackH = ResolvePillHeight(Theme) * 0.62f;
    const float  TrackW = TrackH * 1.85f;
    const float  Pad    = TrackH * 0.12f;
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const ImVec2 Min    = Origin;
    const ImVec2 Max(Origin.x + TrackW, Origin.y + TrackH);

    ImDrawList* Draw = ImGui::GetWindowDrawList();

    const bool On = *Descriptor.Value;

    // 📝 Ease a 0..1 phase toward the target end each frame; drives BOTH the nub slide and the track colour blend.
    ImGuiStorage* Store   = ImGui::GetStateStorage();
    const ImGuiID PhaseKey= ImGui::GetID("##phase");
    const ImGuiID SeedKey = ImGui::GetID("##seed");
    const float   Target  = On ? 1.0f : 0.0f;
    if (Store->GetInt(SeedKey, 0) == 0)
    {
        Store->SetFloat(PhaseKey, Target);   // seed instantly so a fresh switch doesn't animate from the middle
        Store->SetInt(SeedKey, 1);
    }
    float Phase = EaseToward(Store->GetFloat(PhaseKey, Target), Target, 20.0f, ImGui::GetIO().DeltaTime);
    if (std::fabs(Phase - Target) < 0.001f)
    {
        Phase = Target;   // settle crisply
    }
    Store->SetFloat(PhaseKey, Phase);

    // 📝 Track colour blends grey (off) -> accent (on) across the phase.
    const ImU32 Track = LerpColor(Theme.Palette.ControlActive, Theme.Palette.AccentPrimary, Phase);
    Draw->AddRectFilled(Min, Max, Track, TrackH * 0.5f);

    // 📝 White nub, sliding between the off and on ends by the eased phase.
    const float  NubR    = (TrackH - Pad * 2.0f) * 0.5f;
    const float  NubOff  = Min.x + Pad + NubR;
    const float  NubOn   = Max.x - Pad - NubR;
    const ImVec2 Nub(NubOff + (NubOn - NubOff) * Phase, (Min.y + Max.y) * 0.5f);
    Draw->AddCircleFilled(Nub, NubR, Theme.Palette.SliderKnob, 24);
    Draw->AddCircle(Nub, NubR, ImGui::GetColorU32(IM_COL32(0, 0, 0, 90)), 24, 1.0f);

    // 📝 Click hit-test over the whole switch.
    bool Changed = false;
    ImGui::SetCursorScreenPos(Min);
    if (ImGui::InvisibleButton("##switch", ImVec2(TrackW, TrackH)) && Descriptor.Enabled)
    {
        *Descriptor.Value = !*Descriptor.Value;
        Changed = true;
    }

    EndControlRow(Theme);

    if (!Descriptor.Enabled)
    {
        ImGui::EndDisabled();
    }
    ImGui::PopID();

    return Changed && Descriptor.Enabled;
}

}   // namespace Frontier
