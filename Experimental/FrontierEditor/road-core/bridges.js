// Road Editor — parametric bridge kit (7 structural families + footbridge + culvert).
// Each bridge is generated from the road centerline span so decks always match
// the road cross-section: no stacked-slab overlaps, piers land clear of traffic.

export const BRIDGE_TYPES = [
  { id: 'beam',      name: 'Beam / girder',      desc: 'Precast I-girders on bent piers. Workhorse overpass.', minSpan: 8, maxSpan: 40 },
  { id: 'slab',      name: 'Slab / culvert',     desc: 'Flat concrete slab. Short spans, creeks, underpasses.', minSpan: 3, maxSpan: 14 },
  { id: 'arch',      name: 'Arch',               desc: 'Concrete or steel arch with spandrel columns.', minSpan: 20, maxSpan: 120 },
  { id: 'truss',     name: 'Truss',              desc: 'Steel through-truss. Rail + rural icons.', minSpan: 20, maxSpan: 90 },
  { id: 'suspension',name: 'Suspension',         desc: 'Main cables + suspenders + towers. Long crossings.', minSpan: 120, maxSpan: 600 },
  { id: 'cablestay', name: 'Cable-stayed',       desc: 'Fan stays from single / twin pylons.', minSpan: 60, maxSpan: 350 },
  { id: 'viaduct',   name: 'Viaduct',            desc: 'Repeating box-girder spans on tall piers.', minSpan: 15, maxSpan: 60 },
  { id: 'footbridge',name: 'Footbridge',        desc: 'Light pedestrian arch / truss over roads.', minSpan: 10, maxSpan: 50 },
];

export function bridgeById(id) { return BRIDGE_TYPES.find((b) => b.id === id) || BRIDGE_TYPES[0]; }

