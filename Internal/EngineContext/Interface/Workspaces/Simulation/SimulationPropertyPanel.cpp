/*==============================================================================================================================================
                                                           SIMULATIONPROPERTYPANEL.CPP
==============================================================================================================================================*/
// 🧩 Composes the simulation property column from a single shared Playback card — run toggle + timestep + gravity sliders over the placeholder
//    simulation state. Real ticking lands with the Physics extension; the controls are live and bound now.

#include "SimulationPropertyPanel.h"

#include "../../Components/PropertyPanelBase.h"

#include "../../Components/Controls/BooleanEntry.h"

#include "../../Components/Controls/ValueSlider.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructSimulationPropertyPanel(const ThemeConfiguration& Theme, SimulationWorkspaceState& State)
{
    BeginPropertyPanel(Theme, "##simulation-properties");

    if (BeginPropertyCard(Theme, "Playback", &State.PlaybackExpanded))
    {
        BooleanEntryDescriptor Running = {};
        Running.Label = "Running"; Running.Value = &State.RunningEnabled; Running.Enabled = true;
        ConstructBooleanEntry(Theme, Running);

        ValueSliderDescriptor Timestep = {};
        Timestep.Label = "Timestep"; Timestep.Value = &State.Timestep;
        Timestep.Minimum = 1.0f; Timestep.Maximum = 33.0f; Timestep.Format = "%.1f"; Timestep.Unit = "ms";
        Timestep.Enabled = true;
        ConstructValueSlider(Theme, Timestep);

        ValueSliderDescriptor Gravity = {};
        Gravity.Label = "Gravity"; Gravity.Value = &State.GravityStrength;
        Gravity.Minimum = 0.0f; Gravity.Maximum = 2000.0f; Gravity.Format = "%.0f"; Gravity.Unit = "cm/s\xC2\xB2";
        Gravity.Enabled = true;
        ConstructValueSlider(Theme, Gravity);

        EndPropertyCard(Theme);
    }

    EndPropertyPanel(Theme);
}

}   // namespace Frontier
