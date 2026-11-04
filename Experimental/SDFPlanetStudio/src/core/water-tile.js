/**
 * Water surface patches: the ocean as a quadtree of sea-level shells.
 *
 * The sea is not one giant sphere. A planet-sized sphere has to tessellate a
 * 156 km cell per face vertex at any sane vertex count, and the shoreline — the
 * part the camera actually looks at — is exactly where that resolution fails:
 * water depth would be interpolated across kilometres and the foam line would sit
 * in the wrong place.
 *
 * So the ocean reuses the terrain's quadtree: the same patches, at the same LOD,
 * sampled at a coarser cell count. Each water vertex carries the *water depth*
 * under it, computed from the same field the terrain meshes from, which makes the
 * foam band, the shallow-water colour and the transparency agree with the mesh
 * underneath to within one cell.
 *
 * Wave displacement and normals are not stored: they are evaluated in the shader
 * from the same Gerstner spectrum the CPU model owns (`ocean-model.js`), so the
 * sea animates without touching a buffer.
 */

import { CUBE_FACES, faceDirection, tileCenterDirection, tileWorldSize } from './cube-sphere.js';
import { normalize } from './vec3.js';

export const DEFAULT_WATER_OPTIONS = Object.freeze({
  cells: 24,
  warp: 'tangent',
  seaLevelOffset: 0,
});

/**
 * @returns {{
 *   positions: Float32Array, indices: Uint32Array, depths: Float32Array,
 *   vertexCount: number, triangleCount: number, stats: object
 * }}
 * Empty (vertexCount 0) when the patch is entirely dry land.
 */
export function buildWaterTile(field, tile, options = {}) {
  const opts = { ...DEFAULT_WATER_OPTIONS, ...options };
  const started = nowMs();
  const planetRadius = field.radius;
  const seaRadius = planetRadius + opts.seaLevelOffset;
  const centreDir = tileCenterDirection(tile, opts.warp);
  const localOps = field.opsNear(centreDir, tileWorldSize(tile, planetRadius) / planetRadius + 1e-6);

  const nc = opts.cells + 1;
  const positions = new Float32Array(nc * nc * 3);
  const dirs = new Float64Array(nc * nc * 3);
  const depths = new Float32Array(nc * nc);

  let maxDepth = -Infinity;

  for (let j = 0; j < nc; j++) {
    const t = tile.t0 + (j / opts.cells) * tile.span;
    for (let i = 0; i < nc; i++) {
      const s = tile.s0 + (i / opts.cells) * tile.span;
      const dir = faceDirection(tile.face, s, t, opts.warp);
      const index = i + j * nc;
      dirs[index * 3] = dir.x;
      dirs[index * 3 + 1] = dir.y;
      dirs[index * 3 + 2] = dir.z;
      positions[index * 3] = dir.x * seaRadius;
      positions[index * 3 + 1] = dir.y * seaRadius;
      positions[index * 3 + 2] = dir.z * seaRadius;

      const ground = field.surfaceRadius(dir, localOps);
      const depth = seaRadius - ground;
      depths[index] = depth;
      if (depth > maxDepth) maxDepth = depth;
    }
  }

  // Wholly dry patch: no water geometry at all. This is the ocean's culling win.
  if (maxDepth <= 0) {
    return {
      positions: new Float32Array(0),
      indices: new Uint32Array(0),
      depths: new Float32Array(0),
      vertexCount: 0,
      triangleCount: 0,
      stats: { dry: true, cells: opts.cells, buildMs: nowMs() - started },
    };
  }

  const indices = new Uint32Array(opts.cells * opts.cells * 6);
  let write = 0;
  for (let j = 0; j < opts.cells; j++) {
    for (let i = 0; i < opts.cells; i++) {
      const a = i + j * nc;
      const b = i + 1 + j * nc;
      const c = i + 1 + (j + 1) * nc;
      const d = i + (j + 1) * nc;
      // Face parameters (s, t) map to an outward normal via u x v, so this winding
      // faces up, away from the planet.
      indices[write++] = a;
      indices[write++] = b;
      indices[write++] = c;
      indices[write++] = a;
      indices[write++] = c;
      indices[write++] = d;
    }
  }

  const face = CUBE_FACES[tile.face];
  return {
    positions,
    indices,
    depths,
    vertexCount: nc * nc,
    triangleCount: write / 3,
    stats: {
      dry: false,
      cells: opts.cells,
      maxDepth,
      minDepth: minOf(depths),
      seaRadius,
      faceNormal: normalize({ x: face.normal[0], y: face.normal[1], z: face.normal[2] }),
      buildMs: nowMs() - started,
    },
  };
}

/**
 * Fraction of a patch covered by water, from a cheap sparse sample. The viewport
 * uses it to give the ocean manager a priority hint without meshing first.
 */
export function waterCoverage(field, tile, { samples = 5, seaLevelOffset = 0 } = {}) {
  const seaRadius = field.radius + seaLevelOffset;
  const centre = tileCenterDirection(tile);
  const localOps = field.opsNear(centre, 0.2);
  let wet = 0;
  for (let j = 0; j < samples; j++) {
    const t = tile.t0 + ((j + 0.5) / samples) * tile.span;
    for (let i = 0; i < samples; i++) {
      const s = tile.s0 + ((i + 0.5) / samples) * tile.span;
      const dir = faceDirection(tile.face, s, t);
      if (seaRadius - field.surfaceRadius(dir, localOps) > 0) wet++;
    }
  }
  return wet / (samples * samples);
}

function minOf(array) {
  let min = Infinity;
  for (let i = 0; i < array.length; i++) if (array[i] < min) min = array[i];
  return min;
}

function nowMs() {
  if (typeof performance !== 'undefined' && performance.now) return performance.now();
  return Number(process.hrtime.bigint() / 1000n) / 1000;
}
