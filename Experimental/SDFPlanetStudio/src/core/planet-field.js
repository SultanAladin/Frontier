/**
 * The planet's signed field: an analytic SDF shell plus a sparse journal of
 * authored edits.
 *
 * Representation
 * --------------
 * The field is `f(p) = |p| - (radius + elevation(p/|p|))`, negative inside the
 * planet. `elevation` is in metres relative to the mean sea-level sphere, so the
 * zero set is the coastline and the ocean surface is exactly `radius`.
 *
 * `elevation` is *not* a heightmap and is *not* stored on a grid. It is the sum of
 *
 *   1. a continuous analytic base built from 3D noise sampled on the unit
 *      direction (so the six cube faces meet seamlessly and there is no
 *      projection distortion and no fixed raster anywhere), and
 *   2. a small ordered journal of authored brush operations, each a record of a
 *      few dozen bytes (centre direction, footprint radius, strength, falloff,
 *      seed). Nothing is voxelised unless a tile is being built for display.
 *
 * Consequences that matter for this prototype:
 *
 *   * RAM cost is proportional to `journal.length`, not to planet area. A
 *     continent sculpted with 400 strokes costs a few tens of kilobytes, so a
 *     planet with dozens of authored regions still fits in a small working set.
 *   * IO cost is the same journal. There is no base terrain to stream, which is
 *     the whole answer to "planet scale on an HDD": only edits move.
 *   * Sampling anywhere is O(ops covering that direction), and a tile prefilters
 *     the journal once to the handful of ops that can touch it.
 *   * Detail is limited by the sampled resolution at query time, so the same
 *     journal can be meshed at 64 m for a fly-by and 2 m for sculpting.
 *
 * The base is a radial displacement field, i.e. every column has exactly one
 * surface crossing. That is the right primitive for continents, islands, gulfs
 * and shelves. True 3D CSG (caves, overhangs, arches) plugs into the same
 * `distanceAt` signature later; the mesher already consumes a plain scalar field.
 */

import { Noise3 } from './noise.js';
import { angleBetween, clamp, dot, normalize, smoothstep, tangentBasis } from './vec3.js';

export const DEFAULT_PLANET_PARAMS = Object.freeze({
  radius: 900000,          // [m] mean sea-level sphere
  seed: 4029,
  continentScale: 2.35,    // continental noise frequency (cycles across the sphere)
  coastScale: 17.0,        // coastline roughness frequency
  mountainScale: 6.4,      // ridgeline frequency
  detailScale: 48.0,       // surface roughness frequency
  landAmount: 0.46,        // target land fraction of the sphere surface
  coastSharpness: 0.55,    // 0 = smooth blobs, 1 = deeply indented coasts
  continentalRelief: 850,  // [m] plateau/continental interior height
  mountainRelief: 3400,    // [m] peak height above the interior
  oceanFloorDepth: 4200,   // [m] abyssal depth below sea level
  detailMetres: 90,        // [m] high-frequency surface roughness
  shelfSlope: 0.16,        // coastal shelf falloff width in noise units
});

/** Immutable-ish parameter bag with clamping and derived cache invalidation. */
export function normalizePlanetParams(input = {}) {
  const src = input && typeof input === 'object' ? input : {};
  const number = (value, fallback, min, max) => {
    const n = Number(value);
    if (!Number.isFinite(n)) return fallback;
    return Math.min(max, Math.max(min, n));
  };
  const def = DEFAULT_PLANET_PARAMS;
  return {
    radius: number(src.radius, def.radius, 20000, 6371000 * 4),
    seed: Math.round(number(src.seed, def.seed, 0, 4294967295)) >>> 0,
    continentScale: number(src.continentScale, def.continentScale, 0.4, 12),
    coastScale: number(src.coastScale, def.coastScale, 2, 120),
    mountainScale: number(src.mountainScale, def.mountainScale, 1, 40),
    detailScale: number(src.detailScale, def.detailScale, 4, 400),
    landAmount: number(src.landAmount, def.landAmount, 0.02, 0.95),
    coastSharpness: number(src.coastSharpness, def.coastSharpness, 0, 1),
    continentalRelief: number(src.continentalRelief, def.continentalRelief, 50, 12000),
    mountainRelief: number(src.mountainRelief, def.mountainRelief, 0, 20000),
    oceanFloorDepth: number(src.oceanFloorDepth, def.oceanFloorDepth, 100, 12000),
    detailMetres: number(src.detailMetres, def.detailMetres, 0, 800),
    shelfSlope: number(src.shelfSlope, def.shelfSlope, 0.02, 0.6),
  };
}

