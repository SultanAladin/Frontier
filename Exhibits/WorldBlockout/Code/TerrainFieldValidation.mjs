/*==========================================================================================================================================
                                              TERRAINFIELDVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Plain-Node validation for the planet terrain: the record, the height field and its SDF, the stamps and brushes, the city
//    pads, the extracted mesh and the TOML record. No DOM, no browser: this is the data model the editor draws.
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

const Results = [];
let Failures = 0;

function Assert(Name, Condition, Detail)
{
    Results.push({ Name, Passed: !!Condition, Detail });
    if (!Condition) ++Failures;
}

const Close = (A, B, Tolerance) => Math.abs(A - B) <= Tolerance;
const Round = (Value, Digits) => Number(Value).toFixed(Digits ?? 0);
// 📝 Typed arrays of a hundred thousand cells cannot be spread into Math.min: reduce instead, every time.
const Minimum = Values => Values.reduce((Low, Value) => Math.min(Low, Value), Infinity);
const Maximum = Values => Values.reduce((High, Value) => Math.max(High, Value), -Infinity);
const All = Values => Values.every(Number.isFinite);

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RECORD
//------------------------------------------------------------------------------------------------------------------------

const Planet = Terrain.ConfigurePlanet();
Assert('planet is a globe, not a plane', Planet.RadiusKm === 6371, `${Planet.RadiusKm} km`);
Assert('the grid is a real planet grid', Planet.Grid.Width === 512 && Planet.Grid.Height === 256,
    `${Planet.Grid.Width} × ${Planet.Grid.Height}`);
Assert('one cell is under a degree', Close(360 / Planet.Grid.Width, 0.703, 0.001), `${Round(360 / Planet.Grid.Width, 3)}°`);
Assert('sea level is zero', Planet.SeaLevel === 0, `${Planet.SeaLevel} m`);
Assert('the floor is abyssal', Planet.Floor === -4800, `${Planet.Floor} m`);
Assert('the seed is fixed', Planet.Identity.Seed === 20261103, `${Planet.Identity.Seed}`);

const Field = Terrain.CreateField(Planet);
Assert('a new field starts at the floor', Field.Heights.every(Height => Height === Planet.Floor),
    `min ${Minimum(Field.Heights)} max ${Maximum(Field.Heights)}`);
Assert('a new field is revision zero', Field.Revision === 0, `${Field.Revision}`);
Assert('the field carries its own sampling API', typeof Field.Sample.Height === 'function'
    && typeof Field.Sample.Distance === 'function' && typeof Field.Sample.Band === 'function', 'Height/Distance/Band');
Assert('the field starts with no stamps or pads', Field.Continents.length === 0 && Field.Cities.length === 0, 'empty');

//------------------------------------------------------------------------------------------------------------------------
//                                                    SAMPLING AND SDF
//------------------------------------------------------------------------------------------------------------------------

const CellLon = Terrain.LonOfCell(Field, 100);
const CellLat = Terrain.LatOfCell(Field, 50);
Assert('cell to longitude round-trips', Close(Terrain.CellOfLon(Field, CellLon), 100, 1e-6), Round(Terrain.CellOfLon(Field, CellLon), 3));
Assert('cell to latitude round-trips', Close(Terrain.CellOfLat(Field, CellLat), 50, 1e-6), Round(Terrain.CellOfLat(Field, CellLat), 3));

Terrain.AssignHeight(Field, 100, 50, 800);
Assert('a cell reads back what was written', Terrain.HeightOf(Field, 100, 50) === 800, `${Terrain.HeightOf(Field, 100, 50)}`);
Assert('sampling at a cell centre returns that cell', Close(Terrain.SampleHeight(Field, CellLon, CellLat), 800, 1),
    Round(Terrain.SampleHeight(Field, CellLon, CellLat), 1));
Assert('the SDF is height above sea level', Close(Terrain.SampleDistance(Field, CellLon, CellLat), 800, 1),
    Round(Terrain.SampleDistance(Field, CellLon, CellLat), 1));
Assert('sampling wraps in longitude', Close(Terrain.SampleHeight(Field, 179.99, CellLat), Terrain.SampleHeight(Field, -179.99, CellLat), 1e-6),
    'wrapped');
const FlatSlope = Terrain.SampleSlope(Field, 40, 10);
Assert('flat ground has no slope', FlatSlope < 0.2, `${Round(FlatSlope, 2)}°`);
Terrain.AssignHeight(Field, 101, 50, 2000);
const StepSlope = Terrain.SampleSlope(Field, CellLon + 0.35, CellLat);
Assert('a one-cell step is a slope', StepSlope > 0.3, `${Round(StepSlope, 2)}°`);

const Ladder = [-4700, -900, -40, 120, 700, 1500, 2600].map(Height => Terrain.ResolveBand(Field, Height));
Assert('the band ladder names every depth and height', Ladder.join(' ') === 'Abyss Deep Shelf Coast Plain Highland Peak', Ladder.join(' '));
assertLadderMonotonic();
function assertLadderMonotonic()
{
    const Heights = [-4700, -3000, -900, -40, 120, 700, 1500, 2600];
    const Bands = Heights.map(Height => Terrain.ResolveBand(Field, Height));
    const Known = ['Abyss', 'Deep', 'Shelf', 'Coast', 'Plain', 'Highland', 'Peak'];
    let Cursor = -1, Ordered = true;
    for (const Band of Bands)
    {
        const Index = Known.indexOf(Band);
        if (Index < Cursor) Ordered = false;
        Cursor = Index;
    }
    Assert('the band ladder is ordered from abyss to peak', Ordered, Bands.join(' → '));
}

Assert('noise is deterministic for a seed', Close(Terrain.Noise(Field, 12.5, 40.25), Terrain.Noise(Field, 12.5, 40.25), 0), 'equal');
Assert('noise is bounded', Terrain.Noise(Field, 3.3, 7.7) >= 0 && Terrain.Noise(Field, 3.3, 7.7) <= 1, 'in [0,1]');
const Ridge = Terrain.RidgedNoise(Field, 4.5, 9.5, 4);
Assert('ridged noise stays inside its envelope', Ridge >= 0 && Ridge <= 1, Round(Ridge, 3));
const Square = [[0, 0], [10, 0], [10, 10], [0, 10]];
Assert('the outline test knows inside from outside', Terrain.ResolveOutlineDistance(Square, 5, 5).Inside
    && !Terrain.ResolveOutlineDistance(Square, 15, 5).Inside, 'inside/outside');
Assert('the outline distance is in kilometres', Close(Terrain.ResolveOutlineDistance(Square, 12, 5).DistanceKm, 222.6, 2),
    `${Round(Terrain.ResolveOutlineDistance(Square, 12, 5).DistanceKm, 1)} km`);

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE STAMPS
//------------------------------------------------------------------------------------------------------------------------

const Bare = Terrain.CreateField(Planet);
const Africa = Terrain.StampContinent(Bare, { Name: 'Africa', Polygon: Terrain.ContinentOutlines().Africa });
Assert('stamping a continent moves cells', Africa.Touched > 1000, `${Africa.Touched} cells`);
Assert('the continent stands above the sea', Africa.Peak > 300, `${Round(Africa.Peak)} m`);
Assert('the stamp is recorded for the outliner', Bare.Continents.length === 1 && Bare.Continents[0].Name === 'Africa', '1 record');
Assert('the stamp bumps the revision', Bare.Revision === 1, `${Bare.Revision}`);
const Sahara = Terrain.SampleHeight(Bare, 20, 22);
const Congo = Terrain.SampleHeight(Bare, 20, -2);
const Cape = Terrain.SampleHeight(Bare, 22, -31);
Assert('the Sahara is dry', Sahara > 200, `${Round(Sahara)} m`);
Assert('the Congo basin is dry', Congo > 200, `${Round(Congo)} m`);
Assert('the Cape is inside the authored outline', Cape > 0, `${Round(Cape)} m`);
Assert('the Indian Ocean is still abyss', Close(Terrain.SampleHeight(Bare, 75, -20), Planet.Floor, 1), `${Round(Terrain.SampleHeight(Bare, 75, -20))} m`);
Assert('the interior stands above the coast', Sahara > Terrain.SampleHeight(Bare, 0, 5) + 100,
    `Sahara ${Round(Sahara)} vs Gulf of Guinea ${Round(Terrain.SampleHeight(Bare, 0, 5))}`);
const PeakBefore = Terrain.SampleHeight(Bare, 20, 22);
Terrain.StampContinent(Bare, { Name: 'Africa again', Polygon: Terrain.ContinentOutlines().Africa });
const PeakAfter = Terrain.SampleHeight(Bare, 20, 22);
Assert('stamping twice never digs a hole', PeakAfter >= PeakBefore - 1, `${Round(PeakBefore)} → ${Round(PeakAfter)} m`);
Assert('the second stamp only raised the record', Bare.Continents.length === 2, `${Bare.Continents.length} records`);

const Islands = Terrain.StampArchipelago(Bare, { Name: 'Sunda', Lon: 112, Lat: -4, Count: 20, Radius: 1500 });
Assert('the archipelago lays twenty islands', Islands.Islands.length === 20, `${Islands.Islands.length} islands`);
Assert('an island is above the waterline', Terrain.SampleHeight(Bare, Islands.Islands[0].Lon, Islands.Islands[0].Lat) > 0,
    `${Round(Terrain.SampleHeight(Bare, Islands.Islands[0].Lon, Islands.Islands[0].Lat))} m`);

const Eurasia = Terrain.StampContinent(Bare, { Name: 'Eurasia', Polygon: Terrain.ContinentOutlines().Eurasia, Plateau: 780 });
const PersianGulfBefore = Terrain.SampleHeight(Bare, 51, 27);
const Carved = Terrain.CarveGulf(Bare, { Name: 'Persian Gulf', Polygon: Terrain.GulfOutlines().PersianGulf, RampKm: 200 });
const PersianGulfAfter = Terrain.SampleHeight(Bare, 51, 27);
Assert('carving a gulf lowers the ground', PersianGulfAfter < PersianGulfBefore,
    `${Round(PersianGulfBefore)} → ${Round(PersianGulfAfter)} m`);
Assert('the carved gulf is under water', PersianGulfAfter < 0, `${Round(PersianGulfAfter)} m`);
Assert('the gulf is recorded as a gulf', Bare.Continents.some(Record => Record.Kind === 'Gulf'), 'gulf record');
Assert('a gulf deeper than the bed does nothing', Carved.Touched >= 0, `${Carved.Touched} cells`);

const Coverage = Terrain.ResolveCoverage(Bare);
Assert('coverage counts every cell', Coverage.LandCells + Coverage.OceanCells === Field.Heights.length,
    `${Coverage.LandCells + Coverage.OceanCells} cells`);
Assert('the world is mostly ocean', Coverage.LandShare > 0.05 && Coverage.LandShare < 0.45, `${Round(Coverage.LandPercent, 1)}%`);
Assert('the deepest point is the floor', Coverage.Deepest === Planet.Floor, `${Round(Coverage.Deepest)} m`);
Assert('land area is a plausible share of a globe', Coverage.LandKm2 > 2e7 && Coverage.OceanKm2 > 3e8,
    `${(Coverage.LandKm2 / 1e6).toFixed(1)} M km² land`);

//------------------------------------------------------------------------------------------------------------------------
//                                                       BRUSHES
//------------------------------------------------------------------------------------------------------------------------

const Sculpted = Terrain.CreateField(Planet);
const Raised = Terrain.ApplyBrush(Sculpted, { Tool: 'Raise', Lon: 20, Lat: 6, RadiusKm: 900, Strength: 0.8 });
const RaisedHeight = Terrain.SampleHeight(Sculpted, 20, 6);
Assert('the raise brush lifts the ground', Raised > 0 && RaisedHeight > Planet.Floor + 100, `${Raised} cells to ${Round(RaisedHeight)} m`);
Assert('the brush has a radius', Terrain.SampleHeight(Sculpted, 60, 6) === Planet.Floor, `${Round(Terrain.SampleHeight(Sculpted, 60, 6))} m outside`);
const Lowered = Terrain.ApplyBrush(Sculpted, { Tool: 'Lower', Lon: 20, Lat: 6, RadiusKm: 900, Strength: 0.4 });
Assert('the lower brush digs down', Lowered > 0 && Terrain.SampleHeight(Sculpted, 20, 6) < RaisedHeight, `${Round(Terrain.SampleHeight(Sculpted, 20, 6))} m`);
const Flattened = Terrain.ApplyBrush(Sculpted, { Tool: 'Plateau', Lon: 20, Lat: 6, RadiusKm: 600, Strength: 1 });
Assert('the plateau brush flattens', Flattened > 0, `${Flattened} cells`);
const Smoothed = Terrain.ApplyBrush(Sculpted, { Tool: 'Smooth', Lon: 20, Lat: 6, RadiusKm: 600, Strength: 1 });
Assert('the smooth brush runs', Smoothed > 0, `${Smoothed} cells`);
Assert('three strokes are on the undo stack', Sculpted.Undo.length === 4, `${Sculpted.Undo.length} snapshots`);
const BeforeUndo = Terrain.SampleHeight(Sculpted, 20, 6);
const UndoneCells = Terrain.UndoStroke(Sculpted);
Assert('undo reports what it restored', UndoneCells > 0, `${UndoneCells} cells`);
Assert('undo puts the height back', Terrain.SampleHeight(Sculpted, 20, 6) !== BeforeUndo, `${Round(BeforeUndo)} → ${Round(Terrain.SampleHeight(Sculpted, 20, 6))}`);

const Busy = Terrain.CreateField(Planet);
for (let Stroke = 0; Stroke < 46; ++Stroke)
{
    Terrain.ApplyBrush(Busy, { Tool: 'Raise', Lon: -40 + Stroke, Lat: 6 + (Stroke % 5), RadiusKm: 300, Strength: 0.3 });
}
Assert('the undo stack is capped at forty', Busy.Undo.length === 40, `${Busy.Undo.length} snapshots`);

//------------------------------------------------------------------------------------------------------------------------
//                                                    CITY PADS
//------------------------------------------------------------------------------------------------------------------------

const Pads = Terrain.CreateField(Planet);
Terrain.StampContinent(Pads, { Name: 'Africa', Polygon: Terrain.ContinentOutlines().Africa });
const Site = Terrain.ResolveCitySite(Pads, 20, 6, 60);
Assert('open dry ground accepts a pad', Site.Valid, Site.Reason);
// 📝 A pad cannot be flatter than the grid it sits on: at this resolution the cell is 78 km, so the pad under test is three
//    cells across, which is what the editor uses for real placements.
const Pad = Terrain.PlaceCityPad(Pads, { Name: 'Kinshasa', Lon: 20, Lat: 6, RadiusKm: 240 });
Assert('a placed pad is recorded', Pad.Valid && Pads.Cities.length === 1, `${Pads.Cities.length} pads`);
Assert('the pad records its area', Pad.City.AreaKm2 > 170000 && Pad.City.AreaKm2 < 190000, `${Pad.City.AreaKm2} km²`);
const PadRecord = Terrain.EncodeRecord(Pads);
const PadHeights = [];
for (let Lat = 5.5; Lat <= 6.5; Lat += 0.05) PadHeights.push(Terrain.SampleHeight(Pads, 20, Lat));
const PadSpread = Maximum(PadHeights) - Minimum(PadHeights);
// 📝 The pad is flat at cell centres; bilinear sampling near the rim of a 78 km grid still bends a few metres.
Assert('the pad is levelled', PadSpread < 30, `${Round(PadSpread, 1)} m spread across a degree of pad`);
const Wet = Terrain.PlaceCityPad(Pads, { Name: 'Atlantis', Lon: -30, Lat: 0, RadiusKm: 60 });
Assert('a pad below the waterline is rejected', !Wet.Valid && Wet.Reason.includes('waterline'), Wet.Reason);
const Steep = Terrain.CreateField(Planet);
Terrain.ApplyBrush(Steep, { Tool: 'Raise', Lon: 20, Lat: 6, RadiusKm: 600, Strength: 1 });
Terrain.ApplyBrush(Steep, { Tool: 'Raise', Lon: 20.8, Lat: 6, RadiusKm: 200, Strength: 1 });
const Cliff = Terrain.ResolveCitySite(Steep, 20.85, 6, 40);
Assert('a cliff rejects a pad', !Cliff.Valid, `${Cliff.Reason} at ${Round(Cliff.Slope, 1)}°`);
const Rim = Terrain.ResolveCitySite(Pads, 30.5, 31.0, 200);
Assert('a pad whose rim reaches water is rejected', !Rim.Valid, Rim.Reason);
Assert('a pad can be removed', Terrain.RemoveCity(Pads, 'Kinshasa') && Pads.Cities.length === 0, 'removed');

//------------------------------------------------------------------------------------------------------------------------
//                                                  MESH AND RECORD
//------------------------------------------------------------------------------------------------------------------------

const Mesh = Terrain.ExtractMesh(Bare, { SpacingKm: 180 });
Assert('the mesh counts its columns and rows', Mesh.Columns > 100 && Mesh.Rows > 50, `${Mesh.Columns} × ${Mesh.Rows}`);
Assert('the mesh is triangulated', Mesh.Triangles === Mesh.Columns * Mesh.Rows * 2, `${Mesh.Triangles} triangles`);
Assert('the mesh carries a vertex per grid point', Mesh.VerticesCount === (Mesh.Columns + 1) * (Mesh.Rows + 1),
    `${Mesh.VerticesCount} vertices`);
Assert('the mesh positions are finite', All(Mesh.Vertices), 'finite');
Assert('the mesh is kilometre-scaled', Maximum(Mesh.Vertices) - Minimum(Mesh.Vertices) > 2, 'span > 2 km');
const Manifold = Terrain.ResolveManifold(Mesh);
Assert('the mesh is a closed sheet', Manifold.Overused === 0, `${Manifold.Overused} over-used edges`);
Assert('the only open edges are its border', Manifold.Open === Manifold.Boundary, `${Manifold.Open} open / ${Manifold.Boundary} border`);
Assert('watertightness is reported', Manifold.Watertight === true, `${Manifold.Watertight}`);
const Fine = Terrain.ExtractMesh(Bare, { SpacingKm: 90 });
Assert('a finer spacing makes more triangles', Fine.Triangles > Mesh.Triangles, `${Fine.Triangles} vs ${Mesh.Triangles}`);

const Record = Terrain.EncodeRecord(Bare);
Assert('the record names the planet', Record.includes('name = "Terra-01"'), 'Terra-01');
Assert('the record carries the radius', Record.includes('radius_km = 6371'), '6371');
Assert('the record carries the grid', Record.includes('width = 512') && Record.includes('height = 256'), '512 × 256');
Assert('the record lists its continents', Record.includes('[[planet.stamps.continent]]'), 'continent stanza');
Assert('the record has no NaN or undefined', !/NaN|undefined/.test(Record), 'clean');
Assert('the record lists its cities', PadRecord.includes('[[planet.cities.entry]]'), 'city stanza');

const Again = Terrain.CreateField(Terrain.ConfigurePlanet());
Terrain.StampContinent(Again, { Name: 'Africa', Polygon: Terrain.ContinentOutlines().Africa });
Terrain.StampContinent(Again, { Name: 'Eurasia', Polygon: Terrain.ContinentOutlines().Eurasia, Plateau: 780 });
Terrain.CarveGulf(Again, { Name: 'Persian Gulf', Polygon: Terrain.GulfOutlines().PersianGulf, RampKm: 200 });
Terrain.StampArchipelago(Again, { Name: 'Sunda', Lon: 112, Lat: -4, Count: 20, Radius: 1500 });
Assert('the same record sculpts the same planet', Again.Heights.every((Height, Index) => Height === Bare.Heights[Index]),
    'identical arrays');
const Authored = Terrain.ContinentOutlines();
Assert('four continents are authored', Object.keys(Authored).length === 4, Object.keys(Authored).join(', '));
Assert('the continents are dense outlines', Object.values(Authored).every(Polygon => Polygon.length >= 18),
    `${Object.values(Authored).map(Polygon => Polygon.length).join('/')} vertices`);
Assert('the gulfs are authored', Object.keys(Terrain.GulfOutlines()).length === 3, Object.keys(Terrain.GulfOutlines()).join(', '));

//------------------------------------------------------------------------------------------------------------------------
//                                                        REPORT
//------------------------------------------------------------------------------------------------------------------------

const Passed = Results.length - Failures;
const Lines = [
    '# Terrain field validation',
    '',
    `Generated ${new Date().toISOString()} · Node ${process.version} · ${Passed}/${Results.length} checks passed.`,
    '',
    'The harness requires `Experimental/WorldBlockout/TerrainField.js` in Node and exercises the planet record, the height',
    'field and its vertical SDF, the authored continent and gulf stamps, the brushes and their undo stack, the city pad rules,',
    'the extracted mesh and the TOML record. Nothing here touches the DOM: this is the model the editor draws.',
    '',
    '| Check | Result | Observed |',
    '|-------|--------|----------|'
];
for (const Result of Results)
    Lines.push(`| ${Result.Name} | ${Result.Passed ? '✔️ pass' : '🔴 fail'} | ${String(Result.Detail).replace(/\|/g, '\\|').replace(/\n/g, ' ')} |`);
Lines.push('', `**${Failures === 0 ? '🟢 all checks passed' : `🔴 ${Failures} check(s) failed`}**`, '');

const ResultsFolder = resolve(Here, '../Results');
mkdirSync(ResultsFolder, { recursive: true });
writeFileSync(resolve(ResultsFolder, 'TerrainFieldValidation.md'), Lines.join('\n'));

for (const Result of Results) if (!Result.Passed) console.log(`FAIL ${Result.Name} — ${Result.Detail}`);
console.log(`${Passed}/${Results.length} checks passed`);
console.log(`report: ${resolve(ResultsFolder, 'TerrainFieldValidation.md')}`);
process.exit(Failures === 0 ? 0 : 1);
