/*====================================================================================================================================
                                                          GEOMETRY.JS
====================================================================================================================================*/
// 🧩 Sweeps the aqueduct sections into meshes and hangs the architecture: piers, tunnels, overpasses, rails, paint, signs, water.

import * as THREE from '../vendor/three.module.min.js';
import { SectionOutline, SECTION_STYLES } from './path.js';
import { PALETTE, SignTexture } from './textures.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const CONCRETE_DECK   = [0.94, 0.91, 0.84];
const CONCRETE_WALL   = [0.80, 0.78, 0.72];
const CONCRETE_SKIRT  = [0.55, 0.54, 0.51];
const CONCRETE_DEEP   = [0.40, 0.40, 0.39];
const STEEL_RUST      = [0.62, 0.33, 0.22];
const STEEL_DARK      = [0.28, 0.30, 0.31];
const PAINT_CREAM     = [0.99, 0.95, 0.85];
const PAINT_RED       = [0.83, 0.26, 0.14];
const PAINT_TEAL      = [0.10, 0.42, 0.40];

const SIGN_BOOK = [
    ['SLUICE 04', 'FLOW 12 M3/S'],
    ['NO DIVING', 'DEPTH 3.5 M'],
    ['KEEP CLEAR', 'GATE 07'],
    ['SECTOR 9', 'SPILLWAY'],
    ['90 DEG', 'DRIFT AHEAD'],
    ['DROP', 'RISK OF FALL'],
];

const signCache = new Map();

//------------------------------------------------------------------------------------------------------------------------
//                                                    GEOMETRY BUILDER
//------------------------------------------------------------------------------------------------------------------------

// Accumulates many faces into one buffer so a whole chunk is a handful of draw calls.
export class GeometryBuilder
{
    constructor()
    {
        this.positions = [];
        this.normals = [];
        this.uvs = [];
        this.colors = [];
        this.indices = [];
    }

    get empty()
    {
        return this.indices.length === 0;
    }

    // Four corners in perimeter order; winding is corrected against the supplied outward normal.
    AddQuad(Corners, Color, Uvs, Outward = null)
    {
        const [P0, P1, P2, P3] = Corners;
        const edge1 = [P1[0] - P0[0], P1[1] - P0[1], P1[2] - P0[2]];
        const edge2 = [P2[0] - P0[0], P2[1] - P0[1], P2[2] - P0[2]];
        let normal = [
            edge1[1] * edge2[2] - edge1[2] * edge2[1],
            edge1[2] * edge2[0] - edge1[0] * edge2[2],
            edge1[0] * edge2[1] - edge1[1] * edge2[0],
        ];
        const length = Math.hypot(normal[0], normal[1], normal[2]) || 1;
        normal = [normal[0] / length, normal[1] / length, normal[2] / length];

        let order = [P0, P1, P2, P3];
        if (Outward && normal[0] * Outward[0] + normal[1] * Outward[1] + normal[2] * Outward[2] < 0)
        {
            order = [P3, P2, P1, P0];
            normal = [-normal[0], -normal[1], -normal[2]];
        }

        const base = this.positions.length / 3;
        for (let i = 0; i < 4; i++)
        {
            this.positions.push(order[i][0], order[i][1], order[i][2]);
            this.normals.push(normal[0], normal[1], normal[2]);
            this.uvs.push(Uvs[i][0], Uvs[i][1]);
            this.colors.push(Color[0], Color[1], Color[2]);
        }
        this.indices.push(base, base + 1, base + 2, base, base + 2, base + 3);
    }

