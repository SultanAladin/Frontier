/**
 * Planet viewport: three.js scene, camera, worker pool, terrain and ocean layers.
 *
 * Responsibilities
 *   * keep a `PlanetField` authoritative and mirror it into the tile workers;
 *   * drive `TileManager` from the camera every frame and upload finished patches
 *     under a per-frame budget so streaming never stalls the image;
 *   * render the ocean as quadtree water patches that share the terrain's LOD, so
 *     water depth at the shoreline is per-vertex accurate rather than guessed;
 *   * pick the surface analytically (sphere-traced against the SDF, not raycast
 *     against the mesh), which means authoring is independent of mesh LOD;
 *   * own the camera controller and brush cursor.
 *
 * The editor UI talks to this class and never touches three.js.
 */

import * as THREE from 'three';

import { PlanetField } from '../core/planet-field.js';
import { TileManager } from '../core/tile-manager.js';
import { buildWaterTile } from '../core/water-tile.js';
import { buildTileMesh } from '../core/narrow-band.js';
import {
  airDensityAt,
  DEFAULT_OCEAN_PARAMS,
  buildWaveSet,
  normalizeOceanParams,
  sunDirection,
} from '../core/ocean-model.js';
import { directionFromLatLon, latLonFromDirection } from '../core/cube-sphere.js';
import { normalize } from '../core/vec3.js';

const TERRAIN_VERTEX = /* glsl */ `
precision highp float;

attribute float aElevation;
attribute float aSlope;

varying vec3 vWorld;
varying vec3 vNormal;
varying float vElevation;
varying float vSlope;

float hash13(vec3 p) {
  p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
  p *= 17.0;
  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float valueNoise3(vec3 p) {
  vec3 i = floor(p);
  vec3 f = fract(p);
  vec3 u = f * f * (3.0 - 2.0 * f);
  float n000 = hash13(i);
  float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
  float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
  float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
  float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
  float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
  float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
  float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
  return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
             mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

/**
 * Surface detail. Without it, a patch of ground at any distance is a single flat
 * albedo, which reads as untextured plastic no matter how good the geometry is.
 * Three octaves at 40 m / 9 m / 2 m, plus slope-driven exposure, is the cheapest
 * thing that looks like ground.
 */
float groundDetail(vec3 world, float slope) {
  float coarse = valueNoise3(world * 0.024);
  float medium = valueNoise3(world * 0.11);
  float fine = valueNoise3(world * 0.52);
  float detail = coarse * 0.5 + medium * 0.32 + fine * 0.18;
  // Steep ground breaks up more; flat ground gets a gentler mottle.
  return mix(detail, detail * 1.35 + 0.08, smoothstep(0.35, 1.2, slope));
}

void main() {
  vWorld = position;
  vNormal = normal;
  vElevation = aElevation;
  vSlope = aSlope;
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
`;

const TERRAIN_FRAGMENT = /* glsl */ `
precision highp float;

uniform vec3 uSunDirection;
uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uCameraPosition;
uniform float uSeaLevel;
uniform float uSnowLine;
uniform float uHazeDistance;
uniform float uAirDensity;
uniform float uTime;

varying vec3 vWorld;
varying vec3 vNormal;
varying float vElevation;
varying float vSlope;

float hash13(vec3 p) {
  p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
  p *= 17.0;
  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float valueNoise3(vec3 p) {
  vec3 i = floor(p);
  vec3 f = fract(p);
  vec3 u = f * f * (3.0 - 2.0 * f);
  float n000 = hash13(i);
  float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
  float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
  float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
  float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
  float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
  float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
  float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
  return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
             mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

/**
 * Surface detail. Without it, a patch of ground at any distance is a single flat
 * albedo, which reads as untextured plastic no matter how good the geometry is.
 * Three octaves at 40 m / 9 m / 2 m, plus slope-driven exposure, is the cheapest
 * thing that looks like ground.
 */
float groundDetail(vec3 world, float slope) {
  float coarse = valueNoise3(world * 0.024);
  float medium = valueNoise3(world * 0.11);
  float fine = valueNoise3(world * 0.52);
  float detail = coarse * 0.5 + medium * 0.32 + fine * 0.18;
  // Steep ground breaks up more; flat ground gets a gentler mottle.
  return mix(detail, detail * 1.35 + 0.08, smoothstep(0.35, 1.2, slope));
}

void main() {
  vec3 normal = normalize(vNormal);
  float height = vElevation;
  float slope = clamp(vSlope, 0.0, 4.0);

  // Terrain palette in linear space: wet sand, dry sand, grass, rock, scree, snow.
  vec3 wetSand   = vec3(0.085, 0.075, 0.060);
  vec3 drySand   = vec3(0.300, 0.255, 0.180);
  vec3 grass     = vec3(0.062, 0.120, 0.045);
  vec3 alpine    = vec3(0.115, 0.125, 0.080);
  vec3 rock      = vec3(0.115, 0.108, 0.100);
  vec3 scree     = vec3(0.175, 0.165, 0.155);
  vec3 snow      = vec3(0.780, 0.810, 0.860);

  // Slope drives rock exposure; height drives the biome bands.
  float rockiness = smoothstep(0.55, 1.35, slope);
  float snowiness = smoothstep(uSnowLine, uSnowLine + 700.0, height) * (1.0 - rockiness * 0.75);

  vec3 albedo = mix(drySand, grass, smoothstep(4.0, 55.0, height));
  albedo = mix(albedo, alpine, smoothstep(1100.0, 2100.0, height));
  albedo = mix(albedo, rock, rockiness);
  albedo = mix(albedo, scree, smoothstep(0.9, 1.8, slope));
  albedo = mix(albedo, snow, snowiness);

  // Shoreline: wet sand just above the waterline, and a dark seabed below it.
  float shoreBand = 1.0 - smoothstep(0.0, 10.0, abs(height - uSeaLevel));
  albedo = mix(albedo, wetSand, shoreBand * 0.75);
  float submerged = smoothstep(uSeaLevel, uSeaLevel - 12.0, height);
  albedo = mix(albedo, vec3(0.055, 0.075, 0.070), submerged);

  // Ground detail: albedo mottle plus a rock/sand breakup driven by the same noise.
  float detail = groundDetail(vWorld, slope);
  albedo *= 0.80 + 0.42 * detail;
  albedo = mix(albedo, rock * (0.75 + 0.55 * detail), smoothstep(0.6, 1.1, slope) * detail * 0.5);

  vec3 sun = normalize(uSunDirection);
  float ndl = max(dot(normal, sun), 0.0);
  float wrap = max(dot(normal, sun) * 0.5 + 0.5, 0.0);

  // Sky/ground ambient: a cheap hemispherical term.
  float upness = normal.y * 0.5 + 0.5;
  // Sky ambient: a slope that faces away from the sun is blue-lit, never black.
  vec3 ambient = mix(vec3(0.075, 0.085, 0.110), uSkyTop * 1.6 * uAirDensity, upness);

  vec3 colour = albedo * (vec3(1.0, 0.97, 0.92) * (ndl * 1.45 + wrap * 0.20) + ambient);
  colour += snow * pow(ndl, 2.0) * 0.05; // a touch of snow bounce

  // Distance haze: hides LOD transitions and gives the planet scale.
  vec3 toCamera = uCameraPosition - vWorld;
  float distanceToCamera = length(toCamera);
  float haze = (1.0 - exp(-distanceToCamera / max(1.0, uHazeDistance))) * uAirDensity;
  colour = mix(colour, uSkyHorizon * 0.85, clamp(haze, 0.0, 0.85));

  gl_FragColor = vec4(colour, 1.0);
}
`;

