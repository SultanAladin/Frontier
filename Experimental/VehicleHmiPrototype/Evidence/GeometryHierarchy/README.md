# Geometry hierarchy — visual evidence (Phase 1)

Renders produced by `Tools/GeometryHierarchyProof.mjs` (deterministic software
rasterizer, no GPU involved) from the builder in `Source/GeometryHierarchy.js`.
Regenerate with:

```
node Tools/GeometryHierarchyProof.mjs
```

Every scene is run through the independent validator
(`ValidateGeometryHierarchy`: exact leaf coverage, meshlet caps, monotonic
error, parent-contains-child bounds, shared-border vertices bit-exact in the
parent level) before rendering — the proof tool refuses to render a hierarchy
that fails.

## Scenes

| Scene | Source | What it proves |
|---|---|---|
| `sphere` | 5120-tri icosphere (closed manifold) | Full DAG to a **single root cluster** (78 tris) |
| `glass-sphere` | same sphere, latitude band = transmissive family 9 | Opaque/transmissive **class separation** at every level |
| `heightfield-cliff` | 8192-tri heightmap mesh with a 7-unit cliff step (open border) | Heightmap/terrain ingestion, open borders and hard crease stay crack-free |
| `uv-creased-vessel` | 2976-tri closed vessel: meridian UV seam, two cap UV islands, 3 marked sharp rings | UV seams never smear and creases never bend at any LOD level |

## Files per scene

- `<scene>-source.png` — source soup, greyscale flat shading
- `<scene>-level<N>-meshlets.png` — that DAG level's actual simplified
  geometry, coloured by owning cluster (golden-angle palette). Note the
  sphere root is one solid colour: one cluster.
- `<scene>-level<N>-error.png` — same geometry coloured by cluster error
  bound (blue = exact … red = level-maximum error)
- `<scene>-level<N>-uv.png` (vessel only) — 24×24 UV checkerboard with per-pixel interpolated UVs:
  stretching elongates the checks, seams/islands show as discontinuities that must not smear
- `<scene>-level<N>-features.png` (vessel only) — red = triangle touches a marked sharp edge,
  cyan = touches only a UV seam, slate = neither
- `<scene>-level<N>-classes.png` (glass scene only) — slate = opaque,
  cyan = transmissive; the two classes never share a cluster
- `<scene>-levels.gif` — animated meshlet view looping leaf → root
- `stats.json` — per-level triangle/cluster counts, max error bound, sharp-edge and UV-seam counts,
  plus Phase-2 runtime-cut cluster/triangle counts at 8 / 2 / 0.5 px projected-error thresholds

## Reading the error numbers

Per-cluster error is a **conservative additive bound**: a parent's error is
its simplification group's worst collapse deviation (longest distance any
surface point moved) plus the worst child error. It is sound for LOD
selection and bounds expansion by construction, but it is not tight — on the
cliff scene the few cliff-spanning clusters accumulate near cliff-height
deviations per level, so the level *maximum* (what `stats.json` reports)
reaches 49 at the root even though the rendered root surface sits well inside
that envelope. What matters for the runtime cut (Phase 2) is the per-cluster
value; cliff clusters simply refine earlier, which is the desired behaviour.

Two structural properties are visible in the renders:

- **Border exactness.** Open-mesh borders (terrain perimeter) and shared
  group borders keep bit-exact vertex positions at every level — silhouettes
  and neighbour seams cannot crack regardless of which levels adjacent
  clusters are drawn at.
- **Class borders behave like borders.** The opaque/transmissive boundary on
  the glass sphere is locked during simplification, so the cyan band stays
  geometrically coincident with the opaque shell at every level.

## Editor view

The same hierarchies can be inspected live in the Editor: serve the prototype
(`npm run serve`) and open the page with `?hierarchy=1` (add `&glass=9,12` to
declare transmissive families). The overlay draws per-level error-expanded
cluster bounds and the simplified level wireframe over the viewport, with
cluster / error-heat / class colour modes. Without the query flag the overlay
constructs nothing and the editor is unchanged.
