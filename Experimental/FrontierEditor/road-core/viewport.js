// Road Editor — Three.js viewport with Unreal-style fly camera.
// WASD move · Q/E down/up · RMB look · wheel speed+dolly · Shift boost · F focus.

import * as THREE from 'three';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';

const BUCKET_STYLE = {
  'asphalt':         { color: 0xffffff, roughness: 0.96 },
  'asphalt-worn':    { color: 0xffffff, roughness: 0.97 },
  'asphalt-green':   { color: 0xffffff, roughness: 0.95 },
  'concrete':        { color: 0xffffff, roughness: 0.9 },
  'concrete-dark':   { color: 0xffffff, roughness: 0.92 },
  'cobble':          { color: 0xffffff, roughness: 0.95 },
  'sett':            { color: 0xffffff, roughness: 0.95 },
  'brick':           { color: 0xffffff, roughness: 0.92 },
  'paver':           { color: 0xffffff, roughness: 0.9 },
  'gravel':          { color: 0xffffff, roughness: 1.0 },
  'dirt':            { color: 0xffffff, roughness: 1.0 },
  'dirt-dark':       { color: 0xffffff, roughness: 1.0 },
  'redbus':          { color: 0xffffff, roughness: 0.94 },
  'sidewalk':        { color: 0xd8d8d2, roughness: 0.9 },
  'curb':            { color: 0xb5b5b0, roughness: 0.9 },
  'verge':           { color: 0xffffff, roughness: 1.0 },
  'marking-white':   { color: 0xf2f2ee, roughness: 0.55, offset: true },
  'marking-yellow':  { color: 0xd9a52b, roughness: 0.55, offset: true },
  'marking-green':   { color: 0x35a06a, roughness: 0.6, offset: true },
  'steel':           { color: 0xc8ccd2, roughness: 0.32, metalness: 0.85 },
  'railbed':         { color: 0x8a8478, roughness: 1.0 },
  'bridge-deck':     { color: 0x8f9499, roughness: 0.9 },
  'bridge-rail':     { color: 0xb9bdc2, roughness: 0.7 },
  'bridge-steel':    { color: 0x4a6a8f, roughness: 0.55, metalness: 0.5 },
  'bridge-concrete': { color: 0xc2c0b6, roughness: 0.85 },
  'bridge-cable':    { color: 0x3a3d42, roughness: 0.4, metalness: 0.8 },
  'water':           { color: 0x2e7f9e, roughness: 0.15, metalness: 0.25, transparent: true, opacity: 0.9 },
};

export class RoadViewport {
  constructor(container, cb = {}) {
    this.container = container;
    this.cb = cb;
    this.mode = 'select';
    this.drawPoints = [];
    this.drawHeight = 0;
    this.selected = { kind: null, id: null };
    this.layers = { markings: true, furniture: true, water: true, rail: true };
    this.speed = 140;
    this.keys = new Set();
    this.yaw = Math.PI * 0.25; this.pitch = -0.42;

    this.renderer = new THREE.WebGLRenderer({ antialias: true });
    this.renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.06;
    container.appendChild(this.renderer.domElement);

    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x9db4cc);
    this.scene.fog = new THREE.Fog(0x9db4cc, 900, 7000);

    this.camera = new THREE.PerspectiveCamera(60, 1, 0.5, 20000);
    this.camera.position.set(2600, 1500, 2600);
    this.lookFromAngles();

    this.scene.add(new THREE.HemisphereLight(0xbdd4ea, 0x4c4a42, 0.95));
    const sun = new THREE.DirectionalLight(0xfff0da, 1.7);
    sun.position.set(2200, 2800, 1400);
    this.scene.add(sun);

    this.world = new THREE.Group();
    this.scene.add(this.world);
    this.buildGround();

    this.proxyMeshes = [];
    this.handles = [];
    this.highlight = null;
    this.ray = new THREE.Raycaster();
    this.pointer = new THREE.Vector2();

