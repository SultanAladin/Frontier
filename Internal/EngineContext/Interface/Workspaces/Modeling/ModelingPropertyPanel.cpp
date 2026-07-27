/*==============================================================================================================================================
                                                              MODELINGPROPERTYPANEL.CPP
==============================================================================================================================================*/
// 🧩 Composes the modeling property column from shared pieces: PropertyPanelBase cards + VectorEntry / SelectionEntry / BooleanEntry controls.
//    Purely a binding of shared controls to workspace state — the "blank UI" here means real cards + real controls over placeholder values.

#include "ModelingPropertyPanel.h"

#include "../../Components/PropertyPanelBase.h"

#include "../../Components/Controls/VectorEntry.h"

#include "../../Components/Controls/SelectionEntry.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Record one XYZ vector row bound to a 3-float array.
    void ConstructVectorRow(const ThemeConfiguration& Theme, const char* Label, float* Components, float Step)
    {
        VectorEntryDescriptor Entry = {};
        Entry.Label          = Label;
        Entry.Components      = Components;
        Entry.ComponentCount  = 3;
        Entry.Step            = Step;
        Entry.Format          = "%.2f";
        Entry.Enabled         = true;
        ConstructVectorEntry(Theme, Entry);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructModelingPropertyPanel(const ThemeConfiguration& Theme, ModelingWorkspaceState& State)
{
    BeginPropertyPanel(Theme, "##modeling-properties");

    // -- Transform card ------------------------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Transform", &State.TransformExpanded))
    {
        ConstructVectorRow(Theme, "Position", State.TransformPosition, 1.0f);
        ConstructVectorRow(Theme, "Rotation", State.TransformRotation, 0.5f);
        ConstructVectorRow(Theme, "Scale",    State.TransformScale,    0.01f);
        EndPropertyCard(Theme);
    }

    // -- Geometry card -------------------------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Geometry", &State.GeometryExpanded))
    {
        static const char* const ShadeOptions[] = { "Flat", "Smooth", "Auto" };
        static int ShadeIndex = 1;
        SelectionEntryDescriptor Shade = {};
        Shade.Label         = "Shading";
        Shade.SelectedIndex = &ShadeIndex;
        Shade.Options       = ShadeOptions;
        Shade.OptionCount   = 3;
        Shade.Enabled       = true;
        ConstructSelectionEntry(Theme, Shade);
        EndPropertyCard(Theme);
    }

    // -- Shading card --------------------------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Shading", &State.ShadingExpanded))
    {
        ImGui::TextDisabled("Material bindings — wired with Geometry.");
        EndPropertyCard(Theme);
    }

    EndPropertyPanel(Theme);
}

}   // namespace Frontier
