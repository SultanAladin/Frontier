/**
 * Hearth launcher interaction checks.
 *
 * Runs the prototype's markup, styles, and script inside jsdom with stubbed
 * browser APIs, then drives the real listeners: shelf filters, featured
 * selection, detail sheet, launch sequence, session controls, command palette,
 * themes, sound state, update likes, and keyboard handling.
 *
 * Run from Experimental/HearthLauncher:
 *   npm ci
 *   npm test
 */

import { JSDOM, VirtualConsole } from 'jsdom';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = (name) => fs.readFileSync(path.join(root, name), 'utf8');

let html = read('index.html');
const css = read('styles.css');
const script = read('launcher.js');
html = html.replace('<link rel="stylesheet" href="styles.css">', () => `<style>${css}</style>`);
html = html.replace('<script src="launcher.js"></script>', () => `<script>${script}</script>`);

const runtimeErrors = [];
const virtualConsole = new VirtualConsole();
virtualConsole.on('jsdomError', (error) => runtimeErrors.push(`jsdomError: ${error.message}`));
virtualConsole.on('error', (...args) => runtimeErrors.push(`console.error: ${args.join(' ')}`));

const dom = new JSDOM(html, {
  runScripts: 'dangerously',
  pretendToBeVisual: true,
  url: 'http://localhost:5001/',
  virtualConsole,
  beforeParse(window) {
    window.matchMedia = (query) => ({
      matches: false,
      media: query,
      addListener() {},
      removeListener() {},
      addEventListener() {},
      removeEventListener() {},
    });
    window.Element.prototype.animate = () => ({
      finished: Promise.resolve(),
      cancel() {},
      pause() {},
      play() {},
    });
    window.HTMLElement.prototype.scrollIntoView = () => {};
    window.AudioContext = class {
      constructor() {
        this.state = 'running';
        this.currentTime = 0;
        this.sampleRate = 48000;
        this.destination = {};
      }
      createGain() {
        return {
          gain: {
            value: 0,
            setValueAtTime() {},
            exponentialRampToValueAtTime() {},
            setTargetAtTime() {},
            cancelScheduledValues() {},
          },
          connect() { return this; },
        };
      }
      createOscillator() {
        return {
          type: 'sine',
          frequency: { value: 0, setValueAtTime() {}, exponentialRampToValueAtTime() {} },
          detune: { value: 0 },
          connect() { return this; },
          start() {},
          stop() {},
        };
      }
      createBiquadFilter() {
        return {
          type: 'lowpass',
          frequency: { value: 0, setValueAtTime() {}, exponentialRampToValueAtTime() {} },
          Q: { value: 0 },
          connect() { return this; },
        };
      }
      createAnalyser() {
        return {
          fftSize: 64,
          frequencyBinCount: 32,
          smoothingTimeConstant: 0,
          getByteFrequencyData(data) { data.fill(20); },
          connect() { return this; },
        };
      }
      createBuffer(channels, length) {
        const data = new Float32Array(length);
        return { getChannelData() { return data; } };
      }
      createBufferSource() {
        return {
          buffer: null,
          loop: false,
          connect() { return this; },
          start() {},
          stop() {},
        };
      }
      resume() {}
    };
  },
});

const { window } = dom;
const { document } = window;
const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];
const click = (element) => element.dispatchEvent(new window.MouseEvent('click', { bubbles: true }));
const key = (k, extra = {}) => document.dispatchEvent(new window.KeyboardEvent('keydown', { key: k, bubbles: true, ...extra }));
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const assert = (condition, message) => { if (!condition) throw new Error(message); };

const results = [];
async function check(name, action) {
  try {
    await action();
    results.push([name, true]);
  } catch (error) {
    results.push([name, false, error.message]);
  }
}

await sleep(1800);

await check('boot overlay settles and reveals the launcher', () => {
  assert($('#boot').classList.contains('is-done'), 'boot overlay did not dismiss');
  assert(document.body.classList.contains('is-ready'), 'entrance state was not applied');
});

await check('featured stage renders three app slides and four facts', () => {
  assert($('#featTitle').textContent.includes('Rayman'), `unexpected title: ${$('#featTitle').textContent}`);
  assert($$('.slide').length === 3, 'expected three featured slides');
  assert($$('.thumb').length === 3, 'expected three thumbnails');
  assert($('.slide.is-active')?.dataset.id === 'rayman', 'wrong active slide');
  assert($('#featChips').children.length === 4, 'expected four metadata chips');
});

await check('shelf renders the full collection with generated covers', () => {
  assert($$('.card').length === 8, `expected eight cards, found ${$$('.card').length}`);
  assert($$('.cover').length >= 4, 'generated covers missing');
  assert($('#shelfEmpty').hidden, 'empty state should be hidden');
});

await check('weekly stats and roadmap render from data', () => {
  assert($$('.seg').length === 5, 'expected five week segments');
  assert($$('.seglist li').length === 5, 'expected five legend rows');
  assert($$('.timeline li').length === 4, 'expected four update rows');
});

await check('thumbnail selection swaps the featured app and accent', () => {
  click($$('.thumb')[2]);
  assert($('#featTitle').textContent.includes('Clash'), 'title did not update');
  assert($('.slide.is-active').dataset.id === 'clash', 'slide did not update');
  assert($('#stage').style.getPropertyValue('--accent') === '#4f7dff', 'accent did not update');
});

await check('filters narrow the shelf and restore it', () => {
  const studio = $$('.filter').find((button) => button.dataset.filter === 'studio');
  click(studio);
  assert($$('.card').length === 2, `expected two studio apps, found ${$$('.card').length}`);
  click($$('.filter').find((button) => button.dataset.filter === 'all'));
  assert($$('.card').length === 8, 'all filter did not restore the shelf');
});

