// Candy Dragon anatomy — geometry generators.
//
// Nothing is assembled from boxes or spheres. The body is a parametric spine
// integrated through space, a real cross-section is swept along it with a
// propagated frame, and the head, limbs and wings are lofted/patched in the
// same style so the creature reads as one continuous sculpt.

import * as THREE from 'three';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import { clamp, lerp, smoothstep, mulberry32 } from './rng.js';

/** Merge geometry batches, normalising attributes so mixed sources combine. */
export function mergeClean(geos) {
  const list = geos.filter(Boolean);
  for (const g of list) {
    g.deleteAttribute('uv');
    g.deleteAttribute('uv1');
    if (!g.index) {
      const count = g.attributes.position.count;
      const idx = new Uint32Array(count);
      for (let i = 0; i < count; i++) idx[i] = i;
      g.setIndex(new THREE.BufferAttribute(idx, 1));
    }
  }
  const merged = mergeGeometries(list, false);
  if (!merged) throw new Error('mergeClean failed: incompatible attributes');
  return merged;
}

// ---------------------------------------------------------------------------
// Curve helpers
// ---------------------------------------------------------------------------

/** Catmull-Rom interpolation over [[t,v],...] keys (t ascending). */
export function curve1(keys) {
  const n = keys.length;
  return (t) => {
    t = clamp(t, keys[0][0], keys[n - 1][0]);
    let i = 0;
    while (i < n - 2 && t > keys[i + 1][0]) i++;
    const [t0, v0] = keys[i];
    const [t1, v1] = keys[i + 1];
    const f = (t - t0) / (t1 - t0 || 1);
    const vm = keys[Math.max(0, i - 1)][1];
    const vp = keys[Math.min(n - 1, i + 2)][1];
    const m0 = (v1 - vm) * 0.5;
    const m1 = (vp - v0) * 0.5;
    const f2 = f * f, f3 = f2 * f;
    return (
      (2 * f3 - 3 * f2 + 1) * v0 +
      (f3 - 2 * f2 + f) * m0 +
      (-2 * f3 + 3 * f2) * v1 +
      (f3 - f2) * m1
    );
  };
}

/**
 * Piecewise smoothstep keyframes. Unlike Catmull-Rom this never overshoots,
 * which matters for skull profiles where a bump reads as a tumour.
 */
export function smoothCurve(keys) {
  const n = keys.length;
  return (t) => {
    if (t <= keys[0][0]) return keys[0][1];
    if (t >= keys[n - 1][0]) return keys[n - 1][1];
    let i = 0;
    while (i < n - 2 && t > keys[i + 1][0]) i++;
    const [t0, v0] = keys[i];
    const [t1, v1] = keys[i + 1];
    const f = (t - t0) / (t1 - t0 || 1);
    const e = f * f * (3 - 2 * f);
    return v0 + (v1 - v0) * e;
  };
}

// ---------------------------------------------------------------------------
// Spine:  0 = base of skull, 1 = tail tip
// ---------------------------------------------------------------------------

const NECK_T = 0.0;
// Stations along the animal: t = 0 at the head joint, t = 1 at the tail tip.
// [t, x, y, z] — a rearing S-neck, deep chest, long low back, curling tail.
const STATIONS = [
  // Rearing pose. Tail braces on the ground behind, the body rises through a
  // heavy chest, and an S-neck carries the head high and thrust forward.
  // t,     x,      y,      z
  [0.0, -2.62, 0.34, 7.34],   // base of skull
  [0.04, -2.46, 0.3, 7.02],
  [0.08, -2.26, 0.26, 6.7],
  [0.13, -2.02, 0.22, 6.34],
  [0.18, -1.78, 0.2, 5.98],
  [0.24, -1.56, 0.19, 5.6],
  [0.3, -1.38, 0.19, 5.22],
  [0.36, -1.16, 0.21, 4.86],
  [0.42, -0.88, 0.25, 4.56],
  [0.48, -0.5, 0.3, 4.3],
  [0.54, -0.06, 0.36, 4.12],
  [0.6, 0.44, 0.44, 3.96],
  [0.66, 0.96, 0.54, 3.78],
  [0.72, 1.48, 0.4, 3.5],
  [0.78, 1.94, -0.05, 3.2],
  [0.84, 2.3, -0.72, 2.92],
  [0.9, 2.5, -1.5, 2.66],
  [0.95, 2.56, -2.3, 2.46],
  [0.98, 2.48, -3.0, 2.34],
  [1.0, 2.3, -3.6, 2.3],
];

// Neck is slim and sinuous, chest is a barrel, waist tucks, tail whips.
const radiiKeys = [
  [0.0, 0.42],
  [0.04, 0.42],
  [0.1, 0.42],
  [0.18, 0.44],
  [0.26, 0.48],
  [0.32, 0.58],
  [0.38, 0.72],
  [0.44, 0.92],
  [0.5, 1.04],
  [0.55, 1.1],
  [0.6, 1.08],
  [0.66, 0.98],
  [0.72, 0.82],
  [0.78, 0.64],
  [0.82, 0.5],
  [0.88, 0.4],
  [0.93, 0.3],
  [0.97, 0.18],
  [1.0, 0.03],
];

export const SPINE = {
  t0: 0.0,
  t1: 1.0,
  x: curve1(STATIONS.map(([t, x]) => [t, x])),
  y: curve1(STATIONS.map(([t, , y]) => [t, y])),
  z: curve1(STATIONS.map(([t, , , z]) => [t, z])),
  radius: curve1(radiiKeys),
};

/** Half-width / half-height multipliers along the body. */
const widthScale = curve1([
  [0.0, 0.92],
  [0.2, 0.9],
  [0.4, 0.88],
  [0.55, 0.96],
  [0.7, 0.94],
  [1.0, 0.84],
]);
const heightScale = curve1([
  [0.0, 1.0],
  [0.2, 1.05],
  [0.36, 1.16],
  [0.5, 1.3],
  [0.6, 1.28],
  [0.72, 1.14],
  [0.86, 1.0],
  [1.0, 0.9],
]);

function spinePos(t) {
  return new THREE.Vector3(SPINE.x(t), SPINE.y(t), SPINE.z(t));
}

