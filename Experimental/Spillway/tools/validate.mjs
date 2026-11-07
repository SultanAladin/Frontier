/*====================================================================================================================================
                                                        VALIDATE.MJS
====================================================================================================================================*/
// 🧩 Headless verification: DOM-free canvas stub, chunk meshing, ride simulation, collision budget, and an SVG course map.

import { writeFileSync, mkdirSync } from 'node:fs';
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
        getImageData: () => ({ data: new Uint8ClampedArray(4) }),
    };
    return new Proxy(target, {
        get(object, property)
        {
            if (property in object)
            {
                return object[property];
            }
            return () => undefined;
        },
        set(object, property, value)
        {
            object[property] = value;
            return true;
        },
    });
}

const documentStub = {
    createElement(tag)
    {
        if (tag !== 'canvas')
        {
            return { style: {}, appendChild() {}, addEventListener() {} };
        }
        return {
            width: 1,
            height: 1,
            getContext: () => makeContext2D(),
            style: {},
        };
    },
};

globalThis.document = documentStub;

//------------------------------------------------------------------------------------------------------------------------
//                                                       IMPORTS
//------------------------------------------------------------------------------------------------------------------------

const THREE = await import('../vendor/three.module.min.js');
const { CoursePath, SectionOutline } = await import('../src/path.js');
const { Ride } = await import('../src/ride.js');
const Textures = await import('../src/textures.js');
const { CreateMaterials, BuildChunk } = await import('../src/geometry.js');
const { World } = await import('../src/world.js');
const { Rider } = await import('../src/rider.js');

const failures = [];
const notes = [];

