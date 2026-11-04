/**
 * Ocean model: the numeric truth for the water surface, shared by the GPU shader
 * and by the offline image renderer so both show the same sea.
 *
 * What is modelled
 * ----------------
 *   * A directional Gerstner spectrum — each wave carries a steepness and a
 *     phase, so crests sharpen and troughs flatten the way gravity waves do,
 *     instead of the symmetric sine sum that reads as "fake water".
 *   * Depth-aware optics: the water colour comes from absorption over the path
 *     length to the sea floor, so a 2 m shelf is turquoise and a 3 km basin is
 *     near-black. The path length is the field's radial water depth, which the
 *     terrain mesher already evaluates per column.
 *   * Shore response: foam where the water is shallow, where the wave crest is
 *     steepening, and along a breaking band that follows the shoreline; the
 *     shoreline itself is the field's zero set, so it matches the mesh exactly.
 *   * Wind: wave direction, choppiness and the fine ripple spectrum are all
 *     derived from one wind vector and one wind strength, so the sea state is a
 *     single coherent authoring control.
 *
 * The sphere is treated as locally flat for wave displacement. At the scales the
 * camera sees (hundreds of metres of water) that is exact to well under a
 * centimetre; the alternative — evaluating gravity waves in spherical harmonics —
 * buys nothing visible.
 */

import { Noise3 } from './noise.js';
import { clamp, smoothstep } from './vec3.js';

/** Exponential atmosphere scale height, metres. */
export const ATMOSPHERE_SCALE_HEIGHT = 9500;

/**
 * Fraction of the sky's radiance at a given altitude above the sea sphere.
 *
 * Both the water's sky reflection and the terrain's aerial haze are scaled by it,
 * which is what makes the same shading code read correctly on the beach (dense
 * air: bright sky reflection, strong haze) and from orbit (no air: the ocean shows
 * its own body colour and the continents keep their albedo).
 */
export function airDensityAt(altitudeMetres) {
  return Math.exp(-Math.max(0, altitudeMetres) / ATMOSPHERE_SCALE_HEIGHT);
}

export const DEFAULT_OCEAN_PARAMS = Object.freeze({
  seaLevelOffset: 0,      // [m] relative to the planet's mean sea-level sphere
  windDirection: 0.6,     // [rad] around the local frame
  windStrength: 1.0,      // 0 calm .. 2 storm
  waveHeight: 1.15,       // [m] significant wave height at windStrength 1
  dominantWavelength: 34, // [m]
  choppiness: 0.48,       // 0 sine waves .. 0.9 sharp crests
  waveCount: 7,
  foamWidth: 26,          // [m] of water depth over which foam fades out
  foamIntensity: 0.85,
  shoreCrestFoam: 0.5,    // extra foam where crests break in shallow water
  absorption: 0.32,       // [1/m] red absorption (green/blue derived below)
  scattering: 0.055,      // [1/m] in-scattered light, keeps deep water blue
  sunAzimuth: 2.2,        // [rad]
  sunElevation: 0.62,     // [rad]
  rippleScale: 0.35,      // amplitude of the fine normal detail
});

export function normalizeOceanParams(input = {}) {
  const src = input && typeof input === 'object' ? input : {};
  const clampNumber = (value, fallback, min, max) => {
    const n = Number(value);
    if (!Number.isFinite(n)) return fallback;
    return Math.min(max, Math.max(min, n));
  };
  const d = DEFAULT_OCEAN_PARAMS;
  return {
    seaLevelOffset: clampNumber(src.seaLevelOffset, d.seaLevelOffset, -6000, 8000),
    windDirection: clampNumber(src.windDirection, d.windDirection, -Math.PI * 4, Math.PI * 4),
    windStrength: clampNumber(src.windStrength, d.windStrength, 0, 2),
    waveHeight: clampNumber(src.waveHeight, d.waveHeight, 0, 24),
    dominantWavelength: clampNumber(src.dominantWavelength, d.dominantWavelength, 3, 400),
    choppiness: clampNumber(src.choppiness, d.choppiness, 0, 0.92),
    waveCount: Math.round(clampNumber(src.waveCount, d.waveCount, 1, 12)),
    foamWidth: clampNumber(src.foamWidth, d.foamWidth, 0.5, 400),
    foamIntensity: clampNumber(src.foamIntensity, d.foamIntensity, 0, 1),
    shoreCrestFoam: clampNumber(src.shoreCrestFoam, d.shoreCrestFoam, 0, 1),
    absorption: clampNumber(src.absorption, d.absorption, 0.02, 3),
    scattering: clampNumber(src.scattering, d.scattering, 0, 1),
    sunAzimuth: clampNumber(src.sunAzimuth, d.sunAzimuth, -Math.PI * 4, Math.PI * 4),
    sunElevation: clampNumber(src.sunElevation, d.sunElevation, -1.4, 1.5),
    rippleScale: clampNumber(src.rippleScale, d.rippleScale, 0, 1),
  };
}