const WATER_VERTEX = /* glsl */ `
precision highp float;

attribute float aDepth;

uniform vec4 uWaveA[8];
uniform vec4 uWaveB[8];
uniform int uWaveCount;
uniform float uTime;
uniform float uSeaRadius;

varying vec3 vWorld;
varying float vDepth;
varying vec2 vWaveCoord;
varying vec3 vUp;

void main() {
  vec3 up = normalize(position);
  // Waves are evaluated in a planar frame anchored at the patch, which keeps the
  // vertex cost independent of the sphere and avoids per-patch seam work.
  vec2 coord = position.xz;

  float displacement = 0.0;
  for (int i = 0; i < 8; i++) {
    if (i >= uWaveCount) break;
    float k = uWaveA[i].w;
    vec2 dir = uWaveA[i].xy;
    float amplitude = uWaveA[i].z;
    float steepness = uWaveB[i].x;
    float phase = uWaveB[i].y;
    float omega = uWaveB[i].z;
    float theta = k * dot(dir, coord) - omega * uTime + phase;
    displacement += amplitude * sin(theta) * (1.0 - 0.65 * steepness);
  }

  vec3 displaced = position + up * displacement;
  vWorld = displaced;
  vDepth = aDepth;
  vWaveCoord = coord;
  vUp = up;

  gl_Position = projectionMatrix * modelViewMatrix * vec4(displaced, 1.0);
}
`;

const WATER_FRAGMENT = /* glsl */ `
precision highp float;

uniform vec4 uWaveA[8];
uniform vec4 uWaveB[8];
uniform int uWaveCount;
uniform float uTime;

uniform vec3 uSunDirection;
uniform vec3 uCameraPosition;
uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform float uAirDensity;

uniform float uFoamWidth;
uniform float uFoamIntensity;
uniform float uCrestFoam;
uniform float uAbsorption;
uniform float uScattering;
uniform float uRipple;
// World metres covered by one pixel at unit distance (2 * tan(fov/2) / canvasHeight).
uniform float uPixelAngle;

varying vec3 vWorld;
varying float vDepth;
varying vec2 vWaveCoord;
varying vec3 vUp;

float hash21(vec2 p) {
  return fract(sin(dot(p, vec2(41.13, 289.7))) * 43758.5453);
}

float valueNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), u.x),
             mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), u.x), u.y);
}

/**
 * Gerstner normal from the analytic derivative of the same spectrum.
 *
 * The footprint argument is the world size of this pixel. Waves shorter than a few
 * pixels are faded out rather than point-sampled: a distant sea would otherwise
 * shimmer into moire and a grazing shoreline would break into rings. Same rule as
 * gerstnerAt(..., { footprintMetres }) in ocean-model.js.
 */
vec3 waveNormal(vec2 coord, float footprint) {
  float dx = 0.0;
  float dz = 0.0;
  float dyx = 0.0;
  float dyz = 0.0;
  for (int i = 0; i < 8; i++) {
    if (i >= uWaveCount) break;
    float k = uWaveA[i].w;
    vec2 dir = uWaveA[i].xy;
    float amplitude = uWaveA[i].z;
    float steepness = uWaveB[i].x;
    float phase = uWaveB[i].y;
    float omega = uWaveB[i].z;

    float wavelength = 6.2831853 / max(k, 1e-5);
    float fade = smoothstep(1.6, 13.0, wavelength / max(footprint, 1e-4));
    if (fade <= 0.001) continue;

    float theta = k * dot(dir, coord) - omega * uTime + phase;
    float s = sin(theta);
    float c = cos(theta);
    dx += -steepness * amplitude * fade * dir.x * dir.x * k * s;
    dz += -steepness * amplitude * fade * dir.y * dir.y * k * s;
    dyx += amplitude * fade * dir.x * k * c;
    dyz += amplitude * fade * dir.y * k * c;
  }

  // Ripple detail: value noise at a ~3 m period, faded by the same footprint rule.
  float rippleFade = smoothstep(1.6, 13.0, 3.0 / max(footprint, 1e-4));
  float e = 0.35;
  float n0 = valueNoise(coord * 0.35);
  float nx = valueNoise((coord + vec2(e, 0.0)) * 0.35);
  float nz = valueNoise((coord + vec2(0.0, e)) * 0.35);
  dyx += (nx - n0) / e * uRipple * 0.5 * rippleFade;
  dyz += (nz - n0) / e * uRipple * 0.5 * rippleFade;

  return normalize(vec3(-(dx + dyx), 1.0, -(dz + dyz)));
}

void main() {
  float depth = max(vDepth, 0.0);

  // Local frame: replace the planar normal with one in the patch's tangent plane.
  vec3 up = normalize(vUp);
  vec3 east = normalize(cross(abs(up.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), up));
  vec3 north = cross(up, east);
  float viewDistance = length(uCameraPosition - vWorld);
  // Metres per pixel: drives the wave LOD, so detail disappears smoothly instead
  // of aliasing.
  float footprint = max(viewDistance * uPixelAngle, 0.01);
  vec3 planar = waveNormal(vWaveCoord, footprint);
  vec3 normal = normalize(up * planar.y + east * planar.x + north * planar.z);

  vec3 view = normalize(uCameraPosition - vWorld);
  vec3 sun = normalize(uSunDirection);

  float fresnel = min(0.80, 0.02 + 0.98 * pow(1.0 - clamp(dot(normal, view), 0.0, 1.0), 4.0));

  // Reflection: sky gradient plus a sun glitter lobe.
  vec3 reflected = reflect(-view, normal);
  // From orbit there is no sky to reflect; the surface shows its own colour plus
  // the sun lobe, which is why shallow shelves read as turquoise from space.
  vec3 skyGradient = mix(uSkyHorizon, uSkyTop, clamp(dot(reflected, up) * 0.5 + 0.5, 0.0, 1.0));
  // Beyond a few kilometres the wave normals are gone, so the reflection would be a
  // perfect mirror of a flat sky and the sea would read as white paint. Reflecting a
  // blurred sky, and keeping the water's own tint, keeps a distant sea looking like
  // water at every distance.
  vec3 blurredSky = mix(uSkyHorizon, uSkyTop, 0.42);
  skyGradient = mix(skyGradient, blurredSky, smoothstep(2000.0, 14000.0, viewDistance));
  vec3 sky = mix(vec3(0.012, 0.018, 0.032), skyGradient, uAirDensity);
  sky *= 0.82;
  sky.b += 0.03;
  // Two lobes: a tight sun disc and a narrow glitter path. A wide low-power lobe
  // spreads the sun into a white wedge across half the sea; a real glitter path is
  // a narrow road of sparkles under the sun.
  float sunAlign = max(dot(reflected, sun), 0.0);
  float specular = pow(sunAlign, 600.0) * 2.4;
  float sunGlitter = pow(sunAlign, 180.0) * 0.22;

  // Water body colour: Beer-Lambert absorption over the depth, per channel.
  float path = depth * 1.6;
  vec3 shallow = vec3(0.16, 0.42, 0.44);
  vec3 deep = vec3(0.012, 0.055, 0.105) * (1.0 + uScattering * 0.6);
  vec3 body = shallow * exp(-vec3(uAbsorption, uAbsorption * 0.42, uAbsorption * 0.2) * path)
            + deep * (1.0 - exp(-vec3(uAbsorption, uAbsorption * 0.42, uAbsorption * 0.2) * path * 0.55));

  // Foam: a shoreline band, a crest band, and a noise breakup so it reads as froth.
  float shore = 1.0 - smoothstep(0.0, max(0.5, uFoamWidth), depth);
  float crestDrive = smoothstep(0.55, 1.0, planar.y);
  float crest = crestDrive * uCrestFoam * (1.0 - smoothstep(0.0, max(1.0, uFoamWidth * 6.0), depth));

  float foamNoise = valueNoise(vWaveCoord * 0.22 + vec2(uTime * 0.06, -uTime * 0.045));
  foamNoise = mix(foamNoise, valueNoise(vWaveCoord * 0.9 - vec2(uTime * 0.12, uTime * 0.08)), 0.45);
  float foam = clamp((shore * 0.9 + crest) * uFoamIntensity, 0.0, 1.0);
  foam *= smoothstep(0.25, 0.75, foamNoise + 0.25 * shore);

  vec3 colour = mix(body, sky, clamp(fresnel, 0.0, 0.92));
  colour += vec3(1.0, 0.97, 0.92) * (specular + sunGlitter);
  colour = mix(colour, vec3(0.86, 0.90, 0.93), foam * 0.92);

  // Shallow water is see-through so the sand and rocks below stay visible.
  float alpha = mix(0.42, 0.98, smoothstep(0.0, 42.0, depth));
  alpha = clamp(alpha + fresnel * 0.45 + foam * 0.75, 0.0, 1.0);

  gl_FragColor = vec4(colour, alpha);
}
`;

