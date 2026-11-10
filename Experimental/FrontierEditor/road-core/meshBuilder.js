// Road Editor — network mesh builder.
// Ribbons STOP at junction mouths; ONE junction polygon fills the gap.
// Shared mouth coordinates + millimetre weld ⇒ no overlaps, no z-fighting,
// no booleans — the partition is exact by construction.

import { buildProfile, driveLaneCenters } from './crossSection.js';
import { roadTypeById, variantFor, fullWidth } from './roadTypes.js';
import { buildBridge } from './bridges.js';

export function newBucket() {
  return { positions: [], normals: [], uvs: [], colors: [], index: [], count: 0 };
}
function bucketFor(buckets, key) {
  if (!buckets[key]) buckets[key] = newBucket();
  return buckets[key];
}

const SURFACE_BUCKET = {
  'asphalt': 'asphalt', 'asphalt-dark': 'asphalt', 'asphalt-worn': 'asphalt-worn',
  'asphalt-green': 'asphalt-green', 'concrete': 'concrete', 'concrete-dark': 'concrete-dark',
  'cobble': 'cobble', 'sett': 'sett', 'brick': 'brick', 'paver': 'paver',
  'gravel': 'gravel', 'gravel-fine': 'gravel', 'dirt': 'dirt', 'dirt-grass': 'dirt',
  'dirt-forest': 'dirt', 'red-bus': 'redbus', 'grass': 'verge',
};

export function buildNetworkMeshes(graph, opts = {}) {
  const buckets = {};
  const furniture = { lamps: [], bollards: [], signs: [], busStops: [], railGear: [], sleepers: [], posts: [] };
  const proxies = [];
  const stats = { edges: 0, junctions: 0, bridges: 0, verts: 0, tris: 0, markings: 0, furniture: 0 };

  // ---- edges ----
  for (const edge of graph.edges.values()) {
    if (edge.hidden) continue;
    const type = roadTypeById(edge.typeId);
    const profile = buildProfile(edge.typeId);
    const variant = variantFor(edge.id, edge.seed);
    const line = trimPolyline(edge.polyline, edge.trimStart, edge.length - edge.trimEnd);
    if (line.length < 2) continue;
    stats.edges++;
    buildRibbon(buckets, line, profile, type, variant, stats);
    buildMarkings(buckets, line, profile, type, variant, stats);
    collectFurniture(furniture, line, profile, type, edge, stats);
    if (edge.bridge) {
      stats.bridges++;
      const deckY = line[0].y + 0.02;
      buildBridge(edge.bridge.kind || 'beam', {
        p0: line[0], p1: line[line.length - 1],
        width: profile.totalWidth + 1.2, deckY,
      }, buckets, {
        pierY: (opts.groundYAt ? opts.groundYAt((line[0].x + line[line.length - 1].x) / 2, (line[0].z + line[line.length - 1].z) / 2) : 0),
        through: !!edge.bridge.auto,
      });
    }
    proxies.push(proxyForEdge(edge, line, profile.totalWidth));
  }

  // ---- junctions (ONE polygon each, welded to mouths) ----
  for (const node of graph.nodes.values()) {
    if (node.kind === 'junction') {
      if (node.roundabout) buildRoundabout(buckets, graph, node, stats);
      else buildJunction(buckets, graph, node, stats);
      stats.junctions++;
      proxies.push(proxyForNode(node));
    } else if (node.kind === 'endpoint' || node.kind === 'bend' || node.kind === 'through') {
      proxies.push(proxyForNode(node, 3));
    }
  }

  // ---- railways ----
  for (const rail of graph.rails || []) {
    buildRail(buckets, furniture, rail);
  }
  // level-crossing assemblies collected by the generator / editor
  for (const gz of graph.railGear || []) furniture.railGear.push({ ...gz });
  // ---- water ----
  for (const w of graph.water || []) {
    buildWater(buckets, w);
  }

  // ---- winding + weld pass ----
  // Ribbon/marking emitters share one consistent (down-facing) triangle order;
  // flip every non-bridge bucket once so carriageways face up for FrontSide
  // rendering. Bridge solids are closed and already outward — leave them.
  // Afterwards the 1mm weld drops degenerate + zero-area triangles.
  for (const key of Object.keys(buckets)) {
    const b = buckets[key];
    if (!key.startsWith('bridge-')) {
      for (let f = 0; f < b.index.length; f += 3) {
        const t = b.index[f + 1]; b.index[f + 1] = b.index[f + 2]; b.index[f + 2] = t;
      }
    }
    weldBucket(b);
    // Drop zero-area tris; heal the rare folded sliver (trim seam at a sharp
    // kink) by flipping it up. Bridge solids are closed boxes — never touch.
    const kept = [];
    const heal = !key.startsWith('bridge-');
    for (let f = 0; f < b.index.length; f += 3) {
      const A = b.index[f] * 3, C = b.index[f + 1] * 3, E = b.index[f + 2] * 3;
      const ux = b.positions[C] - b.positions[A], uy = b.positions[C + 1] - b.positions[A + 1], uz = b.positions[C + 2] - b.positions[A + 2];
      const vx = b.positions[E] - b.positions[A], vy = b.positions[E + 1] - b.positions[A + 1], vz = b.positions[E + 2] - b.positions[A + 2];
      const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
      if (nx * nx + ny * ny + nz * nz <= 1e-12) continue;
      if (heal && ny < 0) {
        kept.push(b.index[f], b.index[f + 2], b.index[f + 1]);
        stats.healedTris = (stats.healedTris || 0) + 1;
      } else {
        kept.push(b.index[f], b.index[f + 1], b.index[f + 2]);
      }
    }
    b.index = kept;
  }

  for (const key of Object.keys(buckets)) {
    const b = buckets[key];
    stats.verts += b.count;
    stats.tris += b.index.length / 3;
  }
  stats.furniture = furniture.lamps.length + furniture.bollards.length + furniture.signs.length
    + furniture.busStops.length + furniture.railGear.length + furniture.sleepers.length + furniture.posts.length;

  return { buckets, furniture, proxies, stats };
}