/**
 * Deterministic wave set from the authoring parameters.
 *
 * Wavelengths are spread geometrically around the dominant wavelength, each wave
 * gets an angle offset inside a widening fan around the wind, and amplitudes
 * follow a Phillips-like falloff (steeper for long waves) normalised so the sum
 * of amplitudes equals the significant wave height. Steepness is bounded below
 * the Gerstner self-intersection limit (sum of Q * k * A < 1).
 */
export function buildWaveSet(params, seed = 1337) {
  const p = normalizeOceanParams(params);
  const count = p.waveCount;
  const waves = [];
  const random = mulberry32(seed);

  // Geometric wavelength ladder: the long swell carries the shape, the short
  // waves carry the texture. Commensurate spacing (2.0x) makes the sum repeat into
  // a visible plaid, so the ratio is irrational-ish and jittered.
  const wavelengths = [];
  for (let i = 0; i < count; i++) {
    const ratio = Math.pow(2.05 + 0.22 * random(), i - (count - 1) * 0.18);
    wavelengths.push(p.dominantWavelength / ratio);
  }

  // Amplitude weights: long waves dominate, short waves add texture.
  let weightSum = 0;
  const weights = [];
  for (let i = 0; i < count; i++) {
    const w = Math.pow(wavelengths[i] / p.dominantWavelength, 0.75) * (0.72 + 0.56 * random());
    weights.push(w);
    weightSum += w;
  }

  const targetHeight = p.waveHeight * (0.25 + 0.75 * Math.min(1, p.windStrength));
  const amplitudeSum = targetHeight * 0.5;
  let steepnessBudget = p.choppiness * 0.55;

  for (let i = 0; i < count; i++) {
    const wavelength = wavelengths[i];
    const amplitude = (weights[i] / weightSum) * amplitudeSum;
    const k = (Math.PI * 2) / wavelength;
    // Directional spread: cos^2s-style spreading around the wind, walked out in
    // order and jittered, so no two components share a direction and the sum does
    // not settle into a standing-wave lattice.
    const t = count === 1 ? 0.5 : i / (count - 1);
    const fan = 1.15;
    const angle = p.windDirection + (t * 2 - 1) * fan + (random() * 2 - 1) * 0.32;
    const steepness = Math.min(0.12, steepnessBudget / Math.max(1, count));
    steepnessBudget -= steepness;

    waves.push({
      direction: { x: Math.cos(angle), z: Math.sin(angle) },
      wavelength,
      amplitude,
      waveNumber: k,
      phase: random() * Math.PI * 2,
      steepness, // Q in the Gerstner displacement
      angularFrequency: Math.sqrt(9.81 * k), // deep-water dispersion: omega = sqrt(g k)
    });
  }

  return waves;
}

/** Height of the Gerstner sum at a local water-space point (x, z) and time t. */
export function waveHeight(waves, x, z, time) {
  let y = 0;
  for (let i = 0; i < waves.length; i++) {
    const w = waves[i];
    const theta = w.waveNumber * (w.direction.x * x + w.direction.z * z) - w.angularFrequency * time + w.phase;
    y += w.amplitude * Math.cos(theta);
  }
  return y;
}

/**
 * Full Gerstner displacement and the analytic surface normal at (x, z).
 * Writes into `out` to stay allocation free in per-vertex loops.
 */
/**
 * Sample the displaced surface and its normal.
 *
 * `options.footprintMetres` is the world size of one pixel at this fragment. Waves
 * shorter than a few pixels are faded out instead of being point-sampled: without
 * it, a distant sea shimmers into moire, and a shoreline viewed at a grazing angle
 * breaks into rings. The fade is continuous in wavelength, so a wave thins out as
 * the camera pulls away rather than popping.
 */