const SKY_VERTEX = /* glsl */ `
precision highp float;
varying vec3 vDirection;
void main() {
  vDirection = normalize(position);
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
`;

const SKY_FRAGMENT = /* glsl */ `
precision highp float;
uniform vec3 uSunDirection;
uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uGround;
uniform vec3 uLocalUp;
uniform float uAirDensity;
varying vec3 vDirection;

void main() {
  vec3 dir = normalize(vDirection);
  // Height above the *local* horizon, not world +Y: on a planet those differ by
  // the camera's latitude, and using world Y tilts the whole sky gradient.
  float height = clamp(dot(dir, normalize(uLocalUp)), -1.0, 1.0);
  float t = pow(clamp(height * 0.5 + 0.5, 0.0, 1.0), 0.65);
  vec3 colour = mix(uSkyHorizon, uSkyTop, t);
  if (height < 0.0) colour = mix(uSkyHorizon, uGround, clamp(-height * 3.5, 0.0, 1.0));
  // Thin air at altitude: fade the dome toward space instead of keeping a blue bowl.
  colour = mix(vec3(0.004, 0.006, 0.012), colour, uAirDensity);

  vec3 sun = normalize(uSunDirection);
  float sunAmount = max(dot(dir, sun), 0.0);
  colour += vec3(1.0, 0.92, 0.78) * pow(sunAmount, 900.0) * 8.0;
  colour += vec3(1.0, 0.86, 0.66) * pow(sunAmount, 12.0) * 0.16;

  gl_FragColor = vec4(colour, 1.0);
}
`;

//----------------------------------------------------------------------------------------------------------------------
//                                                       WORKER POOL
//----------------------------------------------------------------------------------------------------------------------

const JOB_TIMEOUT_MS = 4000;

export class TileWorkerPool {
  constructor(count, { onResync, fallback = null, onDiagnostic = null } = {}) {
    this.workers = [];
    this.pending = new Map();
    this.jobId = 0;
    this.fallback = fallback;
    this.onDiagnostic = onDiagnostic;
    this.failures = 0;
    this.degraded = false;
    this.revision = 0;
    this.onResync = onResync ?? (() => {});
    this.load = [];

    for (let i = 0; i < count; i++) {
      const worker = new Worker(new URL('../worker/tile-worker.js', import.meta.url), { type: 'module' });
      worker.onmessage = (event) => this.#handle(i, event.data);
      worker.onerror = (event) => {
        console.error('[tile-worker]', event.message ?? event);
        this.#noteFailure(null, new Error(event.message ?? 'worker error'), null);
      };
      this.workers.push(worker);
      this.load.push(0);
    }
  }

  #handle(workerIndex, message) {
    switch (message.type) {
      case 'tile': {
        const entry = this.pending.get(message.jobId);
        if (!entry) return;
        clearTimeout(entry.timer);
        entry.resolve({
          positions: message.positions,
          normals: message.normals,
          elevation: message.elevation,
          slope: message.slope,
          indices: message.indices,
          vertexCount: message.vertexCount,
          triangleCount: message.triangleCount,
          stats: message.stats,
        });
        this.pending.delete(message.jobId);
        this.load[workerIndex] = Math.max(0, this.load[workerIndex] - 1);
        break;
      }
      case 'error': {
        const entry = this.pending.get(message.jobId);
        if (entry) {
          clearTimeout(entry.timer);
          this.pending.delete(message.jobId);
          this.#noteFailure(null, new Error(message.message), entry.reject);
        }
        this.load[workerIndex] = Math.max(0, this.load[workerIndex] - 1);
        break;
      }
      case 'resync':
        this.onResync(workerIndex, message);
        break;
      default:
        break;
    }
  }

  send(workerIndex, message, transfer = []) {
    this.workers[workerIndex].postMessage(message, transfer);
  }

  setField(params, ops, revision, options) {
    this.revision = revision;
    for (let i = 0; i < this.workers.length; i++) {
      this.send(i, { type: 'field', params, ops, revision, options });
    }
  }

  appendOp(op, revision) {
    this.revision = revision;
    for (let i = 0; i < this.workers.length; i++) {
      this.send(i, { type: 'op', op, revision });
    }
  }

  build(tile, fallback = this.fallback) {
    // A worker that failed to start (module workers blocked, bad MIME type, CSP)
    // would otherwise leave the viewport permanently empty. After two failures the
    // pool meshes on the main thread instead of showing nothing.
    if (this.degraded && fallback) {
      return Promise.resolve().then(() => fallback(tile));
    }

    const id = ++this.jobId;
    // Least loaded worker keeps the queue balanced without a scheduler.
    let best = 0;
    for (let i = 1; i < this.load.length; i++) if (this.load[i] < this.load[best]) best = i;
    this.load[best]++;

    return new Promise((resolve, reject) => {
      // A worker that never answers is indistinguishable from one that is slow, so a
      // job that misses its deadline counts as a failure and hands the patch to the
      // fallback. Without this, a worker that failed to load left the pool waiting
      // forever and the planet stayed empty.
      const timer = setTimeout(() => {
        const entry = this.pending.get(id);
        if (!entry) return;
        this.pending.delete(id);
        this.load[best] = Math.max(0, this.load[best] - 1);
        this.#noteFailure(tile, new Error(`worker ${best} did not answer within ${JOB_TIMEOUT_MS} ms`), entry.reject);
      }, JOB_TIMEOUT_MS);

      this.pending.set(id, { resolve, reject, timer });
      try {
        this.send(best, { type: 'build', jobId: id, tile });
      } catch (error) {
        clearTimeout(timer);
        this.pending.delete(id);
        this.load[best] = Math.max(0, this.load[best] - 1);
        this.#noteFailure(tile, error, reject);
      }
    }).catch((error) => {
      if (!fallback) throw error;
      return fallback(tile);
    });
  }

  /** Record a worker-side failure and degrade to main-thread meshing if it repeats. */
  #noteFailure(tile, error, reject) {
    this.failures++;
    this.onDiagnostic?.(
      `tile worker failed (${this.failures}): ${error?.message ?? error} — meshing on the main thread`,
    );
    if (this.failures >= 2) this.degraded = true;
    if (reject) reject(error instanceof Error ? error : new Error(String(error)));
  }

  dispose() {
    for (const worker of this.workers) worker.terminate();
    this.workers = [];
    this.pending.clear();
  }
}

