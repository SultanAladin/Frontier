import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  DEFAULT_OCEAN_PARAMS,
  RippleField,
  buildWaveSet,
  foamAt,
  gerstnerAt,
  localWaterFrame,
  normalizeOceanParams,
  schlickFresnel,
  skyRadiance,
  sunDirection,
  waterColorAtDepth,
  waterDepthAt,
  waveHeight,
} from '../src/core/ocean-model.js';
import { PlanetField } from '../src/core/planet-field.js';
import { directionFromLatLon } from '../src/core/cube-sphere.js';

test('wave set is deterministic, bounded and dispersed', () => {
  const a = buildWaveSet({ waveCount: 8 }, 42);
  const b = buildWaveSet({ waveCount: 8 }, 42);
  assert.deepEqual(a.map((w) => w.wavelength), b.map((w) => w.wavelength));
  assert.equal(a.length, 8);

  for (const w of a) {
    assert.ok(w.amplitude > 0, 'zero-amplitude wave');
    assert.ok(w.wavelength > 0);
    assert.ok(w.direction.x ** 2 + w.direction.z ** 2 > 0.99);
    // Deep-water dispersion relation: omega = sqrt(g k).
    assert.ok(Math.abs(w.angularFrequency - Math.sqrt(9.81 * w.waveNumber)) < 1e-9);
    assert.ok(w.steepness >= 0 && w.steepness <= 0.12);
  }

  const total = a.reduce((sum, w) => sum + w.amplitude, 0);
  assert.ok(total > 0.2, `total amplitude ${total}`);
});

test('the Gerstner sum stays inside its amplitude envelope', () => {
  const waves = buildWaveSet({ waveHeight: 2.4, windStrength: 1.4 }, 7);
  const amplitudeSum = waves.reduce((s, w) => s + w.amplitude, 0);
  let worst = 0;
  const out = { x: 0, y: 0, z: 0, normalX: 0, normalY: 0, normalZ: 0 };
  for (let i = 0; i < 4000; i++) {
    const x = (i % 61) * 1.7 - 50;
    const z = ((i * 7) % 53) * 1.9 - 50;
    gerstnerAt(waves, x, z, i * 0.05, out);
    worst = Math.max(worst, Math.abs(out.y));
    assert.ok(Number.isFinite(out.normalX) && Number.isFinite(out.normalY) && Number.isFinite(out.normalZ));
    const len = Math.hypot(out.normalX, out.normalY, out.normalZ);
    assert.ok(Math.abs(len - 1) < 1e-9, `normal not unit: ${len}`);
  }
  assert.ok(worst <= amplitudeSum * 1.05, `peak ${worst} above envelope ${amplitudeSum}`);
  assert.ok(worst > amplitudeSum * 0.2, `sea looks flat: peak ${worst}`);
});

test('waves are continuous in time: no popping between frames', () => {
  const waves = buildWaveSet({}, 3);
  const dt = 1 / 60;
  const out = { x: 0, y: 0, z: 0, normalX: 0, normalY: 0, normalZ: 0 };
  let worst = 0;
  for (let i = 0; i < 300; i++) {
    const t = i * dt;
    const a = gerstnerAt(waves, 12.3, -4.5, t, out);
    const y0 = a.y;
    const n0 = { x: a.normalX, y: a.normalY, z: a.normalZ };
    const b = gerstnerAt(waves, 12.3, -4.5, t + dt, out);
    worst = Math.max(worst, Math.abs(b.y - y0));
    const dot = n0.x * b.normalX + n0.y * b.normalY + n0.z * b.normalZ;
    assert.ok(dot > 0.9, `normal jumped by more than 25 degrees in one frame: ${dot}`);
  }
  assert.ok(worst < 0.25, `height step per frame ${worst.toFixed(3)} m`);
});

test('calmer wind produces a calmer sea', () => {
  const calm = buildWaveSet({ windStrength: 0.15, waveHeight: 2 }, 5);
  const storm = buildWaveSet({ windStrength: 2, waveHeight: 2 }, 5);
  const height = (waves) => waves.reduce((s, w) => s + w.amplitude, 0);
  assert.ok(height(storm) > height(calm) * 2.5, `${height(storm)} vs ${height(calm)}`);
});

test('shallow water is turquoise, deep water is dark and blue', () => {
  const shallow = waterColorAtDepth({}, 2);
  const deep = waterColorAtDepth({}, 2200);
  const luminance = (c) => 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;

  assert.ok(luminance(shallow) > luminance(deep), 'deep water must be darker');
  assert.ok(shallow.g > shallow.r && shallow.b > shallow.r, 'shallow water should read turquoise');
  assert.ok(deep.b > deep.r * 3, 'deep water should read blue');
  for (const c of [shallow, deep]) {
    for (const channel of ['r', 'g', 'b']) {
      assert.ok(c[channel] >= 0 && c[channel] <= 1, `${channel} out of range: ${c[channel]}`);
    }
  }
});

