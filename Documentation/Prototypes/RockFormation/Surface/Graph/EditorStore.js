/*====================================================================================================================================
                                                       EDITORSTORE.JS
====================================================================================================================================*/
// 🧩 A view of TreeState in the ported editor's record shape — plane transform, selection, card flags

// 📝 This is an ADAPTER, not a store. The reference's GraphStore.js owned the entries and links; here
//    TreeState owns them and this file owns only what the reference kept alongside them: the pan/zoom of
//    the plane, the multi-selection, and the six per-card presentation flags.
//
//    🔴 There is exactly one copy of the tree, and it is TreeState. The reference's RecordStore.Entries
//       array is deliberately NOT recreated. Two arrays holding the same entries is the defect this whole
//       adapter exists to avoid: the editor would drag a card in its copy while the transcriber read the
//       other, so the rock would stop tracking the graph with every module still reporting success.
//
//    🔴 dataset.entry is a STRING; TreeState.Entries is a Map keyed by NUMBER. Map.get("5") returns
//       undefined silently — no throw, no warning, just a card that does nothing. Every identifier
//       crossing the DOM boundary goes through NormalizeIdentifier below.

import { ConstructionSpecificationTable } from "../../Construction/ConstructionSpecifications.js";
import { MayConnect } from "../../Construction/PortCategories.js";
import
{
    ResolveInboundPort,
    ResolvePort,
    InsertLink,
    RemoveLink,
    RemoveEntry
} from "../../Construction/TreeState.js";
import { ResolveAffordances } from "./SpeciesPresentation.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                     PLANE STATE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Mirrors the reference's RecordStore minus Entries/Links. PlaneScale 0.75 is the reference's opening
//    zoom and is kept so the port lands looking like the reference on first paint.
export const PlaneState =
{
    PlaneOffsetX: 0,
    PlaneOffsetY: 0,
    PlaneScale:   0.75,

    CanvasImmobilized: false,
    ControlsRevealed:  false,
    ViewportSide:      'right',                                     // 'right' | 'left' | 'hidden'
    SimulationRunning: false
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 IDENTIFIER CROSSING
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The one place a DOM string becomes a Map key. Read it from nowhere else.
export function NormalizeIdentifier(RawIdentifier)
{
    if (RawIdentifier === null || RawIdentifier === undefined || RawIdentifier === '') return null;
    const Numeric = Number(RawIdentifier);
    return Number.isFinite(Numeric) ? Numeric : null;
}

export function RetrieveEntry(TreeState, RawIdentifier)
{
    const Identifier = NormalizeIdentifier(RawIdentifier);
    if (Identifier === null) return null;
    return TreeState.Entries.get(Identifier) || null;
}

export function RetrievePort(TreeState, RawIdentifier, PortToken)
{
    return ResolvePort(RetrieveEntry(TreeState, RawIdentifier), PortToken);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      SELECTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 A Set of numeric identifiers. The reference carried a Chosen boolean per entry; a Set keeps the
//    selection out of the tree records, so a selection change can never look like a structural change to
//    ComposeTopologyStamp and can never trigger a shader recompile.
export const Selection = new Set();

export function ChosenCondition(Identifier)
{
    return Selection.has(NormalizeIdentifier(Identifier));
}

export function ChooseSolely(RawIdentifier)
{
    const Identifier = NormalizeIdentifier(RawIdentifier);
    Selection.clear();
    if (Identifier !== null) Selection.add(Identifier);
}

export function ChooseAdditively(RawIdentifier)
{
    const Identifier = NormalizeIdentifier(RawIdentifier);
    if (Identifier === null) return;
    if (Selection.has(Identifier)) Selection.delete(Identifier);
    else                           Selection.add(Identifier);
}

export function ClearSelection()
{
    Selection.clear();
}

export function RetrieveChosenEntries(TreeState)
{
    return [...Selection].map(Identifier => TreeState.Entries.get(Identifier)).filter(Boolean);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CARD FLAGS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The six presentation flags the reference kept on each entry. Stored here, keyed by identifier, so
//    they never enter the topology stamp.
//
//    🔴 NONE of these notify the host. Folding a card, pinning it, or typing a comment changes what the
//       card looks like and nothing about what the shader computes — routing any of them through the
//       three-speed contract would recompile a shader that did not change, on every keystroke.
const CardFlags = new Map();

const VacantFlags =
{
    Folded:      false,
    Tacked:      false,
    Immobilized: false,
    Exported:    false,
    Composing:   false,
    Annotation:  undefined,
    Notice:      undefined
};

export function ResolveCardFlags(RawIdentifier)
{
    const Identifier = NormalizeIdentifier(RawIdentifier);
    if (Identifier === null) return { ...VacantFlags };
    if (!CardFlags.has(Identifier)) CardFlags.set(Identifier, { ...VacantFlags });
    return CardFlags.get(Identifier);
}

export function AssignCardFlag(RawIdentifier, FlagNaming, FlagValue)
{
    const Flags = ResolveCardFlags(RawIdentifier);
    Flags[FlagNaming] = FlagValue;
}

export function ToggleCardFlag(RawIdentifier, FlagNaming)
{
    const Flags = ResolveCardFlags(RawIdentifier);
    Flags[FlagNaming] = !Flags[FlagNaming];
    return Flags[FlagNaming];
}

export function DiscardCardFlags(RawIdentifier)
{
    const Identifier = NormalizeIdentifier(RawIdentifier);
    if (Identifier !== null) CardFlags.delete(Identifier);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LINK ADMISSIBILITY
//------------------------------------------------------------------------------------------------------------------------

// 📝 The reference's AdmissibleLinkCondition, restated against category equality rather than its
//    'any'-matches-everything rule.
//
//    🔴 There is no 'any' category in RockFormation and adding one would be a mistake. MayConnect is
//       strict equality precisely so a Resistance yield cannot land on a Distance intake — both are f32
//       in the shader, so nothing downstream would complain, and the rock would just come out wrong.
export function AdmissibleLinkCondition(TreeState, SourceIdentifier, SourcePortToken, TargetIdentifier, TargetPortToken)
{
    const Source = RetrieveEntry(TreeState, SourceIdentifier);
    const Target = RetrieveEntry(TreeState, TargetIdentifier);
    if (!Source || !Target)                          return false;
    if (Source.Identifier === Target.Identifier)     return false;

    const OriginPort  = ResolvePort(Source, SourcePortToken);
    const ArrivalPort = ResolveInboundPort(Target, TargetPortToken);
    if (!OriginPort || !ArrivalPort) return false;

    // 🔴 Direction matters: the origin must be an OUTBOUND port and the arrival an INBOUND one. Without
    //    this the editor happily draws out→out, which the transcriber then reads as a missing operand.
    const OutboundCondition = Source.OutboundPorts.some(Port => Port.Token === SourcePortToken);
    if (!OutboundCondition) return false;

    return MayConnect(OriginPort.Classification, ArrivalPort.Classification);
}

// 📝 Integrating a link goes through TreeState.InsertLink, which carries the cycle guard and the
//    one-link-per-intake replacement. This wrapper only translates a port token into the intake naming.
export function IntegrateLink(TreeState, SourceIdentifier, SourcePortToken, TargetIdentifier, TargetPortToken)
{
    if (!AdmissibleLinkCondition(TreeState, SourceIdentifier, SourcePortToken, TargetIdentifier, TargetPortToken))
    {
        return false;
    }

    const Source = RetrieveEntry(TreeState, SourceIdentifier);
    const Target = RetrieveEntry(TreeState, TargetIdentifier);
    const ArrivalPort = ResolveInboundPort(Target, TargetPortToken);

    return InsertLink(TreeState, Source.Identifier, Target.Identifier, ArrivalPort.IntakeNaming);
}

export function ReclaimLinkByPort(TreeState, TargetIdentifier, TargetPortToken)
{
    const Target = RetrieveEntry(TreeState, TargetIdentifier);
    const ArrivalPort = ResolveInboundPort(Target, TargetPortToken);
    if (!ArrivalPort) return;
    RemoveLink(TreeState, Target.Identifier, ArrivalPort.IntakeNaming);
}

// 📝 Every link touching an entry, in the reference's endpoint shape, so the router can resolve anchors
//    without knowing about intake namings.
export function EnumerateLinkEndpoints(TreeState)
{
    const Endpoints = [];

    for (const Link of TreeState.Links)
    {
        const Source = TreeState.Entries.get(Link.SourceEntry);
        const Target = TreeState.Entries.get(Link.TargetEntry);
        if (!Source || !Target) continue;

        const OriginPort  = Source.OutboundPorts[0];
        const ArrivalPort = Target.InboundPorts.find(Port => Port.IntakeNaming === Link.TargetIntake);
        if (!OriginPort || !ArrivalPort) continue;

        Endpoints.push({
            Token:       `link_${Link.SourceEntry}_${Link.TargetEntry}_${Link.TargetIntake}`,
            SourceEntry: Link.SourceEntry,
            SourcePort:  OriginPort.Token,
            TargetEntry: Link.TargetEntry,
            TargetPort:  ArrivalPort.Token,
            TargetIntake: Link.TargetIntake,
            Classification: ArrivalPort.Classification
        });
    }

    return Endpoints;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY RECLAIM
//------------------------------------------------------------------------------------------------------------------------

// 📝 Reclaiming honours both Singular (the root is not removable) and Immobilized (a locked card survives
//    a Delete keypress, which is the whole point of locking it).
export function ReclaimEntry(TreeState, RawIdentifier)
{
    const Entry = RetrieveEntry(TreeState, RawIdentifier);
    if (!Entry) return false;

    if (!ResolveAffordances(Entry.Species).Reclaim)       return false;
    if (ResolveCardFlags(Entry.Identifier).Immobilized)   return false;

    RemoveEntry(TreeState, Entry.Identifier);
    DiscardCardFlags(Entry.Identifier);
    Selection.delete(Entry.Identifier);
    return true;
}

export function ReclaimChosenEntries(TreeState)
{
    let Reclaimed = 0;
    for (const Identifier of [...Selection])
    {
        if (ReclaimEntry(TreeState, Identifier)) Reclaimed++;
    }
    return Reclaimed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PRESENTATION VIEW
//------------------------------------------------------------------------------------------------------------------------

// 📝 The record RasterizeEntry expects, assembled from a TreeState entry plus its card flags. This is the
//    seam: the reference's rasterizer stays verbatim because it is handed exactly the shape it was
//    written against.
//
//    🔴 A LIVE VIEW, NOT A SNAPSHOT. The reference MUTATES this record in thirteen places — it drags a card
//       by assigning .PlaneX/.PlaneY, selects by assigning .Chosen, and folds by flipping .Folded. Returning
//       a plain object makes every one of those writes land on a throwaway that the next read discards:
//       the card would follow the pointer (the drag handler sets the DOM transform itself) and then snap
//       back on the next refresh, with the links pointing at the old position and nothing logged anywhere.
//
//    🔴 So the mutable fields are ACCESSORS that write through to their real owner — position to the tree,
//       selection to the Set, the six presentation flags to the CardFlags map. The immutable ones (Title,
//       Species, ports) stay plain values, because a write to those is a bug worth surfacing rather than
//       silently forwarding.
export function ComposePresentationView(TreeState, Entry)
{
    const Specification = ConstructionSpecificationTable[Entry.Species] || {};
    const Flags        = ResolveCardFlags(Entry.Identifier);
    const Affordances  = ResolveAffordances(Entry.Species);
    const Identifier   = Entry.Identifier;

    const View =
    {
        Token:          String(Identifier),                         // 🔴 the DOM sees a string, always
        Identifier:     Identifier,
        Species:        Entry.Species,
        CatalogueToken: Entry.Species,
        Category:       Specification.Family || null,
        Compact:        Affordances.Form !== 'full',
        Title:          Specification.Naming || Entry.Species,
        Subtitle:       Specification.Summary || '',
        Badge:          Specification.Glyph || '',
        Glyph:          Specification.Family || 'Mass',
        InboundPorts:   Entry.InboundPorts,
        OutboundPorts:  Entry.OutboundPorts,
        Dials:          Entry.Dials,
        DialSpecifications: Specification.Dials || [],
        Bypassed:       Entry.Bypassed,
        Slot:           Entry.Slot,

        Affordances:    Affordances,
        PortScalars:    {},                                         // 📝 dormant: PortScalar is off
        RevealedScalars:{}
    };

    // ── position: the tree owns it, because the transcriber never reads it but a reload must restore it ──
    Object.defineProperty(View, 'PlaneX', {
        enumerable: true,
        get()      { return Entry.Position.x; },
        set(Value) { Entry.Position.x = Value; }
    });
    Object.defineProperty(View, 'PlaneY', {
        enumerable: true,
        get()      { return Entry.Position.y; },
        set(Value) { Entry.Position.y = Value; }
    });

    // ── selection: the Set owns it, so it can never enter the topology stamp ──
    Object.defineProperty(View, 'Chosen', {
        enumerable: true,
        get()      { return Selection.has(Identifier); },
        set(Value) { if (Value) Selection.add(Identifier); else Selection.delete(Identifier); }
    });

    // ── the six presentation flags: CardFlags owns them, for the same reason ──
    for (const FlagNaming of ['Folded', 'Tacked', 'Immobilized', 'Exported', 'Composing', 'Annotation', 'Notice'])
    {
        Object.defineProperty(View, FlagNaming, {
            enumerable: true,
            get()      { return Flags[FlagNaming]; },
            set(Value) { Flags[FlagNaming] = Value; }
        });
    }

    return View;
}
