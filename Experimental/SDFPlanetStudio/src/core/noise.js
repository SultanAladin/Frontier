/**
 * Seeded 3D gradient noise and the fractal sums the planet field is built from.
 *
 * Everything the landmass base needs is sampled in 3D over the unit sphere, so
 * continents, ridgelines and coastlines wrap the planet with no seam at the
 * cube-face borders and no map projection to distort.
 *
 * The implementation is a classic Perlin lattice with a seeded permutation
 * table: deterministic per seed, no allocation in the sample path, and fast
 * enough that a whole terrain tile is built from a few thousand evaluations.
 */

const GRAD3 = new Float64Array([
  1, 1, 0, -1, 1, 0, 1, -1, 0, -1, -1, 0,
  1, 0, 1, -1, 0, 1, 1, 0, -1, -1, 0, -1,
  0, 1, 1, 0, -1, 1, 0, 1, -1, 0, -1, -1,
]);

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

/** Fade curve 6t^5 - 15t^4 + 10t^3: C2 continuous, the perlin standard. */
function fade(t) {
  return t * t * t * (t * (t * 6 - 15) + 10);
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

export class Noise3 {
  constructor(seed = 1) {
    const random = mulberry32(seed);
    const p = new Uint8Array(256);
    for (let i = 0; i < 256; i++) p[i] = i;
    for (let i = 255; i > 0; i--) {
      const j = Math.floor(random() * (i + 1));
      const tmp = p[i];
      p[i] = p[j];
      p[j] = tmp;
    }
    this.perm = new Uint8Array(512);
    this.permMod12 = new Uint8Array(512);
    for (let i = 0; i < 512; i++) {
      this.perm[i] = p[i & 255];
      this.permMod12[i] = this.perm[i] % 12;
    }
  }

  /** Perlin gradient noise in roughly [-1, 1]. */
  noise(x, y, z) {
    const perm = this.perm;
    const permMod12 = this.permMod12;

    const fx = Math.floor(x);
    const fy = Math.floor(y);
    const fz = Math.floor(z);

    const X = fx & 255;
    const Y = fy & 255;
    const Z = fz & 255;

    x -= fx;
    y -= fy;
    z -= fz;

    const u = fade(x);
    const v = fade(y);
    const w = fade(z);

    const A = perm[X] + Y;
    const AA = perm[A] + Z;
    const AB = perm[A + 1] + Z;
    const B = perm[X + 1] + Y;
    const BA = perm[B] + Z;
    const BB = perm[B + 1] + Z;

    const g000 = permMod12[AA] * 3;
    const g100 = permMod12[BA] * 3;
    const g010 = permMod12[AB] * 3;
    const g110 = permMod12[BB] * 3;
    const g001 = permMod12[AA + 1] * 3;
    const g101 = permMod12[BA + 1] * 3;
    const g011 = permMod12[AB + 1] * 3;
    const g111 = permMod12[BB + 1] * 3;

    const n000 = GRAD3[g000] * x + GRAD3[g000 + 1] * y + GRAD3[g000 + 2] * z;
    const n100 = GRAD3[g100] * (x - 1) + GRAD3[g100 + 1] * y + GRAD3[g100 + 2] * z;
    const n010 = GRAD3[g010] * x + GRAD3[g010 + 1] * (y - 1) + GRAD3[g010 + 2] * z;
    const n110 = GRAD3[g110] * (x - 1) + GRAD3[g110 + 1] * (y - 1) + GRAD3[g110 + 2] * z;
    const n001 = GRAD3[g001] * x + GRAD3[g001 + 1] * y + GRAD3[g001 + 2] * (z - 1);
    const n101 = GRAD3[g101] * (x - 1) + GRAD3[g101 + 1] * y + GRAD3[g101 + 2] * (z - 1);
    const n011 = GRAD3[g011] * x + GRAD3[g011 + 1] * (y - 1) + GRAD3[g011 + 2] * (z - 1);
    const n111 = GRAD3[g111] * (x - 1) + GRAD3[g111 + 1] * (y - 1) + GRAD3[g111 + 2] * (z - 1);

    return lerp(
      lerp(lerp(n000, n100, u), lerp(n010, n110, u), v),
      lerp(lerp(n001, n101, u), lerp(n011, n111, u), v),
      w,
    );
  }

  /** Fractal Brownian motion in roughly [-1, 1], normalised by the amplitude sum. */
  fbm(x, y, z, octaves = 5, lacunarity = 2.02, gain = 0.5) {
    let sum = 0;
    let amplitude = 1;
    let frequency = 1;
    let norm = 0;
    for (let i = 0; i < octaves; i++) {
      sum += this.noise(x * frequency, y * frequency, z * frequency) * amplitude;
      norm += amplitude;
      frequency *= lacunarity;
      amplitude *= gain;
    }
    return sum / norm;
  }

  /** Ridged multifractal in [0, 1]: sharp crests, the mountain-range octave set. */
  ridged(x, y, z, octaves = 5, lacunarity = 2.07, gain = 0.5) {
    let sum = 0;
    let amplitude = 1;
    let frequency = 1;
    let norm = 0;
    for (let i = 0; i < octaves; i++) {
      const n = 1 - Math.abs(this.noise(x * frequency, y * frequency, z * frequency));
      sum += n * n * amplitude;
      norm += amplitude;
      frequency *= lacunarity;
      amplitude *= gain;
    }
    return sum / norm;
  }
}

/**
 * Domain warp: offsets the sample position by a second noise field so coastlines
 * and ridges bend instead of reading as lattice-aligned blobs.
 */
export function warpedSample(noise, x, y, z, strength = 0.35, frequency = 1.7) {
  const wx = noise.noise(x * frequency + 11.3, y * frequency - 7.1, z * frequency + 3.7);
  const wy = noise.noise(x * frequency - 5.9, y * frequency + 13.7, z * frequency - 9.2);
  const wz = noise.noise(x * frequency + 17.1, y * frequency + 2.9, z * frequency + 21.4);
  return {
    x: x + wx * strength,
    y: y + wy * strength,
    z: z + wz * strength,
  };
}