// buckets: material -> mesh bucket {positions,normals,uv,index,colors}
// span: {p0:{x,y,z}, p1:{x,y,z}, width, deckY}
// opts: {pierY (ground), lanes for railing color, variant}
export function buildBridge(bridgeId, span, buckets, opts = {}) {
  const B = ensureBuckets(buckets);
  const dx = span.p1.x - span.p0.x, dz = span.p1.z - span.p0.z;
  const len = Math.hypot(dx, dz) || 1;
  const tx = dx / len, tz = dz / len;
  const nx = -tz, nz = tx; // left normal
  const w = span.width;
  const deckT = bridgeId === 'slab' ? 0.8 : 1.4;
  const deckY = span.deckY;
  const groundY = opts.pierY ?? (deckY - 7);

  const P = (s, off, y) => [span.p0.x + tx * s + nx * off, y, span.p0.z + tz * s + nz * off];

  // --- deck slab (shared by all families, matches road width exactly) ---
  box(B['bridge-deck'], P(0, -w / 2, deckY - deckT), P(len, w / 2, deckY), tx, tz);
  // parapets / railings
  const railH = bridgeId === 'footbridge' ? 1.3 : 1.1;
  box(B['bridge-rail'], P(0, -w / 2 - 0.25, deckY), P(len, -w / 2 + 0.15, deckY + railH), tx, tz);
  box(B['bridge-rail'], P(0, w / 2 - 0.15, deckY), P(len, w / 2 + 0.25, deckY + railH), tx, tz);

  const struct = B['bridge-steel'];
  const conc = B['bridge-concrete'];
  const cable = B['bridge-cable'];
  // Through spans (elevated motorway continuing past the crossing) get piers
  // and girders but NO abutment walls — walls would block the continuing road.
  const through = !!opts.through;
  const abut = (...a) => { if (!through) abutments(...a); };

  if (bridgeId === 'beam' || bridgeId === 'viaduct') {
    const spanLen = bridgeId === 'viaduct' ? 30 : 25;
    const nSpans = Math.max(1, Math.round(len / spanLen));
    // girders: 4 longitudinal I-beams
    for (const off of [-w / 2 + 0.8, -w / 6, w / 6, w / 2 - 0.8]) {
      box(struct, P(0, off - 0.25, deckY - deckT - 1.4), P(len, off + 0.25, deckY - deckT), tx, tz);
    }
    for (let i = 1; i < nSpans; i++) {
      const s = (i / nSpans) * len;
      pier(conc, P(s, 0, groundY), P(s, 0, deckY - deckT - 1.2), w * 0.7, tx, tz);
      box(conc, P(s - 0.6, -w / 2, deckY - deckT - 1.6), P(s + 0.6, w / 2, deckY - deckT - 1.0), tx, tz); // bent cap
    }
    abut(conc, P, len, w, deckY - deckT, groundY, tx, tz);
  } else if (bridgeId === 'slab') {
    box(conc, P(0, -w / 2 - 0.5, deckY - deckT - 0.4), P(len, w / 2 + 0.5, deckY - deckT), tx, tz);
    abut(conc, P, len, w, deckY - deckT, groundY, tx, tz);
  } else if (bridgeId === 'arch') {
    const rise = Math.min(len * 0.18, deckY - groundY - 3);
    // two arch ribs
    for (const off of [-w / 2 + 0.5, w / 2 - 0.5]) {
      archRib(conc, P, len, off, deckY - deckT, groundY, rise, tx, tz);
    }
    // spandrel columns every ~6m
    for (let s = 4; s < len - 2; s += 6) {
      const t = s / len;
      const archY = deckY - deckT - rise * 4 * t * (1 - t) + rise * 0; // parabola springing below deck
      const springY = groundY + 1;
      const topY = deckY - deckT;
      const ay = springY + (topY - springY) * Math.sin(Math.PI * t) * 0.9 + (topY - springY) * 0.1;
      column(conc, P(s, -w / 2 + 0.5, ay), P(s, -w / 2 + 0.5, topY), 0.4);
      column(conc, P(s, w / 2 - 0.5, ay), P(s, w / 2 - 0.5, topY), 0.4);
    }
    abut(conc, P, len, w, groundY + 1, groundY - 1, tx, tz);
  } else if (bridgeId === 'truss') {
    const h = 5.5;
    const panel = 6;
    const n = Math.max(2, Math.round(len / panel));
    for (const side of [-1, 1]) {
      const off = side * (w / 2 - 0.2);
      // chords
      beam(struct, P(0, off, deckY), P(len, off, deckY), 0.35);
      beam(struct, P(0, off, deckY + h), P(len, off, deckY + h), 0.35);
      for (let i = 0; i <= n; i++) {
        const s = (i / n) * len;
        beam(struct, P(s, off, deckY), P(s, off, deckY + h), 0.22); // verticals
        if (i < n) {
          const s2 = ((i + 1) / n) * len;
          beam(struct, P(s, off, deckY), P(s2, off, deckY + h), 0.18); // diagonals
        }
      }
    }
    // portal frames + top struts
    for (let i = 0; i <= n; i += 2) {
      const s = (i / n) * len;
      beam(struct, P(s, -w / 2 + 0.2, deckY + h), P(s, w / 2 - 0.2, deckY + h), 0.25);
    }
    abut(conc, P, len, w, deckY - deckT, groundY, tx, tz);
  } else if (bridgeId === 'suspension') {
    const towerH = Math.max(18, len * 0.09);
    const sideSpan = len * 0.22;
    for (const s of [sideSpan, len - sideSpan]) {
      for (const off of [-w / 2 - 0.6, w / 2 + 0.6]) {
        column(conc, P(s, off, groundY), P(s, off, deckY + towerH), 1.1);
      }
      beam(conc, P(s, -w / 2 - 0.6, deckY + towerH - 2), P(s, w / 2 + 0.6, deckY + towerH - 2), 1.0);
    }
    // main cables (parabolic) + suspenders
    for (const off of [-w / 2 - 0.6, w / 2 + 0.6]) {
      let prev = null;
      for (let i = 0; i <= 32; i++) {
        const s = (i / 32) * len;
        const y = cableY(s / len, deckY + towerH, deckY + 1.5, sideSpan / len);
        const cur = P(s, off, y);
        if (prev) beam(cable, prev, cur, 0.16);
        prev = cur;
      }
      for (let s = sideSpan; s <= len - sideSpan; s += 6) {
        const y = cableY(s / len, deckY + towerH, deckY + 1.5, sideSpan / len);
        beam(cable, P(s, off, deckY), P(s, off, y), 0.07);
      }
    }
  } else if (bridgeId === 'cablestay') {
    const towerH = Math.max(16, len * 0.12);
    const mid = len / 2;
    const single = w < 12;
    const pylons = single ? [0] : [-w / 2 - 0.5, w / 2 + 0.5];
    for (const off of pylons) column(conc, P(mid, off, groundY), P(mid, off, deckY + towerH), 1.2);
    for (const off of (single ? [-w / 2 + 0.6, w / 2 - 0.6] : pylons)) {
      const anchorOff = single ? 0 : off;
      for (let k = 1; k <= 6; k++) {
        const s1 = mid - (k / 6) * (len / 2 - 4);
        const s2 = mid + (k / 6) * (len / 2 - 4);
        const yTop = deckY + towerH - (k - 1) * 1.6;
        beam(cable, P(s1, off, deckY + 0.4), P(mid, anchorOff, yTop), 0.09);
        beam(cable, P(s2, off, deckY + 0.4), P(mid, anchorOff, yTop), 0.09);
      }
    }
    pier(conc, P(mid, 0, groundY), P(mid, 0, deckY - deckT), w * 0.5, tx, tz);
  } else if (bridgeId === 'footbridge') {
    const rise = 2.5;
    for (const off of [-w / 2 - 0.1, w / 2 + 0.1]) {
      let prev = null;
      for (let i = 0; i <= 24; i++) {
        const t = i / 24;
        const s = t * len;
        const y = deckY + Math.sin(Math.PI * t) * rise + 1.0;
        const cur = P(s, off, y);
        if (prev) beam(struct, prev, cur, 0.14);
        prev = cur;
        if (i % 2 === 0) beam(cable, P(s, off, deckY), cur, 0.05);
      }
    }
    abut(conc, P, len, w, deckY - deckT, groundY, tx, tz);
  }
  return { length: len, piers: groundY };
}

