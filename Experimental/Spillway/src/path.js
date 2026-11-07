/*====================================================================================================================================
                                                              PATH.JS
====================================================================================================================================*/
// 🧩 Procedural aqueduct course: chunk sequence, arc-length frames, cross-section heights. No rendering imports.

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

export const SAMPLE_STEP   = 2.0;    // [m] - Distance between stored centreline nodes
export const RIDE_GRAVITY  = 26.0;   // [m/s²] - Deliberately heavier than Earth for snappy arcade flight
export const HALF_PI       = Math.PI * 0.5;
export const SECONDS_AHEAD = 900;    // [m] - Course is generated this far beyond the rider

// Section vocabulary shared by the physics and the mesh builder.
export const SECTION_STYLES = {
    deck    : { half:  9.6, bank: 0.60, curb: 0.50, wall: 1.9, roof: false, skirt: 3.4, water: false },
    pump    : { half:  8.8, bank: 1.45, curb: 0.40, wall: 1.4, roof: false, skirt: 3.0, water: false },
    kicker  : { half:  8.6, bank: 0.25, curb: 0.34, wall: 1.1, roof: false, skirt: 3.0, water: false },
    bowl    : { half: 12.4, bank: 4.20, curb: 0.20, wall: 1.0, roof: false, skirt: 4.0, water: false },
    tunnel  : { half:  7.8, bank: 0.20, curb: 0.28, wall: 7.0, roof: true , skirt: 0.0, water: false },
    bridge  : { half:  9.4, bank: 0.50, curb: 0.44, wall: 1.7, roof: false, skirt: 3.2, water: false },
    chute   : { half:  5.8, bank: 0.15, curb: 0.24, wall: 6.0, roof: false, skirt: 3.4, water: false },
    cascade : { half: 10.2, bank: 0.95, curb: 0.44, wall: 1.8, roof: false, skirt: 3.0, water: false },
    basin   : { half: 12.8, bank: 2.70, curb: 0.34, wall: 1.3, roof: false, skirt: 3.6, water: true  },
    lift    : { half:  8.9, bank: 0.45, curb: 0.38, wall: 1.5, roof: false, skirt: 3.0, water: false },
    turn    : { half: 11.6, bank: 3.60, curb: 0.55, wall: 2.4, roof: false, skirt: 4.2, water: false },
};

const RIDEABLE_KINDS = ['deck', 'pump', 'bowl', 'kicker', 'bridge', 'chute', 'cascade', 'basin', 'tunnel'];

//------------------------------------------------------------------------------------------------------------------------
//                                                       RANDOMNESS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Deterministic 32-bit generator so a seed reproduces an identical course for testing and replays.
export function Mulberry32(Seed)
{
    let state = Seed >>> 0;
    return function ()
    {
        state = (state + 0x6D2B79F5) >>> 0;
        let u = state;
        u = Math.imul(u ^ (u >>> 15), u | 1);
        u ^= u + Math.imul(u ^ (u >>> 7), u | 61);
        return ((u ^ (u >>> 14)) >>> 0) / 4294967296;
    };
}

function Clamp(Value, Low, High)
{
    return Value < Low ? Low : (Value > High ? High : Value);
}

function Smoothstep(Edge0, Edge1, Value)
{
    const u = Clamp((Value - Edge0) / (Edge1 - Edge0), 0, 1);
    return u * u * (3 - 2 * u);
}

