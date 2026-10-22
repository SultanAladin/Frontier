/*====================================================================================================================================
                                                   GEOMETRYKERNELPORT.MD
====================================================================================================================================*/
<!-- 🧩 Geometry Kernel first, Render Engine second — HTML ReSTIR / material-authoring port plan -->

# Geometry Kernel Port — Plan

## Objective

Convert the HTML ReSTIR / material-authoring game engine to C++ in two ordered tranches:

1. **Geometry Kernel** — the neutral authoring foundation every other pillar reads.
2. **Render Engine** — the Vulkan / Slang present that shades the kernel's presentation.

Once both land, render a first triangle / Suzanne to the screen as the Phase-3 gate.
Existing C++ under `Internal/` is **reused and repaired**, not blindly replaced — it already carries
a prior port of the HTML alloy-texturing prototype. Browser instability blocks even local
inspection, so Phase 0 triages the crash before any port work.

Requested scope: commit `06aa448` (download `39682561975`) is believed to be the HTML source.
The working checkout is `SultanAladin/Frontier` at `e016a0b` on `arena/ba09c867-frontier`;
its approved baseline is `SultanAladin/Frontier-@338293863d7`. Whole-tree wholesale replacement
remains out of scope per `Documentation/Plans/Ongoing/EngineUpdate.md`.

---

## Current State

| Area | Reality | Implication |
| :--- | :--- | :--- |
| `CMakeLists.txt` | Builds only `Engine/Interface` + `UI/Editor` + `Exhibits/` | `Internal/` is present on disk but **not linked** by the deliverable target |
| `Engine/Render/` | Blank (`README.md` only) | No scene renderer in the shipped binary |
| `Internal/Authoring/Geometry/` | 14 files ported: `VertexField`, `PolygonCluster`, `PolygonDescriptor`, `FaceTriangulation`, `DisplayPolygonAssembly`, `AdjacencyIndex`, `ComponentSelection`, `ComponentUVSurface`, `Interchange*`, `PrimitiveShape`, `CalibrationSpecimen` + `GeometryExtension` | Foundation exists; needs audit against HTML source before extending |
| `Internal/Authoring/PolygonAuthoring/` | Extrude / Bevel / Inset / LoopCut / Deform / TopologyEdit + Modifiers | Modelling ops present; may need rehook after kernel audit |
| `Internal/Authoring/ParametricAuthoring/` | `ParametricSketchShapeStore` (187 kLOC), `ConstraintSolver`, `Workplane`, Boolean/Fillet/Loft/Transform | Large prior port; treat as reuse candidate, not rewrite target |
| `Internal/Graphics/` | `RenderExtension` (220 kLOC header+impl), `Render/Surface`, `Acceleration`, `Surfel`, `Shadow/SunShadowClipmap`, etc. | Full renderer retained but **untouched** by current CMake target |
| `Documentation/Prototypes/PaintingSurface/` | HTML / JS / WGSL material authoring prototype: `WavefrontDecode.js`, `SurfaceUpload.js`, `SurfaceRasterization.js`, `PaintPass.js`, `LayerStack.js`, `LayerComposite.js`, `GeneratorPass.js`, etc. | Canonical HTML source for the geometry → paint → composite → raster loop |
| `Documentation/Prototypes/BattlezoneReconstruction/` | JS/WGSL scene with `ChassisGeometry.js`, `CombatSimulation.js` | Secondary HTML source; scope to geometry kernel only |
| `Exhibits/Diagnostics/ProjectZero_Telemetry*` | ReSTIR renderer telemetry: 229 k tris, 499 instances, 81–98% ReSTIR GPU share | Confirms a prior ReSTIR path exists outside the current build |
| Browser | Reporter: project browser crashes, blocks inspection | Gate — cannot validate HTML source or prototype renders until stable |

---

## Phase 0 — Triage the Browser Crash

No port claim is credible while the authoring surface cannot be opened.

### Suspected Classes

| Candidate | Signal | Cheap Probe |
| :--- | :--- | :--- |
| WebGPU atlas pressure | `LayerStack.js` caps 12 layers × 3 atlases × 1024² RGBA8 = 144 MiB + masks | Open `PaintingSurface.html` with Task Manager; watch GPU memory; single-layer boot |
| WGSL shader error | `PaintPass.js` DBL + `SurfaceRasterization.js` UV→clip trick | Launch via `PaintingSurface.bat` (serves over HTTP; `file://` is CORS-blocked per `RockFormationNotes.md`) and read the Fault panel |
| Prototype JS exception | `WavefrontDecode.js` corner-key registry, `StrokeRecord.js` ledger | Open DevTools before load; reproduce with `Suzanne.obj` only |
| External: download `39682561975` mismatch | Commit `06aa448` not in this checkout | Verify SHA via `git log --all` on the source repository, not this checkout |

