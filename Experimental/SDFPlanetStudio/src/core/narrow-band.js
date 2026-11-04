/**
 * Tile builder: turns a quadtree patch + the planet field into a renderable mesh.
 *
 * The grid is a *shell*, not a box
 * --------------------------------
 * Cost is (columns) x (radial samples), and both are bounded by the patch, not by
 * the planet. Two comparisons, both measured in `tests/narrow-band.test.mjs`:
 *
 *   * against a volume that spans the planet's whole relief (-5.4 km to +4.6 km)
 *     at the same cell size, a shell sized to the patch's *local* relief uses
 *     roughly an order of magnitude fewer samples;
 *   * against a Cartesian box over the same patch at a 2 m voxel (a 3 km x 3 km
 *     patch with a 1.2 km band is ~1.35e9 samples), the shell is ~4 orders of
 *     magnitude smaller, because it never allocates the empty air or bedrock.
 *
 * So each tile samples (`cellsPerTile`+1)^2 columns in the face parameter plane
 * and a thin radial band sized from the actual relief of that patch.
 *
 * What makes this an SDF build rather than a heightfield build:
 *
 *   * the stored samples are signed distances (r - surfaceRadius), bilinearly
 *     interpolatable and CSG-composable;
 *   * the band is sized from the field's local min/max envelope, so a patch that
 *     contains a trench and a summit gets a band that contains both — nothing is
 *     clipped to a single height per column;
 *   * the band never clips: if the relief exceeds the cell budget the radial step
 *     grows instead, and the tile stays watertight (documented trade: steep
 *     patches get coarser radial sampling rather than holes);
 *   * the mesher is a general scalar-field mesher (surface-nets.js), so the same
 *     tile pipeline can accept a Cartesian brick with overhangs later.
 *
 * Crack handling
 * --------------
 * Two separate problems, two separate fixes.
 *
 *   * Lateral. Surface nets puts one vertex per *cell*, so a mesh built straight
 *     from the cell vertices stops up to a full cell short of the patch it came
 *     from: every tile would show a slit against its neighbour, and no radial
 *     curtain can cover a tangential slit. So the border ring of vertices is
 *     snapped onto the patch boundary (`snapBorderRing`): both sides of a seam
 *     then land on the same world curve, whatever their LOD.
 *
 *   * Radial. Neighbours at different LOD sample that shared curve at different
 *     densities, so one edge is a chord of the other and the two surfaces differ
 *     by up to the sagitta (millimetres to centimetres here). Each tile grows a
 *     short skirt - a curtain extruded inward along the radial direction at the
 *     four parameter edges - which covers that difference. It is hidden inside
 *     the terrain at equal LOD and never shows on a silhouette because the offset
 *     is radial.
 */

import {
  CUBE_FACES,
  faceDirection,
  tileAngularRadius,
  tileCenterDirection,
  tileWorldSize,
} from './cube-sphere.js';
import { surfaceNets, compactMesh } from './surface-nets.js';
import { normalize } from './vec3.js';

export const DEFAULT_TILE_OPTIONS = Object.freeze({
  cellsPerTile: 48,   // quad cells across the patch (49 x 49 columns)
  warp: 'tangent',
  radialBudget: 0.55, // radial step = worldCell * this
  maxBandCells: 40,   // hard cap on radial samples per column
  minBandCells: 7,
  skirtCells: 3,      // curtain depth in world cells
  skirts: true,
});

/**
 * @typedef {object} TileMesh
 * @property {Float32Array} positions  world-space vertex positions
 * @property {Float32Array} normals    smooth analytic normals
 * @property {Float32Array} elevation  metres above sea level at the vertex column
 * @property {Float32Array} slope      |grad elevation| at the vertex column
 * @property {Uint32Array}  indices
 * @property {number} vertexCount
 * @property {number} triangleCount
 * @property {object} stats            sampling counters for the HUD / evidence
 */

