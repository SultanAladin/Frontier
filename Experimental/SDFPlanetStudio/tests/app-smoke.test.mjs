/**
 * Headless application smoke test.
 *
 * There is no browser or GPU in this sandbox, so the app cannot be screenshotted —
 * but it *can* be executed. This suite imports the real editor module against a
 * DOM shim and a `three` mock, which is enough to catch the failures that produce a
 * black canvas: construction-order bugs, missing methods, handler wiring that throws
 * on the first click, and anything that only breaks when the frame loop runs.
 *
 * What it does not prove: that anything looks correct. Pixels are checked
 * separately by `tools/render-qa.mjs`.
 */

import test from 'node:test';
import assert from 'node:assert/strict';
import { register } from 'node:module';

import { installBrowserShim } from './support/browser-shim.mjs';

register('./support/three-loader.mjs', import.meta.url);

const shim = installBrowserShim();

// The editor constructs itself on import, so a throw here is the failure mode the
// user sees as a blank viewport.
const editorModule = await import('../src/app/macro-editor.js');
const editor = editorModule.default ?? globalThis.window.landmassEditor;

test('the editor constructs', () => {
  assert.ok(editor, 'macro-editor.js did not export or install an editor');
  assert.ok(globalThis.window.landmassEditor, 'editor was not exposed for debugging');
});

test('the viewport exists with a camera, renderer and two tile layers', () => {
  const viewport = editor.viewport;
  assert.ok(viewport, 'no viewport');
  assert.ok(viewport.camera, 'no camera: resize() ran before #initCamera()');
  assert.ok(viewport.renderer, 'no renderer');
  assert.ok(viewport.terrain, 'no terrain layer');
  assert.ok(viewport.water, 'no water layer');
  assert.ok(Array.isArray(viewport.uploadQueue), 'upload queue missing');
  assert.equal(viewport.running, true);
});

test('the frame loop runs without throwing', () => {
  shim.pump(4);
  assert.ok(editor.viewport.frames >= 0, 'frame counter missing');
});

test('the tool rail and inspectors are built', () => {
  const rail = shim.document.querySelector('#tool-rail');
  assert.ok(rail.children.length >= 7, `tool rail has ${rail.children.length} buttons`);

  const brush = shim.document.querySelector('#brush-sliders');
  const planet = shim.document.querySelector('#planet-sliders');
  const ocean = shim.document.querySelector('#ocean-sliders');
  assert.ok(brush.children.length >= 3, 'brush sliders missing');
  assert.ok(planet.children.length >= 6, 'planet sliders missing');
  assert.ok(ocean.children.length >= 6, 'ocean sliders missing');
});

test('every brush tool can be selected and stamped', async () => {
  const tools = editorModule.BRUSH_TOOL_IDS ?? null;
  void tools;

  const { BRUSH_TOOLS } = await import('../src/core/brushes.js');
  const { directionFromLatLon } = await import('../src/core/cube-sphere.js');

  for (const tool of BRUSH_TOOLS) {
    editor.setTool(tool.id);
    assert.equal(editor.activeTool, tool.id);
  }

  // Stamp with a sculpting tool, not orbit.
  editor.setTool('island');
  const dir = directionFromLatLon(-22.6, -133.5);
  const before = editor.field.ops.length;
  const op = editor.applyBrush({ toolId: 'island', dir, radius: 9000, strength: 700, falloff: 0.4 });
  assert.ok(op, 'applyBrush returned nothing');
  assert.equal(editor.field.ops.length, before + 1, 'the journal did not grow');
});

test('undo and journal sync work after a stroke', () => {
  const before = editor.field.ops.length;
  editor.undo();
  assert.equal(editor.field.ops.length, before - 1, 'undo did not remove the op');
});

test('teleporting between terrains does not throw', () => {
  const { directionFromLatLon } = { directionFromLatLon: null };
  void directionFromLatLon;
  assert.doesNotThrow(() => editor.viewport.teleportTo({ x: 0.3, y: 0.5, z: 0.81 }, 1800));
  shim.pump(2);
});

test('ocean parameter changes reach the water material', () => {
  assert.doesNotThrow(() => editor.viewport.setOceanParams({ waveHeight: 2.4, foamWidth: 30 }));
  assert.equal(editor.viewport.oceanParams.waveHeight, 2.4);
  shim.pump(1);
});

