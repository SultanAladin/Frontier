/*==============================================================================================================================================
                                                              UVPROPERTYPANEL.CPP
==============================================================================================================================================*/
// 🧩 Composes the UV property column from shared PropertyPanelBase cards + ValueSlider / BooleanEntry controls over placeholder UV state.

#include "UVPropertyPanel.h"

#include "../../Components/PropertyPanelBase.h"

#include "../../Components/Controls/ValueSlider.h"

#include "../../Components/Controls/BooleanEntry.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructUVPropertyPanel(const ThemeConfiguration& Theme, UVWorkspaceState& State)
{
    BeginPropertyPanel(Theme, "##uv-properties");

    // -- Layout card ---------------------------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Layout", &State.LayoutExpanded))
    {
        ValueSliderDescriptor Margin = {};
        Margin.Label   = "Pack Margin";
        Margin.Value   = &State.PackMargin;
        Margin.Minimum = 0.0f;
        Margin.Maximum = 0.25f;
        Margin.Format  = "%.3f";
        Margin.Enabled = true;
        ConstructValueSlider(Theme, Margin);
        EndPropertyCard(Theme);
    }

    // -- Display card --------------------------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Display", &State.DisplayExpanded))
    {
        BooleanEntryDescriptor Seams = {};
        Seams.Label   = "Show Seams";
        Seams.Value   = &State.ShowSeams;
        Seams.Enabled = true;
        ConstructBooleanEntry(Theme, Seams);
        EndPropertyCard(Theme);
    }

    EndPropertyPanel(Theme);
}

}   // namespace Frontier
