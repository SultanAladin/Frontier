/**
 * Landmass editor shell: the toolbar, inspector, HUD and the journal around
 * `PlanetViewport`.
 *
 * The editor owns authoring state: which tool is active, brush parameters, the
 * journal itself (through `PlanetField`), undo, save/load and export. The
 * viewport owns pixels and streaming. This split is what lets the same core run
 * under `node --test` and inside the studio.
 */

import { PlanetField } from '../core/planet-field.js';
import { BRUSH_TOOLS, BRUSH_TOOL_BY_ID, createBrushOp } from '../core/brushes.js';
import { DEFAULT_OCEAN_PARAMS } from '../core/ocean-model.js';
import { PlanetViewport } from './macro-viewport.js';
import { directionFromLatLon, latLonFromDirection } from '../core/cube-sphere.js';
import { normalize } from '../core/vec3.js';

const TOOL_GLYPHS = {
  orbit: '<circle cx="12" cy="12" r="3"/><circle cx="19" cy="5" r="2"/><circle cx="5" cy="19" r="2"/><path d="M20.3 6.5A10 10 0 0 1 10.3 21.9"/><path d="M3.7 17.5A10 10 0 0 1 13.7 2.2"/>',
  up: '<path d="m5 12 7-7 7 7"/><path d="M12 19V5"/>',
  down: '<path d="M12 5v14"/><path d="m19 12-7 7-7-7"/>',
  wave: '<path d="M3 16c3-6 6 4 9-2s6 2 9-4"/>',
  plateau: '<path d="M3 15h5l2-6h4l2 6h5"/><path d="M3 19h18"/>',
  ridge: '<path d="m2 18 6-11 3 5 3-6 8 12z"/>',
  island: '<path d="M3 18c3 0 4-2 7-2s4 2 7 2 4-1 4-1"/><path d="M8 14c1-4 2-6 4-6s3 2 4 6"/>',
  gulf: '<path d="M3 12c4 0 5 5 9 5s5-5 9-5"/><path d="M3 7h18"/>',
};

const PLANET_FIELDS = [
  { key: 'radius', label: 'Radius', unit: 'km', min: 40, max: 6371, step: 1, transform: (v) => v * 1000, format: (v) => (v / 1000).toFixed(0), rebuild: true },
  { key: 'landAmount', label: 'Land fraction', unit: '', min: 0.05, max: 0.9, step: 0.01, format: (v) => v.toFixed(2), rebuild: true },
  { key: 'continentalRelief', label: 'Interior relief', unit: 'm', min: 200, max: 6000, step: 50, format: (v) => v.toFixed(0), rebuild: true },
  { key: 'mountainRelief', label: 'Mountain relief', unit: 'm', min: 0, max: 12000, step: 100, format: (v) => v.toFixed(0), rebuild: true },
  { key: 'oceanFloorDepth', label: 'Abyssal depth', unit: 'm', min: 500, max: 11000, step: 100, format: (v) => v.toFixed(0), rebuild: true },
  { key: 'coastSharpness', label: 'Coast detail', unit: '', min: 0, max: 1, step: 0.01, format: (v) => v.toFixed(2), rebuild: true },
];

const OCEAN_FIELDS = [
  { key: 'seaLevelOffset', label: 'Sea level', unit: 'm', min: -400, max: 400, step: 5, format: (v) => v.toFixed(0) },
  { key: 'waveHeight', label: 'Wave height', unit: 'm', min: 0, max: 8, step: 0.1, format: (v) => v.toFixed(1) },
  { key: 'windStrength', label: 'Wind', unit: '', min: 0, max: 2, step: 0.05, format: (v) => v.toFixed(2) },
  { key: 'foamWidth', label: 'Foam band', unit: 'm', min: 1, max: 120, step: 1, format: (v) => v.toFixed(0) },
  { key: 'absorption', label: 'Water clarity', unit: '1/m', min: 0.05, max: 1.2, step: 0.01, format: (v) => v.toFixed(2) },
  { key: 'sunElevation', label: 'Sun elevation', unit: 'rad', min: -0.2, max: 1.4, step: 0.01, format: (v) => v.toFixed(2) },
];

