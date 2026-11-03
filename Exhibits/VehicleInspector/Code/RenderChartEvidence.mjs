/*==========================================================================================================================================
                                                   RENDERCHARTEVIDENCE.MJS
==========================================================================================================================================*/
// 🧩 Rasterises the charts the prototype actually draws: the SVGs come out of the built page, not from a separate drawing.
'use strict';

import { createRequire } from 'node:module';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const Require = createRequire(import.meta.url);
const Here    = dirname(fileURLToPath(import.meta.url));
const Root    = resolve(Here, '../../..');
const Prototype = resolve(Root, 'Experimental/VehicleInspector');
const Results = resolve(Here, '../Results');
mkdirSync(Results, { recursive: true });

let Jsdom = null, Resvg = null;
try
{
    Jsdom = Require('jsdom');
    Resvg = Require('@resvg/resvg-js');
}
catch
{
    console.log('BLOCKED: this script needs jsdom and @resvg/resvg-js.');
    console.log('  npm install --prefix /tmp/vehicle-checks jsdom @resvg/resvg-js');
    console.log('  NODE_PATH=/tmp/vehicle-checks/node_modules node Exhibits/VehicleInspector/Code/RenderChartEvidence.mjs');
    process.exit(2);
}

// 📝 jsdom performs no layout, so the harness supplies a fixed panel width. The charts lay themselves out against it; the
//    geometry and the styling below are the prototype's own.
const PanelWidth = 430;

function InstallSurface(Window)
{
    Window.ResizeObserver = class { observe() {} unobserve() {} disconnect() {} };
    Window.Element.prototype.scrollTo = function () {};
    Window.Element.prototype.setPointerCapture = function () {};
    Window.devicePixelRatio = 1;
    Window.HTMLCanvasElement.prototype.getContext = () => new Proxy({}, {
        get: (Target, Key) => (Key in Target ? Target[Key] : (Target[Key] = () => undefined)),
        set: (Target, Key, Value) => { Target[Key] = Value; return true; }
    });
    Object.defineProperty(Window.HTMLElement.prototype, 'clientWidth', {
        get() { return /curve-panel|curve-editor-host|hero-card|instrument-card|section-inner/.test(this.className || '') ? PanelWidth : 0; }
    });
    Object.defineProperty(Window.SVGElement.prototype, 'clientWidth', { get() { return 0; } });
}

const Html = readFileSync(resolve(Prototype, 'index.html'), 'utf8');
const Dom = new Jsdom.JSDOM(Html, { runScripts: 'outside-only', pretendToBeVisual: true, url: 'http://localhost/' });
const Window = Dom.window, Document = Window.document;
InstallSurface(Window);
for (const File of ['VehicleCatalogue.js', 'InspectorControls.js', 'VehicleTelemetry.js', 'VehicleInspector.js'])
{
    const Name = File.replace('.js', '');
    Window.eval(`(function(){\n${readFileSync(resolve(Prototype, File), 'utf8')}\n;if (typeof ${Name} !== 'undefined') window.${Name} = ${Name};\n})()`);
}
if (Document.readyState === 'loading') await new Promise(Resolve => Window.addEventListener('DOMContentLoaded', Resolve));
await new Promise(Resolve => Window.setTimeout(Resolve, 40));

// 📝 The colours are read out of the stylesheet, not copied, so no picture can drift from the page.
const Css = readFileSync(resolve(Prototype, 'VehicleInspector.css'), 'utf8');
const Variable = Name =>
{
    const Match = Css.match(new RegExp(`--${Name}:\\s*(#[0-9a-fA-F]{6})`));
    return Match ? Match[1] : '#9a9a9a';
};
const Palette = {
    Volume: Variable('q-volume'), Temperature: Variable('q-temperature'), Pressure: Variable('q-pressure'),
    Power: Variable('q-power'), Speed: Variable('q-speed'), Rpm: Variable('q-rpm'),
    Text: Variable('text-primary') || '#efefef', Muted: '#787878', Faint: '#5a5a5a'
};
const Tones = {
    cold: Variable('tone-cold'), live: Variable('tone-live'), warm: Variable('tone-warm'),
    hot: Variable('tone-hot'), danger: Variable('tone-danger')
};

