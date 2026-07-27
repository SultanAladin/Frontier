/*==============================================================================================================================================
                                                              WORKSPACEDOCKHOST.CPP
==============================================================================================================================================*/
// 🧩 The single dock every application reuses. The host keeps the workspace-registration surface (workspace tabs, active index, the shared
//    PanelRegistry the active workspace fills) and drives the Chrome-style interior dock (WorkspacePanelDock) that owns the freeform trapezoid-tab
//    docking — recursive splits, floating windows, inline rename. Registration stays here so the existing tab strip + workspace descriptors keep
//    working unchanged; the interior dock replaces the former fixed four-region layout.

#include "WorkspaceDockHost.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Fill the active workspace's registry from its ActivateRegistry callback. Called on first activation and on every switch.
    void ActivateWorkspace(WorkspaceDockState& State, int Index)
    {
        if (Index < 0 || Index >= State.WorkspaceCount)
        {
            return;
        }
        State.ActiveIndex = Index;
        ResetPanelRegistry(State.ActiveRegistry);

        const WorkspaceDescriptor& Workspace = State.Workspaces[Index];
        if (Workspace.ActivateRegistry != nullptr)
        {
            Workspace.ActivateRegistry(State.ActiveRegistry, Workspace.Context);
        }
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeWorkspaceDockState(WorkspaceDockState& State)
{
    State.WorkspaceCount    = 0;
    State.ActiveIndex       = -1;
    State.PendingActivation = -1;
    State.InteriorReady     = false;
    State.CataloguePtr      = nullptr;
    State.CatalogueCount    = 0;
    State.CatalogueDefault  = 0;
    ResetPanelRegistry(State.ActiveRegistry);
}


void RegisterWorkspaceCatalogue(WorkspaceDockState& State, const WorkspaceDocumentType* Types, int Count, int DefaultTypeIndex)
{
    State.CataloguePtr     = Types;
    State.CatalogueCount   = Count;
    State.CatalogueDefault = DefaultTypeIndex;
}


void RegisterWorkspace(WorkspaceDockState& State, const WorkspaceDescriptor& Workspace)
{
    if (State.WorkspaceCount >= WorkspaceDockState::Capacity)
    {
        return;
    }
    State.Workspaces[State.WorkspaceCount] = Workspace;
    const int NewIndex = State.WorkspaceCount;
    State.WorkspaceCount += 1;

    // 📝 The first workspace registered becomes active immediately so the desk is never empty.
    if (State.ActiveIndex < 0)
    {
        ActivateWorkspace(State, NewIndex);
    }
}


void RequestWorkspaceActivation(WorkspaceDockState& State, int WorkspaceIndex)
{
    if (WorkspaceIndex >= 0 && WorkspaceIndex < State.WorkspaceCount && WorkspaceIndex != State.ActiveIndex)
    {
        State.PendingActivation = WorkspaceIndex;
    }
}


void ConstructWorkspaceDock(const ThemeConfiguration& Theme, WorkspaceDockState& State)
{
    // 📝 Apply a requested switch at the top of the cycle, before anything reads the registry. The active workspace still fills its registry so
    //    callers that inspect ActiveRegistry keep working; the interior dock owns the visible layout.
    if (State.PendingActivation >= 0)
    {
        ActivateWorkspace(State, State.PendingActivation);
        State.PendingActivation = -1;
    }

    // 📝 Seed the Chrome-style interior dock exactly once (its own root leaf + prototype tabs). Kept out of Initialize so a host constructed
    //    before any ImGui context is valid still seeds on the first record cycle.
    if (!State.InteriorReady)
    {
        InitializeWorkspacePanelDock(State.InteriorDock);
        // 📝 Apply the app's (+) catalogue here (not in Initialize) so the seed instance reads its real stem ("Sketch 1", …) and the (+) offers the
        //    app's types. Deferred to first record because ConfigureWorkspaceCatalogue reseeds via ImGui-free helpers but keeps parity with the boot path.
        if (State.CataloguePtr != nullptr && State.CatalogueCount > 0)
            ConfigureWorkspaceCatalogue(State.InteriorDock, State.CataloguePtr, State.CatalogueCount, State.CatalogueDefault);
        State.InteriorReady = true;
    }

    // 📝 Paint + resolve the whole interior dock for this cycle: trapezoid tabs, recursive splits, floating windows, inline rename. Everything is
    //    drawn on the foreground draw list, so the host does NOT wrap it in a child region.
    ConstructWorkspacePanelDock(Theme, State.InteriorDock);
}

}   // namespace Frontier
