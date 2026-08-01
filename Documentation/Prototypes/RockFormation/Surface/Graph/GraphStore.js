//========================================================================================================================
//                                                  GraphStore.js                                                  🧩
//========================================================================================================================
//
// 📝 The reference editor's store dialect, spoken on top of TreeState.
//
//    EditorSurface.js is NodeFlowEditor's EditorEntry.js byte-for-byte, and it must stay that way — it is
//    62 KB of interaction handling and any hand-edit is a divergence nobody can later diff against the
//    reference. So instead of rewriting its 77 RecordStore call sites, this file presents the names it
//    imports and translates each one down onto TreeState + EditorStore.
//
//    🔴 THERE IS STILL ONLY ONE COPY OF THE TREE. RecordStore.Entries below is a getter that BUILDS a fresh
//       array on every read; it is not a second store. Two arrays holding the same entries is the exact
//       defect this layer exists to avoid — the editor would drag a card in its copy while the transcriber
//       read the other, and the rock would stop tracking the graph with every module reporting success.
//
//    🔴 The reference iterates `for (const Entry of RecordStore.Entries)` and calls .filter/.push on it.
//       TreeState.Entries is a MAP, whose iterator yields [key, value] PAIRS. Handing the Map over
//       directly type-checks, runs, and silently makes every Entry a 2-element array — cards would render
//       as [object Object] with no error anywhere. The array view below is what prevents that.
//
//    🔴 The reference keys entries by STRING token ('entry_1'); TreeState keys by NUMBER. Every identifier
//       crossing this boundary goes through EditorStore.NormalizeIdentifier, and every record handed
//       upward carries Token as a string. Map.get("5") returns undefined with no throw.

import
{
    InsertEntry,
    RemoveLink,
    ResolveInboundPort
} from "../../Construction/TreeState.js";
import
{
    PlaneState,
    NormalizeIdentifier,
    RetrieveEntry     as RetrieveTreeEntry,
    RetrievePort      as RetrieveTreePort,
    Selection,
    ChosenCondition,
    RetrieveChosenEntries,
    AssignCardFlag,
    ResolveCardFlags,
    AdmissibleLinkCondition as AdmissibleByPort,
    IntegrateLink     as IntegrateByPort,
    ReclaimLinkByPort,
    ReclaimEntry      as ReclaimTreeEntry,
    EnumerateLinkEndpoints,
    ComposePresentationView
} from "./EditorStore.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                   TREE ATTACHMENT
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 The reference's store was a module-level singleton, so its editor never passed a store around. The
//    host owns the TreeState here, so it is attached once at boot and read through the accessor below.
//
//    🔴 Every function in this file throws a NAMED error when the tree is missing rather than returning
//       null. An unattached store used to surface as `undefined is not iterable` from inside a pan
//       handler — twelve frames after the actual mistake, in a file that never mentions the tree.

let AttachedTree = null;

export function AttachTree(TreeState)
{
    AttachedTree = TreeState;
}