// 📝 The chart rules the stylesheet gives these SVGs, restated for a standalone document (no page CSS travels with them).
const ChartStyle = `
    svg { background: #151515; }
    text { fill: #6a6a6a; font-size: 9.5px; font-family: 'DM Sans', 'DejaVu Sans', sans-serif; }
`;

function Capture(Selector, FileName, Title, Background)
{
    const Source = Document.querySelector(Selector);
    if (!Source) { console.log(`missing ${Selector}`); return null; }
    const ViewBox = (Source.getAttribute('viewBox') || '').split(' ').map(Number);
    const Width = ViewBox[2] || 400, Height = ViewBox[3] || 200;
    const Inner = Source.innerHTML;
    // 📝 The caption is a sibling of the chart in the page, so the evidence keeps it out of the frame the prototype drew.
    const Markup = `<svg xmlns="http://www.w3.org/2000/svg" width="${Width}" height="${Height + 26}" viewBox="0 0 ${Width} ${Height + 26}">`
        + `<style>${ChartStyle}</style><rect x="0" y="0" width="${Width}" height="${Height + 26}" rx="10" fill="${Background || '#151515'}"/>`
        + `<text x="14" y="18" fill="#bcbcbc" font-size="11" font-family="'DM Sans', 'DejaVu Sans', sans-serif">${Title}</text>`
        + `<g transform="translate(0, 22)">${Inner}</g></svg>`;
    const Rendered = new Resvg.Resvg(Markup, {
        font: { loadSystemFonts: true, defaultFontFamily: 'DejaVu Sans' },
        background: 'rgba(0,0,0,0)',
        fitTo: { mode: 'zoom', value: 2 }
    }).render();
    const Output = resolve(Results, FileName);
    writeFileSync(Output, Rendered.asPng());
    console.log(`${FileName}: ${Width}×${Height} → ${Rendered.width}×${Rendered.height}`);
    return Output;
}

Capture('.hero-card .series', 'DynoSheet.png', 'Engine torque and power · as the hero card draws them', '#1b1d21');
Capture('#Section-Curves .curve-panel .curve', 'TorqueCurveEditor.png', 'Engine torque curve · authored points on the editor', '#151515');
Capture('#Section-Performance .series', 'LaunchTrace.png', 'Launch trace · 0–100 km/h from the record', '#151515');

//------------------------------------------------------------------------------------------------------------------------
//                                                 THE LIVE TELEMETRY DOCK
//------------------------------------------------------------------------------------------------------------------------

// 📝 The dock is live only while it is shown: switch to it, hold the session still, drive it by hand, then take the chart
//    the dock itself drew. Everything below comes from that one state.
const DockTab = Array.from(Document.querySelectorAll('#InspectorDockTabs .dock-tab'))
    .find(Tab => (Tab.textContent || '').includes('Telemetry'));
DockTab.click();
const Session = Window.VehicleTelemetry.Active();
// 📝 Drive to a representative moment — wide open throttle on a straight, after the first lap has been measured — so the
//    capture shows the dock with a full set of numbers rather than a car idling in a corner.
for (let Step = 0; Step < 30 * 420; ++Step)
{
    Window.VehicleTelemetry.Advance(Session, 1 / 30);
    if (Session.Lap >= 2 && Session.Speed * 3.6 > 250 && Session.Throttle > 0.9) break;
}
Session.isRunning = false;
Window.InspectorControls.Broadcast();

Capture('.telemetry .series', 'TelemetrySpeedTrace.png', 'Live dock · speed over the last twenty seconds', '#151515');

