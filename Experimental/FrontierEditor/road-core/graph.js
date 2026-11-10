// Road Editor — road graph with precise (non-boolean) junction merging.
// Same-height crossings SPLIT both splines and share ONE junction polygon:
//   "+" not "|" over "-" · "T" not "|"+"-" · "Y" not "V"+"|"
// Grade-separated crossings (|Δh| ≥ tol) stay unmerged: upper deck = bridge.

import { catmullRom3, resampleByArc, segIntersect, clamp, angleOf, angleDiff } from './math.js';
import { roadTypeById, fullWidth } from './roadTypes.js';

let edgeSeq = 0, nodeSeq = 0;
export function resetIds() { edgeSeq = 0; nodeSeq = 0; }

export class RoadGraph {
  constructor() {
    this.edges = new Map(); // id -> edge
    this.nodes = new Map(); // id -> node
    this.rails = [];        // railway polylines (for crossings + rendering)
    this.water = [];        // river polygons (for bridges/causeways)
  }

  // points: [{x,y,z}] control points. opts: {name, bridge, closed, resample}
  addEdge(typeId, points, opts = {}) {
    const id = opts.id || `road-${++edgeSeq}`;
    const edge = {
      id,
      name: opts.name || `${roadTypeById(typeId).name} ${edgeSeq}`,
      typeId,
      control: points.map((p) => ({ ...p })),
      closed: !!opts.closed,
      bridge: opts.bridge || null,   // {kind} set later by generator / inspector
      tunnel: !!opts.tunnel,
      seed: opts.seed ?? edgeSeq * 7919,
      startNode: null, endNode: null,
      polyline: [],
      length: 0,
      trimStart: 0, trimEnd: 0,       // set by junction trimming
      hidden: false,
    };
    this.edges.set(id, edge);
    this.rebuildEdge(edge);
    return id;
  }

  rebuildEdge(edge) {
    const smooth = edge.control.length > 2 && !edge.closed
      ? catmullRom3(edge.control, 10)
      : edge.closed ? catmullRom3([...edge.control, edge.control[0]], 10) : edge.control.map((p) => ({ ...p }));
    edge.polyline = resampleByArc(smooth, 2.0);
    edge.length = edge.polyline.length ? edge.polyline[edge.polyline.length - 1].s : 0;
  }

  addNode(pos, kind = 'junction') {
    const id = `node-${++nodeSeq}`;
    this.nodes.set(id, {
      id, pos: { ...pos }, kind,
      incident: [], // {edge, end: 'start'|'end', dir:{x,z}}
      junction: { radius: 0, fillet: 6, signals: false, crosswalk: true, type: 'X' },
      roundabout: null,
    });
    return id;
  }

  edgeHalfWidth(edge) { return fullWidth(roadTypeById(edge.typeId)) / 2; }

