/**
 * Permissive `three` stand-in for headless smoke tests.
 *
 * Every export is an auto-vivifying proxy: constructing "a class" yields an object
 * whose properties are callable, whose calls return more of the same, and which
 * accepts any assignment. That is enough to execute real application code paths —
 * constructors, frame loops, event handlers — on a machine with no WebGL, while
 * still letting genuine errors (undefined variables, missing methods on our own
 * classes, wrong argument counts that throw) surface.
 *
 * What it deliberately does *not* do: maths. Numbers come back as proxies or NaN.
 * This mock proves the app runs; it proves nothing about pixels.
 */

const NOOP = function () {};

function makeProxy(name = 'three') {
  const target = function () {};
  Object.defineProperty(target, 'name', { value: name, configurable: true });

  const proxy = new Proxy(target, {
    get(object, property) {
      if (property === Symbol.toPrimitive) return () => 0;
      if (property === Symbol.toStringTag) return name;
      if (property === Symbol.iterator) return undefined;
      if (property === 'then') return undefined; // never look like a promise
      if (property === 'prototype') return object.prototype;
      if (property === 'name') return name;
      if (property === 'constructor') return proxy;
      if (property === 'isProxy') return true;
      if (property === '__value') return 0;

      const nested = makeProxy(`${name}.${String(property)}`);
      // Cache so identity comparisons inside our own code stay stable.
      cache.set(property, nested);
      return nested;
    },
    set(object, property, value) {
      if (property === 'then') return true;
      object[property] = value;
      return true;
    },
    has() {
      return true;
    },
    apply() {
      return makeProxy(`${name}()`);
    },
    construct() {
      return makeProxy(`new ${name}`);
    },
  });

  const cache = new Map();
  return proxy;
}

const THREE = makeProxy('THREE');

/**
 * Materials that keep their `uniforms` object exactly as passed.
 *
 * This matters more than it looks: with a permissive proxy, `uniforms.anything`
 * returns another proxy, so `uniforms.uThing.value = x` silently succeeds even when
 * the uniform was never declared on the material. Real three.js throws
 * "Cannot set properties of undefined" there — which is precisely how a black
 * viewport happened. These two classes reproduce the real behaviour; everything else
 * in this file stays permissive.
 */
class StubMaterial {
  constructor(params = {}) {
    Object.assign(this, params);
    this.uniforms = params.uniforms ?? {};
    this.dispose = () => {};
    this.needsUpdate = true;
  }

  clone() {
    const copy = new this.constructor(this);
    copy.uniforms = { ...this.uniforms };
    return copy;
  }
}

class StubShaderMaterial extends StubMaterial {
  constructor(params = {}) {
    super(params);
    this.type = 'ShaderMaterial';
    this.isShaderMaterial = true;
  }
}

class StubBasicMaterial extends StubMaterial {
  constructor(params = {}) {
    super(params);
    this.type = 'MeshBasicMaterial';
    this.isMeshBasicMaterial = true;
  }
}

// Values that behave like real numbers/strings where the app does arithmetic or
// comparisons on them in control flow.
THREE.SRGBColorSpace = 'srgb';
THREE.ACESFilmicToneMapping = 4;
THREE.FrontSide = 0;
THREE.BackSide = 1;
THREE.DoubleSide = 2;
THREE.AdditiveBlending = 2;
THREE.NoToneMapping = 0;
THREE.RepeatWrapping = 1000;

/** A real Vector3: cheap to implement and used in genuine arithmetic by the app. */
export class Vector3 {
  constructor(x = 0, y = 0, z = 0) {
    this.x = x;
    this.y = y;
    this.z = z;
  }
  set(x, y, z) {
    this.x = x;
    this.y = y;
    this.z = z;
    return this;
  }
  copy(v) {
    this.x = v.x ?? 0;
    this.y = v.y ?? 0;
    this.z = v.z ?? 0;
    return this;
  }
  clone() {
    return new Vector3(this.x, this.y, this.z);
  }
  add(v) {
    this.x += v.x ?? 0;
    this.y += v.y ?? 0;
    this.z += v.z ?? 0;
    return this;
  }
  sub(v) {
    this.x -= v.x ?? 0;
    this.y -= v.y ?? 0;
    this.z -= v.z ?? 0;
    return this;
  }
  multiplyScalar(s) {
    this.x *= s;
    this.y *= s;
    this.z *= s;
    return this;
  }
  normalize() {
    const len = Math.hypot(this.x, this.y, this.z) || 1;
    return this.multiplyScalar(1 / len);
  }
  length() {
    return Math.hypot(this.x, this.y, this.z);
  }
  lengthSq() {
    return this.x * this.x + this.y * this.y + this.z * this.z;
  }
  dot(v) {
    return this.x * (v.x ?? 0) + this.y * (v.y ?? 0) + this.z * (v.z ?? 0);
  }
  crossVectors(a, b) {
    return this.set(
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
    );
  }
  unproject() {
    // The app maps screen coordinates back through the camera; the mock keeps the
    // vector as-is so the picking code still runs end to end.
    return this;
  }
  project() {
    return this;
  }
  applyMatrix4() {
    return this;
  }
  applyQuaternion() {
    return this;
  }
  setFromMatrixPosition() {
    return this;
  }
  distanceTo(v) {
    return Math.hypot(this.x - (v.x ?? 0), this.y - (v.y ?? 0), this.z - (v.z ?? 0));
  }
  toArray() {
    return [this.x, this.y, this.z];
  }
}

THREE.Vector3 = Vector3;
THREE.Vector2 = Vector3;
THREE.Vector4 = Vector3;
THREE.Color = Vector3;

// Named exports, because the app does `import * as THREE from 'three'` — a
// namespace import sees only real exports, never the proxy's properties.
export const Color = Vector3;
export const Vector2 = Vector3;
export const Vector4 = Vector3;
export const WebGLRenderer = THREE.WebGLRenderer;
export const Scene = THREE.Scene;
export const Group = THREE.Group;
export const Mesh = THREE.Mesh;
export const PerspectiveCamera = THREE.PerspectiveCamera;
export const ShaderMaterial = StubShaderMaterial;
export const MeshBasicMaterial = StubBasicMaterial;
export const BufferGeometry = THREE.BufferGeometry;
export const BufferAttribute = THREE.BufferAttribute;
export const SphereGeometry = THREE.SphereGeometry;
export const RingGeometry = THREE.RingGeometry;
export const Clock = THREE.Clock;
export const Quaternion = THREE.Quaternion;

export const SRGBColorSpace = 'srgb';
export const ACESFilmicToneMapping = 4;
export const FrontSide = 0;
export const BackSide = 1;
export const DoubleSide = 2;
export const AdditiveBlending = 2;
export const NoToneMapping = 0;

export default THREE;
