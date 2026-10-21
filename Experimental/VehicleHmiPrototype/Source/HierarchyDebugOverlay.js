//------------------------------------------------------------------------------------------------------------------------
// GEOMETRY HIERARCHY DEBUG OVERLAY
//------------------------------------------------------------------------------------------------------------------------
//
// 🧩 Editor debug view for the virtualized-geometry build (Source/GeometryHierarchy.js): per-level cluster bounds
//    and wireframe of the simplified level geometry itself, so the LOD DAG can be inspected against the live scene.
//
// 📝 Off unless the page is opened with ?hierarchy=1 — the default editor path constructs an inert object and
//    allocates nothing. Optional &glass=9,12 lists the material families treated as transmissive (they are built
//    into their own clusters and shown cyan in the Class colour mode). Hierarchies are built lazily, once per
//    eligible facet set (same static-set rule the clustered draw path uses: >= 256 facets, no dynamic vertex
//    capacity), and rebuilt if the facet count changes. Bounds drawn are the error-expanded DAG node bounds, so
//    coarser levels visibly grow by their own error — that padding is the culling-safety margin, not sloppiness.

import { ProjectGizmo } from "./TransformGizmo.js";
import { BuildGeometryHierarchy, CollectHierarchyLevel } from "./GeometryHierarchy.js";

const SvgNamespace = "http://www.w3.org/2000/svg";
const WireframeTriangleCap = 24000;
const BoundsCap = 6000;

function TransformPoint(Matrix, Point)
{
    return [0, 1, 2].map((Axis) =>
        Matrix[Axis] * Point[0] + Matrix[4 + Axis] * Point[1] + Matrix[8 + Axis] * Point[2] + Matrix[12 + Axis]);
}

export class HierarchyDebugOverlay
{
    constructor(Host)
    {
        this.Host = Host;
        this.Builds = new Map();
        this.SelectedLevel = 0;
        this.Mode = "both";          // both | bounds | wireframe
        this.ColourMode = "cluster"; // cluster | error | class
        const Query = new URLSearchParams(location.search);
        if (Query.get("hierarchy") !== "1") return;
        this.TransmissiveFamilies = (Query.get("glass") ?? "")
            .split(",").map((Value) => Number.parseInt(Value, 10)).filter(Number.isInteger);
        const Stage = document.getElementById("stage");
        if (!Stage || !document.createElementNS) return;
        this.SVG = document.createElementNS(SvgNamespace, "svg");
        Object.assign(this.SVG.style, { position: "absolute", inset: "0", width: "100%", height: "100%",
            pointerEvents: "none", zIndex: 14, overflow: "hidden" });
        Stage.appendChild(this.SVG);
        this.ConstructPanel(Stage);
    }

    ConstructPanel(Stage)
    {
        const Panel = document.createElement("div");
        Object.assign(Panel.style, { position: "absolute", top: "12px", right: "12px", zIndex: 15, width: "300px",
            background: "#171d23e6", color: "#d5dce6", padding: "10px 12px", borderRadius: "8px",
            fontSize: "11px", fontFamily: "inherit", pointerEvents: "auto", lineHeight: "1.5" });
        const Title = document.createElement("div");
        Title.textContent = "Geometry hierarchy — virtualized geometry debug";
        Title.style.fontWeight = "600";
        Panel.appendChild(Title);
        this.Stats = document.createElement("div");
        this.Stats.textContent = "Waiting for scene…";
        Panel.appendChild(this.Stats);
        this.LevelRow = document.createElement("div");
        Object.assign(this.LevelRow.style, { display: "flex", flexWrap: "wrap", gap: "4px", margin: "6px 0" });
        Panel.appendChild(this.LevelRow);
        const MakeSelect = (Label, Options, Current, OnChange) =>
        {
            const Row = document.createElement("label");
            Row.textContent = Label + " ";
            const Select = document.createElement("select");
            for (const [Value, Text] of Options)
            {
                const Option = document.createElement("option");
                Option.value = Value; Option.textContent = Text; Option.selected = Value === Current;
                Select.appendChild(Option);
            }
            Select.addEventListener("change", () => OnChange(Select.value));
            Row.appendChild(Select);
            Panel.appendChild(Row);
            Panel.appendChild(document.createElement("br"));
        };
        MakeSelect("Show", [["both", "Bounds + wireframe"], ["bounds", "Bounds"], ["wireframe", "Wireframe"]],
            this.Mode, (Value) => { this.Mode = Value; });
        MakeSelect("Colour", [["cluster", "Cluster identity"], ["error", "Error heat"], ["class", "Opaque / transmissive"]],
            this.ColourMode, (Value) => { this.ColourMode = Value; });
        const Note = document.createElement("div");
        Note.textContent = "Bounds are error-expanded. Wireframe is the simplified level geometry. " +
            "Class: grey opaque · cyan transmissive · magenta = mixed (must never appear).";
        Note.style.opacity = "0.75";
        Panel.appendChild(Note);
        Stage.appendChild(Panel);
        this.Panel = Panel;
    }