/** Arc-length reparameterisation table: t -> cumulative length. */
const arc = (() => {
  const N = 800;
  const table = [];
  let s = 0;
  let prev = spinePos(SPINE.t0);
  table.push([SPINE.t0, 0]);
  for (let i = 1; i <= N; i++) {
    const t = SPINE.t0 + ((SPINE.t1 - SPINE.t0) * i) / N;
    const p = spinePos(t);
    s += p.distanceTo(prev);
    prev = p;
    table.push([t, s]);
  }
  return { table, length: s };
})();

export const SPINE_LENGTH = arc.length;

/** Parameter for a given fraction of body length. */
export function sToT(fraction) {
  const target = clamp(fraction, 0, 1) * arc.length;
  const { table } = arc;
  for (let i = 1; i < table.length; i++) {
    if (table[i][1] >= target) {
      const f = (target - table[i - 1][1]) / (table[i][1] - table[i - 1][1] || 1);
      return lerp(table[i - 1][0], table[i][0], f);
    }
  }
  return SPINE.t1;
}

// ---------------------------------------------------------------------------
// Live pose: breathing, S-sway, head turn, tail flick
// ---------------------------------------------------------------------------

/**
 * @param {number} t spine parameter
 * @param {number} time seconds
 * @param {{idle?:number}} opts  idle 0..1 (0 = frozen rig pose)
 */
export function bodySample(t, time, opts = {}) {
  const idle = opts.idle === undefined ? 1 : opts.idle;

  const base = spinePos(t);
  const h = 0.004;
  const tangent = spinePos(t + h).sub(spinePos(t - h)).normalize();

  // --- breathing -----------------------------------------------------------
  const breathe = 1 + 0.022 * Math.sin(time * 1.1 - t * 3.4) * idle *
    smoothstep(0.06, 0.28, t) * (1 - smoothstep(0.55, 0.8, t));

  // --- serpentine sway, accumulating toward the tail -----------------------
  const swayPhase = Math.sin(time * 0.9 - t * 4.6);
  const swayAmp = (0.06 + 0.34 * smoothstep(0.45, 1.0, t)) * idle;
  let lateral = swayPhase * swayAmp;
  // head sways very slightly and turns to breathe
  const headTurn = Math.sin(time * 0.47) * 0.26 * idle;

  const pos = base.clone();
  pos.y += lateral;
  pos.z += 0.06 * Math.sin(time * 0.62 - t * 2.4) * smoothstep(0.4, 1.0, t) * idle;

  // Tangent follows the local lateral bend so details stay glued to the body.
  const yaw = -Math.cos(time * 0.9 - t * 4.6) * swayAmp * 0.42 + headTurn * (1 - smoothstep(0.0, 0.45, t)) * 0.5;
  const cosY = Math.cos(yaw), sinY = Math.sin(yaw);
  const tang = new THREE.Vector3(
    tangent.x * cosY - tangent.y * sinY,
    tangent.x * sinY + tangent.y * cosY,
    tangent.z
  ).normalize();

  const r = SPINE.radius(t) * breathe;
  return {
    t,
    pos,
    tangent: tang,
    rx: r * widthScale(t),
    rz: r * heightScale(t),
    r,
    breathe,
    lateral,
    headTurn,
    idle,
  };
}

/**
 * Head transform. The neck is steep, but a proud dragon carries the skull
 * nearly level, so the pitch of the spine tangent is clamped and the residual
 * angle becomes a slight upward tilt.
 */
export function headTransform(time, opts = {}) {
  const { sample, side, up, tangent } = bodyFrame(0, time, opts);
  const horiz = new THREE.Vector3(tangent.x, tangent.y, 0).normalize();
  const pitch = Math.asin(clamp(tangent.z, -1, 1));
  const capped = clamp(pitch * 0.22 - 0.03, -0.22, 0.22);
  const axis = horiz
    .clone()
    .multiplyScalar(Math.cos(capped))
    .add(new THREE.Vector3(0, 0, Math.sin(capped)))
    .normalize();
  const zAxis = up.clone();
  // Head geometry is authored with the muzzle toward local -X.
  const xAxis = axis.clone().negate();
  const yAxis = new THREE.Vector3().crossVectors(zAxis, xAxis).normalize();
  xAxis.crossVectors(yAxis, zAxis).normalize();
  const quaternion = new THREE.Quaternion().setFromRotationMatrix(
    new THREE.Matrix4().makeBasis(xAxis, yAxis, zAxis)
  );
  return {
    pos: sample.pos,
    quaternion,
    axis,
    up: zAxis,
    side: yAxis,
    sample,
    pitch: capped,
  };
}

/**
 * Cheap per-frame surface sampler.
 *
 * The sugar scatter needs thousands of surface points every frame; recomputing
 * a spine frame for each one is far too slow. This caches frames on a bucket
 * grid once per frame and interpolates between neighbours.
 */
export function makeSurfaceSampler(time, opts = {}, buckets = 96) {
  const frames = [];
  for (let i = 0; i <= buckets; i++) {
    const t = i / buckets;
    const { sample, side, up } = bodyFrame(t, time, opts);
    frames.push({
      t, pos: sample.pos, rx: sample.rx, rz: sample.rz,
      side, up, tangent: sample.tangent,
    });
  }
  const tmpPos = new THREE.Vector3();
  const tmpNormal = new THREE.Vector3();

  return function sample(t, angle, out = { pos: new THREE.Vector3(), normal: new THREE.Vector3() }) {
    const x = clamp(t, 0, 1) * buckets;
    const i = Math.min(buckets - 1, Math.floor(x));
    const f = x - i;
    const a = frames[i], b = frames[i + 1];

    // Interpolate the frame (positions and radii); side/up come from the near
    // bucket, which is visually identical at this density.
    tmpPos.copy(a.pos).lerp(b.pos, f);
    const rx = a.rx + (b.rx - a.rx) * f;
    const rz = a.rz + (b.rz - a.rz) * f;
    const side = f < 0.5 ? a.side : b.side;
    const up = f < 0.5 ? a.up : b.up;

    const sx = Math.sin(angle), cy = Math.cos(angle);
    const keel = Math.pow(Math.max(0, cy), 3);
    const px = Math.sign(sx) * Math.pow(Math.abs(sx), 0.86) * (1 - 0.26 * keel);
    const py = (cy < 0 ? -Math.pow(-cy, 0.95) : Math.pow(cy, 0.84)) + 0.09 * keel * (1 - Math.abs(px));

    out.pos.copy(tmpPos)
      .addScaledVector(side, px * rx)
      .addScaledVector(up, py * rz);
    out.normal.copy(side).multiplyScalar(px / Math.max(1e-4, rx))
      .addScaledVector(up, py / Math.max(1e-4, rz)).normalize();
    out.tangent = f < 0.5 ? a.tangent : b.tangent;
    void tmpNormal;
    return out;
  };
}