// ---------------------------------------------------------------- ribbons
export function trimPolyline(line, s0, s1) {
  if (line.length < 2) return line;
  s1 = Math.max(s1, s0 + 2);
  const out = [];
  const at = (s) => {
    s = Math.max(0, Math.min(line[line.length - 1].s, s));
    let i = 0;
    while (i < line.length - 2 && line[i + 1].s < s) i++;
    const a = line[i], b = line[i + 1];
    const t = b.s > a.s ? (s - a.s) / (b.s - a.s) : 0;
    return {
      x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, z: a.z + (b.z - a.z) * t,
      tx: a.tx + (b.tx - a.tx) * t, tz: a.tz + (b.tz - a.tz) * t, s,
    };
  };
  out.push(at(s0));
  for (const p of line) if (p.s > s0 + 0.01 && p.s < s1 - 0.01) out.push(p);
  out.push(at(s1));
  return out;
}

function stripBucketKey(type, strip) {
  if (strip.kind === 'curb') return 'curb';
  if (strip.kind === 'walk') return sidewalkBucket(type);
  if (strip.kind === 'median') {
    if (strip.opts?.wall) return 'concrete';
    if (strip.opts?.planted || strip.opts?.trees) return 'verge';
    return 'concrete';
  }
  if (strip.kind === 'verge') return 'verge';
  if (strip.kind === 'shoulder') return type.surface.startsWith('gravel') || type.surface.startsWith('dirt') ? SURFACE_BUCKET[type.surface] : 'asphalt-worn';
  if (strip.kind === 'rail') return 'railbed';
  if (strip.kind === 'bus') return 'redbus';
  if (strip.kind === 'bike' && type.surface === 'asphalt-green') return 'asphalt-green';
  if (strip.kind === 'parking') return 'asphalt-worn';
  return SURFACE_BUCKET[type.surface] || 'asphalt';
}

function sidewalkBucket(type) {
  if (type.sidewalk?.startsWith('paver') || type.sidewalk?.startsWith('brick') || type.sidewalk?.startsWith('sett')) return 'paver';
  return 'sidewalk';
}

function buildRibbon(buckets, line, profile, type, variant, stats) {
  // per-strip quad strips; UV.v = arc length for wear/dash continuity
  const tint = surfaceTint(type.surface, variant);
  for (const strip of profile.strips) {
    const B = bucketFor(buckets, stripBucketKey(type, strip));
    const base = B.count;
    const y = strip.y;
    for (let i = 0; i < line.length; i++) {
      const p = line[i];
      const nx = -p.tz, nz = p.tx;
      for (const off of [strip.x0, strip.x1]) {
        B.positions.push(p.x + nx * off, p.y + y, p.z + nz * off);
        B.normals.push(0, 1, 0);
        B.uvs.push((off - strip.x0) / Math.max(0.001, strip.x1 - strip.x0), p.s);
        B.colors.push(tint[0], tint[1], tint[2]);
      }
    }
    for (let i = 0; i < line.length - 1; i++) {
      const r0 = base + i * 2, r1 = base + (i + 1) * 2;
      B.index.push(r0, r1, r0 + 1, r0 + 1, r1, r1 + 1);
    }
    B.count += line.length * 2;
  }
  // grass crowns / ruts overlays for farm/dirt tracks
  if (type.lanes.some((l) => l.grassCrown)) {
    const B = bucketFor(buckets, 'verge');
    overlayStrip(B, line, -0.5, 0.5, 0.045, [0.42, 0.5, 0.28]);
  }
  if (type.lanes.some((l) => l.ruts)) {
    const B = bucketFor(buckets, 'dirt-dark');
    overlayStrip(B, line, -1.1, -0.5, 0.04, [0.3, 0.24, 0.18]);
    overlayStrip(B, line, 0.5, 1.1, 0.04, [0.3, 0.24, 0.18]);
  }
  // tram rails
  for (const strip of profile.strips) {
    if (strip.kind === 'tram') {
      const B = bucketFor(buckets, 'steel');
      const cx = (strip.x0 + strip.x1) / 2;
      overlayStrip(B, line, cx - 0.75, cx - 0.68, 0.055, [0.75, 0.77, 0.8]);
      overlayStrip(B, line, cx + 0.68, cx + 0.75, 0.055, [0.75, 0.77, 0.8]);
    }
  }
}

function overlayStrip(B, line, x0, x1, y, tint) {
  const base = B.count;
  for (const p of line) {
    const nx = -p.tz, nz = p.tx;
    for (const off of [x0, x1]) {
      B.positions.push(p.x + nx * off, p.y + y, p.z + nz * off);
      B.normals.push(0, 1, 0);
      B.uvs.push(0, p.s);
      B.colors.push(tint[0], tint[1], tint[2]);
    }
  }
  for (let i = 0; i < line.length - 1; i++) {
    const r0 = base + i * 2, r1 = base + (i + 1) * 2;
    B.index.push(r0, r1, r0 + 1, r0 + 1, r1, r1 + 1);
  }
  B.count += line.length * 2;
}

function surfaceTint(surface, v) {
  const j = v.hueJitter, w = v.wear * 0.12;
  switch (surface) {
    case 'asphalt-dark': return [0.23 + j + w, 0.235 + j + w, 0.245 + j + w];
    case 'asphalt-worn': return [0.32 + j + w, 0.315 + j + w, 0.31 + j + w * 0.8];
    case 'asphalt-green': return [0.25 + j, 0.42 + j, 0.38 + j];
    case 'concrete': return [0.62 + j, 0.615 + j, 0.6 + j];
    case 'concrete-dark': return [0.42 + j, 0.42 + j, 0.43 + j];
    case 'cobble': return [0.5 + j * 2, 0.49 + j * 2, 0.47 + j * 2];
    case 'sett': return [0.45 + j * 2, 0.44 + j * 2, 0.43 + j * 2];
    case 'brick': return [0.55 + j, 0.32 + j * 0.5, 0.28 + j * 0.5];
    case 'paver': return [0.58 + j * 2, 0.55 + j * 2, 0.52 + j * 2];
    case 'gravel': case 'gravel-fine': return [0.6 + j * 2, 0.57 + j * 2, 0.5 + j * 2];
    case 'dirt': case 'dirt-grass': case 'dirt-forest': return [0.45 + j * 2, 0.36 + j * 2, 0.27 + j * 2];
    case 'red-bus': return [0.55 + j, 0.22, 0.2];
    default: return [0.27 + j + w, 0.275 + j + w, 0.285 + j + w];
  }
}

