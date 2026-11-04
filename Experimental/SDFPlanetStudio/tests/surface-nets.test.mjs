import { test } from 'node:test';
import assert from 'node:assert/strict';

import { surfaceNets, compactMesh } from '../src/core/surface-nets.js';

/**
 * Build a Cartesian sample grid of an analytic SDF so the mesher can be tested
 * against a shape whose true surface is known: the unit sphere.
 */
function gridForSphere(nx, ny, nz, halfExtent, radius = 1) {
  const values = new Float32Array(nx * ny * nz);
  const positions = new Float32Array(nx * ny * nz * 3);
  const normals = new Float32Array(nx * ny * nz * 3);
  const step = (halfExtent * 2) / (nx - 1);

  for (let z = 0; z < nz; z++) {
    for (let y = 0; y < ny; y++) {
      for (let x = 0; x < nx; x++) {
        const px = -halfExtent + x * step;
        const py = -halfExtent + y * step;
        const pz = -halfExtent + z * step;
        const i = x + y * nx + z * nx * ny;
        const d = Math.hypot(px, py, pz);
        values[i] = d - radius;
        positions[i * 3] = px;
        positions[i * 3 + 1] = py;
        positions[i * 3 + 2] = pz;
        normals[i * 3] = px / (d || 1);
        normals[i * 3 + 1] = py / (d || 1);
        normals[i * 3 + 2] = pz / (d || 1);
      }
    }
  }
  return { nx, ny, nz, values, positions, normals, step };
}

function triangleNormal(p, ia, ib, ic) {
  const ax = p[ia * 3], ay = p[ia * 3 + 1], az = p[ia * 3 + 2];
  const bx = p[ib * 3], by = p[ib * 3 + 1], bz = p[ib * 3 + 2];
  const cx = p[ic * 3], cy = p[ic * 3 + 1], cz = p[ic * 3 + 2];
  const e1x = bx - ax, e1y = by - ay, e1z = bz - az;
  const e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
  return {
    x: e1y * e2z - e1z * e2y,
    y: e1z * e2x - e1x * e2z,
    z: e1x * e2y - e1y * e2x,
  };
}

test('surface nets reconstructs a sphere within half a cell', () => {
  const grid = gridForSphere(33, 33, 33, 1.4, 1);
  const mesh = surfaceNets(grid);
  assert.ok(mesh.vertexCount > 1000, `only ${mesh.vertexCount} vertices`);
  assert.ok(mesh.triangleCount > 2000, `only ${mesh.triangleCount} triangles`);

  let worst = 0;
  for (let i = 0; i < mesh.vertexCount; i++) {
    const r = Math.hypot(mesh.positions[i * 3], mesh.positions[i * 3 + 1], mesh.positions[i * 3 + 2]);
    worst = Math.max(worst, Math.abs(r - 1));
  }
  const cellDiagonal = grid.step * Math.sqrt(3);
  assert.ok(worst < cellDiagonal * 0.5, `worst radial error ${worst} vs cell ${cellDiagonal}`);
});

test('triangle winding faces outward for a negative-inside field', () => {
  const grid = gridForSphere(27, 27, 27, 1.3, 1);
  const mesh = surfaceNets(grid);
  let inward = 0;
  let tested = 0;
  for (let i = 0; i < mesh.indices.length; i += 3) {
    const a = mesh.indices[i];
    const b = mesh.indices[i + 1];
    const c = mesh.indices[i + 2];
    const n = triangleNormal(mesh.positions, a, b, c);
    const cx = (mesh.positions[a * 3] + mesh.positions[b * 3] + mesh.positions[c * 3]) / 3;
    const cy = (mesh.positions[a * 3 + 1] + mesh.positions[b * 3 + 1] + mesh.positions[c * 3 + 1]) / 3;
    const cz = (mesh.positions[a * 3 + 2] + mesh.positions[b * 3 + 2] + mesh.positions[c * 3 + 2]) / 3;
    const dot = n.x * cx + n.y * cy + n.z * cz;
    if (dot <= 0) inward++;
    tested++;
  }
  assert.equal(inward, 0, `${inward}/${tested} triangles wound inward`);
});

test('the closed sphere mesh is watertight: every directed edge appears once', () => {
  const grid = gridForSphere(25, 25, 25, 1.3, 1);
  const mesh = surfaceNets(grid);
  const edges = new Map();
  for (let i = 0; i < mesh.indices.length; i += 3) {
    const tri = [mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]];
    for (let e = 0; e < 3; e++) {
      const a = tri[e];
      const b = tri[(e + 1) % 3];
      const key = `${a}>${b}`;
      edges.set(key, (edges.get(key) ?? 0) + 1);
    }
  }
  let unpaired = 0;
  for (const [key, count] of edges) {
    assert.equal(count, 1, `directed edge ${key} used ${count} times`);
    const [a, b] = key.split('>');
    const reverse = edges.get(`${b}>${a}`) ?? 0;
    if (reverse !== 1) unpaired++;
  }
  assert.equal(unpaired, 0, `${unpaired} edges without a matching opposite`);
});

