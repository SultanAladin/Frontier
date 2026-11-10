// Road Editor — procedural NFS-scale map generator.
// 6km × 6km · city core + ring motorway + countryside + mountain pass +
// offroad trails + river + lake + railway. Every road gets a distinct
// type/variant assignment — no uniform repeating look.

import { RoadGraph, resetIds } from './graph.js';
import { ROAD_TYPES, roadTypeById } from './roadTypes.js';
import { mulberry32, segIntersect } from './math.js';

const MAP = 3000; // half-extent (m)

export function generateCity(seed = 20261012, opts = {}) {
  resetIds();
  const rng = mulberry32(seed);
  const g = new RoadGraph();
  g.railGear = [];
  const used = new Map(); // typeId -> count
  const use = (id) => used.set(id, (used.get(id) || 0) + 1);
  const pickBag = makeBag(rng);

  const Y = { city: 0, motor: 5, motorHigh: 9, ring: 4 };

  // ---- ring motorway (4 arc edges) ----
  const R = 2200, C = 900; // half-size / corner radius
  const ringPts = {
    N: [[-R + C, -R], [-R + C + 800, -R], [R - C - 800, -R], [R - C, -R]],
    E: [[R, -R + C], [R, -600], [R, 600], [R, R - C]],
    S: [[R - C, R], [600, R], [-600, R], [-R + C, R]],
    W: [[-R, R - C], [-R, 600], [-R, -600], [-R, -R + C]],
    NE: [[R - C, -R], [R - C + C * 0.55, -R + C * 0.12], [R - C * 0.12, -R + C - C * 0.45 + 90], [R, -R + C]],
    SE: [[R, R - C], [R - C * 0.1, R - C + C * 0.45], [R - C + C * 0.45 - 90, R - C * 0.1 + 40], [R - C, R]],
    SW: [[-R + C, R], [-R + C - C * 0.45 + 90, R - C * 0.1 + 40], [-R + C * 0.1 - 40, R - C + C * 0.45], [-R, R - C]],
    NW: [[-R, -R + C], [-R + C * 0.1 - 40, -R + C - C * 0.45], [-R + C - C * 0.45 + 90, -R + C * 0.1 - 40], [-R + C, -R]],
  };
  for (const [k, pts] of Object.entries(ringPts)) {
    addRoad(g, 'motorway-4', pts.map(([x, z]) => ({ x, y: Y.ring, z })), { name: `Ring ${k}` });
    use('motorway-4');
  }

  // ---- crossing motorways ----
  addRoad(g, 'motorway-6', [{ x: -MAP, y: Y.motor, z: -1400 }, { x: -800, y: Y.motor, z: -1400 }, { x: 800, y: Y.motor, z: -1400 }, { x: MAP, y: Y.motor, z: -1400 }], { name: 'Motorway E-W' });
  use('motorway-6');
  addRoad(g, 'motorway-4', [{ x: 1400, y: Y.motorHigh, z: -MAP }, { x: 1400, y: Y.motorHigh, z: 0 }, { x: 1400, y: Y.motorHigh, z: MAP }], { name: 'Motorway N-S' });
  use('motorway-4');

  // ---- city core grid ----
  const CORE = 1200, AX = [-900, -450, 0, 450, 900];
  const artV = ['arterial-4', 'arterial-6', 'boulevard-trees', 'arterial-4', 'arterial-2'];
  AX.forEach((x, i) => {
    // extend northern arterials to meet E-W motorway (interchange)
    const north = i % 2 === 0 ? -1650 : -CORE;
    addRoad(g, artV[i], [{ x, y: 0, z: north }, { x, y: 0, z: 0 }, { x, y: 0, z: CORE }], { name: `Ave ${x}` });
    use(artV[i]);
  });
  const artH = ['arterial-4', 'tram-street', 'arterial-6', 'arterial-2', 'collector-2'];
  const AH = [-900, -450, 0, 450, 900];
  AH.forEach((z, i) => {
    addRoad(g, artH[i], [{ x: -CORE, y: 0, z }, { x: 0, y: 0, z }, { x: CORE, y: 0, z }], { name: `St ${z}` });
    use(artH[i]);
  });
  // busway spine
  addRoad(g, 'busway-2', [{ x: -180, y: 0, z: -CORE }, { x: -180, y: 0, z: CORE }], { name: 'Busway' });
  use('busway-2');

  // ---- collectors (offset grid, residential bands) ----
  const CX = [-1125, -675, -225, 225, 675, 1125];
  for (const x of CX) {
    for (const [z0, z1] of [[-CORE, -520], [520, CORE]]) {
      addRoad(g, pickBag(['collector-2', 'collector-2', 'local-2']), [{ x, y: 0, z: z0 }, { x, y: 0, z: (z0 + z1) / 2 }, { x, y: 0, z: z1 }], {});
      use('collector-2');
    }
  }
  for (const z of CX) {
    for (const [x0, x1] of [[-CORE, -520], [520, CORE]]) {
      addRoad(g, pickBag(['collector-2', 'local-2', 'service-road']), [{ x: x0, y: 0, z }, { x: (x0 + x1) / 2, y: 0, z }, { x: x1, y: 0, z }], {});
    }
  }
  // ---- locals / one-ways / alleys inside blocks ----
  const urbanFill = ['local-2', 'local-2', 'oneway-3', 'oneway-2-contra', 'alley-1', 'service-road', 'brick-street'];
  for (let bx = -2; bx <= 1; bx++) {
    for (let bz = -2; bz <= 1; bz++) {
      const x0 = AX[bx + 2] + 60, x1 = AX[bx + 3] - 60;
      const z0 = AH[bz + 2] + 60, z1 = AH[bz + 3] - 60;
      if (rng() < 0.85) {
        // Block centers sit exactly on collector lines — repel the fill
        // midline so parallel ribbons never run within ~16m of each other.
        let zm = (z0 + z1) / 2 + (rng() - 0.5) * 60;
        for (const c of CX) {
          const d = zm - c;
          if (Math.abs(d) < 16) zm = c + (16 + rng() * 10) * (d >= 0 ? 1 : -1);
        }
        const t = pickBag(urbanFill);
        addRoad(g, t, [{ x: x0, y: 0, z: zm }, { x: (x0 + x1) / 2, y: 0, z: zm }, { x: x1, y: 0, z: zm }], {});
        use(t);
      }
      if (rng() < 0.6) {
        let xm = (x0 + x1) / 2 + (rng() - 0.5) * 60;
        for (const c of CX) {
          const d = xm - c;
          if (Math.abs(d) < 16) xm = c + (16 + rng() * 10) * (d >= 0 ? 1 : -1);
        }
        const t = pickBag(['local-2', 'alley-1', 'oneway-2-contra']);
        addRoad(g, t, [{ x: xm, y: 0, z: z0 }, { x: xm, y: 0, z: (z0 + z1) / 2 }, { x: xm, y: 0, z: z1 }], {});
        use(t);
      }
    }
  }
  // ---- historic quarter (cobble cluster near center-west) ----
  const hist = [[-380, -260], [-260, -260], [-380, -140], [-260, -140]];
  addRoad(g, 'historic-cobble', [{ x: -420, y: 0, z: -260 }, { x: -220, y: 0, z: -260 }], {}); use('historic-cobble');
  addRoad(g, 'historic-cobble', [{ x: -420, y: 0, z: -140 }, { x: -220, y: 0, z: -140 }], {}); use('historic-cobble');
  addRoad(g, 'historic-cobble', [{ x: -320, y: 0, z: -320 }, { x: -320, y: 0, z: -80 }], {}); use('historic-cobble');
  void hist;

  // ---- river ----
  const river = [
    { x: -MAP, y: 0, z: -2600 }, { x: -1900, y: 0, z: -1900 }, { x: -1000, y: 0, z: -1000 },
    { x: -350, y: 0, z: -350 }, { x: 300, y: 0, z: 350 }, { x: 1100, y: 0, z: 1200 },
    { x: 2000, y: 0, z: 1950 }, { x: MAP, y: 0, z: 2350 },
  ];
  g.water.push({ points: river, width: 26 });
  // promenade + cycle track along river through core
  const prom = river.filter((p) => p.x > -800 && p.x < 900).map((p) => ({ x: p.x + 30, y: 0, z: p.z + 18 }));
  if (prom.length >= 2) { addRoad(g, 'promenade', prom, { name: 'Riverside promenade' }); use('promenade'); }
  const cyc = river.filter((p) => p.x > -900 && p.x < 1100).map((p) => ({ x: p.x - 34, y: 0, z: p.z - 14 }));
  if (cyc.length >= 2) { addRoad(g, 'cycle-track', cyc, { name: 'River cycleway' }); use('cycle-track'); }

  // ---- railway loop ----
  const rail = [
    { x: -1600, y: 0, z: -1600 }, { x: 0, y: 0, z: -1600 }, { x: 1600, y: 0, z: -1600 },
    { x: 1600, y: 0, z: 0 }, { x: 1600, y: 0, z: 1600 }, { x: 0, y: 0, z: 1600 },
    { x: -1600, y: 0, z: 1600 }, { x: -1600, y: 0, z: 0 }, { x: -1600, y: 0, z: -1600 },
  ];
  g.rails.push({ points: rail });

  // ---- rural highways (ring → map edge) ----
  const rurals = [
    [[-400, -1200], [-500, -2000], [-620, -MAP]],
    [[400, 1200], [500, 2000], [560, MAP]],
    [[-1200, 300], [-2000, 450], [-MAP, 520]],
    [[1900, -300], [2400, -500], [MAP, -620]],
    [[-1200, -1200], [-1900, -1900], [-2500, -2500]],
    [[1200, 1200], [1900, 1900], [2500, 2500]],
  ];
  rurals.forEach((pts, i) => {
    addRoad(g, i % 3 === 2 ? 'rural-lane' : 'rural-highway', pts.map(([x, z]) => ({ x, y: 0, z })), { name: `Rural ${i}` });
    use('rural-highway');
  });
  // gravel + dirt + farm + forest web (SE + E countryside)
  const offroads = [
    ['gravel-road', [[560, 1500], [1100, 1700], [1700, 2100], [2300, 2500]]],
    ['gravel-road', [[-2000, 450], [-2300, 1100], [-2500, 1700]]],
    ['dirt-track', [[1700, 2100], [2000, 1600], [2500, 1400], [2900, 1500]]],
    ['dirt-track', [[1100, 1700], [1400, 2300], [1800, 2700]]],
    ['farm-track', [[-2300, 1100], [-1800, 1400], [-1500, 1900]]],
    ['farm-track', [[500, 2000], [-200, 2300], [-900, 2500]]],
    ['forest-trail', [[2000, 1600], [2300, 1900], [2600, 2200]]],
    ['forest-trail', [[1400, 2300], [1100, 2600], [900, 2900]]],
    ['trail-gravel', [[-1500, 1900], [-1200, 2300], [-900, 2500]]],
    ['footpath', [[-900, 2500], [-300, 2700], [300, 2900]]],
  ];
  for (const [t, pts] of offroads) {
    addRoad(g, t, pts.map(([x, z]) => ({ x, y: 0, z })), {});
    use(t);
  }
  // ---- mountain pass (NW): switchbacks 0 → 46 m ----
  const pass = [];
  const PX = -2150, PZ = -2150;
  for (let i = 0; i <= 10; i++) {
    const t = i / 10;
    pass.push({ x: PX + (i % 2 === 0 ? -260 : 260) * (1 - t * 0.4), y: t * 46, z: PZ + t * 900 - 450 });
  }
  addRoad(g, 'mountain-pass', [{ x: -1200, y: 0, z: -1900 }, ...pass], { name: 'Mountain pass' });
  use('mountain-pass');
  addRoad(g, 'dirt-track', [{ x: pass[10].x, y: 46, z: pass[10].z }, { x: pass[10].x + 300, y: 44, z: pass[10].z + 200 }], {});
  use('dirt-track');

  // ---- lake + causeway (SW) ----
  g.water.push({ points: [{ x: -2600, y: 0, z: 1800 }, { x: -1600, y: 0, z: 1800 }], width: 420 });
  addRoad(g, 'causeway', [{ x: -2700, y: 1.2, z: 1800 }, { x: -2150, y: 1.2, z: 1800 }, { x: -1500, y: 0.4, z: 1800 }], { name: 'Lake causeway', bridge: { kind: 'slab' } });
  use('causeway');

  // ---- tunnel under N-S motorway ----
  addRoad(g, 'tunnel-road', [
    { x: 950, y: 0, z: 620 }, { x: 1200, y: -4.5, z: 620 }, { x: 1400, y: -5, z: 620 },
    { x: 1600, y: -4.5, z: 620 }, { x: 1850, y: 0, z: 620 },
  ], { name: 'Motorway underpass', tunnel: true });
  use('tunnel-road');

  // ---- diamond interchanges: E-W motorway × arterials x∈{-450,0,450} ----
  for (const x of [-450, 450]) {
    diamond(g, x, -1400, Y.motor, use);
  }
  // ring × rural interchanges (S + W)
  diamond(g, 512, R, Y.ring, use); // rural S crosses ring S at x≈512

  // ---- merge everything with precise topology ----
  const merges = g.autoMerge(0.6);

  // ---- roundabouts at 4 core intersections ----
  for (const [x, z] of [[-450, -450], [450, 450], [-450, 450], [450, -450]]) {
    makeRoundabout(g, x, z, 11 + rng() * 3);
  }
  g.classifyJunctions();
  g.trimAtJunctions();

  // ---- bridges over river: elevate + assign distinct families ----
  const bridgeKinds = ['beam', 'arch', 'truss', 'cablestay', 'viaduct', 'beam', 'suspension', 'slab', 'arch', 'truss'];
  let bi = 0;
  for (const e of g.edgeList()) {
    if (e.bridge) continue;
    if (crossesWater(e, g.water)) {
      // staggered deck heights: neighbouring river spans can never share a
      // same-height crossing (they were merged pre-elevation)
      const H = 6.5 + (bi % 3) * 1.3;
      e.bridge = { kind: bridgeKinds[bi++ % bridgeKinds.length], auto: true };
      elevateBridge(g, e, H);
    }
  }
  // footbridges over motorway + arterials (pedestrian layer).
  // Deck ≥8m: clears 5m motorway formation + 2.5m vehicle envelope.
  for (const [x, z, rot] of [[-700, -1400, 0], [1400, -700, 1], [200, 0, 0]]) {
    const horiz = rot === 0;
    const deck = (x === 1400 || z === -1400) ? 11 : 8;
    const a = horiz ? { x: x - 55, y: 0, z } : { x, y: 0, z: z - 55 };
    const b = horiz ? { x, y: deck, z } : { x, y: deck, z };
    const c = horiz ? { x: x + 55, y: 0, z } : { x, y: 0, z: z + 55 };
    addRoad(g, 'footpath', [a, b, c], { name: 'Footbridge', bridge: { kind: 'footbridge' } });
  }

  // ---- railway level crossings: furniture + approach type ----
  let converted = 0;
  for (const e of g.edgeList()) {
    const hits = crossesRail(e, g.rails);
    for (const h of hits) {
      g.railGear.push({ x: h.x, y: e.control[0].y, z: h.z, rot: Math.atan2(h.tx, h.tz), width: 8 });
      if (converted < 4 && e.typeId !== 'motorway-6' && e.typeId !== 'motorway-4') {
        e.typeId = 'rail-crossing';
        converted++;
      }
    }
  }
  use('rail-crossing');

  // ---- guarantee full catalog coverage ----
  ensureCoverage(g, used, rng);

  // ---- report ----
  let roadKm = 0;
  for (const e of g.edgeList()) roadKm += e.length / 1000;
  const report = {
    seed,
    edges: g.edges.size,
    junctions: [...g.nodes.values()].filter((n) => n.kind === 'junction').length,
    roundabouts: [...g.nodes.values()].filter((n) => n.roundabout).length,
    bridges: [...g.edges.values()].filter((e) => e.bridge).length,
    railCrossings: g.railGear.length,
    roadKm: Math.round(roadKm * 10) / 10,
    mapSize: `${MAP * 2 / 1000}×${MAP * 2 / 1000} km`,
    typeUsage: Object.fromEntries(used),
    merges: merges.length,
  };
  return { graph: g, report };
}

