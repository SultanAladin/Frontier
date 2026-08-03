/*==============================================================================================================================================
                                                    CONSTRUCTIONCONSOLEBRIDGE.CPP
==============================================================================================================================================*/
// 🧩 Builds the console's cluster/action tables from the authored construction catalogue and supplies the resolver that collapses the 5-state gate
//    into the console's 3-state ActionVerdict. The mapping is the whole point of the proof:
//
//        GateResult::Available          -> Live   (Yield = the raised-dimension prediction when the op raises)
//        GateResult::ClosureShortfall   -> Live   (Yield = "-> shell", the law's warning; it still RUNS)
//        GateResult::WorkplaneAbsent    -> Gated  (Shortfall = "set workplane")
//        GateResult::ProfileShortfall   -> Gated  (Shortfall = ShortfallText, the first unmet precondition)
//        GateResult::DimensionMismatch  -> Omitted (dropped from the grid entirely)
//
//    🔴 Available and ClosureShortfall BOTH map to Live, not to Live/Gated: the prototype's gate RUNS both (the closure case only WARNS that the
//       result is a shell, not a solid). Collapsing ClosureShortfall to Gated would grey a tile the reviewer can actually apply. The warning rides
//       Yield, which the console prints as the "-> shell" badge, not as a block.

#include "ConstructionConsoleBridge.h"

#include "EngineContext/Interface/WorkspaceContextConsole/Descriptor/ClusterDescriptor.h"
#include "EngineContext/Interface/WorkspaceContextConsole/Descriptor/ActionDescriptor.h"

#include <cstdio>

namespace ConstructionCatalogueValidation
{

using Frontier::ActionDescriptor;
using Frontier::ActionStanding;
using Frontier::ActionVerdict;
using Frontier::ClusterDescriptor;
using Frontier::VerdictBinding;
using Frontier::WorkspaceContextConsoleDescriptor;

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 GateToken is the DENSE action index across the whole catalogue (0..ActionCount-1), NOT a band/op pack. Dense is what the Shortfall arena
    //    and the address table both need: an action's own index is its arena slot and its row in the token→(band,op) map, so one value serves all
    //    three roles and no packing/unpacking is needed at the per-tile resolve. A sparse pack would leave the arena unaddressable.

    //------------------------------------------------- STATIC DESCRIPTOR TABLES -------------------------------------------------

    // 📝 The console's cluster/action tables, built ONCE from the constexpr catalogue on first use and held for the process. They are pure views of
    //    the authored ConstructionBand/ConstructionOperation strings — every Label/GlyphName/Keystroke pointer is the catalogue's own — so building
    //    them costs a walk, not a copy of the text. 🔴 Parameters are bound to NONE for this gate-lift proof: construction ops carry
    //    Frontier::ToolParameterDescriptor*, a DIFFERENT type from the console's Frontier::ParameterDescriptor, so the options body draws the void
    //    note. Bridging that type is the parameter-port step, deliberately out of this proof's scope.
    constexpr int MaxActions  = 256;
    constexpr int MaxClusters = 64;

    ActionDescriptor  ActionTable[MaxActions];
    ClusterDescriptor ClusterTable[MaxClusters];
    int               ActionCount  = 0;
    int               ClusterCount = 0;
    bool              TablesBuilt  = false;

    // 📝 The dense arena index each (band,op) maps to, filled in lockstep with the tables so the resolver's GateToken — which carries the DENSE
    //    action index, not the sparse pack — addresses the right Shortfall slot. The action table is dense, so an action's own index IS its slot.
    //    GateToken therefore carries the dense action index directly; PackGateToken's sparse form is kept only for the band/op debug view.

    void BuildTablesOnce(const ConstructionBand* Bands, int BandCount)
    {
        if (TablesBuilt) { return; }

        ActionCount  = 0;
        ClusterCount = 0;

        for (int BandIndex = 0; BandIndex < BandCount && ClusterCount < MaxClusters; ++BandIndex)
        {
            const ConstructionBand& Band = Bands[BandIndex];

            const int FirstAction = ActionCount;
            for (int OpIndex = 0; OpIndex < Band.OperationCount && ActionCount < MaxActions; ++OpIndex)
            {
                const ConstructionOperation& Operation = Band.Operations[OpIndex];

                ActionDescriptor& Action = ActionTable[ActionCount];
                Action.Label          = Operation.Label;
                Action.Keystroke      = Operation.Accelerator;
                Action.GlyphName      = Operation.GlyphName;
                // 🔴 The DENSE action index is the token: it is the resolver's key back to (band,op) via a parallel lookup, AND its own Shortfall
                //    slot. Storing the sparse pack here would leave the arena unaddressable.
                Action.GateToken      = static_cast<unsigned int>(ActionCount);
                Action.Parameters     = nullptr;   // parameter-port step; see the note above
                Action.ParameterCount = 0;
                ++ActionCount;
            }

            ClusterDescriptor& Cluster = ClusterTable[ClusterCount];
            Cluster.Caption        = Band.Caption;
            Cluster.Keystroke      = nullptr;
            Cluster.GlyphName      = Band.GlyphName;
            Cluster.Actions        = (ActionCount > FirstAction) ? &ActionTable[FirstAction] : nullptr;
            Cluster.ActionCount    = ActionCount - FirstAction;
            Cluster.SeparatorAbove = false;
            ++ClusterCount;
        }

        TablesBuilt = true;
    }