await check('grid toggle switches the shelf layout', () => {
  click($$('#viewToggle button')[1]);
  assert($('#shelf').dataset.view === 'grid', 'view did not switch to grid');
  click($$('#viewToggle button')[0]);
  assert($('#shelf').dataset.view === 'rail', 'view did not switch back to rail');
});

await check('command palette searches and opens a detail sheet', () => {
  click($('#searchTrigger'));
  assert(!$('#palette').hidden, 'palette did not open');
  const input = $('#paletteInput');
  input.value = 'clash';
  input.dispatchEvent(new window.Event('input', { bubbles: true }));
  assert($$('.result').length === 1, `expected one result, found ${$$('.result').length}`);
  input.dispatchEvent(new window.KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
  assert($('#sheetTitle').textContent === 'Clash Royale', 'detail sheet did not open the result');
});

await sleep(80);

await check('detail sheet animates open and closes on escape', () => {
  assert($('#sheet').classList.contains('is-open'), 'sheet did not enter its open state');
  key('Escape');
});

await sleep(500);

await check('detail sheet is hidden again', () => {
  assert($('#sheet').hidden, 'sheet did not close');
});

await check('card activation opens the detail sheet', () => {
  click($$('.card')[0]);
  assert(!$('#sheet').hidden, 'sheet did not open');
  assert($('#sheetTitle').textContent === 'Rayman Adventures', 'wrong sheet title');
});

await check('play starts the launch sequence with the app key art', () => {
  click($('#sheetPlay'));
  assert(!$('#launch').hidden, 'launch overlay did not open');
  assert($('#launchBackdrop').style.backgroundImage.includes('rayman.jpg'), 'launch backdrop missing');
  assert($('#launchMark').style.backgroundImage.includes('rayman.jpg'), 'launch mark missing');
});

await sleep(3000);

await check('launch hands over to a running session', () => {
  assert(!$('#session').hidden, 'session is hidden');
  assert($('#session').classList.contains('is-on'), 'session did not enter its live state');
  assert($('#sessionTitle').textContent === 'Rayman Adventures', 'session title missing');
  assert($('#statusSession').textContent.startsWith('live'), 'status bar does not show a live session');
});

await sleep(1200);

await check('session clock advances', () => {
  assert($('#sessionTimer').textContent !== '00:00', 'timer did not advance');
});

await check('pause freezes the session and relabels it', () => {
  click($('#sessionPause'));
  assert($('#session').classList.contains('is-paused'), 'session did not pause');
  assert($('#statusSession').textContent === 'paused', 'status bar did not update');
  assert($('#sessionLiveText').textContent === 'paused', 'live badge did not update');
  click($('#sessionPause'));
  assert(!$('#session').classList.contains('is-paused'), 'session did not resume');
});

await check('search is blocked while a session owns the screen', () => {
  click($('#searchTrigger'));
  assert($('#palette').hidden, 'palette should be blocked during a session');
  key('k', { metaKey: true });
  assert($('#palette').hidden, 'keyboard shortcut should also be blocked');
});

await check('escape ends the session and closes the overlay', () => {
  key('Escape');
});

await sleep(700);

await check('launcher returns to idle after the session', () => {
  assert($('#statusSession').textContent === 'idle', 'status bar did not return to idle');
  assert($('#launch').hidden, 'launch overlay still visible');
  assert($('#sheet').hidden, 'sheet still visible');
  assert($('#palette').hidden, 'palette still visible');
});

await check('theme toggle switches the lamp palette', () => {
  const before = document.documentElement.dataset.theme;
  click($('#lampBtn'));
  assert(document.documentElement.dataset.theme !== before, 'theme did not change');
  click($('#lampBtn'));
  assert(document.documentElement.dataset.theme === before, 'theme did not restore');
});

await check('sound toggle updates the interface and footer', () => {
  const pressed = () => $('#soundBtn').getAttribute('aria-pressed') === 'true';
  const before = pressed();
  click($('#soundBtn'));
  assert(pressed() !== before, 'sound state did not change');
  assert($('#audioLabel').textContent.length > 0, 'footer label missing');
});

await check('room tone preview arms the visualiser', () => {
  click($('#tonePlay'));
  assert($('.panel-tone').classList.contains('is-live'), 'tone panel did not go live');
  click($('#tonePlay'));
  assert(!$('.panel-tone').classList.contains('is-live'), 'tone panel did not stop');
});

await check('update support increments and persists in-session', () => {
  const button = $$('.t-like')[0];
  const before = Number(button.querySelector('span').textContent);
  click(button);
  assert(Number(button.querySelector('span').textContent) === before + 1, 'support was not counted');
});

await check('every icon reference resolves', () => {
  const symbols = new Set($$('symbol').map((symbol) => symbol.id));
  const missing = $$('use')
    .map((use) => (use.getAttribute('href') || '').slice(1))
    .filter((id) => !symbols.has(id));
  assert(missing.length === 0, `missing symbols: ${missing.join(', ')}`);
});

await check('no runtime errors were reported', () => {
  assert(runtimeErrors.length === 0, runtimeErrors.join(' | '));
});

const passed = results.filter(([, ok]) => ok).length;
for (const [name, ok, detail] of results) {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? ` — ${detail}` : ''}`);
}
console.log(`\n${passed}/${results.length} interaction checks passed`);

window.close();
process.exit(passed === results.length ? 0 : 1);
