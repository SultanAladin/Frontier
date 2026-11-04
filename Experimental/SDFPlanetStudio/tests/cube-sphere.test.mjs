import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  CUBE_FACES,
  FACE_COUNT,
  createTile,
  directionFromLatLon,
  directionToFace,
  faceDirection,
  faceHandedness,
  latLonFromDirection,
  lodForDistance,
  tileCenterDirection,
  tileCorners,
  tileKey,
  tileNeighbour,
  tileOfDirection,
  tileWorldSize,
} from '../src/core/cube-sphere.js';

function randomDirections(count, seed = 3) {
  let state = seed;
  const random = () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    return state / 4294967296;
  };
  const out = [];
  for (let i = 0; i < count; i++) {
    // Rejection-free spherical sampling is not required here; normalising suffices.
    const x = random() * 2 - 1;
    const y = random() * 2 - 1;
    const z = random() * 2 - 1;
    const len = Math.hypot(x, y, z) || 1;
    out.push({ x: x / len, y: y / len, z: z / len });
  }
  return out;
}

test('every face basis is right-handed with the outward normal', () => {
  for (let face = 0; face < FACE_COUNT; face++) {
    const h = faceHandedness(face);
    const n = CUBE_FACES[face].normal;
    const d = h.x * n[0] + h.y * n[1] + h.z * n[2];
    assert.ok(d > 0.999, `face ${CUBE_FACES[face].name} handedness ${d}`);
  }
});

test('faceDirection returns unit vectors everywhere in the patch domain', () => {
  for (let face = 0; face < FACE_COUNT; face++) {
    for (let i = 0; i <= 20; i++) {
      for (let j = 0; j <= 20; j++) {
        const s = -1 + (i / 20) * 2;
        const t = -1 + (j / 20) * 2;
        const d = faceDirection(face, s, t);
        assert.ok(Math.abs(Math.hypot(d.x, d.y, d.z) - 1) < 1e-12);
      }
    }
  }
});

test('directionToFace inverts faceDirection across all six faces', () => {
  let worst = 0;
  for (let face = 0; face < FACE_COUNT; face++) {
    for (let i = 0; i <= 16; i++) {
      for (let j = 0; j <= 16; j++) {
        const s = -1 + (i / 16) * 2;
        const t = -1 + (j / 16) * 2;
        const dir = faceDirection(face, s, t);
        const back = directionToFace(dir);
        const again = faceDirection(back.face, back.s, back.t);
        worst = Math.max(worst, Math.hypot(again.x - dir.x, again.y - dir.y, again.z - dir.z));
      }
    }
  }
  assert.ok(worst < 1e-12, `roundtrip error ${worst}`);
});

/**
 * Planet requirement: a direction on a shared cube edge must land on the same
 * physical point whichever face addresses it. Sampling the +X edge at s = 1 and
 * the -Z face at its matching s shows both agree.
 */
test('shared cube edges agree between faces', () => {
  let worst = 0;
  for (let i = 0; i <= 64; i++) {
    const t = -1 + (i / 64) * 2;
    const a = faceDirection(0, 1, t); // +X face, +s edge (towards -Z)
    const b = faceDirection(5, -1, t); // -Z face, -s edge (towards +X)
    worst = Math.max(worst, Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z));
  }
  assert.ok(worst < 1e-12, `edge mismatch ${worst}`);
});

test('equi-angular warp reduces area distortion versus linear projection', () => {
  const measure = (warp) => {
    const steps = 48;
    let min = Infinity;
    let max = 0;
    let sum = 0;
    let count = 0;
    for (let face = 0; face < FACE_COUNT; face++) {
      for (let i = 0; i < steps; i++) {
        for (let j = 0; j < steps; j++) {
          const s = -1 + (i / steps) * 2;
          const t = -1 + (j / steps) * 2;
          const h = 2 / steps;
          const p00 = faceDirection(face, s, t, warp);
          const p10 = faceDirection(face, s + h, t, warp);
          const p01 = faceDirection(face, s, t + h, warp);
          const e1 = { x: p10.x - p00.x, y: p10.y - p00.y, z: p10.z - p00.z };
          const e2 = { x: p01.x - p00.x, y: p01.y - p00.y, z: p01.z - p00.z };
          const cx = e1.y * e2.z - e1.z * e2.y;
          const cy = e1.z * e2.x - e1.x * e2.z;
          const cz = e1.x * e2.y - e1.y * e2.x;
          const area = Math.hypot(cx, cy, cz);
          min = Math.min(min, area);
          max = Math.max(max, area);
          sum += area;
          count++;
        }
      }
    }
    return { ratio: max / min, mean: sum / count };
  };

  const linear = measure('linear');
  const tangent = measure('tangent');
  assert.ok(linear.ratio > 3, `linear distortion ${linear.ratio}`);
  assert.ok(
    tangent.ratio < linear.ratio * 0.6,
    `tangent ${tangent.ratio.toFixed(2)} should be well below linear ${linear.ratio.toFixed(2)}`,
  );
});

