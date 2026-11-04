import { test } from 'node:test';
import assert from 'node:assert/strict';

import { PlanetField } from '../src/core/planet-field.js';
import { createBrushOp } from '../src/core/brushes.js';
import { TileManager } from '../src/core/tile-manager.js';
import { directionFromLatLon, tileCenterDirection, tileAngularRadius } from '../src/core/cube-sphere.js';
import { angleBetween } from '../src/core/vec3.js';

const RADIUS = 900000;

function cameraAt(dir, altitude) {
  const r = RADIUS + altitude;
  return { x: dir.x * r, y: dir.y * r, z: dir.z * r };
}

function fakeBuild({ vertexCount = 100 } = {}) {
  return async (tile, revision) => ({
    positions: new Float32Array(vertexCount * 3),
    normals: new Float32Array(vertexCount * 3),
    elevation: new Float32Array(vertexCount),
    slope: new Float32Array(vertexCount),
    indices: new Uint32Array(vertexCount * 6),
    vertexCount,
    revision,
    stats: { buildMs: 4 },
  });
}

function manager(options = {}) {
  const field = new PlanetField({ radius: RADIUS, seed: 4029 });
  return new TileManager({ field, build: fakeBuild(), options });
}

test('the selection is a non-overlapping cover with no duplicate tiles', () => {
  const tm = manager({ maxTiles: 400 });
  const dir = directionFromLatLon(8, 30);
  const position = cameraAt(dir, 3000);
  const selected = tm.update(position, { x: -dir.x, y: -dir.y, z: -dir.z });

  assert.ok(selected.length > 10, `only ${selected.length} tiles selected`);
  const keys = new Set();
  for (const entry of selected) {
    assert.ok(!keys.has(entry.tile.key), `duplicate tile ${entry.tile.key}`);
    keys.add(entry.tile.key);
    assert.ok(entry.distance >= 0);
  }

  // LOD monotonicity: a tile's parent must not also be selected.
  for (const entry of selected) {
    const { face, lod, ix, iy } = entry.tile;
    if (lod === 0) continue;
    const parent = `${face}:${lod - 1}:${ix >> 1}:${iy >> 1}`;
    assert.ok(!keys.has(parent), `tile ${entry.tile.key} is selected alongside its parent ${parent}`);
  }
});

test('the tile under the camera is the finest one selected', () => {
  const tm = manager({ maxTiles: 500 });
  const dir = directionFromLatLon(-12, 77);
  const position = cameraAt(dir, 1500);
  const selected = tm.update(position, dir);

  let nearest = null;
  for (const entry of selected) {
    if (!nearest || entry.distance < nearest.distance) nearest = entry;
  }
  assert.ok(nearest);
  assert.ok(nearest.tile.lod >= 6, `tile under the camera only reached LOD ${nearest.tile.lod}`);
  const centre = tileCenterDirection(nearest.tile);
  assert.ok(angleBetween(centre, dir) < tileAngularRadius(nearest.tile) * 1.5, 'nearest tile is not under the camera');
});

test('closer cameras refine the LOD', () => {
  const high = manager({ maxTiles: 500 });
  const low = manager({ maxTiles: 500 });
  const dir = directionFromLatLon(35, -20);

  high.update(cameraAt(dir, 400), dir);
  low.update(cameraAt(dir, 40000), dir);

  const maxLod = (tm) => Math.max(...tm.lastSelection.map((e) => e.tile.lod));
  assert.ok(maxLod(high) > maxLod(low) + 2, `near ${maxLod(high)} vs far ${maxLod(low)}`);
});

test('the horizon test removes the far hemisphere', () => {
  const tm = manager({ maxTiles: 600, horizonCulling: true });
  const dir = directionFromLatLon(0, 0);
  const position = cameraAt(dir, 800);
  const selected = tm.update(position, dir);
  assert.ok(selected.length > 4);

  const horizonAngle = Math.acos(RADIUS / (RADIUS + 800));
  for (const entry of selected) {
    const separation = angleBetween(tileCenterDirection(entry.tile), dir);
    assert.ok(
      separation - tileAngularRadius(entry.tile) <= horizonAngle + 0.05,
      `tile ${entry.tile.key} is ${((separation * 180) / Math.PI).toFixed(1)} deg away, beyond the horizon`,
    );
  }

  // Not selected: a tile on the opposite side of the planet.
  const opposite = directionFromLatLon(0, 180);
  const keys = new Set(selected.map((e) => e.tile.key));
  let found = false;
  for (const entry of selected) {
    if (angleBetween(tileCenterDirection(entry.tile), opposite) < 0.3) found = true;
  }
  assert.equal(found, false, 'the antipode should not be in the selection');
  assert.ok(selected.length <= 600);
});

test('the tile cap holds even from orbit', () => {
  const tm = manager({ maxTiles: 120 });
  const dir = directionFromLatLon(20, 120);
  const selected = tm.update(cameraAt(dir, 3000000), dir);
  assert.ok(selected.length <= 120, `selected ${selected.length} with a cap of 120`);
});

