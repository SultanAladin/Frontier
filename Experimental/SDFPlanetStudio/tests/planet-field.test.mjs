import { test } from 'node:test';
import assert from 'node:assert/strict';

import { PlanetField, EditIndex, brushFalloff, DEFAULT_PLANET_PARAMS } from '../src/core/planet-field.js';
import { BRUSH_TOOLS, BrushStroke, createBrushOp } from '../src/core/brushes.js';
import { directionFromLatLon, faceDirection } from '../src/core/cube-sphere.js';
import { angleBetween, normalize } from '../src/core/vec3.js';

function fibonacciDirections(count) {
  const out = [];
  const golden = Math.PI * (3 - Math.sqrt(5));
  for (let i = 0; i < count; i++) {
    const y = 1 - (i / (count - 1)) * 2;
    const r = Math.sqrt(Math.max(0, 1 - y * y));
    const theta = golden * i;
    out.push({ x: Math.cos(theta) * r, y, z: Math.sin(theta) * r });
  }
  return out;
}

function landFraction(field, samples = 3000) {
  const dirs = fibonacciDirections(samples);
  let land = 0;
  for (const dir of dirs) {
    if (field.elevationAt(dir, field.opsNear(dir)) > 0) land++;
  }
  return land / samples;
}

test('land fraction calibration follows the requested ratio', () => {
  for (const landAmount of [0.22, 0.46, 0.72]) {
    const field = new PlanetField({ landAmount, seed: 77 });
    const actual = landFraction(field);
    assert.ok(
      Math.abs(actual - landAmount) < 0.07,
      `requested ${landAmount}, measured ${actual.toFixed(3)}`,
    );
  }
});

test('the base field has a real ocean floor and real peaks', () => {
  const field = new PlanetField({ seed: 4029 });
  let min = Infinity;
  let max = -Infinity;
  for (const dir of fibonacciDirections(1500)) {
    const e = field.elevationAt(dir, []);
    min = Math.min(min, e);
    max = Math.max(max, e);
  }
  assert.ok(min < -2500, `deepest basin only ${min.toFixed(0)} m`);
  assert.ok(max > 2500, `highest peak only ${max.toFixed(0)} m`);
  assert.ok(max < DEFAULT_PLANET_PARAMS.continentalRelief + DEFAULT_PLANET_PARAMS.mountainRelief + 400);
});

test('elevation is continuous across cube-face borders', () => {
  const field = new PlanetField({ seed: 12 });
  let worst = 0;
  for (let i = 0; i <= 80; i++) {
    const t = -1 + (i / 80) * 2;
    const a = faceDirection(0, 1, t);
    const b = faceDirection(5, -1, t);
    worst = Math.max(worst, Math.abs(field.elevationAt(a, []) - field.elevationAt(b, [])));
  }
  assert.ok(worst < 1e-6, `seam elevation delta ${worst}`);
});

test('field values are signed distances along the radial direction', () => {
  const field = new PlanetField({ seed: 5, radius: 600000 });
  const dir = directionFromLatLon(12, 34);
  const surfaceRadius = field.surfaceRadius(dir, []);
  const above = { x: dir.x * (surfaceRadius + 250), y: dir.y * (surfaceRadius + 250), z: dir.z * (surfaceRadius + 250) };
  const below = { x: dir.x * (surfaceRadius - 250), y: dir.y * (surfaceRadius - 250), z: dir.z * (surfaceRadius - 250) };
  assert.ok(Math.abs(field.distanceAt(above, []) - 250) < 0.5);
  assert.ok(Math.abs(field.distanceAt(below, []) + 250) < 0.5);
});

test('falloff curves are monotone, bounded and reach zero at the rim', () => {
  for (const falloff of [0, 0.35, 0.7, 1]) {
    let previous = Infinity;
    for (let i = 0; i <= 40; i++) {
      const t = i / 40;
      const w = brushFalloff(t, falloff);
      assert.ok(w >= 0 && w <= 1, `weight ${w} out of range`);
      assert.ok(w <= previous + 1e-12, `falloff not monotone at t=${t}`);
      previous = w;
    }
    assert.equal(brushFalloff(1, falloff), 0);
    assert.equal(brushFalloff(0, falloff), 1);
  }
});

