/*==============================================================================================================================================
                                                              SKETCHPROPERTYPANEL.CPP
==============================================================================================================================================*/
// 🧩 Composes the sketcher property column from shared cards + Dropdown / ScalarEntry controls over placeholder constraint state.

#include "SketchPropertyPanel.h"

#include "../../Components/PropertyPanelBase.h"

#include "../../Components/Controls/Dropdown.h"

#include "../../Components/Controls/ScalarEntry.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructSketchPropertyPanel(const ThemeConfiguration& Theme, ParametricSketcherWorkspaceState& State)
{
    BeginPropertyPanel(Theme, "##sketch-properties");

    // -- Constraint card -----------------------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Constraint", &State.ConstraintExpanded))
    {
        static const char* const ConstraintOptions[] =
            { "Coincident", "Parallel", "Perpendicular", "Horizontal", "Vertical", "Equal", "Tangent" };
        DropdownDescriptor Category = {};
        Category.Label = "Category"; Category.SelectedIndex = &State.ConstraintIndex;
        Category.Options = ConstraintOptions; Category.OptionCount = 7; Category.Enabled = true;
        ConstructDropdown(Theme, Category);
        EndPropertyCard(Theme);
    }

    // -- Dimension card ------------------------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Dimension", &State.DimensionExpanded))
    {
        ScalarEntryDescriptor Value = {};
        Value.Label = "Value"; Value.Value = &State.DimensionValue;
        Value.Step = 0.1f; Value.Minimum = 0.0f; Value.Maximum = 0.0f; Value.Format = "%.2f"; Value.Unit = "mm"; Value.Enabled = true;
        ConstructScalarEntry(Theme, Value);
        EndPropertyCard(Theme);
    }

    EndPropertyPanel(Theme);
}

}   // namespace Frontier
