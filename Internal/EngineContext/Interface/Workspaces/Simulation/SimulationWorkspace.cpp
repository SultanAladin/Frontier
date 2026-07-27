/*==============================================================================================================================================
                                                             SIMULATIONWORKSPACE.CPP
==============================================================================================================================================*/
// 🧩 Wires the shared viewport + a slim property column into the minimal simulation workspace. ActivateRegistry registers only two panels —
//    viewport and properties — leaving the outliner and control-strip regions empty (the dock host tolerates a region with no panel). This is
//    the "viewport-only" workspace SimulationSandbox opens; no new chrome is defined here.

#include "SimulationWorkspace.h"

#include "SimulationPropertyPanel.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void ConstructViewport(const ThemeConfiguration& Theme, void* Context)
    {
        SimulationWorkspaceState& State = *static_cast<SimulationWorkspaceState*>(Context);
        ConstructViewportPanel(Theme, State.Viewport);
    }

    void ConstructProperties(const ThemeConfiguration& Theme, void* Context)
    {
        SimulationWorkspaceState& State = *static_cast<SimulationWorkspaceState*>(Context);
        ConstructSimulationPropertyPanel(Theme, State);
    }

    // 📝 Registry activation: viewport + properties only. Outliner + control regions stay empty for this workspace.
    void ActivateSimulationRegistry(PanelRegistry& Registry, void* Context)
    {
        ResetPanelRegistry(Registry);

        PanelRegistration Viewport = {};
        Viewport.Identifier = "simulation.viewport"; Viewport.Title = "Viewport";
        Viewport.Region = PanelDockRegion::Viewport; Viewport.Construct = &ConstructViewport; Viewport.Context = Context;
        RegisterPanel(Registry, Viewport);

        PanelRegistration Properties = {};
        Properties.Identifier = "simulation.properties"; Properties.Title = "Simulation";
        Properties.Region = PanelDockRegion::RightProperties; Properties.Construct = &ConstructProperties; Properties.Context = Context;
        RegisterPanel(Registry, Properties);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeSimulationWorkspace(SimulationWorkspaceState& State)
{
    InitializeViewportPanelState(State.Viewport, ViewportProjection::Perspective);
    State.PlaybackExpanded = true;
    State.RunningEnabled   = false;
    State.Timestep         = 16.7f;
    State.GravityStrength  = 981.0f;
}


WorkspaceDescriptor ResolveSimulationWorkspaceDescriptor(SimulationWorkspaceState& State)
{
    WorkspaceDescriptor Descriptor = {};
    Descriptor.Identifier       = "Simulation";
    Descriptor.Title            = "Simulation";
    Descriptor.ActivateRegistry = &ActivateSimulationRegistry;
    Descriptor.Context          = &State;
    return Descriptor;
}

}   // namespace Frontier
