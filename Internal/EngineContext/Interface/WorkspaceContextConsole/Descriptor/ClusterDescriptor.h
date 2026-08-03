/*==============================================================================================================================================
                                                            CLUSTERDESCRIPTOR.H
==============================================================================================================================================*/
// 🧩 One rail row and the grid of actions it opens. Ported from ToolCard's ToolBandDescriptor, renamed cluster because a rail row is a named group
//    of related actions, not a horizontal band of pixels. A cluster with Actions drives the grid; a cluster with none is a SINGLE-SHOT row that
//    runs on click (Delete, Dissolve) — an action with no options pane to open, which is why the prototype sits it below the rail's separator.
//
//    🔴 The gate subtraction that applied to ActionDescriptor applies here too: modelling's ToolBandDescriptor carried a StrataMask and inline
//       Parameters for single-shot rows so the rail could filter itself. Those are GONE — a single-shot row is now just a cluster whose lone action
//       carries the GateToken, and the rail asks the VerdictResolver whether the cluster has any non-Omitted action rather than counting a mask
//       itself. SeparatorAbove stays: it is layout, not gate.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_DESCRIPTOR_CLUSTERDESCRIPTOR_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_DESCRIPTOR_CLUSTERDESCRIPTOR_H

namespace Frontier
{

struct ActionDescriptor;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ClusterDescriptor
{
    const char*             Caption;         // [-]  - rail label, also the grid header title
    const char*             Keystroke;       // [-]  - accelerator, single-shot rows only
    const char*             GlyphName;       // [-]  - rail artwork, also the grid header badge
    const ActionDescriptor* Actions;         // [-]  - actions this cluster opens (borrowed); null/empty makes it single-shot
    int                     ActionCount;     // [idx]- number of actions
    bool                    SeparatorAbove;  // [-]  - draw the dividing rule above this row
};

} // namespace Frontier

#endif
