/**
 * Surface nets (naive dual contouring) over a sampled scalar grid.
 *
 * Why this mesher: every chunk of a planet is a thin, moving shell, so the mesh
 * has to be rebuilt constantly while the author sculpts and while the camera
 * moves. Surface nets produce one vertex per sign-changing cell, quads across
 * cell edges, and a watertight manifold — at roughly a fifth of marching-cubes'
 * triangle count and without its sliver triangles. It also consumes a plain
 * scalar field, so the same code serves the radial planet shell today and any
 * Cartesian cave/overhang brick later.
 *
 * Input
 *   values      Float32Array, nx*ny*nz scalar samples (negative inside)
 *   positions   Float32Array, nx*ny*nz*3 world positions of those samples
 *               (the grid may be curved — the mesher never assumes a box)
 *   normals     optional Float32Array, nx*ny*nz*3 smooth normals to interpolate
 *
 * Output: positions/normals/indices plus the originating cell per vertex, which
 * lets a curved-grid caller recover UV/column information for its own shading.
 *
 * Grid convention: index (x, y, z) -> x + y*nx + z*nx*ny. Cells are addressed by
 * their minimum corner and number (nx-1)*(ny-1)*(nz-1). Winding follows the sign
 * of the field: negative inside, so triangle normals point outward.
 */

const INVALID = -1;

export function surfaceNets({ nx, ny, nz, values, positions, normals = null, iso = 0 }) {
  if (nx < 2 || ny < 2 || nz < 2) throw new Error('surface nets needs at least 2 samples per axis');
  const expected = nx * ny * nz;
  if (values.length < expected) throw new Error(`values has ${values.length}, needs ${expected}`);
  if (positions.length < expected * 3) throw new Error(`positions has ${positions.length}, needs ${expected * 3}`);

  const cx = nx - 1;
  const cy = ny - 1;
  const cz = nz - 1;
  const cellCount = cx * cy * cz;

  const sx = 1;
  const sy = nx;
  const sz = nx * ny;

  const cellVertex = new Int32Array(cellCount).fill(INVALID);
  const outPositions = new Float32Array(cellCount * 3);
  const outNormals = normals ? new Float32Array(cellCount * 3) : null;
  const outCells = new Int32Array(cellCount);

  // 8 corner offsets and 12 edges over those corners.
  const CORNER = [0, sx, sy, sx + sy, sz, sx + sz, sy + sz, sx + sy + sz];
  const EDGES = [
    [0, 1], [2, 3], [4, 5], [6, 7],
    [0, 2], [1, 3], [4, 6], [5, 7],
    [0, 4], [1, 5], [2, 6], [3, 7],
  ];

  const cornerValues = new Float64Array(8);
  let vertexCount = 0;

  for (let z = 0; z < cz; z++) {
    for (let y = 0; y < cy; y++) {
      for (let x = 0; x < cx; x++) {
        const base = x + y * sy + z * sz;

        let mask = 0;
        for (let c = 0; c < 8; c++) {
          const v = values[base + CORNER[c]];
          cornerValues[c] = v;
          if (v < iso) mask |= 1 << c;
        }
        if (mask === 0 || mask === 255) continue;

        let px = 0;
        let py = 0;
        let pz = 0;
        let nxc = 0;
        let nyc = 0;
        let nzc = 0;
        let crossings = 0;

        for (let e = 0; e < 12; e++) {
          const a = EDGES[e][0];
          const b = EDGES[e][1];
          const va = cornerValues[a];
          const vb = cornerValues[b];
          const aInside = va < iso;
          if (aInside === vb < iso) continue;

          const denom = vb - va;
          const t = Math.abs(denom) > 1e-12 ? (iso - va) / denom : 0.5;

          const ia = base + CORNER[a];
          const ib = base + CORNER[b];

          px += positions[ia * 3] + (positions[ib * 3] - positions[ia * 3]) * t;
          py += positions[ia * 3 + 1] + (positions[ib * 3 + 1] - positions[ia * 3 + 1]) * t;
          pz += positions[ia * 3 + 2] + (positions[ib * 3 + 2] - positions[ia * 3 + 2]) * t;

          if (normals) {
            nxc += normals[ia * 3] + (normals[ib * 3] - normals[ia * 3]) * t;
            nyc += normals[ia * 3 + 1] + (normals[ib * 3 + 1] - normals[ia * 3 + 1]) * t;
            nzc += normals[ia * 3 + 2] + (normals[ib * 3 + 2] - normals[ia * 3 + 2]) * t;
          }
          crossings++;
        }

        if (crossings === 0) continue;
        const inv = 1 / crossings;
        const cellIndex = x + y * cx + z * cx * cy;
        const vi = vertexCount++;

        outPositions[vi * 3] = px * inv;
        outPositions[vi * 3 + 1] = py * inv;
        outPositions[vi * 3 + 2] = pz * inv;

        if (outNormals) {
          const len = Math.hypot(nxc * inv, nyc * inv, nzc * inv) || 1;
          outNormals[vi * 3] = (nxc * inv) / len;
          outNormals[vi * 3 + 1] = (nyc * inv) / len;
          outNormals[vi * 3 + 2] = (nzc * inv) / len;
        }

        outCells[vi] = cellIndex;
        cellVertex[cellIndex] = vi;
      }
    }
  }

  // One quad per sign-changing cell edge; 2 triangles each.
  const maxIndices = Math.max(0, vertexCount) * 18;
  const indices = new Uint32Array(maxIndices);
  let indexCount = 0;

  const cellIndexAt = (x, y, z) => x + y * cx + z * cx * cy;

  const pushQuad = (a, b, c, d, flip) => {
    if (a === INVALID || b === INVALID || c === INVALID || d === INVALID) return;
    if (indexCount + 6 > indices.length) return;
    if (flip) {
      indices[indexCount++] = a;
      indices[indexCount++] = c;
      indices[indexCount++] = b;
      indices[indexCount++] = a;
      indices[indexCount++] = d;
      indices[indexCount++] = c;
    } else {
      indices[indexCount++] = a;
      indices[indexCount++] = b;
      indices[indexCount++] = c;
      indices[indexCount++] = a;
      indices[indexCount++] = c;
      indices[indexCount++] = d;
    }
  };

  for (let z = 0; z < cz; z++) {
    for (let y = 0; y < cy; y++) {
      for (let x = 0; x < cx; x++) {
        const base = x + y * sy + z * sz;
        const v0 = values[base];
        const inside = v0 < iso;

        // Edge along +x: four cells share it, in the (y, z) plane.
        if (y > 0 && z > 0) {
          const v1 = values[base + sx];
          if (inside !== v1 < iso) {
            const flip = !inside;
            const a = cellVertex[cellIndexAt(x, y - 1, z - 1)];
            const b = cellVertex[cellIndexAt(x, y, z - 1)];
            const c = cellVertex[cellIndexAt(x, y, z)];
            const d = cellVertex[cellIndexAt(x, y - 1, z)];
            pushQuad(a, b, c, d, flip);
          }
        }

        // Edge along +y: four cells in the (x, z) plane. A counter-clockwise loop
        // in (x, z) has normal -y, so this family flips when the cell is inside.
        if (x > 0 && z > 0) {
          const v1 = values[base + sy];
          if (inside !== v1 < iso) {
            const flip = inside;
            const a = cellVertex[cellIndexAt(x - 1, y, z - 1)];
            const b = cellVertex[cellIndexAt(x, y, z - 1)];
            const c = cellVertex[cellIndexAt(x, y, z)];
            const d = cellVertex[cellIndexAt(x - 1, y, z)];
            pushQuad(a, b, c, d, flip);
          }
        }

        // Edge along +z: four cells in the (x, y) plane.
        if (x > 0 && y > 0) {
          const v1 = values[base + sz];
          if (inside !== v1 < iso) {
            const flip = !inside;
            const a = cellVertex[cellIndexAt(x - 1, y - 1, z)];
            const b = cellVertex[cellIndexAt(x, y - 1, z)];
            const c = cellVertex[cellIndexAt(x, y, z)];
            const d = cellVertex[cellIndexAt(x - 1, y, z)];
            pushQuad(a, b, c, d, flip);
          }
        }
      }
    }
  }

  return {
    positions: outPositions.subarray(0, vertexCount * 3),
    normals: outNormals ? outNormals.subarray(0, vertexCount * 3) : null,
    indices: indices.subarray(0, indexCount),
    cells: outCells.subarray(0, vertexCount),
    vertexCount,
    triangleCount: indexCount / 3,
    cellDims: { cx, cy, cz },
  };
}

