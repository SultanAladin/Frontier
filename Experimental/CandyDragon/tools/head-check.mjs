import * as THREE from 'three';
import { buildHeadGeometry, buildMouthGeometry } from '../src/anatomy.js';
import { gather, trace, save } from './trace.js';

const root = new THREE.Group();
const mk = (g, hex, rough = 0.4) => {
  const m = new THREE.Mesh(g, new THREE.MeshPhysicalMaterial({ color: hex, roughness: rough, clearcoat: 1 }));
  m.userData.mat = { color: [((hex >> 16) & 255) / 255, ((hex >> 8) & 255) / 255, (hex & 255) / 255], gloss: 120 };
  return m;
};
root.add(mk(buildHeadGeometry('upper'), 0xff5f9e));
const jaw = mk(buildHeadGeometry('jaw'), 0xc93468);
jaw.position.set(0, 0, -0.03);
root.add(jaw);
root.add(mk(buildMouthGeometry(), 0x5d0e2c, 0.7));
const eye = new THREE.Mesh(new THREE.SphereGeometry(0.1, 14, 12), new THREE.MeshPhysicalMaterial({ color: 0xffb02e, emissive: 0xff5a00, emissiveIntensity: 3 }));
eye.userData.mat = { color: [1, 0.69, 0.18], gloss: 200, emissive: [1.4, 0.4, 0] };
eye.position.set(-0.94, 0.4, 0.2);
root.add(eye);
const eye2 = eye.clone(); eye2.position.y = -0.4; root.add(eye2);

const shots = {
  headSide: { pos: [0.4, -6.0, 0.55], target: [-1.1, 0, 0.15], fov: 34 },
  headTop: { pos: [-1.1, -0.4, 6.0], target: [-1.1, 0, 0.15], fov: 34 },
  headFront: { pos: [-6.0, -0.6, 0.7], target: [-1.1, 0, 0.15], fov: 34 },
};
const want = process.argv.slice(2);
for (const [name, s] of Object.entries(shots)) {
  if (want.length && !want.includes(name)) continue;
  const cam = new THREE.PerspectiveCamera(s.fov, 1, 0.1, 100);
  cam.position.set(...s.pos);
  cam.lookAt(new THREE.Vector3(...s.target));
  const png = trace(gather(root), { width: 460, height: 460, camera: cam, spp: 2 });
  save(png, `tools/out/${name}.png`);
  console.log('done', name);
}
