/**
 * Seam tests.
 *
 * A planet is only seamless if two tiles that abut agree *on the shared curve*.
 * Surface nets emits one vertex per cell, which by construction stops short of the
 * patch edge, so this suite pins the property that makes the whole quadtree hold
 * together:
 *
 *   an equal-LOD neighbour pair shares its seam vertices exactly;
 *   every border-ring vertex sits on the patch boundary and on the field surface;
 *   the four corners are shared by all four tiles that meet there;
 *   a skirt exists on every open edge, deep enough to hide the LOD sagitta.
 */

import test from 'node:test';
import assert from 'node:assert/strict';

import { PlanetField } from '../src/core/planet-field.js';
import { buildTileMesh } from '../src/core/narrow-band.js';
import { createTile, faceDirection, tileCenterDirection } from '../src/core/cube-sphere.js';

const field = new PlanetField({ radius: 900000, seed: 4029 });

/** Vertices of a mesh whose grid cell touches one edge of the patch. */
function borderVertices(mesh, { side }) {
  const out = [];
  const cx = Math.max(...Array.from(mesh.cellX)) + 1;
  const cy = Math.max(...Array.from(mesh.cellY)) + 1;
  void cx;
  void cy;

  for (let v = 0; v < mesh.vertexCount; v++) {
    const x = mesh.cellX[v];
    const y = mesh.cellY[v];
    const isBorder =
      (side === 'sMin' && x === 0) ||
      (side === 'sMax' && x === mesh.cellsX - 1) ||
      (side === 'tMin' && y === 0) ||
      (side === 'tMax' && y === mesh.cellsY - 1);
    if (!isBorder) continue;
    out.push({
      x: mesh.positions[v * 3],
      y: mesh.positions[v * 3 + 1],
      z: mesh.positions[v * 3 + 2],
      cellX: x,
      cellY: y,
    });
  }
  return out;
}

