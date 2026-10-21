# Virtualized geometry — UV/sharp-aware hierarchy build (Phase 1) + runtime cut/culling (Phase 2)

Status: **Phase 1 (build) and Phase 2 (runtime selection/culling) complete in the HTML prototype**, with
unit tests, GPU shaders, an editor debug view and rendered evidence. Raster/materials (visibility buffer,
deferred resolve, two-layer refraction) is Phase 3; the C++ port follows once the HTML behaviour is proven.
`GEOMETRY_CLUSTERING.md` describes the earlier *lossless leaf clustering* (spatial median splits + frustum
cull only) — no LOD, no error metric, no DAG; it is not virtualized geometry and nothing here reuses it.

The design follows the published *concepts* of cluster-based virtualized geometry (meshlets, group
simplification with locked borders, monotonic-error DAG, projected-error cut, two-phase Hi-Z occlusion).
All code is newly written against this prototype's facet-soup mesh model; no third-party implementation
was consulted for code.

## Phase 1 — build (`Source/GeometryHierarchy.js`)

Input is facet soup (`{ Facets: [{ Corners, UVs?, Normals?, SharpEdges?, Family }], FirstVertex }`), which is
what authored meshes, heightmap terrain and voxelized-SDF-cliff meshes all arrive as: displacement is real
geometry by the time it reaches the builder.

1. **Weld** — vertices weld on exact **3D position only**, so connectivity across UV seams and hard creases
   stays visible. Per-corner UVs (`Facet.UVs`), corner normals and marked sharp edges (per-facet
   `SharpEdges` bitmask / pair list, plus optional dihedral-angle and normal-discontinuity detection) are
   extracted into per-triangle arrays.
2. **Meshlets** — greedy adjacency growth (≤128 tris / ≤256 verts) scored by normal-cone alignment,
   scale-normalised distance, and a penalty for crossing a UV seam or marked sharp edge, so meshlets align
   with UV chart boundaries and creases when possible. Material-class keys are hard walls.
3. **Group → simplify → recluster (DAG loop)** — adjacent meshlets grouped (8 at leaf, doubling per level,
   cap 32, with a hole-repair pass). Each group is simplified with quadric-error edge collapses:
   - Group borders, open borders, non-manifold edges and family borders **lock 3D position and corner UVs
     bit-exact** — independently simplified neighbours stay crack-free in 3D *and* in UV.
   - **Marked sharp edges and UV seams** add 3D line-alignment quadrics; feature corners (degree ≠ 2,
     junctions, chart corners) pin; degree-2 feature vertices may only collapse **along** their feature
     curve, interpolating *both* charts' UVs at the shared segment parameter `t` and rejecting any collapse
     that would fold a 2D UV triangle or mix charts. So a crease can lose vertices but never bends away,
     and a UV seam never smears.
   Group outputs re-weld by exact position into one shell for the next round.
4. **Per-node records** — tight AABB/sphere, error-expanded bounds/sphere/cone (unioned over children),
   additive monotonic error bound, class mask (opaque/transmissive), sharp-edge count — plus, for Phase 2,
   a **group-coherent `ParentError`/`ParentSphere`**: every cluster of a simplification group shares the
   exact same parent error and enclosing sphere, which is what makes the runtime cut atomic per group.

**Glass at build time:** transmissive families are partitioned from opaque from the first meshlet up and
their borders lock like mesh borders — the classes stay geometrically coincident but independently
streamable/LOD-able for the Phase-3 refraction resolve.

Validation (`ValidateGeometryHierarchy`, independent of builder bookkeeping) re-checks: leaf coverage/caps,
monotonic error, parent-contains-child bounds, bit-exact shared borders, finite UVs inside the source UV
domain, and per-group `ParentError ≥ Error`. Proof-scene numbers: sphere 5120→80 tris / 1 root cluster;
glass sphere →352 / reserved class clusters; heightfield cliff 8192→254 with exact-ish halving;
UV vessel (below) 2976→92 / 1 root.

**UV + sharp proof mesh** (`ConstructUvSeamCreasedMeshFacets`): a closed vessel with a longitudinal meridian
UV seam, two separate cap UV islands (circular seams on both rims) and three marked sharp rings. At every
level the tests assert: sharp vertices stay exactly on their crease planes (z ∈ {−1, 0, +1}), no triangle
crosses the meridian seam or jumps between UV islands, seams/creases never fully vanish, and the rendered
root level still shows an unsmearing checkerboard (`Evidence/GeometryHierarchy/uv-creased-vessel-*.png`).