function Check(condition, message)
{
    if (!condition)
    {
        failures.push(message);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       TEXTURES
//------------------------------------------------------------------------------------------------------------------------

const textures = {
    concrete : Textures.ConcreteTexture(),
    desert   : Textures.DesertTexture(),
    water    : Textures.WaterTexture(),
    dust     : Textures.DustSprite(),
    beam     : Textures.BeamTexture(),
    sky      : Textures.SkyTexture(),
};

for (const [name, texture] of Object.entries(textures))
{
    Check(texture && texture.isTexture, `texture ${name} was not created`);
}

const materials = CreateMaterials(textures);
notes.push(`materials: ${Object.keys(materials).join(', ')}`);

//------------------------------------------------------------------------------------------------------------------------
//                                                       COURSE
//------------------------------------------------------------------------------------------------------------------------

const course = new CoursePath(20261107);
course.ExtendTo(6000);

const kinds = {};
for (const chunk of course.chunks)
{
    kinds[chunk.kind] = (kinds[chunk.kind] ?? 0) + 1;
}
notes.push(`course: ${course.chunks.length} chunks over ${Math.round(course.endDistance)} m`);
notes.push(`kinds: ${Object.entries(kinds).map(([k, v]) => `${k}×${v}`).join(' ')}`);

Check(course.chunks.filter((c) => c.kind === 'turn').length >= 4, 'not enough 90-degree turns generated');

// Windowed sections keep a consistent outline column count inside a chunk.
for (const chunk of course.chunks)
{
    const outline = SectionOutline({ ...course.nodes[chunk.nodeTo], kind: chunk.kind, half: chunk.half, bank: chunk.bank });
    Check(outline.points.length >= 6, `section for ${chunk.kind} is degenerate`);
    for (const [lateral, height] of outline.points)
    {
        Check(Number.isFinite(lateral) && Number.isFinite(height), `non-finite section point in ${chunk.kind}`);
    }
}

// Surface must be continuous inside a chunk and bounded near the centreline.
for (let i = 1; i < course.nodes.length; i++)
{
    const a = course.nodes[i - 1];
    const b = course.nodes[i];
    const jump = Math.hypot(b.x - a.x, b.z - a.z);
    Check(jump < 2.6, `centreline gap of ${jump.toFixed(2)} m at node ${i}`);
    Check(Math.abs(b.y - a.y) < 2.0, `vertical cliff of ${(b.y - a.y).toFixed(2)} m at node ${i}`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       MESH BUILD
//------------------------------------------------------------------------------------------------------------------------

let triangles = 0;
let vertices = 0;
let meshes = 0;

for (const chunk of course.chunks.slice(0, 40))
{
    const group = BuildChunk(course, chunk, materials);
    group.traverse((child) => {
        if (!child.isMesh || !child.geometry)
        {
            return;
        }
        meshes += 1;
        const position = child.geometry.getAttribute('position');
        vertices += position.count;
        triangles += child.geometry.index ? child.geometry.index.count / 3 : position.count / 3;

        for (let i = 0; i < position.count; i++)
        {
            if (!Number.isFinite(position.getX(i)) || !Number.isFinite(position.getY(i)) || !Number.isFinite(position.getZ(i)))
            {
                failures.push(`non-finite vertex in ${chunk.kind}`);
                return;
            }
        }

        const colour = child.geometry.getAttribute('color');
        if (child.material === materials.concrete)
        {
            Check(Boolean(colour), `concrete mesh in ${chunk.kind} is missing vertex colours`);
        }
    });
}

notes.push(`mesh: ${meshes} meshes, ${vertices} vertices, ${Math.round(triangles)} triangles over 40 chunks`);
Check(meshes > 100, 'chunk meshing produced too few meshes');
Check(triangles > 10000, 'chunk meshing produced too few triangles');

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCENE GRAPH
//------------------------------------------------------------------------------------------------------------------------

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(62, 16 / 9, 0.4, 8200);
camera.position.set(0, 8, -10);
const world = new World(scene, textures);
const rider = new Rider();
scene.add(rider.root);
for (let i = 0; i < 60; i++)
{
    world.update(1 / 60, camera, new THREE.Vector3(0, 0, 100 + i), 30);
    rider.update(1 / 60, { grounded: true, slip: 0.2, driftHeld: true, lean: -0.2 }, 0.6);
}
Check(scene.children.length > 10, 'world scene graph is empty');
Check(Number.isFinite(world.key.position.x), 'shadow light position is not finite');

//------------------------------------------------------------------------------------------------------------------------
//                                                       RIDE SIMULATION
//------------------------------------------------------------------------------------------------------------------------

function Simulate(mode, seconds = 180, seed = 20261107)
{
    const path = new CoursePath(seed);
    const ride = new Ride(path);
    let elapsed = 0;
    let wallHits = 0;
    let maxSpeed = 0;
    let airSeconds = 0;
    let badFrames = 0;
    const events = {};

    while (elapsed < seconds)
    {
        const call = ride.cornerCallout();
        const inCorner = Math.abs(path.Sample(ride.s).curv) > 0.004;
        let drift = false;
        let steer = 0;

        if (mode === 'drift')
        {
            drift = inCorner || (call.active && call.distance < ride.speed * 1.15);
            steer = Math.max(-1, Math.min(1, -ride.u * 0.14 - ride.latVel * 0.11));
        }
        else if (mode === 'steer')
        {
            steer = Math.max(-1, Math.min(1, -ride.u * 0.18 - ride.latVel * 0.14));
        }

        ride.update(1 / 60, { steer, drift });
        for (const trigger of ride.events)
        {
            events[trigger.type] = (events[trigger.type] ?? 0) + 1;
            if (trigger.type === 'wall')
            {
                wallHits += 1;
            }
        }

        maxSpeed = Math.max(maxSpeed, ride.speed);
        if (!ride.grounded)
        {
            airSeconds += 1 / 60;
        }
        if (!Number.isFinite(ride.s + ride.u + ride.y + ride.speed + ride.score))
        {
            badFrames += 1;
            break;
        }
        elapsed += 1 / 60;
    }

    return {
        mode,
        distance: ride.distance,
        score: ride.score,
        maxSpeed,
        wallHits,
        airSeconds,
        drifts: events.boost ?? 0,
        airs: ride.airCount,
        bails: ride.bailCount,
        bestDrift: ride.driftBest,
        badFrames,
    };
}

const driftRun = Simulate('drift');
const steerRun = Simulate('steer');
const idleRun = Simulate('idle');

notes.push(`ride(drift): ${Math.round(driftRun.distance)} m, ${driftRun.score} pts, ${(driftRun.maxSpeed * 3.6).toFixed(0)} km/h, ${driftRun.wallHits} wall hits, ${driftRun.drifts} slingshots, ${driftRun.airs} airs, best carve ${driftRun.bestDrift.toFixed(2)} s`);
notes.push(`ride(steer): ${Math.round(steerRun.distance)} m, ${steerRun.score} pts, ${steerRun.wallHits} wall hits`);
notes.push(`ride(idle):  ${Math.round(idleRun.distance)} m, ${idleRun.wallHits} wall hits, ${idleRun.bails} bails`);

Check(driftRun.badFrames === 0 && steerRun.badFrames === 0 && idleRun.badFrames === 0, 'ride simulation produced non-finite state');
Check(driftRun.distance > 5000, 'rider covered too little ground');
Check(driftRun.drifts > 5, 'carving never charged a slingshot');
Check(driftRun.score > steerRun.score * 1.4, 'drifting does not outscore cruising');
Check(idleRun.wallHits > driftRun.wallHits, 'ignoring the elbows is not punished');
Check(driftRun.airs > 3, 'ramps and rollers never launched the rider');

//------------------------------------------------------------------------------------------------------------------------
//                                                       COURSE MAP
//------------------------------------------------------------------------------------------------------------------------

function WriteCourseMap()
{
    const mapCourse = new CoursePath(20261107);
    mapCourse.ExtendTo(2400);
    let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
    for (const node of mapCourse.nodes)
    {
        minX = Math.min(minX, node.x); maxX = Math.max(maxX, node.x);
        minZ = Math.min(minZ, node.z); maxZ = Math.max(maxZ, node.z);
    }
    const pad = 40;
    const width = 1100;
    const scale = width / (maxX - minX + pad * 2);
    const height = (maxZ - minZ + pad * 2) * scale + 120;
    const toX = (x) => (x - minX + pad) * scale + 40;
    const toY = (z) => (z - minZ + pad) * scale + 60;

    const colours = {
        deck: '#ded6c3', pump: '#d8a13a', bowl: '#c9a87f', kicker: '#c04a2c',
        tunnel: '#5a5147', bridge: '#1f6b66', chute: '#8a4a2f', cascade: '#7d9aa0',
        basin: '#3f8f86', lift: '#a89c86', turn: '#c04a2c',
    };

    const parts = [];
    parts.push(`<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">`);
    parts.push(`<rect width="100%" height="100%" fill="#efe7d4"/>`);
    parts.push(`<text x="40" y="36" font-family="monospace" font-size="18" fill="#211d18">SPILLWAY · COURSE MAP · seed 20261107 · first 2400 m</text>`);

    for (const chunk of mapCourse.chunks)
    {
        const nodes = mapCourse.nodes.slice(chunk.nodeFrom, chunk.nodeTo + 1);
        const left = [], right = [];
        for (const node of nodes)
        {
            const cos = Math.cos(node.heading);
            const sin = Math.sin(node.heading);
            left.push([toX(node.x - cos * chunk.half), toY(node.z + sin * chunk.half)]);
            right.push([toX(node.x + cos * chunk.half), toY(node.z - sin * chunk.half)]);
        }
        const points = left.concat(right.reverse()).map(([x, y]) => `${x.toFixed(1)},${y.toFixed(1)}`).join(' ');
        parts.push(`<polygon points="${points}" fill="${colours[chunk.kind] ?? '#999'}" stroke="#211d18" stroke-width="0.8" opacity="0.9"/>`);
        const mid = nodes[Math.floor(nodes.length / 2)];
        parts.push(`<circle cx="${toX(mid.x).toFixed(1)}" cy="${toY(mid.z).toFixed(1)}" r="2.4" fill="#211d18"/>`);
    }

    parts.push(`<text x="40" y="${height - 20}" font-family="monospace" font-size="13" fill="#5a5147">legend: deck · pump · bowl · kicker · tunnel · overpass · chute · cascade · basin · lift · 90° turn</text>`);
    parts.push(`</svg>`);

    writeFileSync(join(results, 'CourseMap.svg'), parts.join('\n'));
}

WriteCourseMap();

//------------------------------------------------------------------------------------------------------------------------
//                                                       REPORT
//------------------------------------------------------------------------------------------------------------------------

const report = [
    '# Spillway headless verification',
    '',
    `Generated: ${new Date().toISOString()}`,
    `Node: ${process.version}`,
    `three.js: r${THREE.REVISION}`,
    '',
    '## Measurements',
    '',
    '| Check | Result |',
    '|---|---|',
    ...notes.map((note) => `| ${note.split(':')[0]} | ${note.slice(note.indexOf(':') + 1).trim()} |`),
    '',
    '## Ride comparison (180 s each, seed 20261107)',
    '',
    '| Run | Distance | Score | Top speed | Wall hits | Slingshots | Airs | Bails | Best carve |',
    '|---|---|---|---|---|---|---|---|---|',
    ...([driftRun, steerRun, idleRun].map((run) =>
        `| ${run.mode} | ${Math.round(run.distance)} m | ${run.score} | ${(run.maxSpeed * 3.6).toFixed(0)} km/h | ${run.wallHits} | ${run.drifts} | ${run.airs} | ${run.bails} | ${run.bestDrift.toFixed(2)} s |`)),
    '',
    '## Assertions',
    '',
    failures.length === 0 ? 'All assertions passed.' : failures.map((failure) => `- FAIL: ${failure}`).join('\n'),
    '',
    '## Not covered here',
    '',
    'WebGL rasterisation, shader compilation, browser input and audio scheduling are exercised only in a real browser.',
    '',
].join('\n');

writeFileSync(join(results, 'Verification.md'), report);
console.log(report);

if (failures.length > 0)
{
    process.exitCode = 1;
}