class LandmassEditor {
  constructor() {
    this.field = new PlanetField({ radius: 900000, seed: 4029 });
    this.ocean = { ...DEFAULT_OCEAN_PARAMS };
    this.activeTool = 'orbit';
    this.brush = { radius: 4200, strength: 320, falloff: 0.5 };

    this.#installDiagnostics();

    this.canvas = document.querySelector('#viewport-canvas');
    this.viewport = new PlanetViewport(this.canvas, {
      field: this.field,
      ocean: this.ocean,
      onStats: (stats) => this.renderStats(stats),
      onCursor: (cursor) => this.renderCursor(cursor),
      onDiagnostic: (message) => this.reportDiagnostic(message),
    });

    this.viewport.onSculpt = (request) => this.applyBrush(request);

    this.#buildToolRail();
    this.#buildSliders();
    this.#wireActions();
    this.#wireKeys();
    this.#seatCameraOnCoast();
    this.setTool('orbit');
  }

  //------------------------------------------------------------- DIAGNOSTICS

  /**
   * A failure inside the viewport used to mean a black canvas and nothing else.
   * Everything that can go wrong at startup is surfaced here instead: exceptions,
   * rejected promises, shader compile errors (which three only reports on the
   * console), worker failures, and the case where the frame ran but no patch ever
   * arrived.
   */
  #installDiagnostics() {
    this.diagnosticLines = [];
    const panel = document.querySelector('#boot-errors');
    const list = document.querySelector('#boot-errors-list');

    this.reportDiagnostic = (message) => {
      const text = String(message ?? 'unknown error').slice(0, 500);
      if (this.diagnosticLines.includes(text)) return;
      this.diagnosticLines.push(text);
      console.error('[sdf-planet]', text);
      if (!panel || !list) return;
      panel.hidden = false;
      const item = document.createElement('li');
      item.textContent = text;
      list.appendChild(item);
    };

    document.querySelector('#boot-errors-close')?.addEventListener('click', () => {
      if (panel) panel.hidden = true;
    });

    window.addEventListener('error', (event) => {
      this.reportDiagnostic(event?.message ?? event?.error?.message ?? 'script error');
    });
    window.addEventListener('unhandledrejection', (event) => {
      const reason = event?.reason;
      this.reportDiagnostic(reason?.message ?? String(reason ?? 'unhandled rejection'));
    });

    // three reports shader compilation failures through console.error only; if a
    // program fails, the object it belongs to simply draws nothing.
    const originalError = console.error.bind(console);
    console.error = (...args) => {
      originalError(...args);
      const text = args
        .map((value) => (typeof value === 'string' ? value : value?.message ?? String(value)))
        .join(' ');
      if (/GLSL|shader|program|WebGL/i.test(text) && !text.startsWith('[sdf-planet]')) {
        this.reportDiagnostic(text.slice(0, 400));
      }
    };