## Phase 2 — runtime (`Source/GeometryHierarchyRuntime.js`, `Source/VirtualizedGeometry.js`, shaders)

- **Projected-error DAG cut** (`SelectHierarchyCut`): a cluster is drawn iff
  `SelfErrorPx ≤ threshold < ParentErrorPx`, where the projected error divides the (scale- and
  refraction-adjusted) error by distance to the error-expanded sphere. Because `ParentError`/`ParentSphere`
  are shared per simplification group, siblings transition atomically (no cracks/holes); a coarsest-wins
  prune over the multi-parent DAG guarantees the selected set is an antichain covering every leaf.
  Transmissive clusters get a **refraction-deviation factor** `(η−1)·sin(cone half-angle)` so curved glass
  refines earlier than flat panes of equal geometric error.
- **Culling**: per-cluster frustum test on tight bounds; normal-cone backface test
  (`dot(view, axis) > sin(α+β)` culls) — never applied to transmissive clusters; **two-phase Hi-Z
  occlusion**: cut clusters are tested against last frame's depth pyramid, phase-1 survivors render and
  rebuild the pyramid, then last frame's occluded set is retested for disocclusion. Transmissive clusters
  never write Hi-Z depth.
- **Pages & residency** (`PackHierarchyPages`, `HierarchyResidencyManager`): clusters pack root-first into
  fixed-size (default 64 KB) pages; root pages are pinned; an LRU manager loads by projected-error priority
  and evicts unpinned pages to a byte budget. A cut against cold residency falls back to the finest resident
  ancestor group and queues the missing pages instead of stalling.
- **GPU path** (`Source/VirtualizedGeometry.js` + `Shader/VirtualizedGeometryCull.wgsl` +
  `Shader/HiZPyramid.wgsl`): the same cut/cull runs as a parallel compute program over per-(set, occupant)
  cluster records, emitting `drawIndexedIndirect` commands for phase 1 and phase 2; `SurfaceProjection`
  renders phase 1, builds the Hi-Z pyramid from its depth, culls phase 2, and renders it into the same
  attachments. Active only with `?hierarchy=1` (eligible static sets are taken over from the lossless
  cluster path); otherwise the class is inert and the frame is byte-identical to before. GPU execution is
  verified in the user's browser via the live preview — this sandbox has no WebGPU device, so the CPU
  runtime (`ExecuteHierarchyRuntimeFrame`, same maths) carries the automated occlusion/cut proof.

## Editor integration

`Source/HierarchyDebugOverlay.js` (inert unless `?hierarchy=1`, `&glass=` for transmissive families):
per-level error-expanded bounds + simplified wireframe with cluster / error-heat / class / **UV-checker /
sharp-and-seam** colour modes, level buttons, and live counts (clusters, triangles, sharp edges, UV seams,
DAG max error, build time). With `?hierarchy=1` the viewport itself renders through the Phase-2 runtime.

## Tests & evidence

- `Tools/GeometryHierarchy.test.mjs` — build gates incl. UV seam/sharp survival and the group-coherent
  parent contract.
- `Tools/GeometryHierarchyRuntime.test.mjs` — cut completeness (antichain + leaf coverage) at six
  thresholds, cut monotonicity, frustum/cone culling (glass exempt), two-phase Hi-Z (deterministic
  occluder test + two-frame integration), glass draw separation, page packing + LRU residency.
- `Tools/GeometryHierarchyProof.mjs` → `Evidence/GeometryHierarchy/` (renders + GIFs + `stats.json` with
  per-level seam/sharp counts and runtime-cut triangle counts at 8/2/0.5 px thresholds).

Run: `npm test`, `node Tools/GeometryHierarchyProof.mjs`, serve with `npm run serve` and open `?hierarchy=1`.

## Scope: what is *not* here yet

- **Phase 3 — raster & materials:** visibility buffer (cluster+triangle id), hardware raster for large
  triangles + compute raster for micro-triangles, deferred material resolve; transmission gets its own
  visibility class, two-layer refraction gather over the resolved opaque scene, no Hi-Z writes (already
  honoured), and refraction-deviation LOD error (already in the cut).
- **GPU page streaming:** pages/residency are proven on CPU; the WebGPU path currently keeps built pages
  resident (upload-on-demand is Phase 3 plumbing).
- **C++ port** into `Engine/` once Phases 2–3 are proven in HTML.

Known limitations, recorded deliberately: whole-group child attribution makes parent bounds conservative;
the additive error bound is loose near extreme features (sound — such clusters refine earlier); hierarchy
builds run on the main thread in the debug overlay (hundreds of ms at prototype scale).