    AddBox(Center, Size, Yaw, Color)
    {
        const halfX = Size[0] / 2;
        const halfY = Size[1] / 2;
        const halfZ = Size[2] / 2;
        const cos = Math.cos(Yaw);
        const sin = Math.sin(Yaw);

        const transform = (x, y, z) => [
            Center[0] + x * cos + z * sin,
            Center[1] + y,
            Center[2] - x * sin + z * cos,
        ];

        const faces = [
            { outward: [1, 0, 0], corners: [[halfX, -halfY, -halfZ], [halfX, -halfY, halfZ], [halfX, halfY, halfZ], [halfX, halfY, -halfZ]] },
            { outward: [-1, 0, 0], corners: [[-halfX, -halfY, halfZ], [-halfX, -halfY, -halfZ], [-halfX, halfY, -halfZ], [-halfX, halfY, halfZ]] },
            { outward: [0, 1, 0], corners: [[-halfX, halfY, -halfZ], [halfX, halfY, -halfZ], [halfX, halfY, halfZ], [-halfX, halfY, halfZ]] },
            { outward: [0, -1, 0], corners: [[-halfX, -halfY, halfZ], [halfX, -halfY, halfZ], [halfX, -halfY, -halfZ], [-halfX, -halfY, -halfZ]] },
            { outward: [0, 0, 1], corners: [[-halfX, -halfY, halfZ], [-halfX, halfY, halfZ], [halfX, halfY, halfZ], [halfX, -halfY, halfZ]] },
            { outward: [0, 0, -1], corners: [[halfX, -halfY, -halfZ], [halfX, halfY, -halfZ], [-halfX, halfY, -halfZ], [-halfX, -halfY, -halfZ]] },
        ];

        for (const face of faces)
        {
            const corners = face.corners.map(([x, y, z]) => transform(x, y, z));
            const outward = [face.outward[0] * cos + face.outward[2] * sin, face.outward[1], -face.outward[0] * sin + face.outward[2] * cos];
            const shade = 0.86 + 0.14 * Math.abs(outward[0]) + 0.08 * Math.max(0, outward[1]);
            const tinted = [Color[0] * shade, Color[1] * shade, Color[2] * shade];
            this.AddQuad(
                corners,
                tinted,
                [[0, 0], [Size[2] * 0.3, 0], [Size[2] * 0.3, Size[1] * 0.3], [0, Size[1] * 0.3]],
                outward
            );
        }
    }

    AddGrid(Rows, Columns, PointOf, ColorOf, UvOf)
    {
        const base = this.positions.length / 3;
        for (let i = 0; i < Rows; i++)
        {
            for (let j = 0; j < Columns; j++)
            {
                const point = PointOf(i, j);
                const color = ColorOf(i, j);
                const uv = UvOf(i, j);
                this.positions.push(point[0], point[1], point[2]);
                this.normals.push(0, 1, 0);
                this.uvs.push(uv[0], uv[1]);
                this.colors.push(color[0], color[1], color[2]);
            }
        }
        for (let i = 0; i < Rows - 1; i++)
        {
            for (let j = 0; j < Columns - 1; j++)
            {
                const P0 = base + i * Columns + j;
                const P1 = base + (i + 1) * Columns + j;
                const P2 = base + i * Columns + j + 1;
                const P3 = base + (i + 1) * Columns + j + 1;
                this.indices.push(P0, P1, P2, P1, P3, P2);
            }
        }
    }