// Piecewise linear lookup over [t01 (0-1), offset] keypoints.
function KeyProfile(Keys, u)
{
    for (let i = 1; i < Keys.length; i++)
    {
        if (u <= Keys[i][0])
        {
            const [t0, v0] = Keys[i - 1];
            const [t1, v1] = Keys[i];
            const span = Math.max(1e-6, t1 - t0);
            const local = Clamp((u - t0) / span, 0, 1);
            return v0 + (v1 - v0) * local;
        }
    }
    return Keys[Keys.length - 1][1];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       COURSE PATH
//------------------------------------------------------------------------------------------------------------------------

export class CoursePath
{
    constructor(Seed = 20261107)
    {
        this.random   = Mulberry32(Seed);
        this.seed     = Seed;
        this.nodes    = [];
        this.chunks   = [];
        this.pose     = { x: 0, z: 0, y: 0, heading: 0 };   // [m], [m], [m], [rad]
        this.cursor   = 0;                                  // [m] - Arc length at the far end of generated course
        this.lastKind = [];
        this.sinceTurn = 0;
    }

    //-------------------------------------------------------- Public queries --------------------------------------------------------

    get endDistance()
    {
        return this.cursor;
    }

    ExtendTo(TargetDistance)
    {
        let guard = 0;
        while (this.cursor < TargetDistance && guard++ < 4096)
        {
            this.AppendChunk();
        }
    }

    // 📝 Fast lookup: nodes are uniformly spaced, so sampling is arithmetic plus one interpolation.
    Sample(Distance)
    {
        const nodes = this.nodes;
        const i = Clamp(Distance / SAMPLE_STEP, 0, nodes.length - 1.0001);
        const low = Math.floor(i);
        const u = i - low;
        const A = nodes[low];
        const B = nodes[Math.min(low + 1, nodes.length - 1)];

        return {
            x       : A.x + (B.x - A.x) * u,
            z       : A.z + (B.z - A.z) * u,
            y       : A.y + (B.y - A.y) * u,
            heading : A.heading + (B.heading - A.heading) * u,
            half    : A.half + (B.half - A.half) * u,
            bank    : A.bank + (B.bank - A.bank) * u,
            tilt    : A.tilt,
            curv    : A.curv,
            kind    : A.kind,
            roof    : A.roof,
            chunk   : A.chunk,
            slope   : (B.y - A.y) / SAMPLE_STEP,
        };
    }

    // World-space surface height for a lateral offset; also used by the mesh builder.
    GroundHeight(Sample, Lateral)
    {
        const ratio = Clamp(Lateral / Math.max(0.001, Sample.half), -1, 1);
        return Sample.y + Sample.bank * ratio * ratio + (Sample.tilt ?? 0) * Lateral;
    }

    // Curvature sampled slightly ahead, for the corner callout.
    CurvatureAhead(Distance, Reach = 34)
    {
        return this.Sample(Distance + Reach).curv;
    }

    NodeOf(Distance)
    {
        const i = Clamp(Math.round(Distance / SAMPLE_STEP), 0, this.nodes.length - 1);
        return this.nodes[i];
    }

    //-------------------------------------------------------- Chunk selection -------------------------------------------------------

    ChooseKind()
    {
        if (this.chunks.length === 0)
        {
            return 'deck';
        }

        if (this.sinceTurn >= 2 + Math.floor(this.random() * 2))
        {
            return 'turn';
        }

        if (this.pose.y < -7)
        {
            return 'lift';
        }

        const pool = RIDEABLE_KINDS.filter((kind) => !this.lastKind.includes(kind));
        return pool[Math.floor(this.random() * pool.length)];
    }

    AppendChunk()
    {
        const kind = this.ChooseKind();
        const isTurn = kind === 'turn';

        if (isTurn)
        {
            this.appendTurn();
            this.sinceTurn = 0;
        }
        else
        {
            this.appendStraight(kind);
            this.sinceTurn += 1;
        }

        this.lastKind.push(kind);
        if (this.lastKind.length > 2)
        {
            this.lastKind.shift();
        }
    }

    //-------------------------------------------------------- Section assembly ------------------------------------------------------

    AppendNodes(Total, SampleFrame, Profile)
    {
        const count = Math.max(2, Math.round(Total / SAMPLE_STEP));
        const style = SECTION_STYLES[Profile.kind] ?? SECTION_STYLES.deck;
        const chunkIndex = this.chunks.length;
        const boundaryIndex = this.nodes.length === 0 ? 0 : this.nodes.length - 1;
        const first = this.nodes.length === 0 ? 0 : 1;

        for (let i = first; i <= count; i++)
        {
            const t = i * SAMPLE_STEP;                       // [m] - Local distance along the chunk
            const u = t / (count * SAMPLE_STEP);             // [0-1] - Normalised chunk position
            const frame = SampleFrame(t, u);

            this.nodes.push({
                s       : this.cursor + t,
                x       : frame.x,
                z       : frame.z,
                y       : this.pose.y + Profile.elevation(t, u, Total),
                heading : frame.heading,
                curv    : frame.curv,
                half    : Profile.half ?? style.half,
                bank    : Profile.bank ?? style.bank,
                tilt    : Profile.tilt ?? 0,
                kind    : Profile.kind,
                roof    : style.roof,
                chunk   : chunkIndex,
                t       : u,
                local   : t,
            });
        }

        // Advance the carried pose to the far end of this chunk.
        const end = SampleFrame(count * SAMPLE_STEP, 1);
        this.pose.x = end.x;
        this.pose.z = end.z;
        const last = this.nodes[this.nodes.length - 1];
        this.pose.y = last.y;
        this.pose.heading = end.heading;
        this.cursor = last.s;

        this.chunks.push({
            index    : chunkIndex,
            kind     : Profile.kind,
            s0       : this.nodes[boundaryIndex].s,
            s1       : last.s,
            length   : count * SAMPLE_STEP,
            style,
            half     : Profile.half ?? style.half,
            bank     : Profile.bank ?? style.bank,
            curve    : Profile.curve ?? 0,
            radius   : Profile.radius ?? 0,
            nodeFrom : boundaryIndex,
            nodeTo   : this.nodes.length - 1,
            seed     : Math.floor(this.random() * 65535),
        });

        return this.chunks[this.chunks.length - 1];
    }

    //-------------------------------------------------------- Straight chunks ------------------------------------------------------

    appendStraight(kind)
    {
        const random = this.random;
        const base = SECTION_STYLES[kind] ?? SECTION_STYLES.deck;

        const recipe = {
            deck    : { length: 72 + random() * 56, elevation: () => 0 },
            pump    : { length: 88 + random() * 36, elevation: pumpProfile },
            bowl    : { length: 66 + random() * 40, elevation: bowlProfile },
            kicker  : { length: 46 + random() * 16, elevation: kickerProfile },
            tunnel  : { length: 74 + random() * 34, elevation: () => 0 },
            bridge  : { length: 56 + random() * 30, elevation: () => 0 },
            chute   : { length: 62 + random() * 30, elevation: (t) => -0.030 * t },
            cascade : { length: 96, elevation: cascadeProfile },
            basin   : { length: 70 + random() * 24, elevation: basinProfile },
            lift    : { length: 96, elevation: (t) => 0.045 * t },
        }[kind];

        const length = Math.round(recipe.length / SAMPLE_STEP) * SAMPLE_STEP;
        const startX = this.pose.x;
        const startZ = this.pose.z;
        const heading = this.pose.heading;
        const drift = (kind === 'deck' || kind === 'bridge') ? (random() - 0.5) * 0.008 : 0;

        this.AppendNodes(
            length,
            (t) => ({
                x       : startX + Math.sin(heading) * t,
                z       : startZ + Math.cos(heading) * t,
                heading : heading + drift * t,
                curv    : 0,
            }),
            {
                kind,
                elevation : recipe.elevation,
                half      : base.half,
                bank      : base.bank,
            }
        );
    }

    //-------------------------------------------------------- 90° banked turns -----------------------------------------------------

    appendTurn()
    {
        const random = this.random;
        const dir = random() < 0.5 ? 1 : -1;                          // [+] - Right turn, [-] - Left turn
        const radius = 52 + random() * 30;                          // [m]
        const raw = radius * HALF_PI;
        const length = Math.round(raw / SAMPLE_STEP) * SAMPLE_STEP;
        const sweep = (length / radius) * dir;
        const heading0 = this.pose.heading;
        const startX = this.pose.x;
        const startZ = this.pose.z;

        const centreX = startX + Math.cos(heading0) * radius * dir;
        const centreZ = startZ - Math.sin(heading0) * radius * dir;

        this.AppendNodes(
            length,
            (t) => {
                const angle = (t / radius) * dir;
                const heading = heading0 + angle;
                return {
                    x       : centreX - Math.cos(heading) * radius * dir,
                    z       : centreZ + Math.sin(heading) * radius * dir,
                    heading,
                    curv    : dir / radius,
                };
            },
            {
                kind      : 'turn',
                elevation : () => 0,
                half      : 10.6 + random() * 1.8,
                bank      : 1.2 + random() * 0.7,
                tilt      : -dir * (0.20 + random() * 0.05),
                curve     : dir,
                radius,
            }
        );

        // Store turn metadata used by the renderer and HUD prompts.
        const chunk = this.chunks[this.chunks.length - 1];
        chunk.turnSign = dir;
        chunk.radius = radius;
        chunk.sweep = sweep;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ELEVATION PROFILES
//------------------------------------------------------------------------------------------------------------------------

// Pump track: repeated rollers that throw the rider off each crest.
function pumpProfile(t)
{
    return 1.05 * Math.sin((t / 15) * Math.PI * 2) * Smoothstep(0, 10, t);
}

// Shallow concrete bowl with a soft dish.
function bowlProfile(t, u, total)
{
    const rise = 1.6 * Math.sin((t / 26) * Math.PI * 2);
    const dive = 0.9 * Math.sin(u * Math.PI);
    return rise - dive;
}

// Kicker: ramp up, hard lip, steep backside. A fast rider launches; a slow one falls.
function kickerProfile(t, u, total)
{
    return KeyProfile([[0, 0], [0.58, 2.9], [0.70, 3.05], [0.86, 0.9], [1, 0]], u);
}

// Spillway stair: four ogee steps down, then a long climbing transition back to the top.
function cascadeProfile(t, u, total)
{
    if (u < 0.52)
    {
        const step = t / 12.5;
        return -1.35 * step;
    }
    const down = -1.35 * (0.52 * total / 12.5);
    const climb = Smoothstep(0.52, 1, u);
    return down * (1 - climb);
}

// Basin: long shallow dish that skims a water sheet.
function basinProfile(t, u, total)
{
    return -0.55 * Math.sin(u * Math.PI);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SECTION OUTLINE
//------------------------------------------------------------------------------------------------------------------------

// Section control points from the left wall to the right wall, as [lateral (m), height above centreline (m)].
// The mesh builder sweeps this outline along the chunk; the ride keeps the deck portion only.
export function SectionOutline(node)
{
    const style = SECTION_STYLES[node.kind] ?? SECTION_STYLES.deck;
    const half = node.half;
    const bank = node.bank;
    const points = [];
    const deckSteps = node.kind === 'bowl' || node.kind === 'basin' ? 7 : 4;

    const deckHeight = (lateral) => {
        const ratio = Clamp(lateral / half, -1, 1);
        return bank * ratio * ratio + (node.tilt ?? 0) * lateral;
    };

    if (node.kind === 'tunnel')
    {
        const wall = style.wall;
        points.push([-half - 1.6, wall + 1.2]);
        points.push([-half - 0.5, wall]);
        points.push([-half, 0.15]);
        for (let i = 1; i < 6; i++)
        {
            const lateral = -half + (2 * half * i) / 6;
            points.push([lateral, deckHeight(lateral)]);
        }
        points.push([half, 0.15]);
        points.push([half + 0.5, wall]);
        points.push([half + 1.6, wall + 1.2]);
        return { points, roof: true, wall };
    }

    if (node.kind === 'chute')
    {
        const wall = style.wall;
        points.push([-half - 1.1, wall]);
        points.push([-half, 0.1]);
        for (let i = 1; i < 4; i++)
        {
            const lateral = -half + (2 * half * i) / 4;
            points.push([lateral, deckHeight(lateral)]);
        }
        points.push([half, 0.1]);
        points.push([half + 1.1, wall]);
        return { points, roof: false, wall };
    }

    // Standard open aqueduct: outer skirt, curb, banked deck, curb, skirt.
    const curb = style.curb;
    const wall = style.wall;
    const skirt = style.skirt;
    const outer = half + 0.9;

    points.push([-outer, deckHeight(-half) + curb - skirt]);
    points.push([-outer, deckHeight(-half) + curb + wall]);
    points.push([-half - 0.75, deckHeight(-half) + curb + wall]);
    points.push([-half - 0.4, deckHeight(-half) + curb]);
    points.push([-half, deckHeight(-half) + 0.02]);

    for (let i = 1; i < deckSteps; i++)
    {
        const lateral = -half + (2 * half * i) / deckSteps;
        points.push([lateral, deckHeight(lateral)]);
    }

    points.push([half, deckHeight(half) + 0.02]);
    points.push([half + 0.4, deckHeight(half) + curb]);
    points.push([half + 0.75, deckHeight(half) + curb + wall]);
    points.push([outer, deckHeight(half) + curb + wall]);
    points.push([outer, deckHeight(half) + curb - skirt]);

    return { points, roof: false, wall };
}
