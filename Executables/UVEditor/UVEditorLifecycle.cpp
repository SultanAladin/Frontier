/*==============================================================================================================================================
                                                            UVEDITORLIFECYCLE.CPP
==============================================================================================================================================*/
// 🧩 UV Editor entry point — a standalone launcher for the single UV workspace. Owns its workspace state, registers the one descriptor on the
//    shared dock, and hands it to the lifecycle host, which stands up the native window, Vulkan host, ImGui relay, theme, and the frame loop.
//    The state is a stack local because it outlives the ExecuteWorkspaceApplication call.

#include "EngineContext/Interface/WorkspaceHost/WorkspaceLifecycleHost.h"
#include "EngineContext/Interface/Workspaces/UV/UVWorkspace.h"

using namespace Frontier;


//------------------------------------------------------------------------------------------------------------------------
//                                                              MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    (void)ArgumentCount;
    (void)ArgumentValues;

    UVWorkspaceState State;

    WorkspaceDescriptor Workspaces[] = { ResolveUVWorkspaceDescriptor(State) };

    // A standalone editor opens only its own type; the (+) mints "UV Layout 1", "UV Layout 2", …
    WorkspaceDocumentType Catalogue[] = { { "UV Layout", "UV Layout", WorkspaceCategory::UV } };

    WorkspaceApplicationSpec Spec;
    Spec.WindowTitle              = "Frontier \xE2\x80\x94 UV Editor";
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