test('a raise op adds its strength at the centre and nothing at the rim', () => {
  const field = new PlanetField({ seed: 21 });
  const dir = directionFromLatLon(30, 60);
  const before = field.elevationAt(dir, []);
  const op = createBrushOp({ toolId: 'raise', dir, radius: 20000, strength: 500, falloff: 0.5, field });
  field.addOp(op);

  const atCentre = field.elevationAt(dir, field.opsNear(dir, 0.05));
  assert.ok(Math.abs(atCentre - before - 500) < 1e-6, `centre delta ${atCentre - before}`);

  // 25 km away with a 20 km footprint: completely untouched.
  const outside = normalize({ x: dir.x + 0.03, y: dir.y, z: dir.z });
  assert.ok(Math.abs(field.elevationAt(outside, field.opsNear(outside, 0.05)) - field.baseElevation(outside)) < 1e-9);
});

test('undo restores the previous surface exactly', () => {
  const field = new PlanetField({ seed: 3 });
  const dir = directionFromLatLon(-20, 140);
  const before = field.elevationAt(dir, []);
  field.addOp(createBrushOp({ toolId: 'raise', dir, radius: 30000, strength: 900, field }));
  field.addOp(createBrushOp({ toolId: 'lower', dir, radius: 12000, strength: 400, field }));
  assert.notEqual(field.elevationAt(dir, field.opsNear(dir, 0.05)), before);
  field.undo();
  field.undo();
  assert.equal(field.elevationAt(dir, field.opsNear(dir, 0.05)), before);
});

test('flatten drives the surface to its snapshot target', () => {
  const field = new PlanetField({ seed: 8 });
  const dir = directionFromLatLon(5, 5);
  field.addOp(createBrushOp({ toolId: 'raise', dir, radius: 15000, strength: 1200, field }));

  const target = field.elevationAt(dir, field.opsNear(dir, 0.05)) - 300;
  const flatten = createBrushOp({
    toolId: 'flatten',
    dir,
    radius: 15000,
    strength: 1,
    falloff: 0,
    target,
    field,
  });
  field.addOp(flatten);
  const after = field.elevationAt(dir, field.opsNear(dir, 0.05));
  assert.ok(Math.abs(after - target) < 1, `flatten landed at ${after} wanted ${target}`);
});

test('smooth reduces the slope a raise op created', () => {
  const field = new PlanetField({ seed: 15 });
  const centre = directionFromLatLon(0, 0);
  field.addOp(createBrushOp({ toolId: 'raise', dir: centre, radius: 18000, strength: 1400, falloff: 0.9, field }));

  const probe = (f) => {
    const a = f.elevationAt(centre, f.opsNear(centre, 0.05));
    const offset = normalize({ x: centre.x, y: centre.y + 0.004, z: centre.z });
    const b = f.elevationAt(offset, f.opsNear(offset, 0.05));
    return a - b;
  };

  const before = probe(field);
  field.addOp(createBrushOp({ toolId: 'smooth', dir: centre, radius: 18000, strength: 1, falloff: 0.2, field }));
  const after = probe(field);
  assert.ok(Math.abs(after) < Math.abs(before), `slope ${before.toFixed(1)} -> ${after.toFixed(1)}`);
});

test('island stamps raise land above sea level, gulf carves below it', () => {
  const field = new PlanetField({ seed: 61, radius: 400000 });
  const dir = directionFromLatLon(-12, 88);

  const startElevation = field.elevationAt(dir, []);
  field.addOp(createBrushOp({ toolId: 'island', dir, radius: 40000, strength: 900, seed: 7, field }));
  const islandElevation = field.elevationAt(dir, field.opsNear(dir, 0.2));
  assert.ok(islandElevation > startElevation + 500);

  field.addOp(createBrushOp({ toolId: 'gulf', dir, radius: 30000, strength: 1500, seed: 9, field }));
  const gulfElevation = field.elevationAt(dir, field.opsNear(dir, 0.2));
  assert.ok(gulfElevation < startElevation, `gulf ${gulfElevation} vs ${startElevation}`);
});

