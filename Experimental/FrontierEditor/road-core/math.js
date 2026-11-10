// Road Editor — math utilities (2D XZ + Y height).
// No dependencies. Deterministic helpers for spline + topology work.

export function mulberry32(seed) {
  let a = seed >>> 0;
  return function () {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export function hashStr(s) {
  let h = 2166136261;
  for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); }
  return h >>> 0;
}

export const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
export const lerp = (a, b, t) => a + (b - a) * t;

export const v2 = {
  sub: (a, b) => ({ x: a.x - b.x, z: a.z - b.z }),
  add: (a, b) => ({ x: a.x + b.x, z: a.z + b.z }),
  scale: (a, s) => ({ x: a.x * s, z: a.z * s }),
  len: (a) => Math.hypot(a.x, a.z),
  norm: (a) => { const l = Math.hypot(a.x, a.z) || 1; return { x: a.x / l, z: a.z / l }; },
  perp: (a) => ({ x: -a.z, z: a.x }),
  dot: (a, b) => a.x * b.x + a.z * b.z,
  dist: (a, b) => Math.hypot(a.x - b.x, a.z - b.z),
};

export function angleOf(d) { return Math.atan2(d.z, d.x); }
export function angleDiff(a, b) {
  let d = a - b;
  while (d > Math.PI) d -= Math.PI * 2;
  while (d < -Math.PI) d += Math.PI * 2;
  return d;
}

// Centripetal Catmull-Rom through control points. The centripetal
// parametrization is loop- and overshoot-free at kinks and uneven spacing,
// so ribbons never fold back on themselves (uniform CR loops at tight ramp
// joins and snapped endpoints, flipping strip triangles).
export function catmullRom3(points, samplesPerSeg = 12) {
  if (points.length < 2) return points.slice();
  if (points.length === 2) {
    const out = [];
    for (let i = 0; i <= samplesPerSeg; i++) {
      const t = i / samplesPerSeg;
      out.push({
        x: lerp(points[0].x, points[1].x, t),
        y: lerp(points[0].y, points[1].y, t),
        z: lerp(points[0].z, points[1].z, t),
      });
    }
    return out;
  }
  const P = (i) => points[clamp(i, 0, points.length - 1)];
  const dt = (a, b) => Math.pow(Math.hypot(b.x - a.x, b.y - a.y, b.z - a.z), 0.5) || 1e-4;
  const out = [];
  for (let i = 0; i < points.length - 1; i++) {
    const p0 = P(i - 1), p1 = P(i), p2 = P(i + 1), p3 = P(i + 2);
    const t0 = 0, t1 = t0 + dt(p0, p1), t2 = t1 + dt(p1, p2), t3 = t2 + dt(p2, p3);
    for (let j = 0; j < samplesPerSeg; j++) {
      const t = t1 + ((t2 - t1) * j) / samplesPerSeg;
      const A1 = mix3(p0, p1, (t1 - t) / (t1 - t0), (t - t0) / (t1 - t0));
      const A2 = mix3(p1, p2, (t2 - t) / (t2 - t1), (t - t1) / (t2 - t1));
      const A3 = mix3(p2, p3, (t3 - t) / (t3 - t2), (t - t2) / (t3 - t2));
      const B1 = mix3(A1, A2, (t2 - t) / (t2 - t0), (t - t0) / (t2 - t0));
      const B2 = mix3(A2, A3, (t3 - t) / (t3 - t1), (t - t1) / (t3 - t1));
      out.push(mix3(B1, B2, (t2 - t) / (t2 - t1), (t - t1) / (t2 - t1)));
    }
  }
  out.push({ ...points[points.length - 1] });
  return out;
}

function mix3(a, b, fa, fb) {
  return { x: a.x * fa + b.x * fb, y: a.y * fa + b.y * fb, z: a.z * fa + b.z * fb };
}

