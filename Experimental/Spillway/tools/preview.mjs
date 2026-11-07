/*====================================================================================================================================
                                                        PREVIEW.MJS
====================================================================================================================================*/
// 🧩 Software rasteriser for the built scene: no WebGL, no textures. Flat sun shading plus z-buffer and haze, written to PNG.
// Purpose: verify chunk geometry, scale and camera framing without a browser.

import { writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const results = join(here, '..', 'Results');
mkdirSync(results, { recursive: true });

//------------------------------------------------------------------------------------------------------------------------
//                                                       CANVAS STUB
//------------------------------------------------------------------------------------------------------------------------

const gradient = { addColorStop() {} };

function makeContext2D()
{
    const target = {
        createLinearGradient: () => gradient,
        createRadialGradient: () => gradient,
        measureText: () => ({ width: 80 }),
    };
    return new Proxy(target, {
        get(object, property)
        {
            return property in object ? object[property] : () => undefined;
        },
        set(object, property, value)
        {
            object[property] = value;
            return true;
        },
    });
}

globalThis.document = {
    createElement: () => ({ width: 1, height: 1, getContext: () => makeContext2D(), style: {} }),
};

const THREE = await import('../vendor/three.module.min.js');
const { CoursePath } = await import('../src/path.js');
const Textures = await import('../src/textures.js');
const { CreateMaterials, BuildChunk } = await import('../src/geometry.js');
const { Rider } = await import('../src/rider.js');
const { SUN_DIRECTION } = await import('../src/world.js');

const textures = {
    concrete: Textures.ConcreteTexture(),
    desert: Textures.DesertTexture(),
    water: Textures.WaterTexture(),
    dust: Textures.DustSprite(),
    beam: Textures.BeamTexture(),
    sky: Textures.SkyTexture(),
};
const materials = CreateMaterials(textures);

// Average albedo of each textured material, since this rasteriser has no samplers.
const ALBEDO = new Map([
    [materials.concrete, [0.87, 0.85, 0.79]],
    [materials.paint, [0.88, 0.86, 0.80]],
    [materials.steel, [1, 1, 1]],
    [materials.water, [0.24, 0.52, 0.50]],
]);

//------------------------------------------------------------------------------------------------------------------------
//                                                       PNG WRITER
//------------------------------------------------------------------------------------------------------------------------

function crc32(buffer)
{
    let crc = 0xFFFFFFFF;
    for (let i = 0; i < buffer.length; i++)
    {
        crc ^= buffer[i];
        for (let bit = 0; bit < 8; bit++)
        {
            crc = (crc >>> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return (crc ^ 0xFFFFFFFF) >>> 0;
}

function chunk(type, data)
{
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length, 0);
    const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(body), 0);
    return Buffer.concat([length, body, crc]);
}

function writePng(path, width, height, rgb)
{
    const stride = width * 3;
    const raw = Buffer.alloc((stride + 1) * height);
    for (let y = 0; y < height; y++)
    {
        raw[y * (stride + 1)] = 0;
        rgb.copy(raw, y * (stride + 1) + 1, y * stride, y * stride + stride);
    }

    const header = Buffer.alloc(13);
    header.writeUInt32BE(width, 0);
    header.writeUInt32BE(height, 4);
    header[8] = 8;
    header[9] = 2;
    header[10] = 0;
    header[11] = 0;
    header[12] = 0;

    const png = Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
        chunk('IHDR', header),
        chunk('IDAT', deflateSync(raw, { level: 6 })),
        chunk('IEND', Buffer.alloc(0)),
    ]);
    writeFileSync(path, png);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       RASTERISER
//------------------------------------------------------------------------------------------------------------------------

const WIDTH = 1120;
const HEIGHT = 630;

class Raster
{
    constructor(width, height)
    {
        this.width = width;
        this.height = height;
        this.colour = new Float32Array(width * height * 3);
        this.depth = new Float32Array(width * height).fill(Infinity);
        this.paintSky();
    }

    paintSky()
    {
        const zenith = [0.42, 0.51, 0.66];
        const horizon = [0.95, 0.81, 0.64];
        const below = [0.63, 0.52, 0.40];
        const horizonLine = this.height * 0.54;
        for (let y = 0; y < this.height; y++)
        {
            let colour;
            if (y < horizonLine)
            {
                const τ = y / horizonLine;
                colour = zenith.map((value, i) => value + (horizon[i] - value) * Math.pow(τ, 0.62));
            }
            else
            {
                const τ = Math.min(1, (y - horizonLine) / (this.height - horizonLine));
                colour = horizon.map((value, i) => value + (below[i] - value) * τ);
            }
            for (let x = 0; x < this.width; x++)
            {
                const index = (y * this.width + x) * 3;
                this.colour[index] = colour[0];
                this.colour[index + 1] = colour[1];
                this.colour[index + 2] = colour[2];
            }
        }
    }

    triangle(p0, p1, p2, colour, depth0, depth1, depth2)
    {
        const minX = Math.max(0, Math.floor(Math.min(p0[0], p1[0], p2[0])));
        const maxX = Math.min(this.width - 1, Math.ceil(Math.max(p0[0], p1[0], p2[0])));
        const minY = Math.max(0, Math.floor(Math.min(p0[1], p1[1], p2[1])));
        const maxY = Math.min(this.height - 1, Math.ceil(Math.max(p0[1], p1[1], p2[1])));
        if (minX > maxX || minY > maxY)
        {
            return;
        }

        const area = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p1[1] - p0[1]) * (p2[0] - p0[0]);
        if (Math.abs(area) < 1e-9)
        {
            return;
        }
        const inverse = 1 / area;

        for (let y = minY; y <= maxY; y++)
        {
            for (let x = minX; x <= maxX; x++)
            {
                const px = x + 0.5;
                const py = y + 0.5;
                const w0 = ((p1[0] - px) * (p2[1] - py) - (p1[1] - py) * (p2[0] - px)) * inverse;
                const w1 = ((p2[0] - px) * (p0[1] - py) - (p2[1] - py) * (p0[0] - px)) * inverse;
                const w2 = 1 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0)
                {
                    continue;
                }
                const depth = w0 * depth0 + w1 * depth1 + w2 * depth2;
                const index = y * this.width + x;
                if (depth >= this.depth[index] || depth <= 0)
                {
                    continue;
                }
                this.depth[index] = depth;
                const offset = index * 3;
                this.colour[offset] = colour[0];
                this.colour[offset + 1] = colour[1];
                this.colour[offset + 2] = colour[2];
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCENE ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

const course = new CoursePath(20261107);
course.ExtendTo(3000);

const scene = new THREE.Scene();
const rider = new Rider();
scene.add(rider.root);

const materialOf = (material) => material;

function buildAround(distance, span = 420)
{
    for (const chunk of course.chunks)
    {
        if (chunk.s1 < distance - 60 || chunk.s0 > distance + span)
        {
            continue;
        }
        scene.add(BuildChunk(course, chunk, materials));
    }
}

function placeRider(distance, driftAmount)
{
    const sample = course.Sample(distance);
    const cos = Math.cos(sample.heading);
    const sin = Math.sin(sample.heading);
    rider.setPosition(sample.x + cos * 1.2, sample.y + 0.05, sample.z - sin * 1.2, sample.heading + driftAmount);
    rider.update(1 / 60, { grounded: true, slip: driftAmount, driftHeld: true, lean: -0.28 }, 0.8);
}

// Ground plane patch so the aqueduct reads as elevated.
function addGround()
{
    const geometry = new THREE.PlaneGeometry(2400, 2400);
    const material = new THREE.MeshStandardMaterial({ color: 0xc2a173, roughness: 1 });
    const plane = new THREE.Mesh(geometry, material);
    plane.rotation.x = -Math.PI / 2;
    plane.position.y = -22;
    scene.add(plane);
}

addGround();

//------------------------------------------------------------------------------------------------------------------------
//                                                       RENDER SHOT
//------------------------------------------------------------------------------------------------------------------------

function RenderShot(name, distance, options)
{
    scene.clear();
    scene.add(rider.root);
    addGround();
    buildAround(distance, options.span ?? 460);

    const sample = course.Sample(distance);
    const cos = Math.cos(sample.heading);
    const sin = Math.sin(sample.heading);
    const forward = new THREE.Vector3(sin, 0, cos);
    const right = new THREE.Vector3(cos, 0, -sin);
    const riderPosition = new THREE.Vector3(sample.x, sample.y, sample.z);

    placeRider(distance, options.slip ?? -0.22);

    const camera = new THREE.PerspectiveCamera(options.fov ?? 62, WIDTH / HEIGHT, 0.4, 3000);
    const offset = options.offset ?? { back: 10.5, height: 3.9, look: 16 };
    camera.position.copy(riderPosition).addScaledVector(forward, -offset.back).add(new THREE.Vector3(0, offset.height, 0)).addScaledVector(right, options.lateral ?? 0);
    const target = riderPosition.clone().addScaledVector(forward, offset.look).add(new THREE.Vector3(0, 1.4, 0));
    camera.lookAt(target);
    camera.updateMatrixWorld(true);
    camera.updateProjectionMatrix();

    const viewProjection = new THREE.Matrix4().multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
    const raster = new Raster(WIDTH, HEIGHT);

    scene.updateMatrixWorld(true);

    let drawn = 0;
    scene.traverse((child) => {
        if (!child.isMesh || !child.geometry)
        {
            return;
        }
        const geometry = child.geometry;
        const position = geometry.getAttribute('position');
        const normal = geometry.getAttribute('normal');
        const colourAttribute = geometry.getAttribute('color');
        if (!position)
        {
            return;
        }

        const index = geometry.index;
        const count = index ? index.count : position.count;
        const albedo = ALBEDO.get(child.material) ?? null;

        const worldPositions = new Float32Array(position.count * 3);
        const worldNormals = new Float32Array(position.count * 3);
        const point = new THREE.Vector3();
        const normalVector = new THREE.Vector3();
        const normalMatrix = new THREE.Matrix3().getNormalMatrix(child.matrixWorld);

        for (let i = 0; i < position.count; i++)
        {
            point.set(position.getX(i), position.getY(i), position.getZ(i)).applyMatrix4(child.matrixWorld);
            worldPositions[i * 3] = point.x;
            worldPositions[i * 3 + 1] = point.y;
            worldPositions[i * 3 + 2] = point.z;

            normalVector.set(normal.getX(i), normal.getY(i), normal.getZ(i)).applyMatrix3(normalMatrix).normalize();
            worldNormals[i * 3] = normalVector.x;
            worldNormals[i * 3 + 1] = normalVector.y;
            worldNormals[i * 3 + 2] = normalVector.z;
        }

        for (let t = 0; t < count; t += 3)
        {
            const a = index ? index.getX(t) : t;
            const b = index ? index.getX(t + 1) : t + 1;
            const c = index ? index.getX(t + 2) : t + 2;

            const projected = [];
            let depthSum = 0;
            let skip = false;
            for (const vertex of [a, b, c])
            {
                const clip = new THREE.Vector4(worldPositions[vertex * 3], worldPositions[vertex * 3 + 1], worldPositions[vertex * 3 + 2], 1).applyMatrix4(viewProjection);
                if (clip.w <= 0.001)
                {
                    skip = true;
                    break;
                }
                projected.push([(clip.x / clip.w * 0.5 + 0.5) * WIDTH, (1 - (clip.y / clip.w * 0.5 + 0.5)) * HEIGHT, clip.w]);
                depthSum += clip.w;
            }
            if (skip)
            {
                continue;
            }

            // Face normal from world positions for flat sun shading.
            const ax = worldPositions[b * 3] - worldPositions[a * 3];
            const ay = worldPositions[b * 3 + 1] - worldPositions[a * 3 + 1];
            const az = worldPositions[b * 3 + 2] - worldPositions[a * 3 + 2];
            const bx = worldPositions[c * 3] - worldPositions[a * 3];
            const by = worldPositions[c * 3 + 1] - worldPositions[a * 3 + 1];
            const bz = worldPositions[c * 3 + 2] - worldPositions[a * 3 + 2];
            let nx = ay * bz - az * by;
            let ny = az * bx - ax * bz;
            let nz = ax * by - ay * bx;
            const length = Math.hypot(nx, ny, nz);
            if (length < 1e-8)
            {
                continue;
            }
            nx /= length;
            ny /= length;
            nz /= length;

            let base;
            if (albedo)
            {
                base = albedo;
            }
            else
            {
                base = [child.material.color.r, child.material.color.g, child.material.color.b];
                if (child.material.map)
                {
                    base = [base[0] * 0.88, base[1] * 0.85, base[2] * 0.79];
                }
            }
            if (colourAttribute)
            {
                base = [
                    base[0] * colourAttribute.getX(a),
                    base[1] * colourAttribute.getY(a),
                    base[2] * colourAttribute.getZ(a),
                ];
            }

            const sun = Math.max(0, nx * SUN_DIRECTION.x + ny * SUN_DIRECTION.y + nz * SUN_DIRECTION.z);
            const sky = 0.42 + 0.3 * Math.max(0, ny);
            const bounce = 0.16 * Math.max(0, -ny);
            const exposure = 0.26 + sun * 0.95 + sky * 0.34 + bounce;

            let colour = [base[0] * exposure, base[1] * exposure, base[2] * exposure];

            // Haze and a gentle filmic shoulder.
            const depth = depthSum / 3;
            const fog = 1 - Math.exp(-Math.pow(0.00092 * depth, 2) * 1.0);
            const haze = [0.88, 0.78, 0.65];
            colour = colour.map((value, i) => value * (1 - fog) + haze[i] * fog);
            colour = colour.map((value) => {
                const filmic = (value * 1.15) / (value * 1.15 + 0.72);
                return Math.pow(Math.min(1, filmic * 1.12), 1 / 1.85);
            });

            const centre = [(projected[0][0] + projected[1][0] + projected[2][0]) / 3, (projected[0][1] + projected[1][1] + projected[2][1]) / 3];
            const backface = (projected[0][0] - projected[2][0]) * (projected[1][1] - projected[2][1]) - (projected[1][0] - projected[2][0]) * (projected[0][1] - projected[2][1]);
            if (backface === 0)
            {
                continue;
            }

            raster.triangle(
                projected[0], projected[1], projected[2],
                colour,
                projected[0][2], projected[1][2], projected[2][2]
            );
            drawn += 1;
        }
    });

    const rgb = Buffer.alloc(WIDTH * HEIGHT * 3);
    for (let i = 0; i < WIDTH * HEIGHT * 3; i++)
    {
        rgb[i] = Math.max(0, Math.min(255, Math.round(raster.colour[i] * 255)));
    }
    writePng(join(results, `${name}.png`), WIDTH, HEIGHT, rgb);
    return drawn;
}

// Pick showcase distances: an elbow, a tunnel crown, and a kicker lip.
const turn = course.chunks.find((c) => c.kind === 'turn' && c.s0 > 300);
const tunnel = course.chunks.find((c) => c.kind === 'tunnel');
const kicker = course.chunks.find((c) => c.kind === 'kicker');
const bridge = course.chunks.find((c) => c.kind === 'bridge');
const bowl = course.chunks.find((c) => c.kind === 'bowl');
const basin = course.chunks.find((c) => c.kind === 'basin');

const shots = [
    ['PreviewTurn', turn ? turn.s0 + turn.length * 0.45 : 420, { slip: -0.3, offset: { back: 8.2, height: 3.2, look: 14 } }],
    ['PreviewTunnel', tunnel ? tunnel.s0 + tunnel.length * 0.35 : 900, { slip: 0, offset: { back: 7.6, height: 3.0, look: 13 } }],
    ['PreviewKicker', kicker ? kicker.s0 + kicker.length * 0.45 : 1300, { slip: 0.1, offset: { back: 8.4, height: 3.4, look: 14 } }],
    ['PreviewBridge', bridge ? bridge.s0 + bridge.length * 0.4 : 1600, { slip: -0.12, offset: { back: 7.8, height: 3.2, look: 14 } }],
    ['PreviewBowl', bowl ? bowl.s0 + bowl.length * 0.4 : 1900, { slip: -0.35, offset: { back: 8.0, height: 3.6, look: 13 } }],
    ['PreviewBasin', basin ? basin.s0 + basin.length * 0.42 : 2200, { slip: -0.18, offset: { back: 8.4, height: 3.4, look: 15 } }],
];

for (const [name, distance, options] of shots)
{
    const triangles = RenderShot(name, distance, options);
    console.log(`${name}: s=${Math.round(distance)} m, ${triangles} triangles rasterised`);
}

console.log(`written to ${results}`);
