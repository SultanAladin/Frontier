// Offline path tracer for the candy dragon.
//
// Purpose: validate the *look* of the exported geometry and the material
// palette (icing gloss, taffy translucency, sugar-crystal sparkle) without a
// GPU. Not part of the shipped scene — a review tool.
import * as THREE from 'three';
import { PNG } from 'pngjs';
import fs from 'node:fs';

// ---------------------------------------------------------------------------
// BVH over world-space triangles
// ---------------------------------------------------------------------------

class BVH {
  constructor(p0s, p1s, p2s) {
    this.p0 = p0s; this.p1 = p1s; this.p2 = p2s;
    const n = p0s.length / 3;
    this.order = new Uint32Array(n);
    for (let i = 0; i < n; i++) this.order[i] = i;
    // Centroid bounds per triangle
    const cent = new Float32Array(n * 3);
    for (let i = 0; i < n; i++) {
      cent[i * 3] = (p0s[i * 3] + p1s[i * 3] + p2s[i * 3]) / 3;
      cent[i * 3 + 1] = (p0s[i * 3 + 1] + p1s[i * 3 + 1] + p2s[i * 3 + 1]) / 3;
      cent[i * 3 + 2] = (p0s[i * 3 + 2] + p1s[i * 3 + 2] + p2s[i * 3 + 2]) / 3;
    }
    this.cent = cent;
    this.nodes = [];
    this.build(0, n);
  }

  bounds(lo, hi) {
    const { p0, p1, p2, order } = this;
    const min = [Infinity, Infinity, Infinity];
    const max = [-Infinity, -Infinity, -Infinity];
    for (let i = lo; i < hi; i++) {
      const t = order[i];
      for (let v = 0; v < 3; v++) {
        for (const p of [p0, p1, p2]) {
          const x = p[t * 3 + v];
          if (x < min[v]) min[v] = x;
          if (x > max[v]) max[v] = x;
        }
      }
    }
    return { min, max };
  }

  build(lo, hi) {
    const idx = this.nodes.length;
    const b = this.bounds(lo, hi);
    this.nodes.push({ lo, hi, min: b.min, max: b.max, left: -1, right: -1 });
    if (hi - lo <= 6) return idx;
    // Split on the longest axis at the centroid median.
    const ext = [b.max[0] - b.min[0], b.max[1] - b.min[1], b.max[2] - b.min[2]];
    const axis = ext[0] > ext[1] ? (ext[0] > ext[2] ? 0 : 2) : ext[1] > ext[2] ? 1 : 2;
    const sub = Array.from(this.order.slice(lo, hi));
    sub.sort((a, c) => this.cent[a * 3 + axis] - this.cent[c * 3 + axis]);
    for (let i = 0; i < sub.length; i++) this.order[lo + i] = sub[i];
    const mid = (lo + hi) >> 1;
    if (mid === lo || mid === hi) return idx;
    const l = this.build(lo, mid);
    const r = this.build(mid, hi);
    this.nodes[idx].left = l;
    this.nodes[idx].right = r;
    this.nodes[idx].hi = mid;
    return idx;
  }
}

function rayBox(o, d, inv, node) {
  let t0 = (node.min[0] - o.x) * inv.x, t1 = (node.max[0] - o.x) * inv.x;
  let tmin = Math.min(t0, t1), tmax = Math.max(t0, t1);
  t0 = (node.min[1] - o.y) * inv.y; t1 = (node.max[1] - o.y) * inv.y;
  tmin = Math.max(tmin, Math.min(t0, t1)); tmax = Math.min(tmax, Math.max(t0, t1));
  t0 = (node.min[2] - o.z) * inv.z; t1 = (node.max[2] - o.z) * inv.z;
  tmin = Math.max(tmin, Math.min(t0, t1)); tmax = Math.min(tmax, Math.max(t0, t1));
  return tmax >= Math.max(tmin, 0);
}

