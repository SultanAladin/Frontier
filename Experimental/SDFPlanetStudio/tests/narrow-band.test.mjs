import { test } from 'node:test';
import assert from 'node:assert/strict';

import { PlanetField } from '../src/core/planet-field.js';
import { buildTileMesh } from '../src/core/narrow-band.js';
import { createBrushOp } from '../src/core/brushes.js';
import {
  createTile,
  directionToFace,
  faceDirection,
  tileCenterDirection,
  tileWorldSize,
  warpParam,
} from '../src/core/cube-sphere.js';
import { normalize } from '../src/core/vec3.js';

function vertexRadius(mesh, i) {
  return Math.hypot(mesh.positions[i * 3], mesh.positions[i * 3 + 1], mesh.positions[i * 3 + 2]);
}

function vertexDir(mesh, i) {
  const r = vertexRadius(mesh, i);
  return { x: mesh.positions[i * 3] / r, y: mesh.positions[i * 3 + 1] / r, z: mesh.positions[i * 3 + 2] / r };
}

test('a tile mesh follows the field surface to sub-cell accuracy', () => {
  const field = new PlanetField({ radius: 900000, seed: 4029 });
  const tile = createTile(0, 9, 256, 256);
  // No skirts: a skirt hangs a known depth below the surface by design.
  const mesh = buildTileMesh(field, tile, { skirts: false });
  const cell = mesh.stats.worldCellMetres;

  const deviations = [];
  for (let i = 0; i < mesh.vertexCount; i++) {
    const dir = vertexDir(mesh, i);
    const surface = field.surfaceRadius(dir, field.opsNear(dir, 0.02));
    deviations.push(Math.abs(vertexRadius(mesh, i) - surface));
  }
  deviations.sort((a, b) => a - b);
  const median = deviations[Math.floor(deviations.length * 0.5)];
  const p99 = deviations[Math.floor(deviations.length * 0.99)];

  assert.ok(mesh.vertexCount > 1500, `only ${mesh.vertexCount} vertices`);
  // LOD error, not mesher error: the cell cannot represent detail below its own
  // size, so the median must sit well inside a cell and the tail within a few.
  assert.ok(median < cell * 0.2, `median deviation ${median.toFixed(2)} m for a ${cell.toFixed(1)} m cell`);
  assert.ok(p99 < cell * 1.5, `p99 deviation ${p99.toFixed(1)} m for a ${cell.toFixed(1)} m cell`);
});

test('mesh normals are unit length and point away from the planet centre', () => {
  const field = new PlanetField({ radius: 450000, seed: 8 });
  const tile = createTile(3, 8, 100, 130);
  const mesh = buildTileMesh(field, tile);
  const dots = [];
  for (let i = 0; i < mesh.vertexCount; i++) {
    const len = Math.hypot(mesh.normals[i * 3], mesh.normals[i * 3 + 1], mesh.normals[i * 3 + 2]);
    assert.ok(Math.abs(len - 1) < 1e-4, `normal length ${len}`);
    const dir = vertexDir(mesh, i);
    const dot = mesh.normals[i * 3] * dir.x + mesh.normals[i * 3 + 1] * dir.y + mesh.normals[i * 3 + 2] * dir.z;
    // dot == 1 is flat, 0 is a vertical cliff. Steep but outward is legal; inward is not.
    assert.ok(dot > 0.12, `normal points into the ground at vertex ${i}: ${dot.toFixed(3)}`);
    dots.push(dot);
  }
});

test('skirts add one curtain vertex per border column and hang below the surface', () => {
  const field = new PlanetField({ radius: 900000, seed: 3 });
  const tile = createTile(0, 8, 128, 200);
  const plain = buildTileMesh(field, tile, { skirts: false });
  const skirted = buildTileMesh(field, tile, { skirts: true });

  const added = skirted.vertexCount - plain.vertexCount;
  const cells = 48;
  assert.ok(added > 0, 'no skirt vertices were added');
  assert.ok(added <= 4 * cells + 8, `unexpected number of skirt vertices: ${added}`);

  // Skirt vertices are the tail of the buffer and must be radially inside the
  // surface by approximately the requested depth.
  let deepest = 0;
  for (let i = plain.vertexCount; i < skirted.vertexCount; i++) {
    const dir = vertexDir(skirted, i);
    const surface = field.surfaceRadius(dir, field.opsNear(dir, 0.02));
    deepest = Math.max(deepest, surface - vertexRadius(skirted, i));
  }
  const expected = skirted.stats.worldCellMetres * 3;
  assert.ok(deepest > expected * 0.5, `skirt depth ${deepest.toFixed(1)} m vs cell*3 ${expected.toFixed(1)} m`);
  assert.ok(deepest < expected * 2.5, `skirt too deep: ${deepest.toFixed(1)} m`);
});