function addRoad(g, typeId, points, opts) {
  return g.addEdge(typeId, points, { ...opts, seed: (Math.random() * 1e9) | 0 });
}

function makeBag(rng) {
  let bag = [];
  return (options) => {
    if (!bag.length) {
      bag = [...options];
      for (let i = bag.length - 1; i > 0; i--) {
        const j = Math.floor(rng() * (i + 1));
        [bag[i], bag[j]] = [bag[j], bag[i]];
      }
    }
    return bag.pop();
  };
}

function diamond(g, cx, cz, yMotor, use) {
  // Full diamond interchange: 4 quadrant ramps. Ramp starts sit ON the
  // motorway centerline (same height → clean Y-merge with the motorway);
  // ramp ends sit ON the crossroad (T-merge). Divergence ≈28° keeps the
  // gore mouths separable (see junction radius growth).
  const L = 130, OFF = 75;
  const quads = [
    [cx - L, cz - OFF], [cx + L, cz - OFF],
    [cx - L, cz + OFF], [cx + L, cz + OFF],
  ];
  quads.forEach(([mx, qz], i) => {
    const t = i % 2 ? 'freeway-ramp-2' : 'freeway-ramp-1';
    g.addEdge(t, [
      { x: mx, y: yMotor, z: cz },
      { x: (mx + cx) / 2, y: yMotor * 0.5, z: (cz + qz) / 2 },
      { x: cx, y: 0, z: qz },
    ], { name: 'Ramp' });
    use(t);
  });
}