// ---------------------------------------------------------------------------
// Scene assembly
// ---------------------------------------------------------------------------

/** Convert a three.js PBR material into the tracer's compact shading model. */
export function matFromThree(m) {
  if (!m) return { color: [0.8, 0.4, 0.6], gloss: 60 };
  const c = (m.userData && m.userData.tint) ? new THREE.Color(m.userData.tint) : (m.color ? m.color : new THREE.Color(0xffffff));
  const glassy = (m.clearcoat || 0) * 0.5 + (m.transmission || 0) * 0.6;
  const gloss = 20 + (1 - Math.min(1, m.roughness ?? 0.5)) * 150 + glassy * 80;
  const out = {
    color: [c.r, c.g, c.b],
    vertexColors: !!m.vertexColors,
    gloss,
    transmission: m.transmission || 0,
    sheen: m.sheen || 0,
    sheenColor: m.sheenColor ? [m.sheenColor.r, m.sheenColor.g, m.sheenColor.b] : [1, 1, 1],
    iridescence: m.iridescence || 0,
    metalness: m.metalness || 0,
  };
  if (m.emissive && (m.emissiveIntensity || 0) > 0.01) {
    const e = m.emissive, k = Math.min(3, m.emissiveIntensity);
    out.emissive = [e.r * k * 0.45, e.g * k * 0.45, e.b * k * 0.45];
  }
  return out;
}

export function gather(root) {
  root.updateMatrixWorld(true);
  const pos = [];
  const nor = [];
  const col = [];
  const mats = [];
  const v = new THREE.Vector3();
  const n = new THREE.Vector3();

  root.traverse((child) => {
    if (!child.isMesh || !child.visible) return;
    const geo = child.geometry;
    const P = geo.attributes.position;
    const N = geo.attributes.normal;
    const mat = child.userData.mat || matFromThree(child.material);
    const idx = geo.index;
    const count = idx ? idx.count : P.count;
    const m = child.matrixWorld;
    const nm = new THREE.Matrix3().getNormalMatrix(m);
    // Vertex colours multiply the material colour, as in three.js.
    const vcol = mat.vertexColors && geo.attributes.color ? geo.attributes.color : null;
    for (let t = 0; t < count; t += 3) {
      let cr = 1, cg = 1, cb = 1;
      if (vcol) {
        cr = 0; cg = 0; cb = 0;
        for (let k = 0; k < 3; k++) {
          const vi = idx ? idx.getX(t + k) : t + k;
          cr += vcol.getX(vi) / 3; cg += vcol.getY(vi) / 3; cb += vcol.getZ(vi) / 3;
        }
      }
      for (let k = 0; k < 3; k++) {
        const vi = idx ? idx.getX(t + k) : t + k;
        v.fromBufferAttribute(P, vi).applyMatrix4(m);
        if (N) n.fromBufferAttribute(N, vi).applyMatrix3(nm).normalize();
        else n.set(0, 0, 1);
        pos.push(v.x, v.y, v.z);
        nor.push(n.x, n.y, n.z);
        col.push(cr, cg, cb);
      }
      mats.push(mat);
    }
  });

  return {
    pos: new Float32Array(pos),
    nor: new Float32Array(nor),
    col: new Float32Array(col),
    mats,
    tri: mats.length,
  };
}

// ---------------------------------------------------------------------------
// Tracer
// ---------------------------------------------------------------------------

const clamp01 = (v) => (v < 0 ? 0 : v > 1 ? 1 : v);