/** Nearest vertex to a world point, and its radial distance from the planet centre. */
function nearestVertex(mesh, point) {
  let best = Infinity;
  let bestRadius = 0;
  for (let i = 0; i < mesh.vertexCount; i++) {
    const dx = mesh.positions[i * 3] - point.x;
    const dy = mesh.positions[i * 3 + 1] - point.y;
    const dz = mesh.positions[i * 3 + 2] - point.z;
    const d = dx * dx + dy * dy + dz * dz;
    if (d < best) {
      best = d;
      bestRadius = vertexRadius(mesh, i);
    }
  }
  return { distance: Math.sqrt(best), radius: bestRadius };
}

test('both sides of a shared patch edge carry geometry right up to the seam', () => {
  const field = new PlanetField({ radius: 700000, seed: 11 });
  const a = createTile(4, 8, 100, 100);
  const b = createTile(4, 8, 101, 100);

  const meshA = buildTileMesh(field, a, { skirts: false });
  const meshB = buildTileMesh(field, b, { skirts: false });
  const cell = meshA.stats.worldCellMetres;

  let worstA = 0;
  let worstB = 0;
  const columns = 12;
  for (let i = 1; i < columns; i++) {
    const t = a.t0 + (i / columns) * a.span;
    const dir = faceDirection(a.face, a.s0 + a.span, t);
    const radius = field.surfaceRadius(dir, field.opsNear(dir, 0.02));
    const point = { x: dir.x * radius, y: dir.y * radius, z: dir.z * radius };

    worstA = Math.max(worstA, nearestVertex(meshA, point).distance);
    worstB = Math.max(worstB, nearestVertex(meshB, point).distance);
  }
  assert.ok(worstA < cell * 1.5, `tile A leaves a gap of ${worstA.toFixed(1)} m at the seam`);
  assert.ok(worstB < cell * 1.5, `tile B leaves a gap of ${worstB.toFixed(1)} m at the seam`);
});

test('the skirt is deeper than the LOD seam gap it has to hide', () => {
  const field = new PlanetField({ radius: 700000, seed: 11 });
  const fine = createTile(4, 9, 200, 200);
  const coarse = createTile(4, 8, 100, 100);

  const meshFine = buildTileMesh(field, fine, { skirts: false });
  const meshCoarse = buildTileMesh(field, coarse, { skirts: false });

  let worstGap = 0;
  const columns = 16;
  for (let i = 0; i < columns; i++) {
    const t = fine.t0 + (i / (columns - 1)) * fine.span;
    const dir = faceDirection(fine.face, fine.s0, t);
    const radius = field.surfaceRadius(dir, field.opsNear(dir, 0.02));
    const point = { x: dir.x * radius, y: dir.y * radius, z: dir.z * radius };

    const onFine = nearestVertex(meshFine, point);
    const onCoarse = nearestVertex(meshCoarse, point);
    worstGap = Math.max(worstGap, Math.abs(onFine.radius - onCoarse.radius));
  }

  const skirtDepth = meshFine.stats.worldCellMetres * 3;
  assert.ok(worstGap > 0.5, 'the two LODs should differ somewhere along the seam');
  assert.ok(worstGap < skirtDepth, `LOD seam gap ${worstGap.toFixed(1)} m exceeds skirt ${skirtDepth.toFixed(1)} m`);
});

test('the interior of a tile is a manifold; only patch borders stay open', () => {
  const field = new PlanetField({ radius: 500000, seed: 19 });
  const tile = createTile(2, 7, 64, 64);
  const mesh = buildTileMesh(field, tile, { skirts: false });
  const cells = 48;

  const counts = new Map();
  for (let i = 0; i < mesh.indices.length; i += 3) {
    const tri = [mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]];
    for (let e = 0; e < 3; e++) {
      const k = `${tri[e]}>${tri[(e + 1) % 3]}`;
      counts.set(k, (counts.get(k) ?? 0) + 1);
    }
  }

  const isBorderCell = (v) =>
    mesh.cellX[v] === 0 || mesh.cellX[v] === cells - 1 || mesh.cellY[v] === 0 || mesh.cellY[v] === cells - 1;

  let openEdges = 0;
  for (const [k] of counts) {
    const [from, to] = k.split('>');
    if (!counts.has(`${to}>${from}`)) {
      openEdges++;
      assert.ok(
        isBorderCell(Number(from)) && isBorderCell(Number(to)),
        `open edge ${k} does not sit on the patch border`,
      );
    }
  }
  assert.ok(openEdges > 0, 'expected the patch border to be open');
  assert.ok(openEdges <= 4 * cells * 2, `too many open edges: ${openEdges}`);
});