/** Orthonormal frame of the body: tangent along the spine, up = dorsal. */
export function bodyFrame(t, time, opts = {}) {
  const sample = bodySample(t, time, opts);
  const refUp = new THREE.Vector3(0, 0, 1);
  const side = new THREE.Vector3().crossVectors(sample.tangent, refUp);
  if (side.lengthSq() < 1e-9) side.set(0, 1, 0);
  side.normalize();
  const up = new THREE.Vector3().crossVectors(side, sample.tangent).normalize();
  return { sample, side, up, tangent: sample.tangent };
}

/**
 * Cross-section offset (side, up) for ring angle a.
 * Superelliptic flanks, flattened belly, keeled dorsal ridge.
 */
export function crossSection(a, depthBias = 0) {
  const sx = Math.sin(a), cy = Math.cos(a);
  const keel = Math.pow(Math.max(0, cy), 3);
  const px = Math.sign(sx) * Math.pow(Math.abs(sx), 0.86) * (1 - 0.26 * keel);
  let py =
    (cy < 0 ? -Math.pow(-cy, 0.95) : Math.pow(cy, 0.84)) + 0.09 * keel * (1 - Math.abs(px));
  // Chest keel: pull the belly down and pinch the flanks.
  if (py < 0) py *= 1 + depthBias * 0.45;
  return [px, py];
}

/** World-space point on the body surface; angle 0 = dorsal, PI = belly. */
export function surfacePoint(t, angle, time, opts = {}) {
  const { sample, side, up } = bodyFrame(t, time, opts);
  const depthBias =
    Math.exp(-Math.pow((t - 0.47) / 0.16, 2)) * 0.55 + Math.exp(-Math.pow((t - 0.28) / 0.12, 2)) * 0.3;
  const [px, py] = crossSection(angle, depthBias);
  const pos = sample.pos
    .clone()
    .addScaledVector(side, px * sample.rx)
    .addScaledVector(up, py * sample.rz);
  const normal = side
    .clone()
    .multiplyScalar(px / Math.max(1e-4, sample.rx))
    .addScaledVector(up, (py / Math.max(1e-4, sample.rz)))
    .normalize();
  return { pos, normal, sample, side, up, tangent: sample.tangent };
}

// ---------------------------------------------------------------------------
// Body tube
// ---------------------------------------------------------------------------

export const RING_SEGMENTS = 112;

export function bodyRing(t, time, ringSegments = RING_SEGMENTS, opts = {}) {
  const { sample, side, up } = bodyFrame(t, time, opts);
  const pts = [];
  const sugarWave = opts.sugarRipple === false ? 0 : 1;
  // The rib cage is deep between the shoulders and the waist.
  const depthBias =
    Math.exp(-Math.pow((t - 0.47) / 0.16, 2)) * 0.55 + Math.exp(-Math.pow((t - 0.28) / 0.12, 2)) * 0.3;
  // Muscle masses: shoulder and haunch bulges, so the torso has power.
  const muscle = (centre, width, gain) =>
    Math.exp(-Math.pow((t - centre) / width, 2)) * gain;
  const shoulder = muscle(0.4, 0.1, 0.26);
  const haunch = muscle(0.66, 0.09, 0.3);
  for (let i = 0; i < ringSegments; i++) {
    const a = (i / ringSegments) * Math.PI * 2;
    const [px, py] = crossSection(a, depthBias);
    // Flank-local bulge: strongest on the sides, nil on the spine/belly line.
    const sideWeight = Math.pow(Math.abs(Math.sin(a)), 1.4);
    const bulge = 1 + (shoulder + haunch) * sideWeight;
    const ripple = 1 + 0.028 * sugarWave * Math.sin(a * 6 + t * 26 - time * 1.2);
    pts.push(
      sample.pos
        .clone()
        .addScaledVector(side, px * sample.rx * ripple * bulge)
        .addScaledVector(up, py * sample.rz * ripple * bulge)
    );
  }
  return { pts, sample, side, up, tangent: sample.tangent };
}

/**
 * Candy-hide two-tone: raspberry on the back, strawberry cream on the throat
 * and belly, blended with a soft band. Returned as linear RGB.
 */
export function hideColor(t, angle, out = [0, 0, 0]) {
  // cy: +1 on the spine, -1 on the belly.
  const cy = Math.cos(angle);
  // Cream runs only along the true underside, fading out before the flanks.
  const belly = smoothstep(-0.45, -0.97, cy);
  // The throat and chin are pale too, so the underside line reads continuous.
  const throat = smoothstep(0.3, 0.02, t) * smoothstep(-0.1, -0.8, cy) * 0.8;
  const k = Math.min(1, Math.max(belly, throat));
  // Raspberry fondant, warmed toward the flanks.
  const back = [1.0, 0.02, 0.2];
  // Strawberry cream underside.
  const cream = [1.0, 0.8, 0.9];
  out[0] = lerp(back[0], cream[0], k);
  out[1] = lerp(back[1], cream[1], k);
  out[2] = lerp(back[2], cream[2], k);
  return out;
}

/**
 * Real-time body update. Mirrors buildBodyGeometry's layout exactly but writes
 * into an existing buffer, so the creature can breathe and sway every frame
 * without rebuilding geometry.
 */
