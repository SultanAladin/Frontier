// Offline image-based check of the real exported geometry: z-buffered raster
// with interpolated normals, Blinn-Phong highlights, rim light and 2x
// supersampling. Stands in for a GPU render when validating silhouette, pose
// and the candy colour language.
import * as THREE from 'three';
import { PNG } from 'pngjs';
import fs from 'node:fs';

const SS = 2; // supersample factor

export function renderScene(items, opts = {}) {
  const W = (opts.width || 900) * SS;
  const H = (opts.height || 640) * SS;
  const cam = opts.camera;
  cam.aspect = W / H;
  cam.updateProjectionMatrix();
  cam.updateMatrixWorld(true);
  cam.matrixWorldInverse.copy(cam.matrixWorld).invert();

  const color = new Float32Array(W * H * 3);
  const depth = new Float32Array(W * H).fill(Infinity);

  // Background: soft candy-shop dusk gradient.
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      const i = (y * W + x) * 3;
      const t = y / H;
      const u = x / W;
      color[i] = 0.055 + 0.05 * t + 0.02 * u;
      color[i + 1] = 0.03 + 0.03 * t;
      color[i + 2] = 0.11 + 0.1 * t + 0.03 * (1 - u);
    }
  }

  const lights = [
    { dir: new THREE.Vector3(-0.55, -0.75, 0.65).normalize(), color: [1.0, 0.96, 0.92], i: 0.95 },
    { dir: new THREE.Vector3(0.75, 0.35, 0.35).normalize(), color: [1.0, 0.45, 0.8], i: 0.5 },
    { dir: new THREE.Vector3(0.1, 0.85, -0.45).normalize(), color: [0.4, 0.75, 1.0], i: 0.4 },
  ];

  const v0 = new THREE.Vector3(), v1 = new THREE.Vector3(), v2 = new THREE.Vector3();
  const n0 = new THREE.Vector3(), n1 = new THREE.Vector3(), n2 = new THREE.Vector3();
  const tmpA = new THREE.Vector3(), tmpB = new THREE.Vector3();
  const camPos = cam.position;
  const view = cam.matrixWorldInverse;

  const project = (v) => {
    const p = v.clone().applyMatrix4(view).applyMatrix4(cam.projectionMatrix);
    p.x = (p.x * 0.5 + 0.5) * W;
    p.y = (-p.y * 0.5 + 0.5) * H;
    return p;
  };

  const shade = (n, worldPos, base, gloss) => {
    let r = 0.09 * base[0], g = 0.09 * base[1], b = 0.11 * base[2];
    const V = tmpA.copy(camPos).sub(worldPos).normalize();
    for (const L of lights) {
      const d = Math.max(0, n.dot(L.dir));
      r += d * L.color[0] * L.i * base[0];
      g += d * L.color[1] * L.i * base[1];
      b += d * L.color[2] * L.i * base[2];
      // Blinn-Phong specular
      const Hh = tmpB.copy(L.dir).add(V).normalize();
      const spec = Math.pow(Math.max(0, n.dot(Hh)), gloss) * 0.75;
      r += spec * L.color[0] * L.i * 0.3;
      g += spec * L.color[1] * L.i * 0.3;
      b += spec * L.color[2] * L.i * 0.3;
    }
    // Fresnel rim
    const fres = Math.pow(1 - Math.max(0, n.dot(V)), 3.6) * 0.45;
    r += fres * 0.5; g += fres * 0.22; b += fres * 0.7;
    // Cheap fake bounce from the pink ground
    const bounce = Math.max(0, -n.z) * 0.18;
    r += bounce * 0.8; g += bounce * 0.25; b += bounce * 0.5;
    return [r, g, b];
  };

  let tris = 0;
  const drawMesh = (mesh, worldMatrix, mat) => {
    const geo = mesh.geometry;
    const pos = geo.attributes.position;
    if (!pos) return;
    const nrm = geo.attributes.normal;
    const index = geo.index;
    const count = index ? index.count : pos.count;
    const normalMat = new THREE.Matrix3().getNormalMatrix(worldMatrix);

    for (let t = 0; t < count; t += 3) {
      const ia = index ? index.getX(t) : t;
      const ib = index ? index.getX(t + 1) : t + 1;
      const ic = index ? index.getX(t + 2) : t + 2;
      v0.fromBufferAttribute(pos, ia).applyMatrix4(worldMatrix);
      v1.fromBufferAttribute(pos, ib).applyMatrix4(worldMatrix);
      v2.fromBufferAttribute(pos, ic).applyMatrix4(worldMatrix);
      if (nrm) {
        n0.fromBufferAttribute(nrm, ia).applyMatrix3(normalMat).normalize();
        n1.fromBufferAttribute(nrm, ib).applyMatrix3(normalMat).normalize();
        n2.fromBufferAttribute(nrm, ic).applyMatrix3(normalMat).normalize();
      } else {
        n0.copy(tmpA.subVectors(v1, v0).cross(tmpB.subVectors(v2, v0))).normalize();
        n1.copy(n0); n2.copy(n0);
      }
      const P0 = project(v0), P1 = project(v1), P2 = project(v2);
      const area = (P1.x - P0.x) * (P2.y - P0.y) - (P2.x - P0.x) * (P1.y - P0.y);
      if (Math.abs(area) < 1e-9) continue;
      const minX = Math.max(0, Math.floor(Math.min(P0.x, P1.x, P2.x)));
      const maxX = Math.min(W - 1, Math.ceil(Math.max(P0.x, P1.x, P2.x)));
      const minY = Math.max(0, Math.floor(Math.min(P0.y, P1.y, P2.y)));
      const maxY = Math.min(H - 1, Math.ceil(Math.max(P0.y, P1.y, P2.y)));
      const backFacing = area < 0;
      for (let y = minY; y <= maxY; y++) {
        const py = y + 0.5;
        for (let x = minX; x <= maxX; x++) {
          const px = x + 0.5;
          let w0 = ((P1.x - P0.x) * (py - P0.y) - (px - P0.x) * (P1.y - P0.y)) / area;
          let w1 = ((px - P0.x) * (P2.y - P0.y) - (P2.x - P0.x) * (py - P0.y)) / area;
          if (w0 < 0 || w1 < 0) continue;
          const w2 = 1 - w0 - w1;
          if (w2 < 0) continue;
          const z = w0 * P0.z + w1 * P1.z + w2 * P2.z;
          const di = y * W + x;
          if (z >= depth[di]) continue;
          depth[di] = z;
          const nx = w0 * n0.x + w1 * n1.x + w2 * n2.x;
          const ny = w0 * n0.y + w1 * n1.y + w2 * n2.y;
          const nz = w0 * n0.z + w1 * n1.z + w2 * n2.z;
          const len = Math.hypot(nx, ny, nz) || 1;
          const n = new THREE.Vector3(nx / len, ny / len, nz / len);
          if (backFacing) n.multiplyScalar(-1);
          const worldPos = new THREE.Vector3(
            w0 * v0.x + w1 * v1.x + w2 * v2.x,
            w0 * v0.y + w1 * v1.y + w2 * v2.y,
            w0 * v0.z + w1 * v1.z + w2 * v2.z
          );
          const c = shade(n, worldPos, mat.color, mat.gloss ?? 90);
          const i3 = di * 3;
          color[i3] = c[0];
          color[i3 + 1] = c[1];
          color[i3 + 2] = c[2];
        }
      }
      tris++;
    }
  };

  for (const item of items) {
    item.obj.updateMatrixWorld(true);
    item.obj.traverse((child) => {
      if (!child.isMesh || !child.visible) return;
      const mat = child.userData.mat || item.mat || { color: [0.85, 0.42, 0.72] };
      drawMesh(child, child.matrixWorld, mat);
    });
  }

  // Resolve supersampling + tonemap + sRGB.
  const out = new PNG({ width: W / SS, height: H / SS });
  for (let y = 0; y < H / SS; y++) {
    for (let x = 0; x < W / SS; x++) {
      let r = 0, g = 0, b = 0;
      for (let sy = 0; sy < SS; sy++) {
        for (let sx = 0; sx < SS; sx++) {
          const i = ((y * SS + sy) * W + (x * SS + sx)) * 3;
          r += color[i]; g += color[i + 1]; b += color[i + 2];
        }
      }
      const n = SS * SS;
      const tone = (v) => {
        v /= n;
        // Filmic-ish shoulder
        v = (v * (2.51 * v + 0.03)) / (v * (2.43 * v + 0.59) + 0.14);
        v *= 1.25;
        return Math.round(255 * Math.pow(Math.min(1, Math.max(0, v)), 1 / 2.2));
      };
      const i4 = ((y * (W / SS)) + x) * 4;
      out.data[i4] = tone(r);
      out.data[i4 + 1] = tone(g);
      out.data[i4 + 2] = tone(b);
      out.data[i4 + 3] = 255;
    }
  }

  return { png: out, triangles: tris };
}

export function writePNG(png, file) {
  fs.writeFileSync(file, PNG.sync.write(png));
}