  // ---- precise merging -------------------------------------------------
  autoMerge(heightTol = 0.6) {
    const merges = [];
    // 1) crossing scan on CONTROL polylines (cheap, stable)
    const polys = new Map();
    for (const [id, e] of this.edges) polys.set(id, e.control);
    const ids = [...polys.keys()];
    const crossings = [];
    for (let ia = 0; ia < ids.length; ia++) {
      for (let ib = ia + 1; ib < ids.length; ib++) {
        const A = this.edges.get(ids[ia]), B = this.edges.get(ids[ib]);
        if (A.hidden || B.hidden) continue;
        const pa = polys.get(ids[ia]), pb = polys.get(ids[ib]);
        for (let i = 0; i < pa.length - 1; i++) {
          for (let j = 0; j < pb.length - 1; j++) {
            const hit = segIntersect(pa[i], pa[i + 1], pb[j], pb[j + 1]);
            if (!hit) continue;
            // heights at crossing
            const ya = pa[i].y + (pa[i + 1].y - pa[i].y) * hit.t;
            const yb = pb[j].y + (pb[j + 1].y - pb[j].y) * hit.u;
            if (Math.abs(ya - yb) >= heightTol) {
              // Grade-separated: the UPPER edge passes over. Only edges on a
              // genuinely elevated formation (embankment / viaduct) need pier
              // + girder geometry; a flat road over a sunken tunnel needs none.
              const upper = ya > yb ? A : B;
              upper.overpass = true;
              const minY = Math.min(...upper.control.map((p) => p.y));
              if (!upper.bridge && minY >= 3) upper.bridge = { kind: 'beam', auto: true };
              merges.push({ kind: 'grade-separated', a: A.id, b: B.id, x: hit.x, z: hit.z });
              continue;
            }
            // skip if both edges already share a node near here
            const dup = crossings.some((c) => Math.hypot(c.x - hit.x, c.z - hit.z) < 3);
            if (dup) continue;
            crossings.push({ edgeA: A.id, edgeB: B.id, segA: i, segB: j, ...hit, y: (ya + yb) / 2 });
          }
        }
      }
    }
    // 2) split both edges at each crossing, insert junction node.
    // Splits are located by POINT at split time: earlier splits invalidate
    // recorded segment indices, so stale (seg,t) values must never be reused.
    for (const c of crossings) {
      // re-run safe: a junction node may already exist here from a prior merge
      const existing = [...this.nodes.values()].find(
        (n) => Math.hypot(n.pos.x - c.x, n.pos.z - c.z) < 3 && Math.abs(n.pos.y - c.y) < 0.8
      );
      this.splitLineageAt(c.edgeA, { x: c.x, y: c.y, z: c.z });
      this.splitLineageAt(c.edgeB, { x: c.x, y: c.y, z: c.z });
      if (existing) {
        merges.push({ kind: 'junction-existing', node: existing.id, x: c.x, z: c.z });
        continue;
      }
      const nodeId = this.addNode({ x: c.x, y: c.y, z: c.z });
      merges.push({ kind: 'junction', node: nodeId, x: c.x, z: c.z });
    }
    // 3) endpoint-to-interior touches (T / Y): snap endpoints onto other edges
    this.snapEndpoints(heightTol, merges);
    // 4) rebuild connectivity + classify junctions
    this.rebuildConnectivity();
    this.mergeNearbyNodes(4);     // fuse clustered crossing nodes
    this.rebuildConnectivity();
    this.collapseSlivers(2);      // clustered splits can leave <2m fragments
    this.rebuildConnectivity();
    this.classifyJunctions();
    this.trimAtJunctions();
    return merges;
  }

  // Fuse nearly-coincident nodes (clustered shallow-angle crossings leave
  // node pairs ~2-4m apart with sliver quads between them). The loser node's
  // incident edge endpoints are rewritten onto the survivor position, then
  // slivers collapse and connectivity rebuilds. Dead ends near a junction
  // snap in as proper T/Y arms.
  mergeNearbyNodes(tol = 4) {
    const rank = (n) => (n.kind === 'junction' ? 0 : n.kind === 'bend' || n.kind === 'through' ? 1 : 2);
    let merged = true;
    let guard = 0;
    while (merged && guard++ < 12) {
      merged = false;
      const list = [...this.nodes.values()];
      for (let i = 0; i < list.length && !merged; i++) {
        for (let j = i + 1; j < list.length && !merged; j++) {
          const a = list[i], b = list[j];
          if (!this.nodes.has(a.id) || !this.nodes.has(b.id)) continue;
          if (Math.abs(a.pos.y - b.pos.y) > 0.8) continue;
          if (Math.hypot(a.pos.x - b.pos.x, a.pos.z - b.pos.z) > tol) continue;
          // survivor: junction beats bend beats endpoint; then most arms
          const win = (rank(a) - rank(b)) || (b.incident.length - a.incident.length) <= 0 ? a : b;
          const lose = win === a ? b : a;
          for (const inc of lose.incident) {
            const e = this.edges.get(inc.edge);
            if (!e) continue;
            const pt = { x: win.pos.x, y: win.pos.y, z: win.pos.z };
            if (inc.end === 'start') e.control[0] = { ...pt };
            else e.control[e.control.length - 1] = { ...pt };
            this.rebuildEdge(e);
          }
          if (lose.roundabout && !win.roundabout) win.roundabout = lose.roundabout;
          this.nodes.delete(lose.id);
          merged = true;
        }
      }
    }
  }

