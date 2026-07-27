/*==============================================================================================================================================
                                                              BAKEPROPERTYPANEL.CPP
==============================================================================================================================================*/
// 🧩 Composes the bake property column from shared cards + Dropdown / BooleanEntry controls over placeholder bake state.

#include "BakePropertyPanel.h"

#include "../../Components/PropertyPanelBase.h"

#include "../../Components/Controls/Dropdown.h"

#include "../../Components/Controls/BooleanEntry.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void ConstructMapToggle(const ThemeConfiguration& Theme, const char* Label, bool* Value)
    {
        BooleanEntryDescriptor Entry = {};
        Entry.Label = Label; Entry.Value = Value; Entry.Enabled = true;
        ConstructBooleanEntry(Theme, Entry);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructBakePropertyPanel(const ThemeConfiguration& Theme, TextureBakeWorkspaceState& State)
{
    BeginPropertyPanel(Theme, "##bake-properties");

    // -- Output card ---------------------------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Output", &State.OutputExpanded))
    {
        static const char* const ResolutionOptions[] = { "512", "1024", "2048", "4096" };
        DropdownDescriptor Resolution = {};
        Resolution.Label = "Resolution"; Resolution.SelectedIndex = &State.ResolutionIndex;
        Resolution.Options = ResolutionOptions; Resolution.OptionCount = 4; Resolution.Enabled = true;
        ConstructDropdown(Theme, Resolution);
        EndPropertyCard(Theme);
    }

    // -- Maps card -----------------------------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Maps", &State.MapsExpanded))
    {
        ConstructMapToggle(Theme, "Ambient Occlusion", &State.BakeAmbientOcclusion);
        ConstructMapToggle(Theme, "Normal",            &State.BakeNormal);
        ConstructMapToggle(Theme, "Curvature",         &State.BakeCurvature);
        EndPropertyCard(Theme);
    }

    EndPropertyPanel(Theme);
}

}   // namespace Frontier
