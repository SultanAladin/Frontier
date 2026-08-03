/*==============================================================================================================================================
                                                    CONSTRUCTIONCONSOLEBRIDGE.H
==============================================================================================================================================*/
// 🧩 The one translation layer between the construction workspace's 5-state gate and the shared console's gate-agnostic model. It does exactly
//    two things and no more: it BUILDS the console's cluster/action tables from the authored ConstructionBand/ConstructionOperation tables (once,
//    lazily, from the constexpr catalogue), and it supplies the VerdictResolver + Context that collapses GateResult into ActionVerdict every frame.
//    Nothing here draws — the console draws; this only translates.
//
//    🔴 GateToken carries (band << 8 | operation) so the resolver can index back to the exact ConstructionOperation the console is asking about,
//       against the live ConstructionDocument riding Context. The resolver is pure per (Action, Context): it reads the document and the operation
//       table, mutates nothing, and reaches its Shortfall text out of a per-operation buffer arena the Context owns (indexed by GateToken), so two
//       calls in the same frame — the rail's tally pass and the grid's draw pass — return byte-identical pointers.

#pragma once
#ifndef FRONTIER_VALIDATION_CONSTRUCTIONCATALOGUE_CONSTRUCTIONCONSOLEBRIDGE_H
#define FRONTIER_VALIDATION_CONSTRUCTIONCATALOGUE_CONSTRUCTIONCONSOLEBRIDGE_H

#include "EngineContext/Interface/WorkspaceContextConsole/Descriptor/WorkspaceContextConsoleDescriptor.h"

#include "ConstructionCatalogue.h"

namespace ConstructionCatalogueValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The live gate input the resolver reads, handed to the console as VerdictBinding.Context. The document is the reviewer's editable state; the
//    band table is the constexpr catalogue the GateToken indexes back into; the Shortfall arena is a scratch the resolver formats a gated tile's
//    reason into. 🔴 One buffer PER catalogue operation, keyed by the same (band,op) packing GateToken carries, so a resolver call is a pure
//    function of (Action, Context): the same tile always formats into the same slot and returns the same pointer within a frame.
struct ConstructionConsoleContext
{
    const ConstructionDocument* Document;   // [-]  - the live gate input (borrowed; the panel owns it)
    const ConstructionBand*     Bands;      // [-]  - the catalogue GateToken indexes (borrowed constexpr)
    int                         BandCount;  // [idx]- number of bands
    char                        Shortfall[128][64];  // [-] - per-operation reason arena, indexed by the packed (band,op) key
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The console descriptor for the construction catalogue: the cluster/action tables (built once from the authored bands) paired with the resolver
// and the supplied live context. Rebuilt cheaply each frame — only the Context pointer changes; the tables are static.
Frontier::WorkspaceContextConsoleDescriptor ComposeConstructionConsoleDescriptor(ConstructionConsoleContext& Context);

// Seed the context's borrowed pointers from the live document and the constexpr catalogue. Call once per frame before composing the descriptor.
void BindConstructionConsoleContext(ConstructionConsoleContext& Context, const ConstructionDocument& Document);

}   // namespace ConstructionCatalogueValidation

#endif
