# Virtualized Geometry — C++ Port Plan

## Purpose

Port the proven HTML virtualized-geometry implementation (`Experimental/VehicleHmiPrototype/`,
Phases 1–3 + cluster debug views, Display modes 10–14) into production C++ under `Engine/`, in two
divisions built in order: first the geometry build/runtime kernel (CPU, fully testable headlessly),
then the Vulkan render engine that consumes it, ending with geometry on screen. Concepts only —
the HTML implementation is the behavioural reference, not code to transliterate line by line.

## Source of truth (reference behaviour)

| Reference | What it pins down |
|---|---|
| `Experimental/VehicleHmiPrototype/Source/GeometryHierarchy.js` | Build: weld, adjacency, cluster partition, QEM tier simplification, monotonic error |
| `Experimental/VehicleHmiPrototype/Source/GeometryHierarchyRuntime.js` | Runtime: projected-error tier cut, page residency, Hi-Z pyramid (reversed-Z) |
| `Experimental/VehicleHmiPrototype/Source/VisibilityResolve.js` | Visibility field, deferred resolve, two-layer glass refraction |
| `Experimental/VehicleHmiPrototype/GEOMETRY_HIERARCHY.md` | Decisions, guarantees, evidence numbers (level progressions, cut triangle counts) |
| HTML test suites (237/237) | Numeric expectations the C++ validation must reproduce |

## Naming map (HTML term → C++ name, per SKILL-Naming gates)

| HTML concept | C++ name | Mechanism it names |
|---|---|---|
| Facet soup input | `FacetField` | Flat per-corner facet arrays |
| Welded indexed mesh | `PolygonComplex` | Approved geometry noun |
| Meshlet (≤128 tri) | `FacetCluster` | Bounded facet grouping (`VertexCluster` family) |
| LOD DAG | `ClusterTierSequence` | Clusters ordered by `DepthTier`; links are indices, never kinship terms |
| Coarser/finer link | `CoarserClusterIndex` / `FinerClusterIndices` | Enclosure direction stated geometrically |
| QEM | `QuadricDeviation` + `EdgeContraction` | Quadric error evaluation; edge contraction step |
| Geometric error | `Deviation` (metres), `ProjectedDeviation` (px) | Monotonic per cluster, enclosure deviation ≥ enclosed |
| Page / slot allocator | `ClusterPage` / `PageVacancyTable` | Fixed-size pages; LRU slot recycling, pinned immune |
| Runtime cut | `TierCutSelection` | Antichain: self projected deviation ≤ threshold < enclosure deviation |
| Visibility buffer | `VisibilityField` | Cluster-triangle identity + barycentrics per layer |
| Glass | `TransmissiveField` (front/back layers) | Own visibility class, no depth write, refraction gather |

Banned in C++ identifiers (per skill): Mesh, Node, Graph, Hierarchy, Parent/Child, Family, Kernel,
Manager, Record (bare), Pass, Stage. (The HTML names stay in the prototype; they do not cross over.)

## Division 1 — Geometry kernel (`Engine/Geometry/`), CPU, C++20, stdlib only

| Step | Deliverable | Acceptance (all headless, run in sandbox + Windows host verification) |
|---|---|---|
| K1a | `FacetField` input, exact-position weld → `PolygonComplex`, edge adjacency, UV-seam + sharp-edge classification, `FacetCluster` partition (≤128 tri, connected), bounds sphere + normal cone, `ValidateClusterBuild` | Invariants hold on generated corpora (sphere, heightfield cliff, UV-creased vessel, ground grid): coverage exact, caps respected, seams/sharp counts match HTML reference |
| K1b | Grouping + `EdgeContraction` QEM simplification with locked shared borders (position **and** UV), sharp/UV-seam locks, tier construction with additive monotonic `Deviation`, enclosure bounds = union of enclosed | Level progressions reproduce HTML: sphere 5120→2558→1280→640→320→160→80 (1 coarsest cluster set), vessel sharp vertices stay on crease planes, no triangle crosses the UV meridian seam |
| K2a | `TierCutSelection` (projected-deviation antichain, frustum + normal-cone culling), `ClusterPage` packing + `PageVacancyTable` residency (LRU, page 0 pinned), CPU Hi-Z pyramid + two-phase occlusion | Cut triangle counts at 64/8/1 px match HTML proof numbers; cold-residency cut selects only resident clusters and queues missing pages |
| K2b | CPU visibility raster + deferred resolve + two-layer refraction gather (glass never depth-writes, refraction-deviation in glass error) | Reference images under `Exhibits/VirtualizedGeometry/`; glass pixel tests match HTML (`VisibilityResolve` stats) |

## Division 2 — Render engine (`Engine/Render/`), Vulkan + Slang + GLFW (per CLAUDE.md)

| Step | Deliverable | Acceptance |
|---|---|---|
| R1 | GLFW window (no GL context), Vulkan instance/device/swapchain, Slang triangle on screen | Coloured triangle presented; runs on Windows build |
| R2 | Cluster geometry upload, tier-cut compute (Slang port of the WGSL cull), indirect draws, frustum/cone culling | Proving-ground-style grid + a curved static surface drawn **only** via cluster path |
| R3 | Two-phase Hi-Z occlusion, page streaming into arena slots | Occluded clusters culled; `?streampages`-equivalent bound evicts + falls back to coarser resident tiers |
| R4 | Visibility raster + deferred resolve + two-layer glass refraction; debug views equivalent to HTML modes 10–14 (IDs, resolve, Hi-Z, glass layers, LOD levels) | Screen captures in `Exhibits/`; LOD band visibly moves with camera; glass object refracts the resolved opaque scene |

First thing on screen = end of R1/R2 with the K1-built sphere: its silhouette visibly simplifies
with distance — the demonstration the flat HTML proving ground could not give.

## Constraints and honest limits

- Sandbox (this environment): g++ available; **no cmake, no Vulkan, no GPU** — Division 1 is built
  and proven here with g++ directly; Division 2 compiles under the existing
  `FRONTIER_HOST_VERIFICATION` path where possible and is run on Windows by the user.
- No PR until the user says merge (standing instruction). Work stays on `arena/e9bd7db5-frontier`.
- The other agent's branch (`arena/db2f9953-frontier`, head `06aa448`) is **not** merged into this
  one: it diverged at the base commit with ~3000 files changed and does not contain this geometry
  work. The Project Browser crash lives in that branch's Windows build (its own recent commits are
  already chasing a present/acquire `DEVICE_LOST` death window); it cannot be reproduced from this
  Linux sandbox, and blind-fixing it would be guessing — see the separate triage note below.

## Project Browser crash — triage note (branch `arena/db2f9953-frontier`)

- The crash the user hits is in the packaged Windows build tagged `frontier-windows-39682561975`
  (= commit `06aa448` on that branch). That branch's last five commits are instrumentation for
  exactly this: "trace Frontier present/acquire/UI death window", "browser death point",
  "DEVICE_LOST guards" — i.e. the crash is a Vulkan present/acquire device loss around the
  Project Browser preview, still being hunted there.
- Recommendation: that hunt continues on its own branch with the traces it already added (next step
  there: read the trace journal from a crashing run — it was added for precisely that). This branch
  proceeds with the geometry port; when the two lines of work are eventually merged, the geometry
  kernel arrives as new files under `Engine/Geometry/` plus render units under `Engine/Render/`,
  which minimises collision with the browser/editor code.