export function updateBodyGeometry(geo, time, opts = {}) {
  const rings = geo.userData.ringsCount;
  const seg = geo.userData.segments;
  const pos = geo.attributes.position;
  const col = geo.attributes.color;
  const col3 = [0, 0, 0];
  const side = new THREE.Vector3();
  const up = new THREE.Vector3();
  const tmp = new THREE.Vector3();
  let w = 0;

  for (let r = 0; r < rings; r++) {
    const t = r / (rings - 1);
    const sample = bodySample(t, time, opts);
    // Frame, inlined to avoid allocating a frame object per ring.
    const tan = sample.tangent;
    side.set(tan.y, -tan.x, 0);
    if (side.lengthSq() < 1e-9) side.set(0, 1, 0);
    side.normalize();
    up.crossVectors(side, tan).normalize();

    const depthBias =
      Math.exp(-Math.pow((t - 0.47) / 0.16, 2)) * 0.55 +
      Math.exp(-Math.pow((t - 0.28) / 0.12, 2)) * 0.3;
    const shoulder = Math.exp(-Math.pow((t - 0.4) / 0.1, 2)) * 0.26;
    const haunch = Math.exp(-Math.pow((t - 0.66) / 0.09, 2)) * 0.3;
    const muscle = shoulder + haunch;

    for (let i = 0; i < seg; i++) {
      const a = (i / seg) * Math.PI * 2;
      const sx = Math.sin(a), cy = Math.cos(a);
      const keel = Math.pow(Math.max(0, cy), 3);
      const px = Math.sign(sx) * Math.pow(Math.abs(sx), 0.86) * (1 - 0.26 * keel);
      let py =
        (cy < 0 ? -Math.pow(-cy, 0.95) : Math.pow(cy, 0.84)) + 0.09 * keel * (1 - Math.abs(px));
      if (py < 0) py *= 1 + depthBias * 0.45;
      const bulge = 1 + muscle * Math.pow(Math.abs(sx), 1.4);
      const ripple = 1 + 0.028 * Math.sin(a * 6 + t * 26 - time * 1.2);
      const rx = sample.rx * ripple * bulge;
      const rz = sample.rz * ripple * bulge;
      tmp.copy(sample.pos)
        .addScaledVector(side, px * rx)
        .addScaledVector(up, py * rz);
      pos.setXYZ(w, tmp.x, tmp.y, tmp.z);
      hideColor(t, a, col3);
      if (col) col.setXYZ(w, col3[0], col3[1], col3[2]);
      w++;
    }
  }
  // Tail tip
  const tip = bodySample(1.0, time, opts).pos;
  pos.setXYZ(w, tip.x, tip.y, tip.z);
  if (col) { hideColor(1, 0, col3); col.setXYZ(w, col3[0], col3[1], col3[2]); }
  w++;
  // Neck root cap
  const rootP = bodySample(0.0, time, opts).pos;
  pos.setXYZ(w, rootP.x, rootP.y, rootP.z);
  if (col) { hideColor(0, 0, col3); col.setXYZ(w, col3[0], col3[1], col3[2]); }

  pos.needsUpdate = true;
  if (col) col.needsUpdate = true;
  // Deforming geometry: recompute normals in place (the only CPU pass needed)
  // and let three.js handle bounds.
  geo.computeVertexNormals();
  return geo;
}

export function buildBodyGeometry(time = 0, opts = {}) {
  const optsS = { idle: opts.idle };
  const rings = opts.rings || 240;
  const seg = opts.segments || RING_SEGMENTS;
  const positions = [];
  const uvs = [];
  const colors = [];
  const indices = [];
  const tStart = 0.0, tEnd = 1.0;
  const col = [0, 0, 0];

  for (let r = 0; r < rings; r++) {
    const f = r / (rings - 1);
    // Slight bias toward the head/tail tips where curvature is highest.
    const t = tStart + (tEnd - tStart) * f;
    const ring = bodyRing(t, time, seg, optsS);
    for (let i = 0; i < seg; i++) {
      const p = ring.pts[i];
      positions.push(p.x, p.y, p.z);
      uvs.push(t * 9, (i / seg) * 2);
      const angle = (i / seg) * Math.PI * 2;
      hideColor(t, angle, col);
      colors.push(col[0], col[1], col[2]);
    }
  }
  for (let r = 0; r < rings - 1; r++) {
    for (let i = 0; i < seg; i++) {
      const a = r * seg + i;
      const b = r * seg + ((i + 1) % seg);
      const c = a + seg;
      const d = b + seg;
      indices.push(a, c, b, b, c, d);
    }
  }
  // Nose-side cap (hidden inside the head) and tail tip.
  const tailRing = bodyRing(0.999, time, seg, optsS);
  const tip = bodySample(1.0, time, optsS).pos;
  const tipIndex = positions.length / 3;
  positions.push(tip.x, tip.y, tip.z);
  uvs.push(9, 1);
  hideColor(1.0, 0, col);
  colors.push(col[0], col[1], col[2]);
  const off = (rings - 1) * seg;
  for (let i = 0; i < seg; i++) {
    indices.push(off + i, off + ((i + 1) % seg), tipIndex);
  }
  const headRing = bodyRing(0.001, time, seg, optsS);
  const start = bodySample(0.0, time, optsS).pos;
  const startIndex = positions.length / 3;
  positions.push(start.x, start.y, start.z);
  uvs.push(0, 0.5);
  hideColor(0, 0, col);
  colors.push(col[0], col[1], col[2]);
  for (let i = 0; i < seg; i++) {
    indices.push(i, startIndex, (i + 1) % seg);
  }

  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geo.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
  geo.setAttribute('color', new THREE.Float32BufferAttribute(colors, 3));
  geo.setIndex(indices);
  geo.computeVertexNormals();
  geo.userData.ringsCount = rings;
  geo.userData.segments = seg;
  // Live animation writes positions every frame.
  geo.attributes.position.setUsage(THREE.DynamicDrawUsage);
  if (geo.attributes.color) geo.attributes.color.setUsage(THREE.DynamicDrawUsage);
  void tailRing; void headRing;
  return geo;
}

// ---------------------------------------------------------------------------
// Head
// ---------------------------------------------------------------------------

/**
 * Head is lofted from the skull base forward along -X.
 * Upper head (cranium + muzzle) and a hinged lower jaw are separate shells so
 * the mouth can open and show fangs.
 */