// --------------------------------------------------------------- markings
function buildMarkings(buckets, line, profile, type, variant, stats) {
  const y = 0.055;
  // lane boundaries
  for (const m of profile.laneMarks) {
    paintLine(buckets, line, m.x, m.style, y, variant, stats);
  }
  // edge lines at carriage borders
  if (type.edgeLine && type.edgeLine !== 'none') {
    const { x0, x1 } = profile.carriage;
    const style = type.edgeLine === 'rumble' ? 'solid-white' : type.edgeLine === 'bus-line' ? 'bus-line'
      : type.edgeLine === 'solid-green' ? 'solid-green' : 'solid-white';
    paintLine(buckets, line, x0 + 0.25, style, y, variant, stats, type.edgeLine === 'rumble' ? { rumble: true } : {});
    paintLine(buckets, line, x1 - 0.25, style, y, variant, stats, type.edgeLine === 'rumble' ? { rumble: true } : {});
  }
  // lane-use arrows + bike symbols + bus lettering blocks
  const centers = driveLaneCenters(profile);
  let n = 0;
  for (let s = 25; s < line[line.length - 1].s - 15; s += 70) {
    for (const c of centers) {
      if (c.kind === 'bike') paintBike(buckets, atS(line, s + n * 3), c.x, stats);
      else if (c.kind === 'bus') paintBusBlock(buckets, atS(line, s), c.x, stats);
      else if ((n + centers.indexOf(c)) % 2 === 0) paintArrow(buckets, atS(line, s), c.x, c.dir, stats);
    }
    n++;
  }
  // parking T-marks
  for (const strip of profile.strips) {
    if (strip.kind === 'parking') {
      const B = bucketFor(buckets, 'marking-white');
      for (let s = 4; s < line[line.length - 1].s - 2; s += 6) {
        const p = atS(line, s);
        tick(B, p, strip.x0, strip.x1, 0.12, y);
        stats.markings++;
      }
    }
  }
}

function atS(line, s) {
  s = Math.max(0, Math.min(line[line.length - 1].s, s));
  let i = 0;
  while (i < line.length - 2 && line[i + 1].s < s) i++;
  const a = line[i], b = line[i + 1];
  const t = b.s > a.s ? (s - a.s) / (b.s - a.s) : 0;
  return { x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, z: a.z + (b.z - a.z) * t, tx: a.tx, tz: a.tz, s };
}

function paintLine(buckets, line, x, style, y, variant, stats, opts = {}) {
  const total = line[line.length - 1].s;
  const emit = (xx, bucket, wHalf = 0.07) => {
    if (style.startsWith('dashed') || style === 'turn-lane-inner') {
      const dash = style === 'dashed-yellow' ? 3 : 3, gap = style === 'dashed-yellow' ? 6 : 9;
      for (let s = variant.dashPhase; s < total - 1; s += dash + gap) {
        quadAlong(bucketFor(buckets, bucket), line, Math.max(0, s), Math.min(total, s + dash), xx - wHalf, xx + wHalf, y);
        stats.markings++;
      }
    } else {
      quadAlong(bucketFor(buckets, bucket), line, 0, total, xx - wHalf, xx + wHalf, y);
      stats.markings++;
    }
  };
  switch (style) {
    case 'double-yellow':
      emit(x - 0.12, 'marking-yellow'); emit(x + 0.12, 'marking-yellow'); break;
    case 'dashed-yellow': emit(x, 'marking-yellow'); break;
    case 'solid-white': emit(x, 'marking-white'); break;
    case 'solid-green': emit(x, 'marking-green'); break;
    case 'bus-line': emit(x, 'marking-yellow', 0.12); break;
    case 'turn-lane': emit(x - 0.25, 'marking-yellow'); emit(x + 0.25, 'marking-yellow'); break;
    case 'dashed-white': default: emit(x, 'marking-white'); break;
  }
  if (opts.rumble) {
    const B = bucketFor(buckets, 'asphalt-worn');
    for (let s = 0; s < total - 1; s += 1.2) {
      const p = atS(line, s);
      bump(B, p, x - 0.2, x + 0.2, 0.5, 0.03);
    }
  }
}

function quadAlong(B, line, s0, s1, x0, x1, y) {
  const a = atS(line, s0), b = atS(line, s1);
  const quad = (p, off) => {
    const nx = -p.tz, nz = p.tx;
    return [p.x + nx * off, p.y + y, p.z + nz * off];
  };
  const v = [quad(a, x0), quad(a, x1), quad(b, x1), quad(b, x0)];
  const base = B.count;
  for (const p of v) { B.positions.push(...p); B.normals.push(0, 1, 0); B.uvs.push(0, 0); B.colors.push(1, 1, 1); }
  B.index.push(base, base + 2, base + 1, base, base + 3, base + 2);
  B.count += 4;
}

function tick(B, p, x0, x1, w, y) {
  const nx = -p.tz, nz = p.tx;
  const cx = (x0 + x1) / 2;
  const v = [
    [p.x + nx * (cx - w / 2) - p.tx * 0.06, p.y + y, p.z + nz * (cx - w / 2) - p.tz * 0.06],
    [p.x + nx * (cx + w / 2) - p.tx * 0.06, p.y + y, p.z + nz * (cx + w / 2) - p.tz * 0.06],
    [p.x + nx * (cx + w / 2) + p.tx * 0.06, p.y + y, p.z + nz * (cx + w / 2) + p.tz * 0.06],
    [p.x + nx * (cx - w / 2) + p.tx * 0.06, p.y + y, p.z + nz * (cx - w / 2) + p.tz * 0.06],
  ];
  const base = B.count;
  for (const q of v) { B.positions.push(...q); B.normals.push(0, 1, 0); B.uvs.push(0, 0); B.colors.push(1, 1, 1); }
  B.index.push(base, base + 2, base + 1, base, base + 3, base + 2);
  B.count += 4;
}