//----------------------------------------------------------------------------------------------------------------------
//                                                         VIEWPORT
//----------------------------------------------------------------------------------------------------------------------

export class PlanetViewport {
  constructor(canvas, options = {}) {
    this.canvas = canvas;
    this.field = options.field ?? new PlanetField(options.params ?? {});
    this.oceanParams = normalizeOceanParams({ ...DEFAULT_OCEAN_PARAMS, ...(options.ocean ?? {}) });
    this.onStats = options.onStats ?? (() => {});
    this.onCursor = options.onCursor ?? (() => {});
    this.onDiagnostic = options.onDiagnostic ?? (() => {});

    this.tool = 'orbit';
    this.brush = { radius: 4200, strength: 320, falloff: 0.5 };
    this.showOcean = options.showOcean ?? true;
    this.showWireframe = false;

    this.stats = {
      fps: 0,
      frameMs: 0,
      uploads: 0,
      triangles: 0,
      drawCalls: 0,
      lastOp: null,
    };

    // Frame-loop state exists before anything that can touch it: `teleportTo`
    // (called from the camera setup) resets the upload queue.
    this.uploadQueue = [];
    this.accumulated = 0;
    this.frames = 0;
    this.lastFrame = performance.now();
    this.time = 0;
    this.running = true;

    this.#initRenderer();
    this.#initScene();
    // The camera has to exist before the first resize: resize reads its
    // projection and derives the water shader's radians-per-pixel from it.
    this.#initCamera();
    this.resize();
    this.#initLayers();
    // The water material exists only now, and it needs the radians-per-pixel value
    // resize() derives from the camera.
    this.resize();
    this.#initCursor();
    this.#installInput();

    this.clock = new THREE.Clock();
    this.animate = this.animate.bind(this);
    requestAnimationFrame(this.animate);
  }

  //---------------------------------------------------------------- RENDERER

  #initRenderer() {
    this.renderer = new THREE.WebGLRenderer({
      canvas: this.canvas,
      antialias: true,
      // Deliberately *not* logarithmicDepthBuffer: these are raw ShaderMaterials
      // without three's log-depth chunks, and mixing log and non-log depth writes
      // makes geometry vanish. Precision comes from a near plane that scales with
      // the orbit distance instead (see #updateCamera).
      powerPreference: 'high-performance',
    });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.renderer.setSize(this.canvas.clientWidth || 1, this.canvas.clientHeight || 1, false);
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.05;
    this.renderer.autoClear = true;