    ColourFor(Node, MaxError)
    {
        if (this.ColourMode === "error")
        {
            const T = MaxError > 0 ? Math.min(1, Node.Error / MaxError) : 0;
            return "hsl(" + Math.round(240 - 240 * T) + " 85% 60%)";
        }
        if (this.ColourMode === "class")
            return Node.ClassMask === 2 ? "#46dcf0" : Node.ClassMask === 1 ? "#9aa7b8" : "#ff4fd8";
        return "hsl(" + ((Node.Identity * 137.508) % 360).toFixed(1) + " 62% 58%)";
    }

    EnsureBuild(FacetSetIndex, Set)
    {
        const Existing = this.Builds.get(FacetSetIndex);
        if (Existing && Existing.FacetCount === Set.Facets.length) return Existing;
        const Record = { FacetCount: Set.Facets.length, Hierarchy: null, Failure: null, BuildMs: 0, MaxError: 0 };
        try
        {
            const Begin = performance.now();
            Record.Hierarchy = BuildGeometryHierarchy(Set, { TransmissiveFamilies: this.TransmissiveFamilies });
            Record.BuildMs = performance.now() - Begin;
            for (const Node of Record.Hierarchy.Clusters) Record.MaxError = Math.max(Record.MaxError, Node.Error);
        }
        catch (Failure)
        {
            Record.Failure = String(Failure.message ?? Failure);
        }
        this.Builds.set(FacetSetIndex, Record);
        return Record;
    }

    RefreshLevelButtons(MaxLevel)
    {
        const Signature = "levels:" + MaxLevel + ":sel:" + this.SelectedLevel;
        if (Signature === this.LevelSignature) return;
        this.LevelSignature = Signature;
        this.LevelRow.replaceChildren();
        for (let Level = 0; Level <= MaxLevel; ++Level)
        {
            const Button = document.createElement("button");
            Button.textContent = "L" + Level;
            Object.assign(Button.style, { padding: "2px 8px", borderRadius: "6px", cursor: "pointer",
                border: "1px solid " + (Level === this.SelectedLevel ? "#61eaa0" : "#3a4654"),
                background: Level === this.SelectedLevel ? "#1e3a2c" : "#222c36", color: "#d5dce6" });
            Button.addEventListener("click", () => { this.SelectedLevel = Level; this.LevelSignature = ""; });
            this.LevelRow.appendChild(Button);
        }
    }