function Tree()
{
    if (!AttachedTree) throw new Error("GraphStore used before AttachTree — the host must attach at boot");
    return AttachedTree;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOKEN ISSUANCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Kept for signature parity with the reference. Entry identifiers come from TreeState.NextIdentifier, so
//    this counter deliberately drives nothing structural.
let PortCounter = 1;

export function IssuePortToken() { return `port_${PortCounter++}`; }

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECORD STORE
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 The nine fields EditorSurface.js actually touches. Seven are pure view state and live in
//    EditorStore.PlaneState; only Entries and Links reach the tree, and both are derived views.

export const RecordStore =
{
    // 🔴 A GETTER, not a field. Each read walks the Map in identifier order and wraps every entry in the
    //    presentation shape the verbatim rasterizer was written against. Card flags and selection are
    //    merged in here, which is why neither can ever reach ComposeTopologyStamp and force a recompile.
    get Entries()
    {
        const TreeState = Tree();
        return [...TreeState.Entries.keys()]
            .sort((Left, Right) => Left - Right)
            .map(Identifier => ComposePresentationView(TreeState, TreeState.Entries.get(Identifier)));
    },

    // 🔴 Assignment is how the reference deletes: `RecordStore.Entries = RecordStore.Entries.filter(...)`.
    //    A getter with no setter would make that a silent no-op in sloppy mode, so the setter below
    //    reconciles by REMOVING whatever the assigned array omits, rather than replacing the Map.
    set Entries(Retained)
    {
        const TreeState = Tree();
        const Surviving = new Set(Retained.map(View => NormalizeIdentifier(View.Token ?? View.Identifier)));
        for (const Identifier of [...TreeState.Entries.keys()])
        {
            if (!Surviving.has(Identifier)) ReclaimTreeEntry(TreeState, Identifier);
        }
    },

    // 📝 Links in the reference's endpoint shape — SourcePort/TargetPort tokens rather than intake namings.
    get Links()
    {
        return EnumerateLinkEndpoints(Tree());
    },

    set Links(Retained)
    {
        const TreeState = Tree();
        const Surviving = new Set(Retained.map(Link => Link.Token));
        for (const Endpoint of EnumerateLinkEndpoints(TreeState))
        {
            if (!Surviving.has(Endpoint.Token))
            {
                RemoveLink(TreeState, Endpoint.TargetEntry, Endpoint.TargetIntake);
            }
        }
    },

    // ── plane transform + chrome state, forwarded to the one copy in EditorStore ──
    get PlaneOffsetX()      { return PlaneState.PlaneOffsetX; },
    set PlaneOffsetX(Value) { PlaneState.PlaneOffsetX = Value; },
    get PlaneOffsetY()      { return PlaneState.PlaneOffsetY; },
    set PlaneOffsetY(Value) { PlaneState.PlaneOffsetY = Value; },
    get PlaneScale()        { return PlaneState.PlaneScale; },
    set PlaneScale(Value)   { PlaneState.PlaneScale = Value; },

    get CanvasImmobilized()      { return PlaneState.CanvasImmobilized; },
    set CanvasImmobilized(Value) { PlaneState.CanvasImmobilized = Value; },
    get ControlsRevealed()       { return PlaneState.ControlsRevealed; },
    set ControlsRevealed(Value)  { PlaneState.ControlsRevealed = Value; },
    get ViewportSide()           { return PlaneState.ViewportSide; },
    set ViewportSide(Value)      { PlaneState.ViewportSide = Value; },
    get SimulationRunning()      { return PlaneState.SimulationRunning; },
    set SimulationRunning(Value) { PlaneState.SimulationRunning = Value; }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      RETRIEVAL
//------------------------------------------------------------------------------------------------------------------------

// 📝 The reference's one-argument signatures. Each returns a PRESENTATION VIEW, not the raw tree record, so
//    the caller reading .Folded or .Chosen finds them where it expects.
export function RetrieveEntry(EntryToken)
{
    const TreeState = Tree();
    const Entry = RetrieveTreeEntry(TreeState, EntryToken);
    return Entry ? ComposePresentationView(TreeState, Entry) : null;
}

// 🔴 The reference's version returned the FIRST entry with .Chosen, which made the preview follow whichever
//    card happened to sort first rather than the one just clicked. Selection is an insertion-ordered Set
//    here, so the LAST added is the last clicked — decision 5 in the plan.
export function RetrieveChosenEntry()
{
    const Chosen = RetrieveChosenEntries(Tree());
    if (Chosen.length === 0) return null;
    return ComposePresentationView(Tree(), Chosen[Chosen.length - 1]);
}

export function RetrievePort(EntryToken, PortToken)
{
    return RetrieveTreePort(Tree(), EntryToken, PortToken);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ENTRY CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 The reference opened on a bare 'Start' card. RockFormation's root is the seeded SurfaceResolve, which
//    the host has already inserted by the time the editor boots, so the origin entry is the EXISTING root
//    rather than a new record.
//
//    🔴 Returns null when the root is already present, and the caller pushes onto RecordStore.Entries —
//       whose setter ignores additions. Inserting a second root here would trip TreeState's Singular guard
//       anyway, but silently: InsertEntry returns the existing identifier, so nothing would look wrong.
export function ConstructOriginEntry()
{
    const TreeState = Tree();
    for (const Entry of TreeState.Entries.values())
    {
        if (Entry.Species === "SurfaceResolve") return ComposePresentationView(TreeState, Entry);
    }
    return null;
}

// 📝 Spawned from the right-click catalogue. CatalogueItem.Token is the species key.
export function ConstructCatalogueEntry(CatalogueItem, Category, PlaneX, PlaneY)
{
    const TreeState = Tree();
    const Identifier = InsertEntry(TreeState, CatalogueItem.Token, { x: PlaneX, y: PlaneY });
    return ComposePresentationView(TreeState, TreeState.Entries.get(Identifier));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PORT MUTATION
//------------------------------------------------------------------------------------------------------------------------
//
// 🔴 DORMANT BY DESIGN, and these are the four that must stay that way. An intake is a NAMED OPERAND that
//    Transcribe() resolves by name, so a runtime port called 'In 3' has no operand behind it: the editor
//    would draw a link, the transcription would ignore it, and the rock would simply not change. Every
//    species sets AttachInbound/AttachOutbound/DetachOutbound/PortScalar false in its affordances, so the
//    rasterizer never renders the buttons that reach these. They are kept, not deleted, so step ⑦ can
//    prove the reference's port-mutation code still works when a species opts in.

export function AttachInboundPort()
{
    console.warn("AttachInboundPort is dormant — intakes are fixed by species (see GraphStore.js)");
}

export function AttachOutboundPort()
{
    console.warn("AttachOutboundPort is dormant — a species yields exactly one category");
}

export function DetachInboundPort(EntryToken, PortToken)
{
    const Target = RetrieveTreeEntry(Tree(), EntryToken);
    const Port   = ResolveInboundPort(Target, PortToken);
    if (Port) RemoveLink(Tree(), Target.Identifier, Port.IntakeNaming);
}

export function DetachOutboundPort()
{
    console.warn("DetachOutboundPort is dormant — a species yields exactly one category");
}

export function ReclaimEntry(EntryToken)
{
    return ReclaimTreeEntry(Tree(), EntryToken);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LINK ADMISSIBILITY
//------------------------------------------------------------------------------------------------------------------------

export function AdmissibleLinkCondition(SourceEntry, SourcePort, TargetEntry, TargetPort)
{
    return AdmissibleByPort(Tree(), SourceEntry, SourcePort, TargetEntry, TargetPort);
}

// 📝 Goes through TreeState.InsertLink, so the cycle guard and the one-link-per-intake replacement apply.
//    🔴 The reference had NO cycle guard — it could afford one because its viewport was a mock. A cycle
//       reaching a real shader is a blank render, so it is refused at authoring time (decision 3).
export function IntegrateLink(SourceEntry, SourcePort, TargetEntry, TargetPort)
{
    return IntegrateByPort(Tree(), SourceEntry, SourcePort, TargetEntry, TargetPort);
}

// 📝 The reference deleted by link token; the endpoint view mints those tokens, so this parses the target
//    back out of one rather than keeping a second index.
export function ReclaimLink(LinkToken)
{
    for (const Endpoint of EnumerateLinkEndpoints(Tree()))
    {
        if (Endpoint.Token === LinkToken)
        {
            ReclaimLinkByPort(Tree(), Endpoint.TargetEntry, Endpoint.TargetPort);
            return;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   BUILD CONDITION
//------------------------------------------------------------------------------------------------------------------------

// 📝 The reference faked a 500 ms rebuild to animate a progress bar. RockFormation's rebuild is real and is
//    driven by the host's three-speed contract, so this only flips the flag the card reads for its spinner.
//
//    🔴 It must NOT call Notify — the host already schedules the recompile from the dial handler. Routing a
//       second dispatch through here would recompile the shader twice per dial turn.
const RebuildTimers = new Map();

export function ProvokeRebuild(EntryToken, OnSettled)
{
    const Identifier = NormalizeIdentifier(EntryToken);
    if (Identifier === null) return;

    AssignCardFlag(Identifier, "Composing", true);

    if (RebuildTimers.has(Identifier)) clearTimeout(RebuildTimers.get(Identifier));
    RebuildTimers.set(Identifier, setTimeout(() =>
    {
        AssignCardFlag(Identifier, "Composing", false);
        RebuildTimers.delete(Identifier);
        if (OnSettled) OnSettled();
    }, 500));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RE-EXPORTS
//------------------------------------------------------------------------------------------------------------------------

export { Selection, ChosenCondition, ResolveCardFlags, NormalizeIdentifier };
