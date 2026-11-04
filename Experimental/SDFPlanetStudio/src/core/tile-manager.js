/**
 * Tile manager: decides which patches exist, which LOD they need, what to build
 * next, and what to throw away.
 *
 * Design constraints this encodes
 * -------------------------------
 *   * A planet cannot be resident. Selection is a quadtree walk from the six cube
 *     faces, refined by camera distance and bounded by a hard tile count.
 *   * The far side of the planet is invisible. A horizon test (the camera sees
 *     only up to the angle where the surface curves away) removes roughly half of
 *     the six faces immediately, before any per-tile work.
 *   * Rebuilding must never block a frame. Builds are queued, dispatched to a
 *     bounded number of jobs, and completed meshes are uploaded a few per frame.
 *   * Sculpting must feel instant. Edits mark the overlapping resident tiles
 *     dirty and are rebuilt at the front of the queue.
 *   * Errors and stale results must be harmless. Every job carries the tile
 *     revision it was built for; a result for a stale revision is dropped.
 *
 * The manager owns no rendering, no workers and no three.js: it takes a `build`
 * function and emits results. Node tests drive it synchronously; the viewport
 * drives it with workers and uploads the buffers it produces.
 */

import {
  createTile,
  directionToFace,
  faceDirection,
  lodForDistance,
  tileAngularRadius,
  tileCenterDirection,
  tileKey,
  tileWorldSize,
} from './cube-sphere.js';
import { angleBetween, dot } from './vec3.js';

export const DEFAULT_MANAGER_OPTIONS = Object.freeze({
  cellsPerTile: 48,
  maxTiles: 420,
  maxJobsInFlight: 4,
  frontierRadius: 6,      // how many patch widths around the camera stay resident
  lodBias: 1.0,
  minCellSize: 1.5,       // [m] floor on cell size, so detail stops somewhere
  horizonCulling: true,
  maxLod: 14,
});

export class TileManager {
  /**
   * @param {object} config
   * @param {object} config.field       PlanetField (radius, elevation queries)
   * @param {(tile: object, revision: number) => Promise<object>} config.build
   *        resolves to {positions, normals, elevation, slope, indices, stats}
   * @param {(tile: object, mesh: object) => void} [config.onTileReady]
   * @param {(tile: object) => void} [config.onTileDropped]
   */
  constructor(config) {
    this.field = config.field;
    this.build = config.build;
    this.onTileReady = config.onTileReady ?? (() => {});
    this.onTileDropped = config.onTileDropped ?? (() => {});
    this.options = { ...DEFAULT_MANAGER_OPTIONS, ...(config.options ?? {}) };

    this.resident = new Map(); // key -> { tile, mesh, revision, stats, bytes }
    this.inFlight = new Map(); // key -> { tile, revision, startedAt }
    this.queue = []; // { tile, revision, priority }
    this.revision = 0;
    this.lastSelection = [];
    this.stats = {
      frames: 0,
      dispatches: 0,
      uploads: 0,
      evictions: 0,
      staleDrops: 0,
      rejected: 0,
      residentBytes: 0,
      peakResident: 0,
      lastSelected: 0,
      lastBuildMs: 0,
    };
  }

  get residentCount() {
    return this.resident.size;
  }

  /** Bump the revision; every in-flight result from an older one becomes stale. */
  invalidate() {
    this.revision++;
    return this.revision;
  }

