# Virtualized geometry — UV/sharp-aware hierarchy build (Phase 1) + runtime cut/culling (Phase 2)

Status: **Phases 1–3 complete in the HTML prototype** (build, runtime selection/culling, visibility
raster + deferred resolve + two-layer refraction, GPU page streaming), with unit tests, GPU shaders, an
editor debug view and rendered evidence. The C++ port follows now that the HTML behaviour is proven.
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

**Display debug views**: the editor's Display list (next to *Final image*, *Direct sun only*,
*Indirect diffuse (GI)*, …) gains five cluster views — **Clusters: visibility buffer IDs** (stable hash
colour per cluster-triangle, glass front cyan / back magenta), **Clusters: deferred resolve** (the Phase-3
resolved image itself), **Clusters: Hi-Z pyramid** (reversed-Z occluders, near = bright), **Clusters:
glass layers (front/back)** (front green, back red, two-layer pixels yellow) and **Clusters: LOD levels
(near→far)** (DAG level per pixel from the level stored in corner-data z: finest leaves warm red,
coarsening towards blue — the fine→coarse band moving as the camera dollies is the live error-driven
cut; perfectly flat geometry correctly stays one colour at all distances, since its simplification
error is zero and the coarsest level is already pixel-exact). They are written by the
resolve programs into the display inspection target and presented un-tonemapped like the other inspection
modes. Selecting one **activates the virtualized runtime on demand** (`ActivateForDebug`: programs +
targets are built on first use), so the views work from an ordinary editor launch — no `?hierarchy=1`
needed. While activation is in flight, or when no facet set in the scene is eligible, the view paints a
magenta checker fallback (`Shader/ClusterDebugFallback.wgsl`) instead of black, so "no data" can never
be mistaken for a dead view; a genuinely empty visibility buffer reads as a dark-blue field, and the
console names the cause. (Found the hard way: the first cut silently no-opped without the launch flag
and all four modes presented the untouched — black — inspection target.)

**Cluster debug** (same panel): the *View* selector switches between the DAG level view and a **Runtime
cut** view that runs the actual Phase-2 pipeline (cut → frustum/cone → two-phase Hi-Z, CPU mirror at
320×240, throttled) for the live camera — phase-1 visible clusters draw green, phase-2 disoccluded cyan,
Hi-Z occluded red, with frustum/cone/occluded/glass counts and page residency in the stats line
(`?px=` sets the projected-error threshold). The **Streaming page** colour mode hues clusters by page and
greys out pages that have not streamed in yet, so residency fills in root-first in real time.

## Tests & evidence

- `Tools/GeometryHierarchy.test.mjs` — build gates incl. UV seam/sharp survival and the group-coherent
  parent contract.
- `Tools/GeometryHierarchyRuntime.test.mjs` — cut completeness (antichain + leaf coverage) at six
  thresholds, cut monotonicity, frustum/cone culling (glass exempt), two-phase Hi-Z (deterministic
  occluder test + two-frame integration), glass draw separation, page packing + LRU residency.
- `Tools/GeometryHierarchyProof.mjs` → `Evidence/GeometryHierarchy/` (renders + GIFs + `stats.json` with
  per-level seam/sharp counts and runtime-cut triangle counts at 8/2/0.5 px thresholds).

Run: `npm test`, `node Tools/GeometryHierarchyProof.mjs`, serve with `npm run serve` and open `?hierarchy=1`.

## Phase 3 — raster, resolve, streaming (`Source/VisibilityResolve.js`, `Source/VirtualizedGeometry.js`, shaders)

- **Visibility buffer**: a material-free raster writes (cluster-triangle identity, occupant) + barycentrics
  per pixel. Opaque draws write depth; transmissive draws depth-test against opaque depth without writing
  it, and a second peel pass captures the **back glass layer** per pixel.
- **Deferred material resolve** (compute): identity + barycentrics → world position / interpolated UV /
  face normal → family material with UV checker modulation and direct lighting. UVs therefore drive
  materials at every DAG level exactly as authored (see the vessel resolve in `Evidence/VisibilityResolve/`).
- **Two-layer refraction**: glass pixels refract through entry (front layer) and exit (back layer)
  surfaces, gather the resolved opaque image at the exit projection, and mix Beer absorption over the
  travelled thickness with a Schlick Fresnel sky reflection. Curved glass already refines earlier via the
  Phase-2 refraction-deviation error term.
- **GPU page streaming**: hierarchy pages stream through fixed arena slots (pinned root pages first, LRU
  eviction via `PageSlotAllocator`); loading a page uploads its geometry into a slot and patches its
  cluster records (FirstIndex + resident bit), eviction clears the bit so the cut falls back to resident
  ancestors. `?streampages=N` bounds the slot count. The visibility/resolve results land in
  `VirtualizedGeometry.ResolveView` (rgba16float) for the display chain; the CPU reference path in
  `Source/VisibilityResolve.js` carries the automated proof (`Tools/VisibilityResolve.test.mjs`,
  `Tools/VisibilityResolveProof.mjs`), mirroring the WGSL entry-for-entry.

## Scope: what is *not* here yet

- **Display-chain swap**: the final resolved texture is produced every frame when `?hierarchy=1` but the
  present path still shows the forward surface pass; wiring `ResolveView` into the TAA/present chain and
  deleting the forward material work is engine-integration work for the C++ port.
- **C++ port** into `Engine/` (the substrate there already carries `SurfaceCorner` with uv/tangent and
  `SurfaceMaterial` strata for the resolve).

Known limitations, recorded deliberately: whole-group child attribution makes parent bounds conservative;
the additive error bound is loose near extreme features (sound — such clusters refine earlier); hierarchy
builds run on the main thread in the debug overlay (hundreds of ms at prototype scale).