export function buildTileMesh(field, tile, options = {}) {
  const opts = { ...DEFAULT_TILE_OPTIONS, ...options };
  const started = nowMs();

  const planetRadius = field.radius;
  const centreDir = tileCenterDirection(tile, opts.warp);
  const angularRadius = tileAngularRadius(tile, opts.warp);

  // Ops are filtered once per tile, not per sample.
  const localOps = field.opsNear(centreDir, angularRadius * 1.08 + 1e-6);

  const worldCell = tileWorldSize(tile, planetRadius, opts.warp) / opts.cellsPerTile;
  const nc = opts.cellsPerTile + 1;

  const face = CUBE_FACES[tile.face];
  const axisU = { x: face.u[0], y: face.u[1], z: face.u[2] };
  const axisV = { x: face.v[0], y: face.v[1], z: face.v[2] };

  const columnCount = nc * nc;
  // Columns are kept in double precision: adjacent columns are only tens of
  // metres apart on a planet-sized sphere, and Float32 rounding destroys the
  // small-angle differences the slope needs.
  const dirs = new Float64Array(columnCount * 3);
  const columnElev = new Float64Array(columnCount);
  const columnNormal = new Float32Array(columnCount * 3);
  const columnSlope = new Float32Array(columnCount);

  // Pass 1: one field evaluation per column. These exact column heights size the
  // radial band below, so the band can never clip the surface.
  let minElevation = Infinity;
  let maxElevation = -Infinity;
  for (let j = 0; j < nc; j++) {
    const t = tile.t0 + (j / opts.cellsPerTile) * tile.span;
    for (let i = 0; i < nc; i++) {
      const s = tile.s0 + (i / opts.cellsPerTile) * tile.span;
      const dir = faceDirection(tile.face, s, t, opts.warp);
      const col = i + j * nc;
      dirs[col * 3] = dir.x;
      dirs[col * 3 + 1] = dir.y;
      dirs[col * 3 + 2] = dir.z;
      const elevation = field.elevationAt(dir, localOps);
      columnElev[col] = elevation;
      if (elevation < minElevation) minElevation = elevation;
      if (elevation > maxElevation) maxElevation = elevation;
    }
  }

  const relief = Math.max(1, maxElevation - minElevation);
  // Two radial steps of headroom on each side keeps interpolation off the walls.
  const pad = Math.max(worldCell * opts.radialBudget * 2, 8);
  const rMin = planetRadius + minElevation - pad;
  const rMax = planetRadius + maxElevation + pad;

  let radialStep = Math.max(worldCell * opts.radialBudget, (rMax - rMin) / opts.maxBandCells);
  let nr = Math.ceil((rMax - rMin) / radialStep) + 1;
  if (nr < opts.minBandCells) nr = opts.minBandCells;
  if (nr > opts.maxBandCells + 1) nr = opts.maxBandCells + 1;
  // Re-derive so the band is exactly covered: never clip the field to save samples.
  radialStep = (rMax - rMin) / (nr - 1);

  // Pass 2: slope from neighbouring columns (free: they are already evaluated),
  // then the analytic normal n = r_hat - (g1 t1 + g2 t2)/r.
  for (let j = 0; j < nc; j++) {
    for (let i = 0; i < nc; i++) {
      const col = i + j * nc;
      const iL = Math.max(0, i - 1);
      const iR = Math.min(nc - 1, i + 1);
      const jD = Math.max(0, j - 1);
      const jU = Math.min(nc - 1, j + 1);

      const diagonalAngleS = angularBetween(dirs, iR + j * nc, iL + j * nc);
      const diagonalAngleT = angularBetween(dirs, i + jU * nc, i + jD * nc);
      const ds = Math.max(1e-6, diagonalAngleS * planetRadius);
      const dt = Math.max(1e-6, diagonalAngleT * planetRadius);

      const g1 = (columnElev[iR + j * nc] - columnElev[iL + j * nc]) / ds;
      const g2 = (columnElev[i + jU * nc] - columnElev[i + jD * nc]) / dt;

      const d = { x: dirs[col * 3], y: dirs[col * 3 + 1], z: dirs[col * 3 + 2] };
      const t1 = orthogonalize(axisU, d);
      const t2 = orthogonalize(axisV, d);
      const r = planetRadius + columnElev[col];
      const inv = 1 / Math.max(1, r);
      const n = normalize({
        x: d.x - (g1 * t1.x + g2 * t2.x) * inv,
        y: d.y - (g1 * t1.y + g2 * t2.y) * inv,
        z: d.z - (g1 * t1.z + g2 * t2.z) * inv,
      });

      columnNormal[col * 3] = n.x;
      columnNormal[col * 3 + 1] = n.y;
      columnNormal[col * 3 + 2] = n.z;
      columnSlope[col] = Math.hypot(g1, g2);
    }
  }

  // Pass 3: fill the shell samples.
  const sampleCount = nc * nc * nr;
  const values = new Float32Array(sampleCount);
  const positions = new Float32Array(sampleCount * 3);
  const normals = new Float32Array(sampleCount * 3);

  let sampleIndex = 0;
  for (let k = 0; k < nr; k++) {
    const r = rMin + k * radialStep;
    for (let j = 0; j < nc; j++) {
      for (let i = 0; i < nc; i++) {
        const col = i + j * nc;
        const dx = dirs[col * 3];
        const dy = dirs[col * 3 + 1];
        const dz = dirs[col * 3 + 2];
        values[sampleIndex] = r - (planetRadius + columnElev[col]);
        positions[sampleIndex * 3] = dx * r;
        positions[sampleIndex * 3 + 1] = dy * r;
        positions[sampleIndex * 3 + 2] = dz * r;
        normals[sampleIndex * 3] = columnNormal[col * 3];
        normals[sampleIndex * 3 + 1] = columnNormal[col * 3 + 1];
        normals[sampleIndex * 3 + 2] = columnNormal[col * 3 + 2];
        sampleIndex++;
      }
    }
  }

  const raw = surfaceNets({
    nx: nc,
    ny: nc,
    nz: nr,
    values,
    positions,
    normals,
    iso: 0,
  });

  const compact = compactMesh(raw);
  const { cx, cy, cz } = raw.cellDims;

  // The border ring is snapped before anything reads a vertex position, so the
  // skirts, the shading attributes and the vertex buffer all agree.
  const snapColumn = snapBorderRing(compact, {
    cx,
    cy,
    nc,
    dirs,
    columnElev,
    columnNormal,
    planetRadius,
  });
  void cz;

  const elevation = new Float32Array(compact.vertexCount);
  const slope = new Float32Array(compact.vertexCount);
  const cellX = new Int32Array(compact.vertexCount);
  const cellY = new Int32Array(compact.vertexCount);
  const cellZ = new Int32Array(compact.vertexCount);

  for (let v = 0; v < compact.vertexCount; v++) {
    const cell = compact.cells ? compact.cells[v] : -1;
    if (cell < 0) continue;
    const x = cell % cx;
    const y = Math.floor(cell / cx) % cy;
    const z = Math.floor(cell / (cx * cy));
    cellX[v] = x;
    cellY[v] = y;
    cellZ[v] = z;
    const col = snapColumn[v] >= 0 ? snapColumn[v] : x + y * nc;
    elevation[v] = columnElev[col];
    slope[v] = columnSlope[col];
  }

  // Pass 4: skirts along the four parameter edges.
  let merged = {
    positions: compact.positions,
    normals: compact.normals,
    elevation,
    slope,
    indices: compact.indices,
    vertexCount: compact.vertexCount,
    triangleCount: compact.triangleCount,
  };

  if (opts.skirts) {
    merged = addSkirts(merged, {
      cellX,
      cellY,
      cellZ,
      cx,
      cy,
      nc,
      nr,
      skirtMetres: worldCell * opts.skirtCells,
      planetRadius,
    });
  }

  const stats = {
    columns: columnCount,
    radialSamples: nr,
    samples: sampleCount,
    bandMinRadius: rMin,
    bandMaxRadius: rMax,
    bandMetres: rMax - rMin,
    radialStepMetres: radialStep,
    worldCellMetres: worldCell,
    reliefMetres: relief,
    localOps: localOps.length,
    triangles: merged.triangleCount,
    vertices: merged.vertexCount,
    buildMs: nowMs() - started,
  };

  return {
    positions: merged.positions,
    normals: merged.normals,
    elevation: merged.elevation,
    slope: merged.slope,
    indices: merged.indices,
    // Grid cell per vertex: the app does not need it, but it is the cheapest way
    // to reason about border cells in tests and diagnostics.
    cellX,
    cellY,
    cellZ,
    vertexCount: merged.vertexCount,
    triangleCount: merged.triangleCount,
    stats,
  };
}

