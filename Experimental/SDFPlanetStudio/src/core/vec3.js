/**
 * Minimal double-precision 3-vector helpers shared by the planet core.
 *
 * Positions here are absolute planet coordinates in metres, so every routine
 * stays in Float64 until a value is about to be packed into a GPU buffer.
 * The studio's own vec3 module is intentionally not imported: this prototype
 * must stay runnable under plain `node --test` without the app bundle.
 */

export function vec3(x = 0, y = 0, z = 0) {
  return { x, y, z };
}

export function add(a, b) {
  return { x: a.x + b.x, y: a.y + b.y, z: a.z + b.z };
}

export function sub(a, b) {
  return { x: a.x - b.x, y: a.y - b.y, z: a.z - b.z };
}

export function scale(a, s) {
  return { x: a.x * s, y: a.y * s, z: a.z * s };
}

export function dot(a, b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

export function cross(a, b) {
  return {
    x: a.y * b.z - a.z * b.y,
    y: a.z * b.x - a.x * b.z,
    z: a.x * b.y - a.y * b.x,
  };
}

export function length(a) {
  return Math.hypot(a.x, a.y, a.z);
}

export function lengthSquared(a) {
  return a.x * a.x + a.y * a.y + a.z * a.z;
}

export function normalize(a) {
  const len = Math.hypot(a.x, a.y, a.z);
  if (len < 1e-12) return { x: 0, y: 0, z: 0 };
  return { x: a.x / len, y: a.y / len, z: a.z / len };
}

export function lerp(a, b, t) {
  return { x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, z: a.z + (b.z - a.z) * t };
}

/** Angle between two unit vectors, guarded against domain error in the acos. */
export function angleBetween(a, b) {
  const d = Math.min(1, Math.max(-1, dot(a, b)));
  return Math.acos(d);
}

/** An orthonormal tangent basis for a unit direction (stable at the poles). */
export function tangentBasis(dir) {
  const axis = Math.abs(dir.y) < 0.9 ? { x: 0, y: 1, z: 0 } : { x: 1, y: 0, z: 0 };
  const t1 = normalize(cross(axis, dir));
  const t2 = cross(dir, t1);
  return [t1, t2];
}

export function clamp(value, min, max) {
  return value < min ? min : value > max ? max : value;
}

export function smoothstep(edge0, edge1, value) {
  const t = clamp((value - edge0) / (edge1 - edge0 || 1e-12), 0, 1);
  return t * t * (3 - 2 * t);
}