test('the op index finds every op that covers a direction, and no others', () => {
  const field = new PlanetField({ seed: 44, radius: 700000 });
  const ops = [];
  for (let i = 0; i < 60; i++) {
    const lat = (i * 37) % 160 - 80;
    const lon = (i * 53) % 350 - 175;
    const dir = directionFromLatLon(lat, lon);
    ops.push(
      createBrushOp({
        toolId: i % 2 ? 'raise' : 'lower',
        dir,
        radius: 30000 + (i % 5) * 4000,
        strength: 200,
        field,
      }),
    );
  }
  const stored = ops.map((op) => field.addOp(op));

  let checked = 0;
  for (const op of stored) {
    const near = field.opsNear(op.dir, 0.02);
    assert.ok(near.some((o) => o.id === op.id), `op ${op.id} missing at its own centre`);
    checked++;
  }
  assert.equal(checked, stored.length);

  // A direction far from every op must come back with an empty list.
  const empty = field.opsNear(directionFromLatLon(89.9, -179.9), 0.0005);
  for (const op of empty) {
    const gap = angleBetween(directionFromLatLon(89.9, -179.9), op.dir) * field.radius;
    assert.ok(gap <= op.radius + 0.0005 * field.radius, `op ${op.id} wrongly returned at ${gap} m`);
  }
});

test('the journal round-trips through serialize/load', () => {
  const field = new PlanetField({ seed: 71, radius: 500000 });
  const dir = directionFromLatLon(22, -14);
  field.addOp(createBrushOp({ toolId: 'raise', dir, radius: 25000, strength: 640, falloff: 0.4, field }));
  field.addOp(createBrushOp({ toolId: 'island', dir: normalize({ x: -0.4, y: 0.6, z: 0.7 }), radius: 60000, strength: 800, seed: 3, field }));

  const saved = field.serializeEdits();
  const probe = [directionFromLatLon(22, -14), directionFromLatLon(-30, 120), directionFromLatLon(60, -80)];
  const before = probe.map((d) => field.elevationAt(d, field.opsNear(d, 0.2)));

  const restored = new PlanetField({ seed: 71, radius: 500000 });
  assert.equal(restored.loadEdits(saved), saved.length);
  const after = probe.map((d) => restored.elevationAt(d, restored.opsNear(d, 0.2)));

  for (let i = 0; i < probe.length; i++) {
    assert.ok(Math.abs(before[i] - after[i]) < 0.5, `probe ${i}: ${before[i]} vs ${after[i]}`);
  }
});

test('the journal is bytes, not voxels', () => {
  const field = new PlanetField({ seed: 1, radius: 900000 });
  const dir = directionFromLatLon(10, 10);
  for (let i = 0; i < 500; i++) {
    field.addOp(createBrushOp({ toolId: 'raise', dir, radius: 9000, strength: 90, field }));
  }
  const bytes = JSON.stringify(field.serializeEdits()).length;
  assert.ok(bytes < 60000, `500 strokes serialised to ${bytes} bytes`);
});

test('stroke sampling emits spaced ops and respects its cap', () => {
  const field = new PlanetField({ seed: 2, radius: 900000 });
  const stroke = new BrushStroke({ toolId: 'raise', radius: 5000, strength: 100, maxOps: 6, spacing: 0.5 });
  const start = directionFromLatLon(0, 0);
  let emitted = 0;
  for (let i = 0; i < 200; i++) {
    const dir = normalize({ x: start.x, y: start.y + i * 1e-5, z: start.z });
    emitted += stroke.push(dir, field).length;
  }
  assert.ok(emitted > 0);
  assert.ok(stroke.ops.length <= 6, `stroke exceeded cap: ${stroke.ops.length}`);
  assert.equal(field.ops.length, stroke.ops.length);
});

test('every non-navigate tool maps to a real brush kind', () => {
  for (const tool of BRUSH_TOOLS) {
    if (tool.id === 'orbit') {
      assert.equal(tool.kind, null);
      continue;
    }
    assert.ok(
      ['raise', 'lower', 'smooth', 'flatten', 'ridge', 'island', 'gulf'].includes(tool.kind),
      `${tool.id} has unknown kind ${tool.kind}`,
    );
    const op = createBrushOp({ toolId: tool.id, dir: { x: 0, y: 1, z: 0 }, radius: 5000, strength: 1 });
    assert.ok(op.radius > 0);
    assert.equal(op.kind, tool.kind);
  }
});

test('EditIndex declares a small, deduplicated cell set for a cap', () => {
  const cells = EditIndex.cellsForCap({ x: 1, y: 0, z: 0 }, 0.2, []);
  assert.ok(cells.length >= 1 && cells.length <= 64, `cells ${cells.length}`);
  assert.equal(new Set(cells).size, cells.length);
});