/**
 * Tighten a mesh: drop degenerate triangles and re-index the vertex buffer so
 * only referenced vertices survive. Worker payloads shrink noticeably because
 * surface nets allocates for the worst case.
 */
export function compactMesh({ positions, normals, indices, cells }) {
  const used = new Int32Array(positions.length / 3).fill(-1);
  let count = 0;
  const map = (index) => {
    if (used[index] < 0) used[index] = count++;
    return used[index];
  };

  const newIndices = new Uint32Array(indices.length);
  let write = 0;
  for (let i = 0; i < indices.length; i += 3) {
    const a = indices[i];
    const b = indices[i + 1];
    const c = indices[i + 2];
    if (a === b || b === c || a === c) continue;
    newIndices[write++] = map(a);
    newIndices[write++] = map(b);
    newIndices[write++] = map(c);
  }

  const outPositions = new Float32Array(count * 3);
  const outNormals = normals ? new Float32Array(count * 3) : null;
  const outCells = cells ? new Int32Array(count) : null;
  for (let i = 0; i < used.length; i++) {
    const target = used[i];
    if (target < 0) continue;
    outPositions[target * 3] = positions[i * 3];
    outPositions[target * 3 + 1] = positions[i * 3 + 1];
    outPositions[target * 3 + 2] = positions[i * 3 + 2];
    if (outNormals) {
      outNormals[target * 3] = normals[i * 3];
      outNormals[target * 3 + 1] = normals[i * 3 + 1];
      outNormals[target * 3 + 2] = normals[i * 3 + 2];
    }
    if (outCells) outCells[target] = cells[i];
  }

  return {
    positions: outPositions,
    normals: outNormals,
    indices: newIndices.subarray(0, write),
    cells: outCells,
    vertexCount: count,
    triangleCount: write / 3,
  };
}