  // Delete sub-length sliver fragments, then let the caller rebuild
  // connectivity/classification. Junctions absorb the freed arms.
  collapseSlivers(minLen = 2) {
    for (const e of [...this.edges.values()]) {
      if (e.length >= minLen) continue;
      this.edges.delete(e.id);
    }
  }

  // Split the piece of edge `edgeId`'s lineage that passes through `point`.
  // Earlier splits fragment edges, so the recorded id may no longer own the
  // crossing — lineage follows the geometry to the correct fragment without
  // ever touching unrelated nearby edges.
  //
  // Vertex coincidence still splits when the vertex is INTERIOR: a T whose
  // stem ends exactly on the through road's control vertex must break the
  // through road in two, or the through road never joins the junction node
  // (ribbons pass over the stem → overlap). Endpoints attach via clustering.
  // All guards are distance-based (0.6m) — t-fraction guards leave cracks.
  splitLineageAt(edgeId, point) {
    const root = this.edges.get(edgeId);
    const candidates = [];
    if (root) candidates.push(root);
    for (const e of this.edges.values()) {
      if (e.id !== edgeId && (e.lineage || e.id) === (root?.lineage || edgeId)) candidates.push(e);
    }
    for (const edge of candidates) {
      if (edge.closed) continue;
      const c = edge.control;
      if (Math.hypot(c[0].x - point.x, c[0].z - point.z) < 0.6) return;
      if (Math.hypot(c[c.length - 1].x - point.x, c[c.length - 1].z - point.z) < 0.6) return;
      for (let i = 1; i < c.length - 1; i++) {
        if (Math.hypot(c[i].x - point.x, c[i].z - point.z) < 0.6) {
          this.splitEdgeAtVertex(edge.id, i, point);
          return;
        }
      }
    }
    let best = null;
    for (const edge of candidates) {
      if (edge.closed) continue;
      for (let i = 0; i < edge.control.length - 1; i++) {
        const a = edge.control[i], b = edge.control[i + 1];
        const abx = b.x - a.x, abz = b.z - a.z;
        const l2 = abx * abx + abz * abz || 1;
        const t = ((point.x - a.x) * abx + (point.z - a.z) * abz) / l2;
        if (t < 0 || t > 1) continue; // projection off-segment: wrong piece
        const cx = a.x + abx * t, cz = a.z + abz * t;
        if (Math.hypot(cx - a.x, cz - a.z) < 0.6) continue;
        if (Math.hypot(cx - b.x, cz - b.z) < 0.6) continue;
        const d = Math.hypot(point.x - cx, point.z - cz);
        if (d > 2.5) continue;
        if (!best || d < best.d) best = { edge, i, t, d };
      }
    }
    if (best) this.splitEdgeAt(best.edge.id, best.i, best.t, point);
  }

  splitAtPoint(edgeId, point) { this.splitLineageAt(edgeId, point); }

  // Split at an existing interior vertex (snapped exactly onto `point`).
  // Geometry is unchanged; only the topology gains two attachable endpoints.
  splitEdgeAtVertex(edgeId, index, point) {
    const edge = this.edges.get(edgeId);
    if (!edge || edge.closed) return;
    if (index < 1 || index >= edge.control.length - 1) return;
    const left = edge.control.slice(0, index + 1);
    const right = edge.control.slice(index);
    left[left.length - 1] = { ...point };
    right[0] = { ...point };
    edge.control = left;
    this.rebuildEdge(edge);
    const newId = `road-${++edgeSeq}`;
    this.edges.set(newId, {
      ...edge, id: newId, control: right,
      lineage: edge.lineage || edge.id,
      startNode: null, endNode: null, trimStart: 0, trimEnd: 0,
    });
    this.rebuildEdge(this.edges.get(newId));
  }

