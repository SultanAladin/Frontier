/*==============================================================================================================================================
                                                              WORKSPACEDOCKHOST.H
==============================================================================================================================================*/
// 🧩 Drives the active workspace each cycle. The host keeps the workspace-registration surface (the set of workspace tabs, which one is active,
//    and the shared PanelRegistry the active one fills) and threads it into the Chrome-style interior dock (WorkspacePanelDock) that owns the
//    freeform trapezoid-tab docking. The host never names a concrete workspace or panel — it walks the registry the active workspace filled,
//    while the interior dock paints + resolves the actual tabs / splits / floating windows. This is the single dock every application reuses.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_WORKSPACEDOCKHOST_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_WORKSPACEDOCKHOST_H

#include "PanelRegistry.h"
#include "WorkspacePanelDock.h"

#include "../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One selectable workspace. ActivateRegistry is called when this workspace becomes active: it fills the shared registry with this
//    workspace's panels (viewport/outliner/properties/control), each carrying the workspace's own opaque context. Context is the workspace's
//    persistent state, owned by the caller.
struct WorkspaceDescriptor
{
    const char* Identifier;   // [-] - Stable id ("Modeling", "UV", ...) — matches config workspace names
    const char* Title;        // [-] - Tab caption
    void        (*ActivateRegistry)(PanelRegistry& Registry, void* Context);   // [-] - Fill the registry with this workspace's panels
    void*       Context;      // [-] - Opaque per-workspace state owned by the caller
};


// 📝 The host's persistent state: the set of workspaces, which one is active, the shared registry the active one fills, and the embedded
//    Chrome-style interior dock the host drives each cycle. One instance per application, held across cycles. InteriorReady gates the one-time
//    seeding of the interior dock (done on first record so it never re-seeds mid-run).
struct WorkspaceDockState
{
    static const int Capacity = 8;                  // [-] - Max workspaces per application
    WorkspaceDescriptor Workspaces[Capacity];       // [-] - Registered workspaces (tabs)
    int                 WorkspaceCount;             // [-] - Live workspace count
    int                 ActiveIndex;                // [-] - Currently active workspace
    int                 PendingActivation;          // [-] - Index to activate next cycle (-1 -> none); avoids re-fill mid-record
    PanelRegistry       ActiveRegistry;             // [-] - Panels of the active workspace (re-filled on switch)

    WorkspacePanelDock  InteriorDock;               // [-] - Chrome-style freeform dock the host paints + resolves each cycle
    bool                InteriorReady;              // [-] - false until the interior dock is seeded once (first record)

    // 📝 The interior dock's (+) catalogue, staged by RegisterWorkspaceCatalogue and applied at the one-time interior seed (ConfigureWorkspaceCatalogue
    //    needs a valid ImGui context, which only exists inside the record loop). Null CataloguePtr → the interior dock keeps the generic "Tab N".
    const WorkspaceDocumentType* CataloguePtr   = nullptr;   // [-] - staged (+) types (caller-owned), applied on first record
    int                          CatalogueCount = 0;         // [-] - number of staged types
    int                          CatalogueDefault = 0;       // [-] - which staged type seeds the first tab
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Initialize an empty host (no workspaces, none active).
void InitializeWorkspaceDockState(WorkspaceDockState& State);

// 📝 Append a workspace tab. The first workspace registered becomes active + has its registry filled.
void RegisterWorkspace(WorkspaceDockState& State, const WorkspaceDescriptor& Workspace);

// 📝 Stage the interior dock's (+) document types. Applied at the one-time interior seed (first record), where a valid ImGui context exists.
//    Types is caller-owned; the pointer is held, not copied, so it must outlive the record loop (an application-scope array). Null clears it.
void RegisterWorkspaceCatalogue(WorkspaceDockState& State, const WorkspaceDocumentType* Types, int Count, int DefaultTypeIndex);

// 📝 Request a workspace switch by index; applied at the start of the next cycle so the registry is never re-filled mid-record.
void RequestWorkspaceActivation(WorkspaceDockState& State, int WorkspaceIndex);

// 📝 Record the whole workspace desk for this cycle: apply any pending switch, seed the interior dock once, then paint + resolve the Chrome-style
//    interior dock (trapezoid tabs, splits, floating windows). Call once per cycle between the tab strip and the frame end.
void ConstructWorkspaceDock(const ThemeConfiguration& Theme, WorkspaceDockState& State);

}   // namespace Frontier

#endif
