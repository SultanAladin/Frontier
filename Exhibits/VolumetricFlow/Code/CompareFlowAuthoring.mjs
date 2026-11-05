// Run the retained editor's actual JavaScript evaluator and compare it with the native model.
import fs from "node:fs";
import vm from "node:vm";
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";

const [Executable] = process.argv.slice(2);
assert.ok(Executable, "Expected the native validation executable");
const Run = spawnSync(Executable, [], { encoding: "utf8", maxBuffer: 4 * 1024 * 1024 });
assert.equal(Run.status, 0, `native validation failed: ${Run.stderr}`);
const Native = Run.stdout.trim().split("\n").map(Line => JSON.parse(Line));
const Source = fs.readFileSync("Documentation/Plans/References/InheritedEngine/Prototypes/VolumetricFlowSolver.html", "utf8");
function Section(Start, End)
{
    const First = Source.indexOf(Start), Last = Source.indexOf(End, First);
    assert.ok(First >= 0 && Last > First, `Reference section missing: ${Start}`);
    return Source.slice(First, Last);
}
const ReferenceCode = [
    Section("const SolverParameterization =", "const UniformFieldOrder ="),
    Section("const ViewParameterization =", "const ViewByteCount ="),
    Section("const AppearancePresets =", "let EngagedPresetIndex ="),
    "let EngagedPresetIndex = 1; const GraphClaimedFields = new Set();",
    Section("function ApplyAppearancePreset(", "function ConstructPaletteSelector()"),
    "function InscribeGraph() {} function ConstructPaletteSelector() {}",
    Section("const PinCategoryTint =", "const GraphStore ="),
    Section("const GraphStore =", "const GraphEvaluation ="),
    Section("const GraphEvaluation =", "const GraphSurfaces ="),
    Section("function ConstructDefaultGraph()", "//                                                   SETTINGS SHEET"),
    `function Snapshot(Name) { return JSON.stringify({ name: Name, preset: EngagedPresetIndex,
       solver: SolverParameterization, view: ViewParameterization, units: [...GraphStore.Units.values()],
       links: GraphStore.Links, count: GraphEvaluation.ContributingCount, fault: GraphEvaluation.Fault,
       claimed: [...GraphClaimedFields].sort() }); }`
].join("\n");
function NewReference()
{
    const Context = vm.createContext({});
    vm.runInContext(ReferenceCode, Context);
    vm.runInContext("ApplyAppearancePreset(1); ConstructDefaultGraph(); EvaluateGraph();", Context);
    return Context;
}
function Execute(Context, Expression) { return vm.runInContext(Expression, Context); }
const Original = [];
let Context = NewReference();
const Snap = Name => Original.push(JSON.parse(Execute(Context, `Snapshot(${JSON.stringify(Name)})`)));
Snap("default");
for (const [Index, Caption] of ["Smoke", "Fire", "Burning", "Plume", "Explosion"].entries())
{
    Execute(Context, `ApplyAppearancePreset(${Index})`);
    Snap(Caption);
}
Context = NewReference();
Execute(Context, 'GraphStore.Units.get("Unit7").Values.WindGustStrength = 2.25; EvaluateGraph()');
Snap("unreachable gust");
assert.equal(Execute(Context, 'IntegrateLink("Unit8", "Momentum", "Unit2", "Carrier")'), false);
assert.equal(Execute(Context, 'IntegrateLink("Unit4", "Carrier", "Unit3", "Carrier")'), false);
assert.equal(Execute(Context, 'IntegrateLink("Unit3", "Carrier", "Unit3", "Carrier")'), false);
assert.equal(Execute(Context, 'IntegrateLink("Missing", "Carrier", "Unit3", "Carrier")'), false);
assert.equal(Execute(Context, 'IntegrateLink("Unit7", "Momentum", "Unit8", "Momentum")'), true);
Execute(Context, "EvaluateGraph()"); Snap("gust connected");
assert.equal(Execute(Context, `(() => { const Link = GraphStore.Links[0];
    return IntegrateLink(Link.Origin, Link.OriginPin, Link.Target, Link.TargetPin); })()`), true);
Execute(Context, "EvaluateGraph()"); Snap("reconnected");
Execute(Context, 'GraphStore.Units.get("Unit11").Bypassed = true; EvaluateGraph()'); Snap("detail bypassed");
Execute(Context, 'GraphStore.Units.get("Unit11").Bypassed = false; EvaluateGraph()'); Snap("detail restored");
Execute(Context, 'GraphStore.Units.get("Unit16").Bypassed = true; EvaluateGraph()'); Snap("scattering bypassed");
Execute(Context, `GraphStore.Units.get("Unit16").Bypassed = false;
    GraphStore.Links = GraphStore.Links.filter(Link => !(Link.Target === "Unit15" && Link.TargetPin === "Radiance"));
    EvaluateGraph()`);
Snap("pin detached");
Execute(Context, 'DetachUnit("Unit17"); EvaluateGraph()'); Snap("terminal removed");
Execute(Context, `const NewTerminal = IntegrateUnit("Composite", 10, 20);
    IntegrateLink("Unit16", "Radiance", NewTerminal, "Radiance"); EvaluateGraph()`);
Snap("terminal restored");

assert.equal(Native.length, Original.length);
for (let Index = 0; Index < Original.length; ++Index)
{
    assert.deepEqual(Native[Index], Original[Index], `native authoring differs for ${Original[Index].name}`);
    console.log(`${Original[Index].name}: ${Original[Index].units.length} units, ${Original[Index].links.length} links, ` +
                `${Original[Index].count} contributing — exact reference match`);
}
console.log(`Compared ${Original.length} complete native snapshots against the original JavaScript evaluator.`);