  splitEdgeAt(edgeId, segIndex, t, point) {
    const edge = this.edges.get(edgeId);
    if (!edge || edge.closed) return;
    if (segIndex < 0 || segIndex >= edge.control.length - 1) return;
    if (t < 0 || t > 1) return; // off-segment: caller searched a stale control
    const a = edge.control[segIndex], b = edge.control[segIndex + 1];
    const sx = a.x + (b.x - a.x) * t, sz = a.z + (b.z - a.z) * t;
    if (Math.hypot(sx - a.x, sz - a.z) < 0.6) return;
    if (Math.hypot(sx - b.x, sz - b.z) < 0.6) return;
    const left = edge.control.slice(0, segIndex + 1);
    left.push({ ...point });
    const right = [{ ...point }, ...edge.control.slice(segIndex + 1)];
    if (left.length < 2 || right.length < 2) return;
    edge.control = left;
    this.rebuildEdge(edge);
    const newId = `road-${++edgeSeq}`;
    const copy = {
      ...edge, id: newId,
      name: edge.name, control: right,
      lineage: edge.lineage || edge.id,
      startNode: null, endNode: null, trimStart: 0, trimEnd: 0,
    };
    this.edges.set(newId, copy);
    this.rebuildEdge(copy);
  }

  snapEndpoints(heightTol, merges) {
    // 5m: interchange ramps land within ~4.5m of the crossing rural's
    // centerline (exact for city arterials). Still far below the ≥40m
    // spacing between independent roads, so no false snaps.
    // Endpoints already sitting on another edge's vertex are CONNECTED —
    // snapping them a metre along that edge's interior would shatter every
    // crossing into stubble, so they are skipped.
    const SNAP = 5;
    for (const edge of [...this.edges.values()]) {
      for (const end of ['start', 'end']) {
        const p = end === 'start' ? edge.control[0] : edge.control[edge.control.length - 1];
        let attached = false;
        for (const other of this.edges.values()) {
          if (other.id === edge.id) continue;
          for (const v of other.control) {
            if (Math.hypot(p.x - v.x, p.z - v.z) < 1.0 && Math.abs(p.y - v.y) < heightTol) { attached = true; break; }
          }
          if (attached) break;
        }
        if (attached) continue;
        let best = null;
        for (const other of this.edges.values()) {
          if (other.id === edge.id) continue;
          for (let i = 0; i < other.control.length - 1; i++) {
            const a = other.control[i], b = other.control[i + 1];
            const abx = b.x - a.x, abz = b.z - a.z;
            const l2 = abx * abx + abz * abz || 1;
            let t = ((p.x - a.x) * abx + (p.z - a.z) * abz) / l2;
            if (t < 0.05 || t > 0.95) continue;
            const cx = a.x + abx * t, cz = a.z + abz * t;
            const d = Math.hypot(p.x - cx, p.z - cz);
            const y = a.y + (b.y - a.y) * t;
            if (d < SNAP && Math.abs(y - p.y) < heightTol && (!best || d < best.d)) {
              best = { other, seg: i, t, d, x: cx, y: (y + p.y) / 2, z: cz };
            }
          }
        }
        if (best) {
          // move endpoint onto target, split target
          if (end === 'start') edge.control[0] = { x: best.x, y: best.y, z: best.z };
          else edge.control[edge.control.length - 1] = { x: best.x, y: best.y, z: best.z };
          this.rebuildEdge(edge);
          this.splitEdgeAt(best.other.id, best.seg, best.t, { x: best.x, y: best.y, z: best.z });
          merges.push({ kind: 'snap', edge: edge.id, onto: best.other.id });
        }
      }
    }
  }