function cableY(t, towerY, midY, side) {
  // side spans slope down to anchorages, main span parabolic
  if (t < side) { const k = t / side; return towerY + (towerY - 6 - towerY) * (1 - k) * 0 + (towerY - (towerY - midY + 4) * (1 - k)); }
  if (t > 1 - side) { const k = (1 - t) / side; return towerY - (towerY - midY + 4) * (1 - k); }
  const u = (t - side) / (1 - 2 * side); // 0..1 across main span
  return midY + (towerY - midY) * Math.pow(2 * u - 1, 2);
}

function ensureBuckets(buckets) {
  for (const k of ['bridge-deck', 'bridge-rail', 'bridge-steel', 'bridge-concrete', 'bridge-cable']) {
    if (!buckets[k]) buckets[k] = newBucket();
  }
  return buckets;
}

export function newBucket() {
  return { positions: [], normals: [], uvs: [], colors: [], index: [], count: 0 };
}

// Axis-aligned box between two corners, rotated so local X follows (tx,tz).
function box(B, c0, c1, tx, tz) {
  const [x0, y0, z0] = c0, [x1, y1, z1] = c1;
  // corners: s/z already in world; box defined by min/max in (s, off, y) space
  const s0 = 0, s1 = Math.hypot(x1 - x0, z1 - z0) || 1;
  void s0; void s1;
  const minX = Math.min(x0, x1), maxX = Math.max(x0, x1);
  const minY = Math.min(y0, y1), maxY = Math.max(y0, y1);
  const minZ = Math.min(z0, z1), maxZ = Math.max(z0, z1);
  // NOTE: P() already rotated; box edges follow world axes through those points.
  // For near-axis spans this is exact; diagonal spans use beam() segments instead.
  // We build from the 8 world corners derived from (s,off) pairs:
  const corners = [
    [x0, y0, z0], [x1, y0, z1], [x1, y1, z1], [x0, y1, z0],
  ];
  void minX; void maxX; void minY; void maxY; void minZ; void maxZ;
  // Build as two triangles per face from the 4-corner prism: approximate with
  // axis box through both points (sufficient for deck/parapet/piers on any
  // heading because P() spans full length: use oriented quad strip instead).
  orientedBox(B, [x0, y0, z0], [x1, y1, z1]);
}

// Oriented box from corner A=(s0,off0,y0) to B=(s1,off1,y1) in world coords.
function orientedBox(B, A, C) {
  const ux = C[0] - A[0], uy = C[1] - A[1], uz = C[2] - A[2];
  // derive basis: length axis L (horizontal), width axis W (horizontal perp), up
  const L = norm([ux, 0, uz]);
  const W = [-L[2], 0, L[0]];
  const len = Math.hypot(ux, uz);
  // width = component of (C-A) along W — but A..C span diagonally; instead the
  // caller passes true opposite corners, so recover w via perpendicular extent:
  const wEst = Math.abs(ux * W[0] + uz * W[2]);
  const w = Math.max(wEst, 0.05);
  const h = C[1] - A[1];
  const cx = (A[0] + C[0]) / 2, cy = (A[1] + C[1]) / 2, cz = (A[2] + C[2]) / 2;
  const hx = L[0] * len / 2, hz = L[2] * len / 2;
  const wx = W[0] * w / 2, wz = W[2] * w / 2;
  const y0 = cy - h / 2, y1 = cy + h / 2;
  const v = [
    [cx - hx - wx, y0, cz - hz - wz], [cx + hx - wx, y0, cz + hz - wz],
    [cx + hx + wx, y0, cz + hz + wz], [cx - hx + wx, y0, cz - hz + wz],
    [cx - hx - wx, y1, cz - hz - wz], [cx + hx - wx, y1, cz + hz - wz],
    [cx + hx + wx, y1, cz + hz + wz], [cx - hx + wx, y1, cz - hz + wz],
  ];
  const faces = [
    [0, 1, 2, 0, 2, 3], [4, 6, 5, 4, 7, 6],
    [0, 4, 5, 0, 5, 1], [1, 5, 6, 1, 6, 2],
    [2, 6, 7, 2, 7, 3], [3, 7, 4, 3, 4, 0],
  ];
  pushTris(B, v, faces.flat());
}