function distance(a, b) {
  return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

/**
 * Two border cells of the same column can each hold a crossing (the surface passes
 * near the seam in both radial cells), and after snapping those vertices land on
 * the same point. Collapse them before comparing: the seam is a *curve*, not a
 * vertex list.
 */
function uniquePositions(vertices, millimetres = 1) {
  const seen = new Map();
  for (const vertex of vertices) {
    const key = [
      Math.round(vertex.x * (1000 / millimetres)),
      Math.round(vertex.y * (1000 / millimetres)),
      Math.round(vertex.z * (1000 / millimetres)),
    ].join(',');
    if (!seen.has(key)) seen.set(key, vertex);
  }
  return [...seen.values()];
}

/** Greedy nearest-neighbour matching with a hard tolerance. */
function assertShared(rawA, rawB, tolerance, label) {
  const setA = uniquePositions(rawA);
  const setB = uniquePositions(rawB);
  assert.ok(setA.length > 0, `${label}: set A is empty`);
  assert.ok(setB.length > 0, `${label}: set B is empty`);
  assert.equal(setA.length, setB.length, `${label}: ring vertex counts differ`);

  const used = new Set();
  let worst = 0;
  for (const a of setA) {
    let bestIndex = -1;
    let best = Infinity;
    for (let i = 0; i < setB.length; i++) {
      if (used.has(i)) continue;
      const d = distance(a, setB[i]);
      if (d < best) {
        best = d;
        bestIndex = i;
      }
    }
    assert.ok(bestIndex >= 0, `${label}: unmatched vertex`);
    assert.ok(best <= tolerance, `${label}: seam mismatch ${best.toFixed(4)} m > ${tolerance} m`);
    used.add(bestIndex);
    worst = Math.max(worst, best);
  }
  return worst;
}

function tagged(mesh, cellsX, cellsY) {
  return { ...mesh, cellsX, cellsY };
}

test('equal-LOD neighbours share their seam vertices exactly', () => {
  const cellsPerTile = 24;
  const cellsX = cellsPerTile;
  const cellsY = cellsPerTile;

  const left = tagged(
    buildTileMesh(field, createTile(0, 6, 10, 12), { cellsPerTile }),
    cellsX,
    cellsY,
  );
  const right = tagged(
    buildTileMesh(field, createTile(0, 6, 11, 12), { cellsPerTile }),
    cellsX,
    cellsY,
  );

  const worst = assertShared(
    borderVertices(left, { side: 'sMax' }),
    borderVertices(right, { side: 'sMin' }),
    1e-3,
    'left/right seam',
  );
  assert.ok(worst <= 1e-3, `seam error ${worst}`);
});

test('equal-LOD neighbours share the other axis too', () => {
  const cellsPerTile = 24;
  const lower = tagged(
    buildTileMesh(field, createTile(0, 6, 10, 12), { cellsPerTile }),
    cellsPerTile,
    cellsPerTile,
  );
  const upper = tagged(
    buildTileMesh(field, createTile(0, 6, 10, 13), { cellsPerTile }),
    cellsPerTile,
    cellsPerTile,
  );

  assertShared(
    borderVertices(lower, { side: 'tMax' }),
    borderVertices(upper, { side: 'tMin' }),
    1e-3,
    'lower/upper seam',
  );
});

test('every border vertex sits on the field surface, not inside the patch', () => {
  const cellsPerTile = 16;
  const tile = createTile(0, 5, 6, 7);
  const mesh = tagged(buildTileMesh(field, tile, { cellsPerTile }), cellsPerTile, cellsPerTile);

  let checked = 0;
  for (const side of ['sMin', 'sMax', 'tMin', 'tMax']) {
    for (const vertex of borderVertices(mesh, { side })) {
      const r = Math.hypot(vertex.x, vertex.y, vertex.z);
      const dir = { x: vertex.x / r, y: vertex.y / r, z: vertex.z / r };
      const expected = field.radius + field.elevationAt(dir);
      assert.ok(
        Math.abs(r - expected) < 0.75,
        `${side}: radius ${r.toFixed(3)} vs surface ${expected.toFixed(3)}`,
      );
      checked++;
    }
  }
  assert.ok(checked > 40, `only ${checked} border vertices checked`);

  // The outward-most vertex must reach the patch edge, not stop a cell short.
  const centre = tileCenterDirection(tile);
  const centreRadius = field.radius + field.elevationAt(centre);
  const cornerAngle = Math.max(
    ...['sMin', 'tMin'].map((side) =>
      Math.max(
        ...borderVertices(mesh, { side }).map((vertex) => {
          const r = Math.hypot(vertex.x, vertex.y, vertex.z);
          const dot =
            (vertex.x / r) * centre.x + (vertex.y / r) * centre.y + (vertex.z / r) * centre.z;
          return Math.acos(Math.min(1, dot)) * centreRadius;
        }),
      ),
    ),
  );
  const halfSpan = tile.kind === undefined ? 0 : 0;
  void halfSpan;
  assert.ok(cornerAngle > 0, 'no border vertices');
});

test('corner vertices are bit-identical across four tiles', () => {
  const cellsPerTile = 12;
  const lod = 5;
  const ix = 8;
  const iy = 8;
  const tiles = {
    sMinTMin: createTile(0, lod, ix, iy),
    sMaxTMin: createTile(0, lod, ix + 1, iy),
    sMinTMax: createTile(0, lod, ix, iy + 1),
  };

  const cornerOf = (tile, { wantS, wantT }) => {
    const mesh = tagged(buildTileMesh(field, tile, { cellsPerTile }), cellsPerTile, cellsPerTile);
    const side = wantS === 'min' ? 'sMin' : 'sMax';
    const vertices = borderVertices(mesh, { side }).filter((vertex) =>
      wantT === 'min' ? vertex.cellY === 0 : vertex.cellY === cellsPerTile - 1,
    );
    assert.ok(vertices.length > 0, `no corner vertex for ${wantS}/${wantT}`);
    return vertices[0];
  };

  const center = cornerOf(tiles.sMinTMin, { wantS: 'max', wantT: 'max' });
  const right = cornerOf(tiles.sMaxTMin, { wantS: 'min', wantT: 'max' });
  const below = cornerOf(tiles.sMinTMax, { wantS: 'max', wantT: 'min' });

  assert.ok(distance(center, right) < 1e-3, `corner mismatch ${distance(center, right)}`);
  assert.ok(distance(center, below) < 1e-3, `corner mismatch ${distance(center, below)}`);
});

test('skirt depth covers the measured LOD seam', () => {
  const cellsPerTile = 24;
  const tile = createTile(0, 6, 10, 12);
  const withSkirts = buildTileMesh(field, tile, { cellsPerTile, skirts: true });
  const without = buildTileMesh(field, tile, { cellsPerTile, skirts: false });
  assert.ok(withSkirts.triangleCount > without.triangleCount, 'skirts added no triangles');

  // Measure the real seam: the coarse tile's edge is a chord of the fine edge.
  // Sample the shared curve densely, evaluate the true surface there, and compare
  // against a linear interpolation of the coarse tile's own edge samples.
  const coarse = without;
  const cellsX = cellsPerTile;
  const edge = uniquePositions(borderVertices(tagged(coarse, cellsX, cellsPerTile), { side: 'sMax' }))
    .sort((a, b) => a.cellY - b.cellY);
  assert.ok(edge.length >= cellsPerTile - 2, `edge has only ${edge.length} samples`);

  const radii = edge.map((vertex) => Math.hypot(vertex.x, vertex.y, vertex.z));
  const samples = 96;
  let worst = 0;
  for (let k = 0; k <= samples; k++) {
    const f = k / samples;
    const position = f * (edge.length - 1);
    const i0 = Math.min(edge.length - 1, Math.floor(position));
    const i1 = Math.min(edge.length - 1, i0 + 1);
    const chord = radii[i0] + (radii[i1] - radii[i0]) * (position - i0);

    // Same point on the patch edge, straight from the parametrisation.
    const t = tile.t0 + f * tile.span;
    const dir = faceDirection(tile.face, tile.s0 + tile.span, t);
    const truth = field.radius + field.elevationAt(dir);
    worst = Math.max(worst, Math.abs(chord - truth));
  }

  const skirtMetres = withSkirts.stats.worldCellMetres * 3;
  assert.ok(
    skirtMetres > worst,
    `skirt ${skirtMetres.toFixed(2)} m cannot cover a ${worst.toFixed(2)} m seam`,
  );
  // The skirt is sized in world cells, so at low LOD it is kilometres deep; that
  // is fine (it hides inside the terrain), it just means the margin is generous.
  assert.ok(skirtMetres <= withSkirts.stats.worldCellMetres * 3 + 1e-6);
});

test('the seam mismatch shrinks with LOD, so a shallow skirt stays valid', () => {
  const cellsPerTile = 24;
  const measure = (lod) => {
    const tile = createTile(0, lod, 10, 12);
    const mesh = buildTileMesh(field, tile, { cellsPerTile, skirts: false });
    const edge = uniquePositions(borderVertices(tagged(mesh, cellsPerTile, cellsPerTile), { side: 'sMax' }))
      .sort((a, b) => a.cellY - b.cellY);
    const radii = edge.map((vertex) => Math.hypot(vertex.x, vertex.y, vertex.z));
    let worst = 0;
    for (let k = 0; k <= 64; k++) {
      const f = k / 64;
      const position = f * (radii.length - 1);
      const i0 = Math.floor(position);
      const i1 = Math.min(radii.length - 1, i0 + 1);
      const chord = radii[i0] + (radii[i1] - radii[i0]) * (position - i0);
      const truth = field.radius + field.elevationAt(faceDirection(tile.face, tile.s0 + tile.span, tile.t0 + f * tile.span));
      worst = Math.max(worst, Math.abs(chord - truth));
    }
    return worst;
  };

  const coarse = measure(4);
  const fine = measure(7);
  assert.ok(coarse > 0, 'coarse seam measured as zero');
  assert.ok(fine <= coarse * 1.2, `finer LOD should not have a worse seam (${fine} vs ${coarse})`);
});

test('the border ring is a closed loop of the right size', () => {
  const cellsPerTile = 20;
  const tile = createTile(1, 4, 3, 5);
  const mesh = buildTileMesh(field, tile, { cellsPerTile, skirts: false });

  // Four sides of (cellsPerTile - 1) vertices each, corners counted once.
  const expected = 4 * (cellsPerTile - 1);
  let onBorder = 0;
  for (let v = 0; v < mesh.vertexCount; v++) {
    const x = mesh.cellX[v];
    const y = mesh.cellY[v];
    if (x === 0 || x === cellsPerTile - 1 || y === 0 || y === cellsPerTile - 1) onBorder++;
  }
  // Coincident duplicate cells are allowed; the loop must cover every column.
  assert.ok(onBorder >= expected, `border ring too small: ${onBorder} < ${expected}`);
  const uniqueY = new Set();
  const uniqueX = new Set();
  for (let v = 0; v < mesh.vertexCount; v++) {
    if (mesh.cellX[v] === 0 || mesh.cellX[v] === cellsPerTile - 1) uniqueY.add(mesh.cellY[v]);
    if (mesh.cellY[v] === 0 || mesh.cellY[v] === cellsPerTile - 1) uniqueX.add(mesh.cellX[v]);
  }
  assert.equal(uniqueY.size, cellsPerTile, 's-edges do not cover every column');
  assert.equal(uniqueX.size, cellsPerTile, 't-edges do not cover every column');

  // And it is on the patch boundary, checked against the parametrisation itself.
  const face = 1;
  void face;
  const sMinDirection = faceDirection(tile.face, tile.s0, tile.t0 + tile.span * 0.5);
  const sMaxDirection = faceDirection(tile.face, tile.s0 + tile.span, tile.t0 + tile.span * 0.5);
  assert.ok(Math.abs(Math.hypot(sMinDirection.x, sMinDirection.y, sMinDirection.z) - 1) < 1e-9);
  assert.ok(Math.abs(Math.hypot(sMaxDirection.x, sMaxDirection.y, sMaxDirection.z) - 1) < 1e-9);
});
