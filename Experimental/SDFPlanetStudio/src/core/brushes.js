/**
 * Brush tool definitions and the authored-operation journal.
 *
 * A brush stroke is not baked into any raster. It appends a compact op record
 * (centre direction, footprint radius in metres, strength, falloff curve, seed)
 * that `PlanetField` replays while evaluating. Undo is a pop; saving a project is
 * the op list. That is what keeps a continent-sized authored world small on disk
 * and cheap to stream.
 *
 * Stroke sampling: dragging emits one op every `spacing` metres of surface travel
 * so a stroke reads as a continuous ridge instead of a row of stamped craters.
 */

import { angleBetween, normalize } from './vec3.js';

export const BRUSH_TOOLS = Object.freeze([
  Object.freeze({
    id: 'orbit',
    label: 'Orbit',
    glyph: 'orbit',
    hint: 'Orbit (drag), pan (middle/alt-drag), fly (right-drag + WASD)',
    kind: null,
  }),
  Object.freeze({
    id: 'raise',
    label: 'Landmass',
    glyph: 'up',
    hint: 'Push the surface outward: continents, shields, headlands',
    kind: 'raise',
    defaultRadius: 4200,
    defaultStrength: 320,
    strengthUnit: 'm',
  }),
  Object.freeze({
    id: 'lower',
    label: 'Excavate',
    glyph: 'down',
    hint: 'Pull the surface inward: basins, valleys, shelves',
    kind: 'lower',
    defaultRadius: 3800,
    defaultStrength: 260,
    strengthUnit: 'm',
  }),
  Object.freeze({
    id: 'smooth',
    label: 'Smooth',
    glyph: 'wave',
    hint: 'Local relaxation; repeated passes soften noise and strokes',
    kind: 'smooth',
    defaultRadius: 2600,
    defaultStrength: 0.55,
    strengthUnit: 'blend',
  }),
  Object.freeze({
    id: 'flatten',
    label: 'Plateau',
    glyph: 'plateau',
    hint: 'Flatten toward the elevation sampled at stroke start',
    kind: 'flatten',
    defaultRadius: 3400,
    defaultStrength: 0.6,
    strengthUnit: 'blend',
  }),
  Object.freeze({
    id: 'ridge',
    label: 'Ridge',
    glyph: 'ridge',
    hint: 'Elongated mountain crest along the drag direction',
    kind: 'ridge',
    defaultRadius: 5200,
    defaultStrength: 900,
    strengthUnit: 'm',
  }),
  Object.freeze({
    id: 'island',
    label: 'Island',
    glyph: 'island',
    hint: 'Stamp a seeded island budget: shield plus ridges and a shoreline taper',
    kind: 'island',
    defaultRadius: 9000,
    defaultStrength: 620,
    strengthUnit: 'm',
  }),
  Object.freeze({
    id: 'gulf',
    label: 'Gulf',
    glyph: 'gulf',
    hint: 'Carve a bay, gulf or estuary into an existing coast',
    kind: 'gulf',
    defaultRadius: 11000,
    defaultStrength: 720,
    strengthUnit: 'm',
  }),
]);

export const BRUSH_TOOL_BY_ID = new Map(BRUSH_TOOLS.map((tool) => [tool.id, tool]));

export function clampBrushRadius(radius, planetRadius) {
  const min = Math.max(12, planetRadius * 0.00002);
  const max = planetRadius * 0.45;
  const value = Number(radius);
  if (!Number.isFinite(value)) return min * 8;
  return Math.min(max, Math.max(min, value));
}

/**
 * Build a journal op from a tool pick and cursor state.
 *
 * `field` is only read for tools that need a snapshot (flatten) or a drag axis
 * (ridge); every other tool is a pure function of its parameters.
 */
