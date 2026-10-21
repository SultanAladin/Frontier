# Virtualized geometry — hierarchy build (Phase 1 of the full system)

Status: **geometry build + validation + editor debug view + rendered evidence
complete**. Runtime (projected-error DAG cut, Hi-Z occlusion, visibility
buffer, deferred resolve, transmission) is designed but not yet built; see
"Scope" below. This document exists so the boundary of what exists is never
blurred: `GEOMETRY_CLUSTERING.md` describes the earlier *lossless leaf
clustering* (spatial median splits + frustum cull + indirect draws). That is a
useful culling structure, but it has no LOD, no error metric and no DAG — it
is not virtualized geometry, and nothing here reuses its split heuristic.

The design follows the published *concepts* of cluster-based virtualized
geometry (meshlets, group simplification with locked borders, monotonic-error
DAG). All code is newly written against this prototype's facet-soup mesh
model; no third-party implementation was consulted for code.

## What Phase 1 builds (`Source/GeometryHierarchy.js`)

Input is the prototype's facet soup (`{ Facets: [{ Corners, Family }], FirstVertex }`)
— which is exactly what heightmap-generated terrain and voxelized-SDF-cliff
meshes arrive as, so displaced/heightmap geometry is ingested by baking it
into source triangles before virtualization (no runtime displacement shader
required for the geometry to LOD correctly).

1. **Weld** — exact-position vertex deduplication (normals ignored, so flat
   shading facets still share vertices; this is what makes borders lockable).
2. **Meshlets** — greedy adjacency growth over shared edges (≤128 triangles /
   ≤256 vertices), scored by normal-cone alignment minus a distance term
   normalised by the expected patch radius at the local tessellation scale.
   Material-class partition keys are hard walls growth never crosses.
3. **Group → simplify → recluster (the DAG loop)** — adjacent meshlets are
   grouped (8 at the leaf level, doubling per level, capped at 32; a repair
   pass merges undersized "hole" groups into their smallest neighbour). Each
   group is simplified with quadric-error edge collapses (heap-ordered,
   version-stamped, flip-guarded). Vertices on the group border, on open-mesh
   or non-manifold edges, and on material-family borders are **locked**: a
   collapse may land on them but never move or orphan them. That single rule
   is what makes independently simplified neighbours crack-free. Group outputs
   are re-welded by exact position into one shell before the next round
   (border vertices never moved, so the weld is exact).
4. **Per-node records** — AABB + bounding sphere (both expanded by the node's
   error and unioned over children), normal cone (unioned over children),
   error bound (group's worst collapse deviation + worst child error —
   monotonic by construction), and a class mask (bit 0 opaque, bit 1
   transmissive). Children are attributed by whole simplification group.

The loop stops when a level fits in one meshlet, or when a full pass makes no
progress (all-border geometry) — recorded honestly rather than faked.

**Glass/transmission at build time:** families listed in
`TransmissiveFamilies` are partitioned from opaque geometry from the first
meshlet upward, and family borders lock like mesh borders. The two classes
therefore remain geometrically coincident but independently streamable and
LOD-able, which the Phase-3 refraction resolve requires.

## Validation & evidence

- `ValidateGeometryHierarchy` independently re-checks: source triangle count
  preserved by weld; leaves cover every source triangle exactly once and obey
  caps with zero error; error monotonic toward the root; parent bounds
  contain child bounds; every shared-border vertex of every child group
  exists bit-exact in the parent level.
- `Tools/GeometryHierarchy.test.mjs` — weld, multi-level DAG on a sphere
  (asserts a **single root cluster**), heightfield-with-cliff crack-freedom,
  class separation, determinism, tiny-mesh honesty, level soup collection.
- `Tools/HierarchyDebugOverlay.test.mjs` — editor overlay is inert without
  `?hierarchy=1`; with it, builds, draws and reports against a stub DOM.
- `Tools/GeometryHierarchyProof.mjs` → `Evidence/GeometryHierarchy/`
  (renders + animated level GIFs + `stats.json`; see its README).

Run: `npm test` (full prototype suite) and `node Tools/GeometryHierarchyProof.mjs`.

Current proof numbers: sphere 5120 → 78 tris / 1 root cluster; glass sphere
5120 → 362 / 13 (class borders limit merging, correctly); heightfield cliff
8192 → 254 / 6 with exact halving per level.

## Editor integration

`Source/HierarchyDebugOverlay.js`, wired in `EditorShell` next to the
existing cluster overlay. Inert unless the page URL has `?hierarchy=1`
(optional `&glass=<family ids>`). It builds hierarchies lazily for eligible
static facet sets (≥256 facets, the same eligibility spirit as the clustered
draw path), then draws per-level error-expanded bounds and the simplified
level's own wireframe through the occupant transforms, with cluster-identity /
error-heat / class colour modes and level buttons. It is a debug view only —
the render path is unchanged in Phase 1.

## Scope: what is *not* here yet (later phases, by design)

- **Phase 2 — runtime selection:** projected-error DAG cut, frustum +
  normal-cone culling against the records above, two-phase Hi-Z occlusion,
  GPU compaction into indirect draws, page streaming/residency.
- **Phase 3 — raster & materials:** visibility buffer (cluster+triangle id),
  hardware raster for large triangles + compute raster for micro-triangles,
  deferred material resolve; transmission gets its own visibility class,
  two-layer refraction gather over the resolved opaque scene, no Hi-Z writes,
  and a refraction-deviation term in its LOD error.
- **C++ port** into `Engine/Render` once the HTML behaviour is proven
  (the substrate there already carries `SurfaceCorner` with uv/tangent and
  `SurfaceMaterial` strata for the resolve).

Known Phase-1 limitations, deliberately recorded: child attribution by whole
group makes parent bounds conservative; the additive error bound is loose
near extreme features (sound, refines earlier — see the evidence README);
builds run on the main thread in the debug overlay (a few hundred ms for
prototype-scale sets).
