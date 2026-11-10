// Road Editor — parametric cross-sections.
// Produces the lateral strip layout + marking plan for one road type.
// x runs left → right across travel direction; y is height above formation.

import { roadTypeById, sidewalkWidth } from './roadTypes.js';

export const SURFACE_Y = {
  carriageway: 0.02, curb: 0.14, sidewalk: 0.13, median: 0.1, shoulder: 0.0,
  rail: 0.05, walk: 0.12, verge: -0.02,
};

export function buildProfile(typeId) {
  const type = roadTypeById(typeId);
  const strips = [];
  const laneMarks = [];   // {x, style} boundaries between lanes
  const markings = { center: type.center, edge: type.edgeLine, lane: type.laneLine };
  let x = 0;
  // left sidewalk first (outside carriageway)
  const sw = sidewalkWidth(type.sidewalk);
  if (sw > 0 && type.sidewalk !== 'itself') {
    strips.push({ kind: 'walk', x0: x, x1: x + sw, y: SURFACE_Y.sidewalk, side: 'L' });
    strips.push({ kind: 'curb', x0: x + sw, x1: x + sw + 0.3, y: SURFACE_Y.curb, side: 'L' });
    x += sw + 0.3;
  }
  const carriageStart = x;
  type.lanes.forEach((lane, i) => {
    const x0 = x, x1 = x + lane.width;
    strips.push({ kind: lane.kind, x0, x1, y: laneY(lane.kind), lane: i, dir: lane.dir, opts: lane });
    if (i > 0) laneMarks.push({ x: x0, style: laneBoundaryStyle(type, i) });
    x = x1;
  });
  const carriageEnd = x;
  if (sw > 0 && type.sidewalk !== 'itself') {
    strips.push({ kind: 'curb', x0: x, x1: x + 0.3, y: SURFACE_Y.curb, side: 'R' });
    x += 0.3;
    strips.push({ kind: 'walk', x0: x, x1: x + sw, y: SURFACE_Y.sidewalk, side: 'R' });
    x += sw;
  }
  const total = x;
  // recenter so x=0 is the road centerline
  for (const s of strips) { s.x0 -= total / 2; s.x1 -= total / 2; }
  for (const m of laneMarks) m.x -= total / 2;
  return {
    type, strips, laneMarks, markings,
    totalWidth: total,
    carriage: { x0: carriageStart - total / 2, x1: carriageEnd - total / 2 },
    centerX: centerOffset(type),
  };
}

function laneY(kind) {
  switch (kind) {
    case 'median': return SURFACE_Y.median;
    case 'walk': return SURFACE_Y.walk;
    case 'verge': return SURFACE_Y.verge;
    case 'shoulder': return SURFACE_Y.shoulder;
    default: return SURFACE_Y.carriageway;
  }
}

function laneBoundaryStyle(type, i) {
  const a = type.lanes[i - 1], b = type.lanes[i];
  if (a.dir !== 0 && b.dir !== 0 && a.dir !== b.dir) {
    if (type.center === 'double-yellow') return 'double-yellow';
    if (type.center === 'dashed-yellow') return 'dashed-yellow';
    if (type.center === 'solid-white') return 'solid-white';
    if (type.center === 'turn-lane') return 'turn-lane';
    return type.laneLine || 'dashed-white';
  }
  if (a.kind !== b.kind) {
    if (a.kind === 'bike' || b.kind === 'bike') return 'solid-white';
    if (a.kind === 'bus' || b.kind === 'bus') return 'bus-line';
    if (a.kind === 'parking' || b.kind === 'parking') return 'solid-white';
    return 'solid-white';
  }
  return type.laneLine || 'dashed-white';
}

function centerOffset(type) {
  // lateral position of the directional split (for center markings)
  let x = 0;
  const sw = sidewalkWidth(type.sidewalk);
  if (sw > 0 && type.sidewalk !== 'itself') x += sw + 0.3;
  for (const lane of type.lanes) {
    if (lane.dir < 0) break;
    x += lane.width;
  }
  let total = x;
  for (const lane of type.lanes) total = 0; // recompute below
  total = type.lanes.reduce((a, l) => a + l.width, 0) + (sw > 0 && type.sidewalk !== 'itself' ? (sw + 0.3) * 2 : 0);
  return x - total / 2;
}

export function driveLaneCenters(profile) {
  const centers = [];
  profile.strips.forEach((s) => {
    if (s.kind === 'drive' || s.kind === 'bus' || s.kind === 'tram') {
      centers.push({ x: (s.x0 + s.x1) / 2, width: s.x1 - s.x0, dir: s.dir ?? 1, kind: s.kind });
    }
  });
  return centers;
}