function bump(B, p, x0, x1, len, h) {
  const nx = -p.tz, nz = p.tx;
  const v = [
    [p.x + nx * x0, p.y + 0.02, p.z + nz * x0],
    [p.x + nx * x1, p.y + 0.02, p.z + nz * x1],
    [p.x + nx * x1 + p.tx * len, p.y + 0.02, p.z + nz * x1 + p.tz * len],
    [p.x + nx * x0 + p.tx * len, p.y + 0.02, p.z + nz * x0 + p.tz * len],
    [p.x + nx * x0 + p.tx * len / 2, p.y + 0.02 + h, p.z + nz * x0 + p.tz * len / 2],
    [p.x + nx * x1 + p.tx * len / 2, p.y + 0.02 + h, p.z + nz * x1 + p.tz * len / 2],
  ];
  const base = B.count;
  for (const q of v) { B.positions.push(...q); B.normals.push(0, 1, 0); B.uvs.push(0, 0); B.colors.push(0.8, 0.8, 0.8); }
  B.index.push(base, base + 4, base + 1, base + 1, base + 4, base + 5, base + 4, base + 3, base + 5, base + 5, base + 3, base + 2);
  B.count += 6;
}

// Flat convex-polygon emitter with runtime winding correction: orders points
// by signed area so the pre-flip (down-facing) convention always holds,
// regardless of road heading. Used for arrows, symbols and blocks.
function flatPoly(B, pts, y, tint = [1, 1, 1]) {
  let area = 0;
  for (let i = 0; i < pts.length; i++) {
    const [x0, z0] = pts[i], [x1, z1] = pts[(i + 1) % pts.length];
    area += x0 * z1 - x1 * z0;
  }
  const ordered = area > 0 ? pts : [...pts].reverse(); // positive area = down pre-flip
  const base = B.count;
  for (const [x, z] of ordered) {
    B.positions.push(x, y, z); B.normals.push(0, 1, 0); B.uvs.push(0, 0); B.colors.push(...tint);
  }
  for (let i = 1; i < ordered.length - 1; i++) B.index.push(base, base + i, base + i + 1);
  B.count += ordered.length;
}

function paintArrow(buckets, p, x, dir, stats) {
  const B = bucketFor(buckets, 'marking-white');
  const nx = -p.tz * dir, nz = p.tx * dir;
  const fx = p.tx * dir, fz = p.tz * dir;
  const y = p.y + 0.055;
  const cx = p.x + (-p.tz) * x, cz = p.z + p.tx * x;
  const P2 = (lat, fwd) => [cx + nx * lat + fx * fwd, cz + nz * lat + fz * fwd];
  flatPoly(B, [P2(-0.25, 0), P2(0.25, 0), P2(0.25, 2.4), P2(-0.25, 2.4)], y); // shaft
  flatPoly(B, [P2(-0.7, 2.4), P2(0.7, 2.4), P2(0, 3.6)], y); // head
  stats.markings++;
}

function paintBike(buckets, p, x, stats) {
  const B = bucketFor(buckets, 'marking-white');
  const y = p.y + 0.055;
  const cx = p.x + (-p.tz) * x, cz = p.z + p.tx * x;
  // lane-use diamond, rotated into the travel frame
  const fx = p.tx, fz = p.tz, sx = -p.tz, sz = p.tx;
  flatPoly(B, [
    [cx + fx * 1.1, cz + fz * 1.1], [cx + sx * 0.55, cz + sz * 0.55],
    [cx - fx * 1.1, cz - fz * 1.1], [cx - sx * 0.55, cz - sz * 0.55],
  ], y);
  stats.markings++;
}

function paintBusBlock(buckets, p, x, stats) {
  const B = bucketFor(buckets, 'marking-white');
  const y = p.y + 0.055;
  const cx = p.x + (-p.tz) * x, cz = p.z + p.tx * x;
  const fx = p.tx, fz = p.tz, sx = -p.tz, sz = p.tx;
  const w = 1.2, l = 2.4;
  flatPoly(B, [
    [cx - sx * w / 2 - fx * l / 2, cz - sz * w / 2 - fz * l / 2],
    [cx + sx * w / 2 - fx * l / 2, cz + sz * w / 2 - fz * l / 2],
    [cx + sx * w / 2 + fx * l / 2, cz + sz * w / 2 + fz * l / 2],
    [cx - sx * w / 2 + fx * l / 2, cz - sz * w / 2 + fz * l / 2],
  ], y);
  stats.markings++;
}

