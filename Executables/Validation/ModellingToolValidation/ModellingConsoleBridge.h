/*==============================================================================================================================================
                                                      MODELLINGCONSOLEBRIDGE.H
==============================================================================================================================================*/
// 🧩 The translation layer between the modelling workspace's 3-state stratum gate and the shared console's gate-agnostic model. Same shape as the
//    construction bridge: it BUILDS the console's cluster/action tables from the authored ToolBandDescriptor/ToolTileDescriptor tables, CONVERTS the
//    stratum's authored probe readout into the console's probe types, and supplies the VerdictResolver + Context that maps ToolAvailability onto
//    ActionVerdict. Nothing here draws.
//
//    📝 Modelling is the EASY gate: ToolAvailability is already the same three states the console reduces to, so the resolver is close to an identity
//       map. What it still has to do is carry the tile's authored Shortfall onto the verdict, since the console's ActionVerdict holds the prose the
//       old ToolTileDescriptor held inline.

#pragma once
#ifndef FRONTIER_VALIDATION_MODELLINGTOOLVALIDATION_MODELLINGCONSOLEBRIDGE_H
#define FRONTIER_VALIDATION_MODELLINGTOOLVALIDATION_MODELLINGCONSOLEBRIDGE_H

#include "EngineContext/Interface/WorkspaceContextConsole/Descriptor/WorkspaceContextConsoleDescriptor.h"
#include "EngineContext/Interface/WorkspaceContextConsole/Descriptor/ProbeReadoutDescriptor.h"

#include "ModellingCatalogue.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The live gate input the resolver reads, handed to the console as VerdictBinding.Context. For modelling the whole gate input is ONE value — the
//    active stratum bit — because a tool's standing is a test of its authored StrataMask against that bit and nothing else. The converted probe rides
//    along so the panel can hand the console a readout without owning the conversion.
struct ModellingConsoleContext
{
    unsigned int           StratumBit;      // [-]  - the active selection stratum; the entire gate input
    ProbeReadoutDescriptor Probe;           // [-]  - the stratum's readout, converted into the console's types
    bool                   ProbePresent;    // [-]  - false when the stratum authors no readout
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Refresh the context for this frame's stratum: records the bit and converts that stratum's authored probe readout. Call before composing.
void BindModellingConsoleContext(ModellingConsoleContext& Context, unsigned int StratumBit);

// The console descriptor for the modelling catalogue: the cluster/action tables (built once from the authored bands) paired with the resolver, the
// supplied live context, and the converted probe.
WorkspaceContextConsoleDescriptor ComposeModellingConsoleDescriptor(ModellingConsoleContext& Context);

}   // namespace Frontier

#endif
