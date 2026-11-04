/**
 * Cube-sphere topology and quadtree tile addressing for a whole planet.
 *
 * Six faces, each a quadtree of tiles addressed by (face, lod, ix, iy). A tile
 * is a patch of the unit sphere; tile (lod 0) is a full face, and every extra
 * level halves the angular footprint. Because a tile edge is a straight line in
 * the face's tangent plane, neighbouring tiles share exactly the same edge
 * points at the same level — no cracks by construction, and no polar singularity.
 *
 * Face parameters `s, t` run in [-1, 1] and are converted to a direction through
 * an equi-angular (tangent) warp by default: uniform `s` becomes uniform angle
 * from the face centre, which cuts area distortion on the cube-to-sphere map by
 * about a third compared with the naive normalise projection. `warp: 'linear'`
 * keeps the naive projection for comparison and for reprojection of existing
 * heightfield-derived data.
 *
 * All world sizes are metres. Directions are unit vectors in the planet frame.
 */

import { cross, dot, normalize } from './vec3.js';

export const FACE_COUNT = 6;

export const CUBE_FACES = Object.freeze([
  Object.freeze({ name: '+X', normal: Object.freeze([1, 0, 0]), u: Object.freeze([0, 0, -1]), v: Object.freeze([0, 1, 0]) }),
  Object.freeze({ name: '-X', normal: Object.freeze([-1, 0, 0]), u: Object.freeze([0, 0, 1]), v: Object.freeze([0, 1, 0]) }),
  Object.freeze({ name: '+Y', normal: Object.freeze([0, 1, 0]), u: Object.freeze([1, 0, 0]), v: Object.freeze([0, 0, -1]) }),
  Object.freeze({ name: '-Y', normal: Object.freeze([0, -1, 0]), u: Object.freeze([1, 0, 0]), v: Object.freeze([0, 0, 1]) }),
  Object.freeze({ name: '+Z', normal: Object.freeze([0, 0, 1]), u: Object.freeze([1, 0, 0]), v: Object.freeze([0, 1, 0]) }),
  Object.freeze({ name: '-Z', normal: Object.freeze([0, 0, -1]), u: Object.freeze([-1, 0, 0]), v: Object.freeze([0, 1, 0]) }),
]);

/** Forward and inverse of the face parameter warp. */
export function warpParam(s, mode = 'tangent') {
  if (mode === 'linear') return s;
  return Math.tan(s * Math.PI * 0.25);
}

export function unwarpParam(s, mode = 'tangent') {
  if (mode === 'linear') return s;
  return Math.atan(s) * 4 / Math.PI;
}

/**
 * Unit direction for face parameters.
 * `s, t` in [-1, 1]; `warp` selects the spacing model (see module header).
 */
export function faceDirection(face, s, t, warp = 'tangent') {
  const def = CUBE_FACES[face];
  const us = warpParam(s, warp);
  const vt = warpParam(t, warp);
  return normalize({
    x: def.normal[0] + us * def.u[0] + vt * def.v[0],
    y: def.normal[1] + us * def.u[1] + vt * def.v[1],
    z: def.normal[2] + us * def.u[2] + vt * def.v[2],
  });
}

/** World position on the planet surface of a given radius. */
export function facePosition(face, s, t, radius, warp = 'tangent') {
  const dir = faceDirection(face, s, t, warp);
  return { x: dir.x * radius, y: dir.y * radius, z: dir.z * radius };
}

/**
 * Inverse of `faceDirection`: which face owns this direction, and at which
 * (unwarped) parameters. Ties on the dominant axis resolve to the lower face
 * index, so the mapping is total and stable.
 */
export function directionToFace(dir, warp = 'tangent') {
  const ax = Math.abs(dir.x);
  const ay = Math.abs(dir.y);
  const az = Math.abs(dir.z);

  let face;
  let sd;
  let td;

  if (ax >= ay && ax >= az) {
    face = dir.x >= 0 ? 0 : 1;
    const inv = 1 / Math.abs(dir.x);
    // +X: s = -z/x, t = y/x; -X: s = z/|x|, t = y/|x|
    sd = (dir.x >= 0 ? -dir.z : dir.z) * inv;
    td = dir.y * inv;
  } else if (ay >= az) {
    face = dir.y >= 0 ? 2 : 3;
    const inv = 1 / Math.abs(dir.y);
    // +Y: s = x/y, t = -z/y; -Y: s = x/|y|, t = z/|y|
    sd = dir.x * inv;
    td = (dir.y >= 0 ? -dir.z : dir.z) * inv;
  } else {
    face = dir.z >= 0 ? 4 : 5;
    const inv = 1 / Math.abs(dir.z);
    // +Z: s = x/z, t = y/z; -Z: s = -x/|z|, t = y/|z|
    sd = (dir.z >= 0 ? dir.x : -dir.x) * inv;
    td = dir.y * inv;
  }

  return { face, s: unwarpParam(sd, warp), t: unwarpParam(td, warp) };
}

export function tileKey(face, lod, ix, iy) {
  return `${face}:${lod}:${ix}:${iy}`;
}

export function parseTileKey(key) {
  const [face, lod, ix, iy] = key.split(':').map(Number);
  return { face, lod, ix, iy };
}

export function createTile(face, lod, ix, iy) {
  const span = 1 / (1 << lod);
  return {
    face,
    lod,
    ix,
    iy,
    key: tileKey(face, lod, ix, iy),
    s0: -1 + ix * 2 * span,
    t0: -1 + iy * 2 * span,
    span: 2 * span,
  };
}