    Build()
    {
        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.Float32BufferAttribute(this.positions, 3));
        geometry.setAttribute('normal', new THREE.Float32BufferAttribute(this.normals, 3));
        geometry.setAttribute('uv', new THREE.Float32BufferAttribute(this.uvs, 2));
        geometry.setAttribute('color', new THREE.Float32BufferAttribute(this.colors, 3));
        geometry.setIndex(this.indices);
        geometry.computeBoundingSphere();
        return geometry;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       MATERIALS
//------------------------------------------------------------------------------------------------------------------------

export function CreateMaterials(textures)
{
    const concrete = new THREE.MeshStandardMaterial({
        map: textures.concrete,
        vertexColors: true,
        roughness: 0.94,
        metalness: 0.02,
    });

    const paint = new THREE.MeshStandardMaterial({
        map: textures.concrete,
        vertexColors: true,
        roughness: 0.72,
        metalness: 0.0,
        polygonOffset: true,
        polygonOffsetFactor: -1,
        polygonOffsetUnits: -1,
    });

    const steel = new THREE.MeshStandardMaterial({
        vertexColors: true,
        roughness: 0.48,
        metalness: 0.55,
    });

    const water = new THREE.MeshStandardMaterial({
        map: textures.water,
        color: 0xbfe6dd,
        roughness: 0.12,
        metalness: 0.25,
        transparent: true,
        opacity: 0.9,
    });

    const beam = new THREE.MeshBasicMaterial({
        map: textures.beam,
        transparent: true,
        opacity: 0.5,
        blending: THREE.AdditiveBlending,
        depthWrite: false,
        side: THREE.DoubleSide,
        color: 0xffe3b4,
    });

    return { concrete, paint, steel, water, beam };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       HELPERS
//------------------------------------------------------------------------------------------------------------------------

function WorldOf(node, lateral, height)
{
    const cos = Math.cos(node.heading);
    const sin = Math.sin(node.heading);
    return [
        node.x + cos * lateral,
        node.y + height,
        node.z - sin * lateral,
    ];
}

function ChunkNodes(course, chunk)
{
    const nodes = [];
    for (let i = chunk.nodeFrom; i <= chunk.nodeTo; i++)
    {
        const node = course.nodes[i];
        nodes.push({
            ...node,
            kind : chunk.kind,
            half : chunk.half,
            bank : chunk.bank,
        });
    }
    return nodes;
}

function SignMaterial(Title, Subtitle, UseTeal)
{
    const key = `${Title}|${Subtitle}|${UseTeal}`;
    if (!signCache.has(key))
    {
        signCache.set(key, new THREE.MeshStandardMaterial({
            map: SignTexture(Title, Subtitle, UseTeal ? PALETTE.paintTeal : PALETTE.paintRed),
            roughness: 0.82,
            metalness: 0.05,
            side: THREE.DoubleSide,
        }));
    }
    return signCache.get(key);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SECTION SWEEP
//------------------------------------------------------------------------------------------------------------------------

function SweepSection(nodes, builder, ColorOf, UvScale = 0.13)
{
    const outlines = nodes.map((node) => SectionOutline(node));
    const outline = outlines[0];
    const columns = outline.points.length;
    const rows = nodes.length;

    builder.AddGrid(
        rows,
        columns,
        (i, j) => {
            const [lateral, height] = outline.points[j];
            return WorldOf(nodes[i], lateral, height);
        },
        (i, j) => ColorOf(nodes[i], outline.points[j], i, j),
        (i, j) => [outline.points[j][0] * UvScale, nodes[i].s * UvScale]
    );

    return outline;
}

function SectionColor(node, point, i, j)
{
    const lateral = point[0];
    const height = point[1];
    const deck = node.bank * Math.pow(Math.min(1, Math.abs(lateral) / node.half), 2);
    let color;
    if (height < deck + 0.12 && Math.abs(lateral) <= node.half + 0.02)
    {
        color = CONCRETE_DECK;
    }
    else if (height < deck + 0.9)
    {
        color = CONCRETE_WALL;
    }
    else
    {
        color = CONCRETE_SKIRT;
    }

    // Sun-bleach variation along the run and slight grime low in the section.
    const grain = 0.94 + 0.06 * Math.sin(node.s * 0.37 + j * 1.7);
    const grime = height < -1.5 ? 0.82 : 1;
    return [color[0] * grain * grime, color[1] * grain * grime, color[2] * grain * grime];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       PAINT
//------------------------------------------------------------------------------------------------------------------------

function PaintBand(nodes, builder, Centre, Width, Color, Height = 0.055, From = 0, To = null)
{
    const start = Math.max(0, From);
    const end = To === null ? nodes.length - 1 : Math.min(nodes.length - 1, To);
    if (end - start < 1)
    {
        return;
    }

    builder.AddGrid(
        end - start + 1,
        2,
        (i, j) => {
            const node = nodes[start + i];
            const lateral = Centre + (j === 0 ? -Width / 2 : Width / 2);
            const ratio = Math.min(1, Math.abs(lateral) / node.half);
            return WorldOf(node, lateral, node.bank * ratio * ratio + (node.tilt ?? 0) * lateral + Height);
        },
        () => Color,
        (i) => [0, nodes[start + i].s * 0.3]
    );
}

function PaintBars(nodes, builder, FromIndex, ToIndex, ColorA, ColorB)
{
    for (let i = FromIndex; i < ToIndex; i++)
    {
        const node = nodes[i];
        const color = (i % 2 === 0) ? ColorA : ColorB;
        builder.AddGrid(
            2,
            4,
            (row, column) => {
                const target = row === 0 ? node : nodes[i + 1];
                const lateral = -target.half + (2 * target.half * column) / 3;
                const ratio = lateral / target.half;
                return WorldOf(target, lateral, target.bank * ratio * ratio + (target.tilt ?? 0) * lateral + 0.06);
            },
            () => color,
            (row, column) => [column * 0.4, (node.s + row) * 0.2]
        );
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CHUNK BUILD
//------------------------------------------------------------------------------------------------------------------------

export function BuildChunk(course, chunk, materials)
{
    const group = new THREE.Group();
    group.name = `chunk-${chunk.index}-${chunk.kind}`;

    const nodes = ChunkNodes(course, chunk);
    if (nodes.length < 2)
    {
        return group;
    }

    const concrete = new GeometryBuilder();
    const paint = new GeometryBuilder();
    const steel = new GeometryBuilder();
    const water = new GeometryBuilder();

    SweepSection(nodes, concrete, SectionColor);

    const style = SECTION_STYLES[chunk.kind] ?? SECTION_STYLES.deck;
    const half = chunk.half;
    const outer = half + 0.9;
    const deckHeight = (node, lateral) => node.bank * Math.pow(Math.min(1, Math.abs(lateral) / node.half), 2) + (node.tilt ?? 0) * lateral;

    // --- Structural piers ---
    if (style.skirt > 0)
    {
        for (let i = 2; i < nodes.length - 1; i += 12)
        {
            const node = nodes[i];
            for (const side of [-1, 1])
            {
                const lateral = side * (half * 0.72);
                const cos = Math.cos(node.heading);
                const sin = Math.sin(node.heading);
                const x = node.x + cos * lateral;
                const z = node.z - sin * lateral;
                const top = node.y - 2.6;
                const bottom = node.y - 30;
                concrete.AddBox([x, (top + bottom) / 2, z], [2.2, top - bottom, 2.6], node.heading, CONCRETE_SKIRT);
                concrete.AddBox([x, (top + bottom) / 2, z], [3.4, 1.6, 3.8], node.heading, CONCRETE_WALL);
            }
            concrete.AddBox([node.x, node.y - 3.2, node.z], [2 * half * 0.85, 1.5, 2.0], node.heading, CONCRETE_DEEP);
        }
    }

    // --- Edge rails ---
    if (chunk.kind !== 'tunnel' && chunk.kind !== 'chute' && chunk.kind !== 'bridge')
    {
        for (const side of [-1, 1])
        {
            for (let i = 0; i < nodes.length - 1; i++)
            {
                const node = nodes[i];
                const lateral = side * (outer - 0.35);
                const top = deckHeight(node, lateral) + style.curb + style.wall;
                const bottom = top - 0.85;
                const next = nodes[i + 1];
                const corners = [
                    WorldOf(node, lateral - 0.12, bottom),
                    WorldOf(next, lateral - 0.12, bottom),
                    WorldOf(next, lateral - 0.12, top),
                    WorldOf(node, lateral - 0.12, top),
                ];
                steel.AddQuad(
                    corners,
                    STEEL_RUST,
                    [[0, 0], [1, 0], [1, 1], [0, 1]],
                    [-(Math.cos(node.heading)) * side, 0, (Math.sin(node.heading)) * side]
                );
            }
        }
    }

    // --- Paint: a hard red lip line and a cream inset, the way service roads are marked ---
    const line = half - 0.95;
    PaintBand(nodes, paint, -line, 0.5, PAINT_RED, 0.05);
    PaintBand(nodes, paint, line, 0.5, PAINT_RED, 0.05);
    PaintBand(nodes, paint, -(line - 0.75), 0.22, PAINT_CREAM, 0.055);
    PaintBand(nodes, paint, line - 0.75, 0.22, PAINT_CREAM, 0.055);

    if (Math.abs(chunk.curve) < 0.001 && chunk.kind !== 'tunnel')
    {
        for (let i = 0; i < nodes.length - 2; i += 5)
        {
            PaintBand(nodes, paint, 0, 0.3, PAINT_CREAM, 0.05, i, i + 2);
        }
    }

    if (chunk.curve !== 0)
    {
        // Rumble chequers at the elbow entry and a carved apex line through the bank.
        PaintBars(nodes, paint, 1, Math.min(5, nodes.length - 1), PAINT_RED, PAINT_CREAM);
        const apex = -Math.sign(chunk.curve) * (half - 4.2);
        PaintBand(nodes, paint, apex, 0.7, PAINT_RED, 0.05);
    }

    if (chunk.kind === 'kicker')
    {
        const crest = Math.round(nodes.length * 0.64);
        PaintBand(nodes, paint, 0, 2.4, PAINT_TEAL, 0.06, Math.max(1, crest - 1), Math.min(nodes.length - 1, crest + 1));
        PaintBars(nodes, paint, Math.max(1, crest - 3), Math.max(2, crest - 1), PAINT_RED, PAINT_CREAM);
    }

    if (chunk.kind === 'basin')
    {
        PaintBand(nodes, paint, -(half - 2.4), 1.2, PAINT_TEAL, 0.05);
        PaintBand(nodes, paint, half - 2.4, 1.2, PAINT_TEAL, 0.05);
    }

    // --- Kind-specific architecture ---
    switch (chunk.kind)
    {
        case 'tunnel':
            BuildTunnel(nodes, chunk, concrete, materials, group);
            break;

        case 'bridge':
            BuildOverpass(nodes, chunk, concrete, steel, group);
            break;

        case 'basin':
            BuildBasin(nodes, chunk, water);
            break;

        case 'cascade':
            BuildCascadeWater(nodes, chunk, water);
            break;

        case 'chute':
            BuildChute(nodes, chunk, steel, group);
            break;

        case 'lift':
            BuildButtress(nodes, chunk, concrete);
            break;

        default:
            break;
    }

    // Steel gantries: a portal over the channel, useful for reading speed.
    if (['deck', 'bowl', 'lift', 'cascade', 'basin'].includes(chunk.kind) && chunk.index % 3 === 0)
    {
        const node = nodes[Math.floor(nodes.length * 0.55)];
        const frameHeight = 7.4;
        const cos = Math.cos(node.heading);
        const sin = Math.sin(node.heading);
        for (const side of [-1, 1])
        {
            const lateral = side * (half + 0.6);
            const x = node.x + cos * lateral;
            const z = node.z - sin * lateral;
            steel.AddBox([x, node.y + frameHeight * 0.5, z], [0.6, frameHeight, 0.6], node.heading, STEEL_DARK);
            steel.AddBox([x, node.y + 0.5, z], [1.4, 1.0, 1.4], node.heading, STEEL_RUST);
        }
        steel.AddBox([node.x, node.y + frameHeight, node.z], [2 * (half + 0.6), 0.7, 0.8], node.heading + Math.PI / 2, STEEL_RUST);
        steel.AddBox([node.x, node.y + frameHeight - 0.7, node.z], [2 * (half + 0.6) * 0.8, 0.18, 1.6], node.heading + Math.PI / 2, STEEL_DARK);

        const plate = new THREE.Mesh(
            new THREE.PlaneGeometry(6.4, 2.0),
            SignMaterial(chunk.index % 2 === 0 ? 'SLUICE 04' : 'KEEP CLEAR', 'FLOW 12 M3/S', false)
        );
        const offset = (chunk.index % 2 === 0 ? 1 : -1) * (half * 0.42);
        plate.position.set(node.x + cos * offset, node.y + frameHeight - 1.9, node.z - sin * offset);
        plate.rotation.y = node.heading;
        group.add(plate);
    }

    // Maintenance signage on roughly every second chunk.
    if (chunk.index % 2 === 0 && chunk.kind !== 'tunnel')
    {
        const sign = SIGN_BOOK[chunk.index % SIGN_BOOK.length];
        const node = nodes[Math.floor(nodes.length * 0.5)];
        const side = chunk.index % 4 === 0 ? 1 : -1;
        const lateral = side * (half + 0.55);
        const mesh = new THREE.Mesh(new THREE.PlaneGeometry(6.4, 3.2), SignMaterial(sign[0], sign[1], chunk.index % 3 === 0));
        const position = WorldOf(node, lateral, deckHeight(node, lateral) + style.curb + 1.9);
        mesh.position.set(position[0], position[1], position[2]);
        mesh.rotation.y = node.heading + (side === 1 ? -Math.PI / 2 : Math.PI / 2);
        mesh.castShadow = false;
        group.add(mesh);
    }

    const attachments = [
        [concrete, materials.concrete],
        [paint, materials.paint],
        [steel, materials.steel],
        [water, materials.water],
    ];

    for (const [builder, material] of attachments)
    {
        if (builder.empty)
        {
            continue;
        }
        const mesh = new THREE.Mesh(builder.Build(), material);
        mesh.castShadow = material === materials.concrete;
        mesh.receiveShadow = true;
        group.add(mesh);
    }

    return group;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       TUNNEL
//------------------------------------------------------------------------------------------------------------------------

function BuildTunnel(nodes, chunk, concrete, materials, group)
{
    const style = SECTION_STYLES.tunnel;
    const outer = chunk.half + 1.6;
    const roofHeight = style.wall + 1.2;

    // Ribs and a slit roof: gaps between slabs let the low sun streak the interior.
    for (let i = 3; i < nodes.length - 3; i += 10)
    {
        const node = nodes[i];
        const lateral = chunk.half - 0.22;
        for (const side of [-1, 1])
        {
            const position = WorldOf(node, side * lateral, style.wall * 0.5);
            concrete.AddBox([position[0], position[1], position[2]], [0.9, style.wall + 1.2, 0.9], node.heading, CONCRETE_DECK);
        }
        // Transverse lintel across the crown.
        for (let span = 0; span < 5; span++)
        {
            const lateralA = -outer + (2 * outer * span) / 5;
            const point = WorldOf(node, lateralA, roofHeight);
            concrete.AddBox([point[0], point[1], point[2]], [2 * outer / 5, 0.75, 1.0], node.heading, CONCRETE_DEEP);
        }
    }

    // Continuous roof slabs with light slots between them.
    for (let i = 1; i < nodes.length - 6; i += 10)
    {
        const start = i;
        const end = Math.min(i + 7, nodes.length - 1);
        const slab = new GeometryBuilder();
        slab.AddGrid(
            end - start + 1,
            3,
            (row, column) => {
                const node = nodes[start + row];
                const lateral = -outer + (2 * outer * column) / 2;
                return WorldOf(node, lateral, roofHeight + 0.4 + Math.cos((column / 2) * Math.PI) * 0.3);
            },
            () => CONCRETE_WALL,
            (row, column) => [column * 0.6, (start + row) * 0.2]
        );
        const mesh = new THREE.Mesh(slab.Build(), materials.concrete);
        mesh.castShadow = false;
        mesh.receiveShadow = true;
        group.add(mesh);

        // Sun shaft through the gap.
        const shaftNode = nodes[Math.min(start + 4, nodes.length - 1)];
        const beam = new THREE.Mesh(
            new THREE.PlaneGeometry(2.6, style.wall * 2.4),
            materials.beam
        );
        beam.position.set(shaftNode.x, shaftNode.y + style.wall * 0.55, shaftNode.z);
        beam.rotation.y = shaftNode.heading + Math.PI / 2;
        beam.rotation.z = 0.42;
        group.add(beam);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       OVERPASS
//------------------------------------------------------------------------------------------------------------------------

function BuildOverpass(nodes, chunk, concrete, steel, group)
{
    const middle = nodes[Math.floor(nodes.length / 2)];
    const spanWidth = chunk.half * 2 + 32;
    const deckY = middle.y + 10.5;
    const cos = Math.cos(middle.heading);
    const sin = Math.sin(middle.heading);

    // Slab and parapet, laid perpendicular to the route.
    const centre = [
        middle.x + cos * 0,
        deckY,
        middle.z - sin * 0,
    ];
    concrete.AddBox([centre[0], centre[1], centre[2]], [spanWidth, 1.2, 9.5], middle.heading + Math.PI / 2, CONCRETE_WALL);
    concrete.AddBox([centre[0], centre[1] + 1.0, centre[2]], [spanWidth, 1.4, 0.6], middle.heading + Math.PI / 2, CONCRETE_DECK);

    // Piers clear of the ride surface.
    for (const side of [-1, 1])
    {
        const lateral = side * (chunk.half + 6.5);
        const x = middle.x + cos * lateral;
        const z = middle.z - sin * lateral;
        const top = deckY - 0.6;
        const bottom = middle.y - 34;
        concrete.AddBox([x, (top + bottom) / 2, z], [3.4, top - bottom, 4.2], middle.heading, CONCRETE_SKIRT);
        concrete.AddBox([x, top - 1.2, z], [5.2, 2.0, 6.0], middle.heading, CONCRETE_WALL);
    }

    // Steel cross-bracing overhead.
    for (let i = -2; i <= 2; i++)
    {
        const lateral = (spanWidth / 5) * i;
        steel.AddBox([centre[0] + cos * lateral, deckY - 0.9, centre[2] - sin * lateral], [0.5, 0.55, 9.2], middle.heading + Math.PI / 2, STEEL_RUST);
    }

    // Banner hanging from the span.
    const bannerSide = chunk.index % 4 === 0 ? 1 : -1;
    const bannerLateral = bannerSide * (chunk.half * 0.62);
    const banner = new THREE.Mesh(
        new THREE.PlaneGeometry(9.5, 3.2),
        SignMaterial('SPILLWAY', 'SECTOR 09 · NO STOPPING', true)
    );
    banner.position.set(middle.x + cos * bannerLateral, deckY - 3.1, middle.z - sin * bannerLateral);
    banner.rotation.y = middle.heading + Math.PI / 2;
    banner.material.side = THREE.DoubleSide;
    group.add(banner);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       WATER FEATURES
//------------------------------------------------------------------------------------------------------------------------

function BuildBasin(nodes, chunk, water)
{
    const start = 2;
    const end = nodes.length - 3;
    if (end - start < 2)
    {
        return;
    }

    // Sheet flow: a shallow river rides the crown of the dish, so the rider skims water rather than passing over it.
    water.AddGrid(
        end - start + 1,
        7,
        (row, column) => {
            const node = nodes[start + row];
            const lateral = ((column / 6) - 0.5) * 2 * chunk.half * 0.62;
            const ratio = lateral / chunk.half;
            return WorldOf(node, lateral, node.bank * ratio * ratio + 0.14);
        },
        () => [0.70, 0.98, 0.96],
        (row, column) => [column * 0.5, nodes[start + row].s * 0.22]
    );
}

function BuildCascadeWater(nodes, chunk, water)
{
    for (let i = 1; i < nodes.length - 1; i++)
    {
        const drop = nodes[i - 1].y - nodes[i].y;
        if (drop < 0.6)
        {
            continue;
        }
        const node = nodes[i];
        const left = WorldOf(node, -chunk.half * 0.86, 0.35);
        const right = WorldOf(node, chunk.half * 0.86, 0.35);
        const leftLow = [left[0], left[1] - drop - 0.2, left[2]];
        const rightLow = [right[0], right[1] - drop - 0.2, right[2]];
        water.AddQuad(
            [left, right, rightLow, leftLow],
            [0.8, 0.99, 0.97],
            [[0, 0], [2, 0], [2, 1], [0, 1]],
            [0, 0, 1]
        );
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CHUTE FITTINGS
//------------------------------------------------------------------------------------------------------------------------

function BuildChute(nodes, chunk, steel, group)
{
    for (let i = 4; i < nodes.length - 4; i += 14)
    {
        const node = nodes[i];
        for (const side of [-1, 1])
        {
            const lateral = side * (chunk.half + 0.55);
            for (let rung = 0; rung < 7; rung++)
            {
                const position = WorldOf(node, lateral, 0.9 + rung * 0.8);
                steel.AddBox([position[0], position[1], position[2]], [0.9, 0.12, 0.12], node.heading, STEEL_DARK);
            }
            const rail = WorldOf(node, lateral, 3.6);
            steel.AddBox([rail[0], rail[1], rail[2]], [0.12, 6.4, 0.12], node.heading, STEEL_DARK);
        }
    }

    const sign = new THREE.Mesh(
        new THREE.PlaneGeometry(7.2, 3.6),
        SignMaterial('KEEP CLEAR', 'HIGH VELOCITY CHUTE', false)
    );
    const node = nodes[Math.floor(nodes.length * 0.4)];
    const lateral = chunk.half + 0.4;
    const position = WorldOf(node, lateral, 4.2);
    sign.position.set(position[0], position[1], position[2]);
    sign.rotation.y = node.heading - Math.PI / 2;
    group.add(sign);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       BUTTRESS
//------------------------------------------------------------------------------------------------------------------------

function BuildButtress(nodes, chunk, concrete)
{
    for (let i = 4; i < nodes.length - 4; i += 18)
    {
        const node = nodes[i];
        for (const side of [-1, 1])
        {
            const lateral = side * (chunk.half + 2.6);
            const cos = Math.cos(node.heading);
            const sin = Math.sin(node.heading);
            const x = node.x + cos * lateral;
            const z = node.z - sin * lateral;
            concrete.AddBox([x, node.y - 7.5, z], [2.4, 15, 9.0], node.heading, CONCRETE_WALL);
            concrete.AddBox([x, node.y - 1.2, z], [3.2, 2.4, 11.0], node.heading, CONCRETE_DECK);
        }
    }
}