  /**
   * Recompute the desired tile set for the camera and refresh the queue.
   *
   * Selection is a priority-split quadtree: start from the six faces, then keep
   * splitting the leaf with the largest remaining LOD deficit until the tile
   * budget is spent. Splitting the *worst* tile rather than the first child of a
   * depth-first walk is what makes the budget land under the camera instead of in
   * one arbitrary corner of a face.
   *
   * @param {{x,y,z}} position  camera world position
   * @param {{x,y,z}} forward   camera forward direction (reserved for future
   *                            view-dependent refinement; the horizon test is
   *                            orientation independent)
   */
  update(position, forward, options = {}) {
    this.stats.frames++;
    const radius = this.field.radius;
    const camDistance = Math.hypot(position.x, position.y, position.z);
    const camHeight = Math.max(0.5, camDistance - radius);
    const maxTiles = Math.max(6, options.maxTiles ?? this.options.maxTiles);

    // Horizon: a camera at height h sees up to acos(R / (R + h)) around its
    // sub-point. Everything past that is under the curve and cannot be seen.
    const horizonAngle = Math.acos(Math.min(1, radius / (radius + camHeight)));
    const camDir = { x: position.x / camDistance, y: position.y / camDistance, z: position.z / camDistance };
    void forward;

    const evaluate = (tile) => {
      const centreDir = tileCenterDirection(tile);
      const angularRadius = tileAngularRadius(tile);
      const separation = angleBetween(centreDir, camDir);
      const distance = this.#distanceToTile(position, centreDir, angularRadius, radius);
      const desiredLod = lodForDistance(
        distance,
        radius,
        Math.PI * 0.5,
        this.options.cellsPerTile,
        this.options.minCellSize,
        this.options.lodBias,
        this.options.maxLod,
      );
      return {
        tile,
        centreDir,
        angularRadius,
        separation,
        distance,
        desiredLod,
        gap: desiredLod - tile.lod,
        visible: this.options.horizonCulling
          ? separation - angularRadius <= horizonAngle + 0.02
          : true,
      };
    };

    let leaves = [];
    for (let face = 0; face < 6; face++) {
      const entry = evaluate(createTile(face, 0, 0, 0));
      if (entry.visible) leaves.push(entry);
    }

    // Each split replaces one leaf with four, so only split while the +3 growth
    // keeps the count at or under the budget.
    while (leaves.length + 3 <= maxTiles) {
      let bestIndex = -1;
      let bestGap = 0;
      for (let i = 0; i < leaves.length; i++) {
        const gap = leaves[i].gap;
        if (gap > bestGap || (gap === bestGap && gap > 0 && bestIndex >= 0 && leaves[i].distance < leaves[bestIndex].distance)) {
          bestGap = gap;
          bestIndex = i;
        }
      }
      if (bestIndex < 0) break;

      const parent = leaves[bestIndex];
      if (parent.tile.lod >= this.options.maxLod) {
        parent.gap = 0;
        continue;
      }

      const n = 1 << (parent.tile.lod + 1);
      const cx = parent.tile.ix * 2;
      const cy = parent.tile.iy * 2;
      const children = [];
      for (let dy = 0; dy < 2; dy++) {
        for (let dx = 0; dx < 2; dx++) {
          const child = evaluate(createTile(parent.tile.face, parent.tile.lod + 1, cx + dx, cy + dy));
          if (child.visible) children.push(child);
        }
      }

      if (children.length === 0) {
        // The whole patch curved out of sight; stop considering it.
        leaves[bestIndex] = { ...parent, gap: 0, visible: false };
        continue;
      }

      leaves.splice(bestIndex, 1, ...children);
    }

    leaves = leaves.filter((leaf) => leaf.visible);
    this.lastSelection = leaves;
    this.stats.lastSelected = leaves.length;

    const wanted = new Set(leaves.map((entry) => entry.tile.key));

    // Evict residents that left the selection.
    for (const [key, record] of this.resident) {
      if (!wanted.has(key)) {
        this.resident.delete(key);
        this.stats.residentBytes -= record.bytes ?? 0;
        this.stats.evictions++;
        this.onTileDropped(record.tile);
      }
    }

    // Queue anything selected but neither resident nor building.
    const queued = new Set(this.queue.map((job) => job.tile.key));
    for (const entry of leaves) {
      const key = entry.tile.key;
      if (this.resident.has(key) || this.inFlight.has(key) || queued.has(key)) continue;
      this.queue.push({ tile: entry.tile, revision: this.revision, priority: entry.distance });
    }

    // Nearest first: what the camera is looking at appears before the horizon.
    this.queue.sort((a, b) => a.priority - b.priority);
    if (this.queue.length > maxTiles * 2) this.queue.length = maxTiles * 2;
    return leaves;
  }

