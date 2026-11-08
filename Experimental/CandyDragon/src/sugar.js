// Sugar scatter: jimmies, pearls and gumdrops pressed onto the dragon's hide.
//
// Positions come from the same parametric surface the body mesh uses, so every
// sprinkle is welded to the skin and follows the breathing/sway animation.

import * as THREE from 'three';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import { bodyFrame, surfacePoint, makeSurfaceSampler } from './anatomy.js';
import { mulberry32, clamp } from './rng.js';

const SPRINKLE_COLORS = [
  0xff4d94, 0xff8cc4, 0xffd166, 0x6ee7ff, 0x8affc1,
  0xb388ff, 0xfff2a8, 0xff7a5c, 0xffffff,
];

/** Capsule-ish jimmy: a short rounded cylinder with pointed ends. */
function jimmyGeometry() {
  const geo = new THREE.CapsuleGeometry(0.032, 0.115, 3, 6);
  geo.rotateZ(Math.PI / 2);
  return geo;
}

/** Sugar pearl: a tiny faceted sphere. */
function pearlGeometry() {
  return new THREE.IcosahedronGeometry(0.045, 0);
}

/** Sugar crystal shard for the dorsal ridge and crown. */
function shardGeometry() {
  const geo = new THREE.ConeGeometry(0.055, 0.2, 5, 1);
  geo.translate(0, 0.1, 0);
  return geo;
}

/**
 * Distribute details over the body surface.
 * @returns {{sprinkles: THREE.InstancedMesh, pearls: THREE.InstancedMesh}}
 */
export function buildSugarScatter(materials, opts = {}) {
  const seed = opts.seed || 9137;
  const rand = mulberry32(seed);
  const count = opts.count || 1500;
  const pearlCount = opts.pearlCount || 420;

  const dummy = new THREE.Object3D();
  const up = new THREE.Vector3(0, 0, 1);

  const sprinkles = new THREE.InstancedMesh(jimmyGeometry(), materials.sprinkle, count);
  sprinkles.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
  const pearlMesh = new THREE.InstancedMesh(pearlGeometry(), materials.gumdrop, pearlCount);
  pearlMesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);

  // Cache the surface frames: the same data drives every animation frame.
  const anchors = [];
  let attempts = 0;
  while (anchors.length < count && attempts < count * 8) {
    attempts++;
    // Bias toward the flanks and back; keep the belly clean.
    const t = 0.03 + rand() * 0.9;
    const angle = rand() * Math.PI * 2;
    const dorsal = Math.cos(angle);
    // Reject the underside unless near the jaw/throat where candy drips look right.
    if (dorsal < -0.35 && t > 0.12 && t < 0.72) continue;
    // Thin out toward the tail.
    if (t > 0.82 && rand() > 0.45) continue;
    anchors.push({ t, angle, roll: rand() * Math.PI * 2, size: 0.65 + rand() * 0.8, hue: Math.floor(rand() * SPRINKLE_COLORS.length) });
  }

  const pearlAnchors = [];
  attempts = 0;
  while (pearlAnchors.length < pearlCount && attempts < pearlCount * 8) {
    attempts++;
    const t = 0.04 + rand() * 0.86;
    const angle = (rand() * 2 - 1) * 1.9;
    if (Math.cos(angle) < -0.5 && t > 0.12 && t < 0.72) continue;
    pearlAnchors.push({ t, angle, size: 0.5 + rand() * 0.9 });
  }

  const color = new THREE.Color();
  anchors.forEach((a, i) => sprinkles.setColorAt(i, color.setHex(SPRINKLE_COLORS[a.hue])));
  if (sprinkles.instanceColor) sprinkles.instanceColor.needsUpdate = true;

  const sampler = { pos: new THREE.Vector3(), normal: new THREE.Vector3(), tangent: new THREE.Vector3() };

  const update = (time, idle = 1) => {
    const m = new THREE.Matrix4();
    const normal = new THREE.Vector3();
    const tangent = new THREE.Vector3();
    const bitangent = new THREE.Vector3();
    const sample = makeSurfaceSampler(time, { idle }, 72);

    anchors.forEach((a, i) => {
      const p = sample(a.t, a.angle, sampler);
      normal.copy(p.normal);
      tangent.copy(p.tangent).normalize();
      bitangent.crossVectors(normal, tangent).normalize();
      // Lie the jimmy along the surface, pushed out slightly.
      const basis = new THREE.Matrix4().makeBasis(tangent, bitangent, normal);
      dummy.position.copy(p.pos).addScaledVector(normal, 0.018);
      dummy.quaternion.setFromRotationMatrix(basis);
      dummy.rotateZ(a.roll);
      dummy.scale.setScalar(a.size);
      dummy.updateMatrix();
      m.copy(dummy.matrix);
      sprinkles.setMatrixAt(i, m);
    });
    sprinkles.instanceMatrix.needsUpdate = true;
    sprinkles.computeBoundingSphere();

    pearlAnchors.forEach((a, i) => {
      const p = sample(a.t, a.angle, sampler);
      dummy.position.copy(p.pos).addScaledVector(p.normal, 0.014);
      dummy.quaternion.identity();
      dummy.rotateX(a.t * 9);
      dummy.rotateY(a.t * 7);
      dummy.scale.setScalar(a.size * 0.85);
      dummy.updateMatrix();
      pearlMesh.setMatrixAt(i, dummy.matrix);
    });
    pearlMesh.instanceMatrix.needsUpdate = true;
    pearlMesh.computeBoundingSphere();
  };

  update(0, 0);
  return { sprinkles, pearls: pearlMesh, update };
}