export function buildHeadGeometry(part = 'upper', scale = 1.0) {
  const upper = part === 'upper';
  const rings = upper ? 92 : 64;
  const seg = upper ? 88 : 68;
  const positions = [];
  const indices = [];
  // Compact, powerful skull: a tall cranium block, then a stepped muzzle.
  const length = 2.3 * scale;

  const widthAt = smoothCurve(
    upper
      ? [
          [0, 0.3], [0.08, 0.42], [0.16, 0.5], [0.26, 0.53], [0.34, 0.52],
          [0.4, 0.42], [0.46, 0.33], [0.6, 0.31], [0.74, 0.3], [0.84, 0.27],
          [0.93, 0.18], [1, 0.035],
        ]
      : [
          [0, 0.3], [0.1, 0.4], [0.2, 0.44], [0.3, 0.42], [0.44, 0.36],
          [0.6, 0.32], [0.76, 0.28], [0.88, 0.2], [0.96, 0.1], [1, 0.03],
        ]
  );
  const heightAt = smoothCurve(
    upper
      ? [
          [0, 0.34], [0.08, 0.48], [0.16, 0.57], [0.26, 0.6], [0.34, 0.56],
          [0.4, 0.44], [0.46, 0.32], [0.58, 0.3], [0.72, 0.3], [0.83, 0.32],
          [0.92, 0.24], [1, 0.035],
        ]
      : [
          [0, 0.26], [0.1, 0.34], [0.22, 0.37], [0.36, 0.35], [0.52, 0.31],
          [0.7, 0.26], [0.84, 0.2], [0.94, 0.11], [1, 0.03],
        ]
  );

  for (let r = 0; r < rings; r++) {
    const u = r / (rings - 1);
    const x = -u * length;
    const w = widthAt(u) * scale;
    const hgt = heightAt(u) * scale;
    for (let i = 0; i < seg; i++) {
      const a = (i / seg) * Math.PI * 2;
      const sx = Math.sin(a), cy = Math.cos(a);
      // Square off the flanks slightly so the head has planes, not a sausage.
      const boxiness = upper ? lerp(1.0, 1.35, smoothstep(0.2, 0.7, u)) : 1.2;
      const ox = Math.sign(sx) * Math.pow(Math.abs(sx), 0.9);
      let oy;
      if (upper) {
        // Dome above the mouth line, shallow palate beneath it.
        oy = cy >= 0 ? Math.pow(cy, 0.78) : -0.1 * Math.pow(-cy, 0.6);
        // Brow ridge shading the eye.
        const brow = Math.exp(-Math.pow((u - 0.26) / 0.1, 2)) * Math.max(0, cy) * 0.14;
        // A slight muzzle shelf so the nose is squared, not pointed.
        const shelf = smoothstep(0.62, 0.95, u) * Math.max(0, cy) * 0.05;
        oy += brow + shelf;
      } else {
        // Chin dome below the mouth line, flat mouth floor above it.
        oy = cy <= 0 ? -Math.pow(-cy, 0.8) : 0.08 * Math.pow(cy, 0.6);
      }
      const yy = ox * w;
      const zz = oy * hgt;
      // Sugar-crystal faceting on the muzzle only.
      // A whisper of downward cant at the nose so the muzzle isn't a dowel.
      const droop = -0.045 * smoothstep(0.55, 1.0, u) * scale;
      // Raised nose bridge along the centreline of the muzzle.
      const ridge = upper && Math.abs(ox) < 0.4
        ? smoothstep(0.28, 0.75, u) * (1 - smoothstep(0.86, 1.0, u)) * (1 - Math.abs(ox) / 0.4) * 0.09 * scale
        : 0;
      const facet = 1 + 0.014 * Math.sin(a * 8 + u * 26) * smoothstep(0.35, 0.9, u);
      positions.push(x, yy * facet, zz * facet + (upper ? 0.02 : -0.02) * scale + droop + ridge);
    }
  }
  for (let r = 0; r < rings - 1; r++) {
    for (let i = 0; i < seg; i++) {
      const a = r * seg + i;
      const b = r * seg + ((i + 1) % seg);
      const c = a + seg;
      const d = b + seg;
      indices.push(a, b, c, b, d, c);
    }
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geo.setIndex(indices);
  geo.computeVertexNormals();
  geo.computeBoundingSphere();
  geo.userData.length = length;
  return geo;
}

/**
 * Mouth cavity: a dark shell that fills the gap between the upper head and the
 * jaw so teeth sit inside a mouth instead of floating on the cheek.
 */
export function buildMouthGeometry(scale = 1.0) {
  const positions = [];
  const indices = [];
  const seg = 26;
  const rings = 22;
  const len = 1.95 * scale;
  for (let r = 0; r < rings; r++) {
    const u = r / (rings - 1);
    const x = -u * len;
    const w = (0.4 - 0.31 * Math.pow(u, 1.4)) * scale;
    const top = 0.06 * scale;
    const bottom = -0.16 * scale;
    for (let i = 0; i < seg; i++) {
      const a = (i / seg) * Math.PI * 2;
      const cy = Math.cos(a);
      const y = Math.sign(Math.sin(a)) * Math.pow(Math.abs(Math.sin(a)), 0.7) * w;
      const z = cy < 0 ? bottom * Math.abs(cy) ** 0.8 : top * cy;
      positions.push(x, y, z);
    }
  }
  for (let r = 0; r < rings - 1; r++) {
    for (let i = 0; i < seg; i++) {
      const a = r * seg + i, b = r * seg + ((i + 1) % seg), c = a + seg, d = b + seg;
      indices.push(a, b, c, b, d, c);
    }
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geo.setIndex(indices);
  geo.computeVertexNormals();
  return geo;
}

/**
 * Candy horn: dense spiral sugar stick with a wide base that tapers to a point,
 * swept back along `bend` for a proud, premium silhouette.
 */
export function candyHorn(root, dir, bend, length, radius, opts = {}) {
  const rings = opts.rings ?? 5;
  const pts = [];
  for (let i = 0; i <= 12; i++) {
    const f = i / 12;
    const p = root
      .clone()
      .addScaledVector(dir, length * f)
      .addScaledVector(bend, Math.pow(f, 1.8) * length * (opts.curve ?? 0.42));
    pts.push(p);
  }
  return tubeAlong(pts, radius, radius * 0.03, opts.radial ?? 14, (f, a, rad) =>
    rad * (1 + 0.075 * Math.sin(a * rings + f * 26)) * (1 - 0.15 * Math.pow(f, 3))
  );
}

// ---------------------------------------------------------------------------
// Wings: polar scalloped membrane + candy bones
// ---------------------------------------------------------------------------

/**
 * Wing in local space: shoulder at origin, +X outboard, +Y trailing (toward the
 * tail), +Z up.
 *
 * Built as a ruled sail between a leading-edge curve (shoulder -> elbow ->
 * wrist -> tip) and a scalloped trailing-edge curve (wing tip -> concave bays
 * between finger tips -> hip root). That gives real dragon planform: swept
 * leading edge, fans of fingers, and bays bitten out of the trailing edge.
 */
export function buildWing(options = {}) {
  const span = options.span || 6.0;
  const shoulder = new THREE.Vector3(0, 0.3, 0.05);
  const elbow = new THREE.Vector3(span * 0.19, 0.42, span * 0.2);
  const wrist = new THREE.Vector3(span * 0.44, 0.16, span * 0.4);
  const tip0 = new THREE.Vector3(span * 1.0, -span * 0.04, span * 0.46);
  const hip = new THREE.Vector3(-0.1, span * 0.26, -span * 0.05);

  const tipPoints = [
    tip0,
    new THREE.Vector3(span * 0.9, span * 0.16, span * 0.36),
    new THREE.Vector3(span * 0.74, span * 0.26, span * 0.28),
    new THREE.Vector3(span * 0.52, span * 0.32, span * 0.2),
    new THREE.Vector3(span * 0.28, span * 0.34, span * 0.12),
  ];

  const leading = new THREE.CatmullRomCurve3([shoulder, elbow, wrist, tip0], false, 'catmullrom', 0.5);

  const trailingPts = [];
  for (let i = 0; i < tipPoints.length; i++) {
    trailingPts.push(tipPoints[i].clone());
    if (i < tipPoints.length - 1) {
      for (let j = 1; j <= 4; j++) {
        const f = j / 5;
        const a = tipPoints[i], b = tipPoints[i + 1];
        const p = a.clone().lerp(b, f);
        // Concave bay toward the wrist.
        p.addScaledVector(wrist.clone().sub(p), 0.26 * Math.sin(f * Math.PI));
        trailingPts.push(p);
      }
    }
  }
  // Root of the sail runs to the hip.
  for (let j = 1; j <= 8; j++) trailingPts.push(tipPoints[tipPoints.length - 1].clone().lerp(hip, j / 8));
  const trailing = new THREE.CatmullRomCurve3(trailingPts, false, 'catmullrom', 0.35);

  const U = 64; // chordwise stations
  const V = 16; // 0 = leading edge, 1 = trailing edge
  const K = 5; // through-thickness shells (front..back)
  const thicknessProfile = (u, v) => {
    // Taffy sheet: thick along the leading edge and the finger struts,
    // thinning to a translucent veil at the trailing bays.
    const lead = 0.085 * Math.pow(1 - v, 1.2);
    const body = 0.05 * Math.sin(Math.PI * Math.min(1, v * 1.05));
    const tipFade = 1 - 0.55 * Math.pow(u, 2.2);
    return (lead + body) * tipFade * (0.75 + 0.25 * Math.sin(Math.PI * u));
  };

  const positions = [];
  const uvs = [];
  const indices = [];
  const grid = [];
  for (let k = 0; k <= K; k++) {
    const shell = [];
    for (let i = 0; i <= U; i++) {
      const u = i / U;
      const L = leading.getPointAt(u);
      const T = trailing.getPointAt(1 - u);
      for (let j = 0; j <= V; j++) {
        const v = j / V;
        const p = L.clone().lerp(T, v);
        // Camber: a full sail, not a flat card.
        const billow = 0.62 * Math.sin(Math.PI * Math.min(1, v * 1.02)) * Math.sin(Math.PI * u * 0.92);
        p.z += billow;
        p.z -= 0.3 * v * v * (1 - u) * u;
        p.y += 0.2 * Math.sin(Math.PI * v) * Math.sin(Math.PI * u);
        // Fine sugar ripple.
        p.z += 0.03 * Math.sin(v * 9 + u * 22);
        // Inflate across the thickness axis.
        const th = thicknessProfile(u, v);
        const f = (k / K) * 2 - 1; // -1..1
        p.z += f * th;
        // Round the rim: pinch the sheet at its edges.
        const pinch = Math.sqrt(Math.max(0, 1 - f * f * 0.15));
        p.x *= pinch > 0 ? 1 : 1;
        shell.push(p);
        positions.push(p.x, p.y, p.z);
        uvs.push(u, v);
      }
    }
    grid.push(shell);
  }
  const rowLen = V + 1;
  const layerLen = (U + 1) * rowLen;
  for (let k = 0; k < K; k++) {
    for (let i = 0; i < U; i++) {
      for (let j = 0; j < V; j++) {
        const a = k * layerLen + i * rowLen + j;
        const b = a + 1;
        const c = a + rowLen;
        const d = c + 1;
        indices.push(a, c, b, b, c, d);
      }
    }
  }
  const membrane = new THREE.BufferGeometry();
  membrane.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  membrane.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
  membrane.setIndex(indices);
  membrane.computeVertexNormals();

  const bones = [];
  bones.push({ pts: [shoulder.clone(), elbow.clone(), wrist.clone()], r0: 0.24, r1: 0.13, kind: 'arm' });
  bones.push({ pts: [elbow.clone(), hip.clone()], r0: 0.15, r1: 0.08, kind: 'arm' });
  tipPoints.forEach((tip, i) => {
    const pts = [];
    for (let k = 0; k <= 12; k++) {
      const f = k / 12;
      const p = wrist.clone().lerp(tip, Math.pow(f, 0.92));
      p.z += 0.35 * Math.sin(Math.PI * f);
      pts.push(p);
    }
    bones.push({ pts, r0: 0.1, r1: 0.02, kind: 'finger', index: i });
  });

  // Rim tube path: the outer wing edge, used for a candy-cane leading edge.
  const rimPath = [];
  for (let i = 0; i <= 60; i++) {
    const u = i / 60;
    rimPath.push(leading.getPointAt(u));
  }
  const trailingRim = [];
  for (let i = 0; i <= 60; i++) {
    trailingRim.push(trailing.getPointAt(i / 60));
  }

  return { membrane, bones, tipPoints, wrist, shoulder, elbow, hip, span, rimPath, trailingRim };
}

export function mirrorX(geo) {
  const pos = geo.attributes.position;
  for (let i = 0; i < pos.count; i++) pos.setX(i, -pos.getX(i));
  const idx = geo.index;
  if (idx) {
    for (let i = 0; i < idx.count; i += 3) {
      const t = idx.getX(i + 1);
      idx.setX(i + 1, idx.getX(i + 2));
      idx.setX(i + 2, t);
    }
    idx.needsUpdate = true;
  }
  pos.needsUpdate = true;
  geo.computeVertexNormals();
  return geo;
}

/** Tapered tube along a polyline (used for bones, horns, spikes, tails). */
export function tubeAlong(pts, r0, r1, radial = 10, deform = null) {
  const curve = new THREE.CatmullRomCurve3(pts);
  const frames = curve.computeFrenetFrames(radial > 0 ? 60 : 10, false);
  const segs = 56;
  const positions = [];
  const indices = [];
  const up = new THREE.Vector3(0, 0, 1);
  for (let i = 0; i <= segs; i++) {
    const f = i / segs;
    const p = curve.getPointAt(f);
    const tan = curve.getTangentAt(f);
    const side = new THREE.Vector3().crossVectors(tan, up);
    if (side.lengthSq() < 1e-8) side.set(1, 0, 0);
    side.normalize();
    const upv = new THREE.Vector3().crossVectors(side, tan).normalize();
    const rad = lerp(r0, r1, Math.pow(f, 0.85));
    for (let j = 0; j < radial; j++) {
      const a = (j / radial) * Math.PI * 2;
      let rr = rad;
      if (deform) rr = deform(f, a, rad);
      positions.push(
        p.x + side.x * Math.cos(a) * rr + upv.x * Math.sin(a) * rr,
        p.y + side.y * Math.cos(a) * rr + upv.y * Math.sin(a) * rr,
        p.z + side.z * Math.cos(a) * rr + upv.z * Math.sin(a) * rr
      );
    }
  }
  for (let i = 0; i < segs; i++) {
    for (let j = 0; j < radial; j++) {
      const a = i * radial + j;
      const b = i * radial + ((j + 1) % radial);
      const c = a + radial;
      const d = b + radial;
      indices.push(a, c, b, b, c, d);
    }
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geo.setIndex(indices);
  geo.computeVertexNormals();
  void frames;
  return geo;
}

export { curve1 as keyCurve, smoothstep, mulberry32 };

// ---------------------------------------------------------------------------
// Limbs
// ---------------------------------------------------------------------------

/**
 * Leg chain, built in world space at rest and skinned to the body station `t`
 * so it sways with the torso. Fore legs are held forward and slightly raised
 * (the creature is airborne); hind legs are braced, digitigrade.
 */
export function legChain(t, side, kind = 'fore') {
  const { sample, side: sideAxis, up, tangent } = bodyFrame(t, 0, { idle: 0 });
  const S = (x, y, z) =>
    sample.pos
      .clone()
      .addScaledVector(sideAxis, x * side)
      .addScaledVector(tangent, y)
      .addScaledVector(up, z);

  const r = sample.r;
  let joints, radii;
  if (kind === 'fore') {
    // Forelimbs reach forward, elbows tucked, claws spread and grasping.
    joints = [
      S(r * 0.72, -0.06, -0.1),
      S(r * 1.16, 0.26, -0.66),
      S(r * 1.12, -0.3, -1.12),
      S(r * 0.98, -0.72, -1.34),
      S(r * 0.9, -0.96, -1.42),
    ];
    radii = [0.46, 0.36, 0.28, 0.23, 0.19];
  } else {
    // Hind limbs carry the rearing weight: massive thigh, hock, planted foot.
    joints = [
      S(r * 0.66, -0.04, -0.16),
      S(r * 1.12, 0.5, -0.78),
      S(r * 1.2, -0.26, -1.42),
      S(r * 1.04, 0.22, -1.98),
      S(r * 0.96, 0.78, -2.06),
      S(r * 0.98, 1.16, -2.1),
    ];
    radii = [0.6, 0.46, 0.33, 0.27, 0.22, 0.19];
  }
  const geometry = limbTube(joints, radii, 22, 110, 0.2);
  return { geometry, joints, radii };
}

/**
 * Continuous tapered limb: one smooth swept tube whose radius follows the
 * joint radii, so the leg is a sculpted candy limb instead of bamboo segments.
 */
export function limbTube(joints, radii, radial = 18, segs = 90, bulge = 0.12) {
  const curve = new THREE.CatmullRomCurve3(joints, false, 'catmullrom', 0.35);
  const n = joints.length;
  const radiusAt = (f) => {
    const x = clamp(f * (n - 1), 0, n - 1);
    const i = Math.min(n - 2, Math.floor(x));
    const local = x - i;
    const base = lerp(radii[i], radii[i + 1], smoothstep(0, 1, local));
    // Muscle/swelling at the joint.
    const joint = Math.exp(-Math.pow((local - 0.0) * 3.2, 2)) * bulge * radii[i];
    return base + joint;
  };
  const up = new THREE.Vector3(0, 0, 1);
  const positions = [];
  const indices = [];
  const pts = [];
  for (let i = 0; i <= segs; i++) {
    const f = i / segs;
    const p = curve.getPointAt(f);
    const tan = curve.getTangentAt(f);
    let side = new THREE.Vector3().crossVectors(tan, up);
    if (side.lengthSq() < 1e-8) side.set(1, 0, 0);
    side.normalize();
    const upv = new THREE.Vector3().crossVectors(side, tan).normalize();
    const rad = radiusAt(f);
    pts.push({ p, tan, rad });
    for (let j = 0; j < radial; j++) {
      const a = (j / radial) * Math.PI * 2;
      // Slightly flattened cross-section (sugar sticks are extruded).
      const rx = Math.cos(a) * rad;
      const ry = Math.sin(a) * rad * 0.92;
      positions.push(
        p.x + side.x * rx + upv.x * ry,
        p.y + side.y * rx + upv.y * ry,
        p.z + side.z * rx + upv.z * ry
      );
    }
  }
  for (let i = 0; i < segs; i++) {
    for (let j = 0; j < radial; j++) {
      const a = i * radial + j;
      const b = i * radial + ((j + 1) % radial);
      const c = a + radial;
      const d = b + radial;
      indices.push(a, c, b, b, c, d);
    }
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  geo.setIndex(indices);
  geo.computeVertexNormals();
  return geo;
}

/** Rounded gumdrop / crystal blob, used for joints, scales and candy pellets. */
export function gumdrop(center, radius, opts = {}) {
  const geo = new THREE.SphereGeometry(radius, opts.width || 14, opts.height || 10);
  const pos = geo.attributes.position;
  const squash = opts.squash || 0.78;
  for (let i = 0; i < pos.count; i++) {
    const z = pos.getZ(i);
    // Flatten the bottom, keep the dome: a gumdrop.
    pos.setZ(i, z >= 0 ? z : z * squash);
    // Facet the surface slightly for a sugar-crystal look.
    const f = 1 + 0.05 * Math.sin(pos.getX(i) * 40) * Math.sin(pos.getY(i) * 40);
    pos.setX(i, pos.getX(i) * f);
    pos.setY(i, pos.getY(i) * f);
  }
  geo.computeVertexNormals();
  geo.translate(center.x, center.y, center.z);
  return geo;
}

/** Clawed toes fanning from a paw point. */
export function toes(paw, dirDown, sideAxis, forward, scale = 1) {
  const geos = [];
  for (let i = 0; i < 3; i++) {
    const spread = (i - 1) * 0.5;
    const tip = paw
      .clone()
      .addScaledVector(sideAxis, spread * 0.28 * scale)
      .addScaledVector(forward, 0.65 * scale)
      .addScaledVector(dirDown, 0.12 * scale);
    const mid = paw.clone().lerp(tip, 0.45).addScaledVector(dirDown, 0.06);
    geos.push(tubeAlong([paw.clone(), mid, tip], 0.11 * scale, 0.055 * scale, 10));
    // Claw: a curved candy spike.
    const claw1 = tip.clone().addScaledVector(forward, 0.18 * scale).addScaledVector(dirDown, -0.02);
    const claw2 = claw1.clone().addScaledVector(forward, 0.14 * scale).addScaledVector(dirDown, -0.1 * scale);
    geos.push(tubeAlong([tip.clone(), claw1, claw2], 0.07 * scale, 0.004 * scale, 8));
  }
  return mergeClean(geos);
}

// ---------------------------------------------------------------------------
// Head furniture: horns, jaw, teeth, eyes, whiskers
// ---------------------------------------------------------------------------

export function hornGeometry(root, dir, length, r0, r1, curve = 1) {
  // Swept-back candy-cane horn with a gentle S curve.
  const pts = [];
  for (let i = 0; i <= 10; i++) {
    const f = i / 10;
    const p = root
      .clone()
      .addScaledVector(dir, length * f)
      .add(new THREE.Vector3(0, 0.12 * curve * Math.sin(f * Math.PI) * length, -0.1 * f * f * length));
    pts.push(p);
  }
  return tubeAlong(pts, r0, r1, 14, (f, a, rad) => {
    // Ridged horn rings.
    return rad * (1 + 0.06 * Math.sin(f * 42));
  });
}

/** Cone teeth along a jaw line in head-local space. */
export function teethGeometry(side, count, upper = true, scale = 1.0) {
  const geos = [];
  for (let i = 0; i < count; i++) {
    const u = 0.22 + (i / (count - 1)) * 0.66;
    // Follow the head profile at this station.
    const x = -u * 2.0 * scale;
    const w = (0.46 - 0.3 * Math.pow(u, 1.5)) * 0.94 * scale;
    const y = side * w;
    const size = 0.075 * scale * (1 - 0.45 * Math.abs(u - 0.5));
    const root = new THREE.Vector3(x, y, (upper ? -0.03 : 0.03) * scale);
    const tip = root.clone().add(new THREE.Vector3(0.02, 0.01, upper ? -size * 2.4 : size * 2.4));
    geos.push(tubeAlong([root, root.clone().lerp(tip, 0.5), tip], size, 0.004, 7));
  }
  return mergeClean(geos);
}

/** Rock-candy spikes along the spine, from t0 to t1. */
export function spineSpikes(t0, t1, count, sizeCurve) {
  const geos = [];
  for (let i = 0; i < count; i++) {
    const f = i / (count - 1);
    const t = lerp(t0, t1, f);
    const { sample, up, tangent } = bodyFrame(t, 0, { idle: 0 });
    const h = sizeCurve(f) * (0.35 + sample.r * 0.9);
    const root = sample.pos.clone().addScaledVector(up, sample.rz * 0.92);
    const back = tangent.clone().multiplyScalar(1).add(new THREE.Vector3(0, 0, 1.35)).normalize();
    const tip = root.clone().addScaledVector(back, h);
    const mid = root.clone().addScaledVector(back, h * 0.55).add(new THREE.Vector3(0, 0, -h * 0.05));
    geos.push(tubeAlong([root.clone().addScaledVector(back, -h * 0.1), mid, tip], h * 0.2, 0.006, 8, (fr, a, rad) => rad * (1 + 0.05 * Math.sin(a * 3))));
  }
  return mergeClean(geos);
}

/** Tail blade: a fan of crystal shards at the tip. */
export function tailBlade(count = 5) {
  const geos = [];
  const tipSample = bodyFrame(0.985, 0, { idle: 0 });
  const tip = bodySample(1.0, 0, { idle: 0 }).pos;
  for (let i = 0; i < count; i++) {
    const f = i / (count - 1);
    const a = (f - 0.5) * 1.15;
    const dir = tipSample.tangent
      .clone()
      .multiplyScalar(Math.cos(a))
      .addScaledVector(tipSample.up, Math.sin(a))
      .normalize();
    const len = 1.9 * (1 - 0.5 * Math.abs(f - 0.5) * 2);
    const root = tip.clone().addScaledVector(dir, -0.2);
    const mid = tip.clone().addScaledVector(dir, len * 0.5);
    const end = tip.clone().addScaledVector(dir, len);
    // Broad, faceted crystal paddle rather than a hair.
    geos.push(tubeAlong([root, mid, end], 0.34 * len * 0.5, 0.02, 6, (fr, ang, rad) =>
      rad * (1 - 0.35 * fr) * (1 + 0.18 * Math.sin(ang * 3))));
  }
  return mergeClean(geos);
}