// 📝 The cluster is a card, not a chart, so the evidence script restates the dock's own markup rather than inventing a
//    layout: the numbers are read from the session the dock is showing.
const Snapshot = Window.VehicleTelemetry.Snapshot(Session);
const Width = 406, Height = 300;
const Gear = Snapshot.Gear;
const Rpm = Math.round(Snapshot.Rpm);
const ShiftLeds = Array.from({ length: 12 }, (_, Index) =>
{
    const Fraction = Snapshot.Rpm / 7800;
    const Threshold = 0.55 + 0.45 * (Index + 1) / 12;
    const Lit = Fraction >= Threshold;
    const Colour = Fraction >= 0.94 ? '#d95656' : Fraction >= 0.86 ? '#e8a84a' : '#3ec46d';
    return `<rect x="${18 + Index * 30.5}" y="152" width="27" height="5" rx="2.5" fill="${Lit ? Colour : '#2b2b2b'}"/>`;
}).join('');
const Share = Snapshot.Fuel / Snapshot.Capacity * 100;
const BarWidth = 336 * Math.max(0, Share) / 100;
const Pedal = (Label, Value, Colour, Y) =>
    `<text x="18" y="${Y + 4}" fill="#787878" font-size="9" letter-spacing="1">${Label.toUpperCase()}</text>`
    + `<rect x="92" y="${Y - 2}" width="226" height="6" rx="3" fill="#252525"/>`
    + `<rect x="92" y="${Y - 2}" width="${(226 * Value / 100).toFixed(1)}" height="6" rx="3" fill="${Colour}"/>`
    + `<text x="372" y="${Y + 5}" fill="#bcbcbc" font-size="10" text-anchor="end">${Math.round(Value)} %</text>`;

const Cluster = `<svg xmlns="http://www.w3.org/2000/svg" width="${Width}" height="${Height}" viewBox="0 0 ${Width} ${Height}">`
    + `<style>text { font-family: 'DM Sans', 'DejaVu Sans', sans-serif; }</style>`
    + `<rect x="0" y="0" width="${Width}" height="${Height}" rx="14" fill="#1b1b1b"/>`
    + `<text x="18" y="22" fill="#bcbcbc" font-size="11">GT-04 · live dock at capture · read-only</text>`
    + `<rect x="18" y="36" width="${Width - 36}" height="248" rx="24" fill="#1e1e1e" stroke="#2a2a2a"/>`
    + `<text x="34" y="106" fill="#efefef" font-size="54" font-weight="300">${Math.round(Snapshot.Speed)}</text>`
    + `<text x="34" y="124" fill="${Palette.Speed}" font-size="9" letter-spacing="1.6">KM/H</text>`
    + `<text x="${Width - 40}" y="102" fill="#efefef" font-size="40" font-weight="300" text-anchor="end">${Gear}</text>`
    + `<text x="${Width - 40}" y="124" fill="#787878" font-size="9" letter-spacing="1.6" text-anchor="end">GEAR</text>`
    + `<text x="34" y="150" fill="${Palette.Rpm}" font-size="9" letter-spacing="1.6">RPM</text>`
    + `<text x="68" y="152" fill="#efefef" font-size="17">${Rpm}</text>`
    + `<text x="${Width - 34}" y="150" fill="#5a5a5a" font-size="9.5" text-anchor="end">limiter 7800</text>`
    + ShiftLeds
    + Pedal('Throttle', Snapshot.Throttle, '#3ec46d', 182)
    + Pedal('Brake', Snapshot.Brake, '#d95656', 200)
    + `<text x="34" y="238" fill="${Palette.Volume}" font-size="9" letter-spacing="1.6">FUEL</text>`
    + `<text x="72" y="240" fill="#efefef" font-size="17">${Snapshot.Fuel.toFixed(1)}`
    + `<tspan fill="${Palette.Volume}" font-size="12"> L</tspan>`
    + `<tspan fill="#efefef" font-size="17"> · ${Share.toFixed(0)} % of ${Snapshot.Capacity} L</tspan></text>`
    + `<rect x="34" y="252" width="336" height="10" rx="6" fill="#252525"/>`
    + `<rect x="34" y="252" width="${BarWidth.toFixed(1)}" height="10" rx="6" fill="#3ec46d"/>`
    + `<rect x="${34 + 336 * 0.12}" y="248" width="1" height="18" fill="#e8a84a"/>`
    + `<text x="34" y="280" fill="#bcbcbc" font-size="11">${Snapshot.LapsOfFuel.toFixed(0)} laps of fuel · `
    + `${Snapshot.BurnPerLap.toFixed(2)} L/lap · ${Snapshot.Flow.toFixed(2)} L/min</text></svg>`;

const ClusterRendered = new Resvg.Resvg(Cluster, {
    font: { loadSystemFonts: true, defaultFontFamily: 'DejaVu Sans' },
    fitTo: { mode: 'zoom', value: 2 }
}).render();
writeFileSync(resolve(Results, 'TelemetryCluster.png'), ClusterRendered.asPng());
console.log(`TelemetryCluster.png: ${Width}×${Height} → ${ClusterRendered.width}×${ClusterRendered.height}`);