/**
 * Coarse spatial index over brush ops, keyed by cube face and a 16x16 grid of
 * that face. Ops register into every cell their footprint touches; queries ask
 * for the cells a tile covers. This keeps a tile's op list to a handful even
 * with thousands of strokes, without a full BVH.
 */
const INDEX_GRID = 16;

export class EditIndex {
  constructor(radius) {
    this.radius = radius;
    this.cells = new Map();
  }

  clear() {
    this.cells.clear();
  }

  /**
   * Cells whose area intersects a spherical cap of `angularRadius` around `dir`.
   * Filled as a disc (rings times spokes) rather than a rim, so both registration
   * of a large brush and a query over a large tile see every cell they overlap.
   */
  static cellsForCap(dir, angularRadius, out = []) {
    out.length = 0;
    const cap = Math.min(Math.PI, Math.max(0, angularRadius));
    const [t1, t2] = tangentBasis(dir);

    const rings = Math.max(1, Math.min(24, Math.ceil((cap / (Math.PI / 2)) * INDEX_GRID) + 1));
    const spokes = Math.max(6, Math.min(48, rings * 4));

    EditIndex.pushDirectionCell(dir, out);
    for (let ring = 1; ring <= rings; ring++) {
      const spread = (ring / rings) * cap;
      for (let spoke = 0; spoke < spokes; spoke++) {
        const theta = (spoke / spokes) * Math.PI * 2;
        const dx = t1.x * Math.cos(theta) + t2.x * Math.sin(theta);
        const dy = t1.y * Math.cos(theta) + t2.y * Math.sin(theta);
        const dz = t1.z * Math.cos(theta) + t2.z * Math.sin(theta);
        const rimDir = normalize({
          x: dir.x + dx * spread,
          y: dir.y + dy * spread,
          z: dir.z + dz * spread,
        });
        EditIndex.pushDirectionCell(rimDir, out);
      }
    }
    return out;
  }

  static pushDirectionCell(dir, out) {
    const ax = Math.abs(dir.x);
    const ay = Math.abs(dir.y);
    const az = Math.abs(dir.z);
    let face;
    let u;
    let v;
    if (ax >= ay && ax >= az) {
      face = dir.x >= 0 ? 0 : 1;
      u = (dir.x >= 0 ? -dir.z : dir.z) / ax;
      v = dir.y / ax;
    } else if (ay >= az) {
      face = dir.y >= 0 ? 2 : 3;
      u = dir.x / ay;
      v = (dir.y >= 0 ? -dir.z : dir.z) / ay;
    } else {
      face = dir.z >= 0 ? 4 : 5;
      u = (dir.z >= 0 ? dir.x : -dir.x) / az;
      v = dir.y / az;
    }
    const cx = clamp(Math.floor(((u + 1) * 0.5) * INDEX_GRID), 0, INDEX_GRID - 1);
    const cy = clamp(Math.floor(((v + 1) * 0.5) * INDEX_GRID), 0, INDEX_GRID - 1);
    const key = face * INDEX_GRID * INDEX_GRID + cy * INDEX_GRID + cx;
    if (!out.includes(key)) out.push(key);
  }

  insert(op) {
    const angularRadius = Math.min(Math.PI, (op.radius / this.radius) * 1.05 + 1e-4);
    const scratch = [];
    EditIndex.cellsForCap(op.dir, angularRadius, scratch);
    for (const key of scratch) {
      let list = this.cells.get(key);
      if (!list) {
        list = [];
        this.cells.set(key, list);
      }
      list.push(op);
    }
  }

  rebuild(ops) {
    this.clear();
    for (const op of ops) this.insert(op);
  }

  /**
   * Ops that can influence any direction within `angularRadius` of `dir`.
   * Ops too far away are dropped by distance, not just by cell, so a false
   * positive from the coarse grid never survives the query.
   */
  query(dir, angularRadius, radius = this.radius) {
    const keys = EditIndex.cellsForCap(dir, Math.min(Math.PI, angularRadius), []);
    const result = [];
    const seen = new Set();
    for (const key of keys) {
      const list = this.cells.get(key);
      if (!list) continue;
      for (const op of list) {
        if (seen.has(op.id)) continue;
        seen.add(op.id);
        const gap = angleBetween(dir, op.dir) * radius;
        if (gap <= op.radius + angularRadius * radius) result.push(op);
      }
    }
    return result;
  }
}

