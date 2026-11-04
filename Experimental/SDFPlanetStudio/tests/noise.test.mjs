import { test } from 'node:test';
import assert from 'node:assert/strict';

import { Noise3, warpedSample } from '../src/core/noise.js';

function sampleGrid(noise, samples = 4000, seed = 7) {
  let sum = 0;
  let sumSq = 0;
  let min = Infinity;
  let max = -Infinity;
  let state = seed;
  const random = () => {
    state = (state * 1664525 + 1013904223) >>> 0;
    return state / 4294967296;
  };
  for (let i = 0; i < samples; i++) {
    const n = noise.noise(random() * 40 - 20, random() * 40 - 20, random() * 40 - 20);
    sum += n;
    sumSq += n * n;
    if (n < min) min = n;
    if (n > max) max = n;
  }
  return { mean: sum / samples, variance: sumSq / samples - (sum / samples) ** 2, min, max };
}

test('noise is deterministic for a seed and differs across seeds', () => {
  const a = new Noise3(1234);
  const b = new Noise3(1234);
  const c = new Noise3(1235);

  let differing = 0;
  for (let i = 0; i < 200; i++) {
    const x = i * 0.37;
    const y = i * -0.11;
    const z = i * 0.23;
    assert.equal(a.noise(x, y, z), b.noise(x, y, z));
    if (Math.abs(a.noise(x, y, z) - c.noise(x, y, z)) > 1e-9) differing++;
  }
  assert.ok(differing > 180, `expected different seeds to decorrelate, got ${differing}/200`);
});

test('noise stays inside its documented range and has zero mean', () => {
  const stats = sampleGrid(new Noise3(99));
  assert.ok(stats.min > -1 && stats.max < 1, `range ${stats.min}..${stats.max}`);
  assert.ok(Math.abs(stats.mean) < 0.06, `mean ${stats.mean}`);
  assert.ok(stats.variance > 0.02, `variance ${stats.variance}`);
});

test('noise is continuous: small steps change the value by little', () => {
  const noise = new Noise3(5);
  let worst = 0;
  for (let i = 0; i < 500; i++) {
    const x = (i % 37) * 0.9;
    const y = ((i * 7) % 29) * 0.8;
    const z = ((i * 13) % 23) * 0.7;
    const base = noise.noise(x, y, z);
    const stepped = noise.noise(x + 1e-3, y + 1e-3, z + 1e-3);
    worst = Math.max(worst, Math.abs(base - stepped));
  }
  assert.ok(worst < 0.05, `worst step delta ${worst}`);
});

/**
 * Planet requirement: sampling the same physical point from either side of a
 * cube-face border must give the same value. 3D noise is evaluated on the unit
 * direction, so this holds by construction — the test pins it.
 */
test('noise is seamless across the cube-face boundary', () => {
  const noise = new Noise3(4242);
  const freq = 3.4;
  // +X face edge direction and -Z face edge direction name the same point.
  const dir = { x: 1, y: 0.35, z: -1 };
  const len = Math.hypot(dir.x, dir.y, dir.z);
  const unit = { x: dir.x / len, y: dir.y / len, z: dir.z / len };
  const nudged = { x: unit.x - 1e-7, y: unit.y + 1e-7, z: unit.z };

  const a = noise.fbm(unit.x * freq, unit.y * freq, unit.z * freq, 6);
  const b = noise.fbm(nudged.x * freq, nudged.y * freq, nudged.z * freq, 6);
  assert.ok(Math.abs(a - b) < 1e-5, `seam delta ${Math.abs(a - b)}`);
});

test('fbm and ridged stay normalised', () => {
  const noise = new Noise3(31);
  for (let i = 0; i < 800; i++) {
    const x = Math.sin(i * 0.71) * 9;
    const y = Math.cos(i * 1.13) * 9;
    const z = Math.sin(i * 0.31 + 2) * 9;
    const f = noise.fbm(x, y, z, 6);
    const r = noise.ridged(x, y, z, 5);
    assert.ok(Math.abs(f) <= 1.0001, `fbm out of range ${f}`);
    assert.ok(r >= 0 && r <= 1.0001, `ridged out of range ${r}`);
  }
});

test('domain warp perturbs within its strength budget', () => {
  const noise = new Noise3(8);
  const warped = warpedSample(noise, 0.3, -0.4, 0.5, 0.35, 1.7);
  const d = Math.hypot(warped.x - 0.3, warped.y + 0.4, warped.z - 0.5);
  assert.ok(d <= 0.35 * Math.sqrt(3) + 1e-9, `warp displacement ${d}`);
});
