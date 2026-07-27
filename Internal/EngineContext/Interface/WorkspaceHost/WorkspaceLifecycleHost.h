/*==============================================================================================================================================
                                                          WORKSPACELIFECYCLEHOST.H
==============================================================================================================================================*/
// 🧩 The one shared entry point every Vulkan application calls from main(). Given a title and a caller-owned list of WorkspaceDescriptors, it
//    stands up the whole spine — native window, Vulkan host, ImGui relay, theme, dock state — runs the crash-safe frame loop until the window
//    closes, then tears everything down. The five real apps differ only in which workspaces they hand it (Editor: all five; the rest: one).
//    ComponentValidation / SceneDirectory do NOT use this — they are D3D11 test scaffolds.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_WORKSPACELIFECYCLEHOST_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_WORKSPACELIFECYCLEHOST_H

#include "WorkspaceDockHost.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 What an application hands the host. Workspaces is a caller-owned array (their state outlives the call); the host
//    registers each on the dock and activates DefaultWorkspaceIndex first.
struct WorkspaceApplicationSpec
{
    const char*          WindowTitle           = "Frontier";   // [-]  - OS title-bar caption
    uint32_t             InitialWidth          = 1600;         // [px] - Requested client width
    uint32_t             InitialHeight         = 900;          // [px] - Requested client height
    WorkspaceDescriptor* Workspaces            = nullptr;      // [-]  - Caller-owned descriptor array
    uint32_t             WorkspaceCount        = 0;            // [-]  - Number of descriptors
    uint32_t             DefaultWorkspaceIndex = 0;            // [-]  - Workspace active on launch

    // 📝 The interior dock's (+) document types (see ConfigureWorkspaceCatalogue). A standalone editor passes ONE (its own workspace); the Editor
    //    passes all six. Left null → the interior dock keeps the generic "Tab N" (+). Caller-owned for the duration of ExecuteWorkspaceApplication.
    WorkspaceDocumentType* DocumentCatalogue   = nullptr;      // [-]  - (+) menu types, or null for the generic fallback
    uint32_t             DocumentCatalogueCount = 0;           // [-]  - Number of catalogue types
    uint32_t             DefaultDocumentTypeIndex = 0;         // [-]  - Which catalogue type seeds the first tab
};


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Run the application to completion. Returns 0 on clean exit, non-zero if the window / Vulkan / ImGui bring-up failed.
int ExecuteWorkspaceApplication(const WorkspaceApplicationSpec& Spec);

}   // namespace Frontier

#endif