// --------------------------------------------------------------- junctions
function buildJunction(buckets, graph, node, stats) {
  const inc = node.incident;
  if (inc.length < 3) return;
  const R = node.junction.radius;
  const fillet = Math.min(node.junction.fillet, R * 0.9);
  const y = node.pos.y + 0.02;
  // mouth data per incident (sorted by angle already)
  const mouths = inc.map((m) => {
    const e = graph.edges.get(m.edge);
    const half = e ? fullWidth(roadTypeById(e.typeId)) / 2 : 5;
    const dir = m.dir; // outward
    const px = -dir.z, pz = dir.x; // left normal
    const cx = node.pos.x + dir.x * R, cz = node.pos.z + dir.z * R;
    return {
      ...m, half, cx, cz, dir, px, pz,
      L: { x: cx + px * half, z: cz + pz * half },
      Rc: { x: cx - px * half, z: cz - pz * half },
    };
  });
  // boundary loop: R_i → fillet arc → L_{i+1}
  // Boundary in descending arm order: R(k) → corner arc → L(k-1) → mouth
  // chord → R(k-1) … — every consecutive pair is adjacent on the perimeter.
  const loop = [];
  for (let t = 0; t < mouths.length; t++) {
    const k = (mouths.length - t) % mouths.length;
    const cur = mouths[k], prevM = mouths[(k - 1 + mouths.length) % mouths.length];
    loop.push(cur.Rc);
    const arc = filletArc(cur.Rc, cur.dir, prevM.L, prevM.dir, fillet);
    for (const p of arc) loop.push(p);
    loop.push(prevM.L);
  }
  // fan triangulation from center
  const B = bucketFor(buckets, 'asphalt');
  const tint = [0.28, 0.285, 0.295];
  const base = B.count;
  B.positions.push(node.pos.x, y, node.pos.z);
  B.normals.push(0, 1, 0); B.uvs.push(0.5, 0.5); B.colors.push(...tint);
  for (const p of loop) {
    B.positions.push(p.x, y, p.z);
    B.normals.push(0, 1, 0); B.uvs.push(p.x * 0.05, p.z * 0.05); B.colors.push(...tint);
  }
  B.count += 1 + loop.length;
  for (let i = 0; i < loop.length; i++) {
    const a = base + 1 + i, b = base + 1 + ((i + 1) % loop.length);
    B.index.push(base, b, a); // winding: check below via normal recompute? keep CCW-up
  }
  // curb returns along each fillet
  const C = bucketFor(buckets, 'curb');
  for (let i = 0; i < mouths.length; i++) {
    const cur = mouths[i], next = mouths[(i - 1 + mouths.length) % mouths.length]; // R(i) meets L(i-1): clockwise side pairs with previous arm
    const arc = [cur.Rc, ...filletArc(cur.Rc, cur.dir, next.L, next.dir, fillet), next.L];
    ribbonAlong(C, arc, node.pos.y, 0.3, 0.14, [0.55, 0.55, 0.56]);
  }
  // crosswalks + stop lines at mouths with sidewalks
  if (node.junction.crosswalk) {
    for (const m of mouths) {
      const e = graph.edges.get(m.edge);
      const t = e ? roadTypeById(e.typeId) : null;
      if (!t || t.sidewalk === 'none' || t.sidewalk === 'itself') continue;
      if (t.category === 'Motorway' || t.category === 'Rural' || t.category === 'Offroad') continue;
      paintCrosswalk(buckets, node, m, stats);
    }
  }
  // yield/stop sign furniture
  stats.markings += mouths.length;
}

// True tangent-arc curb return between adjacent mouth corners A and B.
// The road edge LINES meet at corner Q (behind both mouths); the arc of
// radius r touches each edge at T1/T2, inset upstream of the mouths exactly
// like a real kerb return. Returns [T1, ...arc, T2] (may be [] if parallel).
function filletArc(A, dirA, Bpt, dirB, r) {
  const Q = lineLine(A, dirA, Bpt, dirB);
  if (!Q) return [];
  const u1 = normXZ({ x: A.x - Q.x, z: A.z - Q.z });
  const u2 = normXZ({ x: Bpt.x - Q.x, z: Bpt.z - Q.z });
  const cosT = Math.max(-1, Math.min(1, u1.x * u2.x + u1.z * u2.z));
  const theta = Math.acos(cosT);
  if (theta < 0.12 || theta > Math.PI - 0.08) return []; // near-parallel: straight chamfer
  const dA = Math.hypot(A.x - Q.x, A.z - Q.z);
  const dB = Math.hypot(Bpt.x - Q.x, Bpt.z - Q.z);
  const tanHalf = Math.tan(theta / 2);
  let rr = Math.min(r, dA * tanHalf * 0.92, dB * tanHalf * 0.92);
  if (!(rr > 0.4)) return [];
  const d = rr / tanHalf;
  const T1 = { x: Q.x + u1.x * d, z: Q.z + u1.z * d };
  const T2 = { x: Q.x + u2.x * d, z: Q.z + u2.z * d };
  const bis = normXZ({ x: u1.x + u2.x, z: u1.z + u2.z });
  const C = { x: Q.x + bis.x * (rr / Math.sin(theta / 2)), z: Q.z + bis.z * (rr / Math.sin(theta / 2)) };
  let a0 = Math.atan2(T1.z - C.z, T1.x - C.x);
  let a1 = Math.atan2(T2.z - C.z, T2.x - C.x);
  let sweep = a1 - a0;
  while (sweep > Math.PI) sweep -= Math.PI * 2;
  while (sweep < -Math.PI) sweep += Math.PI * 2;
  // The tangent arc is always the short way (|sweep| = π−θ < π): it rounds the
  // corner AT Q, so its midpoint legitimately passes near Q. Never flip to
  // the long way — that loops through the junction interior and bowties the
  // boundary (midpoint-near-Q "corrections" are always wrong here).
  const pts = [T1];
  const N = 7;
  for (let i = 1; i < N; i++) {
    const a = a0 + (sweep * i) / N;
    pts.push({ x: C.x + Math.cos(a) * rr, z: C.z + Math.sin(a) * rr });
  }
  pts.push(T2);
  return pts;
}

function normXZ(d) {
  const l = Math.hypot(d.x, d.z) || 1;
  return { x: d.x / l, z: d.z / l };
}

// Intersection of two infinite lines (no t-range clamp — the corner Q sits
// behind both mouths by construction).
function lineLine(p1, d1, p2, d2) {
  const denom = d1.x * d2.z - d1.z * d2.x;
  if (Math.abs(denom) < 1e-9) return null;
  const t = ((p2.x - p1.x) * d2.z - (p2.z - p1.z) * d2.x) / denom;
  return { x: p1.x + d1.x * t, z: p1.z + d1.z * t };
}

function rayRay(p1, d1, p2, d2) {
  const denom = d1.x * d2.z - d1.z * d2.x;
  if (Math.abs(denom) < 1e-6) return null;
  const t = ((p2.x - p1.x) * d2.z - (p2.z - p1.z) * d2.x) / denom;
  if (t < -2 || t > 60) return null;
  return { x: p1.x + d1.x * t, z: p1.z + d1.z * t };
}