  rebuildConnectivity() {
    // clear
    for (const n of this.nodes.values()) n.incident = [];
    // cluster endpoints (within 0.6m) into shared nodes
    const endpoints = [];
    for (const e of this.edges.values()) {
      e.startNode = null; e.endNode = null;
      endpoints.push({ edge: e, end: 'start', p: e.control[0] });
      endpoints.push({ edge: e, end: 'end', p: e.control[e.control.length - 1] });
    }
    const used = new Set();
    const key = (p) => `${Math.round(p.x * 2)},${Math.round(p.z * 2)}`;
    // reuse existing junction nodes first
    for (const n of this.nodes.values()) {
      for (let i = 0; i < endpoints.length; i++) {
        if (used.has(i)) continue;
        const ep = endpoints[i];
        if (Math.hypot(ep.p.x - n.pos.x, ep.p.z - n.pos.z) < 1.2 && Math.abs(ep.p.y - n.pos.y) < 0.8) {
          used.add(i);
          this.attach(n, ep.edge, ep.end);
        }
      }
    }
    // cluster leftovers by rounded position
    const buckets = new Map();
    endpoints.forEach((ep, i) => {
      if (used.has(i)) return;
      const k = key(ep.p);
      if (!buckets.has(k)) buckets.set(k, []);
      buckets.get(k).push({ ep, i });
    });
    for (const [, group] of buckets) {
      if (group.length === 1) {
        const { ep } = group[0];
        const nId = this.addNode({ ...ep.p }, 'endpoint');
        this.attach(this.nodes.get(nId), ep.edge, ep.end);
        used.add(group[0].i);
      } else {
        // shared node for coincident endpoints
        const p = group[0].ep.p;
        // verify they are truly coincident (bucket is coarse)
        const tight = group.filter(({ ep }) => Math.hypot(ep.p.x - p.x, ep.p.z - p.z) < 1.5);
        const loose = group.filter(({ ep }) => Math.hypot(ep.p.x - p.x, ep.p.z - p.z) >= 1.5);
        const nId = this.addNode({ ...p }, 'junction');
        const node = this.nodes.get(nId);
        for (const { ep, i } of tight) { this.attach(node, ep.edge, ep.end); used.add(i); }
        for (const { ep, i } of loose) {
          const sId = this.addNode({ ...ep.p }, 'endpoint');
          this.attach(this.nodes.get(sId), ep.edge, ep.end);
          used.add(i);
        }
      }
    }
    // drop orphan junction nodes
    for (const [id, n] of [...this.nodes]) {
      if (n.incident.length === 0) this.nodes.delete(id);
    }
  }

  attach(node, edge, end) {
    const dir = this.edgeEndDir(edge, end);
    node.incident.push({ edge: edge.id, end, dir });
    if (end === 'start') edge.startNode = node.id; else edge.endNode = node.id;
  }

  edgeEndDir(edge, end) {
    const c = edge.control;
    let a, b;
    if (end === 'start') { a = c[0]; b = c[1] || c[0]; return norm2({ x: a.x - b.x, z: a.z - b.z }); }
    a = c[c.length - 1]; b = c[c.length - 2] || a; return norm2({ x: a.x - b.x, z: a.z - b.z });
  }