function makeRoundabout(g, x, z, radius) {
  let best = null, bestD = 1e9;
  for (const n of g.nodes.values()) {
    if (n.kind !== 'junction' || n.roundabout) continue;
    const d = Math.hypot(n.pos.x - x, n.pos.z - z);
    if (d < bestD) { bestD = d; best = n; }
  }
  if (best && bestD < 30) {
    best.roundabout = { radius, ringWidth: 7.5 };
    best.junction.radius = radius + 7.5 + 1.5;
  }
}

function crossesWater(edge, waters) {
  for (const w of waters) {
    for (let i = 0; i < edge.control.length - 1; i++) {
      for (let j = 0; j < w.points.length - 1; j++) {
        if (segIntersect(edge.control[i], edge.control[i + 1], w.points[j], w.points[j + 1])) return true;
      }
    }
  }
  return false;
}

function crossesRail(edge, rails) {
  const hits = [];
  for (const r of rails) {
    for (let i = 0; i < edge.control.length - 1; i++) {
      for (let j = 0; j < r.points.length - 1; j++) {
        const h = segIntersect(edge.control[i], edge.control[i + 1], r.points[j], r.points[j + 1]);
        if (h) {
          const a = edge.control[i], b = edge.control[i + 1];
          const l = Math.hypot(b.x - a.x, b.z - a.z) || 1;
          hits.push({ x: h.x, z: h.z, tx: (b.x - a.x) / l, tz: (b.z - a.z) / l });
        }
      }
    }
  }
  return hits;
}