    Draw()
    {
        const S = this.SVG, H = this.Host;
        if (!S) return;
        const Scene = H.Scene, Camera = H.CameraState;
        if (!Scene?.DrawRuns || !Camera)
        {
            if (this.Visible) S.replaceChildren();
            this.Visible = false;
            return;
        }
        this.Visible = true;
        const Bounds = S.getBoundingClientRect();
        const Width = Bounds.width, Height = Bounds.height;
        // Group draw runs per built facet set: each run contributes occupants to draw the hierarchy through.
        const PerSet = new Map();
        for (const Run of Scene.DrawRuns)
        {
            const Set = Scene.FacetSets[Run.FacetSet];
            if (!Set?.Facets || Set.Facets.length < 256 || Set.VertexCapacity) continue;
            const Record = this.EnsureBuild(Run.FacetSet, Set);
            if (!Record.Hierarchy) continue;
            if (!PerSet.has(Run.FacetSet)) PerSet.set(Run.FacetSet, { Record, Occupants: [] });
            for (let O = Run.First; O < Run.First + Run.Count; ++O) PerSet.get(Run.FacetSet).Occupants.push(O);
        }
        let MaxLevel = 0;
        for (const { Record } of PerSet.values()) MaxLevel = Math.max(MaxLevel, Record.Hierarchy.Levels.length - 1);
        this.SelectedLevel = Math.min(this.SelectedLevel, MaxLevel);
        this.RefreshLevelButtons(MaxLevel);

        S.replaceChildren();
        let ClusterTotal = 0, TriangleTotal = 0, RootMaxError = 0, BuildMsTotal = 0, Failures = 0, Skipped = 0;
        let BoundsDrawn = 0, TrianglesDrawn = 0;
        const WireframePaths = new Map();
        for (const { Record, Occupants } of PerSet.values())
        {
            const Hierarchy = Record.Hierarchy;
            BuildMsTotal += Record.BuildMs;
            RootMaxError = Math.max(RootMaxError, Record.MaxError);
            const Level = Math.min(this.SelectedLevel, Hierarchy.Levels.length - 1);
            const Entry = Hierarchy.Levels[Level];
            ClusterTotal += Entry.Clusters.length;
            TriangleTotal += Entry.TriangleCount;
            const SoupTriangles = this.Mode !== "bounds" ? CollectHierarchyLevel(Hierarchy, Level) : null;
            for (const Occupant of Occupants)
            {
                const Matrix = Scene.Occupants[Occupant]?.Transform;
                if (!Matrix || Matrix.length < 16) continue;
                if (this.Mode !== "wireframe")
                    for (const Identity of Entry.Clusters)
                    {
                        if (BoundsDrawn >= BoundsCap) { ++Skipped; continue; }
                        const Node = Hierarchy.Clusters[Identity];
                        const Points = Array.from({ length: 8 }, (_, K) =>
                        {
                            const P = Node.Minimum.map((V, A) => (K & (1 << A) ? Node.Maximum[A] : V));
                            return ProjectGizmo(TransformPoint(Matrix, P), Camera, Width, Height);
                        });
                        let Path = "";
                        for (let K = 0; K < 8; ++K) for (let A = 0; A < 3; ++A)
                        {
                            const J = K ^ (1 << A);
                            if (J < K || !Points[K] || !Points[J]) continue;
                            Path += "M" + Points[K][0].toFixed(1) + "," + Points[K][1].toFixed(1) +
                                    "L" + Points[J][0].toFixed(1) + "," + Points[J][1].toFixed(1);
                        }
                        if (!Path) continue;
                        ++BoundsDrawn;
                        const Element = document.createElementNS(SvgNamespace, "path");
                        Element.setAttribute("d", Path);
                        Element.setAttribute("fill", "none");
                        Element.setAttribute("stroke", this.ColourFor(Node, Record.MaxError));
                        Element.setAttribute("stroke-width", "1.2");
                        Element.setAttribute("opacity", "0.85");
                        S.appendChild(Element);
                    }
                if (SoupTriangles)
                    for (let Triangle = 0; Triangle < SoupTriangles.TriangleCount; ++Triangle)
                    {
                        if (TrianglesDrawn >= WireframeTriangleCap) { ++Skipped; continue; }
                        const Node = Hierarchy.Clusters[SoupTriangles.Owners[Triangle]];
                        const Colour = this.ColourFor(Node, Record.MaxError);
                        let Segment = "";
                        const Screen = [];
                        for (let Corner = 0; Corner < 3; ++Corner)
                        {
                            const P = [0, 1, 2].map((A) => SoupTriangles.Positions[Triangle * 9 + Corner * 3 + A]);
                            Screen.push(ProjectGizmo(TransformPoint(Matrix, P), Camera, Width, Height));
                        }
                        if (Screen.some((P) => !P)) continue;
                        for (let Edge = 0; Edge < 3; ++Edge)
                        {
                            const P = Screen[Edge], Q = Screen[(Edge + 1) % 3];
                            Segment += "M" + P[0].toFixed(1) + "," + P[1].toFixed(1) +
                                       "L" + Q[0].toFixed(1) + "," + Q[1].toFixed(1);
                        }
                        WireframePaths.set(Colour, (WireframePaths.get(Colour) ?? "") + Segment);
                        ++TrianglesDrawn;
                    }
            }
        }
        for (const [Colour, Path] of WireframePaths)
        {
            const Element = document.createElementNS(SvgNamespace, "path");
            Element.setAttribute("d", Path);
            Element.setAttribute("fill", "none");
            Element.setAttribute("stroke", Colour);
            Element.setAttribute("stroke-width", "0.7");
            Element.setAttribute("opacity", "0.9");
            S.appendChild(Element);
        }
        for (const Record of this.Builds.values()) if (Record.Failure) ++Failures;
        if (this.Stats)
            this.Stats.textContent =
                PerSet.size + " facet set(s) · showing L" + this.SelectedLevel + " of " + MaxLevel +
                " · " + ClusterTotal + " clusters · " + TriangleTotal + " triangles at this level" +
                " · DAG max error " + RootMaxError.toFixed(4) +
                " · build " + BuildMsTotal.toFixed(0) + " ms" +
                (Skipped ? " · " + Skipped + " items over draw cap" : "") +
                (Failures ? " · " + Failures + " build failure(s): " +
                    [...this.Builds.values()].find((R) => R.Failure).Failure : "") +
                (PerSet.size === 0 ? " · no eligible static facet sets (>=256 facets) in view of the draw runs" : "");
    }
}