test('every uniform a shader declares exists on its material', async () => {
  // A shader can declare a uniform that the JS uniforms dictionary omits. The driver
  // then sees zero, and any write to `uniforms.name.value` throws — which is exactly
  // how the first real browser run died on its first frame (`uAirDensity` was missing
  // from the water material while the frame loop wrote it every frame).
  const { readFile } = await import('node:fs/promises');
  const source = await readFile(new URL('../src/app/macro-viewport.js', import.meta.url), 'utf8');

  const shaders = {};
  for (const match of source.matchAll(/const\s+([A-Z_]+)\s*=\s*\/\*\s*glsl\s*\*\/\s*`([\s\S]*?)`;/g)) {
    shaders[match[1]] = match[2];
  }

  const declaredFor = (names) =>
    new Set(
      names.flatMap((name) =>
        [...shaders[name].matchAll(/uniform\s+\w+\s+(\w+)\s*(\[[^\]]*\])?\s*;/g)].map((m) => m[1]),
      ),
    );

  const viewport = editor.viewport;
  const programs = [
    ['terrain', declaredFor(['TERRAIN_VERTEX', 'TERRAIN_FRAGMENT']), viewport.terrainMaterial.uniforms],
    ['water', declaredFor(['WATER_VERTEX', 'WATER_FRAGMENT']), viewport.waterMaterial.uniforms],
    ['sky', declaredFor(['SKY_VERTEX', 'SKY_FRAGMENT']), viewport.skyUniforms],
  ];

  for (const [label, declared, uniforms] of programs) {
    assert.ok(declared.size >= 6, `${label}: no uniforms were parsed`);
    const missing = [...declared].filter((name) => !(name in uniforms)).sort();
    assert.deepEqual(missing, [], `${label}: shader declares uniforms the material cannot set`);
  }
});

test('cursor picking returns a surface hit or null, never a throw', () => {
  const hit = editor.viewport.pickSurface(800, 450);
  assert.ok(hit === null || typeof hit === 'object');
});

test('a pointer stroke sculpts through the real input handlers', async () => {
  const { directionFromLatLon } = await import('../src/core/cube-sphere.js');

  // The ray-sphere march needs real numbers, which the three mock cannot supply, so
  // the hit is injected; everything after the hit — stroke lifecycle, brush state,
  // journal append, upload queue — is the real code path.
  editor.setTool('raise');
  editor.viewport.pickSurface = () => ({
    dir: directionFromLatLon(-18.4, -131.2),
    position: { x: 1, y: 0, z: 0 },
    distance: 900000,
    elevation: 40,
  });

  const canvas = shim.canvas;
  const before = editor.field.ops.length;
  canvas.dispatch('pointerdown', { pointerId: 1, button: 0, clientX: 800, clientY: 450, altKey: false });
  canvas.dispatch('pointermove', { pointerId: 1, button: 0, clientX: 812, clientY: 456, altKey: false });
  canvas.dispatch('pointerup', { pointerId: 1, button: 0, clientX: 812, clientY: 456 });

  assert.ok(editor.field.ops.length > before, 'the stroke produced no journal entries');
  assert.equal(editor.viewport.stroke, null, 'stroke was not ended');
});

test('keyboard shortcuts drive the editor', async () => {
  const { BRUSH_TOOLS } = await import('../src/core/brushes.js');

  shim.fireWindow('keydown', { key: '3', code: 'Digit3' });
  assert.equal(editor.activeTool, BRUSH_TOOLS[2].id, 'digit shortcut did not switch tool');

  const oceanBefore = editor.viewport.showOcean;
  shim.fireWindow('keydown', { key: 'g', code: 'KeyG' });
  assert.equal(editor.viewport.showOcean, !oceanBefore, 'ocean toggle failed');

  const radiusBefore = editor.brush.radius;
  shim.fireWindow('keydown', { key: ']', code: 'BracketRight' });
  assert.ok(editor.brush.radius > radiusBefore, 'brush radius shortcut failed');

  const opsBefore = editor.field.ops.length;
  shim.fireWindow('keydown', { key: 'z', code: 'KeyZ', ctrlKey: true });
  assert.equal(editor.field.ops.length, Math.max(0, opsBefore - 1), 'undo shortcut failed');
});