    this.bindInput();
    this.resize();
    this.ro = new ResizeObserver(() => this.resize());
    this.ro.observe(container);

    this.clock = new THREE.Clock();
    this.frames = 0; this.fpsT = 0; this.fps = 0;
    this.loop();
  }

  // ------------------------------------------------------------- ground
  buildGround() {
    const cv = document.createElement('canvas');
    cv.width = cv.height = 1024;
    const ctx = cv.getContext('2d');
    // base countryside
    const grad = ctx.createLinearGradient(0, 0, 1024, 1024);
    grad.addColorStop(0, '#5c6a48'); grad.addColorStop(0.45, '#66744c');
    grad.addColorStop(0.75, '#5f6c46'); grad.addColorStop(1, '#57653f');
    ctx.fillStyle = grad; ctx.fillRect(0, 0, 1024, 1024);
    // mountain rock (NW)
    const rock = ctx.createRadialGradient(120, 120, 20, 120, 120, 340);
    rock.addColorStop(0, '#8a857c'); rock.addColorStop(0.6, '#75705f'); rock.addColorStop(1, 'rgba(117,112,95,0)');
    ctx.fillStyle = rock; ctx.fillRect(0, 0, 420, 420);
    // forest (SE)
    const forest = ctx.createRadialGradient(830, 830, 30, 830, 830, 320);
    forest.addColorStop(0, '#3d5230'); forest.addColorStop(1, 'rgba(61,82,48,0)');
    ctx.fillStyle = forest; ctx.fillRect(500, 500, 524, 524);
    // city slab
    ctx.fillStyle = '#6e6e68';
    ctx.fillRect(1024 / 2 - 215, 1024 / 2 - 215, 430, 430);
    ctx.fillStyle = '#67675f';
    ctx.fillRect(1024 / 2 - 165, 1024 / 2 - 165, 330, 330);
    // noise speckle
    for (let i = 0; i < 9000; i++) {
      ctx.fillStyle = `rgba(${Math.random() > 0.5 ? '255,255,255' : '0,0,0'},${Math.random() * 0.05})`;
      ctx.fillRect(Math.random() * 1024, Math.random() * 1024, 2, 2);
    }
    const tex = new THREE.CanvasTexture(cv);
    tex.colorSpace = THREE.SRGBColorSpace;
    tex.anisotropy = 4;
    const ground = new THREE.Mesh(
      new THREE.PlaneGeometry(7200, 7200),
      new THREE.MeshStandardMaterial({ map: tex, roughness: 1 })
    );
    ground.rotation.x = -Math.PI / 2;
    ground.position.y = -0.25;
    this.world.add(ground);
    const grid = new THREE.GridHelper(6400, 64, 0xffffff, 0xffffff);
    grid.material.transparent = true;
    grid.material.opacity = 0.07;
    grid.position.y = 0.0;
    this.world.add(grid);
  }

  // ------------------------------------------------------------- data
  setData(graph, meshes) {
    this.graph = graph;
    this.meshes = meshes;
    // clear old network group
    if (this.net) {
      this.world.remove(this.net);
      this.net.traverse((o) => {
        if (o.geometry) o.geometry.dispose();
        if (o.material) (Array.isArray(o.material) ? o.material : [o.material]).forEach((m) => m.dispose());
      });
    }
    this.net = new THREE.Group();
    this.world.add(this.net);
    this.proxyMeshes = [];

    for (const [key, b] of Object.entries(meshes.buckets)) {
      if (!b.count || !b.index.length) continue;
      const geo = new THREE.BufferGeometry();
      geo.setAttribute('position', new THREE.Float32BufferAttribute(b.positions, 3));
      geo.setAttribute('normal', new THREE.Float32BufferAttribute(b.normals, 3));
      geo.setAttribute('uv', new THREE.Float32BufferAttribute(b.uvs, 2));
      geo.setAttribute('color', new THREE.Float32BufferAttribute(b.colors, 3));
      geo.setIndex(b.index);
      const st = BUCKET_STYLE[key] || { color: 0xffffff, roughness: 0.9 };
      const mat = new THREE.MeshStandardMaterial({
        color: st.color, roughness: st.roughness ?? 0.9, metalness: st.metalness ?? 0,
        vertexColors: true, transparent: !!st.transparent, opacity: st.opacity ?? 1,
        polygonOffset: !!st.offset, polygonOffsetFactor: st.offset ? -2 : 0, polygonOffsetUnits: st.offset ? -2 : 0,
      });
      const mesh = new THREE.Mesh(geo, mat);
      mesh.userData.bucket = key;
      this.applyLayerVisibility(mesh);
      this.net.add(mesh);
    }
    this.buildFurniture(meshes.furniture);
    // proxies (invisible but raycastable)
    const proxyMat = new THREE.MeshBasicMaterial({ transparent: true, opacity: 0, depthWrite: false, colorWrite: false, side: THREE.DoubleSide });
    for (const p of meshes.proxies) {
      const geo = new THREE.BufferGeometry();
      geo.setAttribute('position', new THREE.BufferAttribute(p.positions, 3));
      geo.setIndex(p.index);
      const m = new THREE.Mesh(geo, proxyMat);
      m.userData.pick = { kind: p.kind, id: p.id };
      this.net.add(m);
      this.proxyMeshes.push(m);
    }
    this.refreshSelection();
    this.applyLayerVisibility();
  }

  applyLayerVisibility(mesh) {
    const check = (m) => {
      const b = m.userData.bucket;
      if (!b) return;
      if (b.startsWith('marking')) m.visible = this.layers.markings;
      else if (b === 'water') m.visible = this.layers.water;
      else if (b === 'steel' || b === 'railbed') m.visible = this.layers.rail;
    };
    if (mesh) check(mesh);
    else if (this.net) this.net.traverse((o) => { if (o.isMesh) check(o); });
    if (this.furnitureGroup) this.furnitureGroup.visible = this.layers.furniture;
  }

  buildFurniture(f) {
    this.furnitureGroup = new THREE.Group();
    this.net.add(this.furnitureGroup);
    const dummy = new THREE.Object3D();

    // lamps: pole + arm + head
    if (f.lamps.length) {
      const pole = new THREE.CylinderGeometry(0.09, 0.13, 8.5, 6); pole.translate(0, 4.25, 0);
      const arm = new THREE.BoxGeometry(0.12, 0.12, 2.2); arm.translate(0, 8.3, 1.0);
      const head = new THREE.BoxGeometry(0.3, 0.14, 0.9); head.translate(0, 8.25, 2.0);
      const geo = mergeGeometries([pole, arm, head]);
      const mat = new THREE.MeshStandardMaterial({ color: 0x3c4044, roughness: 0.6, metalness: 0.4 });
      const inst = new THREE.InstancedMesh(geo, mat, f.lamps.length);
      f.lamps.forEach((l, i) => {
        dummy.position.set(l.x, l.y, l.z); dummy.rotation.set(0, l.rot, 0); dummy.scale.setScalar(1);
        dummy.updateMatrix(); inst.setMatrixAt(i, dummy.matrix);
      });
      this.furnitureGroup.add(inst);
    }
    // bollards
    if (f.bollards.length) {
      const geo = new THREE.CylinderGeometry(0.09, 0.11, 0.9, 8); geo.translate(0, 0.45, 0);
      const mat = new THREE.MeshStandardMaterial({ color: 0x2f3336, roughness: 0.5, metalness: 0.5 });
      const inst = new THREE.InstancedMesh(geo, mat, f.bollards.length);
      f.bollards.forEach((p, i) => {
        dummy.position.set(p.x, p.y, p.z); dummy.rotation.set(0, 0, 0); dummy.scale.setScalar(1);
        dummy.updateMatrix(); inst.setMatrixAt(i, dummy.matrix);
      });
      this.furnitureGroup.add(inst);
    }
    // guardrail posts + rails (rails join near-neighbour posts ≤4.5m)
    if (f.posts.length) {
      const geo = new THREE.BoxGeometry(0.14, 0.75, 0.14); geo.translate(0, 0.37, 0);
      const mat = new THREE.MeshStandardMaterial({ color: 0x6a6e72, roughness: 0.6, metalness: 0.5 });
      const inst = new THREE.InstancedMesh(geo, mat, f.posts.length);
      f.posts.forEach((p, i) => {
        dummy.position.set(p.x, p.y, p.z); dummy.rotation.set(0, 0, 0); dummy.scale.setScalar(1);
        dummy.updateMatrix(); inst.setMatrixAt(i, dummy.matrix);
      });
      this.furnitureGroup.add(inst);
      const cell = 5;
      const grid = new Map();
      f.posts.forEach((p, i) => {
        const k = Math.floor(p.x / cell) + ',' + Math.floor(p.z / cell);
        if (!grid.has(k)) grid.set(k, []);
        grid.get(k).push(i);
      });
      const rails = [];
      const used = new Set();
      for (let i = 0; i < f.posts.length; i++) {
        let best = -1, bestD = 4.6;
        const cx = Math.floor(f.posts[i].x / cell), cz = Math.floor(f.posts[i].z / cell);
        for (let gx = cx - 1; gx <= cx + 1; gx++) {
          for (let gz = cz - 1; gz <= cz + 1; gz++) {
            const bucket = grid.get(gx + ',' + gz);
            if (!bucket) continue;
            for (const j of bucket) {
              if (i === j || used.has(j * 1000000 + i)) continue;
              const d = Math.hypot(f.posts[i].x - f.posts[j].x, f.posts[i].z - f.posts[j].z);
              if (d < bestD && Math.abs(f.posts[i].y - f.posts[j].y) < 1.2) { bestD = d; best = j; }
            }
          }
        }
        if (best >= 0) { rails.push([f.posts[i], f.posts[best]]); used.add(i * 1000000 + best); }
      }
      if (rails.length) {
        const rg = new THREE.BoxGeometry(0.08, 0.32, 1);
        const rm = new THREE.MeshStandardMaterial({ color: 0xb9bec4, roughness: 0.35, metalness: 0.7 });
        const rinst = new THREE.InstancedMesh(rg, rm, rails.length);
        rails.forEach(([a, b], i) => {
          const mx = (a.x + b.x) / 2, mz = (a.z + b.z) / 2;
          const len = Math.hypot(b.x - a.x, b.z - a.z);
          dummy.position.set(mx, (a.y + b.y) / 2 + 0.62, mz);
          dummy.rotation.set(0, Math.atan2(b.x - a.x, b.z - a.z), 0);
          dummy.scale.set(1, 1, len);
          dummy.updateMatrix(); rinst.setMatrixAt(i, dummy.matrix);
        });
        this.furnitureGroup.add(rinst);
      }
    }
    // sleepers
    if (f.sleepers.length) {
      const geo = new THREE.BoxGeometry(2.3, 0.12, 0.26);
      const mat = new THREE.MeshStandardMaterial({ color: 0x4a3a2a, roughness: 1 });
      const inst = new THREE.InstancedMesh(geo, mat, f.sleepers.length);
      f.sleepers.forEach((s, i) => {
        dummy.position.set(s.x, s.y + 0.06, s.z); dummy.rotation.set(0, s.rot, 0); dummy.scale.setScalar(1);
        dummy.updateMatrix(); inst.setMatrixAt(i, dummy.matrix);
      });
      this.furnitureGroup.add(inst);
    }
    // speed tables
    const tables = f.signs.filter((s) => s.kind === 'bump-table');
    if (tables.length) {
      const geo = new THREE.BoxGeometry(1, 0.09, 2.2); geo.translate(0, 0.045, 0);
      const mat = new THREE.MeshStandardMaterial({ color: 0x3a3d40, roughness: 0.95 });
      const inst = new THREE.InstancedMesh(geo, mat, tables.length);
      tables.forEach((s, i) => {
        dummy.position.set(s.x, s.y + 0.02, s.z); dummy.rotation.set(0, s.rot, 0);
        dummy.scale.set(s.width || 6, 1, 1);
        dummy.updateMatrix(); inst.setMatrixAt(i, dummy.matrix);
      });
      this.furnitureGroup.add(inst);
    }
    // bus shelters (groups — few)
    const shelterMat = new THREE.MeshStandardMaterial({ color: 0x2e6f8e, roughness: 0.6 });
    const roofMat = new THREE.MeshStandardMaterial({ color: 0x37414a, roughness: 0.5, metalness: 0.3 });
    for (const s of f.busStops.slice(0, 400)) {
      const grp = new THREE.Group();
      const roof = new THREE.Mesh(new THREE.BoxGeometry(4.2, 0.12, 1.8), roofMat);
      roof.position.y = 2.5; grp.add(roof);
      for (const px of [-1.9, 1.9]) {
        const post = new THREE.Mesh(new THREE.BoxGeometry(0.1, 2.5, 0.1), shelterMat);
        post.position.set(px, 1.25, 0.7); grp.add(post);
      }
      const bench = new THREE.Mesh(new THREE.BoxGeometry(3.6, 0.08, 0.4), shelterMat);
      bench.position.set(0, 0.55, 0.5); grp.add(bench);
      grp.position.set(s.x, s.y, s.z); grp.rotation.y = s.rot;
      this.furnitureGroup.add(grp);
    }
    // railway level crossings
    for (const c of f.railGear.slice(0, 200)) {
      const grp = new THREE.Group();
      const postMat = new THREE.MeshStandardMaterial({ color: 0xd8d8d8, roughness: 0.6 });
      const redMat = new THREE.MeshStandardMaterial({ color: 0xc22e2e, roughness: 0.5 });
      for (const side of [-1, 1]) {
        for (const along of [-1, 1]) {
          const post = new THREE.Mesh(new THREE.BoxGeometry(0.14, 3.4, 0.14), postMat);
          post.position.set(side * (c.width / 2 + 1.2), 1.7, along * 3.2);
          grp.add(post);
          const buck1 = new THREE.Mesh(new THREE.BoxGeometry(1.4, 0.18, 0.06), postMat);
          buck1.position.set(side * (c.width / 2 + 1.2), 3.1, along * 3.2); buck1.rotation.z = 0.6; grp.add(buck1);
          const buck2 = buck1.clone(); buck2.rotation.z = -0.6; grp.add(buck2);
          const lamp = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.5, 0.2), redMat);
          lamp.position.set(side * (c.width / 2 + 1.2), 2.5, along * 3.2); grp.add(lamp);
        }
        const arm = new THREE.Mesh(new THREE.BoxGeometry(c.width / 2 + 0.6, 0.12, 0.12), redMat);
        arm.position.set(side * (c.width / 4 + 0.6), 1.3, side * 3.2);
        arm.rotation.y = Math.PI / 2;
        grp.add(arm);
      }
      // timber deck panels between rails
      const deck = new THREE.Mesh(new THREE.BoxGeometry(c.width, 0.1, 2.6),
        new THREE.MeshStandardMaterial({ color: 0x6b5a44, roughness: 1 }));
      deck.position.y = 0.1; grp.add(deck);
      grp.position.set(c.x, c.y, c.z); grp.rotation.y = c.rot;
      this.furnitureGroup.add(grp);
    }
  }

  // ------------------------------------------------------------- selection
  setSelected(kind, id) {
    this.selected = { kind, id };
    this.refreshSelection();
  }

  refreshSelection() {
    if (this.highlight) { this.net?.remove(this.highlight); this.highlight = null; }
    for (const h of this.handles) this.net?.remove(h);
    this.handles = [];
    if (!this.selected.id || !this.net) return;
    const proxy = this.proxyMeshes.find((m) => m.userData.pick.id === this.selected.id);
    if (proxy) {
      this.highlight = new THREE.Mesh(proxy.geometry,
        new THREE.MeshBasicMaterial({ color: 0xff9a2e, transparent: true, opacity: 0.35, depthWrite: false, side: THREE.DoubleSide }));
      this.highlight.position.y = 0.15;
      this.net.add(this.highlight);
    }
    // control-point handles for edges
    if (this.selected.kind === 'edge' && this.graph) {
      const e = this.graph.edges.get(this.selected.id);
      if (e) {
        const geo = new THREE.SphereGeometry(1.6, 12, 10);
        e.control.forEach((p, i) => {
          const m = new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ color: 0xffb020, depthTest: false, transparent: true, opacity: 0.95 }));
          m.position.set(p.x, p.y + 1.2, p.z);
          m.renderOrder = 999;
          m.userData.handle = { edge: e.id, index: i };
          this.net.add(m);
          this.handles.push(m);
        });
      }
    }
    // draw-mode preview refresh
    this.refreshDrawPreview();
  }

  refreshDrawPreview() {
    if (this.drawPreview) {
      this.scene.remove(this.drawPreview);
      this.drawPreview.traverse((o) => {
        if (o.geometry) o.geometry.dispose();
        if (o.material) (Array.isArray(o.material) ? o.material : [o.material]).forEach((m) => m.dispose());
      });
      this.drawPreview = null;
    }
    if (this.mode !== 'draw' || this.drawPoints.length === 0) return;
    const grp = new THREE.Group();
    const mat = new THREE.LineBasicMaterial({ color: 0xffb020 });
    const pts = this.drawPoints.map((p) => new THREE.Vector3(p.x, p.y + 1, p.z));
    if (pts.length > 1) grp.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts), mat));
    const sgeo = new THREE.SphereGeometry(1.4, 10, 8);
    const smat = new THREE.MeshBasicMaterial({ color: 0xffb020, depthTest: false, transparent: true });
    for (const p of pts) {
      const s = new THREE.Mesh(sgeo, smat); s.position.copy(p); s.renderOrder = 999; grp.add(s);
    }
    this.drawPreview = grp;
    this.scene.add(grp);
  }

  // ------------------------------------------------------------- input
  bindInput() {
    const el = this.renderer.domElement;
    el.tabIndex = 0;
    this.looking = false; this.dragging = null; this.downPos = null;

    this._onKeyDown = (e) => {
      if (e.target.matches('input,textarea,select')) return;
      this.keys.add(e.code);
      if (e.code === 'KeyF') this.focusSelection();
      if (e.code === 'Enter' && this.mode === 'draw' && this.drawPoints.length >= 2) {
        e.preventDefault();
        this.cb.onCommitDraw?.([...this.drawPoints]);
        this.drawPoints = []; this.refreshDrawPreview();
      }
      if (e.code === 'Escape') {
        if (this.mode === 'draw' && this.drawPoints.length) { this.drawPoints = []; this.refreshDrawPreview(); }
        else this.cb.onSelect?.(null, null);
      }
      if (e.code === 'Backspace' && this.mode === 'draw' && this.drawPoints.length) {
        this.drawPoints.pop(); this.refreshDrawPreview(); e.preventDefault();
      }
      if (['KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE', 'Space'].includes(e.code)) e.preventDefault();
    };
    window.addEventListener('keydown', this._onKeyDown);
    this._onKeyUp = (e) => this.keys.delete(e.code);
    window.addEventListener('keyup', this._onKeyUp);

    el.addEventListener('contextmenu', (e) => e.preventDefault());
    el.addEventListener('pointerdown', (e) => {
      el.focus();
      this.downPos = { x: e.clientX, y: e.clientY };
      if (e.button === 2) { this.looking = true; el.setPointerCapture(e.pointerId); return; }
      if (e.button !== 0) return;
      // handle drag?
      this.setPointer(e);
      this.ray.setFromCamera(this.pointer, this.camera);
      const hits = this.ray.intersectObjects(this.handles, false);
      if (hits.length) {
        this.dragging = hits[0].object.userData.handle;
        el.setPointerCapture(e.pointerId);
      }
    });
    el.addEventListener('pointermove', (e) => {
      if (this.looking) {
        this.yaw += e.movementX * 0.0026;
        if (this.yaw > Math.PI) this.yaw -= Math.PI * 2;
        else if (this.yaw < -Math.PI) this.yaw += Math.PI * 2;
        this.pitch = Math.max(-1.45, Math.min(1.2, this.pitch - e.movementY * 0.0026));
        this.lookFromAngles();
        return;
      }
      if (this.dragging) {
        this.setPointer(e);
        this.ray.setFromCamera(this.pointer, this.camera);
        const plane = new THREE.Plane(new THREE.Vector3(0, 1, 0), -(this.graph?.edges.get(this.dragging.edge)?.control[this.dragging.index]?.y ?? 0));
        const hit = new THREE.Vector3();
        if (this.ray.ray.intersectPlane(plane, hit)) {
          const h = this.handles[this.dragging.index];
          if (h) h.position.set(hit.x, h.position.y, hit.z);
          this.dragPos = { x: hit.x, z: hit.z };
        }
        return;
      }
    });
    el.addEventListener('pointerup', (e) => {
      if (e.button === 2) { this.looking = false; return; }
      if (e.button !== 0) return;
      if (this.dragging) {
        const d = this.dragging;
        this.dragging = null;
        if (this.dragPos) {
          this.cb.onEditPoint?.(d.edge, d.index, { ...this.dragPos });
          this.dragPos = null;
        }
        return;
      }
      // click select / draw (ignore drags)
      const moved = this.downPos ? Math.hypot(e.clientX - this.downPos.x, e.clientY - this.downPos.y) : 99;
      if (moved > 5) return;
      this.setPointer(e);
      this.ray.setFromCamera(this.pointer, this.camera);
      if (this.mode === 'draw') {
        const plane = new THREE.Plane(new THREE.Vector3(0, 1, 0), -this.drawHeight);
        const hit = new THREE.Vector3();
        if (this.ray.ray.intersectPlane(plane, hit)) {
          this.drawPoints.push({ x: Math.round(hit.x * 10) / 10, y: this.drawHeight, z: Math.round(hit.z * 10) / 10 });
          this.refreshDrawPreview();
          this.cb.onDrawProgress?.(this.drawPoints.length);
        }
        return;
      }
      const hits = this.ray.intersectObjects(this.proxyMeshes, false);
      if (hits.length) {
        const { kind, id } = hits[0].object.userData.pick;
        this.cb.onSelect?.(kind, id);
      } else {
        this.cb.onSelect?.(null, null);
      }
    });
    el.addEventListener('wheel', (e) => {
      e.preventDefault();
      const dir = new THREE.Vector3();
      this.camera.getWorldDirection(dir);
      if (e.deltaY < 0) { this.speed = Math.min(1200, this.speed * 1.18); this.camera.position.addScaledVector(dir, this.speed * 0.12); }
      else { this.speed = Math.max(2, this.speed / 1.18); this.camera.position.addScaledVector(dir, -this.speed * 0.12); }
      this.cb.onSpeed?.(this.speed);
    }, { passive: false });
    el.addEventListener('dblclick', () => this.focusSelection());
  }

  setPointer(e) {
    const r = this.renderer.domElement.getBoundingClientRect();
    this.pointer.set(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1);
  }

  lookFromAngles() {
    const d = new THREE.Vector3(
      Math.cos(this.pitch) * Math.cos(this.yaw),
      Math.sin(this.pitch),
      Math.cos(this.pitch) * Math.sin(this.yaw)
    );
    this.camera.lookAt(this.camera.position.clone().add(d));
  }

  focusSelection() {
    let target = new THREE.Vector3(0, 0, 0);
    if (this.selected.id && this.graph) {
      if (this.selected.kind === 'edge') {
        const e = this.graph.edges.get(this.selected.id);
        if (e?.polyline.length) {
          const p = e.polyline[Math.floor(e.polyline.length / 2)];
          target.set(p.x, p.y, p.z);
        }
      } else {
        const n = this.graph.nodes.get(this.selected.id);
        if (n) target.set(n.pos.x, n.pos.y, n.pos.z);
      }
    }
    const d = new THREE.Vector3();
    this.camera.getWorldDirection(d);
    this.camera.position.copy(target).addScaledVector(d, -320).add(new THREE.Vector3(0, 180, 0));
    const nd = target.clone().sub(this.camera.position).normalize();
    this.pitch = Math.asin(Math.max(-1, Math.min(1, nd.y)));
    this.yaw = Math.atan2(nd.z, nd.x);
    this.lookFromAngles();
  }

  focusAll() {
    this.camera.position.set(2600, 1500, 2600);
    this.yaw = Math.atan2(-2600, -2600);
    this.pitch = -0.42;
    this.lookFromAngles();
  }

  setLayers(l) { this.layers = { ...l }; this.applyLayerVisibility(); }
  setMode(m) { this.mode = m; this.drawPoints = []; this.refreshDrawPreview(); }

  resize() {
    const w = this.container.clientWidth || 800, h = this.container.clientHeight || 600;
    this.renderer.setSize(w, h);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  }

  loop() {
    if (this.dead) return;
    this.raf = requestAnimationFrame(() => this.loop());
    const dt = Math.min(this.clock.getDelta(), 0.1);
    // fly
    const boost = (this.keys.has('ShiftLeft') || this.keys.has('ShiftRight')) ? 4 : 1;
    const slow = (this.keys.has('AltLeft') || this.keys.has('ControlLeft')) ? 0.25 : 1;
    const v = this.speed * boost * slow * dt;
    const fwd = new THREE.Vector3(); this.camera.getWorldDirection(fwd);
    const right = new THREE.Vector3().crossVectors(fwd, new THREE.Vector3(0, 1, 0)).normalize();
    const up = new THREE.Vector3(0, 1, 0);
    if (this.keys.has('KeyW')) this.camera.position.addScaledVector(fwd, v);
    if (this.keys.has('KeyS')) this.camera.position.addScaledVector(fwd, -v);
    if (this.keys.has('KeyD')) this.camera.position.addScaledVector(right, v);
    if (this.keys.has('KeyA')) this.camera.position.addScaledVector(right, -v);
    if (this.keys.has('KeyE') || this.keys.has('Space')) this.camera.position.addScaledVector(up, v);
    if (this.keys.has('KeyQ') || this.keys.has('KeyC')) this.camera.position.addScaledVector(up, -v);
    this.renderer.render(this.scene, this.camera);
    this.frames++; this.fpsT += dt;
    if (this.fpsT >= 0.5) {
      this.fps = Math.round(this.frames / this.fpsT);
      this.frames = 0; this.fpsT = 0;
      this.cb.onStats?.({ fps: this.fps, calls: this.renderer.info.render.calls, tris: this.renderer.info.render.triangles });
    }
  }

  dispose() {
    this.dead = true;
    cancelAnimationFrame(this.raf);
    window.removeEventListener('keydown', this._onKeyDown);
    window.removeEventListener('keyup', this._onKeyUp);
    this.ro?.disconnect();
    if (this.renderer.domElement.parentElement === this.container) {
      this.container.removeChild(this.renderer.domElement);
    }
    this.renderer.dispose();
  }
}