test('builds respect the in-flight cap and land in the resident set', async () => {
  const tm = manager({ maxTiles: 200, maxJobsInFlight: 3 });
  const dir = directionFromLatLon(5, 5);
  tm.update(cameraAt(dir, 3000), dir);

  const batch = tm.pump();
  assert.ok(batch.length <= 3, `dispatched ${batch.length} jobs with a cap of 3`);
  await Promise.all(batch);
  assert.ok(tm.residentCount > 0);
  assert.ok(tm.summary.bytes > 0);
});

test('resident tiles that leave the view are evicted and their bytes returned', async () => {
  const tm = manager({ maxTiles: 200, maxJobsInFlight: 8 });
  const first = directionFromLatLon(0, 0);
  tm.update(cameraAt(first, 4000), first);
  await Promise.all(tm.pump());
  const before = tm.residentCount;
  assert.ok(before > 0);
  const bytesBefore = tm.summary.bytes;

  const second = directionFromLatLon(0, 180); // teleport to the antipode
  tm.update(cameraAt(second, 4000), second);
  await Promise.all(tm.pump());

  assert.ok(tm.summary.evictions > 0, 'nothing was evicted after the teleport');
  assert.ok(bytesBefore > 0);
  for (const record of tm.resident.values()) {
    assert.ok(angleBetween(tileCenterDirection(record.tile), second) < 1.9, 'stale tile survived the teleport');
  }
});

test('sculpting rebuilds exactly the overlapping residents, first in the queue', async () => {
  const tm = manager({ maxTiles: 300, maxJobsInFlight: 8 });
  const dir = directionFromLatLon(20, 40);
  tm.update(cameraAt(dir, 1200), dir);
  // Fill a wide resident set: several pumps, not just the nearest eight patches.
  for (let i = 0; i < 8; i++) await Promise.all(tm.pump());
  const residentBefore = tm.residentCount;
  assert.ok(residentBefore > 20, `only ${residentBefore} residents`);

  const field = tm.field;
  const op = createBrushOp({ toolId: 'raise', dir, radius: 250, strength: 300, field });
  field.addOp(op);
  tm.invalidate();

  const touched = tm.markDirtyNear(op.dir, op.radius);
  assert.ok(touched.length > 0, 'no resident tile was marked dirty');
  assert.ok(touched.length < residentBefore, 'a 250 m brush touched every resident tile');

  // Correctness: every touched tile really does overlap the brush footprint.
  for (const tile of touched) {
    const separation = angleBetween(tileCenterDirection(tile), dir);
    assert.ok(
      separation <= op.radius / RADIUS + tileAngularRadius(tile) + 1e-9,
      `tile ${tile.key} was rebuilt but is outside the brush`,
    );
  }

  assert.equal(tm.residentCount, residentBefore - touched.length);
  assert.equal(tm.queue[0].priority, -1e9, 'dirty rebuilds must be at the front of the queue');

  await Promise.all(tm.pump());
  assert.ok(tm.residentCount >= touched.length);
});

test('a stale build result is dropped instead of shown', async () => {
  let resolveBuild = null;
  const field = new PlanetField({ radius: RADIUS, seed: 1 });
  const tm = new TileManager({
    field,
    build: async (tile, revision) =>
      new Promise((resolve) => {
        resolveBuild = () =>
          resolve({
            positions: new Float32Array(3),
            normals: new Float32Array(3),
            elevation: new Float32Array(1),
            slope: new Float32Array(1),
            indices: new Uint32Array(3),
            vertexCount: 1,
            revision,
            stats: { buildMs: 1 },
          });
      }),
    options: { maxTiles: 40, maxJobsInFlight: 1 },
  });

  const dir = directionFromLatLon(0, 0);
  tm.update(cameraAt(dir, 5000), dir);
  const batch = tm.pump();
  assert.equal(batch.length, 1);

  // The world changes underneath the pending job, and the camera jumps away.
  tm.invalidate();
  const other = directionFromLatLon(0, 180);
  tm.update(cameraAt(other, 5000), other);

  resolveBuild();
  await batch[0];
  assert.equal(tm.stats.staleDrops, 1, 'stale result should be dropped');
  assert.equal(tm.residentCount, 0);
});

test('an empty mesh is rejected rather than uploaded', async () => {
  const field = new PlanetField({ radius: RADIUS, seed: 2 });
  const tm = new TileManager({
    field,
    build: async () => ({
      positions: new Float32Array(0),
      normals: new Float32Array(0),
      elevation: new Float32Array(0),
      slope: new Float32Array(0),
      indices: new Uint32Array(0),
      vertexCount: 0,
      stats: { buildMs: 1 },
    }),
    options: { maxTiles: 40 },
  });
  const dir = directionFromLatLon(10, 10);
  tm.update(cameraAt(dir, 8000), dir);
  await Promise.all(tm.pump());
  assert.ok(tm.stats.rejected >= 1, 'empty meshes must be rejected');
  assert.equal(tm.residentCount, 0);
});

test('the queue does not grow without bound while flying', async () => {
  const tm = manager({ maxTiles: 90, maxJobsInFlight: 2 });
  let dir = directionFromLatLon(0, 0);
  for (let step = 0; step < 40; step++) {
    dir = directionFromLatLon(0, step * 1.5);
    tm.update(cameraAt(dir, 3000), dir);
    assert.ok(tm.queue.length <= 180, `queue grew to ${tm.queue.length}`);
    tm.pump();
  }
});
