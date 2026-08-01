//========================================================================================================================
//                                         ConstructionTranscriber.js                🧩
//========================================================================================================================
import { ConstructionSpecificationTable } from "./ConstructionSpecifications.js";


//------------------------------------------------------------------------------------------------------------------------
//                                              CONSTRUCTION TRANSCRIBER
//------------------------------------------------------------------------------------------------------------------------

// 📝 Flattens the tree into straight-line WGSL. 🔴 The whole reason for transcribing rather than
//    interpreting: an interpreter would need a stack and dynamic indexing in the innermost loop of the
//    march, which is the single hottest code path in the prototype. Emitting `let` bindings lets the
//    shader compiler inline, constant-fold and reorder freely.
//
//    Topology edit  -> retranscribe + recompile pipeline (tens of ms)
//    Dial turn      -> uniform write only, no recompile (sub-millisecond)
export const ConstructionTranscriber =
{
    // ① Resolve which entry feeds a given intake port.
    ResolveOperand(TreeState, EntryIdentifier, IntakeNaming)
    {
        const Link = TreeState.Links.find(Candidate =>
            Candidate.TargetEntry === EntryIdentifier && Candidate.TargetIntake === IntakeNaming);
        return Link ? Link.SourceEntry : null;
    },

    // ② Depth-first emit with memoisation, so a high-fanout entry is evaluated once and reused.
    EmitEntry(TreeState, EntryIdentifier, Emitted, Lines, Visiting)
    {
        if (Emitted.has(EntryIdentifier)) return Emitted.get(EntryIdentifier);

        // 🔴 Cycle guard. A cycle would recurse until the stack dies, so bail to a far constant.
        if (Visiting.has(EntryIdentifier)) return "1e9";
        Visiting.add(EntryIdentifier);

        const Entry         = TreeState.Entries.get(EntryIdentifier);
        const Specification = ConstructionSpecificationTable[Entry.Species];
        const Operands      = {};

        for (const Intake of Specification.Intakes)
        {
            const SourceIdentifier = this.ResolveOperand(TreeState, EntryIdentifier, Intake.Naming);
            if (SourceIdentifier !== null && TreeState.Entries.has(SourceIdentifier))
            {
                const SourceEntry = TreeState.Entries.get(SourceIdentifier);
                if (!SourceEntry.Bypassed)
                {
                    Operands[Intake.Naming] =
                        this.EmitEntry(TreeState, SourceIdentifier, Emitted, Lines, Visiting);
                }
            }
        }

        Visiting.delete(EntryIdentifier);

        // ③ A bypassed entry forwards its first same-category operand rather than vanishing, which is
        //    what makes bypass useful for A/B comparison instead of just deleting a branch.
        if (Entry.Bypassed)
        {
            const Passthrough = Specification.Intakes.find(Intake =>
                Intake.Category === Specification.Yields && Operands[Intake.Naming]);
            const Forwarded = Passthrough
                ? Operands[Passthrough.Naming]
                : (Specification.Yields === "Warp" ? "Probe"
                :  Specification.Yields === "Resistance" ? "0.5"
                :  Specification.Yields === "Tint" ? "vec3f(0.5)" : "1e9");
            Emitted.set(EntryIdentifier, Forwarded);
            return Forwarded;
        }

        const Expression = Specification.Transcribe(Operands, Entry.Dials, Entry.Slot);
        const Binding    = `Value${EntryIdentifier}`;
        const TypeNaming = Specification.Yields === "Warp" ? "vec3f"
                         : Specification.Yields === "Tint" ? "vec3f" : "f32";

        Lines.push(`    let ${Binding} : ${TypeNaming} = ${Expression};`);
        Emitted.set(EntryIdentifier, Binding);
        return Binding;
    },

    // ④ Emit the three tree entry points the resolve half expects.
    Transcribe(TreeState)
    {
        const RootIdentifier = [...TreeState.Entries.keys()].find(Identifier =>
            TreeState.Entries.get(Identifier).Species === "SurfaceResolve");

        if (RootIdentifier === undefined)
        {
            return {
                Source: this.ComposeFallback("no Surface Resolve entry"),
                LineTally: 0
            };
        }

        // 🔴 The intake is named "mass", NOT "distance". SurfaceResolve renamed it when the seed stopped
        //    being the finished shape, and this reader was the one place the rename missed. Asking for
        //    "distance" matches no link, so the guard below fired on a fully-wired tree and emitted the
        //    fallback sphere — a rock that renders, from a shader that compiles, with nothing in any log.
        //    Resolve by a naming the species table actually declares, never by a remembered one.
        const DistanceSource = this.ResolveOperand(TreeState, RootIdentifier, "mass");
        const TintSource     = this.ResolveOperand(TreeState, RootIdentifier, "tint");

        if (DistanceSource === null || !TreeState.Entries.has(DistanceSource))
        {
            return {
                Source: this.ComposeFallback("Surface Resolve has no mass operand"),
                LineTally: 0
            };
        }

        // ⑤ Distance body.
        const DistanceLines = [];
        const DistanceBinding = this.EmitEntry(
            TreeState, DistanceSource, new Map(), DistanceLines, new Set());

        // ⑥ Resistance body.
        //
        // 🔴 The ROOT'S OWN "resist" INTAKE IS AUTHORITATIVE and must be read first. The discovery walk
        //    below starts from the distance source and goes upstream, but resistance is a SEPARATE TREE
        //    (see PortCategories) — it is never an ancestor of the shape branch, so the walk cannot reach
        //    it and returned null on a fully-wired tree. Resistance then emitted a constant 0.5, meaning
        //    uniform hardness: the erosion has no reason to bite one place harder than another, which is
        //    exactly the flat-wall failure the species table was redesigned to escape. It compiles, it
        //    renders, and the rock just comes out wrong.
        const ResistanceIdentifier =
               this.ResolveOperand(TreeState, RootIdentifier, "resist")
            ?? this.DiscoverResistanceSource(TreeState, DistanceSource);
        const ResistanceLines = [];
        let ResistanceBinding = "0.5";
        if (ResistanceIdentifier !== null && TreeState.Entries.has(ResistanceIdentifier))
        {
            ResistanceBinding = this.EmitEntry(
                TreeState, ResistanceIdentifier, new Map(), ResistanceLines, new Set());
        }

        // ⑦ Tint body.
        const TintLines = [];
        let TintBinding = null;
        if (TintSource !== null && TreeState.Entries.has(TintSource))
        {
            TintBinding = this.EmitEntry(TreeState, TintSource, new Map(), TintLines, new Set());
        }

        const TintBody = TintBinding !== null
            ? `${TintLines.join("\n")}\n    return ${TintBinding};`
            : `${ResistanceLines.join("\n")}\n    return EvaluateStratumTint(Probe, ${ResistanceBinding}, vec4f(0.0, 1.0, 0.34, 0.42));`;

        const Source =
`
fn EvaluateConstructionTree(Probe : vec3f) -> f32
{
${DistanceLines.join("\n")}
    return ${DistanceBinding};
}

fn EvaluateConstructionResistance(Probe : vec3f) -> f32
{
${ResistanceLines.join("\n")}
    return ${ResistanceBinding};
}

fn EvaluateConstructionTint(Probe : vec3f) -> vec3f
{
${TintBody}
}
`;
        return {
            Source,
            LineTally: DistanceLines.length + ResistanceLines.length + TintLines.length
        };
    },

    // ⑥ One CARD's own thumbnail: the same three entry points, but rooted at an arbitrary entry instead
    //    of at SurfaceResolve. That is the whole difference -- a preview shows what this entry alone
    //    produces, so the walk starts here and goes upstream, and whatever is downstream is not its
    //    business.
    //
    //    🔴 RETURNS Source: null FOR ANYTHING THAT IS NOT A SURFACE. A Tint or Warp or Resistance card has
    //       no shape to sphere-trace -- vec3f is not a distance. Emitting one anyway yields a shader that
    //       compiles and a thumbnail that is uniformly one colour, which reads as "the preview is broken"
    //       rather than "this card has no surface". The caller skips the pipeline entirely on null.
    //
    //    🔴 A BYPASSED entry still previews. Bypass forwards an operand rather than deleting the branch,
    //       so its thumbnail should show what it forwards; EmitEntry already implements exactly that, so
    //       this must not special-case it.
    TranscribeEntry(TreeState, Identifier)
    {
        if (!TreeState.Entries.has(Identifier)) return { Source: null, LineTally: 0 };

        const Entry         = TreeState.Entries.get(Identifier);
        const Specification = ConstructionSpecificationTable[Entry.Species];

        // The preview sphere-traces a distance field. Only a Distance-yielding entry has one.
        if (!Specification || Specification.Yields !== "Distance") return { Source: null, LineTally: 0 };

        const DistanceLines = [];
        const DistanceBinding = this.EmitEntry(
            TreeState, Identifier, new Map(), DistanceLines, new Set());

        // 📝 Resistance is a SEPARATE tree and is never an ancestor of the shape branch, so the walk from
        //    this entry cannot reach it. Discovering it upstream is the same accommodation Transcribe()
        //    makes; a preview with uniform 0.5 hardness is acceptable where the viewport's is not, because
        //    the thumbnail shows shape rather than differential weathering.
        const ResistanceIdentifier = this.DiscoverResistanceSource(TreeState, Identifier);
        const ResistanceLines = [];
        let   ResistanceBinding = "0.5";
        if (ResistanceIdentifier !== null && TreeState.Entries.has(ResistanceIdentifier))
        {
            ResistanceBinding = this.EmitEntry(
                TreeState, ResistanceIdentifier, new Map(), ResistanceLines, new Set());
        }

        const Source =
`
fn EvaluateConstructionTree(Probe : vec3f) -> f32
{
${DistanceLines.join("\n")}
    return ${DistanceBinding};
}

fn EvaluateConstructionResistance(Probe : vec3f) -> f32
{
${ResistanceLines.join("\n")}
    return ${ResistanceBinding};
}

fn EvaluateConstructionTint(Probe : vec3f) -> vec3f
{
${ResistanceLines.join("\n")}
    return EvaluateStratumTint(Probe, ${ResistanceBinding}, vec4f(0.0, 1.0, 0.34, 0.42));
}
`;
        return {
            Source,
            LineTally: DistanceLines.length + ResistanceLines.length
        };
    },

    // 📝 Find a resistance-yielding entry reachable from the shape branch. Prefers the one furthest
    //    from the root, since that is the fully-composed resistance field rather than a partial one.
    DiscoverResistanceSource(TreeState, StartIdentifier)
    {
        const Frontier = [StartIdentifier];
        const Seen     = new Set();
        let   Deepest  = null;

        while (Frontier.length > 0)
        {
            const Identifier = Frontier.pop();
            if (Seen.has(Identifier)) continue;
            Seen.add(Identifier);
            if (!TreeState.Entries.has(Identifier)) continue;

            const Entry = TreeState.Entries.get(Identifier);
            const Specification = ConstructionSpecificationTable[Entry.Species];
            if (Specification.Yields === "Resistance" && !Entry.Bypassed) Deepest = Identifier;

            for (const Link of TreeState.Links)
            {
                if (Link.TargetEntry === Identifier) Frontier.push(Link.SourceEntry);
            }
        }
        return Deepest;
    },

    // 📝 A visible fallback rather than a compile failure, so an incomplete tree still renders and the
    //    reason is legible on screen instead of only in the console.
    ComposeFallback(Reason)
    {
        return `
// fallback — ${Reason}
fn EvaluateConstructionTree(Probe : vec3f) -> f32
{
    return DistanceToSphere(Probe, 1.6);
}

fn EvaluateConstructionResistance(Probe : vec3f) -> f32
{
    return EvaluateStratumBand(Probe, 0.5, vec4f(0.62, 0.07, 0.62, 0.0));
}

fn EvaluateConstructionTint(Probe : vec3f) -> vec3f
{
    return vec3f(0.30, 0.31, 0.34);
}
`;
    }
};
