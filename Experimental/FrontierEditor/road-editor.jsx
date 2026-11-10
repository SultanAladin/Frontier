// Frontier Road Editor — NFS-scale spline road network workbench.
// Precise (non-boolean) junction merging · 33 world road types · 8 bridge
// families · procedural 6×6 km city + countryside + offroad · Unreal fly cam.
import React, { useEffect, useMemo, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import {
  MousePointer2, Spline, Plus, ShieldCheck, Save, FolderOpen, Download, Focus,
  HelpCircle, X, Layers, Trash2, Dices, Route, GitFork, ArrowUpRight, Camera,
} from 'lucide-react';
import './road-editor.css';
import { RoadGraph } from './road-core/graph.js';
import { ROAD_TYPES, ROAD_CATEGORIES, roadTypeById, fullWidth } from './road-core/roadTypes.js';
import { BRIDGE_TYPES } from './road-core/bridges.js';
import { buildNetworkMeshes } from './road-core/meshBuilder.js';
import { generateCity } from './road-core/cityGen.js';
import { auditTopology } from './road-core/validate.js';
import { RoadViewport } from './road-core/viewport.js';
import { serializeProject, deserializeProject, downloadText, exportOBJ } from './road-core/io.js';

const CAT_COLORS = {
  Motorway: '#e8a33d', Arterial: '#e07a5f', Urban: '#9aa8e8', Transit: '#5fce7d',
  Path: '#7dc3bf', Rural: '#c2b783', Offroad: '#a07a4f', Special: '#bd97db',
};

function App() {
  const mountRef = useRef(null);
  const viewportRef = useRef(null);
  const graphRef = useRef(new RoadGraph());
  const [ready, setReady] = useState(false);
  const [building, setBuilding] = useState(false);
  const [buildMs, setBuildMs] = useState(0);
  const [stats, setStats] = useState({ edges: 0, junctions: 0, bridges: 0, verts: 0, tris: 0, markings: 0, furniture: 0 });
  const [audit, setAudit] = useState(null);
  const [report, setReport] = useState(null);
  const [sel, setSel] = useState({ kind: null, id: null });
  const [mode, setMode] = useState('select');
  const [drawType, setDrawType] = useState('collector-2');
  const drawTypeRef = useRef(drawType); drawTypeRef.current = drawType;
  const selRef = useRef(sel); selRef.current = sel;
  const [drawHeight, setDrawHeight] = useState(0);
  const [drawCount, setDrawCount] = useState(0);
  const [layers, setLayers] = useState({ markings: true, furniture: true, water: true, rail: true });
  const [query, setQuery] = useState('');
  const [seed, setSeed] = useState(20261012);
  const [fps, setFps] = useState({ fps: 0, calls: 0, tris: 0 });
  const [speed, setSpeed] = useState(140);
  const [showHelp, setShowHelp] = useState(false);
  const [version, setVersion] = useState(0); // bump to refresh lists
  const fileRef = useRef(null);

  const rebuild = async (why) => {
    const vp = viewportRef.current;
    if (!vp) return;
    setBuilding(true);
    await new Promise((r) => setTimeout(r, 30));
    const t0 = performance.now();
    try {
      const g = graphRef.current;
      const meshes = buildNetworkMeshes(g, { groundYAt: () => 0 });
      vp.setData(g, meshes);
      vp.setSelected(selRef.current.kind, selRef.current.id);
      const a = auditTopology(g, meshes);
      setStats(meshes.stats);
      setAudit(a);
      setBuildMs(Math.round(performance.now() - t0));
      setVersion((v) => v + 1);
    } catch (err) {
      console.error('rebuild failed:', err);
    }
    setBuilding(false);
    void why;
  };

  const newCity = async (s) => {
    setBuilding(true);
    await new Promise((r) => setTimeout(r, 30));
    try {
      const { graph, report } = generateCity(s);
      graphRef.current = graph;
      setReport(report);
      setSel({ kind: null, id: null });
      viewportRef.current?.setSelected(null, null);
      const t0 = performance.now();
      const meshes = buildNetworkMeshes(graph, { groundYAt: () => 0 });
      viewportRef.current?.setData(graph, meshes);
      setStats(meshes.stats);
      setAudit(auditTopology(graph, meshes));
      setBuildMs(Math.round(performance.now() - t0));
      setVersion((v) => v + 1);
    } catch (err) {
      console.error('city gen failed:', err);
    }
    setBuilding(false);
  };

  // ---- viewport lifecycle ----
  useEffect(() => {
    const vp = new RoadViewport(mountRef.current, {
      onSelect: (kind, id) => setSel({ kind, id }),
      onCommitDraw: (points) => {
        const g = graphRef.current;
        const id = g.addEdge(drawTypeRef.current, points, { seed: (Math.random() * 1e9) | 0 });
        g.autoMerge(0.6);
        setSel({ kind: 'edge', id });
        setDrawCount(0);
        rebuild('draw');
      },
      onDrawProgress: (n) => setDrawCount(n),
      onEditPoint: (edgeId, index, pos) => {
        const g = graphRef.current;
        const e = g.edges.get(edgeId);
        if (!e) return;
        e.control[index] = { ...e.control[index], x: pos.x, z: pos.z };
        g.rebuildEdge(e);
        g.autoMerge(0.6);
        rebuild('drag');
      },
      onStats: (s) => setFps(s),
      onSpeed: (v) => setSpeed(Math.round(v)),
    });
    viewportRef.current = vp;
    setReady(true);
    newCity(seed);
    const onKey = (e) => {
      if (e.target.matches('input,textarea,select')) return;
      if (e.code === 'KeyV') setMode('select');
      if (e.code === 'KeyD') setMode('draw');
    };
    window.addEventListener('keydown', onKey);
    return () => { window.removeEventListener('keydown', onKey); vp.dispose(); };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => { viewportRef.current?.setMode(mode); }, [mode]);
  useEffect(() => { viewportRef.current?.setLayers(layers); }, [layers]);
  useEffect(() => { viewportRef.current.setSelected(sel.kind, sel.id); }, [sel]);
  useEffect(() => { if (viewportRef.current) viewportRef.current.drawHeight = drawHeight; }, [drawHeight]);

  // ---- derived lists ----
  const g = graphRef.current;
  const edges = useMemo(() => g.edgeList(), [g, version]);
  const nodes = useMemo(() => g.nodeList().filter((n) => n.kind === 'junction'), [g, version]);
  const bridges = useMemo(() => edges.filter((e) => e.bridge), [edges]);
  const q = query.toLowerCase();
  const fEdges = edges.filter((e) => !q || e.name.toLowerCase().includes(q) || e.typeId.includes(q)).slice(0, 220);
  const fNodes = nodes.filter((n) => !q || n.id.includes(q) || n.junction.type.includes(q.toUpperCase())).slice(0, 120);

  const selEdge = sel.kind === 'edge' ? g.edges.get(sel.id) : null;
  const selNode = sel.kind === 'node' ? g.nodes.get(sel.id) : null;
  const selType = selEdge ? roadTypeById(selEdge.typeId) : null;

  // ---- mutations ----
  const mutate = (fn, why) => { fn(graphRef.current); rebuild(why); };
  const setEdgeType = (id, typeId) => mutate((gg) => {
    const e = gg.edges.get(id); if (!e) return;
    e.typeId = typeId;
    gg.classifyJunctions(); gg.trimAtJunctions();
  }, 'type');
  const setBridge = (id, kind) => mutate((gg) => {
    const e = gg.edges.get(id); if (!e) return;
    e.bridge = kind ? { kind } : null;
  }, 'bridge');
  const reshuffle = (id) => mutate((gg) => {
    const e = gg.edges.get(id); if (e) e.seed = (Math.random() * 1e9) | 0;
  }, 'variant');
  const deleteEdge = (id) => mutate((gg) => {
    gg.edges.delete(id);
    gg.rebuildConnectivity(); gg.classifyJunctions(); gg.trimAtJunctions();
    setSel({ kind: null, id: null });
  }, 'delete');
  const nodeProp = (id, key, val) => mutate((gg) => {
    const n = gg.nodes.get(id); if (!n) return;
    if (key === 'roundabout') {
      n.roundabout = val ? { radius: 11, ringWidth: 7.5 } : null;
      gg.classifyJunctions(); gg.trimAtJunctions();
    } else {
      n.junction[key] = val;
      if (key === 'radius') gg.trimAtJunctions();
    }
  }, 'node');

  const saveProject = () => {
    downloadText(`frontier-roads-${Date.now()}.json`, serializeProject(graphRef.current, { seed, report }));
  };
  const loadProject = (file) => {
    const reader = new FileReader();
    reader.onload = async () => {
      try {
        graphRef.current = deserializeProject(reader.result);
        setReport(null);
        setSel({ kind: null, id: null });
        await rebuild('load');
      } catch (err) { window.alert('Could not load project: ' + err.message); }
    };
    reader.readAsText(file);
  };
  const doExport = () => {
    const meshes = buildNetworkMeshes(graphRef.current, { groundYAt: () => 0 });
    downloadText(`frontier-roads-${Date.now()}.obj`, exportOBJ(meshes), 'text/plain');
  };

  const maxUse = Math.max(1, ...Object.values(report?.typeUsage || { x: 1 }));

  return (
    <div className="road-shell">
      {!ready && <div className="loading-veil"><h1>frontier<span>.</span> road editor</h1><div className="spinner" /><p>Building viewport…</p></div>}
      {/* ------------------------------ toolbar ------------------------------ */}
      <header className="topbar">
        <div className="brand"><Route size={17} color="#e8a33d" />frontier<span className="dot">.</span><span className="brand-sub">ROAD EDITOR</span></div>
        <div className="tb-sep" />
        <div className="tb-group">
          <button className={`tb-btn ${mode === 'select' ? 'active' : ''}`} onClick={() => setMode('select')} title="Select / move spline points (V)"><MousePointer2 size={14} />Select</button>
          <button className={`tb-btn ${mode === 'draw' ? 'active' : ''}`} onClick={() => setMode('draw')} title="Draw road spline — click points, Enter commits (D)"><Spline size={14} />Draw</button>
          {mode === 'draw' && (
            <>
              <select className="tb-select" value={drawType} onChange={(e) => setDrawType(e.target.value)}>
                {ROAD_CATEGORIES.map((c) => (
                  <optgroup key={c} label={c}>
                    {ROAD_TYPES.filter((t) => t.category === c).map((t) => <option key={t.id} value={t.id}>{t.name}</option>)}
                  </optgroup>
                ))}
              </select>
              <span className="tb-hint">h=</span>
              <input className="tb-num" type="number" value={drawHeight} step={0.5} onChange={(e) => setDrawHeight(+e.target.value)} title="Spline height (grade separation ≥0.6m)" />
              {drawCount > 0 && <span className="tb-hint">{drawCount} pts · <b>Enter</b> commits · <b>⌫</b> undoes</span>}
            </>
          )}
        </div>
        <div className="tb-sep" />
        <div className="tb-group">
          <button className="tb-btn primary" onClick={() => newCity(seed)} title="Regenerate the whole map"><Dices size={14} />Generate</button>
          <input className="tb-num" style={{ width: 96 }} type="number" value={seed} onChange={(e) => setSeed(+e.target.value)} title="City seed" />
          <button className="tb-btn" onClick={() => rebuild('validate')} title="Rebuild meshes + re-run topology audit"><ShieldCheck size={14} />Validate</button>
        </div>
        <div className="tb-sep" />
        <div className="tb-group">
          <button className="tb-btn" onClick={saveProject} title="Save .json project"><Save size={14} />Save</button>
          <button className="tb-btn" onClick={() => fileRef.current?.click()} title="Load .json project"><FolderOpen size={14} />Load</button>
          <input ref={fileRef} type="file" accept=".json" style={{ display: 'none' }} onChange={(e) => e.target.files[0] && loadProject(e.target.files[0])} />
          <button className="tb-btn" onClick={doExport} title="Export merged network as .obj"><Download size={14} />OBJ</button>
          <button className="tb-btn" onClick={() => viewportRef.current?.focusAll()} title="Frame the whole map"><Focus size={14} /></button>
          <button className="tb-btn" onClick={() => setShowHelp(true)} title="Camera + topology help"><HelpCircle size={14} /></button>
        </div>
      </header>

      <div className="workbench">
        {/* ------------------------------ left ------------------------------ */}
        <aside className="side left">
          <div className="card">
            <h3><Layers size={12} />Layers</h3>
            <div className="layer-toggles">
              {([['markings', 'Markings'], ['furniture', 'Furniture'], ['water', 'Water'], ['rail', 'Rail']]).map(([k, label]) => (
                <button key={k} className={`toggle ${layers[k] ? 'on' : ''}`} onClick={() => setLayers((l) => ({ ...l, [k]: !l[k] }))}>
                  <span className="pill" />{label}
                </button>
              ))}
            </div>
          </div>

          {report && (
            <div className="card">
              <h3><Camera size={12} />Generated map <span className="count">seed {report.seed}</span></h3>
              <div className="row"><label>Extent</label><span className="val">{report.mapSize}</span></div>
              <div className="row"><label>Road length</label><span className="val">{report.roadKm} km</span></div>
              <div className="row"><label>Edges / junctions</label><span className="val">{report.edges} / {report.junctions}</span></div>
              <div className="row"><label>Roundabouts / bridges</label><span className="val">{report.roundabouts} / {report.bridges}</span></div>
              <div className="row"><label>Rail crossings</label><span className="val">{report.railCrossings}</span></div>
            </div>
          )}

          <div className="card">
            <h3><Route size={12} />Roads <span className="count">{edges.length}</span></h3>
            <input className="search" placeholder="Filter roads…" value={query} onChange={(e) => setQuery(e.target.value)} />
            <div style={{ height: 8 }} />
            <div className="list">
              {fEdges.map((e) => {
                const t = roadTypeById(e.typeId);
                return (
                  <div key={e.id} className={`list-item ${sel.id === e.id ? 'selected' : ''}`}
                    onClick={() => setSel({ kind: 'edge', id: e.id })}
                    onDoubleClick={() => viewportRef.current?.focusSelection()}>
                    <span className="swatch" style={{ background: CAT_COLORS[t.category] }} />
                    <span>{e.name}</span>
                    <span className="tag">{e.bridge ? `⌁ ${e.bridge.kind}` : t.name}</span>
                  </div>
                );
              })}
            </div>
          </div>

          <div className="card">
            <h3><GitFork size={12} />Junctions <span className="count">{nodes.length}</span></h3>
            <div className="list">
              {fNodes.slice(0, 60).map((n) => (
                <div key={n.id} className={`list-item ${sel.id === n.id ? 'selected' : ''}`}
                  onClick={() => setSel({ kind: 'node', id: n.id })}
                  onDoubleClick={() => viewportRef.current?.focusSelection()}>
                  <span className="swatch" style={{ background: n.roundabout ? '#5fce7d' : '#e8a33d' }} />
                  <span>{n.roundabout ? 'Roundabout' : `${n.junction.type} junction`} · {n.incident.length} arms</span>
                  <span className="tag">{n.id}</span>
                </div>
              ))}
            </div>
          </div>

          <div className="card">
            <h3><ArrowUpRight size={12} />Bridges <span className="count">{bridges.length}</span></h3>
            <div className="list">
              {bridges.slice(0, 60).map((e) => (
                <div key={e.id} className={`list-item ${sel.id === e.id ? 'selected' : ''}`}
                  onClick={() => setSel({ kind: 'edge', id: e.id })}>
                  <span className="swatch" style={{ background: '#6aa8e8' }} />
                  <span>{e.name}</span>
                  <span className="tag">{e.bridge.kind}</span>
                </div>
              ))}
            </div>
          </div>
        </aside>

        {/* ------------------------------ viewport ------------------------------ */}
        <div className="viewport-wrap">
          <div ref={mountRef} className="viewport-mount" />
          <div className="viewport-overlay">
            <span className="mode-chip"><b>{mode === 'draw' ? 'DRAW' : 'SELECT'}</b> · {mode === 'draw' ? `${roadTypeById(drawType).name} — click to place, Enter commits` : 'click road / junction · drag orange handles · RMB look · WASD fly'}</span>
          </div>
          {building && <div className="building-veil"><div className="spinner" />Rebuilding network + audit…</div>}
        </div>

        {/* ------------------------------ right ------------------------------ */}
        <aside className="side right">
          {selEdge && selType && (
            <div className="card">
              <h3><Route size={12} />Road</h3>
              <div className="row"><label>Name</label><span className="val">{selEdge.name}</span></div>
              <div className="row"><label>Length</label><span className="val">{Math.round(selEdge.length)} m</span></div>
              <div className="row"><label>Width</label><span className="val">{fullWidth(selType).toFixed(1)} m · {selType.lanes.length} strips</span></div>
              <div className="row"><label>Speed</label><span className="val">{selType.speed} km/h</span></div>
              <div className="row"><label>Ends</label><span className="val muted">{selEdge.startNode || '—'} → {selEdge.endNode || '—'}</span></div>
              <div className="row">
                <label>Bridge</label>
                <select className="inline" value={selEdge.bridge?.kind || ''} onChange={(e) => setBridge(selEdge.id, e.target.value || null)}>
                  <option value="">— at grade —</option>
                  {BRIDGE_TYPES.map((b) => <option key={b.id} value={b.id}>{b.name}</option>)}
                </select>
              </div>
              <div className="row">
                <label>Surface variant</label>
                <button className="tb-btn" onClick={() => reshuffle(selEdge.id)} title="New seeded wear/patch variation"><Dices size={13} />Shuffle</button>
                <button className="tb-btn danger" onClick={() => deleteEdge(selEdge.id)} title="Delete road"><Trash2 size={13} /></button>
              </div>
              <div style={{ height: 10 }} />
              <h3>Road type · {selType.category}</h3>
              <div className="type-groups">
                {ROAD_CATEGORIES.map((c) => (
                  <div key={c}>
                    <div className="type-cat">{c}</div>
                    <div className="type-grid">
                      {ROAD_TYPES.filter((t) => t.category === c).map((t) => (
                        <button key={t.id} className={`type-btn ${t.id === selEdge.typeId ? 'active' : ''}`} onClick={() => setEdgeType(selEdge.id, t.id)}>
                          {t.name}<small>{t.lanes.filter((l) => l.kind === 'drive').length} drive · {t.surface}</small>
                        </button>
                      ))}
                    </div>
                  </div>
                ))}
              </div>
            </div>
          )}

          {selNode && (
            <div className="card">
              <h3><GitFork size={12} />{selNode.roundabout ? 'Roundabout' : `${selNode.junction.type} junction`}</h3>
              <div className="row"><label>Arms</label><span className="val">{selNode.incident.length}</span></div>
              <div className="row"><label>Radius</label><input type="range" min={4} max={30} step={0.5} value={selNode.junction.radius} onChange={(e) => nodeProp(selNode.id, 'radius', +e.target.value)} /><span className="val">{selNode.junction.radius.toFixed(1)}m</span></div>
              <div className="row"><label>Curb fillet</label><input type="range" min={1} max={14} step={0.5} value={selNode.junction.fillet} onChange={(e) => nodeProp(selNode.id, 'fillet', +e.target.value)} /><span className="val">{selNode.junction.fillet.toFixed(1)}m</span></div>
              <div className="row"><label>Crosswalks</label><button className={`toggle ${selNode.junction.crosswalk ? 'on' : ''}`} onClick={() => nodeProp(selNode.id, 'crosswalk', !selNode.junction.crosswalk)}><span className="pill" />{selNode.junction.crosswalk ? 'On' : 'Off'}</button></div>
              <div className="row"><label>Signals</label><button className={`toggle ${selNode.junction.signals ? 'on' : ''}`} onClick={() => nodeProp(selNode.id, 'signals', !selNode.junction.signals)}><span className="pill" />{selNode.junction.signals ? 'On' : 'Off'}</button></div>
              <div className="row"><label>Roundabout</label><button className={`toggle ${selNode.roundabout ? 'on' : ''}`} onClick={() => nodeProp(selNode.id, 'roundabout', !selNode.roundabout)}><span className="pill" />{selNode.roundabout ? 'On' : 'Off'}</button></div>
              <p className="muted">One polygon fills the junction — ribbons stop at mouths, so + / T / Y merge exactly with no stacked slabs.</p>
            </div>
          )}

          {!selEdge && !selNode && (
            <div className="card">
              <h3><Plus size={12} />Inspector</h3>
              <p className="muted">Select a road to swap its type, bridge family or surface variant — or a junction to tune radius, fillets and crosswalks. Drag the orange spline handles in the viewport to reshape roads; crossings re-merge automatically.</p>
              <div className="row"><label>Road types</label><span className="val">{ROAD_TYPES.length} catalogued</span></div>
              <div className="row"><label>Bridge families</label><span className="val">{BRIDGE_TYPES.length}</span></div>
            </div>
          )}

          <div className="card">
            <h3><ShieldCheck size={12} />Topology audit {audit && <span className="count">{audit.passed}/{audit.total}</span>}</h3>
            {audit && <div className={`audit-banner ${audit.clean ? 'clean' : 'dirty'}`}>{audit.clean ? 'CLEAN — no overlaps, weld sealed' : 'ISSUES FOUND — see checks'}</div>}
            <div style={{ height: 8 }} />
            <div className="audit">
              {(audit?.checks || []).map((c) => (
                <div key={c.id} className={`audit-item ${c.pass ? 'pass' : 'fail'}`}>
                  <span className="st">{c.pass ? '✓' : '✕'}</span>
                  <span>{c.name}<small>{c.detail}</small></span>
                </div>
              ))}
            </div>
          </div>

          {report && (
            <div className="card">
              <h3>Type coverage <span className="count">{Object.keys(report.typeUsage).length}/{ROAD_TYPES.length}</span></h3>
              <div className="usage">
                {ROAD_TYPES.map((t) => {
                  const n = report.typeUsage[t.id] || 0;
                  return (
                    <div key={t.id} className="usage-row">
                      <span>{t.name}</span>
                      <span className="bar"><i style={{ width: `${(n / maxUse) * 100}%`, background: n ? CAT_COLORS[t.category] : '#333' }} /></span>
                      <span>{n || '—'}</span>
                    </div>
                  );
                })}
              </div>
            </div>
          )}
        </aside>
      </div>

      {/* ------------------------------ status ------------------------------ */}
      <footer className="statusbar">
        <span>verts <b>{stats.verts.toLocaleString()}</b></span>
        <span>tris <b>{(stats.tris / 1000).toFixed(0)}k</b></span>
        <span>markings <b>{stats.markings.toLocaleString()}</b></span>
        <span>furniture <b>{stats.furniture.toLocaleString()}</b></span>
        <span>build <b>{buildMs} ms</b></span>
        <span>fps <b>{fps.fps}</b> · {fps.calls} calls</span>
        <span>fly <b>{speed} m/s</b></span>
        {audit && <span className={audit.clean ? 'ok' : 'bad'}>{audit.clean ? '● topology clean' : '● topology issues'}</span>}
        <span className="spacer" />
        <span>{sel.id ? `${sel.kind} ${sel.id}` : 'nothing selected'}</span>
      </footer>

      {showHelp && (
        <div className="help-veil" onClick={() => setShowHelp(false)}>
          <div className="help-card" onClick={(e) => e.stopPropagation()}>
            <div className="row"><h2>Road Editor — guide</h2><span style={{ flex: 1 }} /><button className="tb-btn" onClick={() => setShowHelp(false)}><X size={14} /></button></div>
            <p>Unreal-style fly camera over a 6×6 km spline road network with precise junction merging — same-height splines split and share <b>one</b> junction polygon (+ / T / Y), never stacked slabs. Height differences ≥ 0.6 m stay grade-separated with bridge decks.</p>
            <h4>Camera</h4>
            <div className="keys-grid">
              <span><kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> fly</span>
              <span><kbd>Q</kbd> / <kbd>E</kbd> down / up</span>
              <span><kbd>RMB drag</kbd> look</span>
              <span><kbd>wheel</kbd> speed + dolly</span>
              <span><kbd>Shift</kbd> 4× boost</span>
              <span><kbd>F</kbd> / double-click focus</span>
            </div>
            <h4>Editing</h4>
            <p><kbd>V</kbd> select · <kbd>D</kbd> draw · click roads/junctions to inspect · drag orange spline handles to reshape (crossings re-merge) · <kbd>Enter</kbd> commits a drawn spline · <kbd>⌫</kbd> removes last point · <kbd>Esc</kbd> cancels.</p>
            <h4>Content</h4>
            <p>33 road types: motorways, arterials, busways, tram streets, one-ways, alleys, cobble + brick heritage, promenades, cycle superhighways, rural highways, gravel, dirt / farm / forest offroad, mountain pass, causeway, tunnel and rail-crossing approaches. Furniture: lamps, bollards, guardrails, speed tables, bus shelters, level-crossing barriers + crossbucks. Bridges: beam, slab, arch, truss, suspension, cable-stayed, viaduct, footbridge.</p>
            <h4>Audit</h4>
            <p>Validate re-runs 8 checks: unmerged overlaps, mouth trimming, junction valence, finite vertices, bridge clearance, one-way directions, welded triangles and marking lift.</p>
          </div>
        </div>
      )}
    </div>
  );
}

createRoot(document.getElementById('root')).render(<App />);