    // 📝 The dense-token → (band,op) map, filled alongside the tables so the resolver can reach the ConstructionOperation the console names. The
    //    token IS the dense action index, so this is a flat parallel array indexed by it.
    struct OperationAddress { int Band; int Operation; };
    OperationAddress AddressTable[MaxActions];

    void BuildAddressTableOnce(const ConstructionBand* Bands, int BandCount)
    {
        static bool AddressBuilt = false;
        if (AddressBuilt) { return; }

        int Cursor = 0;
        for (int BandIndex = 0; BandIndex < BandCount && Cursor < MaxActions; ++BandIndex)
        {
            for (int OpIndex = 0; OpIndex < Bands[BandIndex].OperationCount && Cursor < MaxActions; ++OpIndex)
            {
                AddressTable[Cursor].Band      = BandIndex;
                AddressTable[Cursor].Operation = OpIndex;
                ++Cursor;
            }
        }
        AddressBuilt = true;
    }

    //------------------------------------------------- THE RESOLVER -------------------------------------------------

    // 📝 The single extension point. Given an action (its GateToken is the dense index) and the construction context, evaluate the live gate and
    //    collapse the 5 states to the console's 3. Pure per (Action, Context): reads the document + catalogue, formats into the token's own arena
    //    slot, returns pointers stable for the frame.
    ActionVerdict ResolveConstructionVerdict(const ActionDescriptor& Action, const void* ContextPointer)
    {
        const ConstructionConsoleContext& Context = *static_cast<const ConstructionConsoleContext*>(ContextPointer);
        const int Token = static_cast<int>(Action.GateToken);

        if (Token < 0 || Token >= MaxActions || Context.Document == nullptr)
        {
            return ActionVerdict{ ActionStanding::Omitted, nullptr, nullptr };
        }

        const OperationAddress& Address = AddressTable[Token];
        if (Address.Band < 0 || Address.Band >= Context.BandCount)
        {
            return ActionVerdict{ ActionStanding::Omitted, nullptr, nullptr };
        }
        const ConstructionBand&      Band      = Context.Bands[Address.Band];
        if (Address.Operation < 0 || Address.Operation >= Band.OperationCount)
        {
            return ActionVerdict{ ActionStanding::Omitted, nullptr, nullptr };
        }
        const ConstructionOperation& Operation = Band.Operations[Address.Operation];
        const ConstructionDocument&  Document  = *Context.Document;

        const GateResult Condition = EvaluateOperation(Operation, Document);

        // The gated reasons format into this action's own arena slot (Token is the dense index, bounded to the arena above).
        const int   ArenaIndex = (Token < 128) ? Token : 0;
        char* const Reason     = const_cast<char*>(Context.Shortfall[ArenaIndex]);

        switch (Condition)
        {
            case GateResult::DimensionMismatch:
                return ActionVerdict{ ActionStanding::Omitted, nullptr, nullptr };

            case GateResult::WorkplaneAbsent:
                std::snprintf(Reason, 64, "set workplane");
                return ActionVerdict{ ActionStanding::Gated, Reason, nullptr };

            case GateResult::ProfileShortfall:
                ShortfallText(Operation, Document, Reason, 64);
                return ActionVerdict{ ActionStanding::Gated, Reason, nullptr };

            case GateResult::Available:
            case GateResult::ClosureShortfall:
            default:
            {
                // Live. When the op raises dimension, the predicted result rides Yield as the "-> dimension" badge the console prints; the closure
                // case's Yield is that same prediction (a shell), which is exactly the warning the prototype shows on a runnable-but-open tile.
                ConstructionDimension Result = ConstructionDimension::Nothing;
                if (ResultType(Operation, Document, Result))
                {
                    char* const Yield = Reason;   // reuse the same slot: a Live action has no Shortfall, so the slot is free for Yield
                    const char* Name  = DescribeDimension(Result);
                    char Lower[24] = {};
                    int  Cursor = 0;
                    for (; Name[Cursor] != '\0' && Cursor < 23; ++Cursor)
                    {
                        const char Character = Name[Cursor];
                        Lower[Cursor] = (Character >= 'A' && Character <= 'Z') ? static_cast<char>(Character - 'A' + 'a') : Character;
                    }
                    Lower[Cursor] = '\0';
                    std::snprintf(Yield, 64, "\xE2\x86\x92 %s", Lower);
                    return ActionVerdict{ ActionStanding::Live, nullptr, Yield };
                }
                return ActionVerdict{ ActionStanding::Live, nullptr, nullptr };
            }
        }
    }
}   // namespace


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void BindConstructionConsoleContext(ConstructionConsoleContext& Context, const ConstructionDocument& Document)
{
    int BandCount = 0;
    const ConstructionBand* const Bands = ResolveConstructionBands(BandCount);

    Context.Document  = &Document;
    Context.Bands     = Bands;
    Context.BandCount = BandCount;
    // The Shortfall arena is left as-is: the resolver overwrites each slot it uses every frame, so stale text is never read.
}


WorkspaceContextConsoleDescriptor ComposeConstructionConsoleDescriptor(ConstructionConsoleContext& Context)
{
    int BandCount = 0;
    const ConstructionBand* const Bands = ResolveConstructionBands(BandCount);

    BuildTablesOnce(Bands, BandCount);
    BuildAddressTableOnce(Bands, BandCount);

    WorkspaceContextConsoleDescriptor Descriptor = {};
    Descriptor.Clusters     = ClusterTable;
    Descriptor.ClusterCount = ClusterCount;
    Descriptor.Gate         = VerdictBinding{ &ResolveConstructionVerdict, &Context };
    return Descriptor;
}

}   // namespace ConstructionCatalogueValidation
