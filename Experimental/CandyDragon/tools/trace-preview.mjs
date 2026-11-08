// Path-traced review render of the actual exported dragon geometry.
import * as THREE from 'three';
import { assemble } from './assemble.mjs';
import { gather, trace, save } from './trace.js';

const shots = {
  hero: { pos: [-9.5, -15.0, 7.0], target: [-0.6, 1.2, 4.0], fov: 38 },
  side: { pos: [1.0, -19.0, 4.6], target: [0.4, 1.2, 3.8], fov: 38 },
  face: { pos: [-9.6, -6.6, 6.4], target: [-1.9, 0.2, 5.7], fov: 28 },
};
const wanted = process.argv.slice(2);
const W = +(process.env.W || 620);
const H = +(process.env.H || 480);
const spp = +(process.env.SPP || 2);

for (const [name, shot] of Object.entries(shots)) {
  if (wanted.length && !wanted.includes(name)) continue;
  const cam = new THREE.PerspectiveCamera(shot.fov, W / H, 0.1, 200);
  cam.position.set(...shot.pos);
  cam.lookAt(new THREE.Vector3(...shot.target));
  const root = assemble(0, 1);
  const scene = gather(root);
  console.log(name, 'triangles in trace:', scene.tri);
  const t0 = Date.now();
  const png = trace(scene, { width: W, height: H, camera: cam, spp });
  save(png, `tools/out/trace-${name}.png`);
  console.log(name, 'ms', Date.now() - t0);
}