// Arc-length resample to ~step spacing. Adds tangent + cumulative s to each sample.
export function resampleByArc(points, step = 2) {
  if (points.length < 2) return points.map((p) => ({ ...p, s: 0, tx: 1, tz: 0 }));
  const cum = [0];
  for (let i = 1; i < points.length; i++) {
    const dx = points[i].x - points[i - 1].x;
    const dy = points[i].y - points[i - 1].y;
    const dz = points[i].z - points[i - 1].z;
    cum.push(cum[i - 1] + Math.hypot(dx, dy, dz));
  }
  const total = cum[cum.length - 1] || 1;
  const n = Math.max(2, Math.round(total / step) + 1);
  const out = [];
  let seg = 0;
  for (let i = 0; i < n; i++) {
    const s = (i / (n - 1)) * total;
    while (seg < cum.length - 2 && cum[seg + 1] < s) seg++;
    const s0 = cum[seg], s1 = cum[seg + 1];
    const t = s1 > s0 ? (s - s0) / (s1 - s0) : 0;
    const a = points[seg], b = points[seg + 1];
    const p = { x: lerp(a.x, b.x, t), y: lerp(a.y, b.y, t), z: lerp(a.z, b.z, t), s };
    const d = { x: b.x - a.x, z: b.z - a.z };
    const l = Math.hypot(d.x, d.z) || 1;
    p.tx = d.x / l; p.tz = d.z / l;
    out.push(p);
  }
  return out;
}

// 2D segment intersection in XZ. Returns {t, u, x, z} or null.
export function segIntersect(p1, p2, p3, p4) {
  const d1x = p2.x - p1.x, d1z = p2.z - p1.z;
  const d2x = p4.x - p3.x, d2z = p4.z - p3.z;
  const denom = d1x * d2z - d1z * d2x;
  if (Math.abs(denom) < 1e-9) return null;
  const t = ((p3.x - p1.x) * d2z - (p3.z - p1.z) * d2x) / denom;
  const u = ((p3.x - p1.x) * d1z - (p3.z - p1.z) * d1x) / denom;
  if (t < -1e-6 || t > 1 + 1e-6 || u < -1e-6 || u > 1 + 1e-6) return null;
  return { t: clamp(t, 0, 1), u: clamp(u, 0, 1), x: p1.x + d1x * t, z: p1.z + d1z * t };
}

export function pointSegDist2D(p, a, b) {
  const abx = b.x - a.x, abz = b.z - a.z;
  const l2 = abx * abx + abz * abz || 1;
  let t = ((p.x - a.x) * abx + (p.z - a.z) * abz) / l2;
  t = clamp(t, 0, 1);
  const cx = a.x + abx * t, cz = a.z + abz * t;
  return { dist: Math.hypot(p.x - cx, p.z - cz), t, cx, cz };
}

// Polyline (control) intersection scan. Returns list of {edgeA, edgeB, segA, segB, t, u, x, z}.
export function findCrossings(polys, opts = {}) {
  const minGap = opts.minGap ?? 4; // ignore crossings closer than this to an existing one
  const found = [];
  const ids = [...polys.keys()];
  for (let ia = 0; ia < ids.length; ia++) {
    for (let ib = ia + 1; ib < ids.length; ib++) {
      const A = polys.get(ids[ia]), B = polys.get(ids[ib]);
      if (opts.skip && opts.skip(ids[ia], ids[ib])) continue;
      for (let i = 0; i < A.length - 1; i++) {
        for (let j = 0; j < B.length - 1; j++) {
          const hit = segIntersect(A[i], A[i + 1], B[j], B[j + 1]);
          if (!hit) continue;
          // skip near-shared endpoints (already connected)
          if (found.some((f) => Math.hypot(f.x - hit.x, f.z - hit.z) < minGap)) continue;
          found.push({ edgeA: ids[ia], edgeB: ids[ib], segA: i, segB: j, ...hit });
        }
      }
    }
  }
  return found;
}

// Chaikin corner cutting for rounded rectangles etc.
export function roundedRectPoints(cx, cz, w, d, r, seg = 6) {
  const pts = [];
  const corners = [
    { c: { x: cx + w / 2 - r, z: cz + d / 2 - r }, a0: 0 },
    { c: { x: cx - w / 2 + r, z: cz + d / 2 - r }, a0: Math.PI / 2 },
    { c: { x: cx - w / 2 + r, z: cz - d / 2 + r }, a0: Math.PI },
    { c: { x: cx + w / 2 - r, z: cz - d / 2 + r }, a0: Math.PI * 1.5 },
  ];
  for (const { c, a0 } of corners) {
    for (let i = 0; i <= seg; i++) {
      const a = a0 + (i / seg) * (Math.PI / 2);
      pts.push({ x: c.x + Math.cos(a) * r, y: 0, z: c.z + Math.sin(a) * r });
    }
  }
  return pts;
}

export function circlePoints(cx, cz, y, r, n = 48) {
  const pts = [];
  for (let i = 0; i < n; i++) {
    const a = (i / n) * Math.PI * 2;
    pts.push({ x: cx + Math.cos(a) * r, y, z: cz + Math.sin(a) * r });
  }
  return pts;
}