  classifyJunctions() {
    for (const n of this.nodes.values()) {
      const k = n.incident.length;
      // sort incident by angle
      n.incident.sort((p, q) => angleOf(p.dir) - angleOf(q.dir));
      if (k <= 1) { n.kind = 'endpoint'; n.junction.type = 'END'; }
      else if (k === 2) {
        const d = Math.abs(angleDiff(angleOf(n.incident[0].dir), angleOf(n.incident[1].dir)));
        n.kind = d > Math.PI * 0.75 ? 'through' : 'bend';
        n.junction.type = 'L';
      } else {
        n.kind = 'junction';
        if (k === 3) {
          // T if one pair ~opposite (180°), else Y
          let tShape = false;
          for (let i = 0; i < 3; i++) {
            const d = Math.abs(angleDiff(angleOf(n.incident[i].dir), angleOf(n.incident[(i + 1) % 3].dir)));
            if (Math.abs(d - Math.PI) < 0.35) tShape = true;
          }
          n.junction.type = tShape ? 'T' : 'Y';
        } else if (k === 4) n.junction.type = '+';
        else n.junction.type = `${k}-way`;
      }
      // junction radius = max incident half-width + margin, then grown until
      // adjacent mouths separate angularly: atan(w1/R)+atan(w2/R) < gap.
      // Without this a wide arm's mouth swallows its neighbour's and the
      // junction polygon self-intersects (bowtie → flipped triangles).
      let r = 0;
      for (const inc of n.incident) {
        const e = this.edges.get(inc.edge);
        r = Math.max(r, e ? this.edgeHalfWidth(e) : 0);
      }
      let R = r + 1.2;
      if (k >= 3) {
        for (let i = 0; i < k; i++) {
          const A0 = n.incident[i], B0 = n.incident[(i + 1) % k];
          const ea = this.edges.get(A0.edge), eb = this.edges.get(B0.edge);
          const w1 = ea ? this.edgeHalfWidth(ea) : 4, w2 = eb ? this.edgeHalfWidth(eb) : 4;
          let gap = angleOf(B0.dir) - angleOf(A0.dir);
          while (gap <= 0) gap += Math.PI * 2;
          const gEff = Math.max(gap, 0.5); // near-parallel forks: clamp, don't explode
          R = Math.max(R, ((w1 + w2) / gEff) * 1.15);
        }
        R = Math.min(R, 42);
      }
      n.junction.radius = R;
      if (n.roundabout) n.junction.radius = n.roundabout.radius + 5;
    }
  }

  trimAtJunctions() {
    for (const e of this.edges.values()) { e.trimStart = 0; e.trimEnd = 0; }
    for (const n of this.nodes.values()) {
      if (n.kind !== 'junction' && n.kind !== 'roundabout') continue;
      const R = n.junction.radius;
      for (const inc of n.incident) {
        const e = this.edges.get(inc.edge);
        if (!e) continue;
        const trim = R / Math.max(0.35, Math.cos(0)); // mouth setback ≈ R
        if (inc.end === 'start') e.trimStart = Math.max(e.trimStart, Math.min(trim, e.length * 0.45));
        else e.trimEnd = Math.max(e.trimEnd, Math.min(trim, e.length * 0.45));
      }
    }
  }

  // iteration helpers
  edgeList() { return [...this.edges.values()]; }
  nodeList() { return [...this.nodes.values()]; }

  toJSON() {
    return {
      edges: [...this.edges.values()].map((e) => ({
        id: e.id, name: e.name, typeId: e.typeId, control: e.control,
        closed: e.closed, bridge: e.bridge, tunnel: e.tunnel, seed: e.seed, hidden: e.hidden,
      })),
      nodes: [...this.nodes.values()].map((n) => ({
        id: n.id, pos: n.pos, kind: n.kind, junction: n.junction, roundabout: n.roundabout,
      })),
      rails: this.rails, water: this.water,
      seq: [edgeSeq, nodeSeq],
    };
  }

  static fromJSON(data) {
    const g = new RoadGraph();
    resetIds();
    for (const e of data.edges || []) {
      g.edges.set(e.id, {
        ...e, polyline: [], length: 0, startNode: null, endNode: null, trimStart: 0, trimEnd: 0,
      });
      const edge = g.edges.get(e.id);
      g.rebuildEdge(edge);
      const m = /road-(\d+)/.exec(e.id);
      if (m) edgeSeq = Math.max(edgeSeq, +m[1]);
    }
    for (const n of data.nodes || []) {
      g.nodes.set(n.id, { ...n, incident: [] });
      const m = /node-(\d+)/.exec(n.id);
      if (m) nodeSeq = Math.max(nodeSeq, +m[1]);
    }
    g.rails = data.rails || [];
    g.water = data.water || [];
    g.rebuildConnectivity();
    g.collapseSlivers(2);
    g.rebuildConnectivity();
    g.classifyJunctions();
    g.trimAtJunctions();
    return g;
  }
}

function norm2(d) {
  const l = Math.hypot(d.x, d.z) || 1;
  return { x: d.x / l, z: d.z / l };
}

export { clamp };