export function createBrushOp({
  toolId,
  dir,
  radius,
  strength,
  falloff = 0.5,
  seed = 0,
  field = null,
  dragAxis = null,
  target = null,
  pressure = 1,
}) {
  const tool = BRUSH_TOOL_BY_ID.get(toolId);
  if (!tool || !tool.kind) throw new Error(`not a sculpt tool: ${toolId}`);

  const unit = normalize(dir);
  const planetRadius = field ? field.radius : 6371000;
  const footprint = clampBrushRadius(radius ?? tool.defaultRadius, planetRadius);
  const amount = Number.isFinite(strength) ? strength : tool.defaultStrength;
  const scale = Math.min(3, Math.max(0, pressure));

  const op = {
    kind: tool.kind,
    dir: unit,
    radius: footprint,
    strength:
      tool.strengthUnit === 'blend'
        ? Math.min(1, Math.max(0, amount * scale))
        : Math.max(0, amount * scale),
    falloff: Math.min(1, Math.max(0, falloff)),
    seed: seed >>> 0,
    extra: null,
  };

  if (tool.kind === 'flatten') {
    const resolved =
      target !== null
        ? target
        : field
          ? field.elevationAt(unit, field.opsNear(unit, footprint / planetRadius))
          : 0;
    op.extra = { target: resolved };
  }

  if (tool.kind === 'ridge') {
    op.extra = { axis: dragAxis ?? tangentOf(unit), sharpness: 0.6 };
  }

  return op;
}

function tangentOf(dir) {
  // Deterministic tangent axis for a ridge stamped without a drag direction.
  const axis = Math.abs(dir.y) < 0.9 ? { x: 0, y: 1, z: 0 } : { x: 1, y: 0, z: 0 };
  const cx = axis.y * dir.z - axis.z * dir.y;
  const cy = axis.z * dir.x - axis.x * dir.z;
  const cz = axis.x * dir.y - axis.y * dir.x;
  const len = Math.hypot(cx, cy, cz) || 1;
  return { x: cx / len, y: cy / len, z: cz / len };
}

/**
 * Stroke accumulator. Feed it the cursor direction each frame; it decides when a
 * new op is due, keeps a tangent axis for ridge strokes and enforces a cap so a
 * long drag cannot fill the journal with thousands of records.
 */
export class BrushStroke {
  constructor({ toolId, radius, strength, falloff = 0.5, seed = 0, maxOps = 384, spacing = 0.28 }) {
    this.toolId = toolId;
    this.radius = radius;
    this.strength = strength;
    this.falloff = falloff;
    this.seed = seed >>> 0;
    this.maxOps = maxOps;
    this.spacingRatio = spacing;
    this.ops = [];
    this.lastDir = null;
    this.previousDir = null;
    this.startedAt = 0;
    this.spacingMetres = radius * spacing;
  }

  /** @returns {object[]} newly created ops (usually zero or one per call). */
  push(dir, field, pressure = 1) {
    const unit = normalize(dir);
    const planetRadius = field.radius;
    const travelled = this.lastDir
      ? angleBetween(unit, this.lastDir) * planetRadius
      : Infinity;

    if (travelled < this.spacingMetres && this.ops.length > 0) return [];

    const dragAxis =
      this.lastDir !== null && travelled > 1e-3
        ? normalize({
            x: unit.x - this.lastDir.x,
            y: unit.y - this.lastDir.y,
            z: unit.z - this.lastDir.z,
          })
        : null;

    const op = createBrushOp({
      toolId: this.toolId,
      dir: unit,
      radius: this.radius,
      strength: this.strength,
      falloff: this.falloff,
      seed: this.seed + this.ops.length,
      field,
      dragAxis,
      pressure,
    });

    if (this.ops.length >= this.maxOps) return [];
    field.addOp(op);
    this.ops.push(op);
    this.previousDir = this.lastDir;
    this.lastDir = unit;
    this.startedAt += 1;
    return [op];
  }

  /** Directions touched by this stroke, for dirty-tile invalidation. */
  footprintDirs() {
    return this.ops.map((op) => ({ dir: op.dir, radius: op.radius }));
  }
}
