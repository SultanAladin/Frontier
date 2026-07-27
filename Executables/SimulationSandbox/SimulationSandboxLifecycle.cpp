/*==============================================================================================================================================
                                                        SIMULATIONSANDBOXLIFECYCLE.CPP
==============================================================================================================================================*/
// 🧩 Simulation Sandbox entry point — a standalone launcher for the single Simulation workspace. Owns its workspace state, registers the one
//    descriptor on the shared dock, and hands it to the lifecycle host, which stands up the native window, Vulkan host, ImGui relay, theme, and
//    the frame loop. The state is a stack local because it outlives the ExecuteWorkspaceApplication call.

#include "EngineContext/Interface/WorkspaceHost/WorkspaceLifecycleHost.h"
#include "EngineContext/Interface/Workspaces/Simulation/SimulationWorkspace.h"

using namespace Frontier;


//------------------------------------------------------------------------------------------------------------------------
//                                                              MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    (void)ArgumentCount;
    (void)ArgumentValues;

    SimulationWorkspaceState State;

    WorkspaceDescriptor Workspaces[] = { ResolveSimulationWorkspaceDescriptor(State) };

    // A standalone editor opens only its own type; the (+) mints "Simulation 1", "Simulation 2", …
    WorkspaceDocumentType Catalogue[] = { { "Simulation", "Simulation", WorkspaceCategory::Simulation } };

    WorkspaceApplicationSpec Spec;
    Spec.WindowTitle              = "Frontier \xE2\x80\x94 Simulation Sandbox";
    Spec.InitialWidth             = 1600;
    Spec.InitialHeight            = 900;
    Spec.Workspaces               = Workspaces;
    Spec.WorkspaceCount           = 1;
    Spec.DefaultWorkspaceIndex    = 0;
    Spec.DocumentCatalogue        = Catalogue;
    Spec.DocumentCatalogueCount   = 1;
    Spec.DefaultDocumentTypeIndex = 0;

    return ExecuteWorkspaceApplication(Spec);
}
