/*==============================================================================================================================================
                                                     MODELLINGCONSOLEBRIDGE.CPP
==============================================================================================================================================*/
// 🧩 Builds the console's cluster/action tables from the authored modelling catalogue, converts the stratum probe, and supplies the resolver that
//    maps ToolAvailability onto ActionVerdict. The mapping is near-identity — modelling's gate is already the three states the console reduces to:
//
//        ToolAvailability::Absent -> Omitted   (dropped from the grid)
//        ToolAvailability::Gated  -> Gated     (Shortfall = the tile's authored prose)
//        ToolAvailability::Live   -> Live
//
//    🔴 A SINGLE-SHOT band (Delete, Dissolve — a band with no tile table) becomes a cluster with exactly ONE action that carries the band's own
//       StrataMask and parameters. That is the console's stated model for single-shot rows, and it is what lets the rail filter them by asking the
//       resolver rather than reading a mask itself. Emitting them as clusters with ZERO actions would make the rail drop them entirely, because an
//       empty cluster has no action whose standing could keep it alive.

#include "ModellingConsoleBridge.h"

#include "EngineContext/Interface/WorkspaceContextConsole/Descriptor/ClusterDescriptor.h"
#include "EngineContext/Interface/WorkspaceContextConsole/Descriptor/ActionDescriptor.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int MaxActions  = 256;
    constexpr int MaxClusters = 64;

    ActionDescriptor  ActionTable[MaxActions];
    ClusterDescriptor ClusterTable[MaxClusters];
    int               ActionCount  = 0;
    int               ClusterCount = 0;
    bool              TablesBuilt  = false;

    // 📝 The token→tile map. GateToken is the DENSE action index, and this is the tile each index came from, so the resolver reaches the authored
    //    StrataMask and Shortfall without re-walking the catalogue. A single-shot band's lone action points at no tile, so its mask and (absent)
    //    shortfall are carried here directly instead.
    struct ActionOrigin
    {
        const ToolTileDescriptor* Tile;        // [-] - the authored tile; null for a single-shot band's own action
        unsigned int              StrataMask;  // [-] - the mask to test (the tile's, or the band's for a single shot)
        const char*               Shortfall;   // [-] - the authored precondition prose; null when it always runs
    };
    ActionOrigin OriginTable[MaxActions];

    void BuildTablesOnce()
    {
        if (TablesBuilt) { return; }

        int BandCount = 0;
        const ToolBandDescriptor* const Bands = ResolveModellingBands(BandCount);

        ActionCount  = 0;
        ClusterCount = 0;

        for (int BandIndex = 0; BandIndex < BandCount && ClusterCount < MaxClusters; ++BandIndex)
        {
            const ToolBandDescriptor& Band = Bands[BandIndex];

            const int FirstAction = ActionCount;

            if (Band.Tiles != nullptr && Band.TileCount > 0)
            {
                for (int TileIndex = 0; TileIndex < Band.TileCount && ActionCount < MaxActions; ++TileIndex)
                {
                    const ToolTileDescriptor& Tile = Band.Tiles[TileIndex];

                    ActionDescriptor& Action = ActionTable[ActionCount];
                    Action.Label          = Tile.Label;
                    Action.Keystroke      = Tile.Keystroke;
                    Action.GlyphName      = Tile.GlyphName;
                    Action.GateToken      = static_cast<unsigned int>(ActionCount);
                    // 🔴 Parameters stay null: modelling authors ToolParameterDescriptor, a DIFFERENT type from the console's ParameterDescriptor,
                    //    so the options body draws the void note until the parameter table is ported. Same deferral the construction bridge takes.
                    Action.Parameters     = nullptr;
                    Action.ParameterCount = 0;

                    OriginTable[ActionCount] = ActionOrigin{ &Tile, Tile.StrataMask, Tile.Shortfall };
                    ++ActionCount;
                }
            }
            else if (ActionCount < MaxActions)
            {
                // The single-shot band's own action: it carries the BAND's mask, and no shortfall (a single shot either applies or does not).
                ActionDescriptor& Action = ActionTable[ActionCount];
                Action.Label          = Band.Caption;
                Action.Keystroke      = Band.Keystroke;
                Action.GlyphName      = Band.GlyphName;
                Action.GateToken      = static_cast<unsigned int>(ActionCount);
                Action.Parameters     = nullptr;
                Action.ParameterCount = 0;

                OriginTable[ActionCount] = ActionOrigin{ nullptr, Band.StrataMask, nullptr };
                ++ActionCount;
            }

            ClusterDescriptor& Cluster = ClusterTable[ClusterCount];
            Cluster.Caption        = Band.Caption;
            Cluster.Keystroke      = Band.Keystroke;
            Cluster.GlyphName      = Band.GlyphName;
            Cluster.Actions        = (ActionCount > FirstAction) ? &ActionTable[FirstAction] : nullptr;
            Cluster.ActionCount    = ActionCount - FirstAction;
            Cluster.SeparatorAbove = Band.SeparatorAbove;
            ++ClusterCount;
        }

        TablesBuilt = true;
    }

    //------------------------------------------------- THE RESOLVER -------------------------------------------------

    // 📝 Near-identity: ask the shipped 3-state gate, then name the same three states in the console's vocabulary. The authored Shortfall rides the
    //    Gated verdict, which is where the console's header text reads it from. Pure per (Action, Context): every pointer returned is into the
    //    constexpr catalogue, so nothing is formatted and nothing can go stale between the tally pass and the draw pass.
    ActionVerdict ResolveModellingVerdict(const ActionDescriptor& Action, const void* ContextPointer)
    {
        const ModellingConsoleContext& Context = *static_cast<const ModellingConsoleContext*>(ContextPointer);
        const int Token = static_cast<int>(Action.GateToken);

        if (Token < 0 || Token >= ActionCount)
        {
            return ActionVerdict{ ActionStanding::Omitted, nullptr, nullptr };
        }

        const ActionOrigin& Origin = OriginTable[Token];

        // A single-shot band's action has no tile, so the shipped tile predicate cannot judge it; its standing is the plain mask test the band
        // carries. A tile goes through ResolveTileAvailability so the shortfall rule stays in ONE place.
        if (Origin.Tile == nullptr)
        {
            const bool Applies = (Origin.StrataMask & Context.StratumBit) != 0u;
            return Applies ? ActionVerdict{ ActionStanding::Live, nullptr, nullptr }
                           : ActionVerdict{ ActionStanding::Omitted, nullptr, nullptr };
        }

        switch (ResolveTileAvailability(*Origin.Tile, Context.StratumBit))
        {
            case ToolAvailability::Absent:
                return ActionVerdict{ ActionStanding::Omitted, nullptr, nullptr };

            case ToolAvailability::Gated:
                return ActionVerdict{ ActionStanding::Gated, Origin.Shortfall, nullptr };

            case ToolAvailability::Live:
            default:
                return ActionVerdict{ ActionStanding::Live, nullptr, nullptr };
        }
    }

    //------------------------------------------------- PROBE CONVERSION -------------------------------------------------

    // 📝 The console's probe types are field-for-field the ToolCard's, under console names. Converted field by field rather than reinterpret-cast:
    //    the two structs are only INCIDENTALLY identical, and a cast would turn a future divergence in either header into silent field shear rather
    //    than a compile error.
    ProbeRowDescriptor ConvertRow(const ToolProbeRowDescriptor& Source)
    {
        ProbeRowDescriptor Row = {};
        Row.Key          = Source.Key;
        Row.Reading      = Source.Reading;
        Row.Unit         = Source.Unit;
        Row.VectorLabel  = Source.VectorLabel;
        Row.VectorUnit   = Source.VectorUnit;
        for (int Axis = 0; Axis < ProbeAxisLimit && Axis < ToolProbeAxisLimit; ++Axis)
        {
            Row.AxisLabels[Axis]   = Source.AxisLabels[Axis];
            Row.AxisReadings[Axis] = Source.AxisReadings[Axis];
        }
        Row.ConditionLabel = Source.ConditionLabel;
        Row.ConditionMet   = Source.ConditionMet;
        Row.LimitPresent   = Source.LimitPresent;
        Row.LimitFloor     = Source.LimitFloor;
        Row.LimitCeiling   = Source.LimitCeiling;
        return Row;
    }

    void ConvertProbe(const ToolProbeReadoutDescriptor& Source, ProbeReadoutDescriptor& Target)
    {
        Target = ProbeReadoutDescriptor{};
        Target.Identity = Source.Identity;

        Target.SectionCount = (Source.SectionCount < ProbeSectionLimit) ? Source.SectionCount : ProbeSectionLimit;
        for (int SectionIndex = 0; SectionIndex < Target.SectionCount; ++SectionIndex)
        {
            const ToolProbeSectionDescriptor& SourceSection = Source.Sections[SectionIndex];
            ProbeSectionDescriptor&           TargetSection = Target.Sections[SectionIndex];

            TargetSection.Title    = SourceSection.Title;
            TargetSection.RowCount = (SourceSection.RowCount < ProbeRowLimit) ? SourceSection.RowCount : ProbeRowLimit;
            for (int RowIndex = 0; RowIndex < TargetSection.RowCount; ++RowIndex)
            {
                TargetSection.Rows[RowIndex] = ConvertRow(SourceSection.Rows[RowIndex]);
            }
        }

        Target.AggregateCount = (Source.AggregateCount < ProbeAggregateLimit) ? Source.AggregateCount : ProbeAggregateLimit;
        for (int RowIndex = 0; RowIndex < Target.AggregateCount; ++RowIndex)
        {
            Target.Aggregate[RowIndex] = ConvertRow(Source.Aggregate[RowIndex]);
        }
    }
}   // namespace


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void BindModellingConsoleContext(ModellingConsoleContext& Context, unsigned int StratumBit)
{
    Context.StratumBit = StratumBit;

    const ToolProbeReadoutDescriptor* const Source = ResolveModellingProbe(StratumBit);
    Context.ProbePresent = (Source != nullptr);
    if (Source != nullptr)
    {
        ConvertProbe(*Source, Context.Probe);
    }
}


WorkspaceContextConsoleDescriptor ComposeModellingConsoleDescriptor(ModellingConsoleContext& Context)
{
    BuildTablesOnce();

    WorkspaceContextConsoleDescriptor Descriptor = {};
    Descriptor.Clusters     = ClusterTable;
    Descriptor.ClusterCount = ClusterCount;
    Descriptor.Gate         = VerdictBinding{ &ResolveModellingVerdict, &Context };
    Descriptor.Probe        = Context.ProbePresent ? &Context.Probe : nullptr;
    return Descriptor;
}

}   // namespace Frontier