function elevateBridge(g, edge, H) {
  // Smooth plateau profile: dead flat at both ends (junction mouths meet the
  // junction polygon exactly), cosine ramps, full deck height over the middle.
  const c = edge.control;
  if (c.length < 2) return;
  const a = c[0], b = c[c.length - 1];
  const ya = a.y, yb = b.y;
  const baseMid = (ya + yb) / 2;
  const target = Math.max(H, baseMid + 1.5);
  const shape = (t) => {
    if (t <= 0.15 || t >= 0.85) return 0;
    if (t >= 0.35 && t <= 0.65) return 1;
    if (t < 0.35) { const u = (t - 0.15) / 0.2; return 0.5 - 0.5 * Math.cos(u * Math.PI); }
    const u = (t - 0.65) / 0.2; return 0.5 + 0.5 * Math.cos(u * Math.PI);
  };
  const pts = [];
  const N = 12;
  for (let i = 0; i <= N; i++) {
    const t = i / N;
    const base = ya + (yb - ya) * t;
    pts.push({
      x: a.x + (b.x - a.x) * t,
      y: base + (target - base) * shape(t),
      z: a.z + (b.z - a.z) * t,
    });
  }
  edge.control = pts;
  g.rebuildEdge(edge);
}

function ensureCoverage(g, used, rng) {
  // every catalog type must appear ≥1 so the map shows the full world vocabulary
  const missing = ROAD_TYPES.filter((t) => !used.has(t.id)).map((t) => t.id);
  let kx = -2600, kz = 2600;
  for (const id of missing) {
    const t = roadTypeById(id);
    if (t.category === 'Special' && (id === 'roundabout-ring')) continue; // represented by roundabout nodes
    const len = 120 + rng() * 80;
    g.addEdge(id, [
      { x: kx, y: 0, z: kz },
      { x: kx + len / 2, y: 0, z: kz + (rng() - 0.5) * 30 },
      { x: kx + len, y: 0, z: kz },
    ], { name: `Showcase ${t.name}` });
    used.set(id, 1);
    kx += 40; kz -= 34;
    if (kx > -1500) { kx = -2600; kz -= 60; }
  }
}