test('absorption is monotone: deeper water never gets brighter', () => {
  let previous = Infinity;
  for (const depth of [0, 0.5, 1, 3, 8, 25, 90, 400, 2000, 5400]) {
    const c = waterColorAtDepth({}, depth);
    const luminance = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
    assert.ok(luminance <= previous + 1e-9, `brightness rose at depth ${depth}`);
    previous = luminance;
  }
});

test('foam hugs the shore and fades with depth', () => {
  const params = { foamWidth: 20, foamIntensity: 1 };
  const atShore = foamAt(params, 0.05);
  const knee = foamAt(params, 10);
  const offshore = foamAt(params, 60);
  assert.ok(atShore > 0.7, `shoreline foam ${atShore}`);
  assert.ok(knee < atShore && knee > 0);
  assert.equal(offshore, 0);

  const breaking = foamAt(params, 4, 1);
  assert.ok(breaking > foamAt(params, 4, 0), 'crest foam should add coverage');
  assert.ok(foamAt({ ...params, foamIntensity: 0 }, 0.01) === 0, 'foam intensity 0 must switch it off');
});

test('water depth follows the terrain field and is positive at sea', () => {
  const field = new PlanetField({ radius: 300000, seed: 44, landAmount: 0.35 });
  let ocean = 0;
  let land = 0;
  for (let i = 0; i < 220; i++) {
    const lat = (i * 37) % 170 - 85;
    const lon = (i * 53) % 350 - 175;
    const dir = directionFromLatLon(lat, lon);
    const depth = waterDepthAt(field, dir);
    if (depth > 0) ocean++;
    else land++;
    assert.ok(Number.isFinite(depth));
    const ground = field.surfaceRadius(dir, field.opsNear(dir, 0.02));
    assert.ok(Math.abs(depth - (field.radius - ground)) < 1e-6);
  }
  assert.ok(ocean > 20, `only ${ocean} ocean samples`);
  assert.ok(land > 10, `only ${land} land samples`);
});

test('the local water frame is orthonormal at any planet direction', () => {
  for (const dir of [directionFromLatLon(0, 0), directionFromLatLon(89, 10), directionFromLatLon(-45, 170)]) {
    const frame = localWaterFrame(dir, 0.7);
    const dot = (a, b) => a.x * b.x + a.y * b.y + a.z * b.z;
    assert.ok(Math.abs(dot(frame.up, frame.axisU)) < 1e-9);
    assert.ok(Math.abs(dot(frame.up, frame.axisV)) < 1e-9);
    assert.ok(Math.abs(dot(frame.axisU, frame.axisV)) < 1e-9);
    assert.ok(Math.abs(Math.hypot(frame.axisU.x, frame.axisU.y, frame.axisU.z) - 1) < 1e-9);
  }
});

test('sun, sky and Fresnel behave physically', () => {
  const sun = sunDirection({ sunAzimuth: 0, sunElevation: 0 });
  assert.ok(Math.abs(sun.x - 1) < 1e-9 && Math.abs(sun.y) < 1e-9);

  const horizon = skyRadiance({}, 0);
  const zenith = skyRadiance({}, Math.PI / 2);
  assert.ok(
    zenith.b - zenith.r > horizon.b - horizon.r,
    'zenith should be more saturated blue than the hazy horizon',
  );
  assert.ok(zenith.r < horizon.r, 'zenith should be less washed out than the horizon');

  assert.ok(schlickFresnel(0) > 0.9, 'grazing water is reflective');
  assert.ok(Math.abs(schlickFresnel(1) - 0.02) < 1e-9, 'head-on water is mostly transmissive');
});

test('ripple detail is small, smooth and deterministic', () => {
  const a = new RippleField(9);
  const b = new RippleField(9);
  const p = a.sample(12, -3);
  assert.deepEqual(p, b.sample(12, -3));

  let worst = 0;
  for (let i = 0; i < 200; i++) {
    const x = i * 0.7;
    const z = i * -0.3;
    const s0 = a.sample(x, z);
    const s1 = a.sample(x + 1e-3, z + 1e-3);
    worst = Math.max(worst, Math.abs(s0.dx - s1.dx), Math.abs(s0.dz - s1.dz));
    assert.ok(Math.abs(s0.dx) < 0.4 && Math.abs(s0.dz) < 0.4);
  }
  assert.ok(worst < 0.01, `ripple is not continuous: ${worst}`);
});

test('parameter normalisation rejects nonsense without throwing', () => {
  const p = normalizeOceanParams({ waveHeight: -5, foamWidth: NaN, waveCount: 99, choppiness: 4 });
  assert.ok(p.waveHeight >= 0);
  assert.equal(p.foamWidth, DEFAULT_OCEAN_PARAMS.foamWidth);
  assert.equal(p.waveCount, 12);
  assert.ok(p.choppiness <= 0.92);
});
