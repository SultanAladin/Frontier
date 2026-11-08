// Path-traced review of the shipped scene graph (src/dragon.js).
import * as THREE from 'three';
import { createCandyDragon } from '../src/dragon.js';
import { gather, trace, save } from './trace.js';
import fs from 'node:fs';
fs.mkdirSync('tools/out', { recursive: true });

const shots = {
  hero: { pos: [-11.0, -16.5, 9.0], target: [-0.2, 2.2, 5.0], fov: 40 },
  side: { pos: [1.0, -21.0, 5.6], target: [0.2, 2.2, 4.6], fov: 38 },
  face: { pos: [-10.5, -7.0, 7.2], target: [-2.2, 1.0, 6.6], fov: 30 },
  wing: { pos: [-6.0, -12.0, 10.5], target: [0.4, 1.8, 5.0], fov: 42 },
};
const wanted = process.argv.slice(2);
const W = +(process.env.W || 620), H = +(process.env.H || 480), spp = +(process.env.SPP || 2);
const d = createCandyDragon({});
d.update(+(process.env.T || 2.4));

for (const [name, shot] of Object.entries(shots)) {
  if (wanted.length && !wanted.includes(name)) continue;
  const cam = new THREE.PerspectiveCamera(shot.fov, W / H, 0.1, 200);
  cam.position.set(...shot.pos);
  cam.lookAt(new THREE.Vector3(...shot.target));
  const scene = gather(d.root);
  console.log(name, 'tris', scene.tri);
  const t0 = Date.now();
  save(trace(scene, { width: W, height: H, camera: cam, spp }), `tools/out/dragon-${name}.png`);
  console.log(name, 'ms', Date.now() - t0);
}