export function gerstnerAt(waves, x, z, time, out, options = {}) {
  const footprint = options.footprintMetres ?? 0;
  let dx = 0;
  let dz = 0;
  let dy = 0;
  let tx = 0; // d/dx of displacement
  let ty = 0;
  let tz = 0;
  let bx = 0; // d/dz
  let by = 0;
  let bz = 0;

  for (let i = 0; i < waves.length; i++) {
    const w = waves[i];
    let fade = 1;
    if (footprint > 0) {
      const wavelength = (2 * Math.PI) / w.waveNumber;
      fade = smoothstep(1.6, 13, wavelength / footprint);
      if (fade <= 0.001) continue;
    }
    const theta = w.waveNumber * (w.direction.x * x + w.direction.z * z) - w.angularFrequency * time + w.phase;
    const sin = Math.sin(theta);
    const cos = Math.cos(theta);
    const qa = w.steepness * w.amplitude * fade;

    dx += qa * w.direction.x * cos;
    dz += qa * w.direction.z * cos;
    dy += w.amplitude * fade * sin;

    // Partial derivatives of the displaced surface.
    const k = w.waveNumber;
    tx += -w.steepness * w.amplitude * fade * w.direction.x * w.direction.x * k * sin;
    ty += w.amplitude * fade * w.direction.x * k * cos;
    tz += -w.steepness * w.amplitude * fade * w.direction.x * w.direction.z * k * sin;

    bx += -w.steepness * w.amplitude * fade * w.direction.z * w.direction.x * k * sin;
    by += w.amplitude * fade * w.direction.z * k * cos;
    bz += -w.steepness * w.amplitude * fade * w.direction.z * w.direction.z * k * sin;
  }

  const px = x + dx;
  const pz = z + dz;

  // Normal = normalize(cross(dP/dx, dP/dz)) with dP/dx = (1+tx, ty, tz).
  const ax = 1 + tx;
  const ay = ty;
  const az = tz;
  const cx = bx;
  const cy = by;
  const cz = 1 + bz;

  const nx = ay * cz - az * cy;
  const ny = az * cx - ax * cz;
  const nz = ax * cy - ay * cx;
  const len = Math.hypot(nx, ny, nz) || 1;

  out.x = px;
  out.y = dy;
  out.z = pz;
  out.normalX = nx / len;
  out.normalY = ny / len;
  out.normalZ = nz / len;
  return out;
}

/**
 * Water optics: colour of the water column over a given depth.
 *
 * Uses Beer-Lambert absorption of a down-and-up path, plus a scattering term so
 * deep water settles on a deep blue rather than black. Returns linear RGB.
 */
export function waterColorAtDepth(params, depthMetres, out = { r: 0, g: 0, b: 0 }) {
  const p = normalizeOceanParams(params);
  const depth = Math.max(0, depthMetres);
  const path = depth * 1.6; // down, scatter, and back up: a little more than 2d

  // Per-channel absorption: red dies first, then green, blue persists. Because
  // each channel is a mix between the bright shallow colour and a darker deep
  // colour with a monotone weight, brightness can never rise with depth.
  const alphaR = 1 - Math.exp(-p.absorption * path);
  const alphaG = 1 - Math.exp(-p.absorption * 0.42 * path);
  const alphaB = 1 - Math.exp(-p.absorption * 0.2 * path);

  const shallow = { r: 0.16, g: 0.42, b: 0.44 };
  const scatterLift = 1 + p.scattering * 0.6;
  const deep = { r: 0.012 * scatterLift, g: 0.055 * scatterLift, b: 0.105 * scatterLift };

  out.r = shallow.r * (1 - alphaR) + deep.r * alphaR;
  out.g = shallow.g * (1 - alphaG) + deep.g * alphaG;
  out.b = shallow.b * (1 - alphaB) + deep.b * alphaB;
  return out;
}

/**
 * Foam coverage: shallow-water foam that fades over `foamWidth`, plus crest foam
 * where a breaking wavelet passes and the water is already shallow.
 */