function ribbonAlong(B, pts, baseY, width, h, tint) {
  // flat curb-top ribbon following pts
  const base = B.count;
  for (const p of pts) {
    // direction estimated from neighbours
    B.positions.push(p.x, baseY + h, p.z);
    B.normals.push(0, 1, 0); B.uvs.push(0, 0); B.colors.push(...tint);
    B.positions.push(p.x, baseY + h, p.z); // placeholder replaced below
  }
  // rebuild with proper lateral offsets
  B.positions.splice(base * 3, pts.length * 2 * 3);
  B.normals.splice(base * 3, pts.length * 2 * 3);
  B.uvs.splice(base * 2, pts.length * 2 * 2);
  B.colors.splice(base * 3, pts.length * 2 * 3);
  for (let i = 0; i < pts.length; i++) {
    const a = pts[Math.max(0, i - 1)], b = pts[Math.min(pts.length - 1, i + 1)];
    let dx = b.x - a.x, dz = b.z - a.z;
    const l = Math.hypot(dx, dz) || 1; dx /= l; dz /= l;
    const nx = -dz, nz = dx;
    for (const s of [-width / 2, width / 2]) {
      B.positions.push(pts[i].x + nx * s, baseY + h, pts[i].z + nz * s);
      B.normals.push(0, 1, 0); B.uvs.push(0, i); B.colors.push(...tint);
    }
  }
  for (let i = 0; i < pts.length - 1; i++) {
    const r0 = base + i * 2, r1 = base + (i + 1) * 2;
    B.index.push(r0, r1, r0 + 1, r0 + 1, r1, r1 + 1);
  }
  B.count += pts.length * 2;
}

function paintCrosswalk(buckets, node, m, stats) {
  const B = bucketFor(buckets, 'marking-white');
  const y = node.pos.y + 0.055;
  // stripes across the mouth, just outside the junction mouth line
  const back = 1.6; // toward junction from mouth center
  const cx = m.cx - m.dir.x * back, cz = m.cz - m.dir.z * back;
  const n = Math.max(3, Math.floor((m.half * 2 - 1) / 1.1));
  for (let i = 0; i < n; i++) {
    const off = -m.half + 0.7 + i * ((m.half * 2 - 1.4) / Math.max(1, n - 1));
    const sx = cx + m.px * off, sz = cz + m.pz * off;
    const w = 0.45, l = 2.6;
    const v = [
      [sx - m.px * w / 2 - m.dir.x * l / 2, y, sz - m.pz * w / 2 - m.dir.z * l / 2],
      [sx + m.px * w / 2 - m.dir.x * l / 2, y, sz + m.pz * w / 2 - m.dir.z * l / 2],
      [sx + m.px * w / 2 + m.dir.x * l / 2, y, sz + m.pz * w / 2 + m.dir.z * l / 2],
      [sx - m.px * w / 2 + m.dir.x * l / 2, y, sz - m.pz * w / 2 + m.dir.z * l / 2],
    ];
    const base = B.count;
    for (const q of v) { B.positions.push(...q); B.normals.push(0, 1, 0); B.uvs.push(0, 0); B.colors.push(1, 1, 1); }
    B.index.push(base, base + 2, base + 1, base, base + 3, base + 2);
    B.count += 4;
    stats.markings++;
  }
  // stop line
  const S = bucketFor(buckets, 'marking-white');
  const lx = cx + m.dir.x * 2.2, lz = cz + m.dir.z * 2.2;
  const v = [
    [lx - m.px * (m.half - 0.6), y, lz - m.pz * (m.half - 0.6)],
    [lx + m.px * (m.half - 0.6), y, lz + m.pz * (m.half - 0.6)],
    [lx + m.px * (m.half - 0.6) + m.dir.x * 0.4, y, lz + m.pz * (m.half - 0.6) + m.dir.z * 0.4],
    [lx - m.px * (m.half - 0.6) + m.dir.x * 0.4, y, lz - m.pz * (m.half - 0.6) + m.dir.z * 0.4],
  ];
  const base = S.count;
  for (const q of v) { S.positions.push(...q); S.normals.push(0, 1, 0); S.uvs.push(0, 0); S.colors.push(1, 1, 1); }
  S.index.push(base, base + 2, base + 1, base, base + 3, base + 2);
  S.count += 4;
}

