/*==========================================================================================================================================
                                              OCEANFIELDVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Plain-Node validation for the 2.5D ocean: the spectral sea state, the shallow-water patch, the Courant limit, volume and
//    energy, wave propagation, the swell forcing, the probes and the near/far blend.
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
//                                                       THE RECORD
//------------------------------------------------------------------------------------------------------------------------

const Field = Terrain.CreateField(Terrain.ConfigurePlanet());
Terrain.StampContinent(Field, { Name: 'Africa', Polygon: Terrain.ContinentOutlines().Africa });
const Ocean = OceanField.CreateOcean(Field);

Assert('the ocean record is named after the planet', Ocean.Identity.Name === 'Terra-01-Ocean', Ocean.Identity.Name);
Assert('gravity is real', Ocean.Gravity === 9.81, `${Ocean.Gravity} m/s²`);
Assert('the sea level matches the planet', Ocean.SeaLevel === 0, `${Ocean.SeaLevel} m`);
Assert('the sea state has nine trains', Ocean.Waves.length === 9, `${Ocean.Waves.length} waves`);
Assert('the swell is a couple of metres', Ocean.SwellHeight > 1.5 && Ocean.SwellHeight < 4, `${Round(Ocean.SwellHeight, 2)} m`);
Assert('wavelengths run long to short', Ocean.Waves.every((Wave, Index) => Index === 0
    || Wave.WavelengthM > Ocean.Waves[Index - 1].WavelengthM), `${Round(Ocean.Waves[0].WavelengthM)} → ${Round(Ocean.Waves[8].WavelengthM)} m`);
Assert('amplitudes fall away with wavelength', Ocean.Waves.every((Wave, Index) => Index === 0
    || Wave.AmplitudeM <= Ocean.Waves[Index - 1].AmplitudeM + 1e-9), `${Round(Ocean.Waves[0].AmplitudeM, 2)} → ${Round(Ocean.Waves[8].AmplitudeM, 2)} m`);
const Twin = OceanField.CreateOcean(Field);
Assert('the sea state is deterministic', Twin.Waves.every((Wave, Index) => Wave.WavelengthM === Ocean.Waves[Index].WavelengthM),
    'identical trains');

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE SPECTRAL FIELD
//------------------------------------------------------------------------------------------------------------------------

const Sample = OceanField.SpectralHeight(Ocean, -20, 5, 12);
Assert('the spectral field is finite', Number.isFinite(Sample), `${Round(Sample, 3)} m`);
Assert('the spectral field stays inside its envelope', Math.abs(Sample) <= Ocean.SwellHeight + 1e-6,
    `${Round(Sample, 3)} m of ${Round(Ocean.SwellHeight, 2)} m`);
Assert('the spectral field repeats in time', Close(Sample, OceanField.SpectralHeight(Ocean, -20, 5, 12), 0), 'equal');
const Slopes = OceanField.SpectralSlope(Ocean, -20, 5, 12);
Assert('the spectral field has a slope', Number.isFinite(Slopes.Steepness) && Slopes.Steepness > 0,
    `${Round(Slopes.Steepness, 3)}`);
const Wavelength = Ocean.Waves[0].WavelengthM;
const Period = 2 * Math.PI / Math.sqrt(9.81 * 2 * Math.PI / Wavelength);
Assert('the longest train follows deep-water dispersion', Period > 4 && Period < 9, `period ${Round(Period, 2)} s`);
Assert('the spectral field moved after a period', !Close(OceanField.SpectralHeight(Ocean, -20, 5, 12),
    OceanField.SpectralHeight(Ocean, -20, 5, 12 + Period), 0.001), 'oscillates');

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE PATCH
//------------------------------------------------------------------------------------------------------------------------

// 📝 A coastal patch just west of Africa's Gulf of Guinea shore: the SDF decides which cells are wet.
const Anchor = { Lon: 8.2, Lat: 4 };
const Patch = OceanField.CreatePatch(Ocean, { FocusLon: Anchor.Lon, FocusLat: Anchor.Lat, Cells: 128, ExtentKm: 1.6 });
Ocean.Patch = Patch;
Assert('the patch has the cells it was asked for', Patch.Cells === 128, `${Patch.Cells}`);
Assert('the patch cell is in metres', Close(Patch.CellKm * 1000, 12.5, 0.01), `${Round(Patch.CellKm * 1000, 2)} m`);
Assert('the patch is square in kilometres', Close(Patch.ExtentKm, 1.6, 0.001), `${Patch.ExtentKm} km`);
Assert('the patch reads its depth from the terrain SDF', Patch.Deepest > 0 && Patch.Deepest <= Ocean.MaxDepth,
    `${Round(Patch.Shoalest)}–${Round(Patch.Deepest)} m`);
Assert('every cell is either wet or dry', Patch.Wet.every(Wet => Wet === 0 || Wet === 1), 'binary');
Assert('the patch counts its wet cells', Patch.WetCells > 0 && Patch.WetCells <= Patch.Cells * Patch.Cells, `${Patch.WetCells} cells`);
Assert('dry cells have no depth', Array.from(Patch.Depth).every((Depth, Index) => Depth > 0 || Patch.Wet[Index] === 0),
    'no wet water');

const Celerity = OceanField.ResolveCelerity(Ocean, 4800);
Assert('celerity clamps to the solvable depth', Close(Celerity, Math.sqrt(9.81 * Ocean.SolvableDepth), 1e-6),
    `${Round(Celerity, 2)} m/s at 4800 m`);
const Stable = OceanField.ResolveStableStep(Ocean, Patch);
Assert('the Courant step follows the celerity', Close(Stable, 0.45 * Patch.CellKm * 1000 / OceanField.ResolveMaxCelerity(Ocean, Patch), 1e-9),
    `${Round(Stable, 4)} s`);

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SOLVE
//------------------------------------------------------------------------------------------------------------------------

const StartVolume = Patch.Volume;
const Settled = OceanField.SettlePatch(Ocean, Patch, 200);
Assert('settling advances the clock', Patch.Time > 100, `${Round(Patch.Time, 1)} s`);
Assert('the swell drives the surface', Settled.PeakEta > 0.2, `peak ${Round(Settled.PeakEta, 2)} m`);
Assert('the surface is finite everywhere', AllFinite(Patch.Eta), 'finite');
Assert('the water has energy', Settled.Energy > 0, `${Round(Settled.Energy, 1)} MJ`);
// 📝 The forced ring is a wave maker, so it can add water on purpose; what must not happen is the solver inventing or losing
//    it everywhere else, which the unforced workshop patch below measures to a hundredth of a percent.
Assert('the volume holds through the swell forcing', Math.abs(Patch.Volume - StartVolume) / StartVolume < 2e-3,
    `${((Patch.Volume - StartVolume) / StartVolume * 100).toFixed(3)}% drift`);
Assert('the volume is the depth times the area', Close(Patch.Volume / 1e9, Patch.Deepest * 128 * 128 * Math.pow(Patch.CellKm, 2) / 1e9, 0.5),
    `${Round(Patch.Volume / 1e9, 3)} km³`);

const Before = Patch.Time;
const Stepped = OceanField.StepPatch(Ocean, Patch, 1 / 30);
Assert('a frame is one substep at this scale', Stepped.Substeps === 1, `${Stepped.Substeps} substeps`);
Assert('a frame advances by the frame time', Close(Patch.Time - Before, 1 / 30, 1e-9), `${Round(Patch.Time - Before, 4)} s`);
Assert('the solver reports its step', Stepped.Dt > 0, `${Round(Stepped.Dt, 4)} s`);
for (let Frame = 0; Frame < 240; ++Frame) OceanField.StepPatch(Ocean, Patch, 1 / 30);
Assert('four hundred steps stay finite', AllFinite(Patch.Eta), 'finite');
Assert('the surface keeps moving', Patch.PeakEta > 0.1, `peak ${Round(Patch.PeakEta, 2)} m`);
const Drift = Math.abs(Patch.Volume - StartVolume) / StartVolume;
Assert('a long run does not gain or lose water', Drift < 2e-3, `${(Drift * 100).toFixed(3)}% drift`);

// 📝 The whole point of the solve: a disturbance has to travel. Seed a bump away from the forced ring and watch it spread.
const Workshop = OceanField.CreateOcean(Field, { SwellAmplitude: 0, Damping: 0 });
const Canvas = OceanField.CreatePatch(Workshop, { FocusLon: Anchor.Lon, FocusLat: Anchor.Lat, Cells: 33, ExtentKm: 33 });
Workshop.Patch = Canvas;
const Centre = 16 * 33 + 16;
Canvas.Eta[Centre] = 2;
const Spread = [];
for (let Round = 0; Round < 6; ++Round)
{
    for (let Step = 0; Step < 40; ++Step) OceanField.StepPatch(Workshop, Canvas, OceanField.ResolveStableStep(Workshop, Canvas));
    Spread.push(Math.abs(Canvas.Eta[Centre - 6]) + Math.abs(Canvas.Eta[Centre + 6]));
}
Assert('a bump spreads outwards', Spread[5] > 0.01, `neighbours at ${Round(Spread[5], 3)} m after 40 steps`);
Assert('the spread is symmetric', Close(Math.abs(Canvas.Eta[16 * 33 + 16 - 10]), Math.abs(Canvas.Eta[16 * 33 + 16 + 10]), 0.6),
    `${Round(Canvas.Eta[16 * 33 + 6], 3)} / ${Round(Canvas.Eta[16 * 33 + 26], 3)}`);
const WorkshopVolume = Canvas.Volume;
for (let Step = 0; Step < 200; ++Step) OceanField.StepPatch(Workshop, Canvas, OceanField.ResolveStableStep(Workshop, Canvas));
Assert('a deep-water run conserves volume', Math.abs(Canvas.Volume - WorkshopVolume) / WorkshopVolume < 1e-5,
    `${((Canvas.Volume - WorkshopVolume) / WorkshopVolume * 100).toExponential(1)}% drift`);

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROBES AND BLEND
//------------------------------------------------------------------------------------------------------------------------

OceanField.AddProbe(Patch, { Name: 'Surf', Lon: Anchor.Lon + 0.0016, Lat: Anchor.Lat });
OceanField.AddProbe(Patch, { Name: 'Off', Lon: Anchor.Lon - 0.0046, Lat: Anchor.Lat });
Assert('probes are recorded', Patch.Probes.length === 2, `${Patch.Probes.length} probes`);
for (let Frame = 0; Frame < 30; ++Frame) OceanField.StepPatch(Ocean, Patch, 1 / 30);
Assert('probes hold a history', Patch.Probes[0].Samples.length > 5, `${Patch.Probes[0].Samples.length} samples`);
Assert('probes report their own height', Patch.Probes.every(Probe => Number.isFinite(Probe.Height)), 'finite');
Assert('a probe reports whether it is wet', Patch.Probes.some(Probe => Probe.Wet), `${Patch.Probes.filter(Probe => Probe.Wet).length} wet`);

Assert('the blend is closed near the viewer', OceanField.BlendWeight(Ocean, 0) === 0, 'weight 0');
Assert('the blend is open past the far radius', OceanField.BlendWeight(Ocean, 4) === 1, 'weight 1');
Assert('the blend rises monotonically', [0, 0.25, 0.5, 1, 1.4, 2, 3].every((Distance, Index, Distances) => Index === 0
    || OceanField.BlendWeight(Ocean, Distance) >= OceanField.BlendWeight(Ocean, Distances[Index - 1])), 'monotonic');
Assert('the blend is smooth at the seam', Math.abs(OceanField.BlendWeight(Ocean, Ocean.FarBlendKm + 0.001)
    - OceanField.BlendWeight(Ocean, Ocean.FarBlendKm - 0.001)) < 1e-3, 'continuous');
const Near = OceanField.SurfaceHeight(Ocean, Anchor.Lon, Anchor.Lat, { Time: Patch.Time });
Assert('the surface is near field at the focus', Near.Weight === 0 && Number.isFinite(Near.Height),
    `weight ${Near.Weight}, ${Round(Near.Height, 2)} m`);
const Far = OceanField.SurfaceHeight(Ocean, Anchor.Lon + 0.4, Anchor.Lat, { Time: Patch.Time });
Assert('the surface is far field away from the focus', Far.Weight === 1, `weight ${Far.Weight}`);
const EdgeBlend = OceanField.SurfaceHeight(Ocean, Anchor.Lon + 0.0063, Anchor.Lat, { Time: Patch.Time });
Assert('the surface blends between the two', EdgeBlend.Weight > 0 && EdgeBlend.Weight < 1, `weight ${Round(EdgeBlend.Weight, 2)}`);

const Record = OceanField.EncodeRecord(Ocean);
Assert('the record names the solver', Record.includes('saint-venant 2.5D'), 'saint-venant 2.5D');
Assert('the record carries the sea state', (Record.match(/wave = \{/g) || []).length === 9, '9 wave rows');
Assert('the record has no NaN or undefined', !/NaN|undefined/.test(Record), 'clean');
const Summary = OceanField.ResolveSummary(Ocean);
Assert('the summary reports the patch', Summary.Patch.Cells === 128 && Summary.Patch.WetCells > 0, `${Summary.Patch.Cells}² cells`);
Assert('the summary reports the celerity', Summary.Patch.Celerity > 0, `${Round(Summary.Patch.Celerity, 1)} m/s`);

//------------------------------------------------------------------------------------------------------------------------
//                                                        REPORT
//------------------------------------------------------------------------------------------------------------------------

const Passed = Results.length - Failures;
const Lines = [
    '# Ocean field validation',
    '',
    `Generated ${new Date().toISOString()} · Node ${process.version} · ${Passed}/${Results.length} checks passed.`,
    '',
    'The harness requires `Experimental/WorldBlockout/OceanField.js` and exercises the sea state, the Saint-Venant patch, the',
    'Courant limit, volume and energy, wave propagation away from a seeded bump, the swell forcing, the probes and the',
    'near/far blend. Depth comes from the terrain SDF, so the patch is water only where the planet says water is.',
    '',
    '| Check | Result | Observed |',
    '|-------|--------|----------|'
];
for (const Result of Results)
    Lines.push(`| ${Result.Name} | ${Result.Passed ? '✔️ pass' : '🔴 fail'} | ${String(Result.Detail).replace(/\|/g, '\\|').replace(/\n/g, ' ')} |`);
Lines.push('', `**${Failures === 0 ? '🟢 all checks passed' : `🔴 ${Failures} check(s) failed`}**`, '');

const ResultsFolder = resolve(Here, '../Results');
mkdirSync(ResultsFolder, { recursive: true });
writeFileSync(resolve(ResultsFolder, 'OceanFieldValidation.md'), Lines.join('\n'));

for (const Result of Results) if (!Result.Passed) console.log(`FAIL ${Result.Name} — ${Result.Detail}`);
console.log(`${Passed}/${Results.length} checks passed`);
console.log(`report: ${resolve(ResultsFolder, 'OceanFieldValidation.md')}`);
process.exit(Failures === 0 ? 0 : 1);