test('the radial band never clips big relief inside one tile', () => {
  const field = new PlanetField({ radius: 300000, seed: 23, landAmount: 0.5 });
  const tile = createTile(1, 7, 40, 88);
  const centre = tileCenterDirection(tile);

  // A spike and a pit inside a single patch: the band has to contain both.
  field.addOp(createBrushOp({ toolId: 'raise', dir: centre, radius: 9000, strength: 2600, falloff: 0.8, field }));
  const pitDir = normalize({ x: centre.x - 0.02, y: centre.y + 0.01, z: centre.z });
  field.addOp(createBrushOp({ toolId: 'lower', dir: pitDir, radius: 9000, strength: 2400, falloff: 0.8, field }));

  // Skirts are extruded deliberately outside the band; measure the surface itself.
  const mesh = buildTileMesh(field, tile, { skirts: false });
  let minRadius = Infinity;
  let maxRadius = 0;
  for (let i = 0; i < mesh.vertexCount; i++) {
    const r = vertexRadius(mesh, i);
    minRadius = Math.min(minRadius, r);
    maxRadius = Math.max(maxRadius, r);
  }

  const spikeSurface = field.surfaceRadius(centre, field.opsNear(centre, 0.05));
  const pitSurface = field.surfaceRadius(pitDir, field.opsNear(pitDir, 0.05));

  assert.ok(maxRadius > spikeSurface - mesh.stats.radialStepMetres * 2, 'the spike was clipped by the band');
  assert.ok(minRadius < pitSurface + mesh.stats.radialStepMetres * 2, 'the pit was clipped by the band');
  // The band must contain the geometry it produced, with a radial step of margin.
  const relief = maxRadius - minRadius;
  assert.ok(
    mesh.stats.bandMetres > relief + mesh.stats.radialStepMetres,
    `band ${mesh.stats.bandMetres.toFixed(0)} m cannot contain ${relief.toFixed(0)} m of surface`,
  );
});

test('sculpting rebuilds the tile with the new surface', () => {
  const field = new PlanetField({ radius: 900000, seed: 31 });
  const tile = createTile(0, 9, 300, 100);
  const before = buildTileMesh(field, tile);
  const centre = tileCenterDirection(tile);

  const beforeMean = meanRadius(before);
  field.addOp(createBrushOp({ toolId: 'raise', dir: centre, radius: 12000, strength: 1800, falloff: 0.6, field }));
  const after = buildTileMesh(field, tile);
  const afterMean = meanRadius(after);

  assert.ok(afterMean > beforeMean + 200, `mean radius ${beforeMean.toFixed(0)} -> ${afterMean.toFixed(0)}`);
  assert.equal(after.stats.localOps, 1, 'tile should see exactly one op');
});

test('tile sampling stays inside the batching budget', () => {
  const field = new PlanetField({ radius: 900000, seed: 2 });
  const tile = createTile(0, 9, 20, 20);
  const mesh = buildTileMesh(field, tile);
  // A Cartesian box over the same patch and band would be ~4 orders of magnitude
  // larger; the shell must stay in the tens of thousands.
  assert.ok(mesh.stats.samples < 120000, `samples ${mesh.stats.samples}`);
  assert.equal(mesh.stats.columns, 49 * 49);
  assert.ok(mesh.stats.radialSamples >= 7 && mesh.stats.radialSamples <= 41);

  // Same cell size, but a band that spans the planet's whole relief instead of
  // this patch's local relief.
  const fullRelief = 5400 + 4600;
  const fullBandSamples = 2401 * (fullRelief / mesh.stats.radialStepMetres);
  const saving = fullBandSamples / mesh.stats.samples;
  assert.ok(saving > 3, `shell only ${saving.toFixed(1)}x cheaper than a full-relief volume`);

  // A Cartesian box over the same patch at 2 m cells would be ~1.35e9 samples.
  const cartesianAtTwoMetres = (3000 / 2) * (3000 / 2) * (1200 / 2);
  assert.ok(cartesianAtTwoMetres / mesh.stats.samples > 1000);
});

test('tile world size halves per LOD so the patch grid stays stable', () => {
  const radius = 900000;
  const a = createTile(0, 6, 32, 32);
  const b = createTile(0, 7, 64, 64);
  const sizeA = tileWorldSize(a, radius);
  const sizeB = tileWorldSize(b, radius);
  assert.ok(Math.abs(sizeA / 2 - sizeB) < sizeA * 0.02, `${sizeA} vs ${sizeB}`);
  assert.ok(Math.abs(Math.hypot(...Object.values(faceDirection(0, 0.5, 0.2))) - 1) < 1e-12);
});

function meanRadius(mesh) {
  let sum = 0;
  for (let i = 0; i < mesh.vertexCount; i++) sum += vertexRadius(mesh, i);
  return sum / mesh.vertexCount;
}