export class PlanetField {
  constructor(params = {}) {
    this.params = normalizePlanetParams(params);
    this.ops = [];
    this.nextOpId = 1;
    this.index = new EditIndex(this.params.radius);
    this._buildNoise();
  }

  _buildNoise() {
    const seed = this.params.seed;
    this.continentalNoise = new Noise3(seed);
    this.coastNoise = new Noise3((seed ^ 0x9e3779b9) >>> 0);
    this.mountainNoise = new Noise3((seed ^ 0x85ebca6b) >>> 0);
    this.detailNoise = new Noise3((seed ^ 0xc2b2ae35) >>> 0);
    this.warpNoise = new Noise3((seed ^ 0x27d4eb2f) >>> 0);
    this.landBias = this._calibrateLandBias();
  }

  setParams(patch) {
    const next = normalizePlanetParams({ ...this.params, ...patch });
    const needsRebuild =
      next.seed !== this.params.seed ||
      next.radius !== this.params.radius ||
      next.continentScale !== this.params.continentScale ||
      next.coastScale !== this.params.coastScale ||
      next.landAmount !== this.params.landAmount ||
      next.coastSharpness !== this.params.coastSharpness;
    this.params = next;
    if (needsRebuild) {
      const radiusChanged = next.radius !== this.index.radius;
      this._buildNoise();
      if (radiusChanged) {
        this.index.radius = next.radius;
        this.index.rebuild(this.ops);
      }
    }
    return this.params;
  }

  /** Mean sea-level sphere radius in metres. */
  get radius() {
    return this.params.radius;
  }

  /**
   * Continental signal: warped low-frequency noise plus a rougher coastline
   * term. Returns roughly [-1.3, 1.3]; values above `landBias` are land.
   */
  continentalSignal(dir) {
    const p = this.params;
    const f = p.continentScale;
    const wx = this.warpNoise.noise(dir.x * 1.7 + 11.3, dir.y * 1.7 - 7.1, dir.z * 1.7 + 3.7) * 0.35;
    const wy = this.warpNoise.noise(dir.x * 1.7 - 5.9, dir.y * 1.7 + 13.7, dir.z * 1.7 - 9.2) * 0.35;
    const wz = this.warpNoise.noise(dir.x * 1.7 + 17.1, dir.y * 1.7 + 2.9, dir.z * 1.7 + 21.4) * 0.35;

    const x = (dir.x + wx) * f;
    const y = (dir.y + wy) * f;
    const z = (dir.z + wz) * f;

    const broad = this.continentalNoise.fbm(x, y, z, 5, 2.02, 0.52);
    const coast = this.coastNoise.fbm(dir.x * p.coastScale, dir.y * p.coastScale, dir.z * p.coastScale, 4);
    return broad + coast * 0.34 * (0.35 + p.coastSharpness);
  }

  /**
   * Land fraction calibration: sample the sphere once, take the quantile that
   * leaves `landAmount` above the bias. Gives a real land/ocean ratio control
   * instead of an abstract "sea level" knob.
   */
  _calibrateLandBias(samples = 1400) {
    const values = new Float64Array(samples);
    const golden = Math.PI * (3 - Math.sqrt(5));
    for (let i = 0; i < samples; i++) {
      const y = 1 - (i / (samples - 1)) * 2;
      const r = Math.sqrt(Math.max(0, 1 - y * y));
      const theta = golden * i;
      const dir = { x: Math.cos(theta) * r, y, z: Math.sin(theta) * r };
      values[i] = this.continentalSignal(dir);
    }
    values.sort();
    const index = Math.min(samples - 1, Math.max(0, Math.floor((1 - this.params.landAmount) * (samples - 1))));
    return values[index];
  }