  #distanceToTile(position, centreDir, angularRadius, radius) {
    const camDistance = Math.hypot(position.x, position.y, position.z);
    const cos = Math.min(1, Math.max(-1, dot(position, centreDir) / Math.max(1e-9, camDistance)));
    const angle = Math.acos(cos);
    const surfaceDistance = Math.max(0, angle - angularRadius) * radius;
    const altitude = Math.abs(camDistance - radius);
    return Math.hypot(surfaceDistance, altitude);
  }

  /**
   * Dispatch queued builds, up to the in-flight cap. `build` is injected, so the
   * viewport can hand this to workers while tests resolve immediately.
   */
  pump(maxJobs = this.options.maxJobsInFlight) {
    const started = [];
    const cap = Math.max(1, Math.min(maxJobs, this.options.maxJobsInFlight));
    while (this.inFlight.size < cap && this.queue.length > 0) {
      const job = this.queue.shift();
      if (this.resident.has(job.tile.key) || this.inFlight.has(job.tile.key)) continue;

      this.inFlight.set(job.tile.key, { tile: job.tile, revision: job.revision, startedAt: nowMs() });
      this.stats.dispatches++;

      const promise = this.build(job.tile, job.revision)
        .then((mesh) => this.#complete(job.tile, job.revision, mesh))
        .catch((error) => {
          this.inFlight.delete(job.tile.key);
          this.stats.rejected++;
          this.stats.lastError = String(error?.message ?? error);
          return null;
        });

      started.push(promise);
    }
    return started;
  }

  #complete(tile, revision, mesh) {
    this.inFlight.delete(tile.key);
    if (!mesh || mesh.vertexCount === 0) {
      this.stats.rejected++;
      return null;
    }
    // A result is only worth uploading if its patch is still part of the current
    // selection. This covers both "the camera flew away" and "the world changed
    // while the job was running"; either way the buffer is stale.
    const wanted = this.selectionKeys();
    if (!wanted.has(tile.key)) {
      this.stats.staleDrops++;
      return null;
    }

    const bytes = meshBytes(mesh);
    const existing = this.resident.get(tile.key);
    if (existing) this.stats.residentBytes -= existing.bytes ?? 0;

    const record = { tile, mesh, revision, bytes, stats: mesh.stats ?? null };
    this.resident.set(tile.key, record);
    this.stats.residentBytes += bytes;
    this.stats.peakResident = Math.max(this.stats.peakResident, this.stats.residentBytes);
    this.stats.uploads++;
    this.stats.lastBuildMs = mesh.stats?.buildMs ?? 0;
    this.onTileReady(tile, mesh);
    return record;
  }

  /**
   * Sculpt support: exactly the resident tiles whose patch the brush can touch.
   * Deletes them and requeues them at the highest priority.
   */
  markDirtyNear(dir, footprintMetres) {
    const radius = this.field.radius;
    const footprintAngle = footprintMetres / radius;
    const touched = [];
    for (const [key, record] of this.resident) {
      const centre = tileCenterDirection(record.tile);
      const separation = angleBetween(centre, dir);
      if (separation <= footprintAngle + tileAngularRadius(record.tile) + 1e-9) {
        this.resident.delete(key);
        this.stats.residentBytes -= record.bytes ?? 0;
        this.onTileDropped(record.tile);
        touched.push(record.tile);
      }
    }

    // Also drop queued jobs for those tiles so they rebuild against the new field.
    const touchedKeys = new Set(touched.map((tile) => tile.key));
    this.queue = this.queue.filter((job) => !touchedKeys.has(job.tile.key));

    for (const tile of touched) {
      this.queue.unshift({ tile, revision: this.revision, priority: -1e9 });
    }
    return touched;
  }

  selectionKeys() {
    if (!this.selectionCache || this.selectionCacheFrame !== this.stats.frames) {
      this.selectionCache = new Set(this.lastSelection.map((entry) => entry.tile.key));
      this.selectionCacheFrame = this.stats.frames;
    }
    return this.selectionCache;
  }

  /**
   * Rebuild everything resident without a visual gap (undo, journal reload).
   * Unlike `clear()`, the tiles are requeued at the front instead of forgotten.
   */
  markAllDirty() {
    const tiles = [];
    for (const record of this.resident.values()) tiles.push(record.tile);
    for (const tile of tiles) {
      const record = this.resident.get(tile.key);
      if (record) this.onTileDropped(record.tile);
      this.resident.delete(tile.key);
      this.stats.residentBytes -= record?.bytes ?? 0;
    }
    this.queue = this.queue.filter((job) => !tiles.some((tile) => tile.key === job.tile.key));
    for (const tile of tiles) this.queue.unshift({ tile, revision: this.revision, priority: -1e9 });
    return tiles.length;
  }

  /** Release everything (teleport, world reset). */
  clear() {
    for (const record of this.resident.values()) this.onTileDropped(record.tile);
    this.resident.clear();
    this.queue = [];
    this.inFlight.clear();
    this.stats.residentBytes = 0;
  }

  get summary() {
    return {
      selected: this.stats.lastSelected,
      resident: this.resident.size,
      inFlight: this.inFlight.size,
      queued: this.queue.length,
      bytes: this.stats.residentBytes,
      megabytes: +(this.stats.residentBytes / (1024 * 1024)).toFixed(2),
      peakMegabytes: +(this.stats.peakResident / (1024 * 1024)).toFixed(2),
      dispatches: this.stats.dispatches,
      evictions: this.stats.evictions,
      rejected: this.stats.rejected,
      staleDrops: this.stats.staleDrops,
      lastBuildMs: +this.stats.lastBuildMs.toFixed(1),
      revision: this.revision,
    };
  }
}

/** Bytes a mesh occupies in RAM/VRAM: three attributes plus indices. */
export function meshBytes(mesh) {
  const vertices = mesh.vertexCount ?? (mesh.positions?.length ?? 0) / 3;
  const indices = mesh.indices?.length ?? 0;
  return vertices * 4 * 4 + indices * 4; // position, normal, elevation, slope + indices
}

function normalizePosition(p) {
  const len = Math.hypot(p.x, p.y, p.z) || 1;
  return { x: p.x / len, y: p.y / len, z: p.z / len };
}

/** Which tile a direction belongs to at a given LOD, exported for picking tests. */
export function tileForDirection(dir, lod) {
  const { face, s, t } = directionToFace(dir);
  const n = 1 << lod;
  const ix = Math.min(n - 1, Math.max(0, Math.floor(((s + 1) * 0.5) * n)));
  const iy = Math.min(n - 1, Math.max(0, Math.floor(((t + 1) * 0.5) * n)));
  return { tile: createTile(face, lod, ix, iy), s, t };
}

/** Patch key helper re-exported so callers do not import cube-sphere directly. */
export { tileKey, faceDirection };

function nowMs() {
  if (typeof performance !== 'undefined' && performance.now) return performance.now();
  return Number(process.hrtime.bigint() / 1000n) / 1000;
}