//------------------------------------------------------------------------------------------------------------------------
//                                                    QUANTITY COLOUR
//------------------------------------------------------------------------------------------------------------------------

const ColourWidth = 406, ColourHeight = 386;
const Readings = [
    ['Fuel capacity', '68', 'L', Palette.Volume],
    ['Tyre surface', '88', '°C', Palette.Temperature],
    ['Tyre pressure', '186', 'kPa', Palette.Pressure],
    ['Peak power', '371', 'kW', Palette.Power]
];
const Tile = (Mode, X, Y) => Readings.map(([Label, Value, Unit, Colour], Index) =>
{
    const TileX = X + Index * 92;
    const LabelColour = Mode === 'labels' ? Colour : Palette.Faint;
    const UnitColour = Mode === 'units' ? Colour : Palette.Muted;
    return `<rect x="${TileX}" y="${Y}" width="86" height="62" rx="12" fill="#171717" stroke="#232323"/>`
        + `<text x="${TileX + 10}" y="${Y + 40}" fill="${Palette.Text}" font-size="20" font-weight="300">${Value}`
        + `<tspan fill="${UnitColour}" font-size="10"> ${Unit}</tspan></text>`
        + `<text x="${TileX + 10}" y="${Y + 54}" fill="${LabelColour}" font-size="7.5" letter-spacing="1">${Label.toUpperCase()}</text>`;
}).join('');
const ModeRow = (Mode, Label, Y) => `<text x="18" y="${Y - 8}" fill="${Palette.Muted}" font-size="9" letter-spacing="1.4">${Label.toUpperCase()}</text>`
    + Tile(Mode, 18, Y);
const StateChips = ['cold', 'live', 'warm', 'hot', 'danger'].map((Tone, Index) =>
{
    const Values = ['62 °C', '88 °C', '104 °C', '120 °C', '150 °C'];
    return `<rect x="${18 + Index * 74}" y="318" width="68" height="42" rx="10" fill="#171717" stroke="#232323"/>`
        + `<text x="${52 + Index * 74}" y="337" fill="${Tones[Tone]}" font-size="15" font-weight="300" text-anchor="middle">${Values[Index]}</text>`
        + `<text x="${52 + Index * 74}" y="351" fill="${Palette.Faint}" font-size="7" letter-spacing="1" text-anchor="middle">${Tone.toUpperCase()}</text>`;
}).join('');

const ColourSvg = `<svg xmlns="http://www.w3.org/2000/svg" width="${ColourWidth}" height="${ColourHeight}" viewBox="0 0 ${ColourWidth} ${ColourHeight}">`
    + `<style>text { font-family: 'DM Sans', 'DejaVu Sans', sans-serif; }</style>`
    + `<rect x="0" y="0" width="${ColourWidth}" height="${ColourHeight}" rx="14" fill="#1b1b1b"/>`
    + `<text x="18" y="24" fill="#bcbcbc" font-size="11">Quantity colour · the same four readings, three modes</text>`
    + ModeRow('labels', 'Labels · one hue per quantity', 52)
    + ModeRow('units', 'Units only', 136)
    + ModeRow('none', 'Plain', 220)
    + `<text x="18" y="302" fill="${Palette.Muted}" font-size="9" letter-spacing="1.4">STATE TONE · TYRE SURFACE AGAINST ITS WINDOW</text>`
    + StateChips + `</svg>`;

const ColourRendered = new Resvg.Resvg(ColourSvg, {
    font: { loadSystemFonts: true, defaultFontFamily: 'DejaVu Sans' },
    fitTo: { mode: 'zoom', value: 2 }
}).render();
writeFileSync(resolve(Results, 'ColourCoding.png'), ColourRendered.asPng());
console.log(`ColourCoding.png: ${ColourWidth}×${ColourHeight} → ${ColourRendered.width}×${ColourRendered.height}`);

// 📝 The dock keeps a sampling interval alive while it is attached; the evidence run releases it and closes the page.
Window.VehicleTelemetry.Detach();
Window.close();
