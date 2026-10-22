# Geometry Division — HTML ReSTIR engine to C++

## Purpose

Port the proven HTML ReSTIR engine (`Experimental/VehicleHmiPrototype` on `arena/e9bd7db5-frontier`,
opened through the Frontier application shell) into production C++ under `Engine/`, in two divisions:

1. **Geometry division** — the virtualized-geometry kernel. CPU, C++20, standard library only, provable headlessly.
2. **Render division** — Vulkan + Slang + GLFW consumption of that geometry, ending with ReSTIR GI on screen.

The HTML implementation is the behavioural reference, not code to transliterate. Naming follows
`EngineDocs/AgenticInstuctions/SKILL-Naming.md`: `Kernel`, `Mesh`, `Node`, `Graph`, `Hierarchy`,
`Parent`/`Child` and `Pass` are barred identifiers, so the divisions are named and the links are
`CoarserClusterIndex` / `FinerClusterIndices` / `EnclosedClusters`.

## Source of truth

| Reference | What it pins down |
|---|---|
| `Experimental/VehicleHmiPrototype/Source/GeometryHierarchy.js` | weld, adjacency, cluster partition, QEM tier simplification, monotonic deviation |
| `Experimental/VehicleHmiPrototype/Source/GeometryHierarchyRuntime.js` | tier cut, page residency, Hi-Z pyramid (reversed Z) |
| `Experimental/VehicleHmiPrototype/Source/VisibilityResolve.js` | visibility field, deferred resolve, two-layer glass refraction |
| `Experimental/VehicleHmiPrototype/Source/RayTracingSpace.js` | binned-SAH bottom-level structure, per-frame top-level structure |
| `Experimental/VehicleHmiPrototype/Source/ResamplingSequence.js` + `Shader/Reservoir*.wgsl` | ReSTIR GI: 32-byte reservoir, temporal reprojection, spatial reuse |
| `Experimental/VehicleHmiPrototype/Shader/RayTraversal.wgsl` | path tracing, RIS, NEE, MIS, Russian roulette |
| `Experimental/VehicleHmiPrototype/GEOMETRY_HIERARCHY.md` | decisions, guarantees, per-level counts |

## Division 1 — Geometry (`Engine/Geometry/`)

| Step | Deliverable | Acceptance | Status |
|---|---|---|---|
| A0 | Port the geometry kernel into this branch, wire CMake and ctest | Builds under g++ and MSVC; tests registered | Implemented |
| A1 | `FacetField` → exact-position weld → `PolygonComplex`; edge incidence; UV-seam and sharp classification; `FacetCluster` partition (≤128 facets) with bounds sphere and normal cone; `ValidateClusterBuild` | Coverage exact, caps respected, connectivity, class purity, containment | Implemented |
| A2 | `QuadricDeviation` + `EdgeContraction` simplification with locked borders; cluster grouping; `ClusterTierSequence` with monotonic `Deviation` and enclosure bounds; `ValidateTierSequence` | Sequence invariants; seam never crossed; rim stays planar | Implemented |
| A3 | `TierCutSelection` (projected-deviation antichain), frustum + normal-cone culling, `ClusterPage` packing, `PageVacancyTable` residency, CPU Hi-Z two-phase occlusion | Cut counts match the HTML at 8 / 2 / 0.5 px thresholds | Planned |
| A4 | Deterministic binary tier artifact + TOML index; CPU reference renderer (raster + path trace) as the image oracle | Reference images under `Exhibits/Geometry/` | Planned |
| A5 | Validation expansion, evidence, tracker entries | Numbers and captures retained | In progress |

### A2 implementation notes (what the code actually does)

- Collapse cost is the summed plane quadric of the edge endpoints plus a UV-space length penalty.
- A collapse is refused when it would fold a facet, degenerate one, duplicate a facet, split a sharp edge,
  open a border (border vertices may merge only with other border vertices, at their midpoint), move a
  pinned seam vertex, run away from the edge, or make a facet span the U wrap of its chart.
- A welded vertex carrying two corner UVs is a UV-seam vertex and is pinned; corner UVs of a merge are
  unified so the merged vertex never becomes accidentally two-valued.
- Group borders are locked during simplification, so independently simplified groups never crack.
- Coarser bounds spheres are expanded to enclose every enclosed finer sphere, because the finer sphere is
  fitted to facets the collapse has removed.
- The sequence ends at the fixed point of coarsening; an open mesh with a pinned border settles at a few
  clusters rather than one. Every cluster still respects the 128-facet cap.

## Division 2 — Render (`Engine/Render/`)

| Step | Deliverable | Screen milestone |
|---|---|---|
| B0 | Seed the host from `arena/db2f9953-frontier` (`VulkanPresentation`, window callbacks); allocator, depth, validation layers, `FrontierRender` + `FrontierViewer` | S0: cleared window, three frames, zero validation errors |
| B1 | Slang build: per-entry-point compile with reflection JSON driving bind tables | Shader reload works |
| B2 | Cluster upload, GPU tier cut, indirect draws, visibility buffer + deferred resolve | S1: geometry on screen, LOD switching visibly |
| B3 | Intersection substrate: binned-SAH bottom level + per-frame top level; primary rays; G-buffer | S2: ray-hit shading of the same geometry |
| B4 | ReSTIR GI: 32-byte reservoir, RIS, temporal reprojection, spatial reuse | S3: a ReSTIR frame |
| B5 | SVGF, exposure, tonemap, present | S4: final image |
| B6 | `MaterialResolve` onto the existing 1504-byte `MaterialScattering` contract | Authored materials on the scene |
| B7 | Optional parity slices: atmosphere, distance field + cards, glass, clouds | — |

Seventeen shaders carry S4; the remaining sixty-odd cover simulation, weather and HMI and are out of scope.

## Constraints

- Sandbox: g++ 12.2 only, no CMake, no Vulkan, no GPU. Division 1 is built and proven with g++ directly;
  Division 2 compiles under the existing `FRONTIER_HOST_VERIFICATION` path and runs on Windows.
- Windows binaries remain the delivery target. GitHub Actions need explicit approval.
- No pull request until the user says merge.