function buildRoundabout(buckets, graph, node, stats) {
  const rb = node.roundabout;
  const R = rb.radius;         // island radius
  const ringW = rb.ringWidth || 7.5;
  const outer = R + ringW;
  const y = node.pos.y + 0.02;
  const cx = node.pos.x, cz = node.pos.z;
  // Paved apron disc under ring + mouths: seals the gaps between ribbon
  // mouth chords and the ring circle. 30mm below the ring: no z-fighting.
  let maxHw = 6;
  for (const m of node.incident) {
    const e = graph.edges.get(m.edge);
    if (e) maxHw = Math.max(maxHw, fullWidth(roadTypeById(e.typeId)) / 2);
  }
  const apronR = outer + 1.5 + maxHw * 0.8;
  const NA = 48;
  const AP = bucketFor(buckets, 'asphalt');
  const aBase = AP.count;
  AP.positions.push(cx, y - 0.03, cz);
  AP.normals.push(0, 1, 0); AP.uvs.push(0.5, 0.5); AP.colors.push(0.27, 0.275, 0.285);
  for (let i = 0; i <= NA; i++) {
    const a = (i / NA) * Math.PI * 2;
    AP.positions.push(cx + Math.cos(a) * apronR, y - 0.03, cz + Math.sin(a) * apronR);
    AP.normals.push(0, 1, 0); AP.uvs.push(0, 0); AP.colors.push(0.27, 0.275, 0.285);
  }
  AP.count += NA + 2;
  // Apron points run CCW (angle increasing) → (c, cur, next) is down pre-flip.
  for (let i = 0; i < NA; i++) AP.index.push(aBase, aBase + 1 + i, aBase + 2 + i);
  // ring asphalt annulus
  const B = bucketFor(buckets, 'asphalt');
  const N = 64;
  const base = B.count;
  for (let i = 0; i <= N; i++) {
    const a = (i / N) * Math.PI * 2;
    for (const r of [R, outer]) {
      B.positions.push(cx + Math.cos(a) * r, y, cz + Math.sin(a) * r);
      B.normals.push(0, 1, 0); B.uvs.push(i / N * 8, r); B.colors.push(0.28, 0.285, 0.295);
    }
  }
  // NOTE: angular parametrization runs CCW, opposite to ribbon travel, so the
  // ring strips take the mirrored order (down-facing pre-flip, like ribbons).
  for (let i = 0; i < N; i++) {
    const r0 = base + i * 2, r1 = base + (i + 1) * 2;
    B.index.push(r0, r0 + 1, r1, r0 + 1, r1 + 1, r1);
  }
  B.count += (N + 1) * 2;
  // island: curb ring + grass/verge dome
  const C = bucketFor(buckets, 'curb');
  const cBase = C.count;
  for (let i = 0; i <= N; i++) {
    const a = (i / N) * Math.PI * 2;
    for (const [r, yy] of [[R - 0.4, y + 0.12], [R, y + 0.12]]) {
      C.positions.push(cx + Math.cos(a) * r, yy, cz + Math.sin(a) * r);
      C.normals.push(0, 1, 0); C.uvs.push(0, 0); C.colors.push(0.55, 0.55, 0.56);
    }
  }
  for (let i = 0; i < N; i++) {
    const r0 = cBase + i * 2, r1 = cBase + (i + 1) * 2;
    C.index.push(r0, r0 + 1, r1, r0 + 1, r1 + 1, r1);
  }
  C.count += (N + 1) * 2;
  const G = bucketFor(buckets, 'verge');
  const gBase = G.count;
  G.positions.push(cx, y + 0.14, cz); G.normals.push(0, 1, 0); G.uvs.push(0.5, 0.5); G.colors.push(0.35, 0.46, 0.26);
  for (let i = 0; i <= N; i++) {
    const a = (i / N) * Math.PI * 2;
    G.positions.push(cx + Math.cos(a) * (R - 0.4), y + 0.14, cz + Math.sin(a) * (R - 0.4));
    G.normals.push(0, 1, 0); G.uvs.push(0, 0); G.colors.push(0.35, 0.46, 0.26);
  }
  G.count += N + 2;
  for (let i = 0; i < N; i++) G.index.push(gBase, gBase + 1 + i, gBase + 2 + i);
  // yield dashes on each entry
  for (const m of node.incident) {
    const dir = m.dir;
    const px = -dir.z, pz = dir.x;
    const ex = cx + dir.x * (outer + 1.2), ez = cz + dir.z * (outer + 1.2);
    const YB = bucketFor(buckets, 'marking-white');
    for (let k = -2; k <= 2; k++) {
      const sx = ex + px * k * 1.1, sz = ez + pz * k * 1.1;
      const v = [
        [sx - px * 0.3, y + 0.035, sz - pz * 0.3], [sx + px * 0.3, y + 0.035, sz + pz * 0.3],
        [sx + px * 0.3 + dir.x * 0.6, y + 0.035, sz + pz * 0.3 + dir.z * 0.6],
        [sx - px * 0.3 + dir.x * 0.6, y + 0.035, sz - pz * 0.3 + dir.z * 0.6],
      ];
      const bb = YB.count;
      for (const q of v) { YB.positions.push(...q); YB.normals.push(0, 1, 0); YB.uvs.push(0, 0); YB.colors.push(1, 1, 1); }
      YB.index.push(bb, bb + 2, bb + 1, bb, bb + 3, bb + 2);
      YB.count += 4;
    }
  }
}

// --------------------------------------------------------------- furniture
function collectFurniture(furniture, line, profile, type, edge, stats) {
  const total = line[line.length - 1].s;
  const half = profile.totalWidth / 2;
  // street lights
  if (type.lights && type.lights !== 'none') {
    for (let s = 10; s < total - 5; s += type.category === 'Motorway' ? 45 : 30) {
      const p = atS(line, s);
      const side = (Math.floor(s / 30) % 2 === 0 ? 1 : -1);
      furniture.lamps.push({ x: p.x + (-p.tz) * side * (half + 0.8), y: p.y, z: p.z + p.tx * side * (half + 0.8), rot: Math.atan2(p.tx, p.tz), kind: type.lights });
    }
  }
  // guardrails as posts + rail ribbon handled in viewport instancing (posts here)
  if (type.guardrail) {
    for (let s = 2; s < total - 1; s += 4) {
      const p = atS(line, s);
      for (const side of [-1, 1]) {
        furniture.posts.push({ x: p.x + (-p.tz) * side * (half - 0.4), y: p.y, z: p.z + p.tx * side * (half - 0.4), kind: 'guardrail' });
      }
    }
  }
  // bollards
  if (type.bollards) {
    for (let s = 4; s < total - 2; s += 4) {
      const p = atS(line, s);
      for (const side of [-1, 1]) {
        furniture.bollards.push({ x: p.x + (-p.tz) * side * (half - 0.6), y: p.y + 0.13, z: p.z + p.tx * side * (half - 0.6), kind: type.bollards });
      }
    }
  }
  // speed tables on traffic-calmed streets
  if (type.bumps) {
    for (let s = 60; s < total - 30; s += 120) {
      const p = atS(line, s);
      furniture.signs.push({ x: p.x, y: p.y, z: p.z, kind: 'bump-table', rot: Math.atan2(p.tx, p.tz), width: profile.carriage.x1 - profile.carriage.x0 });
    }
  }
  // bus stops
  if (type.busStop || type.lanes.some((l) => l.kind === 'bus')) {
    for (let s = 80; s < total - 40; s += 260) {
      const p = atS(line, s);
      furniture.busStops.push({ x: p.x + (-p.tz) * (half + 1.5), y: p.y, z: p.z + p.tx * (half + 1.5), rot: Math.atan2(p.tx, p.tz) });
    }
  }
}