  /** Analytic base elevation in metres relative to the sea-level sphere. */
  baseElevation(dir, outTangentSlope = null) {
    const p = this.params;
    const signal = this.continentalSignal(dir);
    const land = smoothstep(this.landBias - p.shelfSlope, this.landBias + p.shelfSlope, signal);
    const ocean = 1 - land;

    const interior = Math.pow(land, 1.25);
    const basin = Math.pow(ocean, 1.55);

    // Ridgelines sit inland, fading out at the coast so beaches stay walkable.
    const ridge = this.mountainNoise.ridged(
      dir.x * p.mountainScale,
      dir.y * p.mountainScale,
      dir.z * p.mountainScale,
      5,
    );
    const inland = smoothstep(0.32, 0.86, land);
    const rangeMask = ridge * ridge * inland;

    // Sub-ridges: a range built from one ridged octave is a smooth swell; real
    // ranges carry crests at every scale down to the outcrop. The second ridged
    // octave is what makes a hillside look like a hillside from two kilometres out.
    const subRidge = this.mountainNoise.ridged(
      dir.x * p.mountainScale * 3.15,
      dir.y * p.mountainScale * 3.15,
      dir.z * p.mountainScale * 3.15,
      4,
    );
    const subRidgeMask = subRidge * subRidge * inland;

    const detail = this.detailNoise.fbm(dir.x * p.detailScale, dir.y * p.detailScale, dir.z * p.detailScale, 4);

    // Mid- and fine-frequency roughness. `detailScale` alone is a ~19 km feature,
    // which is invisible from a coastal vantage; these two octaves put relief at
    // the 6 km and 2 km scale where a sculpting session actually looks.
    const rough = this.detailNoise.fbm(
      dir.x * p.detailScale * 3.2,
      dir.y * p.detailScale * 3.2,
      dir.z * p.detailScale * 3.2,
      3,
    );
    const micro = this.detailNoise.fbm(
      dir.x * p.detailScale * 8.6,
      dir.y * p.detailScale * 8.6,
      dir.z * p.detailScale * 8.6,
      2,
    );

    const surfaceWeight = 0.3 + land;
    let elevation =
      p.continentalRelief * interior -
      p.oceanFloorDepth * basin +
      p.mountainRelief * rangeMask * (0.55 + 0.45 * detail) +
      p.mountainRelief * 0.3 * subRidgeMask * (0.7 + 0.3 * detail) +
      p.detailMetres * detail * (0.2 + land) +
      p.detailMetres * 1.7 * rough * surfaceWeight +
      p.detailMetres * 0.6 * micro * surfaceWeight;

    // The basin term already leaves a shelf at the coastline because `basin`
    // reaches zero exactly at the shoreline; `shelfSlope` widens the band where
    // land is near zero, which is what makes shallows and breakers possible.
    if (outTangentSlope) {
      outTangentSlope[0] = 0;
      outTangentSlope[1] = 0;
    }
    return elevation;
  }

  /**
   * Elevation including authored ops, in metres relative to sea level.
   *
   * `localOps` is the tile's prefiltered op list (the fast path, no allocation).
   * `maxOrder` evaluates the journal only up to a given op, which is how the
   * smooth brush samples the surface it is about to modify without recursing.
   */
  elevationAt(dir, localOps = null, maxOrder = Infinity) {
    let elevation = this.baseElevation(dir);
    const ops = localOps ?? this.ops;
    if (ops.length === 0) return elevation;

    for (let i = 0; i < ops.length; i++) {
      const op = ops[i];
      if (op.order > maxOrder) continue;
      const gap = angleBetween(dir, op.dir) * this.params.radius;
      if (gap >= op.radius) continue;
      elevation = applyBrushOp(op, elevation, gap, dir, this, maxOrder);
    }
    return elevation;
  }

  /**
   * Tangential gradient of the elevation at `dir`, in metres per metre.
   * Used for mesh normals and for slope-aware shading; sampled with two extra
   * height evaluations on the local tangent plane.
   */
  elevationGradient(dir, stepMetres = 24, localOps = null) {
    const [t1, t2] = tangentBasis(dir);
    const eps = stepMetres / this.params.radius;
    const sample = (dx, dy) => {
      const moved = normalize({
        x: dir.x + t1.x * dx * eps + t2.x * dy * eps,
        y: dir.y + t1.y * dx * eps + t2.y * dy * eps,
        z: dir.z + t1.z * dx * eps + t2.z * dy * eps,
      });
      return this.elevationAt(moved, localOps);
    };
    const e0 = sample(0, 0);
    const g1 = (sample(1, 0) - e0) / stepMetres;
    const g2 = (sample(0, 1) - e0) / stepMetres;
    return {
      g1,
      g2,
      t1,
      t2,
      slope: Math.hypot(g1, g2),
    };
  }

  /** Surface radius (metres from the planet centre) along a direction. */
  surfaceRadius(dir, localOps = null) {
    return this.params.radius + this.elevationAt(dir, localOps);
  }