    this.resizeObserver = new ResizeObserver(() => this.resize());
    this.resizeObserver.observe(this.canvas.parentElement ?? this.canvas);
  }

  resize() {
    if (!this.renderer) return;
    const width = this.canvas.clientWidth || 1;
    const height = this.canvas.clientHeight || 1;
    this.renderer.setSize(width, height, false);
    if (!this.camera) return;
    this.camera.aspect = width / height;
    this.camera.updateProjectionMatrix();

    // Radians per pixel: the water shader turns it into metres-per-pixel at any
    // depth and fades waves shorter than a few pixels, which is what keeps a
    // distant sea from shimmering.
    const pixelAngle = (2 * Math.tan((this.camera.fov * Math.PI) / 360)) / height;
    if (this.waterMaterial?.uniforms?.uPixelAngle) {
      this.waterMaterial.uniforms.uPixelAngle.value = pixelAngle;
    }
  }

  //------------------------------------------------------------------ SCENE

  #initScene() {
    this.scene = new THREE.Scene();
    this.sunDirection = sunDirection(this.oceanParams);

    this.skyUniforms = {
      uSunDirection: { value: new THREE.Vector3(this.sunDirection.x, this.sunDirection.y, this.sunDirection.z) },
      uSkyTop: { value: new THREE.Color(0.12, 0.26, 0.56) },
      uSkyHorizon: { value: new THREE.Color(0.62, 0.71, 0.80) },
      uGround: { value: new THREE.Color(0.09, 0.10, 0.11) },
      uLocalUp: { value: new THREE.Vector3(0, 1, 0) },
      uAirDensity: { value: 1 },
    };

    const skyGeometry = new THREE.SphereGeometry(this.field.radius * 2.4, 32, 24);
    const skyMaterial = new THREE.ShaderMaterial({
      uniforms: this.skyUniforms,
      vertexShader: SKY_VERTEX,
      fragmentShader: SKY_FRAGMENT,
      side: THREE.BackSide,
      depthWrite: false,
      depthTest: false,
    });
    this.skyRadius = this.field.radius * 2.4;
    this.sky = new THREE.Mesh(skyGeometry, skyMaterial);
    this.sky.renderOrder = -10;
    this.sky.frustumCulled = false;
    this.scene.add(this.sky);
  }

  #initCamera() {
    this.camera = new THREE.PerspectiveCamera(58, 1.6, 0.4, this.field.radius * 8);
    this.camera.position.set(this.field.radius * 1.6, this.field.radius * 0.4, 0);

    this.focusDir = normalize({ x: 0.72, y: 0.34, z: 0.6 });
    this.focusRadius = this.field.radius;
    this.orbitYaw = 0.6;
    this.orbitPitch = 0.42;
    this.viewDistance = 2600;
    this.keys = new Set();

    this.teleportTo(directionFromLatLon(12, 24), 2600);
  }

  //----------------------------------------------------------------- LAYERS

  #initLayers() {
    const terrainOptions = { cellsPerTile: 48, maxTiles: 420, maxJobsInFlight: 4, minCellSize: 1.5 };
    const waterOptions = { cellsPerTile: 24, maxTiles: 220, maxJobsInFlight: 2, minCellSize: 6 };

    this.workers = new TileWorkerPool(
      Math.max(2, Math.min(4, (navigator.hardwareConcurrency || 4) - 1)),
      {
        onResync: () => this.resyncWorkers(),
        onDiagnostic: (message) => this.onDiagnostic?.(message),
        // Per-layer main-thread meshing keeps the scene alive when workers cannot
        // run; TileManager passes the right fallback for its own layer.
      },
    );
    this.workerOptions = { cellsPerTile: terrainOptions.cellsPerTile, skirts: true };

    this.terrainMaterial = new THREE.ShaderMaterial({
      uniforms: {
        uSunDirection: this.skyUniforms.uSunDirection,
        uSkyTop: this.skyUniforms.uSkyTop,
        uSkyHorizon: this.skyUniforms.uSkyHorizon,
        uCameraPosition: { value: new THREE.Vector3() },
        uSeaLevel: { value: 0 },
        uSnowLine: { value: 2400 },
        uHazeDistance: { value: 90000 },
        uAirDensity: { value: 1 },
        uTime: { value: 0 },
      },
      vertexShader: TERRAIN_VERTEX,
      fragmentShader: TERRAIN_FRAGMENT,
      wireframe: false,
    });

    this.waterMaterial = this.#createWaterMaterial();
    this.terrainGroup = new THREE.Group();
    this.waterGroup = new THREE.Group();
    this.scene.add(this.terrainGroup, this.waterGroup);

    this.terrain = new TileManager({
      field: this.field,
      options: terrainOptions,
      build: (tile) => this.workers.build(tile, (pending) => this.#buildTerrainLocally(pending)),
      onTileReady: (tile, mesh) => this.uploadQueue.push({ tile, mesh, water: false }),
      onTileDropped: (tile) => this.#dropMesh(tile, false),
    });

    this.water = new TileManager({
      field: this.field,
      options: waterOptions,
      build: (tile) => this.workers.build(tile, (pending) => this.#buildWaterLocally(pending)),
      onTileReady: (tile, mesh) => this.uploadQueue.push({ tile, mesh, water: true }),
      onTileDropped: (tile) => this.#dropMesh(tile, true),
    });

    this.terrainMeshes = new Map();
    this.waterMeshes = new Map();

    this.workers.setField(
      this.field.params,
      this.field.serializeEdits(),
      this.terrain.revision,
      this.workerOptions,
    );
  }

  /**
   * Main-thread build, used only when the worker pool has failed. Same meshers,
   * synchronous, which is why it is the fallback and not the default.
   */
  #buildTerrainLocally(tile) {
    return buildTileMesh(this.field, tile, this.workerOptions);
  }

  #buildWaterLocally(tile) {
    return buildWaterTile(this.field, tile, {
      cells: this.water.options.cellsPerTile,
      seaLevelOffset: this.oceanParams.seaLevelOffset,
    });
  }

  #createWaterMaterial() {
    const waveA = [];
    const waveB = [];
    for (let i = 0; i < 8; i++) {
      waveA.push(new THREE.Vector4(1, 0, 0, 0));
      waveB.push(new THREE.Vector4(0, 0, 0, 0));
    }

    const material = new THREE.ShaderMaterial({
      uniforms: {
        uWaveA: { value: waveA },
        uWaveB: { value: waveB },
        uWaveCount: { value: 0 },
        uTime: { value: 0 },
        uSeaRadius: { value: this.field.radius },
        uSunDirection: this.skyUniforms.uSunDirection,
        uCameraPosition: { value: new THREE.Vector3() },
        uSkyTop: this.skyUniforms.uSkyTop,
        uSkyHorizon: this.skyUniforms.uSkyHorizon,
        uFoamWidth: { value: this.oceanParams.foamWidth },
        uFoamIntensity: { value: this.oceanParams.foamIntensity },
        uCrestFoam: { value: this.oceanParams.shoreCrestFoam },
        uAbsorption: { value: this.oceanParams.absorption },
        uScattering: { value: this.oceanParams.scattering },
        uRipple: { value: this.oceanParams.rippleScale },
        // Shared with the sky: one object, written once per frame, so it cannot be
        // missing here and present there.
        uAirDensity: this.skyUniforms.uAirDensity,
        // Radians per screen pixel; resize() overwrites it every time the canvas or
        // the field of view changes.
        uPixelAngle: { value: 0.0015 },
      },
      vertexShader: WATER_VERTEX,
      fragmentShader: WATER_FRAGMENT,
      transparent: true,
      depthWrite: false,
      // The sea surface and the sand meet at exactly zero depth. Pulling the water
      // one depth unit toward the camera stops the shoreline from z-fighting while
      // keeping submerged terrain correctly occluded.
      polygonOffset: true,
      polygonOffsetFactor: -1,
      polygonOffsetUnits: -4,
      side: THREE.FrontSide,
    });

    this.updateWaveUniforms(material);
    return material;
  }

  updateWaveUniforms(material = this.waterMaterial) {
    const waves = buildWaveSet(this.oceanParams, 1337);
    const count = Math.min(8, waves.length);
    for (let i = 0; i < 8; i++) {
      const wave = waves[i];
      if (!wave) {
        material.uniforms.uWaveA.value[i].set(1, 0, 0, 0);
        material.uniforms.uWaveB.value[i].set(0, 0, 0, 0);
        continue;
      }
      material.uniforms.uWaveA.value[i].set(wave.direction.x, wave.direction.z, wave.amplitude, wave.waveNumber);
      material.uniforms.uWaveB.value[i].set(wave.steepness, wave.phase, wave.angularFrequency, 0);
    }
    material.uniforms.uWaveCount.value = count;
  }

  //----------------------------------------------------------------- CURSOR

  #initCursor() {
    const geometry = new THREE.RingGeometry(0.94, 1.0, 64);
    const material = new THREE.MeshBasicMaterial({
      color: 0x4ade80,
      transparent: true,
      opacity: 0.85,
      side: THREE.DoubleSide,
      depthTest: false,
    });
    this.cursor = new THREE.Mesh(geometry, material);
    this.cursor.visible = false;
    this.cursor.renderOrder = 20;
    this.scene.add(this.cursor);

    const innerGeometry = new THREE.RingGeometry(0.55, 0.58, 48);
    const innerMaterial = material.clone();
    innerMaterial.opacity = 0.35;
    this.innerCursor = new THREE.Mesh(innerGeometry, innerMaterial);
    this.innerCursor.visible = false;
    this.innerCursor.renderOrder = 19;
    this.scene.add(this.innerCursor);
  }

  //------------------------------------------------------------------- INPUT

  #installInput() {
    const canvas = this.canvas;
    this.pointer = { x: 0, y: 0, ndcX: 0, ndcY: 0, inside: false };
    this.drag = null;
    this.stroke = null;

    canvas.addEventListener('contextmenu', (event) => event.preventDefault());

    canvas.addEventListener('pointerdown', (event) => {
      canvas.setPointerCapture?.(event.pointerId);
      this.pointer.inside = true;
      this.#updatePointer(event);

      const sculpting = event.button === 0 && this.tool !== 'orbit' && !event.altKey;
      this.drag = {
        mode: sculpting ? 'sculpt' : event.button === 2 ? 'fly' : event.button === 1 || event.altKey ? 'pan' : 'orbit',
        x: event.clientX,
        y: event.clientY,
        pointerId: event.pointerId,
      };

      if (sculpting) this.#beginStroke();
    });

    canvas.addEventListener('pointermove', (event) => {
      this.#updatePointer(event);
      if (!this.drag) return;
      const dx = event.clientX - this.drag.x;
      const dy = event.clientY - this.drag.y;
      this.drag.x = event.clientX;
      this.drag.y = event.clientY;

      if (this.drag.mode === 'orbit' || this.drag.mode === 'fly') {
        this.orbitYaw -= dx * 0.0045;
        this.orbitPitch = Math.min(1.52, Math.max(0.06, this.orbitPitch + dy * 0.0035));
      } else if (this.drag.mode === 'pan') {
        this.#pan(dx, dy);
      } else if (this.drag.mode === 'sculpt') {
        this.#continueStroke();
      }
    });

    const end = (event) => {
      if (this.drag?.mode === 'sculpt') this.#endStroke();
      this.drag = null;
      canvas.releasePointerCapture?.(event.pointerId);
    };
    canvas.addEventListener('pointerup', end);
    canvas.addEventListener('pointercancel', end);
    canvas.addEventListener('pointerleave', () => {
      this.pointer.inside = false;
    });

    canvas.addEventListener(
      'wheel',
      (event) => {
        event.preventDefault();
        const factor = Math.exp(event.deltaY * 0.0012);
        this.viewDistance = Math.min(this.field.radius * 3.2, Math.max(6, this.viewDistance * factor));
      },
      { passive: false },
    );

    window.addEventListener('keydown', (event) => {
      this.keys.add(event.code.toLowerCase());
      if (event.code === 'Space') event.preventDefault();
    });
    window.addEventListener('keyup', (event) => this.keys.delete(event.code.toLowerCase()));
    window.addEventListener('blur', () => this.keys.clear());
  }

  #updatePointer(event) {
    const rect = this.canvas.getBoundingClientRect();
    this.pointer.x = event.clientX - rect.left;
    this.pointer.y = event.clientY - rect.top;
    this.pointer.ndcX = (this.pointer.x / Math.max(1, rect.width)) * 2 - 1;
    this.pointer.ndcY = -((this.pointer.y / Math.max(1, rect.height)) * 2 - 1);
  }

  #pan(dx, dy) {
    const [east, north] = this.#focusBasis();
    const scale = this.viewDistance * 0.0016;
    const moveEast = -dx * scale;
    const moveNorth = dy * scale;
    const moved = normalize({
      x: this.focusDir.x + (east.x * moveEast + north.x * moveNorth) / this.field.radius,
      y: this.focusDir.y + (east.y * moveEast + north.y * moveNorth) / this.field.radius,
      z: this.focusDir.z + (east.z * moveEast + north.z * moveNorth) / this.field.radius,
    });
    this.focusDir = moved;
  }

  #focusBasis() {
    const up = this.focusDir;
    const reference = Math.abs(up.y) < 0.95 ? { x: 0, y: 1, z: 0 } : { x: 1, y: 0, z: 0 };
    const east = normalize(cross(reference, up));
    const north = cross(up, east);
    return [east, north];
  }

  //----------------------------------------------------------------- PICKING

  /**
   * Sphere-trace the field along the current pointer ray. Returns null when the
   * ray leaves the planet's bounding sphere without a hit.
   */
  pickSurface() {
    const ray = this.#pointerRay();
    if (!ray) return null;

    const radius = this.field.radius;
    const maxDistance = radius * 4;
    let t = 0;
    let previous = 0;

    for (let step = 0; step < 220; step++) {
      const point = {
        x: ray.origin.x + ray.direction.x * t,
        y: ray.origin.y + ray.direction.y * t,
        z: ray.origin.z + ray.direction.z * t,
      };
      const distance = this.field.distanceAt(point);
      if (distance < Math.max(0.15, t * 0.0006)) {
        // Bisect between the last safe point and this one for a stable cursor.
        let low = previous;
        let high = t;
        for (let i = 0; i < 12; i++) {
          const mid = (low + high) * 0.5;
          const probe = {
            x: ray.origin.x + ray.direction.x * mid,
            y: ray.origin.y + ray.direction.y * mid,
            z: ray.origin.z + ray.direction.z * mid,
          };
          if (this.field.distanceAt(probe) < 0) high = mid;
          else low = mid;
        }
        const hitT = (low + high) * 0.5;
        const position = {
          x: ray.origin.x + ray.direction.x * hitT,
          y: ray.origin.y + ray.direction.y * hitT,
          z: ray.origin.z + ray.direction.z * hitT,
        };
        const dir = normalize(position);
        return { position, dir, distance: hitT, elevation: this.field.elevationAt(dir) };
      }
      previous = t;
      t += Math.max(distance * 0.85, 1.0);
      if (t > maxDistance) break;
    }
    return null;
  }

  #pointerRay() {
    const origin = this.camera.position.clone();
    const direction = new THREE.Vector3(this.pointer.ndcX, this.pointer.ndcY, 0.5)
      .unproject(this.camera)
      .sub(origin)
      .normalize();
    return { origin, direction };
  }

  //----------------------------------------------------------------- EDITING

  #beginStroke() {
    const hit = this.pickSurface();
    if (!hit) return 0;
    this.stroke = {
      tool: this.tool,
      lastDir: hit.dir,
      count: 0,
    };
    return this.#stamp(hit);
  }

  #continueStroke() {
    if (!this.stroke) return 0;
    const hit = this.pickSurface();
    if (!hit) return 0;
    const moved = Math.acos(
      Math.min(1, Math.max(-1, hit.dir.x * this.stroke.lastDir.x + hit.dir.y * this.stroke.lastDir.y + hit.dir.z * this.stroke.lastDir.z)),
    ) * this.field.radius;
    if (moved < this.brush.radius * 0.3) return 0;
    this.stroke.lastDir = hit.dir;
    return this.#stamp(hit);
  }

  #stamp(hit) {
    if (!this.onSculpt) return 0;
    const op = this.onSculpt({
      toolId: this.tool,
      dir: hit.dir,
      radius: this.brush.radius,
      strength: this.brush.strength,
      falloff: this.brush.falloff,
    });
    if (op) {
      this.stroke.count++;
      this.stats.lastOp = op.kind;
    }
    return op ? 1 : 0;
  }

  #endStroke() {
    this.stroke = null;
  }

  /** Called by the editor after it appended the op to the field. */
  noteOp(op) {
    this.terrain.invalidate();
    this.water.invalidate();
    this.workers.appendOp(op, this.terrain.revision);
    this.terrain.markDirtyNear(op.dir, op.radius);
    this.water.markDirtyNear(op.dir, op.radius);
    this.lastEditAt = performance.now();
  }

  /** Planet parameters changed: rebuild everything and re-seat the camera. */
  rebuildField({ params, clearTiles = true } = {}) {
    if (params) this.field.setParams(params);
    this.terrain.invalidate();
    this.water.invalidate();
    this.workers.setField(
      this.field.params,
      this.field.serializeEdits(),
      this.terrain.revision,
      this.workerOptions,
    );
    if (clearTiles) {
      this.terrain.clear();
      this.water.clear();
      for (const [, record] of this.terrainMeshes) this.#disposeRecord(record);
      for (const [, record] of this.waterMeshes) this.#disposeRecord(record);
      this.terrainMeshes.clear();
      this.waterMeshes.clear();
    }
    this.#applySeaLevel();
  }

  /**
   * Undo, journal load and other non-append edits: resend the whole journal and
   * rebuild resident patches in place (no visible gap, unlike a full clear).
   */
  syncJournal() {
    this.terrain.invalidate();
    this.water.invalidate();
    this.workers.setField(
      this.field.params,
      this.field.serializeEdits(),
      this.terrain.revision,
      this.workerOptions,
    );
    this.terrain.markAllDirty();
    this.water.markAllDirty();
    this.#applySeaLevel();
  }

  resyncWorkers() {
    this.workers.setField(
      this.field.params,
      this.field.serializeEdits(),
      this.terrain.revision,
      this.workerOptions,
    );
  }

  //------------------------------------------------------------------ UPLOAD

  #upload(entry) {
    const { tile, mesh, water } = entry;
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(mesh.positions, 3));
    geometry.setAttribute('normal', new THREE.BufferAttribute(mesh.normals, 3));
    geometry.setIndex(new THREE.BufferAttribute(mesh.indices, 1));

    if (water) {
      geometry.setAttribute('aDepth', new THREE.BufferAttribute(mesh.depths, 1));
    } else {
      geometry.setAttribute('aElevation', new THREE.BufferAttribute(mesh.elevation, 1));
      geometry.setAttribute('aSlope', new THREE.BufferAttribute(mesh.slope, 1));
    }
    geometry.computeBoundingSphere();

    const object = new THREE.Mesh(geometry, water ? this.waterMaterial : this.terrainMaterial);
    object.matrixAutoUpdate = false;
    object.updateMatrix();
    object.userData.tileKey = tile.key;

    const target = water ? this.waterMeshes : this.terrainMeshes;
    const group = water ? this.waterGroup : this.terrainGroup;
    const previous = target.get(tile.key);
    if (previous) this.#disposeRecord(previous);

    target.set(tile.key, { object, geometry });
    group.add(object);
    this.stats.uploads++;
  }

  #dropMesh(tile, water) {
    const target = water ? this.waterMeshes : this.terrainMeshes;
    const record = target.get(tile.key);
    if (!record) return;
    this.#disposeRecord(record);
    target.delete(tile.key);
  }

  #disposeRecord(record) {
    record.object.removeFromParent();
    record.geometry.dispose();
  }

  #applySeaLevel() {
    this.terrainMaterial.uniforms.uSeaLevel.value = this.oceanParams.seaLevelOffset;
    this.waterMaterial.uniforms.uSeaRadius.value = this.field.radius + this.oceanParams.seaLevelOffset;
  }

  //------------------------------------------------------------------- API

  setTool(toolId) {
    this.tool = toolId;
    this.cursor.visible = false;
    this.innerCursor.visible = false;
  }

  setBrush({ radius, strength, falloff }) {
    if (Number.isFinite(radius)) this.brush.radius = radius;
    if (Number.isFinite(strength)) this.brush.strength = strength;
    if (Number.isFinite(falloff)) this.brush.falloff = falloff;
  }

  setOceanParams(patch) {
    this.oceanParams = normalizeOceanParams({ ...this.oceanParams, ...patch });
    this.updateWaveUniforms();
    this.#applySeaLevel();
    for (const [key, value] of Object.entries({
      uFoamWidth: this.oceanParams.foamWidth,
      uFoamIntensity: this.oceanParams.foamIntensity,
      uCrestFoam: this.oceanParams.shoreCrestFoam,
      uAbsorption: this.oceanParams.absorption,
      uScattering: this.oceanParams.scattering,
      uRipple: this.oceanParams.rippleScale,
    })) {
      const uniform = this.waterMaterial.uniforms[key];
      if (uniform) uniform.value = value;
      else this.onDiagnostic?.(`ocean parameter ${key} has no uniform on the water material`);
    }
    this.sunDirection = sunDirection(this.oceanParams);
    this.skyUniforms.uSunDirection.value.set(this.sunDirection.x, this.sunDirection.y, this.sunDirection.z);
    this.water.invalidate();
    this.water.markDirtyNear(this.focusDir, this.field.radius);
  }

  setOceanVisible(visible) {
    this.showOcean = visible;
    this.waterGroup.visible = visible;
  }

  setWireframe(enabled) {
    this.showWireframe = enabled;
    this.terrainMaterial.wireframe = enabled;
  }

  teleportTo(dir, altitude = 3000) {
    const unit = normalize(dir);
    this.focusDir = unit;
    this.viewDistance = Math.max(8, altitude);
    this.orbitPitch = 0.34;
    this.focusRadius = this.field.radius + this.field.elevationAt(unit);
    this.terrain?.clear();
    this.water?.clear();
    if (this.uploadQueue) this.uploadQueue.length = 0;
    for (const [, record] of this.terrainMeshes ?? []) this.#disposeRecord(record);
    for (const [, record] of this.waterMeshes ?? []) this.#disposeRecord(record);
    this.terrainMeshes?.clear();
    this.waterMeshes?.clear();
  }

  getFocusLatLon() {
    return latLonFromDirection(this.focusDir);
  }

  //------------------------------------------------------------------ FRAME

  animate() {
    if (!this.running) return;
    requestAnimationFrame(this.animate);

    // A throw in the middle of a frame used to stop the loop for good: the canvas
    // stayed black and nothing said why. The loop now survives and reports instead.
    try {
      this.#frame();
    } catch (error) {
      this.frameFailures = (this.frameFailures ?? 0) + 1;
      const message = `frame error ${this.frameFailures}: ${error?.message ?? error}`;
      if (this.frameFailures <= 3) this.onDiagnostic?.(message);
      else if (this.frameFailures === 4) this.onDiagnostic?.('further frame errors suppressed');
      if (this.frameFailures > 240) this.running = false;
    }
  }

  #frame() {
    const now = performance.now();
    const dt = Math.min(0.1, (now - this.lastFrame) / 1000);
    this.lastFrame = now;
    this.time += dt;

    this.#updateCamera(dt);
    this.#updateLayers();
    this.#updateCursor();

    const uniformsToTime = [this.terrainMaterial.uniforms, this.waterMaterial.uniforms];
    uniformsToTime[0].uTime.value = this.time;
    uniformsToTime[1].uTime.value = this.time;

    // Air density at the camera: scales sky reflection and aerial haze, so one set
    // of shaders is correct on the beach and from orbit.
    const cameraAltitude =
      this.camera.position.length() - (this.field.radius + this.oceanParams.seaLevelOffset);
    const density = airDensityAt(cameraAltitude);
    this.skyUniforms.uAirDensity.value = density;
    this.skyUniforms.uLocalUp.value.copy(this.camera.position).normalize();
    this.terrainMaterial.uniforms.uCameraPosition.value.copy(this.camera.position);
    this.waterMaterial.uniforms.uCameraPosition.value.copy(this.camera.position);
    this.sky.position.copy(this.camera.position);

    this.renderer.render(this.scene, this.camera);

    this.frames++;
    this.accumulated += dt;
    if (this.accumulated > 0.35) {
      this.stats.fps = Math.round(this.frames / this.accumulated);
      this.stats.frameMs = (this.accumulated / this.frames) * 1000;
      this.stats.triangles = this.renderer.info.render.triangles;
      this.stats.drawCalls = this.renderer.info.render.calls;
      this.frames = 0;
      this.accumulated = 0;
      this.#reportStats();
    }
  }

  #updateCamera(dt) {
    // Keyboard flight: move the focus along the surface in the camera's heading.
    const flySpeed = Math.max(30, this.viewDistance * 1.4) * dt;
    let moved = false;
    const forward = new THREE.Vector3();
    this.camera.getWorldDirection(forward);
    const up = new THREE.Vector3(this.focusDir.x, this.focusDir.y, this.focusDir.z);
    const tangentForward = forward.clone().sub(up.clone().multiplyScalar(forward.dot(up))).normalize();
    const tangentRight = new THREE.Vector3().crossVectors(tangentForward, up).normalize();

    const step = new THREE.Vector3();
    if (this.keys.has('keyw')) step.add(tangentForward);
    if (this.keys.has('keys')) step.sub(tangentForward);
    if (this.keys.has('keya')) step.sub(tangentRight);
    if (this.keys.has('keyd')) step.add(tangentRight);
    if (this.keys.has('keyq')) this.viewDistance = Math.max(6, this.viewDistance * (1 - dt * 1.5));
    if (this.keys.has('keye')) this.viewDistance = Math.min(this.field.radius * 3.2, this.viewDistance * (1 + dt * 1.5));

    if (step.lengthSq() > 0) {
      step.normalize().multiplyScalar(flySpeed / this.field.radius);
      this.focusDir = normalize({
        x: this.focusDir.x + step.x,
        y: this.focusDir.y + step.y,
        z: this.focusDir.z + step.z,
      });
      moved = true;
    }
    this.focusRadius = this.field.radius + this.field.elevationAt(this.focusDir);

    const [east, north] = this.#focusBasis();
    const upV = new THREE.Vector3(this.focusDir.x, this.focusDir.y, this.focusDir.z);
    const eastV = new THREE.Vector3(east.x, east.y, east.z);
    const northV = new THREE.Vector3(north.x, north.y, north.z);

    const cosPitch = Math.cos(this.orbitPitch);
    const offset = upV
      .clone()
      .multiplyScalar(Math.sin(this.orbitPitch))
      .add(eastV.clone().multiplyScalar(Math.cos(this.orbitYaw) * cosPitch))
      .add(northV.clone().multiplyScalar(Math.sin(this.orbitYaw) * cosPitch))
      .normalize()
      .multiplyScalar(this.viewDistance);

    const focusPoint = upV.clone().multiplyScalar(this.focusRadius);
    this.camera.position.copy(focusPoint).add(offset);
    // Up is the local radial, never world +Y: on a planet a fixed world axis rolls
    // the frame by the camera's latitude and tilts the horizon.
    this.camera.up.copy(upV);
    this.camera.lookAt(focusPoint);

    // Near/far are derived from the orbit distance so depth precision stays usable
    // without a logarithmic depth buffer: the near plane tracks how close the camera
    // actually gets, and the far plane just covers the local horizon (plus the planet
    // itself when the whole thing is in view).
    const horizonDistance = Math.sqrt(2 * this.field.radius * Math.max(1, this.viewDistance));
    this.camera.near = Math.max(0.35, this.viewDistance * 0.0012);
    this.camera.far = Math.min(
      this.field.radius * 6,
      Math.max(horizonDistance * 2.5, this.viewDistance * 60, 20000),
    );
    this.camera.updateProjectionMatrix();

    // The sky dome is drawn first with depth test off, but its vertices would still
    // be clipped if it poked past the far plane: keep it just inside.
    if (this.sky) this.sky.scale.setScalar((this.camera.far * 0.8) / this.skyRadius);

    this.terrainMaterial.uniforms.uHazeDistance.value = Math.max(8000, this.viewDistance * 26);
  }

  #updateLayers() {
    // Selection is cheap; run it a few times a second rather than every frame.
    const now = performance.now();
    if (!this.lastSelectionAt || now - this.lastSelectionAt > 90) {
      this.lastSelectionAt = now;
      const position = this.camera.position;
      const forward = new THREE.Vector3();
      this.camera.getWorldDirection(forward);
      this.terrain.update(position, forward);
      if (this.showOcean) this.water.update(position, forward, { maxTiles: 200 });
      this.terrain.pump();
      if (this.showOcean) this.water.pump();
    }

    // Upload at most a few patches per frame so streaming cannot spike frame time.
    let budget = 3;
    while (this.uploadQueue.length > 0 && budget > 0) {
      const entry = this.uploadQueue.shift();
      this.#upload(entry);
      budget--;
    }
  }

  #updateCursor() {
    const sculpting = this.tool !== 'orbit';
    if (!sculpting || !this.pointer.inside) {
      this.cursor.visible = false;
      this.innerCursor.visible = false;
      return;
    }

    const hit = this.pickSurface();
    if (!hit) {
      this.cursor.visible = false;
      this.innerCursor.visible = false;
      return;
    }

    const normal = this.field.normalAt(hit.dir);
    const quaternion = new THREE.Quaternion().setFromUnitVectors(
      new THREE.Vector3(0, 0, 1),
      new THREE.Vector3(normal.x, normal.y, normal.z),
    );
    const position = new THREE.Vector3(hit.position.x, hit.position.y, hit.position.z);
    const depthOffset = Math.max(0.5, this.viewDistance * 0.002);
    const lifted = position.clone().add(new THREE.Vector3(normal.x, normal.y, normal.z).multiplyScalar(depthOffset));

    this.cursor.position.copy(lifted);
    this.cursor.quaternion.copy(quaternion);
    this.cursor.scale.setScalar(this.brush.radius);

    this.innerCursor.position.copy(lifted);
    this.innerCursor.quaternion.copy(quaternion);
    this.innerCursor.scale.setScalar(this.brush.radius * (1 - this.brush.falloff * 0.55));

    this.cursor.visible = true;
    this.innerCursor.visible = true;

    this.onCursor({
      latitude: latLonFromDirection(hit.dir).latitude,
      longitude: latLonFromDirection(hit.dir).longitude,
      elevation: hit.elevation,
      distance: this.viewDistance,
    });
  }

  #reportStats() {
    const terrain = this.terrain.summary;
    const water = this.showOcean ? this.water.summary : null;
    this.onStats({
      fps: this.stats.fps,
      frameMs: +this.stats.frameMs.toFixed(2),
      triangles: this.stats.triangles,
      drawCalls: this.stats.drawCalls,
      terrain,
      water,
      seaLevel: this.oceanParams.seaLevelOffset,
      radius: this.field.radius,
      focus: this.getFocusLatLon(),
      altitude: +this.viewDistance.toFixed(0),
      journal: this.field.ops.length,
    });
  }

  dispose() {
    this.running = false;
    this.resizeObserver?.disconnect();
    this.workers.dispose();
    for (const [, record] of this.terrainMeshes) this.#disposeRecord(record);
    for (const [, record] of this.waterMeshes) this.#disposeRecord(record);
    this.renderer.dispose();
  }
}

function cross(a, b) {
  return {
    x: a.y * b.z - a.z * b.y,
    y: a.z * b.x - a.x * b.z,
    z: a.x * b.y - a.y * b.x,
  };
}