### Exit Criteria

- [ ] Crash reproduced with console / Fault text captured under `Exhibits/Diagnostics/`.
- [ ] Root cause filed in `Documentation/Tracker.md` as ID with `Blocked` → `Open`.
- [ ] Prototype serves via HTTP and reaches `ProbeReport.Stage = ready` for at least one model.

---

## Phase 1 — Geometry Kernel Port

The kernel is the neutral shared foundation. Every authoring library reads it; none owns it.

### 1.1 Inventory

| HTML Source | C++ Destination (reuse) | Verdict |
| :--- | :--- | :--- |
| `Ingest/WavefrontDecode.js` (corner-key, fan triangulate, seam UV) | `Interchange/InterchangeWavefrontDecoder.cpp` + `FaceTriangulation.*` | Audit parity: negative indices, n-gon fan, corner vs vertex UV |
| `Ingest/SurfaceUpload.js` (de-indexed triangle arrays, bounds) | `PolygonCluster`, `VertexField`, `PolygonDescriptor` | Already ported; verify `FaceCornerTexture` length invariant |
| `Projection/SurfaceRasterization.js` (camera raster, depth) | `DisplayPolygonAssembly.*`, `TriangleOrigin.*` | Float cast at presentation boundary — confirm double→float rule |
| `Adjacency/Mesh adjacency` | `Adjacency/AdjacencyIndex.*` | Check half-edge vs JS adjacency contract |
| `Selection / Picking` | `Selection/ComponentSelection.*`, `Picking/RayPickIntersection.*` | Validate selection-traversal exempt name retained |
| `UV/` (AtlasGroundTruth, unwrap) | `UV/ComponentUVSurface.*` | Compare pan/zoom affine against JS |
| `Deposits / Layers / Instruments` (paint, masks, generators) | Out of kernel — **deferred** to Phase 2 / Material authoring | Do not pull into `Geometry/` |

### 1.2 Anti-Duplication Gate

Per `EngineDocs/ImportantNotes.md` — run on **every** file before porting:

1. Grep `Internal/` for class / function / struct name.
2. If found, read destination file — confirm same logic under possibly renamed identity (`VectorAlgebra` → `LinearAlgebra_Float64`).
3. **Rewire** call sites / includes; do **not** duplicate.
4. Only if absent: Option A (merge into existing file) or Option B (new file at `FolderStructure.md` home).

Known rehooks:

| Retired | Canonical |
| :--- | :--- |
| `VectorAlgebra.h` | `EngineContext/Math/LinearAlgebra_Float64.h` (`Vector2`→`Vector2d`, `Vector3`→`Vector3d`, `namespace Frontier`) |
| `IntersectionSolver.h` | `EngineContext/Math/IntersectionSolver.h` (already namespaced) |
| `WorkerPool.h` | `Platform/Concurrency/WorkerPool.h` |

### 1.3 Work Packages

| ID | Package | Files Touched | Verification |
| :--- | :--- | :--- | :--- |
| G1 | Wavefront + Interchange parity | `InterchangeWavefrontDecoder.*` vs `WavefrontDecode.js` | Round-trip `Suzanne.obj`: vertex / face / UV counts + bounds within `1e-9` |
| G2 | PolygonCluster invariant repair | `PolygonCluster.*`, `VertexField.*` | `EvaluatePolygonDescriptor` + `ConstructRenderVertexStream` false on degenerate face, true otherwise |
| G3 | Face triangulation hardening | `FaceTriangulation.*` | Fan vs ear-clip on quad / n-gon specimen; no index out of range |
| G4 | Display assembly contract | `DisplayPolygonAssembly.*`, `TriangleOrigin.*` | Stride-32 `RenderVertex` layout matches shader vertex input; float cast only at boundary |
| G5 | Adjacency + Selection audit | `AdjacencyIndex.*`, `ComponentSelection.*` | Selection predicate vs JS; lateral-walk returns same edge set |
| G6 | UV surface audit | `ComponentUVSurface.*` | UV → pixel affine at 3 zoom/pan samples vs JS oracle |
| G7 | Interchange bake (obj/gltf/fbx → wsdoc) | `WorkspaceDocumentEncoder.*`, `InterchangeGltfDecoder.*`, `InterchangeFilmboxDecoder.*` | `WorkspaceDocumentWriter` validation still passes |
| G8 | GeometryExtension lifecycle | `GeometryExtension.*` | `Initialize` → `Advance` × N → `Finalize`; revision monotonic |

### 1.4 Completion Gate

- [ ] All G1–G8 pass `Build.bat` for `AuthoringGeometry.lib` on Windows (MSVC).
- [ ] `Exhibits/GeometryKernel/` holds retained evidence: counts, bounds diff, image of wireframe.

