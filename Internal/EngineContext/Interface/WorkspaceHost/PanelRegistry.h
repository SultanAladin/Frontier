/*==============================================================================================================================================
                                                              PANELREGISTRY.H
==============================================================================================================================================*/
// 🧩 Workspaces register their panels here; the host is NOT special-cased. Every panel — viewport, outliner, properties, control strip — is
//    the SAME registered record: an identifier, a dock region, a title, and a Construct callback the host invokes each cycle. A workspace is
//    just a set of registrations, so the host never names a concrete panel. This is what lets ONE shared viewport / outliner serve every
//    workspace (the "shared, not duplicated" rule) — the workspace supplies the callback + its own context pointer.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_PANELREGISTRY_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_PANELREGISTRY_H

#include "../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Where a panel docks inside the workspace desk. The dock host arranges regions; a panel names only its slot, not pixels.
enum class PanelDockRegion
{
    Viewport,       // [-] - Central 2D/3D view (fills the remainder)
    LeftOutliner,   // [-] - Scene hierarchy column
    RightProperties,// [-] - Properties column
    BottomControl   // [-] - Control / timeline strip
};


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Everything the host needs to place + record one panel. Context is opaque: the workspace threads its own state through it, so the shared
//    callback stays workspace-agnostic. The signature takes the resolved theme by const-ref (never hardcoded look).
struct PanelRegistration
{
    const char*     Identifier;   // [-] - Stable unique id (dock keys, persistence)
    const char*     Title;        // [-] - Tab / header caption
    PanelDockRegion Region;       // [-] - Dock slot
    void            (*Construct)(const ThemeConfiguration& Theme, void* Context);   // [-] - Per-cycle body emit
    void*           Context;      // [-] - Opaque per-panel state owned by the workspace
};


// 📝 The registry a workspace fills once (on activation) and the host walks each cycle. Fixed capacity — a workspace has a handful of panels.
struct PanelRegistry
{
    static const int Capacity = 16;                 // [-] - Max panels per workspace
    PanelRegistration Entries[Capacity];            // [-] - Registered panels
    int               EntryCount;                   // [-] - Live entry count
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Clear a registry back to empty (called when a workspace (re)builds its panel set).
void ResetPanelRegistry(PanelRegistry& Registry);

// 📝 Append one panel registration. Silently ignored past Capacity (a workspace should never exceed a handful).
void RegisterPanel(PanelRegistry& Registry, const PanelRegistration& Registration);

// 📝 Retrieve the first registered panel for a dock region, or null if none. The host records regions in a fixed order.
const PanelRegistration* ResolvePanelForRegion(const PanelRegistry& Registry, PanelDockRegion Region);

}   // namespace Frontier

#endif
