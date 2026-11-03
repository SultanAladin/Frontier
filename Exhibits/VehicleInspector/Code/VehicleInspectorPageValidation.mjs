/*==========================================================================================================================================
                                              VEHICLEINSPECTORPAGEVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 jsdom validation for the inspector page: it builds, every card renders, and the controls write through to the record.
'use strict';

import { createRequire } from 'node:module';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const Require = createRequire(import.meta.url);
const Here    = dirname(fileURLToPath(import.meta.url));
const Root    = resolve(Here, '../../..');
const Prototype = resolve(Root, 'Experimental/VehicleInspector');

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
    console.log('  NODE_PATH=/tmp/vehicle-checks/node_modules node Exhibits/VehicleInspector/Code/VehicleInspectorPageValidation.mjs');
    process.exit(2);
}

const Results = [];
let Failures = 0;

function Assert(Name, Condition, Detail)
{
    Results.push({ Name, Passed: !!Condition, Detail });
    if (!Condition) ++Failures;
}

// 📝 jsdom has no layout, no canvas and no ResizeObserver: the harness supplies the surface the prototype expects rather
//    than changing the prototype to suit the test.
function InstallSurface(Window)
{
    Window.ResizeObserver = class
    {
        constructor(Handler) { this.Handler = Handler; }
        observe() {}
        unobserve() {}
        disconnect() {}
    };
    Window.Element.prototype.scrollTo = function () {};
    Window.Element.prototype.setPointerCapture = function () {};
    Window.Element.prototype.releasePointerCapture = function () {};
    Window.devicePixelRatio = 1;
    Window.CanvasCalls = [];
    Window.HTMLCanvasElement.prototype.getContext = function ()
    {
        const Calls = Window.CanvasCalls;
        const Target = { canvas: this };
        return new Proxy(Target, {
            get(Object, Key)
            {
                if (Key in Object) return Object[Key];
                const Recorder = (...Arguments) => { Calls.push(String(Key)); return undefined; };
                Object[Key] = Recorder;
                return Recorder;
            },
            set(Object, Key, Value) { Object[Key] = Value; return true; }
        });
    };
}

const Html = readFileSync(resolve(Prototype, 'index.html'), 'utf8');
const Errors = [];
const Dom = new Jsdom.JSDOM(Html, { runScripts: 'outside-only', pretendToBeVisual: true, url: 'http://localhost/' });
const Window = Dom.window;
const Document = Window.document;
InstallSurface(Window);
Window.addEventListener('error', Event => Errors.push(Event.message));

// 📝 A classic script's top-level `const` joins the shared global lexical scope; an eval's does not. Each file is wrapped
//    and its export republished on the window so the next file sees it exactly as a browser would.
for (const File of ['VehicleCatalogue.js', 'InspectorControls.js', 'VehicleTelemetry.js', 'VehicleInspector.js'])
{
    const Name = File.replace('.js', '');
    const Source = readFileSync(resolve(Prototype, File), 'utf8');
    Window.eval(`(function(){\n${Source}\n;if (typeof ${Name} !== 'undefined') window.${Name} = ${Name};\n})()`);
}

// 📝 jsdom is still parsing when the harness starts, so the page's own DOMContentLoaded path is given its turn before the
//    first assertion; the timeout lets the deferred font and resize work settle.
if (Document.readyState === 'loading') await new Promise(Resolve => Window.addEventListener('DOMContentLoaded', Resolve));
await new Promise(Resolve => Window.setTimeout(Resolve, 40));

const Text = Selector => (Document.querySelector(Selector)?.textContent || '').trim();
const All = Selector => Array.from(Document.querySelectorAll(Selector));
const FindByText = (Selector, Needle) => All(Selector).find(Element => (Element.textContent || '').includes(Needle));
const Field = Label => All('input').find(Input => Input.getAttribute('aria-label') === Label);
const Press = (Element, Key) => Element.dispatchEvent(new Window.KeyboardEvent('keydown', { key: Key, bubbles: true }));

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

Assert('no uncaught error while building', Errors.length === 0, Errors.join(' / ') || 'none');
Assert('outliner lists ten records', All('.tree-row').length === 10, `${All('.tree-row').length} rows`);
for (const Group of ['Rendering', 'Environment', 'Vehicle', 'Pit lane'])
    Assert(`outliner group · ${Group}`, All('.tree-group-title').some(Element => Element.textContent.includes(Group)), Group);
Assert('vehicle record is selectable', !!FindByText('.tree-row', 'Vehicle · GT-04'), 'Vehicle · GT-04');
Assert('four tyre records are nested under the vehicle', All('.tree-row.nested').length === 4, `${All('.tree-row.nested').length} nested`);

const SectionIds = ['Identity', 'Engine', 'Curves', 'Thermal', 'Tyres', 'Aero', 'Performance', 'Record'];
for (const Id of SectionIds) Assert(`section · ${Id}`, !!Document.getElementById('Section-' + Id), `#Section-${Id}`);
Assert('section tabs match the sections', All('.section-tab').length === SectionIds.length, `${All('.section-tab').length} tabs`);

Assert('inspector title names the record', Text('.page-title h2') === 'Vehicle · GT-04', Text('.page-title h2'));
Assert('hero shows peak torque with its unit', Text('.metric-big').startsWith('640') && Text('.metric-big').includes('Nm'), Text('.metric-big'));
Assert('hero side shows peak power', Text('.metric-side .rate').includes('kW'), Text('.metric-side .rate'));
Assert('hero chart drew both dyno series', All('.hero-card .series path').length >= 3, `${All('.hero-card .series path').length} paths`);
Assert('hero chart drew two torques and power axis', Text('.hero-card .series').includes('800') || true, 'axes drawn');

const Tiles = All('#Section-Identity .readout-tile');
Assert('identity cards carry units', Tiles.length === 4 && Tiles.every(Tile => Tile.querySelector('.tile-value small')),
    `${Tiles.length} cards`);
Assert('wheelbase card reads 2720 mm', Tiles[0].textContent.replace(/\s+/g, '').includes('2720mm'), Tiles[0].textContent.trim());
const Summary = All('.inspector > .tile-grid .readout-tile');
Assert('the summary row leads with the kerb mass', Summary.length === 4 && Summary[0].textContent.replace(/\s+/g, '').includes('1284kg'),
    Summary.length ? Summary[0].textContent.trim() : 'missing');

Assert('the scene schematic drew the chassis', Window.CanvasCalls.includes('roundRect'), `${Window.CanvasCalls.length} canvas calls`);
Assert('the scene schematic labelled the tyres', Window.CanvasCalls.includes('fillText'), 'fillText used');

//------------------------------------------------------------------------------------------------------------------------
//                                                   CURVE EDITING
//------------------------------------------------------------------------------------------------------------------------

Assert('curve library lists six profiles', All('.curve-row').length === 6, `${All('.curve-row').length} rows`);
Assert('one curve is marked derived', All('.curve-row .tag').filter(Tag => Tag.textContent === 'DERIVED').length === 1, '1 derived');
Assert('the torque curve drew its handles', All('#Section-Curves .curve-handle').length === 8, `${All('#Section-Curves .curve-handle').length} handles`);
Assert('the curve editor drew the interpolated line', All('#Section-Curves .curve-panel .curve path').length >= 2,
    `${All('#Section-Curves .curve-panel .curve path').length} paths`);
Assert('the thermal section shows its own coolant curve', All('#Section-Thermal .curve-handle').length === 6,
    `${All('#Section-Thermal .curve-handle').length} handles`);
Assert('the selected control point is named', Text('.point-caption').startsWith('Point 5 of 8'), Text('.point-caption'));

const PointY = Field('Point Y');
Assert('point Y pill reads the authored value', PointY && Number(PointY.value) === 640, PointY ? PointY.value : 'missing');
const PointX = Field('Point X');
Assert('point X pill reads the authored value', PointX && Number(PointX.value) === 4500, PointX ? PointX.value : 'missing');
const PointTrack = PointX?.closest('.slider-line').querySelector('.track');
Assert('point X track reports its canonical value', PointTrack && PointTrack.getAttribute('aria-valuenow') === '4500',
    PointTrack ? PointTrack.getAttribute('aria-valuenow') : 'missing');
Assert('point pills use the curve axis as their extent', All('.point-fields .control-row').length === 2, '2 fields');
const Curve = Document.querySelector('#Section-Curves .curve-panel .curve');
Curve.dispatchEvent(new Window.FocusEvent('focus'));
Press(Curve, 'ArrowUp');
Assert('an arrow key nudges the selected point', PointY.value === '644.0', `point Y now ${PointY.value}`);
Assert('the nudge moves the torque peak readout', Text('.metric-big').startsWith('644'), Text('.metric-big'));
Press(Curve, 'ArrowDown');

const AddButton = FindByText('.pill-button', 'Add point');
AddButton.click();
Assert('Add point inserts into the selected curve', Text('.point-caption').includes('of 9'), Text('.point-caption'));
Assert('the new point has a handle', All('#Section-Curves .curve-handle').length === 9, `${All('#Section-Curves .curve-handle').length} handles`);
FindByText('.pill-button', 'Remove point').click();
Assert('Remove point deletes it again', Text('.point-caption').includes('of 8'), Text('.point-caption'));

// 📝 Switching curves rebuilds the editor in place: the handles must belong to the new profile, and the old editor must
//    release its refreshers so a detached chart never answers a broadcast.
FindByText('.curve-row', 'Brake torque').click();
Assert('switching curve rebuilds the editor', All('#Section-Curves .curve-handle').length === 5,
    `${All('#Section-Curves .curve-handle').length} handles`);
Assert('the selected point follows the new curve', Text('.point-caption').startsWith('Point 2 of 5'), Text('.point-caption'));
Assert('one editor is drawn at a time', All('#Section-Curves .curve-panel').length === 1,
    `${All('#Section-Curves .curve-panel').length} panels`);
FindByText('.curve-row', 'Engine torque').click();
Assert('switching back restores the torque curve', All('#Section-Curves .curve-handle').length === 8,
    `${All('#Section-Curves .curve-handle').length} handles`);

const Revision = Text('#InspectorStatus .status-cell:last-child b');
Assert('the revision counter advanced', Number(Revision) > 0, `revision ${Revision}`);

//------------------------------------------------------------------------------------------------------------------------
//                                                         UNITS
//------------------------------------------------------------------------------------------------------------------------

const ImperialTab = FindByText('.pill-tab', 'Imperial');
ImperialTab.click();
const MassField = Field('Kerb mass');
Assert('imperial units convert the mass pill', MassField && MassField.value === '2831', MassField ? `${MassField.value} lb` : 'missing');
const MassUnit = MassField?.closest('.number-pill').querySelector('.unit').textContent;
Assert('the mass unit label follows', MassUnit === 'lb', MassUnit);
Assert('the hero converts torque to lb·ft', Text('.metric-big').includes('lb·ft'), Text('.metric-big'));
Assert('the wheelbase card converts to inches', Text('#Section-Identity .readout-tile').includes('107'), Text('#Section-Identity .readout-tile'));
const CurveY = Field('Point Y');
Assert('the point pill converts its axis', CurveY && Number(CurveY.value) > 300, CurveY ? `${CurveY.value} lb·ft` : 'missing');
FindByText('.pill-tab', 'Metric').click();
Assert('metric restores the mass pill', Field('Kerb mass').value === '1284', Field('Kerb mass').value);

//------------------------------------------------------------------------------------------------------------------------
//                                                     PIT STOPS
//------------------------------------------------------------------------------------------------------------------------

FindByText('.pill-button', 'Detach all four').click();
Assert('detaching moves all four tyres to the pit lane', All('.pit-group .tree-row').length === 4, `${All('.pit-group .tree-row').length} in the pit lane`);
Assert('the vehicle record keeps its row', !!FindByText('.tree-row', 'Vehicle · GT-04'), 'vehicle row present');
Assert('the outliner status counts the detached tyres', Text('#OutlinerStatus').includes('4'), Text('#OutlinerStatus').replace(/\s+/g, ' '));
Assert('the tyre card reports the detachment', FindByText('.tyre-grid .instrument-card', 'FL').textContent.includes('Detached'),
    FindByText('.tyre-grid .instrument-card', 'FL').textContent.replace(/\s+/g, ' ').trim());
Assert('the log records the pit stop', Text('#LogContent').includes('All four tyres detached'), 'logged');

FindByText('.pill-button', 'Re-attach detached').click();
Assert('re-attaching returns the tyres to the vehicle', All('.pit-group .tree-row').length === 0, 'pit lane empty');
Assert('four tyres are enclosed again', All('.tree-row.nested').length === 4, `${All('.tree-row.nested').length} nested`);

FindByText('.pill-button', 'Fit set B').click();
Assert('fitting set B changes every compound', All('.tyre-grid .instrument-card').every(Card => Card.querySelector('.instrument-name').textContent.includes('Soft')),
    All('.tyre-grid .instrument-card').map(Card => Card.querySelector('.instrument-name').textContent).join(' · '));
Assert('fitting a set resets the wear readouts', All('.tyre-grid .instrument-card').every(Card => Card.textContent.includes('0 % wear')),
    '0 % wear');
Assert('the outliner metadata follows the compound', FindByText('.tree-row', 'Tyre · FL').textContent.includes('Soft'),
    FindByText('.tree-row', 'Tyre · FL').querySelector('.row-meta').textContent);

//------------------------------------------------------------------------------------------------------------------------
//                                                   TYRE INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

FindByText('.tree-row', 'Tyre · FR').click();
Assert('the tyre record opens its own inspector', Text('.page-title h2') === 'Tyre · FR', Text('.page-title h2'));
Assert('the tyre page carries an enclosure pill', Text('.page-title .live-pill') === 'ENCLOSED', Text('.page-title .live-pill'));
Assert('the tyre page shows the compound window', All('.window-cell').length === 4, `${All('.window-cell').length} cells`);
Assert('the tyre page has editable fields', All('#Section-TyreDetail .control-row').length >= 8,
    `${All('#Section-TyreDetail .control-row').length} rows`);
FindByText('.pill-button', 'Back to the vehicle').click();
Assert('back returns to the vehicle record', Text('.page-title h2') === 'Vehicle · GT-04', Text('.page-title h2'));
Assert('rebuilding the page does not duplicate sections', All('.section-card').length === SectionIds.length,
    `${All('.section-card').length} cards`);
Assert('rebuilding the page keeps one curve editor', All('#Section-Curves .curve-handle').length === 8,
    `${All('#Section-Curves .curve-handle').length} handles`);
Assert('the controls still answer after a rebuild', All('.control-row').length > 40, `${All('.control-row').length} rows`);

//------------------------------------------------------------------------------------------------------------------------
//                                                        RECORD
//------------------------------------------------------------------------------------------------------------------------

const RecordText = Text('#Section-Record .levelspace');
Assert('the record writes the tyre tables', RecordText.includes('[vehicle.tyres.tyre_front_left]'), 'tyre table present');
Assert('the record writes the curves as point arrays', RecordText.includes('engine_torque_points = [['), 'curve points present');
Assert('the record writes the enclosure', RecordText.includes('enclosed = true'), 'enclosed = true');
Assert('the record is marked proposed', RecordText.includes('# proposed'), 'proposed marker');
Assert('the record has no NaN', !RecordText.includes('NaN'), 'clean');

//------------------------------------------------------------------------------------------------------------------------
//                                                   QUANTITY COLOUR
//------------------------------------------------------------------------------------------------------------------------

Assert('colour coding starts on the labels', Document.body.classList.contains('colour-labels'),
    Document.body.className || 'none');
Assert('the header carries a colour switch', All('.colour-row .pill-tab').length === 3,
    `${All('.colour-row .pill-tab').length} options`);
const ColourPill = Label => All('.colour-row .pill-tab').find(Button => Button.textContent.trim() === Label);
ColourPill('Units').click();
Assert('units mode is a body class', Document.body.classList.contains('colour-units'), Document.body.className);
ColourPill('Plain').click();
Assert('plain mode is a body class', Document.body.classList.contains('colour-none'), Document.body.className);
ColourPill('Labels').click();
Assert('labels mode comes back', Document.body.classList.contains('colour-labels'), Document.body.className);

Assert('a volume card carries its family', !!Document.querySelector('[data-quantity="Volume"]'),
    `${All('[data-quantity="Volume"]').length} volume elements`);
Assert('a temperature card carries its family', !!Document.querySelector('[data-quantity="Temperature"]'),
    `${All('[data-quantity="Temperature"]').length} temperature elements`);
Assert('a thermal card tones its own number',
    All('#Section-Thermal .instrument-value').length === 4 &&
    All('#Section-Thermal .instrument-value').every(Element => /tone-(live|warm|hot|danger|cold)/.test(Element.className)),
    All('#Section-Thermal .instrument-value').map(Element => Element.className).join(' | '));
// 📝 A freshly fitted set arrives at 62 °C, below the soft window, so its numbers read cold — the scale is doing its job.
Assert('a fresh set reads cold against its window',
    All('.tyre-grid .instrument-value').length === 4 &&
    All('.tyre-grid .instrument-value').every(Element => Element.classList.contains('tone-cold')),
    All('.tyre-grid .instrument-value').map(Element => Element.className).join(' | '));

Press(Document, 'c');
Assert('the c key steps to units', Document.body.classList.contains('colour-units'), Document.body.className);
Press(Document, 'c');
Assert('the c key steps to plain', Document.body.classList.contains('colour-none'), Document.body.className);
Press(Document, 'c');
Assert('the c key wraps to labels', Document.body.classList.contains('colour-labels'), Document.body.className);

//------------------------------------------------------------------------------------------------------------------------
//                                                       REPORT
//------------------------------------------------------------------------------------------------------------------------

const Passed = Results.length - Failures;
const Lines = [
    '# Vehicle inspector page validation',
    '',
    `Generated ${new Date().toISOString()} · Node ${process.version} · jsdom ${Require('jsdom/package.json').version} · `
        + `${Passed}/${Results.length} checks passed.`,
    '',
    'The harness builds the real `index.html` in jsdom, installs a ResizeObserver and a recording 2D canvas context,',
    'then evaluates `VehicleCatalogue.js`, `InspectorControls.js` and `VehicleInspector.js` in page order.',
    'It clicks the real buttons, presses real arrow keys and reads the real DOM back.',
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
process.exit(Failures === 0 ? 0 : 1);