/**
 * Extrude a curtain inward along the four edges of the parameter patch.
 *
 * `cellX/cellY` come from the mesher, so border vertices are known without
 * inspecting the index buffer. Every curtain vertex is the border vertex pushed
 * inward along its own radial direction, which keeps the curtain behind the
 * surface even on a sphere.
 *
 * Winding: with (s_hat, t_hat, r_hat) right-handed, a quad walked along +t and
 * extruded inward has normal -s_hat, which faces away from a patch on the +s
 * side. The two `flip` edges use the reverse walk.
 */
function addSkirts(mesh, { cellX, cellY, cellZ, cx, cy, nr, skirtMetres }) {
  const keyOf = (edge, along, radial) => edge * 1e7 + along * 1e3 + radial;
  const lookup = new Map();
  for (let v = 0; v < mesh.vertexCount; v++) {
    const x = cellX[v];
    const y = cellY[v];
    const z = cellZ[v];
    if (x === 0) lookup.set(keyOf(0, y, z), v);
    if (x === cx - 1) lookup.set(keyOf(1, y, z), v);
    if (y === 0) lookup.set(keyOf(2, x, z), v);
    if (y === cy - 1) lookup.set(keyOf(3, x, z), v);
  }

  const edges = [
    { id: 0, steps: cy, flip: false },
    { id: 1, steps: cy, flip: true },
    { id: 2, steps: cx, flip: true },
    { id: 3, steps: cx, flip: false },
  ];

  // Upper bound: one curtain vertex per border column, two triangles per pair.
  const capacity = mesh.vertexCount + 2 * (cx + cy) + 8;
  const positions = new Float32Array(capacity * 3);
  const normals = new Float32Array(capacity * 3);
  const elevation = new Float32Array(capacity);
  const slope = new Float32Array(capacity);
  const indices = new Uint32Array(capacity * 6 + mesh.indices.length);

  positions.set(mesh.positions.subarray(0, mesh.vertexCount * 3));
  normals.set(mesh.normals.subarray(0, mesh.vertexCount * 3));
  elevation.set(mesh.elevation.subarray(0, mesh.vertexCount));
  slope.set(mesh.slope.subarray(0, mesh.vertexCount));
  indices.set(mesh.indices);

  let nextIndex = mesh.vertexCount;
  let indexWrite = mesh.indices.length;

  for (const edge of edges) {
    let previous = null;
    for (let along = 0; along < edge.steps; along++) {
      // The radial band is thin, so a border column has at most one crossing.
      let v = -1;
      for (let z = 0; z < nr - 1; z++) {
        const candidate = lookup.get(keyOf(edge.id, along, z));
        if (candidate !== undefined) {
          v = candidate;
          break;
        }
      }
      if (v < 0) {
        previous = null;
        continue;
      }

      const px = mesh.positions[v * 3];
      const py = mesh.positions[v * 3 + 1];
      const pz = mesh.positions[v * 3 + 2];
      const r = Math.hypot(px, py, pz);
      const shrink = Math.max(0, 1 - skirtMetres / Math.max(1, r));

      const skirt = nextIndex++;
      positions[skirt * 3] = px * shrink;
      positions[skirt * 3 + 1] = py * shrink;
      positions[skirt * 3 + 2] = pz * shrink;
      normals[skirt * 3] = mesh.normals[v * 3];
      normals[skirt * 3 + 1] = mesh.normals[v * 3 + 1];
      normals[skirt * 3 + 2] = mesh.normals[v * 3 + 2];
      elevation[skirt] = mesh.elevation[v];
      slope[skirt] = mesh.slope[v];

      if (previous !== null && previous.along === along - 1) {
        const a = previous.surface;
        const b = v;
        const a2 = previous.skirt;
        const b2 = skirt;
        if (edge.flip) {
          indices[indexWrite++] = a;
          indices[indexWrite++] = b2;
          indices[indexWrite++] = b;
          indices[indexWrite++] = a;
          indices[indexWrite++] = a2;
          indices[indexWrite++] = b2;
        } else {
          indices[indexWrite++] = a;
          indices[indexWrite++] = b;
          indices[indexWrite++] = b2;
          indices[indexWrite++] = a;
          indices[indexWrite++] = b2;
          indices[indexWrite++] = a2;
        }
      }
      previous = { along, surface: v, skirt };
    }
  }

  return {
    positions: positions.subarray(0, nextIndex * 3),
    normals: normals.subarray(0, nextIndex * 3),
    elevation: elevation.subarray(0, nextIndex),
    slope: slope.subarray(0, nextIndex),
    indices: indices.subarray(0, indexWrite),
    vertexCount: nextIndex,
    triangleCount: indexWrite / 3,
  };
}