export function foamAt(params, depthMetres, crestFactor = 0) {
  const p = normalizeOceanParams(params);
  const depth = Math.max(0, depthMetres);
  const shore = 1 - smoothstep(0, p.foamWidth, depth);
  const crest = smoothstep(0.45, 1, crestFactor) * p.shoreCrestFoam * (1 - smoothstep(0, p.foamWidth * 6, depth));
  return clamp((shore * 0.85 + crest) * p.foamIntensity, 0, 1);
}

/**
 * Depth of water above the sea floor along a planet direction, in metres.
 * Positive over ocean, negative over land.
 */
export function waterDepthAt(field, dir, params = DEFAULT_OCEAN_PARAMS) {
  const p = normalizeOceanParams(params);
  const seaRadius = field.radius + p.seaLevelOffset;
  const ground = field.surfaceRadius(dir, field.opsNear(dir, 0.02));
  return seaRadius - ground;
}

/**
 * Local tangent frame for wave evaluation at a planet direction, so waves travel
 * flat across the curved sea surface instead of following meridians.
 */
export function localWaterFrame(dir, windDirection) {
  const up = { x: dir.x, y: dir.y, z: dir.z };
  const reference = Math.abs(up.y) < 0.95 ? { x: 0, y: 1, z: 0 } : { x: 1, y: 0, z: 0 };
  const east = normalizeCross(reference, up);
  const north = {
    x: up.y * east.z - up.z * east.y,
    y: up.z * east.x - up.x * east.z,
    z: up.x * east.y - up.y * east.x,
  };
  const windX = Math.cos(windDirection);
  const windZ = Math.sin(windDirection);
  return {
    up,
    axisU: { x: east.x * windX + north.x * windZ, y: east.y * windX + north.y * windZ, z: east.z * windX + north.z * windZ },
    axisV: { x: north.x * windX - east.x * windZ, y: north.y * windX - east.y * windZ, z: north.z * windX - east.z * windZ },
  };
}

/** Sun direction in planet space from azimuth/elevation. */
export function sunDirection(params) {
  const p = normalizeOceanParams(params);
  const ce = Math.cos(p.sunElevation);
  return {
    x: ce * Math.cos(p.sunAzimuth),
    y: Math.sin(p.sunElevation),
    z: ce * Math.sin(p.sunAzimuth),
  };
}

/** Fine ripple detail: two octaves of gradient noise, scaled by the ripple control. */
export class RippleField {
  constructor(seed = 991) {
    this.noise = new Noise3(seed);
  }

  /** Returns a small normal perturbation in the tangent plane. */
  sample(x, z, scale = 1) {
    const f = 0.09 * scale;
    const e = 0.35;
    const h = (px, pz) => this.noise.noise(px * f, pz * f, 0.37) + 0.5 * this.noise.noise(px * f * 2.7, pz * f * 2.7, 4.1);
    const hx = (h(x + e, z) - h(x - e, z)) / (2 * e);
    const hz = (h(x, z + e) - h(x, z - e)) / (2 * e);
    return { dx: -hx, dz: -hz };
  }
}

/** Sky radiance approximation used for the water's reflection lobe. */
export function skyRadiance(params, elevationRadians) {
  const p = normalizeOceanParams(params);
  const horizon = { r: 0.46, g: 0.55, b: 0.66 };
  const zenith = { r: 0.12, g: 0.26, b: 0.56 };
  const sunTint = Math.max(0, Math.sin(p.sunElevation)) * 0.25;
  const t = smoothstep(-0.15, 1.2, elevationRadians);
  return {
    r: horizon.r + (zenith.r + sunTint - horizon.r) * t,
    g: horizon.g + (zenith.g + sunTint * 0.85 - horizon.g) * t,
    b: horizon.b + (zenith.b + sunTint * 0.6 - horizon.b) * t,
  };
}

/** Fresnel reflectance for a dielectric water surface. */
export function schlickFresnel(cosTheta, f0 = 0.02) {
  const c = clamp(cosTheta, 0, 1);
  return f0 + (1 - f0) * Math.pow(1 - c, 5);
}

function normalizeCross(a, b) {
  const x = a.y * b.z - a.z * b.y;
  const y = a.z * b.x - a.x * b.z;
  const z = a.x * b.y - a.y * b.x;
  const len = Math.hypot(x, y, z) || 1;
  return { x: x / len, y: y / len, z: z / len };
}

function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