// ---------------------------------------------------------------- rail/water
function buildRail(buckets, furniture, rail) {
  const pts = rail.points;
  if (!pts || pts.length < 2) return;
  const y = (rail.y ?? 0.02);
  // ballast bed
  const B = bucketFor(buckets, 'railbed');
  ribbonFlat(B, pts, 3.4, y, [0.4, 0.38, 0.36]);
  // sleepers
  let acc = 0;
  for (let i = 0; i < pts.length - 1; i++) {
    const a = pts[i], b = pts[i + 1];
    const segLen = Math.hypot(b.x - a.x, b.z - a.z);
    const tx = (b.x - a.x) / (segLen || 1), tz = (b.z - a.z) / (segLen || 1);
    for (let s = acc; s < segLen; s += 0.7) {
      const t = s / segLen;
      furniture.sleepers.push({ x: a.x + (b.x - a.x) * t, y, z: a.z + (b.z - a.z) * t, rot: Math.atan2(tx, tz) });
    }
    acc = (acc + segLen) % 0.7;
  }
  // rails (two steel strips, standard gauge 1.435)
  const S = bucketFor(buckets, 'steel');
  ribbonFlat(S, pts, 0.07, y + 0.14, [0.7, 0.72, 0.75], -0.7175);
  ribbonFlat(S, pts, 0.07, y + 0.14, [0.7, 0.72, 0.75], 0.7175);
}

function ribbonFlat(B, pts, width, y, tint, lateral = 0) {
  const base = B.count;
  for (let i = 0; i < pts.length; i++) {
    const a = pts[Math.max(0, i - 1)], b = pts[Math.min(pts.length - 1, i + 1)];
    let dx = b.x - a.x, dz = b.z - a.z;
    const l = Math.hypot(dx, dz) || 1; dx /= l; dz /= l;
    const nx = -dz, nz = dx;
    const cx = pts[i].x + nx * lateral, cz = pts[i].z + nz * lateral;
    for (const s of [-width / 2, width / 2]) {
      B.positions.push(cx + nx * s, (pts[i].y ?? 0) + y, cz + nz * s);
      B.normals.push(0, 1, 0); B.uvs.push(0, 0); B.colors.push(...tint);
    }
  }
  for (let i = 0; i < pts.length - 1; i++) {
    const r0 = base + i * 2, r1 = base + (i + 1) * 2;
    B.index.push(r0, r1, r0 + 1, r0 + 1, r1, r1 + 1);
  }
  B.count += pts.length * 2;
}

function buildWater(buckets, w) {
  const B = bucketFor(buckets, 'water');
  ribbonFlat(B, w.points, w.width || 24, 0.02, [0.2, 0.42, 0.55]);
  // banks
  const V = bucketFor(buckets, 'verge');
  ribbonFlat(V, w.points, (w.width || 24) + 8, -0.15, [0.42, 0.48, 0.3]);
}

// ---------------------------------------------------------------- proxies
function proxyForEdge(edge, line, width) {
  const positions = new Float32Array(line.length * 2 * 3);
  line.forEach((p, i) => {
    const nx = -p.tz, nz = p.tx;
    positions.set([p.x + nx * width / 2, p.y + 0.5, p.z + nz * width / 2], i * 6);
    positions.set([p.x - nx * width / 2, p.y + 0.5, p.z - nz * width / 2], i * 6 + 3);
  });
  const index = [];
  for (let i = 0; i < line.length - 1; i++) {
    const r0 = i * 2, r1 = (i + 1) * 2;
    index.push(r0, r1, r0 + 1, r0 + 1, r1, r1 + 1);
  }
  return { id: edge.id, kind: 'edge', positions, index };
}

function proxyForNode(node, r = 0) {
  const rad = r || node.junction.radius + 2;
  const N = 20;
  const positions = new Float32Array((N + 1) * 3);
  positions.set([node.pos.x, node.pos.y + 0.6, node.pos.z], 0);
  for (let i = 0; i < N; i++) {
    const a = (i / N) * Math.PI * 2;
    positions.set([node.pos.x + Math.cos(a) * rad, node.pos.y + 0.6, node.pos.z + Math.sin(a) * rad], (i + 1) * 3);
  }
  const index = [];
  for (let i = 0; i < N; i++) index.push(0, 1 + i, 1 + ((i + 1) % N));
  return { id: node.id, kind: 'node', positions, index };
}

// ---------------------------------------------------------------- weld
function weldBucket(B) {
  const map = new Map();
  const P = [], N = [], U = [], C = [];
  const remap = new Int32Array(B.count);
  let next = 0;
  for (let i = 0; i < B.count; i++) {
    const x = B.positions[i * 3], y = B.positions[i * 3 + 1], z = B.positions[i * 3 + 2];
    const key = `${Math.round(x * 1000)},${Math.round(y * 1000)},${Math.round(z * 1000)}`;
    let id = map.get(key);
    if (id === undefined) {
      id = next++;
      map.set(key, id);
      P.push(x, y, z);
      N.push(B.normals[i * 3], B.normals[i * 3 + 1], B.normals[i * 3 + 2]);
      U.push(B.uvs[i * 2], B.uvs[i * 2 + 1]);
      C.push(B.colors[i * 3], B.colors[i * 3 + 1], B.colors[i * 3 + 2]);
    }
    remap[i] = id;
  }
  const idx = [];
  for (let f = 0; f < B.index.length; f += 3) {
    const a = remap[B.index[f]], b = remap[B.index[f + 1]], c = remap[B.index[f + 2]];
    if (a === b || b === c || a === c) continue; // degenerate
    idx.push(a, b, c);
  }
  B.positions = P; B.normals = N; B.uvs = U; B.colors = C; B.index = idx; B.count = next;
}