---

## Phase 2 — Render Engine Port

### 2.1 Scope Boundary

| In | Out (deferred) |
| :--- | :--- |
| `SurfaceRasterization.js` → Vulkan/Slang surface pass | Volumetric cloud / fog (separate plans) |
| `PaintPass.js` atlas trick (UV→clip) → paint bake target | Full ReSTIR GI re-tune (needs TAA — see `PLAN-ClusteredLightCulling.md`) |
| `LayerComposite.js` / `ChannelSet.js` → channel composite | Audio (no source, plan only) |
| `OrbitProjection.js` → camera + `Navigation/Camera/` | Terrain field (blocked on `FlowNetwork`) |
| `Grid / Radiance / Surface` pillars already in `Internal/Graphics/` | RockFormation voxel prototype |

### 2.2 Reuse Decision

| Existing C++ | Action |
| :--- | :--- |
| `Internal/Graphics/RenderExtension.*` + `Render/Surface/*` | Audit first: reuse device / submission / viewport scaffolding; do **not** re-port `VulkanHost`, `WindowSubstrate` |
| `Internal/Graphics/Surfel/`, `Acceleration/`, `Shadow/` | Keep — they are not replaced by the HTML ReSTIR path; wire only if HTML source proves newer |
| `Engine/Render/` (currently blank) | New `Render/` target lives under `Engine/Render/` per `Documentation/FolderStructure.md`; migrate from `Internal/Graphics/Render/` after review — no new top-level folder |

Render-unit suffix per `SKILL-Naming.md` §2:

| Mechanism | Suffix |
| :--- | :--- |
| Rasterizes geometry into targets (including overlays onto existing target) | `…Rasterization` |
| Records + submits batched GPU workload | `…Submission` |
| Ordered multi-step chain | `…Sequence` |

### 2.3 Work Packages

| ID | Package | Verification |
| :--- | :--- | :--- |
| R1 | VulkanHost / WindowSubstrate reuse audit | Existing validation target still builds; swapchain image acquired |
| R2 | Surface presentation → `Radiance` target + resolve | Linear HDR target + `PBR Neutral` resolve only; no double tone-map |
| R3 | Camera + Grid (analytic ground plane) | Perspective + ortho; depth-test via sampled scene depth |
| R4 | Atlas / channel composite (material authoring) | 1024² bake matches JS `LayerComposite` pixel checksum on test atlas |
| R5 | Light + shadow wiring (reuse `SunShadowClipmap`) | Single punctual light + shadow page census non-zero |
| R6 | Exhibit host that draws Suzanne | `SketchModelViewport`-style host builds and presents one frame |

### 2.4 Completion Gate

- [ ] `FrontierRender` target builds under `Engine/Render/` and is linked by a new `Executables/RenderValidation/` host.
- [ ] Host opens a GLFW window (Vulkan, no OpenGL context) and renders Suzanne to the swapchain.
- [ ] Capture under `Exhibits/RenderPort/` proves the screen is not blank.

---

## Phase 3 — Render Something to Screen

The minimal visible proof that Phases 1–2 are wired:

```
Suzanne.obj → WavefrontDecode → PolygonCluster → RenderVertexStream
            → VulkanHost → Radiance (R16G16B16A16_SFLOAT) → Resolve (PBR Neutral)
            → GLFW swapchain (sRGB, FIFO)
```

Exit criteria:

- [ ] Window appears; Suzanne centred; resize does not crash.
- [ ] Frame timing captured (CPU + GPU) — even a single `GpuTimestampScope` sample counts.
- [ ] Evidence retained: screenshot, build log, revision under `Exhibits/RenderPort/`.

---

## Risks and Guards

| Risk | Guard |
| :--- | :--- |
| Re-porting `VectorAlgebra` as a second copy | Anti-duplication gate + `LinearAlgebra_Float64` rehook contract |
| HTML source at `06aa448` not matching checkout | Fetch source repo; pin SHA before claiming parity |
| Browser crash hides HTML truth | Phase 0 triage; serve over HTTP; capture `ProbeReport` |
| Naming drift (`Mesh`, `Node`, `Backend`, `Pass`, `Inscription` banned) | `Documentation/AUDIT-NamingScan.md` re-scan after each batch |
| Double tone-map recurrence (`SurfaceShade` + resolve) | Assert one tone-map site: `RadianceResolve` only |
| Whole-tree replacement temptation | `EngineUpdate.md` scope — batches, not wholesale import |

---

## Tracking

- Single tracker: `Documentation/Tracker.md` (no per-agent duplicate).
- This plan lives at `Documentation/Plans/Ongoing/GeometryKernelPort.md` until G1–G8 + R1–R6 + screen proof are green,
  then moves to `Documentation/Plans/Completed/`.