/**
 * Move every vertex whose cell touches a patch edge onto that edge.
 *
 * The direction comes from the border *column* (exactly on the boundary), and the
 * radius from that column's own surface sample, so the snapped vertex stays on
 * the field's surface instead of being projected onto a plane. Corner cells snap
 * on both axes, which is what makes the four corners agree between neighbours.
 *
 * @returns {Int32Array} per-vertex column index (-1 when the vertex is interior)
 */
function snapBorderRing(mesh, { cx, cy, nc, dirs, columnElev, columnNormal, planetRadius }) {
  const columns = new Int32Array(mesh.vertexCount).fill(-1);
  if (!mesh.cells) return columns;

  let moved = 0;
  for (let v = 0; v < mesh.vertexCount; v++) {
    const cell = mesh.cells[v];
    if (cell < 0) continue;
    const x = cell % cx;
    const y = Math.floor(cell / cx) % cy;
    const onS = x === 0 || x === cx - 1;
    const onT = y === 0 || y === cy - 1;
    if (!onS && !onT) continue;

    const i = x === 0 ? 0 : x === cx - 1 ? nc - 1 : Math.min(nc - 1, x);
    const j = y === 0 ? 0 : y === cy - 1 ? nc - 1 : Math.min(nc - 1, y);
    const col = i + j * nc;

    const r = planetRadius + columnElev[col];
    mesh.positions[v * 3] = dirs[col * 3] * r;
    mesh.positions[v * 3 + 1] = dirs[col * 3 + 1] * r;
    mesh.positions[v * 3 + 2] = dirs[col * 3 + 2] * r;
    mesh.normals[v * 3] = columnNormal[col * 3];
    mesh.normals[v * 3 + 1] = columnNormal[col * 3 + 1];
    mesh.normals[v * 3 + 2] = columnNormal[col * 3 + 2];
    columns[v] = col;
    moved++;
  }

  mesh.snapped = moved;
  return columns;
}

function angularBetween(dirs, a, b) {
  const ax = dirs[a * 3];
  const ay = dirs[a * 3 + 1];
  const az = dirs[a * 3 + 2];
  const bx = dirs[b * 3];
  const by = dirs[b * 3 + 1];
  const bz = dirs[b * 3 + 2];
  const d = Math.min(1, Math.max(-1, ax * bx + ay * by + az * bz));
  return Math.acos(d);
}

function orthogonalize(axis, dir) {
  const dot = axis.x * dir.x + axis.y * dir.y + axis.z * dir.z;
  return normalize({
    x: axis.x - dir.x * dot,
    y: axis.y - dir.y * dot,
    z: axis.z - dir.z * dot,
  });
}

function nowMs() {
  if (typeof performance !== 'undefined' && performance.now) return performance.now();
  return Number(process.hrtime.bigint() / 1000n) / 1000;
}