  /**
   * Signed field value at a point: negative inside the planet, positive in the
   * air, zero on the surface. This is the function the mesher samples.
   */
  distanceAt(point, localOps = null) {
    const r = Math.hypot(point.x, point.y, point.z);
    if (r < 1e-9) return -this.params.radius;
    const dir = { x: point.x / r, y: point.y / r, z: point.z / r };
    const local = localOps ?? this.opsNear(dir);
    return r - (this.params.radius + this.elevationAt(dir, local));
  }

  /**
   * Outward surface normal at a direction, from the analytic gradient:
   * grad f = r_hat - (g1*t1 + g2*t2)/r, with g the tangential height gradient.
   */
  normalAt(dir, localOps = null, stepMetres = 24) {
    const grad = this.elevationGradient(dir, stepMetres, localOps);
    const r = this.params.radius + this.elevationAt(dir, localOps);
    const inv = 1 / Math.max(1, r);
    return normalize({
      x: dir.x - (grad.g1 * grad.t1.x + grad.g2 * grad.t2.x) * inv,
      y: dir.y - (grad.g1 * grad.t1.y + grad.g2 * grad.t2.y) * inv,
      z: dir.z - (grad.g1 * grad.t1.z + grad.g2 * grad.t2.z) * inv,
    });
  }

  /** Ops that can affect a direction, from the coarse cube-face index. */
  opsNear(dir, angularRadius = 0) {
    if (this.ops.length === 0) return this.ops;
    return this.index.query(dir, angularRadius, this.params.radius);
  }

  addOp(op) {
    const stored = { ...op, id: this.nextOpId++, order: this.ops.length };
    this.ops.push(stored);
    this.index.insert(stored);
    return stored;
  }

  removeOp(id) {
    const before = this.ops.length;
    this.ops = this.ops.filter((op) => op.id !== id);
    if (this.ops.length !== before) {
      this.ops.forEach((op, i) => {
        op.order = i;
      });
      this.index.rebuild(this.ops);
      return true;
    }
    return false;
  }

  undo() {
    const op = this.ops.pop();
    if (!op) return null;
    this.index.rebuild(this.ops);
    return op;
  }

  clearEdits() {
    this.ops = [];
    this.index.clear();
  }

  /** Journal export: the full authored state, small enough to embed in a project file. */
  serializeEdits() {
    return this.ops.map((op) => ({
      kind: op.kind,
      dir: [+op.dir.x.toFixed(7), +op.dir.y.toFixed(7), +op.dir.z.toFixed(7)],
      radius: Math.round(op.radius * 100) / 100,
      strength: Math.round(op.strength * 1000) / 1000,
      falloff: Math.round(op.falloff * 1000) / 1000,
      seed: op.seed ?? 0,
      extra: op.extra ?? null,
    }));
  }

  loadEdits(list) {
    this.clearEdits();
    for (const entry of list ?? []) {
      const dir = normalize({ x: entry.dir[0], y: entry.dir[1], z: entry.dir[2] });
      this.addOp({
        kind: entry.kind,
        dir,
        radius: entry.radius,
        strength: entry.strength,
        falloff: entry.falloff,
        seed: entry.seed ?? 0,
        extra: entry.extra ?? null,
      });
    }
    return this.ops.length;
  }

  /**
   * Coarse elevation envelope over a spherical cap: min/max/mean, from a
   * fibonacci sample. The tile builder uses it to size the narrow band.
   */
  elevationEnvelope(centerDir, angularRadius, samples = 49, localOps = null) {
    const [t1, t2] = tangentBasis(centerDir);
    const ops = localOps ?? this.opsNear(centerDir, angularRadius);
    let min = Infinity;
    let max = -Infinity;
    let sum = 0;
    for (let i = 0; i < samples; i++) {
      const k = (i + 0.5) / samples;
      // Spiral over the cap so the envelope is unbiased for round caps.
      const radiusFactor = Math.sqrt(k);
      const theta = i * 2.399963229728653;
      const displacement = angularRadius * radiusFactor;
      const dir = normalize({
        x: centerDir.x + (t1.x * Math.cos(theta) + t2.x * Math.sin(theta)) * displacement,
        y: centerDir.y + (t1.y * Math.cos(theta) + t2.y * Math.sin(theta)) * displacement,
        z: centerDir.z + (t1.z * Math.cos(theta) + t2.z * Math.sin(theta)) * displacement,
      });
      const e = this.elevationAt(dir, ops);
      if (e < min) min = e;
      if (e > max) max = e;
      sum += e;
    }
    return { min, max, mean: sum / samples };
  }
}

