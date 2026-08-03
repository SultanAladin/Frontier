/*==============================================================================================================================================
                                                    WORKSPACECONTEXTCONSOLEDESCRIPTOR.H
==============================================================================================================================================*/
// 🧩 The whole console's authored input, gathered into one hand-off. A workspace fills this once — its cluster table (the rail, and the grids each
//    rail row opens), the glyph atlas the actions draw from, and the VerdictBinding that judges every action's standing — and passes it to the
//    console's draw entry point. This is the ONE type a workspace constructs to drive the console; everything else in Descriptor/ is a field of it
//    or a field of a field.
//
//    📝 It is a view, not an owner: every pointer is borrowed from the workspace's own constexpr tables and live document. The console reads through
//       it for one frame and holds nothing, so a workspace can rebuild it each frame (the binding's Context is the only part that changes) without
//       churning the tables behind it.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_DESCRIPTOR_WORKSPACECONTEXTCONSOLEDESCRIPTOR_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_DESCRIPTOR_WORKSPACECONTEXTCONSOLEDESCRIPTOR_H

#include "../Predicate/VerdictResolver.h"
#include "../Predicate/SurfacePainter.h"
#include "../Token/MetricsSpecification.h"
#include "FooterSpecification.h"

namespace Frontier
{

struct ClusterDescriptor;
struct ProbeReadoutDescriptor;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct WorkspaceContextConsoleDescriptor
{
    const ClusterDescriptor*      Clusters;      // [-]  - the rail in order, each row's grid hanging off it (borrowed)
    int                           ClusterCount;  // [idx]- number of rail rows
    VerdictBinding                Gate;          // [-]  - how this workspace judges an action's standing (resolver + live context)
    // 📝 What the second slide's left column measures for the current context, refilled by the workspace each frame. NULL is the honest answer for a
    //    workspace with nothing to report (construction's gate has no measurement surface) — the console then leaves that column as surface ground
    //    rather than drawing an empty readout frame.
    const ProbeReadoutDescriptor* Probe;         // [-]  - the readout, or null for no probe (borrowed)
    // 🔴 The geometry override MetricsSpecification's own header promises. The console used to resolve the metrics internally and had no way to be
    //    told otherwise, so "a workspace overrides the fields it disagrees on before handing it to the console" was unreachable — and the disagreement
    //    is real, not cosmetic: paint's box is 560x420 with a 363 px right column against modelling's 523x396/326. Null keeps the resolved defaults,
    //    so a workspace that agrees says nothing.
    const MetricsSpecification*   Metrics;       // [-]  - geometry override, or null for the theme-resolved defaults (borrowed)
    // 📝 The workspace's own painters for the two regions the console lays out but has no vocabulary to fill — slide 2's left column and an action
    //    tile's artwork. Both default to null, which is what modelling and construction bind: they want the probe strip and the glyph mark the
    //    console draws natively. Paint binds both, because a stroke preview and a nib well cannot be said in the console's terms. See SurfacePainter.h.
    SurfaceBinding                Surfaces;      // [-]  - workspace-drawn regions inside the console's frame
    // 📝 What the options slide's pinned footer says — its two button captions and the note beside them. Left default, it draws Cancel/Apply over the
    //    open action's label, which is what modelling and construction want. Paint fills it with Reset/Select over a live tally. See FooterSpecification.
    FooterSpecification           Footer;        // [-]  - the options footer's captions and note
};

} // namespace Frontier

#endif
