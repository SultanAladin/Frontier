# SDF Planet Studio (experimental)

A planet-scale, volumetric terrain authoring prototype: a signed-distance planet that
you sculpt with a brush journal, streamed as curved SDF patches, with a real wave
spectrum on the ocean.

This is the **base stage** asked for: landmass authoring only, no erosion yet. The
erosion layers (particle erosion, cracks, chemical, sedimentation, snow/thermal
weathering) are meant to be ported in one at a time on top of this field later.

```
npm install
npm run dev        # http://localhost:5173  (binds 0.0.0.0)
npm test           # node --test tests/*.test.mjs   (112 tests)
npm run bench      # streaming IO benchmark  -> Exhibits/SDFPlanetStudio/
npm run qa-render  # offline CPU renders      -> Exhibits/SDFPlanetStudio/
```

## What it is, precisely

The terrain is **not a heightmap and not a voxel grid**. The planet is the zero set of

```
f(p) = |p| - (radius + elevation(p/|p|))
```

* `elevation` is an **analytic** continuous field: continental signal, ridged mountain
  octaves, mid- and fine-frequency roughness, all sampled as 3D noise on the unit
  direction. No projection, no raster, no tile-local storage, six cube faces meet
  seamlessly.
* Sculpting **appends to a journal**: `{tool, direction, radius, strength, falloff, seed}`
  is a few dozen bytes. Nothing is voxelised until a patch is meshed for display, so RAM
  cost is proportional to the number of strokes, not to planet area.
* A patch is meshed by sampling a **curved shell**: `(cells+1)^2` columns across the
  patch and a radial band sized from that patch's own min/max surface radius, then
  surface nets over the shell. Samples are signed distances, so true 3D CSG (caves,
  overhangs, arches) drops into the same mesher later without changing the pipeline.

Planet radius defaults to **900 km** with ~4 km of relief, i.e. continents, island
chains, gulfs and archipelagos, not a 100 km box.

## Modules

| File | Role |
| --- | --- |
| `src/core/vec3.js` | vector/colour maths, smoothstep, tangent frames |
| `src/core/noise.js` | seeded 3D value/gradient noise, fBm, ridged |
| `src/core/cube-sphere.js` | cube-face parametrisation, tiles, LOD from distance, lat/lon |
| `src/core/planet-field.js` | the SDF planet: analytic base + journal, edit index, serialisation |
| `src/core/brushes.js` | tool definitions and journal-op construction (raise, lower, smooth, flatten, ridge, island, gulf) |
| `src/core/surface-nets.js` | generic scalar-field mesher (one vertex per cell) |
| `src/core/narrow-band.js` | patch mesher: curved shell, radial band, border snap + skirts |
| `src/core/tile-store.js` | 160 KiB page container on disk, FNV-1a keys, whole-page CRC32 |
| `src/core/tile-cache.js` | byte-budgeted LRU |
| `src/core/stream-scheduler.js` | page-ordered, priority-driven read scheduler with readahead |
| `src/core/tile-manager.js` | quadtree selection, horizon cull, priority queue, stale-result gate |
| `src/core/water-tile.js` | ocean surface patches plus per-vertex water depth |
| `src/core/ocean-model.js` | Gerstner spectrum, dispersion, Beer-Lambert optics, foam, sky, air density |
| `src/worker/tile-worker.js` | module worker: meshes terrain and water off the main thread |
| `src/app/macro-viewport.js` | three.js viewport: worker pool, GLSL, picking, camera, upload budget |
| `src/app/macro-editor.js` | studio shell: tool rail, inspector, HUD, journal save/load, OBJ export |
| `src/app/style.css` | charcoal `tp-*` editor skin (same vocabulary as the Frontier studio) |

## How the seams are kept closed

Surface nets emits one vertex per *cell*, so a mesh built straight from cells stops up
to a cell short of its patch — every tile would show a slit against its neighbour, and a
radial skirt cannot hide a tangential slit. `narrow-band.js` therefore snaps the border
ring of vertices onto the patch boundary (`snapBorderRing`), using the border column's
own surface sample. Both sides of a seam then land on the same world curve at any LOD;
the only remaining mismatch is radial (the coarse edge is a chord of the fine one) and
that is what the skirt covers.

`tests/tile-seams.test.mjs` pins this: equal-LOD neighbours share seam vertices exactly,
all four corners agree across four tiles, border vertices sit on the field surface, and
the skirt depth covers the measured LOD chord error.

## Status

* Core: **112 tests passing** (`node --test tests/*.test.mjs`).
* App: builds clean under Vite; **not yet executed in a browser** in this environment
  (no browser or GPU in the sandbox), so treat the viewport as unproven until you run
  `npm run dev`.
* Visual evidence in `Exhibits/SDFPlanetStudio/` is produced by an **offline CPU
  rasteriser** that consumes the same meshes, the same field and the same ocean model —
  it is evidence about geometry and shading maths, not a screenshot of the app.
* Ocean: analytic deep-water dispersion, a 7-component spectrum with directional
  spreading, per-pixel wave LOD by pixel footprint, Beer-Lambert body colour over real
  per-vertex depth, depth-driven surf foam with noise breakup, sun glitter. It is a
  shading-and-simulation model, not a fluid solver.
