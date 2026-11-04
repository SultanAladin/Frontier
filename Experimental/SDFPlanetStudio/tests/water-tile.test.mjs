import { test } from 'node:test';
import assert from 'node:assert/strict';

import { PlanetField } from '../src/core/planet-field.js';
import { createBrushOp } from '../src/core/brushes.js';
import { buildWaterTile, waterCoverage } from '../src/core/water-tile.js';
import { createTile, directionFromLatLon, tileOfDirection } from '../src/core/cube-sphere.js';

/** First LOD-`lod` patch that is at least 90% water, so tests are not seed-fragile. */
function findWetTile(field, lod) {
  for (let lat = -60; lat <= 60; lat += 15) {
    for (let lon = -180; lon < 180; lon += 20) {
      const tile = tileOfDirection(directionFromLatLon(lat, lon), lod);
      if (waterCoverage(field, tile, { samples: 5 }) > 0.9) return tile;
    }
  }
  throw new Error('no wet patch found');
}

test('water tiles sit exactly on the sea sphere', () => {
  const field = new PlanetField({ radius: 900000, seed: 4029, landAmount: 0.4 });
  const tile = findWetTile(field, 7);
  const water = buildWaterTile(field, tile, { cells: 12, seaLevelOffset: 30 });
  assert.ok(water.vertexCount > 0, 'expected an ocean patch here');

  assert.equal(water.vertexCount, 13 * 13);
  assert.equal(water.indices.length, 12 * 12 * 6);
  const seaRadius = 900000 + 30;
  for (let i = 0; i < water.vertexCount; i++) {
    const r = Math.hypot(water.positions[i * 3], water.positions[i * 3 + 1], water.positions[i * 3 + 2]);
    // Float32 world positions: ~1e-7 relative precision, i.e. ~10 cm at this radius.
    assert.ok(Math.abs(r - seaRadius) < seaRadius * 2e-6, `vertex ${i} at radius ${r}`);
  }
});

test('water depth matches the terrain underneath', () => {
  const field = new PlanetField({ radius: 400000, seed: 12, landAmount: 0.45 });
  const tile = findWetTile(field, 8);
  const water = buildWaterTile(field, tile, { cells: 10 });

  for (let i = 0; i < water.vertexCount; i++) {
    const r = Math.hypot(water.positions[i * 3], water.positions[i * 3 + 1], water.positions[i * 3 + 2]);
    const dir = {
      x: water.positions[i * 3] / r,
      y: water.positions[i * 3 + 1] / r,
      z: water.positions[i * 3 + 2] / r,
    };
    const ground = field.surfaceRadius(dir, field.opsNear(dir, 0.01));
    assert.ok(Math.abs(water.depths[i] - (r - ground)) < 0.1, `depth mismatch at ${i}`);
  }
});

test('a dry patch produces no water geometry at all', () => {
  const field = new PlanetField({ radius: 400000, seed: 5, landAmount: 0.2 });
  const centre = directionFromLatLon(0, 0);

  // Raise the whole patch far above sea level.
  for (const offset of [0, 0.02, -0.02, 0.04, -0.04]) {
    const dir = { x: centre.x + offset, y: centre.y + offset * 0.5, z: centre.z };
    field.addOp(createBrushOp({ toolId: 'raise', dir, radius: 40000, strength: 6000, falloff: 0.1, field }));
  }

  const tile = tileOfDirection(centre, 9);
  const water = buildWaterTile(field, tile, { cells: 8 });
  assert.equal(water.vertexCount, 0);
  assert.equal(water.stats.dry, true);
  assert.equal(waterCoverage(field, tile, { samples: 4 }), 0);
});

test('water tiles are wound upward', () => {
  const field = new PlanetField({ radius: 900000, seed: 3, landAmount: 0.3 });
  const tile = findWetTile(field, 6);
  const water = buildWaterTile(field, tile, { cells: 8 });
  assert.ok(water.vertexCount > 0);

  for (let i = 0; i < Math.min(water.indices.length, 60); i += 3) {
    const ia = water.indices[i];
    const ib = water.indices[i + 1];
    const ic = water.indices[i + 2];
    const ax = water.positions[ia * 3], ay = water.positions[ia * 3 + 1], az = water.positions[ia * 3 + 2];
    const bx = water.positions[ib * 3], by = water.positions[ib * 3 + 1], bz = water.positions[ib * 3 + 2];
    const cx = water.positions[ic * 3], cy = water.positions[ic * 3 + 1], cz = water.positions[ic * 3 + 2];
    const e1 = { x: bx - ax, y: by - ay, z: bz - az };
    const e2 = { x: cx - ax, y: cy - ay, z: cz - az };
    const nx = e1.y * e2.z - e1.z * e2.y;
    const ny = e1.z * e2.x - e1.x * e2.z;
    const nz = e1.x * e2.y - e1.y * e2.x;
    const outward = nx * ax + ny * ay + nz * az;
    assert.ok(outward > 0, `triangle ${i / 3} is wound downward`);
  }
});

test('coverage sampling is a cheap, sensible proxy for the real mesh', () => {
  const field = new PlanetField({ radius: 900000, seed: 77 });
  let checked = 0;
  for (let i = 0; i < 12; i++) {
    const tile = tileOfDirection(directionFromLatLon(i * 14 - 70, i * 29 - 170), 6);
    const coverage = waterCoverage(field, tile, { samples: 5 });
    const water = buildWaterTile(field, tile, { cells: 10 });
    if (coverage === 0) assert.equal(water.vertexCount, 0, 'coverage 0 but a mesh was built');
    if (coverage === 1) assert.equal(water.stats.dry, false);
    checked++;
  }
  assert.equal(checked, 12);
});

test('neighbouring water tiles share their edge vertices', () => {
  const field = new PlanetField({ radius: 900000, seed: 21 });
  const a = createTile(4, 7, 40, 40);
  const b = createTile(4, 7, 41, 40);
  const waterA = buildWaterTile(field, a, { cells: 12 });
  const waterB = buildWaterTile(field, b, { cells: 12 });
  assert.ok(waterA.vertexCount > 0 && waterB.vertexCount > 0);

  const nc = 13;
  for (let j = 0; j < nc; j++) {
    const indexA = 12 + j * nc; // last column of A
    const indexB = 0 + j * nc; // first column of B
    for (let axis = 0; axis < 3; axis++) {
      assert.ok(
        Math.abs(waterA.positions[indexA * 3 + axis] - waterB.positions[indexB * 3 + axis]) < 1e-4,
        `edge mismatch on axis ${axis} row ${j}`,
      );
    }
  }
});
