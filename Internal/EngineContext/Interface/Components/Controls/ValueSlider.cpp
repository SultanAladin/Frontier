/*==============================================================================================================================================
                                                                VALUESLIDER.CPP
==============================================================================================================================================*/
// 🧩 A bounded value row in the ControlsPreview.html style: a "[ black number | grey unit ]" value pill on the left and a black/grey/white
//    slider track on the right, sharing one control row. The number pill is a REAL editable field (drag-scrub, double-click-select, type),
//    and the track is draggable. Stateless: the edited value lives in the caller's struct.

#include "ValueSlider.h"

#include "ControlLayout.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ConstructValueSlider(const ThemeConfiguration& Theme, const ValueSliderDescriptor& Descriptor)
{
    if (Descriptor.Value == nullptr || Descriptor.Maximum <= Descriptor.Minimum)
    {
        return false;
    }

    const char* Format = Descriptor.Format ? Descriptor.Format : "%.3f";

    ImGui::PushID(Descriptor.Label);
    if (!Descriptor.Enabled)
    {
        ImGui::BeginDisabled();
    }

    // 📝 Field column split: a value pill (~40%) then a gap then the slider track filling the rest.
    const float FieldWidth = BeginControlRow(Theme, Descriptor.Label);
    const float Height     = ResolvePillHeight(Theme);
    const float Gap        = Theme.Metrics.ControlSpacing * 2.0f;
    const float PillWidth  = FieldWidth * 0.42f;
    const float TrackWidth = FieldWidth - PillWidth - Gap;

    const ImVec2 Origin = ImGui::GetCursorScreenPos();

    // -- Value pill (editable number + unit cap) -----------------------------------------------------------------------
    const ValuePillLayout Pill = DrawValuePill(Theme, Origin, ImVec2(PillWidth, Height),
                                               nullptr, Descriptor.Unit, Descriptor.Enabled);
    // 📝 Editing inside the bounds; step chosen so a full pill drag spans the range comfortably.
    const float DragSpeed = (Descriptor.Maximum - Descriptor.Minimum) / 400.0f;
    bool Changed = EditPillNumber(Theme, Pill, "num", Descriptor.Value, DragSpeed,
                                  Descriptor.Minimum, Descriptor.Maximum, Format);

    // -- Slider track --------------------------------------------------------------------------------------------------
    const float  TrackH   = Height * 0.72f;
    const ImVec2 TrackPos(Origin.x + PillWidth + Gap, Origin.y + (Height - TrackH) * 0.5f);
    const float  Fraction = (*Descriptor.Value - Descriptor.Minimum) / (Descriptor.Maximum - Descriptor.Minimum);
    DrawSliderTrack(Theme, TrackPos, ImVec2(TrackWidth, TrackH), Fraction, Descriptor.Enabled);

    // 📝 An invisible button over the track owns the drag: press or drag maps the pointer X to the value.
    ImGui::SetCursorScreenPos(TrackPos);
    ImGui::InvisibleButton("##track", ImVec2(TrackWidth > 1.0f ? TrackWidth : 1.0f, TrackH));
    if (Descriptor.Enabled && ImGui::IsItemActive())
    {
        const float MouseX   = ImGui::GetIO().MousePos.x;
        const float RawFrac  = (MouseX - TrackPos.x) / (TrackWidth > 1.0f ? TrackWidth : 1.0f);
        const float ClampedT = RawFrac < 0.0f ? 0.0f : (RawFrac > 1.0f ? 1.0f : RawFrac);
        const float NewValue = Descriptor.Minimum + ClampedT * (Descriptor.Maximum - Descriptor.Minimum);
        if (NewValue != *Descriptor.Value)
        {
            *Descriptor.Value = NewValue;
            Changed = true;
        }
    }

    // 📝 Advance the ImGui cursor past the taller-than-default row so the next control stacks correctly.
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
