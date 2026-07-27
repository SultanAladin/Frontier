/*==============================================================================================================================================
                                                                VECTORENTRY.CPP
==============================================================================================================================================*/
// 🧩 A 2/3/4-component drag row in the ControlsPreview.html style: N evenly-split "[ grey axis | black number ]" pills sharing one control row,
//    each carrying its axis letter (X/Y/Z/W) in the left grey cap and a real editable number in the black segment. Stateless; the components
//    live in the caller's contiguous array.

#include "VectorEntry.h"

#include "ControlLayout.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Axis captions for the grey leading cap, indexed 0..3.
    const char* AxisCaption(int Index)
    {
        static const char* const Captions[] = { "X", "Y", "Z", "W" };
        return (Index >= 0 && Index < 4) ? Captions[Index] : "";
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ConstructVectorEntry(const ThemeConfiguration& Theme, const VectorEntryDescriptor& Descriptor)
{
    if (Descriptor.Components == nullptr || Descriptor.ComponentCount < 2 || Descriptor.ComponentCount > 4)
    {
        return false;
    }

    const char* Format = Descriptor.Format ? Descriptor.Format : "%.3f";
    const float Step    = Descriptor.Step > 0.0f ? Descriptor.Step : 0.01f;

    ImGui::PushID(Descriptor.Label);
    if (!Descriptor.Enabled)
    {
        ImGui::BeginDisabled();
    }

    const float FieldWidth = BeginControlRow(Theme, Descriptor.Label);
    const float Height     = ResolvePillHeight(Theme);
    const float Spacing    = Theme.Metrics.ControlSpacing;
    const float CellWidth  = (FieldWidth - Spacing * (Descriptor.ComponentCount - 1)) / Descriptor.ComponentCount;

    const ImVec2 Origin = ImGui::GetCursorScreenPos();

    bool Changed = false;
    for (int Index = 0; Index < Descriptor.ComponentCount; ++Index)
    {
        const ImVec2 PillPos(Origin.x + Index * (CellWidth + Spacing), Origin.y);
        const ValuePillLayout Pill = DrawValuePill(Theme, PillPos, ImVec2(CellWidth, Height),
                                                   AxisCaption(Index), nullptr, Descriptor.Enabled);
        ImGui::PushID(Index);
        Changed |= EditPillNumber(Theme, Pill, "num", &Descriptor.Components[Index], Step, 0.0f, 0.0f, Format);
        ImGui::PopID();
    }

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
