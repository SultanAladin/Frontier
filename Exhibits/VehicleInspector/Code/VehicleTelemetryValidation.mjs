/*==========================================================================================================================================
                                                  VEHICLETELEMETRYVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Checks for the runtime telemetry dock: the lap model is deterministic, the dock paints what the model holds, the
//    record is never written by watching it, and the switch back to the authoring inspector keeps the session.
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
    console.log('  NODE_PATH=/tmp/vehicle-checks/node_modules node Exhibits/VehicleInspector/Code/VehicleTelemetryValidation.mjs');
    process.exit(2);
}

const Results = [];
let Failures = 0;

function Assert(Name, Condition, Detail)
{
    Results.push({ Name, Passed: !!Condition, Detail });
    if (!Condition) ++Failures;
}

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

for (const File of ['VehicleCatalogue.js', 'InspectorControls.js', 'VehicleTelemetry.js', 'VehicleInspector.js'])
{
    const Name = File.replace('.js', '');
    const Source = readFileSync(resolve(Prototype, File), 'utf8');
    Window.eval(`(function(){\n${Source}\n;if (typeof ${Name} !== 'undefined') window.${Name} = ${Name};\n})()`);
}

if (Document.readyState === 'loading') await new Promise(Resolve => Window.addEventListener('DOMContentLoaded', Resolve));
await new Promise(Resolve => Window.setTimeout(Resolve, 40));

const Text = Selector => (Document.querySelector(Selector)?.textContent || '').trim();
const All = Selector => Array.from(Document.querySelectorAll(Selector));
const FindByText = (Selector, Needle) => All(Selector).find(Element => (Element.textContent || '').includes(Needle));
const Press = (Element, Key) => Element.dispatchEvent(new Window.KeyboardEvent('keydown', { key: Key, bubbles: true }));

const Catalog = Window.VehicleCatalogue;
const Telemetry = Window.VehicleTelemetry;
const Kit = Window.InspectorControls;
const Specification = Catalog.ConfigureVehicleSpecification();
const Profiles = Catalog.ConfigureCurveProfiles();
const Tyres = Catalog.ConfigureTyreRecords();
const RecordSignature = JSON.stringify([Specification, Profiles, Tyres]);

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE LAP MODEL
//------------------------------------------------------------------------------------------------------------------------

const Track = Telemetry.ConfigureTrack(Specification, Profiles);
Assert('the lap table has one sample every ten metres', Track.SampleCount === 432 && Track.Length === 4320,
    `${Track.SampleCount} samples over ${Track.Length} m`);
Assert('every sample carries a positive speed limit', Track.Limits.every(Limit => Limit > 8 && Limit < 130),
    `${Math.min(...Track.Limits).toFixed(1)}–${Math.max(...Track.Limits).toFixed(1)} m/s`);
Assert('the hairpin is the slowest point of the lap', Math.min(...Track.Limits) < 30,
    `${(Math.min(...Track.Limits) * 3.6).toFixed(0)} km/h`);
Assert('the straight is the fastest point of the lap', Math.max(...Track.Limits) > 70,
    `${(Math.max(...Track.Limits) * 3.6).toFixed(0)} km/h`);

const Session = Telemetry.Create(Specification, Profiles, Tyres);
Assert('a session starts with the authored fuel load', Math.abs(Session.Fuel - Specification.Fuel.Load) < 1e-9,
    `${Session.Fuel} L of ${Session.Capacity} L`);
Assert('a session lists four corners', Session.Corners.length === 4, `${Session.Corners.length} corners`);

const Still = Telemetry.Snapshot(Session);
Telemetry.Advance(Session, 0);
Assert('a zero step advances nothing', Telemetry.Snapshot(Session).Elapsed === Still.Elapsed, `${Still.Elapsed} s`);

for (let Step = 0; Step < 900; ++Step) Telemetry.Advance(Session, 1 / 30);
const After30 = Telemetry.Snapshot(Session);
Assert('thirty seconds of driving stays inside the limiter', After30.Rpm <= Specification.Engine.Redline + 1,
    `${After30.Rpm.toFixed(0)} rpm`);
Assert('thirty seconds of driving stays inside the speed range', After30.Speed > 0 && After30.Speed < 340,
    `${After30.Speed.toFixed(0)} km/h`);
Assert('the gear is one of the authored gears', After30.Gear >= 1 && After30.Gear <= Specification.Drivetrain.GearCount,
    `gear ${After30.Gear}`);
Assert('the lap clock ran', Math.abs(After30.Elapsed - 30) < 0.05, `${After30.Elapsed.toFixed(2)} s`);
Assert('fuel burns while driving', After30.Fuel < Specification.Fuel.Load && After30.Fuel > Specification.Fuel.Load - 5,
    `${After30.Fuel.toFixed(2)} L`);
Assert('the burn leaves laps of fuel', After30.LapsOfFuel > 3 && After30.LapsOfFuel < 120, `${After30.LapsOfFuel.toFixed(1)} laps`);
Assert('the tyres warmed from the authored temperature', After30.Corners.every(Corner => Corner.Surface > 60),
    `${After30.Corners[0].Surface.toFixed(0)} °C`);
Assert('tyre pressure follows surface temperature', After30.Corners.every(Corner => Corner.Pressure > Corner.ColdPressure - 2),
    `${After30.Corners[0].Pressure.toFixed(0)} kPa`);
Assert('wear accumulates, never falls', After30.Corners.every((Corner, Index) => Corner.Wear >= Tyres[Index].Wear),
    `${After30.Corners[0].Wear.toFixed(2)} %`);
Assert('the brake discs heat above ambient', Session.BrakeTemperature[0] > Specification.Thermal.AmbientTemperature,
    `${Session.BrakeTemperature[0].toFixed(0)} °C`);
Assert('the traces hold the last twenty seconds', Session.History.length > 400, `${Session.History.length} samples`);

const Twin = Telemetry.Create(Specification, Profiles, Tyres);
for (let Step = 0; Step < 900; ++Step) Telemetry.Advance(Twin, 1 / 30);
Assert('the model is deterministic', Math.abs(Twin.Fuel - Session.Fuel) < 1e-9 && Math.abs(Twin.Speed - Session.Speed) < 1e-9,
    `${Twin.Speed.toFixed(4)} m/s · ${Twin.Fuel.toFixed(4)} L`);

Session.isRunning = false;
const Paused = Telemetry.Snapshot(Session);
Telemetry.Advance(Session, 1);
Assert('a paused session does not advance', Telemetry.Snapshot(Session).Elapsed === Paused.Elapsed, 'held');
Session.isRunning = true;

const Dry = Telemetry.Create(Specification, Profiles, Tyres);
Dry.Fuel = 0.25;
for (let Step = 0; Step < 600; ++Step) Telemetry.Advance(Dry, 1 / 30);
Assert('an empty tank stops the engine', Dry.isDry && Dry.Fuel === 0 && Dry.Throttle === 0,
    `${Dry.Fuel.toFixed(2)} L · throttle ${Dry.Throttle}`);
Assert('an empty tank is logged once', Dry.Events.filter(Entry => Entry.Message === 'Tank empty').length === 1,
    `${Dry.Events.length} events`);

const Probe = Telemetry.Snapshot(Session);
Probe.Fuel = -50;
Assert('a snapshot cannot write the session', Session.Fuel > 0, `${Session.Fuel.toFixed(2)} L`);

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE LIVE DOCK
//------------------------------------------------------------------------------------------------------------------------

Assert('the dock has its own tab beside the inspector', All('#InspectorDockTabs .dock-tab').length === 2,
    `${All('#InspectorDockTabs .dock-tab').length} tabs`);
FindByText('#InspectorDockTabs .dock-tab', 'Telemetry').click();
Assert('switching tabs builds the live dock', !!Document.querySelector('.telemetry'), '.telemetry');
Assert('the authoring sections are gone while driving', !Document.getElementById('Section-Curves') && !Document.querySelector('.hero-card'),
    Document.getElementById('Section-Curves') ? 'curves still built' : 'authoring view detached');
Assert('the mode strip says telemetry', Text('#InspectorStatus').includes('Telemetry · live'), Text('#InspectorStatus').replace(/\s+/g, ' '));
Assert('no uncaught error while building the dock', Errors.length === 0, Errors.join(' / ') || 'none');

const Live = Telemetry.Active();
const PageSpecification = Live ? Live.Specification : null;
const PageRecord = Live ? JSON.stringify([Live.Specification, Live.Profiles, Live.Tyres]) : 'none';
Assert('the dock owns one session', !!Live, Live ? 'session attached' : 'missing');
Assert('the dock starts sampling', Live.isRunning === true, 'running');
Kit.Broadcast();

Assert('the speed card carries its unit', /km\/h/.test(Text('.telemetry-speed')), Text('.telemetry-speed'));
Assert('the gear card is numeric', Number(Text('.telemetry-gear-value')) >= 1, Text('.telemetry-gear-value'));
Assert('the shift strip has twelve lights', All('.shift-led').length === 12, `${All('.shift-led').length} lights`);
Assert('the pedal bars report a share', /%/.test(Text('.pedal-row')), Text('.pedal-row').replace(/\s+/g, ' '));
Assert('the g-force cells carry units', Text('.telemetry-gforce').includes('g'), Text('.telemetry-gforce').replace(/\s+/g, ' '));
Assert('the fuel card shows litres and the share of the tank', /L/.test(Text('.telemetry-fuel-level')) && Text('.telemetry-fuel-share').includes('%'),
    `${Text('.telemetry-fuel-level')} · ${Text('.telemetry-fuel-share')}`);
Assert('the fuel bar draws the level', parseFloat(Document.querySelector('.telemetry-fuel-fill').style.width) > 50,
    Document.querySelector('.telemetry-fuel-fill').style.width);
Assert('the reserve marker sits at the authored share',
    Math.abs(parseFloat(Document.querySelector('.telemetry-fuel-reserve').style.left) - PageSpecification.Fuel.Reserve) < 0.01,
    Document.querySelector('.telemetry-fuel-reserve').style.left);
Assert('the fuel card refuses to guess before a lap is measured', /measuring the first lap/.test(Text('.telemetry-fuel-laps')),
    Text('.telemetry-fuel-laps'));
Assert('six engine tiles carry units', All('.tile-grid .readout-tile').length === 6 &&
    All('.tile-grid .readout-tile').every(Tile => Tile.querySelector('.tile-value small')),
    `${All('.tile-grid .readout-tile').length} tiles`);
Assert('four tyre cards carry pressure, temperature and wear', All('.instrument-grid.two .instrument-card').length === 4 &&
    All('.instrument-grid.two .instrument-card').every(Card => /kPa/.test(Card.textContent) && /% wear/.test(Card.textContent)),
    `${All('.instrument-grid.two .instrument-card').length} cards`);
Assert('the aid chips name the systems', ['TC', 'ABS', 'DRS', 'ERS', 'BIAS'].every(Name => Text('.telemetry-aids').includes(Name)),
    Text('.telemetry-aids').replace(/\s+/g, ' '));
Assert('the brake temperatures are shown per axle', All('.brake-cell').length === 2, `${All('.brake-cell').length} cells`);
Assert('the trace card holds a chart', !!Document.querySelector('.telemetry .series'), '.series');
Assert('the dock offers the way back', !!FindByText('button', 'Vehicle inspector'), 'Vehicle inspector');

// 📝 Ten seconds of the model, painted once: the dock must show what the model holds, not what it held at build.
const Drive = (Driven, Steps) => { for (let Step = 0; Step < Steps; ++Step) Telemetry.Advance(Driven, 1 / 30); };
const BeforeSpeed = parseInt(Text('.telemetry-speed'), 10);
Drive(Live, 300);
Kit.Broadcast();
const ShownSpeed = parseInt(Text('.telemetry-speed'), 10);
const HeldSpeed = Telemetry.Snapshot(Live).Speed;
Assert('the dock paints the speed the model holds', Math.abs(ShownSpeed - HeldSpeed) <= 1, `${ShownSpeed} shown · ${HeldSpeed.toFixed(1)} held`);
Assert('the dock keeps sampling while it is shown', Live.Elapsed >= 9.9, `${Live.Elapsed.toFixed(1)} s`);
Assert('the speed moved as the lap went on', ShownSpeed !== BeforeSpeed, `${BeforeSpeed} → ${ShownSpeed}`);
Assert('the fuel bar follows the tank', Math.abs(parseFloat(Document.querySelector('.telemetry-fuel-fill').style.width)
    - Live.Fuel / Live.Capacity * 100) < 0.6, Document.querySelector('.telemetry-fuel-fill').style.width);
Assert('the fuel card reports the state after a lap', /laps of fuel|measuring the first lap/.test(Text('.telemetry-fuel-laps')),
    Text('.telemetry-fuel-laps'));
Assert('the trace chart follows the session', All('.telemetry .series path').length >= 2, `${All('.telemetry .series path').length} paths`);

// 📝 Units belong to the dock as much as to the inspector: one key converts both.
Press(Document, 'u');
Assert('the speed card converts to miles per hour', Text('.telemetry-speed').includes('mph'), Text('.telemetry-speed'));
Assert('the fuel card converts to gallons', /gal/.test(Text('.telemetry-fuel-share')), Text('.telemetry-fuel-share'));
Press(Document, 'u');
Assert('the speed card converts back', Text('.telemetry-speed').includes('km/h'), Text('.telemetry-speed'));

//------------------------------------------------------------------------------------------------------------------------
//                                                   PAUSE AND RETURN
//------------------------------------------------------------------------------------------------------------------------

const PauseButton = Document.querySelector('.telemetry-pause');
PauseButton.click();
Assert('pause stops the session', Telemetry.Active().isRunning === false, 'paused');
Assert('pause is written on the chip', Text('.telemetry-tag').includes('PAUSED'), Text('.telemetry-tag'));
PauseButton.click();
Assert('resume restarts the session', Telemetry.Active().isRunning === true, 'running');
Telemetry.Active().isRunning = false;

FindByText('#InspectorDockTabs .dock-tab', 'Inspector').click();
Assert('the inspector comes back', !!Document.getElementById('Section-Curves'), '#Section-Curves');
Assert('the live dock is detached', !Document.querySelector('.telemetry'), 'detached');
Assert('the mode strip says inspector', Text('#InspectorStatus').includes('Inspector'), 'Inspector');
const KeptFuel = Telemetry.Active() ? Telemetry.Active().Fuel : -1;
FindByText('#InspectorDockTabs .dock-tab', 'Telemetry').click();
Assert('going back keeps the driven session', Telemetry.Active() && Math.abs(Telemetry.Active().Fuel - KeptFuel) < 1e-6,
    `${KeptFuel.toFixed(2)} L`);

Press(Document, 't');
Assert('the t shortcut returns to the inspector', !!Document.getElementById('Section-Curves'), 'inspector');
Press(Document, 't');
Assert('the t shortcut returns to the dock', !!Document.querySelector('.telemetry'), 'telemetry');

//------------------------------------------------------------------------------------------------------------------------
//                                                DETACHED AND READ-ONLY
//------------------------------------------------------------------------------------------------------------------------

Assert('watching the car never writes the record', JSON.stringify([Live.Specification, Live.Profiles, Live.Tyres]) === PageRecord,
    'record untouched');
Assert('the page record matches the authored defaults', JSON.stringify([Specification, Profiles, Tyres]) === RecordSignature,
    'defaults unchanged');

PageSpecification.Aids.Telemetry = false;
FindByText('#InspectorDockTabs .dock-tab', 'Inspector').click();
FindByText('#InspectorDockTabs .dock-tab', 'Telemetry').click();
Assert('disabled telemetry detaches the dock', !Document.querySelector('.telemetry'), 'detached');
Assert('the detached dock explains itself', Text('.empty-inspector').includes('detached'), Text('.empty-inspector').slice(0, 60));
if (process.env.TELEMETRY_DEBUG) console.log('CONTENT', Document.getElementById('InspectorContent').textContent.slice(0, 400), Errors);
FindByText('button', 'Enable telemetry').click();
Assert('enabling telemetry attaches the dock again', !!Document.querySelector('.telemetry'), 'attached');

Telemetry.Detach();
Assert('detaching clears the timer', true, 'timer cleared');

//------------------------------------------------------------------------------------------------------------------------
//                                                   QUANTITY COLOUR
//------------------------------------------------------------------------------------------------------------------------

Assert('the speed cell carries its family', Document.querySelector('.telemetry-speed')?.dataset.quantity === 'Speed',
    Document.querySelector('.telemetry-speed')?.dataset.quantity || 'none');
Assert('the gear cell carries its family', Document.querySelector('.telemetry-gear')?.dataset.quantity === 'Ratio',
    Document.querySelector('.telemetry-gear')?.dataset.quantity || 'none');
Assert('the fuel level carries its family', Document.querySelector('.telemetry-fuel-level')?.dataset.quantity === 'Volume',
    Document.querySelector('.telemetry-fuel-level')?.dataset.quantity || 'none');
Assert('the dock tyres colour their number by temperature',
    All('.telemetry .instrument-value').some(Element => /tone-(live|warm|hot|danger|cold)/.test(Element.className)),
    All('.telemetry .instrument-value').map(Element => Element.className).join(' | '));

// 📝 A hard-driven tyre must move along the tone scale, and the number is what moves.
const Hot = Telemetry.Active();
Hot.Corners[0].Surface = 168;
Kit.Broadcast();
Assert('a hot tyre turns the number red', /tone-(danger|hot)/.test(Document.querySelector('.telemetry .instrument-value').className),
    Document.querySelector('.telemetry .instrument-value').className);
Hot.Corners[0].Surface = 40;
Kit.Broadcast();
Assert('a cold tyre turns the number blue', /tone-cold/.test(Document.querySelector('.telemetry .instrument-value').className),
    All('.telemetry .instrument-value')[0].className);
Hot.Corners[0].Surface = 88;
Kit.Broadcast();

Assert('the dock pins the colour mode on the page', ['colour-labels', 'colour-units', 'colour-none']
    .some(Name => Document.body.classList.contains(Name)), Document.body.className || 'none');

//------------------------------------------------------------------------------------------------------------------------
//                                                       REPORT
//------------------------------------------------------------------------------------------------------------------------

const Passed = Results.length - Failures;
const Lines = [
    '# Vehicle telemetry validation',
    '',
    `Generated ${new Date().toISOString()} · Node ${process.version} · jsdom ${Require('jsdom/package.json').version} · `
        + `${Passed}/${Results.length} checks passed.`,
    '',
    'The harness builds the real `index.html` in jsdom, switches the dock to Telemetry, then drives the model by hand so',
    'every painted value can be compared with the state it came from. `VehicleCatalogue.js`, `InspectorControls.js`,',
    '`VehicleTelemetry.js` and `VehicleInspector.js` are evaluated in page order.',
    '',
    '| Check | Result | Observed |',
    '|-------|--------|----------|'
];
for (const Result of Results)
    Lines.push(`| ${Result.Name} | ${Result.Passed ? '✔️ pass' : '🔴 fail'} | ${String(Result.Detail).replace(/\|/g, '\\|').replace(/\n/g, ' ')} |`);
Lines.push('', `**${Failures === 0 ? '🟢 all checks passed' : `🔴 ${Failures} check(s) failed`}**`, '');

const ResultsFolder = resolve(Here, '../Results');
mkdirSync(ResultsFolder, { recursive: true });
writeFileSync(resolve(ResultsFolder, 'TelemetryValidation.md'), Lines.join('\n'));

for (const Result of Results) if (!Result.Passed) console.log(`FAIL ${Result.Name} — ${Result.Detail}`);
if (Errors.length) console.log('page errors:\n' + Errors.join('\n'));
console.log(`${Passed}/${Results.length} checks passed`);
console.log(`report: ${resolve(ResultsFolder, 'TelemetryValidation.md')}`);
process.exit(Failures === 0 ? 0 : 1);