function beam(B, A, C, r) {
  // square-section beam between points
  const d = norm([C[0] - A[0], C[1] - A[1], C[2] - A[2]]);
  let up = Math.abs(d[1]) > 0.9 ? [1, 0, 0] : [0, 1, 0];
  const s = norm(cross(d, up));
  const t = cross(d, s);
  const h = r;
  const corners = (P, sgn) => [
    [P[0] + (s[0] * sgn[0] + t[0] * sgn[1]) * h, P[1] + (s[1] * sgn[0] + t[1] * sgn[1]) * h, P[2] + (s[2] * sgn[0] + t[2] * sgn[1]) * h],
  ];
  const q = [[1, 1], [1, -1], [-1, -1], [-1, 1]];
  const A8 = q.map((s2) => corners(A, s2)[0]);
  const C8 = q.map((s2) => corners(C, s2)[0]);
  const v = [...A8, ...C8];
  const faces = [];
  for (let i = 0; i < 4; i++) {
    const j = (i + 1) % 4;
    faces.push(i, 4 + i, 4 + j, i, 4 + j, j);
  }
  faces.push(0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7);
  pushTris(B, v, faces);
}

function column(B, A, C, r) { beam(B, A, C, r); }

function pier(B, baseC, topC, width, tx, tz) {
  const [bx, by, bz] = baseC;
  const w2 = width / 2;
  const nx = -tz, nz = tx;
  orientedBox(B,
    [bx + nx * -1 * 0 + tx * -0.8 - nx * w2 * 0 + -0.0, by, bz],
    [bx, by, bz]); // placeholder replaced below
  // single rectangular pier + cap: build directly
  const cx = (baseC[0] + topC[0]) / 2, cz = (baseC[2] + topC[2]) / 2;
  const px = nx * 0, pz = nz * 0;
  void px; void pz;
  const thickness = 1.2;
  const A = [cx - tx * thickness / 2 - nx * w2 * 0.12, baseC[1], cz - tz * thickness / 2 - nz * w2 * 0.12];
  const C = [cx + tx * thickness / 2 + nx * w2 * 0.12, topC[1], cz + tz * thickness / 2 + nz * w2 * 0.12];
  void w2;
  orientedBox(B, A, C);
}

function abutments(B, P, len, w, deckBottom, groundY, tx, tz) {
  void tx; void tz;
  orientedBox(B, P(-2.5, -w / 2 - 1, groundY - 0.5), P(0.5, w / 2 + 1, deckBottom));
  orientedBox(B, P(len - 0.5, -w / 2 - 1, groundY - 0.5), P(len + 2.5, w / 2 + 1, deckBottom));
}

function archRib(B, P, len, off, deckBottom, groundY, rise, tx, tz) {
  void tx; void tz;
  const springY = groundY + 1.2;
  let prev = null;
  for (let i = 0; i <= 24; i++) {
    const t = i / 24;
    const s = t * len;
    const y = springY + (deckBottom - 0.8 - springY) * Math.sin(Math.PI * Math.pow(t, 0.9)) * 1.0;
    const yy = Math.max(y, springY);
    const cur = P(s, off, yy);
    if (prev) beam(B, prev, cur, 0.7);
    prev = cur;
  }
  void rise;
}

function pushTris(B, verts, idx) {
  const base = B.count;
  // compute per-face normals (flat shading reads better for structure)
  for (let f = 0; f < idx.length; f += 3) {
    const a = verts[idx[f]], b = verts[idx[f + 1]], c = verts[idx[f + 2]];
    const n = norm(cross([b[0] - a[0], b[1] - a[1], b[2] - a[2]], [c[0] - a[0], c[1] - a[1], c[2] - a[2]]));
    for (const vi of [idx[f], idx[f + 1], idx[f + 2]]) {
      const p = verts[vi];
      B.positions.push(p[0], p[1], p[2]);
      B.normals.push(n[0], n[1], n[2]);
      B.uvs.push(0, 0);
      B.colors.push(1, 1, 1);
    }
    B.index.push(base + f, base + f + 1, base + f + 2);
  }
  B.count += idx.length;
}

function norm(a) { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; }
function cross(a, b) {
  return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}