    // The frame loop can be healthy while the terrain never arrives (workers
    // blocked, build throwing). Say so instead of leaving an empty planet.
    setTimeout(() => {
      const resident = this.viewport?.terrain?.summary?.resident ?? 0;
      if (resident === 0) {
        this.reportDiagnostic(
          'No terrain patches after 4 s: tile workers may not have started. Check the console; meshing falls back to the main thread after two worker failures.',
        );
      }
    }, 4000);
  }

  //----------------------------------------------------------------- AUTHORING

  applyBrush({ toolId, dir, radius, strength, falloff }) {
    const tool = BRUSH_TOOL_BY_ID.get(toolId);
    if (!tool?.kind) return null;

    const op = createBrushOp({
      toolId,
      dir,
      radius,
      strength,
      falloff,
      seed: (Math.random() * 0xffffff) | 0,
      field: this.field,
    });
    const stored = this.field.addOp(op);
    this.viewport.noteOp(stored);
    this.#updateJournalCount();
    return stored;
  }

  undo() {
    const removed = this.field.undo();
    if (!removed) return;
    this.viewport.syncJournal();
    this.#updateJournalCount();
  }

  clearEdits() {
    this.field.clearEdits();
    this.viewport.syncJournal();
    this.#updateJournalCount();
  }

  //---------------------------------------------------------------------- UI

  #buildToolRail() {
    const rail = document.querySelector('#tool-rail');
    rail.innerHTML = '';

    for (const tool of BRUSH_TOOLS) {
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'tp-tool';
      button.dataset.tool = tool.id;
      button.title = tool.hint;
      button.innerHTML = `
        <span class="tp-tool-glyph">
          <svg viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor"
               stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
            ${TOOL_GLYPHS[tool.glyph] ?? TOOL_GLYPHS.orbit}
          </svg>
        </span>
        <span class="tp-tool-label">${tool.label}</span>`;
      button.addEventListener('click', () => this.setTool(tool.id));
      rail.appendChild(button);
    }
  }

  #buildSliders() {
    this.#renderSliderGroup('#brush-sliders', [
      { key: 'radius', label: 'Radius', unit: 'm', min: 100, max: 40000, step: 50, format: (v) => v.toFixed(0) },
      { key: 'strength', label: 'Strength', unit: '', min: 5, max: 4000, step: 5, format: (v) => v.toFixed(0) },
      { key: 'falloff', label: 'Falloff', unit: '', min: 0, max: 1, step: 0.01, format: (v) => v.toFixed(2) },
    ], (key, value) => {
      this.brush[key] = value;
      this.viewport.setBrush(this.brush);
    });

    this.#renderSliderGroup('#planet-sliders', PLANET_FIELDS, (key, value) => {
      this.field.setParams({ [key]: value });
      this.viewport.rebuildField({ params: this.field.params });
      this.#seatCameraOnCoast(false);
    });

    this.#renderSliderGroup('#ocean-sliders', OCEAN_FIELDS, (key, value) => {
      this.ocean[key] = value;
      this.viewport.setOceanParams({ [key]: value });
    });
  }

  #renderSliderGroup(selector, fields, onChange) {
    const host = document.querySelector(selector);
    host.innerHTML = '';

    for (const field of fields) {
      const current = this[selector.includes('ocean') ? 'ocean' : selector.includes('planet') ? 'field' : 'brush'];
      const value = selector.includes('planet') ? this.field.params[field.key] : current[field.key];

      const row = document.createElement('label');
      row.className = 'tp-field';
      row.innerHTML = `
        <span class="tp-field-head">
          <span class="tp-field-label">${field.label}</span>
          <span class="tp-field-value" data-value="${field.key}">${field.format(value)}</span>
        </span>
        <input type="range" min="${field.min}" max="${field.max}" step="${field.step}"
               value="${field.transform ? value / field.transform(1) : value}" data-key="${field.key}" />`;
      const input = row.querySelector('input');
      const output = row.querySelector('[data-value]');

      input.addEventListener('input', () => {
        const raw = Number(input.value);
        const resolved = field.transform ? field.transform(raw) : raw;
        output.textContent = field.format(resolved);
        onChange(field.key, resolved);
      });

      host.appendChild(row);
    }
  }

  #wireActions() {
    document.querySelector('#btn-undo').addEventListener('click', () => this.undo());
    document.querySelector('#btn-clear').addEventListener('click', () => this.clearEdits());
    document.querySelector('#btn-save').addEventListener('click', () => this.saveJournal());
    document.querySelector('#btn-export').addEventListener('click', () => this.exportObj());

    const fileInput = document.querySelector('#journal-file');
    document.querySelector('#btn-load').addEventListener('click', () => fileInput.click());
    fileInput.addEventListener('change', async () => {
      const file = fileInput.files?.[0];
      if (!file) return;
      const text = await file.text();
      try {
        const payload = JSON.parse(text);
        this.field.loadEdits(payload.edits ?? payload);
        if (payload.params) this.field.setParams(payload.params);
        this.viewport.rebuildField({ params: this.field.params });
        this.#updateJournalCount();
        this.toast(`Loaded ${this.field.ops.length} ops`);
      } catch (error) {
        this.toast(`Load failed: ${error.message}`, true);
      }
      fileInput.value = '';
    });

    const oceanToggle = document.querySelector('#btn-ocean');
    oceanToggle.addEventListener('click', () => {
      const visible = !this.viewport.showOcean;
      this.viewport.setOceanVisible(visible);
      oceanToggle.classList.toggle('on', visible);
      oceanToggle.textContent = visible ? 'Ocean on' : 'Ocean off';
    });

    const wireToggle = document.querySelector('#btn-wire');
    wireToggle.addEventListener('click', () => {
      const enabled = !this.viewport.showWireframe;
      this.viewport.setWireframe(enabled);
      wireToggle.classList.toggle('on', enabled);
    });

    document.querySelector('#btn-teleport').addEventListener('click', () => {
      const lat = Number(document.querySelector('#teleport-lat').value);
      const lon = Number(document.querySelector('#teleport-lon').value);
      const altitude = Number(document.querySelector('#teleport-alt').value);
      if (!Number.isFinite(lat) || !Number.isFinite(lon)) return;
      this.viewport.teleportTo(directionFromLatLon(lat, lon), Number.isFinite(altitude) ? altitude : 3000);
      this.toast(`Teleported to ${lat.toFixed(1)}, ${lon.toFixed(1)}`);
    });
  }

  #wireKeys() {
    window.addEventListener('keydown', (event) => {
      const tag = event.target?.tagName;
      if (tag === 'INPUT' || tag === 'TEXTAREA') return;

      if (event.key === '[') {
        this.brush.radius = Math.max(100, this.brush.radius / 1.25);
        this.viewport.setBrush(this.brush);
        this.#syncBrushSlider();
      } else if (event.key === ']') {
        this.brush.radius = Math.min(40000, this.brush.radius * 1.25);
        this.viewport.setBrush(this.brush);
        this.#syncBrushSlider();
      } else if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'z') {
        event.preventDefault();
        this.undo();
      } else if (event.key.toLowerCase() === 'g') {
        const visible = !this.viewport.showOcean;
        this.viewport.setOceanVisible(visible);
        document.querySelector('#btn-ocean').classList.toggle('on', visible);
      } else if (event.key.toLowerCase() === 'h') {
        this.viewport.setWireframe(!this.viewport.showWireframe);
        document.querySelector('#btn-wire').classList.toggle('on', this.viewport.showWireframe);
      } else {
        const index = Number(event.key);
        if (Number.isInteger(index) && index >= 1 && index <= BRUSH_TOOLS.length) {
          this.setTool(BRUSH_TOOLS[index - 1].id);
        }
      }
    });
  }

  #syncBrushSlider() {
    const input = document.querySelector('#brush-sliders input[data-key="radius"]');
    const output = document.querySelector('#brush-sliders [data-value="radius"]');
    if (input) input.value = String(Math.round(this.brush.radius));
    if (output) output.textContent = this.brush.radius.toFixed(0);
  }

  setTool(toolId) {
    const tool = BRUSH_TOOL_BY_ID.get(toolId);
    if (!tool) return;
    this.activeTool = toolId;
    this.viewport.setTool(toolId);

    if (tool.kind) {
      // A tool switch adopts that tool's own defaults, which keeps "Ridge" and
      // "Island" from feeling like the same brush with different names.
      this.brush.radius = tool.defaultRadius;
      this.brush.strength = tool.defaultStrength;
      this.viewport.setBrush(this.brush);
      this.#syncBrushSlider();
      this.#renderSliderGroup('#brush-sliders', [
        { key: 'radius', label: 'Radius', unit: 'm', min: 100, max: 40000, step: 50, format: (v) => v.toFixed(0) },
        { key: 'strength', label: 'Strength', unit: '', min: tool.strengthUnit === 'blend' ? 0.05 : 5, max: tool.strengthUnit === 'blend' ? 1 : 4000, step: tool.strengthUnit === 'blend' ? 0.01 : 5, format: (v) => (tool.strengthUnit === 'blend' ? v.toFixed(2) : v.toFixed(0)) },
        { key: 'falloff', label: 'Falloff', unit: '', min: 0, max: 1, step: 0.01, format: (v) => v.toFixed(2) },
      ], (key, value) => {
        this.brush[key] = value;
        this.viewport.setBrush(this.brush);
      });
      this.#syncBrushSlider();
    }

    for (const button of document.querySelectorAll('.tp-tool')) {
      button.classList.toggle('active', button.dataset.tool === toolId);
    }
    document.querySelector('#tool-hint').textContent = tool.hint;
    document.body.dataset.tool = toolId;
  }

  #seatCameraOnCoast(lookForCoast = true) {
    if (!lookForCoast) return;
    // Find a coastline with land in view: highest score wins, deterministic.
    let best = null;
    const golden = Math.PI * (3 - Math.sqrt(5));
    for (let i = 0; i < 640; i++) {
      const y = 1 - (i / 639) * 1.7;
      const r = Math.sqrt(Math.max(0, 1 - y * y));
      const theta = golden * i;
      const dir = normalize({ x: Math.cos(theta) * r, y, z: Math.sin(theta) * r });
      const elevation = this.field.elevationAt(dir);
      const lat = latLonFromDirection(dir).latitude;
      const coast = Math.abs(elevation);
      const score = coast < 220 && Math.abs(lat) < 45 ? 1 / (coast + 30) : 0;
      if (score > 0 && (!best || score > best.score)) best = { dir, score };
    }
    const target = best?.dir ?? directionFromLatLon(12, 24);
    this.viewport.teleportTo(target, 3200);
  }

  //----------------------------------------------------------------- STATS/IO

  renderStats(stats) {
    const set = (selector, value) => {
      const node = document.querySelector(selector);
      if (node) node.textContent = value;
    };

    set('#stat-fps', `${stats.fps}`);
    set('#stat-frame', `${stats.frameMs.toFixed(1)} ms`);
    set('#stat-tris', formatCount(stats.triangles));
    set('#stat-tiles', `${stats.terrain.resident} / ${stats.terrain.selected}`);
    set('#stat-ram', `${stats.terrain.megabytes.toFixed(1)} MB`);
    set('#stat-build', `${stats.terrain.lastBuildMs.toFixed(1)} ms`);
    set('#stat-water', stats.water ? `${stats.water.resident} tiles` : 'off');
    set('#stat-queue', `${stats.terrain.queued} queued`);
    set('#stat-alt', `${formatCount(stats.altitude)} m`);
    set('#stat-radius', `${formatCount(stats.radius / 1000)} km`);

    const lat = stats.focus.latitude.toFixed(2);
    const lon = stats.focus.longitude.toFixed(2);
    set('#stat-latlon', `${lat}, ${lon}`);
    const latInput = document.querySelector('#teleport-lat');
    const lonInput = document.querySelector('#teleport-lon');
    if (latInput && document.activeElement !== latInput) latInput.placeholder = lat;
    if (lonInput && document.activeElement !== lonInput) lonInput.placeholder = lon;
  }

  renderCursor(cursor) {
    const node = document.querySelector('#cursor-readout');
    if (!node) return;
    node.textContent = `elev ${cursor.elevation.toFixed(0)} m · ${cursor.latitude.toFixed(2)}, ${cursor.longitude.toFixed(2)}`;
  }

  #updateJournalCount() {
    const node = document.querySelector('#stat-journal');
    if (node) node.textContent = `${this.field.ops.length}`;
  }

  saveJournal() {
    const payload = {
      format: 'frontier-sdf-planet',
      version: 1,
      params: this.field.params,
      ocean: this.ocean,
      brush: this.brush,
      edits: this.field.serializeEdits(),
    };
    const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
    download(blob, 'planet-journal.json');
    this.toast(`Saved ${this.field.ops.length} ops`);
  }

  exportObj() {
    const lines = ['# Frontier SDF planet export', '# positions in metres, planet-centred'];
    let vertexOffset = 1;
    let vertexCount = 0;
    let triangleCount = 0;

    for (const record of this.viewport.terrainMeshes.values()) {
      const geometry = record.geometry;
      const position = geometry.getAttribute('position');
      const index = geometry.getIndex();

      for (let i = 0; i < position.count; i++) {
        lines.push(`v ${position.getX(i).toFixed(3)} ${position.getY(i).toFixed(3)} ${position.getZ(i).toFixed(3)}`);
      }
      for (let i = 0; i < index.count; i += 3) {
        lines.push(`f ${index.getX(i) + vertexOffset} ${index.getX(i + 1) + vertexOffset} ${index.getX(i + 2) + vertexOffset}`);
      }
      vertexOffset += position.count;
      vertexCount += position.count;
      triangleCount += index.count / 3;
    }

    download(new Blob([lines.join('\n')], { type: 'text/plain' }), 'planet.obj');
    this.toast(`Exported ${formatCount(vertexCount)} verts / ${formatCount(triangleCount)} tris`);
  }

  toast(message, error = false) {
    const node = document.querySelector('#toast');
    node.textContent = message;
    node.classList.toggle('error', error);
    node.classList.add('show');
    clearTimeout(this.toastTimer);
    this.toastTimer = setTimeout(() => node.classList.remove('show'), 2200);
  }
}

function formatCount(value) {
  if (value >= 1e6) return `${(value / 1e6).toFixed(1)}M`;
  if (value >= 1e3) return `${(value / 1e3).toFixed(1)}k`;
  return `${Math.round(value)}`;
}

function download(blob, filename) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = filename;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 4000);
}

const editor = new LandmassEditor();
window.landmassEditor = editor;
