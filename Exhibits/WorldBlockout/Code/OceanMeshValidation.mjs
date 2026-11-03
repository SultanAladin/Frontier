/*==========================================================================================================================================
                                              OCEANMESHV ALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Plain-Node validation for the adaptive ocean mesh: the screen-space-error plan, the triangle budget, the near field kept
//    at the solve's own resolution, the far fan, the skirts that close the cracks, and the claim that this is cheaper than a
//    uniform grid for the same surface.
'use strict';

import { createRequire } from 'node:module';
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const Require = createRequire(import.meta.url);
const Here    = dirname(fileURLToPath(import.meta.url));
const Root    = resolve(Here, '../../..');
const Prototype = resolve(Root, 'Experimental/WorldBlockout');
const Terrain = Require(resolve(Prototype, 'TerrainField.js'));
const OceanField = Require(resolve(Prototype, 'OceanField.js'));
const OceanMesh = Require(resolve(Prototype, 'OceanMesh.js'));

const Results = [];
let Failures = 0;

function Assert(Name, Condition, Detail)
{
    Results.push({ Name, Passed: !!Condition, Detail });
    if (!Condition) ++Failures;
}

const Close = (A, B, Tolerance) => Math.abs(A - B) <= Tolerance;
const Round = (Value, Digits) => Number(Value).toFixed(Digits ?? 0);
const AllFinite = Values => Values.every(Number.isFinite);

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE PATCH
//------------------------------------------------------------------------------------------------------------------------

const Field = Terrain.CreateField(Terrain.ConfigurePlanet());
Terrain.StampContinent(Field, { Name: 'Africa', Polygon: Terrain.ContinentOutlines().Africa });
const Ocean = OceanField.CreateOcean(Field);
const Anchor = { Lon: 8.2, Lat: 4 };
const Patch = OceanField.CreatePatch(Ocean, { FocusLon: Anchor.Lon, FocusLat: Anchor.Lat, Cells: 128, ExtentKm: 1.6 });
Ocean.Patch = Patch;
OceanField.SettlePatch(Ocean, Patch, 160);

const Settings = { FocusLon: Anchor.Lon, FocusLat: Anchor.Lat, CameraHeightKm: 2, AdaptiveKm: 12, MaxTriangles: 120000,
    PixelBudget: 2.5 };
const Actual = () => Plan.Settings;
const Plan = OceanMesh.CreatePlan(Ocean, Settings);

Assert('the plan produces tiles', Plan.Leaves.length > 0, `${Plan.Leaves.length} tiles`);
Assert('the plan splits more than once', Plan.Splits > 10, `${Plan.Splits} splits`);
Assert('every tile has a screen error', Plan.Leaves.every(Tile => Tile.Error > 0 && Number.isFinite(Tile.Error)), 'finite');
Assert('the plan prefers the solve footprint', Plan.Leaves.some(Tile => Tile.Near), `${Plan.Leaves.filter(Tile => Tile.Near).length} near tiles`);
Assert('near tiles reach the patch cell', Plan.Leaves.filter(Tile => Tile.Near)
    .every(Tile => Tile.Size / Actual().TileCells <= Plan.NearCellKm + 1e-9),
    `${Round(Math.max(...Plan.Leaves.filter(Tile => Tile.Near).map(Tile => Tile.Size / Actual().TileCells * 1000)), 1)} m near cell`);
Assert('the plan knows its pixel scale', Plan.Pixels > 900 && Plan.Pixels < 1200, `${Round(Plan.Pixels, 0)} px/rad`);

const Mesh = OceanMesh.BuildMesh(Ocean, Plan);
const Summary = OceanMesh.Summarize(Plan, Mesh);

Assert('the mesh holds its triangle budget', Summary.WithinBudget, `${Summary.Triangles} / ${Summary.Budget}`);
Assert('the mesh spent most of its budget', Summary.Triangles > Summary.Budget * 0.9,
    `${Round(Summary.Triangles / Summary.Budget * 100, 1)}%`);
Assert('the mesh has three tiers', Summary.NearTriangles > 0 && Summary.MidTriangles > 0 && Summary.FarTriangles > 0,
    `${Summary.NearTriangles} near / ${Summary.MidTriangles} mid / ${Summary.FarTriangles} far`);
Assert('the near tier is a real share of the budget', Summary.NearTriangles > 15000, `${Summary.NearTriangles} triangles`);
Assert('the far tier is cheap', Summary.FarTriangles < Summary.Triangles * 0.15, `${Summary.FarTriangles} fan triangles`);
Assert('the mesh is cheaper than a uniform grid', Summary.Ratio > 20, `${Round(Summary.Ratio, 0)}× cheaper`);
Assert('the vertices are finite', AllFinite(Mesh.Height) && AllFinite(Mesh.X) && AllFinite(Mesh.Y), 'finite');
Assert('the mesh has vertices and indices', Mesh.Vertices > 1000 && Mesh.Triangles > 1000, `${Mesh.Vertices} / ${Mesh.Triangles}`);
Assert('every index is in range', Array.from(Mesh.Indices).every(Index => Index >= 0 && Index < Mesh.Vertices), 'in range');
Assert('every triangle has three corners', Mesh.Indices.length % 3 === 0, `${Mesh.Indices.length} indices`);
Assert('tile triangles add up to the mesh', Math.abs(Mesh.NearTriangles + Mesh.MidTriangles + Mesh.FarTriangles - Mesh.Triangles) <= 1,
    `${Mesh.NearTriangles + Mesh.MidTriangles + Mesh.FarTriangles} vs ${Mesh.Triangles}`);

//------------------------------------------------------------------------------------------------------------------------
//                                                   CELL SIZE BY DISTANCE
//------------------------------------------------------------------------------------------------------------------------

const Distance = Tile => Math.hypot(Math.max(Tile.X0, Math.min(0, Tile.X0 + Tile.Size)),
    Math.max(Tile.Y0, Math.min(0, Tile.Y0 + Tile.Size)));
const Near = Mesh.Tiles.filter(Tile => Distance(Tile) < 0.5);
const Far = Mesh.Tiles.filter(Tile => Distance(Tile) > 8 && Distance(Tile) < 11);
Assert('near tiles are fine', Near.length > 0 && Near.every(Tile => Tile.CellKm <= 0.02), `worst ${Round(Math.max(...Near.map(Tile => Tile.CellKm)) * 1000, 1)} m`);
Assert('far tiles are coarse', Far.length > 0 && Far.every(Tile => Tile.CellKm > 0.05), `best ${Round(Math.min(...Far.map(Tile => Tile.CellKm)) * 1000, 0)} m`);
Assert('resolution falls away with distance', Math.max(...Near.map(Tile => Tile.CellKm)) < Math.min(...Far.map(Tile => Tile.CellKm)),
    `${Round(Math.max(...Near.map(Tile => Tile.CellKm)) * 1000, 0)} m near vs ${Round(Math.min(...Far.map(Tile => Tile.CellKm)) * 1000, 0)} m far`);
Assert('the finest cell is the near cell', Close(Summary.MinCellKm, Summary.NearCellKm, 0.002),
    `${Round(Summary.MinCellKm * 1000, 1)} m vs patch ${Round(Summary.NearCellKm * 1000, 1)} m`);
// 📝 With this budget the plan is budget-limited by construction, so the claim is not "every tile is under a pixel": it is
//    "the near field is exact and the worst accepted error is reported". The unbinding run below tests the split rule itself.
Assert('the plan reports its worst screen error', Summary.WorstErrorPx > 0 && Summary.WorstErrorPx < 200,
    `worst ${Round(Summary.WorstErrorPx, 1)} px at budget ${Summary.PixelBudget} px`);

// 📝 With a budget that cannot bind, the split rule must actually reach the pixel target — that is the rule being tested, not
//    the size of the machine it runs on.
// 📝 A budget that cannot bind, a pixel target that can actually be met at this scale (a quarter-kilometre cell thirty
//    kilometres out), so what is being tested is the split rule rather than the ceiling above it.
const Generous = OceanMesh.CreatePlan(Ocean, { FocusLon: Anchor.Lon, FocusLat: Anchor.Lat, CameraHeightKm: 20,
    AdaptiveKm: 30, MaxTriangles: 900000, PixelBudget: 12 });
// 📝 A cell cannot be finer than the floor, so tiles already at the floor are allowed to sit over the target.
const OverTarget = Generous.Leaves.filter(Tile => !Tile.Near
    && Tile.Size / Generous.Settings.TileCells > Generous.Settings.MinCellKm + 1e-9);
const GenerousWorst = OverTarget.reduce((Worst, Tile) => Math.max(Worst, Tile.Error), 0);
Assert('an unbinding budget meets the pixel target', GenerousWorst <= Generous.Settings.PixelBudget + 1e-9,
    `worst ${Round(GenerousWorst, 2)} px over ${OverTarget.length} tiles`);
Assert('more budget means more tiles', Generous.Leaves.length > Plan.Leaves.length, `${Generous.Leaves.length} vs ${Plan.Leaves.length}`);

//------------------------------------------------------------------------------------------------------------------------
//                                                    CRACKS AND SKIRTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Neighbours at different levels leave a T-junction; the skirt has to be deeper than the widest step between them.
let WidestStep = 0;
for (const Tile of Mesh.Tiles)
{
    for (const Other of Mesh.Tiles)
    {
        if (Other === Tile) continue;
        const Touches = Math.abs((Tile.X0 + Tile.Size) - Other.X0) < 1e-9 || Math.abs(Tile.X0 - (Other.X0 + Other.Size)) < 1e-9;
        const Overlaps = Tile.Y0 < Other.Y0 + Other.Size && Other.Y0 < Tile.Y0 + Tile.Size;
        if (Touches && Overlaps) WidestStep = Math.max(WidestStep, Math.abs(Tile.CellKm - Other.CellKm));
    }
}
Assert('neighbouring tiles can differ by a level', WidestStep > 0, `${Round(WidestStep * 1000, 1)} m widest step`);
Assert('the skirt covers the widest step', Mesh.SkirtKm >= WidestStep, `skirt ${Round(Mesh.SkirtKm * 1000, 0)} m vs step ${Round(WidestStep * 1000, 1)} m`);

//------------------------------------------------------------------------------------------------------------------------
//                                                     CAMERA AND TIME
//------------------------------------------------------------------------------------------------------------------------

// 📝 Camera height is tested on a budget that cannot bind, or both plans would stop at the same ceiling.
const Wide = { ...Settings, AdaptiveKm: 3, MaxTriangles: 900000 };
const Low = OceanMesh.CreatePlan(Ocean, { ...Wide, CameraHeightKm: 0.2 });
const High = OceanMesh.CreatePlan(Ocean, { ...Wide, CameraHeightKm: 20 });
Assert('a lower camera asks for more detail', Low.Leaves.length > High.Leaves.length, `${Low.Leaves.length} vs ${High.Leaves.length} tiles`);
Assert('a higher camera costs less', Low.Leaves.length * 64 > High.Leaves.length * 64, 'monotonic');

const Heights = Float32Array.from(Mesh.Height);
OceanMesh.RefreshMesh(Ocean, Mesh, Patch.Time + 1.7);
Assert('a refresh moves the surface', Array.from(Mesh.Height).some((Height, Index) => Height !== Heights[Index]), 'moved');
Assert('a refresh keeps the topology', Mesh.Triangles === Summary.Triangles, `${Mesh.Triangles} triangles`);
const Rebuilt = OceanMesh.BuildMesh(Ocean, Plan, { Time: Mesh.Time });
const Difference = Mesh.Height.reduce((Worst, Height, Index) => Math.max(Worst, Math.abs(Height - Rebuilt.Height[Index])), 0);
// 📝 Heights live in a Float32Array, so a refresh is allowed to differ from a rebuild by millimetres, not by waves.
Assert('a refresh matches a rebuild', Difference < 5e-3, `${Difference.toExponential(1)} m worst difference`);

const Weights = {
    Near: Mesh.Weight.reduce((Lowest, Weight, Index) => Distance({ X0: Mesh.X[Index], Y0: Mesh.Y[Index], Size: 0 }) < 0.4
        ? Math.max(Lowest, Weight) : Lowest, 0),
    Far: Mesh.Weight.reduce((Highest, Weight, Index) => Distance({ X0: Mesh.X[Index], Y0: Mesh.Y[Index], Size: 0 }) > 3
        ? Math.min(Highest, Weight) : Highest, 1)
};
Assert('the near vertices are the solve', Weights.Near < 0.2, `weight ${Round(Weights.Near, 2)}`);
Assert('the far vertices are the spectrum', Weights.Far > 0.99, `weight ${Round(Weights.Far, 2)}`);
const DepthKnown = Mesh.Depth.some(Depth => Depth > 0);
Assert('the near vertices know the water depth', DepthKnown, 'depth sampled');

const FarOnly = OceanField.CreateOcean(Field, { NearBlendKm: -1, FarBlendKm: 0.0001 });
FarOnly.Patch = Patch;
const FarPlan = OceanMesh.CreatePlan(FarOnly, Settings);
const FarMesh = OceanMesh.BuildMesh(FarOnly, FarPlan);
Assert('far-field-only leaves every vertex spectral', Array.from(FarMesh.Weight).every(Weight => Weight > 0.99), 'all weight 1');

//------------------------------------------------------------------------------------------------------------------------
//                                                        REPORT
//------------------------------------------------------------------------------------------------------------------------

const Passed = Results.length - Failures;
const Lines = [
    '# Ocean mesh validation',
    '',
    `Generated ${new Date().toISOString()} · Node ${process.version} · ${Passed}/${Results.length} checks passed.`,
    '',
    `At the settings under test (${Settings.AdaptiveKm} km adaptive span, ${Settings.CameraHeightKm} km camera, `
        + `${Settings.PixelBudget} px budget, ${Settings.MaxTriangles} triangle budget) the mesh came out at `
        + `**${Summary.Triangles.toLocaleString('en-GB')} triangles** — ${Summary.NearTriangles.toLocaleString('en-GB')} near, `
        + `${Summary.MidTriangles.toLocaleString('en-GB')} mid, ${Summary.FarTriangles.toLocaleString('en-GB')} far — against `
        + `${Math.round(Summary.UniformTriangles).toLocaleString('en-GB')} for a uniform grid at the near cell size.`,
    '',
    'The harness requires the three ocean modules in Node, builds a plan from a camera, then asserts the budget, the tier',
    'counts, the near field matching the solve resolution, resolution falling with distance, skirts covering the widest crack,',
    'and a height refresh matching a full rebuild.',
    '',
    '| Check | Result | Observed |',
    '|-------|--------|----------|'
];
for (const Result of Results)
    Lines.push(`| ${Result.Name} | ${Result.Passed ? '✔️ pass' : '🔴 fail'} | ${String(Result.Detail).replace(/\|/g, '\\|').replace(/\n/g, ' ')} |`);
Lines.push('', `**${Failures === 0 ? '🟢 all checks passed' : `🔴 ${Failures} check(s) failed`}**`, '');

const ResultsFolder = resolve(Here, '../Results');
mkdirSync(ResultsFolder, { recursive: true });
writeFileSync(resolve(ResultsFolder, 'OceanMeshValidation.md'), Lines.join('\n'));

for (const Result of Results) if (!Result.Passed) console.log(`FAIL ${Result.Name} — ${Result.Detail}`);
console.log(`${Passed}/${Results.length} checks passed · ${Summary.Triangles} triangles · ${Round(Summary.Ratio, 0)}× cheaper`);
console.log(`report: ${resolve(ResultsFolder, 'OceanMeshValidation.md')}`);
process.exit(Failures === 0 ? 0 : 1);
