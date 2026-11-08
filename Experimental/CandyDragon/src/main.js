// Sugarwyrm — scene, lighting, post and interaction.

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { createCandyDragon } from './dragon.js';

const DPR_CAP = 1.85;

export async function boot() {
  const canvas = document.getElementById('stage');
  const loader = document.getElementById('loader');

  // ---------------------------------------------------------------- renderer
  const renderer = new THREE.WebGLRenderer({
    canvas,
    antialias: true,
    powerPreference: 'high-performance',
    stencil: false,
  });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, DPR_CAP));
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.0;
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  if ('useLegacyLights' in renderer) renderer.useLegacyLights = false;

  // ---------------------------------------------------------------- scene
  const scene = new THREE.Scene();
  scene.background = null;
  scene.fog = new THREE.FogExp2(0x1a0a1c, 0.019);

  const camera = new THREE.PerspectiveCamera(41, window.innerWidth / window.innerHeight, 0.1, 260);
  camera.position.set(-12.6, -16.4, 8.6);

  // Environment: a real studio probe so the clearcoat/transmission react.
  const pmrem = new THREE.PMREMGenerator(renderer);
  pmrem.compileEquirectangularShader();
  const roomEnv = new RoomEnvironment();
  const envRT = pmrem.fromScene(roomEnv, 0.035);
  scene.environment = envRT.texture;
  scene.environmentIntensity = 0.85;
  roomEnv.dispose?.();

  // ---------------------------------------------------------------- lights
  const key = new THREE.DirectionalLight(0xfff4ea, 3.4);
  key.position.set(-14, -12, 20);
  key.castShadow = true;
  key.shadow.mapSize.set(2048, 2048);
  key.shadow.camera.near = 1;
  key.shadow.camera.far = 70;
  const s = 18;
  key.shadow.camera.left = -s;
  key.shadow.camera.right = s;
  key.shadow.camera.top = s;
  key.shadow.camera.bottom = -s;
  key.shadow.bias = -0.0012;
  key.shadow.normalBias = 0.035;
  key.shadow.radius = 3;
  scene.add(key);

  const rimPink = new THREE.PointLight(0xff2f8e, 320, 50, 2);
  rimPink.position.set(11, 7, 9);
  scene.add(rimPink);

  const rimBlue = new THREE.PointLight(0x5c8cff, 260, 50, 2);
  rimBlue.position.set(-6, 13, 7.5);
  scene.add(rimBlue);

  const underGlow = new THREE.PointLight(0xff9ad4, 90, 22, 2);
  underGlow.position.set(0, 0, -1.4);
  scene.add(underGlow);

  const hemi = new THREE.HemisphereLight(0xffd9ec, 0x2a0d2a, 0.7);
  scene.add(hemi);

  const rimMint = new THREE.PointLight(0x63f5c0, 150, 34, 2);
  rimMint.position.set(4, 4, 2.2);
  scene.add(rimMint);

  // ---------------------------------------------------------------- ground
  const groundMat = new THREE.MeshPhysicalMaterial({
    color: 0xf7cee2,
    roughness: 0.22,
    metalness: 0.0,
    clearcoat: 1,
    clearcoatRoughness: 0.16,
    envMapIntensity: 0.9,
    sheen: 1,
    sheenColor: new THREE.Color(0xffd7ea),
  });
  const ground = new THREE.Mesh(new THREE.CircleGeometry(90, 96), groundMat);
  ground.rotation.x = -Math.PI / 2;
  ground.position.y = -0.02;
  ground.receiveShadow = true;
  scene.add(ground);

  // Candy-cane runway rings: thin emissive hoops on the floor for depth.
  const ringMat = new THREE.MeshBasicMaterial({ color: 0xff7ab8, transparent: true, opacity: 0.16 });
  for (let i = 1; i <= 4; i++) {
    const ring = new THREE.Mesh(new THREE.RingGeometry(i * 6.2, i * 6.2 + 0.09, 128), ringMat);
    ring.rotation.x = -Math.PI / 2;
    ring.position.y = 0.002;
    scene.add(ring);
  }

  // A scatter of loose candy on the floor, for scale and charm.
  {
    const colors = [0xff5fa2, 0xffd166, 0x6ef2c0, 0x9b6bff, 0xffffff];
    const geoGum = new THREE.SphereGeometry(0.2, 14, 10);
    geoGum.scale(1, 1, 0.72);
    const geoJim = new THREE.CapsuleGeometry(0.07, 0.3, 3, 8);
    geoJim.rotateZ(Math.PI / 2);
    const mats = colors.map((c) => new THREE.MeshPhysicalMaterial({
      color: c, roughness: 0.18, clearcoat: 1, clearcoatRoughness: 0.1,
      transmission: 0.25, thickness: 0.4, sheen: 0.6, sheenColor: new THREE.Color(0xffffff),
    }));
    const dummy = new THREE.Object3D();
    for (let i = 0; i < 46; i++) {
      const isGum = i % 3 === 0;
      const mesh = new THREE.Mesh(isGum ? geoGum : geoJim, mats[i % mats.length]);
      const a = Math.random() * Math.PI * 2;
      const r = 5 + Math.random() * 16;
      mesh.position.set(Math.cos(a) * r, Math.sin(a) * r, isGum ? 0.14 : 0.05);
      mesh.rotation.set(Math.random() * 3, Math.random() * 3, Math.random() * 3);
      if (!isGum) mesh.rotation.z = Math.random() * Math.PI;
      const sc = 0.7 + Math.random() * 1.1;
      mesh.scale.setScalar(sc);
      mesh.castShadow = true;
      mesh.receiveShadow = true;
      scene.add(mesh);
    }
    void dummy;
  }

  // ---------------------------------------------------------------- dragon
  const dragon = createCandyDragon({
    sprinkleCount: 1500,
    pearlCount: 420,
    moteCount: 800,
  });
  scene.add(dragon.root);
  dragon.root.rotation.z = 0;

  // ---------------------------------------------------------------- bloom
  const composer = new EffectComposer(renderer);
  composer.setPixelRatio(Math.min(window.devicePixelRatio || 1, DPR_CAP));
  composer.setSize(window.innerWidth, window.innerHeight);
  const renderPass = new RenderPass(scene, camera);
  composer.addPass(renderPass);
  const bloom = new UnrealBloomPass(
    new THREE.Vector2(window.innerWidth, window.innerHeight), 0.42, 0.72, 0.82
  );
  composer.addPass(bloom);
  composer.addPass(new OutputPass());

  // ---------------------------------------------------------------- controls
  const controls = new OrbitControls(camera, renderer.domElement);
  controls.target.set(-0.3, 2.4, 4.8);
  controls.enableDamping = true;
  controls.dampingFactor = 0.055;
  controls.rotateSpeed = 0.62;
  controls.zoomSpeed = 0.72;
  controls.panSpeed = 0.5;
  controls.minDistance = 11;
  controls.maxDistance = 46;
  controls.minPolarAngle = 0.22;
  controls.maxPolarAngle = Math.PI * 0.495;
  controls.autoRotate = true;
  controls.autoRotateSpeed = 0.42;
  controls.update();

  // ---------------------------------------------------------------- state
  const state = {
    wings: true,
    autoOrbit: true,
    sugar: true,
    roarUntil: 0,
    time: 0,
  };

  const btnRoar = document.getElementById('btnRoar');
  const btnFlap = document.getElementById('btnFlap');
  const btnOrbit = document.getElementById('btnOrbit');
  const btnSugar = document.getElementById('btnSugar');

  const setPressed = (el, on) => el.setAttribute('aria-pressed', on ? 'true' : 'false');

  const roar = () => {
    state.roarUntil = state.time + 1.6;
    btnRoar.setAttribute('aria-pressed', 'true');
    setTimeout(() => btnRoar.setAttribute('aria-pressed', 'false'), 1600);
    // Camera push-in for drama.
    controls.autoRotateSpeed = 1.5;
    setTimeout(() => { controls.autoRotateSpeed = state.autoOrbit ? 0.42 : 0; }, 1700);
  };

  btnRoar.addEventListener('click', roar);
  btnFlap.addEventListener('click', () => {
    state.wings = !state.wings;
    setPressed(btnFlap, state.wings);

  });
  btnOrbit.addEventListener('click', () => {
    state.autoOrbit = !state.autoOrbit;
    controls.autoRotate = state.autoOrbit;
    setPressed(btnOrbit, state.autoOrbit);
  });
  btnSugar.addEventListener('click', () => {
    state.sugar = !state.sugar;
    setPressed(btnSugar, state.sugar);
    dragon.root.traverse((o) => {
      if (o.isPoints) o.visible = state.sugar;
    });
  });
  window.addEventListener('keydown', (e) => {
    if (e.code === 'Space') { e.preventDefault(); roar(); }
  });

  setPressed(btnFlap, true);
  setPressed(btnOrbit, true);
  setPressed(btnSugar, true);

  // ---------------------------------------------------------------- resize
  const onResize = () => {
    const w = window.innerWidth, h = window.innerHeight;
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, DPR_CAP));
    renderer.setSize(w, h);
    composer.setPixelRatio(Math.min(window.devicePixelRatio || 1, DPR_CAP));
    composer.setSize(w, h);
    bloom.setSize(w, h);
  };
  window.addEventListener('resize', onResize);

  // ---------------------------------------------------------------- loop
  const clock = new THREE.Clock();
  let frames = 0;
  let fpsAccum = 0;

  function frame() {
    const dt = Math.min(clock.getDelta(), 0.05);
    state.time += dt;
    const t = state.time;

    // Wing beat amplitude is gated by the toggle.
    dragon.update(t, dt, { wingAmplitude: state.wings ? 1 : 0.12 });

    // Roar: push the jaw wider than the idle breath (jaw is owned by the rig).
    if (t < state.roarUntil) {
      const p = 1 - (state.roarUntil - t) / 1.6;
      dragon.setRoar(Math.sin(Math.PI * Math.min(1, Math.max(0, p))));
    } else {
      dragon.setRoar(0);
    }

    // Camera breathes a little so the shot never feels frozen.
    controls.target.z = 4.8 + Math.sin(t * 0.24) * 0.12;
    controls.update();

    // Bloom follows the roar for a sweeter flash.
    const targetBloom = state.time < state.roarUntil ? 0.78 : 0.42;
    bloom.strength += (targetBloom - bloom.strength) * 0.06;
    key.intensity = 3.0 + Math.sin(t * 0.7) * 0.08;
    rimPink.intensity = 240 + Math.sin(t * 0.9) * 26;

    composer.render();
    adapt(dt);
    frames++;
    fpsAccum += dt;
    requestAnimationFrame(frame);
  }

  // Adaptive quality: drop pixel ratio if the machine struggles.
  let slowFrames = 0;
  const adapt = (dt) => {
    if (dt > 0.045) slowFrames++;
    else slowFrames = Math.max(0, slowFrames - 0.35);
    if (slowFrames > 45) {
      const next = Math.max(1, Math.min(window.devicePixelRatio || 1, DPR_CAP) - 0.35);
      renderer.setPixelRatio(next);
      composer.setPixelRatio(next);
      slowFrames = 0;
    }
  };
  const quality = { adapt };

  // Warm the shader cache before revealing, so the first frames are smooth.
  renderer.compile(scene, camera);
  composer.render();
  requestAnimationFrame(frame);

  // Reveal
  requestAnimationFrame(() => {
    loader.setAttribute('hidden-fade', '');
    setTimeout(() => loader.remove(), 900);
  });

  return { renderer, scene, camera, dragon, controls, state, quality, composer, bloom };
}