test('tile addressing is consistent with its own centre', () => {
  for (let lod = 0; lod <= 6; lod++) {
    const n = 1 << lod;
    for (let i = 0; i < 40; i++) {
      const face = i % FACE_COUNT;
      const ix = Math.floor(((i * 37) % n + n) % n);
      const iy = Math.floor(((i * 53) % n + n) % n);
      const tile = createTile(face, lod, ix, iy);
      const centre = tileCenterDirection(tile);
      const resolved = tileOfDirection(centre, lod);
      assert.equal(resolved.key, tile.key, `centre of ${tile.key} resolved to ${resolved.key}`);
    }
  }
});

test('tiles at the same level share edge samples exactly', () => {
  const lod = 4;
  const left = createTile(0, lod, (1 << lod) - 2, 3);
  const right = createTile(0, lod, (1 << lod) - 1, 3);
  const radius = 1000;
  for (let i = 0; i <= 32; i++) {
    const t = left.t0 + (i / 32) * left.span;
    const a = faceDirection(left.face, left.s0 + left.span, t);
    const b = faceDirection(right.face, right.s0, t);
    assert.ok(Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z) < 1e-12);
  }
  const size = tileWorldSize(left, radius);
  assert.ok(size > 0 && Number.isFinite(size));
});

test('neighbour lookup is symmetric for on-face steps', () => {
  const tile = createTile(4, 5, 12, 15);
  for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
    const neighbour = tileNeighbour(tile, dx, dy);
    const back = tileNeighbour(neighbour, -dx, -dy);
    assert.equal(back.key, tile.key, `step ${dx},${dy} returned ${back.key}`);
  }
  assert.equal(tileNeighbour(tile, 1, 0).key !== tile.key, true);
});

test('neighbour lookup crosses cube faces without dead ends', () => {
  const tile = createTile(0, 3, 7, 2); // +X face, +s edge
  const across = tileNeighbour(tile, 1, 0);
  assert.notEqual(across.face, 0, 'stepping past the +s edge must leave the +X face');
  assert.ok(across.face >= 0 && across.face < FACE_COUNT);
  const dir = tileCenterDirection(across);
  assert.ok(Math.abs(Math.hypot(dir.x, dir.y, dir.z) - 1) < 1e-12);
});

test('tile keys round-trip through their string form', () => {
  const tile = createTile(3, 7, 42, 99);
  assert.equal(tile.key, tileKey(3, 7, 42, 99));
  assert.equal(tile.key, '3:7:42:99');
});

test('LOD refines as the camera approaches and stays bounded', () => {
  const radius = 900000;
  const face = (Math.PI / 2) * 0.9;
  let previous = -1;
  let refined = 0;
  for (const distance of [2000000, 400000, 100000, 20000, 5000, 1000, 200, 50]) {
    const lod = lodForDistance(distance, radius, face, 48, 2);
    assert.ok(lod >= previous, `lod ${lod} should refine past ${previous} at ${distance} m`);
    assert.ok(lod <= 15);
    if (lod > previous) refined++;
    previous = lod;
  }
  assert.ok(refined >= 4, `expected several refinements, got ${refined}`);
  assert.ok(lodForDistance(2000000, radius, face, 48, 2) <= 2, 'planet-wide view uses face-scale tiles');
  assert.ok(lodForDistance(50, radius, face, 48, 2) > 10);
});

test('latitude and longitude round-trip through directions', () => {
  for (const [lat, lon] of [[0, 0], [45, 90], [-30, -120], [80, 179]]) {
    const dir = directionFromLatLon(lat, lon);
    const back = latLonFromDirection(dir);
    assert.ok(Math.abs(back.latitude - lat) < 1e-9);
    assert.ok(Math.abs(back.longitude - lon) < 1e-9 || Math.abs(Math.abs(back.longitude - lon) - 360) < 1e-9);
  }
});

test('tile corners lie on the requested sphere and cover the patch', () => {
  const tile = createTile(2, 6, 7, 60);
  const corners = tileCorners(tile, 6371000);
  for (const corner of corners) {
    assert.ok(Math.abs(Math.hypot(corner.x, corner.y, corner.z) - 6371000) < 1e-6);
  }
  assert.equal(corners.length, 4);
});

test('random directions stay inside the face grid at every LOD', () => {
  for (const dir of randomDirections(200)) {
    for (let lod = 0; lod <= 8; lod++) {
      const tile = tileOfDirection(dir, lod);
      const n = 1 << lod;
      assert.ok(tile.ix >= 0 && tile.ix < n && tile.iy >= 0 && tile.iy < n);
      assert.ok(tile.face >= 0 && tile.face < FACE_COUNT);
    }
  }
});
