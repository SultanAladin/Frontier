import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

// 🧩 HMI demo contract (owner brief): one vehicle in the viewport, ReSTIR render, paint-only controls.
const Html = readFileSync(new URL('../HmiDemo.html', import.meta.url), 'utf8');
const Demo = readFileSync(new URL('../Source/HmiDemo.js', import.meta.url), 'utf8');
const Facets = readFileSync(new URL('../Source/FacetStructure.js', import.meta.url), 'utf8');

test('HMI demo page: viewport + paint panel only', () => {
    assert.match(Html, /id="viewport"/, 'the renderer canvas is the viewport');
    assert.match(Html, /id="paint-panel"/);
    assert.match(Html, /IlluminationHost\.js/, 'same ReSTIR renderer as the Lobby');
    for (const Removed of ['wheel', 'finish-', 'climate', 'drive-mode', 'lighting-panel'])
        assert.ok(!Html.toLowerCase().includes(Removed), `no ${Removed} controls`);
});

test('HMI demo wiring: paint applies to the live vehicle via the shared appearance', () => {
    assert.match(Demo, /PaintOptions/, 'the eight named finishes');
    assert.match(Demo, /Cabin\.Appearance\.Apply\(\{ Colour: Paint\.Hex \}\)/, 'shared material records, driven car included');
    assert.match(Demo, /drag onto the car/, 'drag-onto-car applies like the reference configurator');
});

test('single vehicle: ?hmi=1 never constructs the display fleet', () => {
    assert.match(Facets, /SingleVehicle/, 'gate exists');
    assert.match(Facets, /if \(SingleVehicle\) return; \/\/ hero \+ its plates only/, 'guest bodies, wheels and plates skipped');
});
