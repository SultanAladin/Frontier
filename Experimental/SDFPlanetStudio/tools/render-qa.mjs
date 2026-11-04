#!/usr/bin/env node
/**
 * Offline QA renders: build real tiles through the real manager and rasterise them
 * on the CPU, so the images are evidence about the geometry, not an illustration.
 *
 * The shading here mirrors the viewport's terrain and water shaders closely enough
 * to judge the result (same palette bands, same Beer-Lambert water, same foam
 * model), but it is an independent implementation: if the meshes were broken, the
 * pictures would show it.
 *
 * Output: Evidence/*.png plus Evidence/render-qa.json with timings and counts.
 *
 * Usage: node tools/render-qa.mjs [--width 1600] [--height 900] [--only coast]
 */

import { deflateSync } from 'node:zlib';
import { writeFile, mkdir } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { PlanetField } from '../src/core/planet-field.js';
import { TileManager } from '../src/core/tile-manager.js';
import { buildTileMesh } from '../src/core/narrow-band.js';
import { buildWaterTile } from '../src/core/water-tile.js';
import { createBrushOp } from '../src/core/brushes.js';
import {
  airDensityAt,
  DEFAULT_OCEAN_PARAMS,
  buildWaveSet,
  foamAt,
  gerstnerAt,
  normalizeOceanParams,
  skyRadiance,
  sunDirection,
  waterColorAtDepth,
} from '../src/core/ocean-model.js';
import { directionFromLatLon, latLonFromDirection } from '../src/core/cube-sphere.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const EVIDENCE = join(HERE, '..', '..', '..', 'Exhibits', 'SDFPlanetStudio');

const args = process.argv.slice(2);
const readArg = (name, fallback) => {
  const index = args.indexOf(name);
  return index >= 0 ? Number(args[index + 1]) : fallback;
};
const WIDTH = readArg('--width', 1600);
const HEIGHT = readArg('--height', 900);
const ONLY = args.includes('--only') ? args[args.indexOf('--only') + 1] : null;
const NO_WATER = args.includes('--no-water');
const DEBUG = args.includes('--debug');
const TILE_TINT = args.includes('--tile-tint');
const SKY_MAGENTA = args.includes('--sky-magenta');
const WATER_ONLY = args.includes('--water-only');
const WATER_TINT = args.includes('--water-tint');

//------------------------------------------------------------------------------------------------
//                                                    PNG
//------------------------------------------------------------------------------------------------

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[i] = c >>> 0;
  }
  return table;
})();

function crc32(buffer) {
  let crc = 0xffffffff;
  for (let i = 0; i < buffer.length; i++) crc = CRC_TABLE[(crc ^ buffer[i]) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
  const length = Buffer.alloc(4);
  length.writeUInt32BE(data.length, 0);
  const typeBuffer = Buffer.from(type, 'ascii');
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(Buffer.concat([typeBuffer, data])), 0);
  return Buffer.concat([length, typeBuffer, data, crc]);
}

