// Deterministic noise helpers. Everything about the dragon must be identical on
// every reload so the "sugar sprinkles" and "candy scales" never reshuffle.

export function mulberry32(seed) {
  let a = seed >>> 0;
  return function random() {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// Classic 3D value-noise on an integer lattice, trilinearly smoothed.
function hash3(x, y, z) {
  let h = Math.imul(x, 374761393) ^ Math.imul(y, 668265263) ^ Math.imul(z, 2147483647);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}

const fade = (t) => t * t * (3 - 2 * t);

export function valueNoise3(x, y, z) {
  const xi = Math.floor(x), yi = Math.floor(y), zi = Math.floor(z);
  const xf = fade(x - xi), yf = fade(y - yi), zf = fade(z - zi);
  const c000 = hash3(xi, yi, zi), c100 = hash3(xi + 1, yi, zi);
  const c010 = hash3(xi, yi + 1, zi), c110 = hash3(xi + 1, yi + 1, zi);
  const c001 = hash3(xi, yi, zi + 1), c101 = hash3(xi + 1, yi, zi + 1);
  const c011 = hash3(xi, yi + 1, zi + 1), c111 = hash3(xi + 1, yi + 1, zi + 1);
  const x00 = c000 + (c100 - c000) * xf, x10 = c010 + (c110 - c010) * xf;
  const x01 = c001 + (c101 - c001) * xf, x11 = c011 + (c111 - c011) * xf;
  const y0 = x00 + (x10 - x00) * yf, y1 = x01 + (x11 - x01) * yf;
  return y0 + (y1 - y0) * zf;
}

// Smooth pseudo-random undulation used for spine sway and wing motion.
export function fbm3(x, y, z) {
  return (
    valueNoise3(x, y, z) * 0.6 +
    valueNoise3(x * 2.03, y * 2.03, z * 2.03) * 0.28 +
    valueNoise3(x * 4.11, y * 4.11, z * 4.11) * 0.12
  );
}

export const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
export const lerp = (a, b, t) => a + (b - a) * t;
export const smoothstep = (a, b, t) => {
  const x = clamp((t - a) / (b - a), 0, 1);
  return x * x * (3 - 2 * x);
};

export function sampleCurve(points, t) {
  if (t <= points[0].x) return points[0].clone();
  const last = points[points.length - 1];
  if (t >= last.x) return last.clone();
  for (let i = 0; i < points.length - 1; i++) {
    const a = points[i], b = points[i + 1];
    if (t >= a.x && t <= b.x) {
      const f = (t - a.x) / (b.x - a.x || 1);
      return a.clone().lerp(b, f);
    }
  }
  return last.clone();
}