/**
 * Brush application. Kept in this module so `PlanetField.elevationAt` has one
 * implementation of op semantics; `brushes.js` owns authoring, falloff and the
 * journal records.
 *
 * Deliberately free of allocation: called per column, per op, thousands of times
 * per tile.
 */
export function applyBrushOp(op, elevation, gapMetres, dir, field, maxOrder = Infinity) {
  const t = gapMetres / op.radius; // 0 at centre, 1 at the rim
  const w = brushFalloff(t, op.falloff);
  if (w <= 0) return elevation;

  switch (op.kind) {
    case 'raise':
      return elevation + op.strength * w;
    case 'lower':
      return elevation - op.strength * w;
    case 'flatten': {
      const target = op.extra?.target ?? 0;
      return elevation + (target - elevation) * Math.min(1, op.strength * w);
    }
    case 'smooth': {
      // Smooth needs surrounding heights. Sampling the prefix chain (ops before
      // this one) keeps the op idempotent and avoids feeding on itself.
      const [t1, t2] = tangentBasis(dir);
      const reach = op.radius * 0.5;
      const eps1 = reach / field.params.radius;
      let sum = 0;
      for (let k = 0; k < 4; k++) {
        const theta = (k * Math.PI) / 2 + Math.PI / 4;
        const moved = normalize({
          x: dir.x + (t1.x * Math.cos(theta) + t2.x * Math.sin(theta)) * eps1,
          y: dir.y + (t1.y * Math.cos(theta) + t2.y * Math.sin(theta)) * eps1,
          z: dir.z + (t1.z * Math.cos(theta) + t2.z * Math.sin(theta)) * eps1,
        });
        sum += field.elevationAt(moved, field.ops, op.order - 1);
      }
      const average = sum / 4;
      return elevation + (average - elevation) * Math.min(1, op.strength * w);
    }
    case 'ridge': {
      // Elongated crest along the op's tangent axis: two offset lobes plus a
      // sharpened centre line, which is what a mountain brush needs to read as a
      // ridge rather than a dome.
      const axis = op.extra?.axis ?? { x: 1, y: 0, z: 0 };
      const along = dot(dir, axis);
      const lateral = Math.sqrt(Math.max(0, 1 - along * along));
      const crest = Math.exp(-((lateral / (0.55 + 0.45 * op.extra?.sharpness)) ** 2) * 3.2);
      const profile = Math.pow(w, 1.35) * crest;
      return elevation + op.strength * profile;
    }
    case 'island': {
      // A seeded island budget inside one op record: a broad shield, a couple of
      // ridges and a shoreline taper. One journal entry, many features.
      const seed = op.seed || 1;
      const local = gapMetres / op.radius;
      const shield = Math.pow(Math.max(0, 1 - local * local), 1.2);
      const n1 = Math.sin(local * 11.7 + seed * 0.017) * 0.5 + 0.5;
      const n2 = Math.sin(local * 27.3 - seed * 0.031) * 0.5 + 0.5;
      const lumpy = 0.72 + 0.18 * n1 + 0.10 * n2;
      return elevation + op.strength * shield * lumpy * w;
    }
    case 'gulf': {
      // Inverse island: a bay/estuary carved into an existing coast.
      const local = gapMetres / op.radius;
      const bowl = Math.pow(Math.max(0, 1 - local * local), 0.85);
      return elevation - op.strength * bowl * w;
    }
    default:
      return elevation;
  }
}

/**
 * Falloff curves. `falloff` is 0 (soft, wide plateau) .. 1 (hard, steep rim);
 * every curve reaches zero at t = 1 so a brush footprint is exactly its radius.
 */
export function brushFalloff(t, falloff = 0.5) {
  if (t >= 1) return 0;
  if (t <= 0) return 1;
  const hardness = clamp(falloff, 0, 1);
  const smooth = 1 - t * t * (3 - 2 * t);
  const flat = Math.pow(1 - t * t, 2);
  const sharp = Math.pow(1 - t, 1 + hardness * 3);
  return clamp(smooth * (1 - hardness) + flat * 0.55 * hardness + sharp * 0.45 * hardness, 0, 1);
}