/** 8-bit RGB PNG from a Float32 linear-light framebuffer. */
function encodePng(width, height, rgb) {
  const raw = Buffer.alloc((width * 3 + 1) * height);
  let offset = 0;
  for (let y = 0; y < height; y++) {
    raw[offset++] = 0; // filter: none
    for (let x = 0; x < width; x++) {
      const i = (y * width + x) * 3;
      raw[offset++] = toSrgb(rgb[i]);
      raw[offset++] = toSrgb(rgb[i + 1]);
      raw[offset++] = toSrgb(rgb[i + 2]);
    }
  }

  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8;
  header[9] = 2; // truecolour
  header[10] = 0;
  header[11] = 0;
  header[12] = 0;

  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', header),
    chunk('IDAT', deflateSync(raw, { level: 6 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

/** ACES approximation + sRGB transfer, matching the viewport's tone mapping. */
function aces(x) {
  const a = 2.51;
  const b = 0.03;
  const c = 2.43;
  const d = 0.59;
  const e = 0.14;
  return Math.max(0, Math.min(1, (x * (a * x + b)) / (x * (c * x + d) + e)));
}

function toSrgb(value) {
  const mapped = aces(value * 1.05);
  const srgb = mapped <= 0.0031308 ? mapped * 12.92 : 1.055 * Math.pow(mapped, 1 / 2.4) - 0.055;
  return Math.round(Math.max(0, Math.min(1, srgb)) * 255);
}

//------------------------------------------------------------------------------------------------
//                                                    SCENE
//------------------------------------------------------------------------------------------------

function makeCamera({ eye, target, fovDeg = 58, width = WIDTH, height = HEIGHT, upHint = null }) {
  const sub = (a, b) => ({ x: a.x - b.x, y: a.y - b.y, z: a.z - b.z });
  const dot = (a, b) => a.x * b.x + a.y * b.y + a.z * b.z;
  const cross = (a, b) => ({
    x: a.y * b.z - a.z * b.y,
    y: a.z * b.x - a.x * b.z,
    z: a.x * b.y - a.y * b.x,
  });
  const norm = (a) => {
    const len = Math.hypot(a.x, a.y, a.z) || 1;
    return { x: a.x / len, y: a.y / len, z: a.z / len };
  };

  const forward = norm(sub(target, eye));
  // "Up" on a planet is the local radial, not world +Y: using the world axis rolls
  // the frame by the camera's latitude and longitude, which tilts the horizon.
  const worldUp = upHint ? norm(upHint) : { x: 0, y: 1, z: 0 };
  const roll = Math.abs(dot(forward, worldUp));
  const safeUp = roll > 0.999 ? { x: 1, y: 0, z: 0 } : worldUp;
  const right = norm(cross(forward, safeUp));
  const up = cross(right, forward);
  const tanHalf = Math.tan((fovDeg * Math.PI) / 360);

  return {
    eye,
    forward,
    right,
    up,
    tanHalf,
    aspect: width / height,
    pixelAngle: (2 * tanHalf) / height,
    project(point, out) {
      const v = sub(point, eye);
      const z = dot(v, forward);
      if (z <= 1e-3) return false;
      const x = dot(v, right);
      const y = dot(v, up);
      out.x = ((x / (z * tanHalf * this.aspect)) * 0.5 + 0.5) * width;
      out.y = (1 - ((y / (z * tanHalf)) * 0.5 + 0.5)) * height;
      out.z = z;
      out.w = 1 / z;
      return true;
    },
  };
}

function createFramebuffer(width, height, skyColor) {
  const colour = new Float32Array(width * height * 3);
  const depth = new Float32Array(width * height).fill(Infinity);
  for (let i = 0; i < width * height; i++) {
    colour[i * 3] = skyColor[0];
    colour[i * 3 + 1] = skyColor[1];
    colour[i * 3 + 2] = skyColor[2];
  }
  return { colour, depth, width, height };
}

const p0 = { x: 0, y: 0, z: 0, w: 0 };
const p1 = { x: 0, y: 0, z: 0, w: 0 };
const p2 = { x: 0, y: 0, z: 0, w: 0 };

function rasterize(fb, camera, mesh, shade, { blend = false } = {}) {
  const { positions, indices } = mesh;
  const W = fb.width;
  const H = fb.height;
  let drawn = 0;

  for (let t = 0; t < indices.length; t += 3) {
    const ia = indices[t];
    const ib = indices[t + 1];
    const ic = indices[t + 2];

    const a = { x: positions[ia * 3], y: positions[ia * 3 + 1], z: positions[ia * 3 + 2] };
    const b = { x: positions[ib * 3], y: positions[ib * 3 + 1], z: positions[ib * 3 + 2] };
    const c = { x: positions[ic * 3], y: positions[ic * 3 + 1], z: positions[ic * 3 + 2] };

    const ma = camera.eye;
    const facing =
      (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y) === 0 &&
      (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z) === 0;
    void facing;
    void ma;

    if (!camera.project(a, p0) || !camera.project(b, p1) || !camera.project(c, p2)) continue;

    const minX = Math.max(0, Math.floor(Math.min(p0.x, p1.x, p2.x)));
    const maxX = Math.min(W - 1, Math.ceil(Math.max(p0.x, p1.x, p2.x)));
    const minY = Math.max(0, Math.floor(Math.min(p0.y, p1.y, p2.y)));
    const maxY = Math.min(H - 1, Math.ceil(Math.max(p0.y, p1.y, p2.y)));
    if (minX > maxX || minY > maxY) continue;

    const area = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
    if (Math.abs(area) < 1e-9) continue;
    const invArea = 1 / area;

    for (let y = minY; y <= maxY; y++) {
      const py = y + 0.5;
      for (let x = minX; x <= maxX; x++) {
        const px = x + 0.5;
        const w0 = ((p1.x - px) * (p2.y - py) - (p1.y - py) * (p2.x - px)) * invArea;
        const w1 = ((p2.x - px) * (p0.y - py) - (p2.y - py) * (p0.x - px)) * invArea;
        const w2 = 1 - w0 - w1;
        const EPS = 1e-6;
        if (w0 < -EPS || w1 < -EPS || w2 < -EPS) continue;

        // Perspective-correct depth and attributes.
        const invZ = w0 * p0.w + w1 * p1.w + w2 * p2.w;
        if (invZ <= 0) continue;
        const z = 1 / invZ;
        const index = y * W + x;

        if (!blend) {
          if (z >= fb.depth[index]) continue;
        } else if (z > fb.depth[index] * 1.002) {
          // Water gets the same depth bias the viewport's polygonOffset applies.
          continue;
        }

        const ba = w0 * p0.w;
        const bb = w1 * p1.w;
        const bc = w2 * p2.w;
        const sum = ba + bb + bc || 1;

        const shadeResult = shade({
          alpha: ba / sum,
          beta: bb / sum,
          gamma: bc / sum,
          mesh,
          ia,
          ib,
          ic,
          world: {
            x: a.x * (ba / sum) + b.x * (bb / sum) + c.x * (bc / sum),
            y: a.y * (ba / sum) + b.y * (bb / sum) + c.y * (bc / sum),
            z: a.z * (ba / sum) + b.z * (bb / sum) + c.z * (bc / sum),
          },
          z,
        });
        if (!shadeResult) continue;

        if (!blend) {
          fb.depth[index] = z;
          fb.colour[index * 3] = shadeResult[0];
          fb.colour[index * 3 + 1] = shadeResult[1];
          fb.colour[index * 3 + 2] = shadeResult[2];
        } else {
          const alpha = shadeResult[3];
          if (alpha <= 0.001) continue;
          fb.colour[index * 3] = fb.colour[index * 3] * (1 - alpha) + shadeResult[0] * alpha;
          fb.colour[index * 3 + 1] = fb.colour[index * 3 + 1] * (1 - alpha) + shadeResult[1] * alpha;
          fb.colour[index * 3 + 2] = fb.colour[index * 3 + 2] * (1 - alpha) + shadeResult[2] * alpha;
        }
        drawn++;
      }
    }
  }
  return drawn;
}

//------------------------------------------------------------------------------------------------
//                                                  SHADERS
//------------------------------------------------------------------------------------------------

function makeTerrainShader(field, ocean, camera, seaLevel) {
  const sun = sunDirection(ocean);
  const density = airDensityAt(Math.hypot(camera.eye.x, camera.eye.y, camera.eye.z) - field.radius - seaLevel);
  const skyTop = [0.12, 0.26, 0.56];
  const skyHorizon = [0.46, 0.55, 0.66];
  const hazeDistance = 90000;

  return ({ mesh, ia, ib, ic, alpha, beta, gamma, world }) => {
    const elevation =
      mesh.elevation[ia] * alpha + mesh.elevation[ib] * beta + mesh.elevation[ic] * gamma;
    const slope = mesh.slope[ia] * alpha + mesh.slope[ib] * beta + mesh.slope[ic] * gamma;

    let nx = mesh.normals[ia * 3] * alpha + mesh.normals[ib * 3] * beta + mesh.normals[ic * 3] * gamma;
    let ny = mesh.normals[ia * 3 + 1] * alpha + mesh.normals[ib * 3 + 1] * beta + mesh.normals[ic * 3 + 1] * gamma;
    let nz = mesh.normals[ia * 3 + 2] * alpha + mesh.normals[ib * 3 + 2] * beta + mesh.normals[ic * 3 + 2] * gamma;
    const len = Math.hypot(nx, ny, nz) || 1;
    nx /= len;
    ny /= len;
    nz /= len;

    const smooth = (edge0, edge1, value) => {
      const t = Math.min(1, Math.max(0, (value - edge0) / (edge1 - edge0)));
      return t * t * (3 - 2 * t);
    };
    const mix = (a, b, t) => a + (b - a) * t;
    const mix3 = (a, b, t) => [mix(a[0], b[0], t), mix(a[1], b[1], t), mix(a[2], b[2], t)];

    const drySand = [0.3, 0.255, 0.18];
    const grass = [0.062, 0.12, 0.045];
    const alpine = [0.115, 0.125, 0.08];
    const rock = [0.115, 0.108, 0.1];
    const scree = [0.175, 0.165, 0.155];
    const snow = [0.78, 0.81, 0.86];
    const wetSand = [0.085, 0.075, 0.06];

    // Same idea as the viewport's groundDetail(): without it, ground is flat paint.
    const dCoarse = valueNoise(world.x * 0.024, world.z * 0.024);
    const dMedium = valueNoise(world.x * 0.11, world.z * 0.11);
    const dFine = valueNoise(world.x * 0.52, world.z * 0.52);
    const detail = dCoarse * 0.5 + dMedium * 0.32 + dFine * 0.18;
    const rockiness = smooth(0.55, 1.35, slope);
    const snowiness = smooth(2600, 3300, elevation) * (1 - rockiness * 0.75);

    let albedo = mix3(drySand, grass, smooth(4, 55, elevation));
    albedo = mix3(albedo, alpine, smooth(1100, 2100, elevation));
    albedo = mix3(albedo, rock, rockiness);
    albedo = mix3(albedo, scree, smooth(0.9, 1.8, slope));
    albedo = mix3(albedo, snow, snowiness);

    albedo = [
      albedo[0] * (0.8 + 0.42 * detail),
      albedo[1] * (0.8 + 0.42 * detail),
      albedo[2] * (0.8 + 0.42 * detail),
    ];

    const shoreBand = 1 - smooth(0, 10, Math.abs(elevation - seaLevel));
    albedo = mix3(albedo, wetSand, shoreBand * 0.75);
    const submerged = smooth(seaLevel, seaLevel - 12, elevation);
    albedo = mix3(albedo, [0.055, 0.075, 0.07], submerged);

    const ndl = Math.max(0, nx * sun.x + ny * sun.y + nz * sun.z);
    const wrap = Math.max(0, (nx * sun.x + ny * sun.y + nz * sun.z) * 0.5 + 0.5);
    const upness = ny * 0.5 + 0.5;
    const ambient = [
      mix(0.075, skyTop[0] * 1.6 * density, upness),
      mix(0.085, skyTop[1] * 1.6 * density, upness),
      mix(0.11, skyTop[2] * 1.6 * density, upness),
    ];

    const light = ndl * 1.45 + wrap * 0.2;
    let r = albedo[0] * (sun.x * 0 + light + ambient[0]);
    let g = albedo[1] * (light + ambient[1]);
    let b = albedo[2] * (light + ambient[2]);
    r = albedo[0] * light + ambient[0] * albedo[0];

    const distance = Math.hypot(camera.eye.x - world.x, camera.eye.y - world.y, camera.eye.z - world.z);
    const haze = Math.min(0.85, (1 - Math.exp(-distance / hazeDistance)) * density);
    r = mix(r, skyHorizon[0] * 0.85, haze);
    g = mix(g, skyHorizon[1] * 0.85, haze);
    b = mix(b, skyHorizon[2] * 0.85, haze);

    return [r, g, b];
  };
}

/** One flat colour per tile, so a seam shows up as a hole or a colour break. */
const TINTS = [
  [0.9, 0.25, 0.2],
  [0.95, 0.68, 0.16],
  [0.35, 0.8, 0.3],
  [0.2, 0.55, 0.95],
  [0.7, 0.3, 0.9],
  [0.15, 0.85, 0.82],
  [0.95, 0.4, 0.75],
  [0.55, 0.55, 0.62],
];
function tintShaderFor(index) {
  const colour = TINTS[index % TINTS.length];
  return () => colour;
}

/** Cheap 2D value noise, only used for the foam breakup in these stills. */
function hash21(x, y) {
  const v = Math.sin(x * 41.13 + y * 289.7) * 43758.5453;
  return v - Math.floor(v);
}

function valueNoise(x, y) {
  const ix = Math.floor(x);
  const iy = Math.floor(y);
  const fx = x - ix;
  const fy = y - iy;
  const ux = fx * fx * (3 - 2 * fx);
  const uy = fy * fy * (3 - 2 * fy);
  const a = hash21(ix, iy);
  const b = hash21(ix + 1, iy);
  const c = hash21(ix, iy + 1);
  const d = hash21(ix + 1, iy + 1);
  return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy;
}

function makeWaterShader(ocean, waves, camera) {
  const sun = sunDirection(ocean);
  const density = airDensityAt(Math.hypot(camera.eye.x, camera.eye.y, camera.eye.z) - 900000 - ocean.seaLevelOffset);
  const skyTop = [0.12, 0.26, 0.56];
  const skyHorizon = [0.46, 0.55, 0.66];
  const out = { x: 0, y: 0, z: 0, normalX: 0, normalY: 0, normalZ: 0 };

  return ({ mesh, ia, ib, ic, alpha, beta, gamma, world }) => {
    const depth = mesh.depths[ia] * alpha + mesh.depths[ib] * beta + mesh.depths[ic] * gamma;
    if (depth <= 0) return null;

    // Evaluate the same Gerstner spectrum the shader uses, at the same coordinates,
    // with the same pixels-per-metre wave LOD.
    const camDistance = Math.hypot(
      camera.eye.x - world.x,
      camera.eye.y - world.y,
      camera.eye.z - world.z,
    );
    const footprint = Math.max(camDistance * camera.pixelAngle, 0.01);
    gerstnerAt(waves, world.x, world.z, TIME, out, { footprintMetres: footprint });

    const view = {
      x: camera.eye.x - world.x,
      y: camera.eye.y - world.y,
      z: camera.eye.z - world.z,
    };
    const viewLen = Math.hypot(view.x, view.y, view.z) || 1;
    view.x /= viewLen;
    view.y /= viewLen;
    view.z /= viewLen;

    const nx = out.normalX;
    const ny = out.normalY;
    const nz = out.normalZ;

    const cosTheta = Math.max(0, nx * view.x + ny * view.y + nz * view.z);
    const fresnel = Math.min(0.8, 0.02 + 0.98 * Math.pow(1 - cosTheta, 4));

    // Reflection vector and sky lookup.
    const dotNV = nx * view.x + ny * view.y + nz * view.z;
    let rx = view.x - 2 * dotNV * nx;
    let ry = view.y - 2 * dotNV * ny;
    let rz = view.z - 2 * dotNV * nz;
    const rlen = Math.hypot(rx, ry, rz) || 1;
    rx /= rlen;
    ry /= rlen;
    rz /= rlen;

    // Sky elevation is measured from the local horizon, not world +Y.
    const upLocal = {
      x: world.x / (Math.hypot(world.x, world.y, world.z) || 1),
      y: world.y / (Math.hypot(world.x, world.y, world.z) || 1),
      z: world.z / (Math.hypot(world.x, world.y, world.z) || 1),
    };
    const skyElevation = Math.asin(Math.min(1, Math.max(-1, rx * upLocal.x + ry * upLocal.y + rz * upLocal.z)));
    const skyRaw = skyRadiance(ocean, skyElevation);
    // Blur the reflection and keep the water's tint, as the viewport shader does.
    const blurMix = Math.min(1, Math.max(0, (camDistance - 2000) / 12000));
    const blurred = skyRadiance(ocean, 0.28);
    const skyRef = {
      r: (skyRaw.r + (blurred.r - skyRaw.r) * blurMix) * 0.82,
      g: (skyRaw.g + (blurred.g - skyRaw.g) * blurMix) * 0.82,
      b: (skyRaw.b + (blurred.b - skyRaw.b) * blurMix) * 0.82 + 0.03,
    };
    const sky = {
      r: 0.012 + (skyRef.r - 0.012) * density,
      g: 0.018 + (skyRef.g - 0.018) * density,
      b: 0.032 + (skyRef.b - 0.032) * density,
    };
    const sunAlign = Math.max(0, rx * sun.x + ry * sun.y + rz * sun.z);
    const specular = Math.pow(sunAlign, 600) * 2.4;
    const glitter = Math.pow(sunAlign, 180) * 0.22;

    const body = waterColorAtDepth(ocean, depth);
    // Foam with the same noise breakup the viewport shader uses, so the surf line
    // is patchy rather than a painted stripe.
    const foamNoise = valueNoise(-world.x * 0.022 + TIME * 0.06, world.z * 0.022 - TIME * 0.045);
    let foam = foamAt(ocean, depth, Math.max(0, ny));
    foam *= Math.min(1, Math.max(0, (foamNoise - 0.25) / 0.5));

    const base = [
      body.r + (sky.r - body.r) * fresnel,
      body.g + (sky.g - body.g) * fresnel,
      body.b + (sky.b - body.b) * fresnel,
    ];
    base[0] += specular + glitter;
    base[1] += specular * 0.97 + glitter * 0.95;
    base[2] += specular * 0.92 + glitter * 0.9;

    const foamColour = [0.86, 0.9, 0.93];
    const outColour = [
      base[0] + (foamColour[0] - base[0]) * foam * 0.92,
      base[1] + (foamColour[1] - base[1]) * foam * 0.92,
      base[2] + (foamColour[2] - base[2]) * foam * 0.92,
    ];

    const smooth = (edge0, edge1, value) => {
      const t = Math.min(1, Math.max(0, (value - edge0) / (edge1 - edge0)));
      return t * t * (3 - 2 * t);
    };

    let alphaOut = 0.42 + (0.98 - 0.42) * smooth(0, 42, depth);
    alphaOut = Math.min(1, alphaOut + fresnel * 0.45 + foam * 0.75);

    return [outColour[0], outColour[1], outColour[2], alphaOut];
  };
}

let TIME = 0;

//------------------------------------------------------------------------------------------------
//                                                   BUILD
//------------------------------------------------------------------------------------------------

async function collectTiles(field, cameraPosition, forward, options, build) {
  const manager = new TileManager({ field, options, build: async (tile) => build(tile) });
  manager.update(cameraPosition, forward);
  // Drain properly: each round must settle before the next, or the in-flight cap
  // stops the queue after one batch.
  for (let round = 0; round < 600; round++) {
    const batch = manager.pump(16);
    if (batch.length === 0) break;
    await Promise.all(batch);
  }
  return manager;
}

async function renderScene(options) {
  const { name, fovDeg = 58, time = 6.5, sculpt = null, oceanOverrides = {}, cameraFor = null } = options;
  let eye = options.eye;
  let target = options.target;
  const started = process.hrtime.bigint();
  const field = new PlanetField({ radius: 900000, seed: 4029 });
  const ocean = normalizeOceanParams({ ...DEFAULT_OCEAN_PARAMS, ...oceanOverrides });
  const seaLevel = ocean.seaLevelOffset;

  if (sculpt) for (const request of sculpt) {
    field.addOp(createBrushOp({ ...request, field }));
  }

  // A shot that changes the terrain has to re-seat its camera on the *new*
  // terrain, or it ends up buried inside the landmass it just raised.
  if (cameraFor) {
    const seated = cameraFor(field);
    eye = seated.eye;
    target = seated.target;
  }

  const camera = makeCamera({ eye, target, fovDeg, upHint: { x: eye.x, y: eye.y, z: eye.z } });
  const forward = camera.forward;

  if (DEBUG) {
    console.log(`[${name}] eye=${fmtVec(eye)} target=${fmtVec(target)}`);

    console.log(`[${name}] forward=${fmtVec(camera.forward)} right=${fmtVec(camera.right)} up=${fmtVec(camera.up)}`);
    console.log(`[${name}] |eye|=${Math.hypot(eye.x, eye.y, eye.z).toFixed(1)} |target|=${Math.hypot(target.x, target.y, target.z).toFixed(1)} dist=${Math.hypot(eye.x - target.x, eye.y - target.y, eye.z - target.z).toFixed(1)}`);
  }

  const terrainManager = await collectTiles(
    field,
    eye,
    forward,
    { cellsPerTile: 48, maxTiles: 1400, minCellSize: 1.5 },
    (tile) => buildTileMesh(field, tile, { cellsPerTile: 48 }),
  );

  const waterManager = NO_WATER
    ? { resident: new Map(), residentCount: 0 }
    : await collectTiles(
        field,
        eye,
        forward,
        { cellsPerTile: 20, maxTiles: 900, minCellSize: 8 },
        (tile) => buildWaterTile(field, tile, { cells: 20, seaLevelOffset: seaLevel }),
      );

  const skyRaw = skyRadiance(ocean, 0.35);
  const density = airDensityAt(
    Math.hypot(eye.x, eye.y, eye.z) - field.radius - seaLevel,
  );
  const sky = {
    r: 0.006 + (skyRaw.r - 0.006) * density,
    g: 0.008 + (skyRaw.g - 0.008) * density,
    b: 0.016 + (skyRaw.b - 0.016) * density,
  };
  const fb = createFramebuffer(
    WIDTH,
    HEIGHT,
    SKY_MAGENTA ? [1, 0, 1] : [sky.r * 0.92, sky.g * 0.95, sky.b * 1.0],
  );

  TIME = time;
  let triangles = 0;
  const tintShader = makeTerrainShader(field, ocean, camera, seaLevel);
  let tintIndex = 0;
  if (!WATER_ONLY) {
    for (const record of terrainManager.resident.values()) {
      triangles += rasterize(fb, camera, record.mesh, TILE_TINT ? tintShaderFor(tintIndex++) : tintShader);
    }
  }

  const waves = buildWaveSet(ocean, 1337);
  const waterTriangles = [];
  for (const record of waterManager.resident.values()) waterTriangles.push(record.mesh);

  // Painter's order, far to near, so overlapping ocean patches blend correctly.
  const centroids = [];
  for (const mesh of waterTriangles) {
    for (let t = 0; t < mesh.indices.length; t += 3) {
      const ia = mesh.indices[t];
      centroids.push({
        mesh,
        t,
        z:
          Math.hypot(
            mesh.positions[ia * 3] - camera.eye.x,
            mesh.positions[ia * 3 + 1] - camera.eye.y,
            mesh.positions[ia * 3 + 2] - camera.eye.z,
          ),
      });
    }
  }
  centroids.sort((a, b) => b.z - a.z);

  let blended = 0;
  let waterTintIndex = 0;
  for (const entry of centroids) {
    const single = {
      positions: entry.mesh.positions,
      depths: entry.mesh.depths,
      indices: entry.mesh.indices.subarray(entry.t, entry.t + 3),
    };
    const waterShader = WATER_TINT
      ? ((colour) => () => [colour[0], colour[1], colour[2], 1])(TINTS[waterTintIndex++ % TINTS.length])
      : makeWaterShader(ocean, waves, camera);
    blended += rasterize(fb, camera, single, waterShader, { blend: true });
  }

  const png = encodePng(WIDTH, HEIGHT, fb.colour);
  await mkdir(EVIDENCE, { recursive: true });
  const path = join(EVIDENCE, `${name}.png`);
  await writeFile(path, png);

  const elapsedMs = Number(process.hrtime.bigint() - started) / 1e6;
  return {
    name,
    path,
    resolution: `${WIDTH}x${HEIGHT}`,
    terrainTiles: terrainManager.residentCount,
    waterTiles: waterManager.residentCount,
    terrainTriangles: triangles,
    waterTriangles: centroids.length,
    blendedWaterFragments: blended,
    renderMs: +elapsedMs.toFixed(0),
    eye,
    target,
    journal: field.ops.length,
    latLon: latLonFromDirection(
      { x: target.x, y: target.y, z: target.z },
    ),
  };
}

//------------------------------------------------------------------------------------------------
//                                                    SHOTS
//------------------------------------------------------------------------------------------------

/** Rotate a unit direction toward a tangent by `angle` radians (great-circle step). */
function stepToward(dir, tangent, angle) {
  return normalize({
    x: dir.x * Math.cos(angle) + tangent.x * Math.sin(angle),
    y: dir.y * Math.cos(angle) + tangent.y * Math.sin(angle),
    z: dir.z * Math.cos(angle) + tangent.z * Math.sin(angle),
  });
}

/**
 * Pick a *scenic* coast rather than merely a waterline point: sea on one flank,
 * land rising well above sea level on the other, and real relief further inland.
 * Deterministic, and the same rule the editor uses to seat its camera.
 */
function findScenicCoast(field, ocean = DEFAULT_OCEAN_PARAMS) {
  const R = field.radius;
  const sun = sunDirection(ocean);
  let best = null;
  const golden = Math.PI * (3 - Math.sqrt(5));

  for (let i = 0; i < 2400; i++) {
    const y = 1 - (i / 2399) * 1.85;
    const r = Math.sqrt(Math.max(0, 1 - y * y));
    const theta = golden * i;
    const dir = { x: Math.cos(theta) * r, y, z: Math.sin(theta) * r };
    const lat = latLonFromDirection(dir).latitude;
    if (Math.abs(lat) > 50) continue;

    const elevation = field.elevationAt(dir);
    if (Math.abs(elevation) > 70) continue;

    const east = normalize(cross({ x: 0, y: 1, z: 0 }, dir));
    const near = 4500 / R;
    const eastElevation = field.elevationAt(stepToward(dir, east, near));
    const westElevation = field.elevationAt(stepToward(dir, east, -near));
    const land = Math.max(eastElevation, westElevation);
    const sea = Math.min(eastElevation, westElevation);
    if (sea > -40) continue;
    if (land < 35 || land > 340) continue;

    const inland = stepToward(dir, east, eastElevation > westElevation ? 16000 / R : -16000 / R);
    const relief = field.elevationAt(inland) - sea;

    // The slope that faces us must face the sun, or the shot is a silhouette.
    const uphillSign = eastElevation > westElevation ? 1 : -1;
    const uphillWorld = {
      x: east.x * uphillSign,
      y: east.y * uphillSign,
      z: east.z * uphillSign,
    };
    const sunFacing = uphillWorld.x * sun.x + uphillWorld.y * sun.y + uphillWorld.z * sun.z;
    if (sunFacing < 0.4) continue;

    const score = (land - sea) * 0.5 + relief * 0.35 + sunFacing * 260 - Math.abs(lat) * 2;
    if (!best || score > best.score) {
      best = { dir, score, east, landEast: eastElevation >= westElevation, detail: { elevation, land, sea, relief } };
    }
  }
  return best;
}

/** Find a coastline with land in view; deterministic, same rule the editor uses. */
function findCoast(field) {
  let best = null;
  const golden = Math.PI * (3 - Math.sqrt(5));
  for (let i = 0; i < 900; i++) {
    const y = 1 - (i / 899) * 1.7;
    const r = Math.sqrt(Math.max(0, 1 - y * y));
    const theta = golden * i;
    const dir = { x: Math.cos(theta) * r, y, z: Math.sin(theta) * r };
    const elevation = field.elevationAt(dir);
    const lat = latLonFromDirection(dir).latitude;
    if (Math.abs(lat) > 45) continue;
    const score = Math.abs(elevation) < 120 ? 1 / (Math.abs(elevation) + 25) : 0;
    if (score > 0 && (!best || score > best.score)) best = { dir, score };
  }
  return best?.dir ?? directionFromLatLon(12, 24);
}

/**
 * Stand offshore and look back at the coast: the camera sits `offshoreRadians`
 * along the surface at a fixed altitude, so the shoreline sits in the lower third
 * and the horizon in the upper third, like a scouting still.
 */
/**
 * Stand on the water side of the coast and look *across* the shoreline, so the
 * waterline runs across the frame: water, then beach, then land rising behind.
 *
 * The uphill direction is measured from the field itself (finite differences of
 * the elevation a couple of kilometres out), so the shot always faces inland no
 * matter which way the coast happens to run.
 */
function placeCameraOnCoast(field, dir, { altitude = 1650, offshore = 2600, lookLift = 240 } = {}) {
  const up = dir;
  const reference = Math.abs(up.y) < 0.95 ? { x: 0, y: 1, z: 0 } : { x: 1, y: 0, z: 0 };
  const east = normalize(cross(reference, up));
  const north = cross(up, east);

  const R = field.radius;
  const step = 2200 / R;
  const de = field.elevationAt(stepToward(dir, east, step)) - field.elevationAt(stepToward(dir, east, -step));
  const dn = field.elevationAt(stepToward(dir, north, step)) - field.elevationAt(stepToward(dir, north, -step));
  const uphill = normalize({
    x: east.x * de + north.x * dn,
    y: east.y * de + north.y * dn,
    z: east.z * de + north.z * dn,
  });

  // Offshore = downhill, on the sea sphere.
  const offshoreDir = stepToward(up, uphill, -offshore / R);
  const eyeRadius = R + altitude;
  const eye = {
    x: offshoreDir.x * eyeRadius,
    y: offshoreDir.y * eyeRadius,
    z: offshoreDir.z * eyeRadius,
  };

  // Aim a little inland of the waterline, just above it.
  const lookDir = stepToward(up, uphill, 420 / R);
  const coastRadius = field.surfaceRadius(lookDir);
  const target = {
    x: lookDir.x * (Math.max(coastRadius, R) + lookLift),
    y: lookDir.y * (Math.max(coastRadius, R) + lookLift),
    z: lookDir.z * (Math.max(coastRadius, R) + lookLift),
  };
  return { eye, target };
}

function fmtVec(v) {
  return `(${v.x.toFixed(0)}, ${v.y.toFixed(0)}, ${v.z.toFixed(0)})`;
}

function normalize(v) {
  const len = Math.hypot(v.x, v.y, v.z) || 1;
  return { x: v.x / len, y: v.y / len, z: v.z / len };
}

function cross(a, b) {
  return {
    x: a.y * b.z - a.z * b.y,
    y: a.z * b.x - a.x * b.z,
    z: a.x * b.y - a.y * b.x,
  };
}

async function main() {
  const probe = new PlanetField({ radius: 900000, seed: 4029 });
  const scenic = findScenicCoast(probe, DEFAULT_OCEAN_PARAMS);
  const coast = scenic?.dir ?? findCoast(probe);
  const coastLatLon = latLonFromDirection(coast);
  console.log(`coast at lat ${coastLatLon.latitude.toFixed(2)} lon ${coastLatLon.longitude.toFixed(2)}:`,
    JSON.stringify(scenic?.detail ?? {}));
  const coastCamera = placeCameraOnCoast(probe, coast);

  const shots = [
    {
      name: 'coast-day',
      description: 'Coastline from 5 km offshore: shoreline foam, shallow-to-deep water, terrain bands',
      ...coastCamera,
      fovDeg: 55,
    },
    {
      name: 'planet',
      description: 'Whole planet at LOD 0-3: continents, gulfs and islands from the analytic base',
      eye: { x: 900000 * 0.62, y: 900000 * 2.05, z: 900000 * 1.35 },
      target: { x: 0, y: 0, z: 0 },
      fovDeg: 42,
    },
    {
      name: 'island-sculpt',
      description: 'Same coast after an Island stamp and a Gulf carve (journal: 2 ops), re-framed on the sculpted terrain',
      ...coastCamera,
      fovDeg: 55,
      cameraFor: (sculpted) => {
        const scenicAfter = findScenicCoast(sculpted, DEFAULT_OCEAN_PARAMS);
        const anchor = scenicAfter?.dir ?? coast;
        return placeCameraOnCoast(sculpted, anchor, { altitude: 3200, offshore: 7200, lookLift: 1100 });
      },
      sculpt: [
        { toolId: 'island', dir: coast, radius: 26000, strength: 2400, falloff: 0.35, seed: 77 },
        {
          toolId: 'gulf',
          dir: normalize({ x: coast.x + 0.02, y: coast.y - 0.01, z: coast.z + 0.015 }),
          radius: 15000,
          strength: 1600,
          falloff: 0.45,
          seed: 91,
        },
      ],
    },
  ];

  const selected = ONLY ? shots.filter((shot) => shot.name === ONLY) : shots;
  const results = [];
  for (const shot of selected) {
    const result = await renderScene(shot);
    result.description = shot.description;
    results.push(result);
    console.log(`rendered ${shot.name}: ${result.terrainTiles} terrain + ${result.waterTiles} water tiles, ` +
      `${result.terrainTriangles} tris, ${result.renderMs} ms -> ${result.path}`);
  }

  const report = {
    tool: 'tools/render-qa.mjs',
    generatedAt: new Date().toISOString(),
    resolution: `${WIDTH}x${HEIGHT}`,
    coastlineProbe: { latitude: +coastLatLon.latitude.toFixed(2), longitude: +coastLatLon.longitude.toFixed(2) },
    shots: results,
    notes: [
      'Meshes are built by the same TileManager, PlanetField and meshers the viewport uses.',
      'Shading is an independent CPU reimplementation of the viewport shaders (terrain palette + Beer-Lambert water).',
      'No textures, no post-processing, no screenshots of the browser build.',
    ],
  };
  await writeFile(join(EVIDENCE, 'render-qa.json'), `${JSON.stringify(report, null, 2)}\n`);
  console.log(`\nreport: ${join(EVIDENCE, 'render-qa.json')}`);
}

await main();