test('interpolated normals are unit length and agree with the analytic gradient', () => {
  const grid = gridForSphere(29, 29, 29, 1.3, 1);
  const mesh = surfaceNets(grid);
  let worstLength = 0;
  let worstAlignment = 0;
  for (let i = 0; i < mesh.vertexCount; i++) {
    const nx = mesh.normals[i * 3];
    const ny = mesh.normals[i * 3 + 1];
    const nz = mesh.normals[i * 3 + 2];
    worstLength = Math.max(worstLength, Math.abs(Math.hypot(nx, ny, nz) - 1));
    const r = Math.hypot(mesh.positions[i * 3], mesh.positions[i * 3 + 1], mesh.positions[i * 3 + 2]);
    const rx = mesh.positions[i * 3] / r;
    const ry = mesh.positions[i * 3 + 1] / r;
    const rz = mesh.positions[i * 3 + 2] / r;
    worstAlignment = Math.max(worstAlignment, 1 - (nx * rx + ny * ry + nz * rz));
  }
  assert.ok(worstLength < 1e-5, `normal length error ${worstLength}`);
  assert.ok(worstAlignment < 0.02, `normal alignment error ${worstAlignment}`);
});

test('an empty (all-outside) grid produces no geometry', () => {
  const n = 6;
  const values = new Float32Array(n * n * n).fill(1);
  const positions = new Float32Array(n * n * n * 3);
  const mesh = surfaceNets({ nx: n, ny: n, nz: n, values, positions });
  assert.equal(mesh.vertexCount, 0);
  assert.equal(mesh.triangleCount, 0);
});

test('a fully solid grid produces no geometry', () => {
  const n = 6;
  const values = new Float32Array(n * n * n).fill(-1);
  const positions = new Float32Array(n * n * n * 3);
  const mesh = surfaceNets({ nx: n, ny: n, nz: n, values, positions });
  assert.equal(mesh.vertexCount, 0);
});

test('compactMesh drops unreferenced vertices and degenerate triangles', () => {
  const grid = gridForSphere(17, 17, 17, 1.2, 1);
  const mesh = surfaceNets(grid);
  const compact = compactMesh(mesh);
  assert.equal(compact.positions.length, compact.vertexCount * 3);
  assert.ok(compact.triangleCount <= mesh.triangleCount);
  assert.ok(compact.vertexCount <= mesh.vertexCount);
  // No index may exceed the compacted vertex range.
  for (let i = 0; i < compact.indices.length; i++) {
    assert.ok(compact.indices[i] < compact.vertexCount);
  }
  const again = compactMesh(compact);
  assert.equal(again.vertexCount, compact.vertexCount, 'compaction is idempotent');
  assert.equal(again.triangleCount, compact.triangleCount);
});

test('surface nets tolerates a curved (non Cartesian) sample grid', () => {
  // Radial shell grid: (longitude, latitude, radius) like the planet tile builder.
  const nu = 24;
  const nv = 24;
  const nr = 9;
  const radius = 100;
  const values = new Float32Array(nu * nv * nr);
  const positions = new Float32Array(nu * nv * nr * 3);
  const normals = new Float32Array(nu * nv * nr * 3);

  // Surface radius varies with longitude/latitude: a lumpy shell.
  const surfaceAt = (u, v) => radius + Math.sin(u * 6.1) * Math.cos(v * 4.7) * 3;
  const rMin = radius - 12;
  const rMax = radius + 12;

  for (let k = 0; k < nr; k++) {
    const r = rMin + (k / (nr - 1)) * (rMax - rMin);
    for (let j = 0; j < nv; j++) {
      for (let i = 0; i < nu; i++) {
        const u = (i / (nu - 1)) * Math.PI * 1.6 - 0.8;
        const v = (j / (nv - 1)) * Math.PI * 1.6 - 0.8;
        const dir = {
          x: Math.cos(v) * Math.cos(u),
          y: Math.sin(v),
          z: Math.cos(v) * Math.sin(u),
        };
        const i3 = i + j * nu + k * nu * nv;
        values[i3] = r - surfaceAt(u, v);
        positions[i3 * 3] = dir.x * r;
        positions[i3 * 3 + 1] = dir.y * r;
        positions[i3 * 3 + 2] = dir.z * r;
        normals[i3 * 3] = dir.x;
        normals[i3 * 3 + 1] = dir.y;
        normals[i3 * 3 + 2] = dir.z;
      }
    }
  }

  const mesh = surfaceNets({ nx: nu, ny: nv, nz: nr, values, positions, normals });
  assert.ok(mesh.vertexCount > 200, `curved grid produced ${mesh.vertexCount} vertices`);
  for (let i = 0; i < mesh.vertexCount; i++) {
    const r = Math.hypot(mesh.positions[i * 3], mesh.positions[i * 3 + 1], mesh.positions[i * 3 + 2]);
    assert.ok(r > rMin - 1 && r < rMax + 1, `vertex radius ${r} outside the band`);
  }
});