/** Face parameter of the tile centre. */
export function tileCenterParams(tile) {
  return { s: tile.s0 + tile.span * 0.5, t: tile.t0 + tile.span * 0.5 };
}

export function tileCenterDirection(tile, warp = 'tangent') {
  const c = tileCenterParams(tile);
  return faceDirection(tile.face, c.s, c.t, warp);
}

/** Half the diagonal angle of the patch, in radians. */
export function tileAngularRadius(tile, warp = 'tangent') {
  const c = tileCenterParams(tile);
  const centre = faceDirection(tile.face, c.s, c.t, warp);
  const corner = faceDirection(tile.face, tile.s0, tile.t0, warp);
  return Math.acos(Math.min(1, Math.max(-1, dot(centre, corner))));
}

/** Approximate world edge length of the patch on the given sphere radius. */
export function tileWorldSize(tile, radius, warp = 'tangent') {
  const c = tileCenterParams(tile);
  const a = faceDirection(tile.face, c.s, c.t, warp);
  const b = faceDirection(tile.face, c.s + tile.span, c.t, warp);
  return Math.acos(Math.min(1, Math.max(-1, dot(a, b)))) * radius;
}

/** Which tile at `lod` contains this direction. */
export function tileOfDirection(dir, lod, warp = 'tangent') {
  const { face, s, t } = directionToFace(dir, warp);
  const n = 1 << lod;
  const ix = Math.min(n - 1, Math.max(0, Math.floor(((s + 1) * 0.5) * n)));
  const iy = Math.min(n - 1, Math.max(0, Math.floor(((t + 1) * 0.5) * n)));
  return createTile(face, lod, ix, iy);
}

/**
 * Neighbour tile at the same lod, one step along the face grid (dx, dy in -1..1).
 * Off-face steps are resolved by walking a direction across the edge and asking
 * which face owns it, so the six faces need no hand-written adjacency table.
 */
export function tileNeighbour(tile, dx, dy, warp = 'tangent') {
  const c = tileCenterParams(tile);
  // 0.75 span from the centre lands a quarter-span inside the adjacent patch,
  // clear of the shared edge where the face address is ambiguous.
  const step = tile.span * 0.75;
  const dir = faceDirection(tile.face, c.s + dx * step, c.t + dy * step, warp);
  return tileOfDirection(dir, tile.lod, warp);
}

export function tileCorners(tile, radius, warp = 'tangent') {
  return [
    facePosition(tile.face, tile.s0, tile.t0, radius, warp),
    facePosition(tile.face, tile.s0 + tile.span, tile.t0, radius, warp),
    facePosition(tile.face, tile.s0 + tile.span, tile.t0 + tile.span, radius, warp),
    facePosition(tile.face, tile.s0, tile.t0 + tile.span, radius, warp),
  ];
}

/**
 * LOD choice for the patch under a given camera distance.
 *
 * LOD 0 is a whole cube face (coarsest, six tiles for the planet); each level
 * halves the patch and therefore halves the world size of a cell, because every
 * tile carries the same `cellsPerTile` resolution. The rule picks the coarsest
 * patch whose cell still lands inside the screen budget, so distance and detail
 * trade off the way a clipmap does:
 *
 *   budget = max(minCellSize, distance * pixelBudget)
 *   cell(lod) = faceAngularSize * radius / (cellsPerTile * 2^lod)
 *
 * `pixelBudget` is metres of cell allowed per metre of camera distance (~2-4 mm/m
 * for a 1200-pixel-tall viewport with 2 m near-detail target). `bias` > 1 accepts
 * coarser patches earlier, `bias` < 1 refines early.
 */
export function lodForDistance(
  distance,
  radius,
  faceAngularSize = Math.PI * 0.5,
  cellsPerTile = 48,
  minCellSize = 2,
  bias = 1,
  maxLod = 15,
) {
  const budget = Math.max(minCellSize, Math.max(0, distance) * 0.004 * bias);
  const baseCell = (faceAngularSize * radius) / cellsPerTile;
  for (let lod = 0; lod <= maxLod; lod++) {
    if (baseCell / (1 << lod) <= budget) return lod;
  }
  return maxLod;
}

/** Convenience: unit direction from planet latitude/longitude in degrees. */
export function directionFromLatLon(latitudeDeg, longitudeDeg) {
  const lat = (latitudeDeg * Math.PI) / 180;
  const lon = (longitudeDeg * Math.PI) / 180;
  return {
    x: Math.cos(lat) * Math.cos(lon),
    y: Math.sin(lat),
    z: Math.cos(lat) * Math.sin(lon),
  };
}

export function latLonFromDirection(dir) {
  const n = normalize(dir);
  return {
    latitude: (Math.asin(Math.max(-1, Math.min(1, n.y))) * 180) / Math.PI,
    longitude: (Math.atan2(n.z, n.x) * 180) / Math.PI,
  };
}

/** Outward unit normal is the direction itself; helper keeps call sites uniform. */
export function radialNormal(dir) {
  return normalize(dir);
}

export function faceBasis(face) {
  const def = CUBE_FACES[face];
  return {
    normal: { x: def.normal[0], y: def.normal[1], z: def.normal[2] },
    u: { x: def.u[0], y: def.u[1], z: def.u[2] },
    v: { x: def.v[0], y: def.v[1], z: def.v[2] },
  };
}

/** Cross product of the face's parametric directions, for winding checks. */
export function faceHandedness(face) {
  const b = faceBasis(face);
  return cross(b.u, b.v);
}
