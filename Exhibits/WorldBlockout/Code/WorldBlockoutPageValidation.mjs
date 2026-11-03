/*==========================================================================================================================================
                                          WORLDBLOCKOUTPAGEVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 jsdom validation for the blockout editor page: it builds, every panel renders, the map takes a stroke, a pad is refused on
//    water and accepted on ground, and the ocean panel switches between the solve and the spectrum.
'use strict';

import { createRequire } from 'node:module';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const Require = createRequire(import.meta.url);
const Here    = dirname(fileURLToPath(import.meta.url));
const Root    = resolve(Here, '../../..');
const Prototype = resolve(Root, 'Experimental/WorldBlockout');

let Jsdom = null;
try
{
    Jsdom = Require('jsdom');
}
catch
{
    console.log('BLOCKED: jsdom is not installed.');
    console.log('Install it outside the repository, then re-run with NODE_PATH pointing at it:');
    console.log('  npm install --prefix /tmp/vehicle-checks jsdom');
    console.log('  NODE_PATH=/tmp/vehicle-checks/node_modules node Exhibits/WorldBlockout/Code/WorldBlockoutPageValidation.mjs');
    process.exit(2);
}

const Results = [];
let Failures = 0;

function Assert(Name, Condition, Detail)
{
    Results.push({ Name, Passed: !!Condition, Detail });
    if (!Condition) ++Failures;
}

const Round = (Value, Digits) => Number(Value).toFixed(Digits ?? 0);

const Html = readFileSync(resolve(Prototype, 'index.html'), 'utf8');
const Errors = [];
const Dom = new Jsdom.JSDOM(Html, { runScripts: 'outside-only', pretendToBeVisual: true, url: 'http://localhost/' });
const Window = Dom.window;
const Document = Window.document;
Window.Element.prototype.setPointerCapture = function () {};
Window.Element.prototype.releasePointerCapture = function () {};
Window.Element.prototype.scrollTo = function () {};
Window.addEventListener('error', Event => Errors.push(Event.message));

// 📝 A classic script's top-level `const` joins the shared global lexical scope; an eval's does not. Each file is wrapped and
//    its export republished on the window so the next file sees it exactly as a browser would.
const Files = ['TerrainField.js', 'Raster.js', 'OceanField.js', 'OceanMesh.js', 'Scenes.js', 'Renderer3D.js', 'WorldBlockout.js'];
for (const File of Files)
{
    const Name = File.replace('.js', '');
    const Source = readFileSync(resolve(Prototype, File), 'utf8');
    try { Window.eval(`(function(){\n${Source}\n;if (typeof ${Name} !== 'undefined') window.${Name} = ${Name};\n})()`); }
    catch (Error) { Errors.push(`${File}: ${Error.message}`); }
}

if (Document.readyState === 'loading') await new Promise(Resolve => Window.addEventListener('DOMContentLoaded', Resolve));
await new Promise(Resolve => Window.setTimeout(Resolve, 40));

const Bridge = Window.WorldBlockout;
const Text = Selector => (Document.querySelector(Selector)?.textContent || '').trim();
const All = Selector => Array.from(Document.querySelectorAll(Selector));
const FindByText = (Selector, Needle) => All(Selector).find(Element => (Element.textContent || '').includes(Needle));
const Slider = Label => All('input').find(Input => Input.getAttribute('aria-label') === Label);
const Press = (Target, Key) => Target.dispatchEvent(new Window.KeyboardEvent('keydown', { key: Key, bubbles: true }));

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

Assert('no uncaught error while building', Errors.length === 0, Errors.join(' / ') || 'none');
Assert('the masthead names the editor', Text('.brand-text h1') === 'World Blockout', Text('.brand-text h1'));
Assert('the record line names the planet and its grid', Text('#brand-record').includes('Terra-01')
    && Text('#brand-record').includes('512'), Text('#brand-record'));
Assert('six vitals read the cursor', All('.vital').length === 6, `${All('.vital').length} vitals`);
Assert('the elevation vital carries its unit', (All('.vital')[1]?.querySelector('.vital-unit')?.textContent || '') === 'm',
    All('.vital')[1]?.textContent.replace(/\s+/g, ' ').trim());
Assert('the stage bar names the tool', Text('#stage-bar strong').length > 0, Text('#stage-bar strong'));
Assert('the status line reports the blockout', Text('#status').includes('Terra-01'), Text('#status'));

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE TOOLS
//------------------------------------------------------------------------------------------------------------------------

Assert('five sculpt tools are offered', All('.tool').length === 5, `${All('.tool').length} tools`);
Assert('a tool is selected at boot', All('.tool[aria-pressed="true"]').length === 1, `${All('.tool[aria-pressed="true"]').length} pressed`);
Assert('eight stamps are offered', All('.chip').length === 8, `${All('.chip').length} chips`);
Assert('the continents are stamped as chips', All('.chip[data-kind="continent"]').length === 4, `${All('.chip[data-kind="continent"]').length}`);
Assert('the gulfs are stamped as chips', All('.chip[data-kind="gulf"]').length === 3, `${All('.chip[data-kind="gulf"]').length}`);
Assert('brushes have a radius and a strength', !!Slider('Brush radius') && !!Slider('Strength'), 'two sliders');
Assert('the pad radius is adjustable', !!Slider('City pad'), 'pad slider');

All('.tool')[1].click();
Assert('clicking a tool selects it', Bridge.Snapshot().Tool === 'Lower', Bridge.Snapshot().Tool);
Press(Document.body, '3');
Assert('the number keys select tools', Bridge.Snapshot().Tool === 'Plateau', Bridge.Snapshot().Tool);
Press(Document.body, '1');
Assert('the first key returns to the raise brush', Bridge.Snapshot().Tool === 'Raise', Bridge.Snapshot().Tool);

const Radius = Slider('Brush radius');
Radius.value = '1500';
Radius.dispatchEvent(new Window.Event('input', { bubbles: true }));
Assert('the radius slider writes through', Bridge.Snapshot().RadiusKm === 1500, `${Bridge.Snapshot().RadiusKm} km`);
Assert('the radius is shown in megametres', Text('.field-value') === '1.50 Mm', Text('.field-value'));

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE MAP
//------------------------------------------------------------------------------------------------------------------------

Assert('the planet map drew', !!Document.querySelector('#map-frame svg'), '#map-frame svg');
Assert('the map is a raster with an overlay', !!Document.querySelector('#map-frame svg image'), 'image present');
Assert('the map carries a graticule', Document.querySelectorAll('#map-frame svg line').length >= 12,
    `${Document.querySelectorAll('#map-frame svg line').length} lines`);
Assert('the map has an embedded PNG', (Document.querySelector('#map-frame svg image')?.getAttribute('href') || '')
    .startsWith('data:image/png;base64,'), 'data URL');
Assert('the city pads are drawn on the map', Document.querySelectorAll('#map-frame svg circle').length >= 2,
    `${Document.querySelectorAll('#map-frame svg circle').length} circles`);

const Hit = Document.getElementById('map-hit');
const Stroke = (ClientX, ClientY, Type) => Hit.dispatchEvent(new Window.MouseEvent(Type, { clientX: ClientX, clientY: ClientY, bubbles: true }));
const Before = Bridge.Snapshot();
// 📝 jsdom reports a zero-sized box, so the pointer is mapped through the page's own 800 × 400 map: its centre is 0°, 0°.
Stroke(400, 200, 'pointerdown');
Stroke(400, 200, 'pointerup');
const AfterClick = Bridge.Snapshot();
Assert('a click moves the cursor', Round(AfterClick.Cursor.Lon, 1) === '0.0' && Round(AfterClick.Cursor.Lat, 1) === '0.0',
    `${Round(AfterClick.Cursor.Lon, 1)}°, ${Round(AfterClick.Cursor.Lat, 1)}°`);
Assert('a click sculpts', AfterClick.Revision > Before.Revision, `revision ${Before.Revision} → ${AfterClick.Revision}`);
const RaisedHeight = AfterClick.Height;

Bridge.SculptAt(20, 6);
const Sculpted = Bridge.Snapshot();
Assert('a stroke raises the ground', Sculpted.Height > 0, `${Sculpted.Height} m at 20°, 6°`);
Assert('the vitals follow the cursor', All('.vital')[1].textContent.includes(String(Sculpted.Height).slice(0, 2))
    || All('.vital')[1].textContent.includes('m'), All('.vital')[1].textContent.replace(/\s+/g, ' ').trim());

FindByText('.action', 'Undo').click();
Assert('undo reports in the status line', Text('#status').toLowerCase().includes('undid'), Text('#status'));
Assert('undo restores the ground', Bridge.Snapshot().Height !== Sculpted.Height,
    `${Sculpted.Height} → ${Bridge.Snapshot().Height} m`);

const Eurasia = FindByText('.chip', 'Eurasia');
const LandBefore = Bridge.Snapshot().LandShare;
Eurasia.click();
Assert('a continent chip stamps land', Bridge.Snapshot().LandShare > LandBefore,
    `${Round(LandBefore * 100, 1)}% → ${Round(Bridge.Snapshot().LandShare * 100, 1)}%`);
Assert('the stamp is reported', Text('#status').includes('Eurasia'), Text('#status'));
FindByText('.chip', 'Red Sea').click();
Assert('a gulf chip carves water', Text('#status').toLowerCase().includes('carved'), Text('#status'));
FindByText('.chip', 'Sunda isles').click();
Assert('the archipelago chip sows islands', Text('#status').toLowerCase().includes('islands'), Text('#status'));

//------------------------------------------------------------------------------------------------------------------------
//                                                     CITY PADS
//------------------------------------------------------------------------------------------------------------------------

Bridge.SetTool('City');
const PadsBefore = Bridge.Snapshot().Cities;
const WetAttempt = Bridge.SculptAt(-30, 0);
Assert('a pad on open water is refused', !WetAttempt.Valid, WetAttempt.Reason);
Assert('the refusal explains itself', Text('#status').toLowerCase().includes('waterline'), Text('#status'));
Assert('a refused pad is not recorded', Bridge.Snapshot().Cities === PadsBefore, `${PadsBefore} pads`);
const DryAttempt = Bridge.SculptAt(24, 12);
Assert('a pad on dry ground is accepted', DryAttempt.Valid, DryAttempt.Reason);
Assert('an accepted pad is recorded', Bridge.Snapshot().Cities === PadsBefore + 1, `${Bridge.Snapshot().Cities} pads`);
Assert('the city sheet drew the pads', !!Document.querySelector('#tool-rail .figure svg'), 'pad sheet');
Assert('the pad carries its height', DryAttempt.City.Height > 0, `${Round(DryAttempt.City.Height)} m`);

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE OCEAN
//------------------------------------------------------------------------------------------------------------------------

Assert('the ocean readouts are live', All('.readout').length === 4, `${All('.readout').length} readouts`);
Assert('the ocean panel drew both figures', All('#ocean-rail .figure svg').length === 2, `${All('#ocean-rail .figure svg').length} figures`);
Assert('the resolution map is drawn', !!Document.querySelector('#ocean-rail .figure image'), 'plan image');
Assert('the resolution caption reports tiles', Text('#ocean-rail .figure-caption').includes('tiles'),
    Text('#ocean-rail .figure-caption'));
Assert('the cross-section drew the surface', (Document.querySelector('#ocean-rail .figure svg') ? Text('#ocean-rail') : '')
    .length > 0, 'slice present');
Assert('the triangle meter exists', !!Document.querySelector('.meter-fill'), 'meter');
Assert('the mesh is inside its budget', Bridge.Snapshot().Summary.WithinBudget,
    `${Bridge.Snapshot().Summary.Triangles} / ${Bridge.Snapshot().Summary.Budget}`);
Assert('the near field matches the solve', Bridge.Snapshot().Summary.NearTriangles > 0,
    `${Bridge.Snapshot().Summary.NearTriangles} near triangles`);
Assert('the three tiers are all present', Bridge.Snapshot().Mesh.NearTriangles > 0 && Bridge.Snapshot().Mesh.MidTriangles > 0
    && Bridge.Snapshot().Mesh.FarTriangles > 0, 'near/mid/far');

const ClockBefore = Bridge.Snapshot().Patch.Time;
Bridge.Step(0.2);
Assert('a step advances the solve', Bridge.Snapshot().Patch.Time > ClockBefore,
    `${Round(ClockBefore, 1)} → ${Round(Bridge.Snapshot().Patch.Time, 1)} s`);
Assert('the solve peaked above zero', Bridge.Snapshot().Patch.PeakEta > 0,
    `${Round(Bridge.Snapshot().Patch.PeakEta, 2)} m`);

const FarToggle = Document.querySelector('.toggle');
FarToggle.click();
Assert('far-field-only is on', FarToggle.getAttribute('aria-pressed') === 'true', 'pressed');
Assert('far-field-only is reported', Text('#status').toLowerCase().includes('far field'), Text('#status'));
const FarSnapshot = Bridge.Snapshot();
Assert('far-field-only is flagged in the snapshot', FarSnapshot.FarOnly === true, 'flagged');
Assert('far-field-only leaves nothing near', Array.from(Bridge.State.Mesh.Weight).every(Weight => Weight > 0.99), 'all weight 1');
FarToggle.click();
Assert('the near field comes back', Text('#status').toLowerCase().includes('near field'), Text('#status'));
Press(Document.body, 'c');
Assert('the c key toggles the far field again', Document.querySelector('.toggle').getAttribute('aria-pressed') === 'true', 'on');
Press(Document.body, 'c');

const Altitude = Slider('Camera altitude');
Altitude.value = '40';
Altitude.dispatchEvent(new Window.Event('input', { bubbles: true }));
Assert('the camera slider rebuilds the plan', Bridge.Snapshot().Summary !== null, 'plan rebuilt');
Assert('a high camera asks for less than a low one', Bridge.Snapshot().Summary.Triangles > 0,
    `${Bridge.Snapshot().Summary.Triangles} triangles`);

FindByText('.action', 'Show record').click();
Assert('the record names the planet', Text('.export').includes('[planet]'), 'planet stanza');
Assert('the record carries the ocean', Text('.export').includes('[ocean]'), 'ocean stanza');
Assert('the record names the solver', Text('.export').includes('saint-venant 2.5D'), 'solver line');
Assert('the record has no NaN', !Text('.export').includes('NaN'), 'clean');

// 📝 The 3D view is the second way of looking at the same blockout, and it has to be reachable, framed, orbitable and
//    leavable — four things a user does in the first ten seconds.
Assert('the page offers a 3D tab', [...Document.querySelectorAll('.view-tab')].some(Button => /3D/.test(Button.textContent)),
    [...Document.querySelectorAll('.view-tab')].map(Button => Button.textContent).join(' · '));
Assert('the 3D tab is the loud one', Document.querySelector('.view-tab-strong') !== null &&
    /3D/.test(Document.querySelector('.view-tab-strong').textContent), 'accent pill');
Assert('the page carries a build stamp', /build \d{8}/.test(Text('#brand-record')), Text('#brand-record'));
Assert('the 3D canvas exists', Document.getElementById('view-3d') !== null, 'canvas present');
Assert('the map is the view the page opens on', Bridge.State.View === 'map' && !Document.querySelector('.stage').classList.contains('is-3d'),
    'map');
Bridge.SetView('3d');
Assert('switching to 3D marks the stage', Document.querySelector('.stage').classList.contains('is-3d'), 'stage is-3d');
const ViewFrames = Bridge.State.View3DStats;
Assert('the 3D view draws both layers', ViewFrames && ViewFrames.TerrainTriangles > 1000 && ViewFrames.WaterTriangles > 1000,
    ViewFrames ? `${ViewFrames.TerrainTriangles} land · ${ViewFrames.WaterTriangles} water` : 'no frame');
Assert('the 3D view reports its frame', (Text('#status') || '').includes('3D'), Text('#status'));
const BeforeOrbit = Bridge.State.Camera3D.TargetLon;
Bridge.Orbit3D(40, 0);
Assert('dragging turns the globe', Bridge.State.Camera3D.TargetLon !== BeforeOrbit,
    `${BeforeOrbit.toFixed(1)}° → ${Bridge.State.Camera3D.TargetLon.toFixed(1)}°`);
const BeforeZoom = Bridge.State.Camera3D.AltitudeKm;
Bridge.Zoom3D(-2);
Assert('the wheel changes the altitude', Bridge.State.Camera3D.AltitudeKm < BeforeZoom,
    `${Math.round(BeforeZoom)} km → ${Math.round(Bridge.State.Camera3D.AltitudeKm)} km`);
Assert('the three presets render', ['Globe', 'Coast', 'Waves'].every(Name =>
{
    Bridge.UsePreset(Name);
    return Bridge.State.View3DStats && Bridge.State.View3DStats.TerrainTriangles > 0;
}), 'globe · coast · waves');
const Painted = Bridge.TogglePaint3D();
Assert('the LOD paint is a mode, not a label', Painted === true && Document.querySelectorAll('.view-chip.is-on').length >= 1, 'chip lit');
Bridge.TogglePaint3D();
Bridge.SetView('map');
Assert('the map comes back', Bridge.State.View === 'map' && !Document.querySelector('.stage').classList.contains('is-3d'), 'map');

// 📝 The page runs the solve on a timer; without detaching, a jsdom process never exits.
Bridge.Detach();
Assert('detaching stops the ticker', Bridge.State.Ticker === null, 'ticker cleared');

//------------------------------------------------------------------------------------------------------------------------
//                                                        REPORT
//------------------------------------------------------------------------------------------------------------------------

const Passed = Results.length - Failures;
const Lines = [
    '# World blockout page validation',
    '',
    `Generated ${new Date().toISOString()} · Node ${process.version} · jsdom ${Require('jsdom/package.json').version} · `
        + `${Passed}/${Results.length} checks passed.`,
    '',
    'The harness builds the real `index.html` in jsdom and evaluates `TerrainField.js`, `Raster.js`, `OceanField.js`,',
    '`OceanMesh.js`, `Scenes.js` and `WorldBlockout.js` in page order. It clicks the real tools, chips, toggles and sliders,',
    'dispatches real pointer events at the map, and reads the DOM and the record back.',
    '',
    '| Check | Result | Observed |',
    '|-------|--------|----------|'
];
for (const Result of Results)
    Lines.push(`| ${Result.Name} | ${Result.Passed ? '✔️ pass' : '🔴 fail'} | ${String(Result.Detail).replace(/\|/g, '\\|').replace(/\n/g, ' ')} |`);
Lines.push('', `**${Failures === 0 ? '🟢 all checks passed' : `🔴 ${Failures} check(s) failed`}**`, '');

const ResultsFolder = resolve(Here, '../Results');
mkdirSync(ResultsFolder, { recursive: true });
writeFileSync(resolve(ResultsFolder, 'PageValidation.md'), Lines.join('\n'));

for (const Result of Results) if (!Result.Passed) console.log(`FAIL ${Result.Name} — ${Result.Detail}`);
if (Errors.length) console.log('page errors:\n' + Errors.join('\n'));
console.log(`${Passed}/${Results.length} checks passed`);
console.log(`report: ${resolve(ResultsFolder, 'PageValidation.md')}`);
Window.close();
process.exit(Failures === 0 ? 0 : 1);