export function trace(scene, opts = {}) {
  const W = opts.width || 640;
  const H = opts.height || 480;
  const spp = opts.spp || 2;
  const cam = opts.camera;
  cam.updateMatrixWorld(true);
  cam.matrixWorldInverse.copy(cam.matrixWorld).invert();

  const { pos, nor, col, mats } = scene;
  const tri = pos.length / 9;

  // Triangle-level arrays (flat, cache friendly)
  const ax = new Float32Array(tri), ay = new Float32Array(tri), az = new Float32Array(tri);
  const bx = new Float32Array(tri), by = new Float32Array(tri), bz = new Float32Array(tri);
  const cx = new Float32Array(tri), cy = new Float32Array(tri), cz = new Float32Array(tri);
  const nx0 = new Float32Array(tri), ny0 = new Float32Array(tri), nz0 = new Float32Array(tri);
  const nx1 = new Float32Array(tri), ny1 = new Float32Array(tri), nz1 = new Float32Array(tri);
  const nx2 = new Float32Array(tri), ny2 = new Float32Array(tri), nz2 = new Float32Array(tri);
  const c0 = new Float32Array(tri * 3), c1 = new Float32Array(tri * 3), c2 = new Float32Array(tri * 3);
  for (let i = 0; i < tri; i++) {
    ax[i] = pos[i * 9]; ay[i] = pos[i * 9 + 1]; az[i] = pos[i * 9 + 2];
    bx[i] = pos[i * 9 + 3]; by[i] = pos[i * 9 + 4]; bz[i] = pos[i * 9 + 5];
    cx[i] = pos[i * 9 + 6]; cy[i] = pos[i * 9 + 7]; cz[i] = pos[i * 9 + 8];
    nx0[i] = nor[i * 9]; ny0[i] = nor[i * 9 + 1]; nz0[i] = nor[i * 9 + 2];
    nx1[i] = nor[i * 9 + 3]; ny1[i] = nor[i * 9 + 4]; nz1[i] = nor[i * 9 + 5];
    nx2[i] = nor[i * 9 + 6]; ny2[i] = nor[i * 9 + 7]; nz2[i] = nor[i * 9 + 8];
    for (let k = 0; k < 3; k++) {
      c0[i * 3 + k] = col[i * 9 + k];
      c1[i * 3 + k] = col[i * 9 + 3 + k];
      c2[i * 3 + k] = col[i * 9 + 6 + k];
    }
  }
  const bvh = new BVH({ length: 0 }, { length: 0 }, { length: 0 });
  // Flat triangle BVH (simpler bounds than the generic one)
  const cent = new Float32Array(tri * 3);
  for (let i = 0; i < tri; i++) {
    cent[i * 3] = (ax[i] + bx[i] + cx[i]) / 3;
    cent[i * 3 + 1] = (ay[i] + by[i] + cy[i]) / 3;
    cent[i * 3 + 2] = (az[i] + bz[i] + cz[i]) / 3;
  }
  const order = new Uint32Array(tri);
  for (let i = 0; i < tri; i++) order[i] = i;
  const nodes = [];
  const buildNode = (lo, hi) => {
    const id = nodes.length;
    const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
    for (let k = lo; k < hi; k++) {
      const t = order[k];
      const xs = [ax[t], bx[t], cx[t]], ys = [ay[t], by[t], cy[t]], zs = [az[t], bz[t], cz[t]];
      for (let v = 0; v < 3; v++) {
        if (xs[v] < min[0]) min[0] = xs[v]; if (xs[v] > max[0]) max[0] = xs[v];
        if (ys[v] < min[1]) min[1] = ys[v]; if (ys[v] > max[1]) max[1] = ys[v];
        if (zs[v] < min[2]) min[2] = zs[v]; if (zs[v] > max[2]) max[2] = zs[v];
      }
    }
    nodes.push({ min, max, lo, hi, left: -1, right: -1 });
    if (hi - lo <= 4) return id;
    const ext = [max[0] - min[0], max[1] - min[1], max[2] - min[2]];
    const axis = ext[0] > ext[1] ? (ext[0] > ext[2] ? 0 : 2) : ext[1] > ext[2] ? 1 : 2;
    const sub = Array.from(order.slice(lo, hi)).sort((a, c) => cent[a * 3 + axis] - cent[c * 3 + axis]);
    for (let i = 0; i < sub.length; i++) order[lo + i] = sub[i];
    const mid = (lo + hi) >> 1;
    const l = buildNode(lo, mid);
    const r = buildNode(mid, hi);
    nodes[id].left = l; nodes[id].right = r; nodes[id].hi = mid;
    return id;
  };
  buildNode(0, tri);
  void bvh;

  // Occluders for shadow rays: keep it cheap, skip the membrane shells.
  const hitTri = (o, d, tmax) => {
    let best = tmax, bt = -1, bu = 0, bv = 0;
    const ix = 1 / (d.x || 1e-9), iy = 1 / (d.y || 1e-9), iz = 1 / (d.z || 1e-9);
    const stack = [0];
    while (stack.length) {
      const ni = stack.pop();
      const n = nodes[ni];
      if (!rayBoxFast(o, ix, iy, iz, n.min, n.max, best)) continue;
      if (n.left < 0) {
        for (let k = n.lo; k < n.hi; k++) {
          const t = order[k];
          const e1x = bx[t] - ax[t], e1y = by[t] - ay[t], e1z = bz[t] - az[t];
          const e2x = cx[t] - ax[t], e2y = cy[t] - ay[t], e2z = cz[t] - az[t];
          const px = d.y * e2z - d.z * e2y;
          const py = d.z * e2x - d.x * e2z;
          const pz = d.x * e2y - d.y * e2x;
          const det = e1x * px + e1y * py + e1z * pz;
          if (Math.abs(det) < 1e-12) continue;
          const inv = 1 / det;
          const tx = o.x - ax[t], ty = o.y - ay[t], tz = o.z - az[t];
          const u = (tx * px + ty * py + tz * pz) * inv;
          if (u < 0 || u > 1) continue;
          const qx = ty * e1z - tz * e1y;
          const qy = tz * e1x - tx * e1z;
          const qz = tx * e1y - ty * e1x;
          const v = (d.x * qx + d.y * qy + d.z * qz) * inv;
          if (v < 0 || u + v > 1) continue;
          const tt = (e2x * qx + e2y * qy + e2z * qz) * inv;
          if (tt > 1e-5 && tt < best) { best = tt; bt = t; bu = u; bv = v; }
        }
      } else {
        stack.push(n.left, n.right);
      }
    }
    if (bt < 0) return null;
    const w0 = 1 - bu - bv, w1 = bu, w2 = bv;
    return {
      t: best,
      tri: bt,
      p: { x: o.x + d.x * best, y: o.y + d.y * best, z: o.z + d.z * best },
      n: {
        x: w0 * nx0[bt] + w1 * nx1[bt] + w2 * nx2[bt],
        y: w0 * ny0[bt] + w1 * ny1[bt] + w2 * ny2[bt],
        z: w0 * nz0[bt] + w1 * nz1[bt] + w2 * nz2[bt],
      },
      c: [
        w0 * c0[bt * 3] + w1 * c1[bt * 3] + w2 * c2[bt * 3],
        w0 * c0[bt * 3 + 1] + w1 * c1[bt * 3 + 1] + w2 * c2[bt * 3 + 1],
        w0 * c0[bt * 3 + 2] + w1 * c1[bt * 3 + 2] + w2 * c2[bt * 3 + 2],
      ],
    };
  };

  const occluded = (o, d, dist) => {
    const ix = 1 / (d.x || 1e-9), iy = 1 / (d.y || 1e-9), iz = 1 / (d.z || 1e-9);
    const stack = [0];
    while (stack.length) {
      const ni = stack.pop();
      const n = nodes[ni];
      if (!rayBoxFast(o, ix, iy, iz, n.min, n.max, dist)) continue;
      if (n.left < 0) {
        for (let k = n.lo; k < n.hi; k++) {
          const t = order[k];
          const e1x = bx[t] - ax[t], e1y = by[t] - ay[t], e1z = bz[t] - az[t];
          const e2x = cx[t] - ax[t], e2y = cy[t] - ay[t], e2z = cz[t] - az[t];
          const px = d.y * e2z - d.z * e2y;
          const py = d.z * e2x - d.x * e2z;
          const pz = d.x * e2y - d.y * e2x;
          const det = e1x * px + e1y * py + e1z * pz;
          if (Math.abs(det) < 1e-12) continue;
          const inv = 1 / det;
          const tx = o.x - ax[t], ty = o.y - ay[t], tz = o.z - az[t];
          const u = (tx * px + ty * py + tz * pz) * inv;
          if (u < 0 || u > 1) continue;
          const qx = ty * e1z - tz * e1y;
          const qy = tz * e1x - tx * e1z;
          const qz = tx * e1y - ty * e1x;
          const v = (d.x * qx + d.y * qy + d.z * qz) * inv;
          if (v < 0 || u + v > 1) continue;
          const tt = (e2x * qx + e2y * qy + e2z * qz) * inv;
          if (tt > 1e-4 && tt < dist) return true;
        }
      } else {
        stack.push(n.left, n.right);
      }
    }
    return false;
  };

  // --- lighting ------------------------------------------------------------
  const lights = opts.lights || [
    { pos: new THREE.Vector3(-13, -15, 16), radius: 6.0, color: [1.0, 0.95, 0.9], power: 210 },
    { pos: new THREE.Vector3(12, -2, 8), radius: 5.0, color: [1.0, 0.42, 0.8], power: 80 },
    { pos: new THREE.Vector3(2, 14, 7), radius: 5.0, color: [0.4, 0.7, 1.0], power: 90 },
  ];

  const env = (d) => {
    // Studio-sweet gradient: warm above, magenta toward the horizon, deep plum below.
    const up = clamp01(d.y * 0.5 + 0.5);
    const horizon = Math.exp(-Math.pow(d.y * 2.2, 2));
    const r = 0.045 + 0.31 * Math.pow(up, 1.6) + 0.34 * horizon;
    const g = 0.028 + 0.2 * Math.pow(up, 2.0) + 0.1 * horizon;
    const b = 0.11 + 0.36 * Math.pow(up, 1.2) + 0.3 * horizon;
    return [r, g, b];
  };

  // Ground: glossy candy counter reflecting the room.
  const groundZ = opts.groundZ ?? 0.0;
  const groundAlbedo = opts.groundColor || [0.34, 0.2, 0.28];
  const groundGloss = 0.55;

  const shadeHit = (hit, dir, baseIn, gloss) => {
    const base = baseIn && baseIn.color ? baseIn : { color: baseIn };
    const n = hit.n;
    const nl = Math.hypot(n.x, n.y, n.z) || 1;
    let nx = n.x / nl, ny = n.y / nl, nz = n.z / nl;
    // Two-sided: shade the facing side.
    if (nx * dir.x + ny * dir.y + nz * dir.z > 0) { nx = -nx; ny = -ny; nz = -nz; }
    const p = hit.p;
    const out = [0, 0, 0];
    const roughP = Math.pow(1 - Math.min(1, gloss / 220), 2);
    const shininess = 8 + gloss * 2.2;

    for (const L of lights) {
      const lx = L.pos.x - p.x, ly = L.pos.y - p.y, lz = L.pos.z - p.z;
      const dist2 = lx * lx + ly * ly + lz * lz;
      const dist = Math.sqrt(dist2);
      const dx = lx / dist, dy = ly / dist, dz = lz / dist;
      const ndl = Math.max(0, nx * dx + ny * dy + nz * dz);
      if (ndl <= 0) continue;
      const shadowOrigin = { x: p.x + nx * 1e-3, y: p.y + ny * 1e-3, z: p.z + nz * 1e-3 };
      if (occluded(shadowOrigin, { x: dx, y: dy, z: dz }, dist - 0.15)) continue;
      // Area-ish falloff
      const atten = L.power / (dist2 + 1);
      // Diffuse
      for (let c = 0; c < 3; c++) out[c] += base.color[c] * L.color[c] * atten * ndl * 0.32;
      // Specular (Blinn)
      const hx = dx - dir.x, hy = dy - dir.y, hz = dz - dir.z;
      const hlen = Math.hypot(hx, hy, hz) || 1;
      const ndh = Math.max(0, (nx * hx + ny * hy + nz * hz) / hlen);
      const spec = Math.pow(ndh, shininess) * (0.55 + 0.45 * (1 - roughP)) * 1.55;
      const fres = 0.06 + 0.94 * Math.pow(1 - Math.max(0, ndl), 4);
      for (let c = 0; c < 3; c++) out[c] += L.color[c] * atten * spec * fres * 0.55;
    }

    // Ambient occlusion + environment diffuse
    const ao = sampleAO(p, { x: nx, y: ny, z: nz }, 3);
    const e = env({ x: nx, y: ny, z: nz });
    // Translucent candy: light bleeds through, tinted by the body colour.
    const trans = (base.transmission || 0);
    for (let c = 0; c < 3; c++) {
      out[c] += base.color[c] * e[c] * (0.55 + 0.45 * ao) * 1.3;
      if (trans > 0) out[c] *= 1 + trans * 0.55;
    }
    // Sheen: soft velvety rim typical of fondant and icing sugar.
    if (base.sheen) {
      const vel = Math.pow(1 - Math.abs(nx * dir.x + ny * dir.y + nz * dir.z), 2.2);
      for (let c = 0; c < 3; c++) out[c] += vel * base.sheen * base.sheenColor[c] * 0.16;
    }
    // Iridescence: shift the rim toward mint/violet.
    if (base.iridescence) {
      const f = Math.pow(1 - Math.abs(nx * dir.x + ny * dir.y + nz * dir.z), 3);
      out[0] += f * base.iridescence * 0.16;
      out[1] += f * base.iridescence * 0.3;
      out[2] += f * base.iridescence * 0.26;
    }
    return out;
  };

  const jitter = (() => {
    let s = 12345;
    return () => {
      s = (s * 1664525 + 1013904223) >>> 0;
      return s / 4294967296;
    };
  })();

  const sampleAO = (p, n, rays) => {
    let acc = 0;
    for (let i = 0; i < rays; i++) {
      // Cosine-ish hemisphere sample around n.
      const u1 = jitter(), u2 = jitter();
      const r = Math.sqrt(u1);
      const theta = 2 * Math.PI * u2;
      const dx = r * Math.cos(theta), dy = r * Math.sin(theta), dz = Math.sqrt(Math.max(0, 1 - u1));
      // Build a basis around n.
      const upv = Math.abs(n.z) < 0.9 ? { x: 0, y: 0, z: 1 } : { x: 1, y: 0, z: 0 };
      let tx = upv.y * n.z - upv.z * n.y, ty = upv.z * n.x - upv.x * n.z, tz = upv.x * n.y - upv.y * n.x;
      const tl = Math.hypot(tx, ty, tz) || 1;
      tx /= tl; ty /= tl; tz /= tl;
      const bxv = n.y * tz - n.z * ty, byv = n.z * tx - n.x * tz, bzv = n.x * ty - n.y * tx;
      const d = {
        x: tx * dx + bxv * dy + n.x * dz,
        y: ty * dx + byv * dy + n.y * dz,
        z: tz * dx + bzv * dy + n.z * dz,
      };
      if (!occluded({ x: p.x + n.x * 2e-3, y: p.y + n.y * 2e-3, z: p.z + n.z * 2e-3 }, d, 6)) acc += 1;
    }
    return acc / rays;
  };

  const color = new Float32Array(W * H * 3);
  const forward = new THREE.Vector3();
  const right = new THREE.Vector3();
  const upv = new THREE.Vector3();
  cam.getWorldDirection(forward);
  right.crossVectors(forward, new THREE.Vector3(0, 0, 1)).normalize();
  upv.crossVectors(right, forward).normalize();
  const fov = (cam.fov * Math.PI) / 180;
  const aspect = W / H;

  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      let r = 0, g = 0, b = 0;
      for (let s = 0; s < spp; s++) {
        const sx = ((x + (spp > 1 ? jitter() : 0.5)) / W) * 2 - 1;
        const sy = 1 - ((y + (spp > 1 ? jitter() : 0.5)) / H) * 2;
        const d = new THREE.Vector3()
          .copy(forward)
          .addScaledVector(right, sx * Math.tan(fov / 2) * aspect)
          .addScaledVector(upv, sy * Math.tan(fov / 2))
          .normalize();
        const hit = hitTri(cam.position, d, 1e9);
        let c;
        if (hit) {
          const mat0 = mats[hit.tri];
          const mat = mat0.vertexColors
            ? {
                ...mat0,
                color: [
                  mat0.color[0] * hit.c[0],
                  mat0.color[1] * hit.c[1],
                  mat0.color[2] * hit.c[2],
                ],
              }
            : mat0;
          c = shadeHit(hit, d, mat, mat.gloss ?? 60);
          if (mat.emissive) {
            c = [c[0] + mat.emissive[0], c[1] + mat.emissive[1], c[2] + mat.emissive[2]];
          }
        } else if (d.z < 0) {
          // Ground plane
          const t = (groundZ - cam.position.z) / d.z;
          if (t > 0) {
            const gp = { x: cam.position.x + d.x * t, y: cam.position.y + d.y * t, z: groundZ };
            const n = { x: 0, y: 0, z: 1 };
            c = shadeHit({ p: gp, n }, d, { color: groundAlbedo }, 130);
            // Cheap mirror sheen
            const sheen = Math.pow(Math.abs(d.z), 2) * 0.12;
            for (let k = 0; k < 3; k++) c[k] += sheen * groundAlbedo[k];
            void groundGloss;
          } else {
            c = env(d);
          }
        } else {
          c = env(d);
        }
        r += c[0]; g += c[1]; b += c[2];
      }
      const i = (y * W + x) * 3;
      color[i] = r / spp; color[i + 1] = g / spp; color[i + 2] = b / spp;
    }
  }

  // Tonemap (ACES approx) + sRGB
  const png = new PNG({ width: W, height: H });
  const aces = (v) => {
    const a = 2.51, bb = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp01((v * (a * v + bb)) / (v * (c * v + d) + e));
  };
  const exposure = opts.exposure ?? 0.72;
  for (let i = 0; i < W * H; i++) {
    for (let c = 0; c < 3; c++) {
      let v = color[i * 3 + c] * exposure;
      v = aces(v);
      png.data[i * 4 + c] = Math.round(255 * Math.pow(v, 1 / 2.2));
    }
    png.data[i * 4 + 3] = 255;
  }
  return png;
}

function rayBoxFast(o, ix, iy, iz, min, max, tmax) {
  let t0 = (min[0] - o.x) * ix, t1 = (max[0] - o.x) * ix;
  let tmin = Math.min(t0, t1), tmax2 = Math.max(t0, t1);
  t0 = (min[1] - o.y) * iy; t1 = (max[1] - o.y) * iy;
  tmin = Math.max(tmin, Math.min(t0, t1)); tmax2 = Math.min(tmax2, Math.max(t0, t1));
  t0 = (min[2] - o.z) * iz; t1 = (max[2] - o.z) * iz;
  tmin = Math.max(tmin, Math.min(t0, t1)); tmax2 = Math.min(tmax2, Math.max(t0, t1));
  return tmax2 >= Math.max(tmin, 0) && tmin < tmax;
}

export function save(png, file) {
  fs.writeFileSync(file, PNG.sync.write(png));
}