/**
 * Dorsal crystal row: chunky mint rock-candy shards standing on the spine,
 * diminishing toward the tail. Static geometry merged once.
 */
export function buildCrystalCrest(materials) {
  const group = new THREE.Group();
  const geos = [];
  const total = 22;
  for (let i = 0; i < total; i++) {
    const f = i / (total - 1);
    const t = 0.045 + f * 0.9;
    const { sample, up, tangent } = bodyFrame(t, 0, { idle: 0 });
    const size = (0.7 + 1.5 * Math.sin(Math.PI * clamp(f * 1.05, 0, 1))) * (0.45 + sample.r);
    const root = sample.pos.clone().addScaledVector(up, sample.rz * 0.88);
    const dir = up.clone().multiplyScalar(1.5).addScaledVector(tangent, 0.75).normalize();
    const g = shardGeometry();
    g.scale(size * 0.55, size * 1.5, size * 0.55);
    const q = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir);
    g.applyQuaternion(q);
    g.translate(root.x, root.y, root.z);
    geos.push(g);
    const g2 = shardGeometry();
    g2.scale(size * 0.36, size * 0.8, size * 0.36);
    const q2 = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0),
      dir.clone().add(new THREE.Vector3(0, 0, 0.35)).normalize());
    g2.applyQuaternion(q2);
    g2.translate(root.x - tangent.x * size * 0.35, root.y - tangent.y * size * 0.35, root.z + size * 0.05);
    geos.push(g2);
  }
  const merged = mergeGeometries(geos.map((g) => {
    g.deleteAttribute('uv');
    return g;
  }), false);
  group.add(new THREE.Mesh(merged, materials.spike));
  return group;
}

/** Candy drips hanging from the jaw and belly: little gummy droplets. */
export function buildCandyDrips(materials) {
  const group = new THREE.Group();
  const rand = mulberry32(4421);
  const geos = [];
  for (let i = 0; i < 16; i++) {
    const t = 0.12 + rand() * 0.5;
    const angle = Math.PI * (0.62 + rand() * 0.76); // underside band
    const p = surfacePoint(t, angle, 0, { idle: 0 });
    const len = 0.1 + rand() * 0.26;
    const r = 0.045 + rand() * 0.05;
    const drop = new THREE.SphereGeometry(r, 10, 8);
    drop.scale(1, 1, 1.35);
    drop.translate(0, 0, -len);
    const neck = new THREE.CylinderGeometry(r * 0.72, r * 0.5, len, 8, 1, true);
    neck.rotateX(Math.PI / 2);
    neck.translate(0, 0, -len / 2);
    const q = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 0, 1), p.normal.clone().negate());
    for (const g of [drop, neck]) {
      g.applyQuaternion(q);
      g.translate(p.pos.x, p.pos.y, p.pos.z);
      g.deleteAttribute('uv');
      geos.push(g);
    }
  }
  const merged = mergeGeometries(geos, false);
  const mesh = new THREE.Mesh(merged, materials.gumdrop);
  group.add(mesh);
  return group;
}

export { jimmyGeometry, pearlGeometry, shardGeometry };
