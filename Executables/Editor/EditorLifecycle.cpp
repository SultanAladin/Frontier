/*==============================================================================================================================================
                                                             EDITORLIFECYCLE.CPP
==============================================================================================================================================*/
// 🧩 Editor entry point — the full application. Owns one instance of every workspace's state, registers all six on the shared dock through the
//    lifecycle host, and opens Modeling first. The host (WorkspaceLifecycleHost) stands up the native window, Vulkan host, ImGui relay, theme,
//    and the crash-safe frame loop; this file only names the window and hands over the workspace roster. Each workspace state is a stack local
//    here because it outlives the ExecuteWorkspaceApplication call (the descriptors point at it).

#include "EngineContext/Interface/WorkspaceHost/WorkspaceLifecycleHost.h"

#include "EngineContext/Interface/Workspaces/Modeling/ModelingWorkspace.h"
#include "EngineContext/Interface/Workspaces/UV/UVWorkspace.h"
#include "EngineContext/Interface/Workspaces/TexturePaint/TexturePaintWorkspace.h"
#include "EngineContext/Interface/Workspaces/TextureBake/TextureBakeWorkspace.h"
#include "EngineContext/Interface/Workspaces/Simulation/SimulationWorkspace.h"
#include "EngineContext/Interface/Workspaces/ParametricSketcher/ParametricSketcherWorkspace.h"

using namespace Frontier;


//------------------------------------------------------------------------------------------------------------------------
//                                                              MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    (void)ArgumentCount;
    (void)ArgumentValues;

    // -- Per-workspace state (caller-owned; outlives the host call) -----------------------------------------------------
    ModelingWorkspaceState           ModelingState;
    UVWorkspaceState                 UVState;
    TexturePaintWorkspaceState       PaintState;
    TextureBakeWorkspaceState        BakeState;
    SimulationWorkspaceState         SimulationState;
    ParametricSketcherWorkspaceState SketcherState;

    // -- The workspace roster (Modeling first, then the rest as tabs) ---------------------------------------------------
    WorkspaceDescriptor Workspaces[] =
    {
        ResolveModelingWorkspaceDescriptor(ModelingState),
        ResolveUVWorkspaceDescriptor(UVState),
        ResolveTexturePaintWorkspaceDescriptor(PaintState),
        ResolveTextureBakeWorkspaceDescriptor(BakeState),
        ResolveSimulationWorkspaceDescriptor(SimulationState),
        ResolveParametricSketcherWorkspaceDescriptor(SketcherState),
    };

    // -- The interior (+) catalogue: the Editor is the only host that can open ANY of the six workspace types, each numbered per stem -----------
    WorkspaceDocumentType Catalogue[] =
    {
        { "Modeling Workspace",   "ModelingWorkspace",     WorkspaceCategory::Modeling   },
        { "UV Layout",            "UV Layout",             WorkspaceCategory::UV         },
        { "Paint Workspace",      "PaintWorkspace",        WorkspaceCategory::Painting   },
        { "TextureBake Workspace","TextureBake Workspace", WorkspaceCategory::Baking     },
        { "Simulation",           "Simulation",            WorkspaceCategory::Simulation },
        { "Sketch",               "Sketch",                WorkspaceCategory::Draughting },
    };

    WorkspaceApplicationSpec Spec;
    Spec.WindowTitle              = "Frontier \xE2\x80\x94 Editor";
    Spec.InitialWidth             = 1600;
    Spec.InitialHeight            = 900;
    Spec.Workspaces               = Workspaces;
    Spec.WorkspaceCount           = static_cast<uint32_t>(sizeof(Workspaces) / sizeof(Workspaces[0]));
    Spec.DefaultWorkspaceIndex    = 0;
    Spec.DocumentCatalogue        = Catalogue;
    Spec.DocumentCatalogueCount   = static_cast<uint32_t>(sizeof(Catalogue) / sizeof(Catalogue[0]));
    Spec.DefaultDocumentTypeIndex = 0;

    return ExecuteWorkspaceApplication(Spec);
}
