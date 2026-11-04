# SDF Planet Studio — evidence

Delivered scope: a planet-scale **volumetric (SDF) terrain authoring prototype** with a
sculpting toolbar and a physically-motivated ocean, as the base stage before erosion.
Prototype source: `Experimental/SDFPlanetStudio/`. Its README documents the field
definition, the module map, and the seam strategy.

## Reproduction

```bash
cd Experimental/SDFPlanetStudio
npm install

# 1. Core suites (no browser, no GPU needed)
npm test
#   -> 112 tests, 0 failures

# 2. Streaming IO benchmark (real reads/writes in this sandbox + labelled seek model)
node tools/bench-streaming.mjs
#   -> writes Exhibits/SDFPlanetStudio/streaming-bench.json

# 3. Offline renders: builds real tiles through the real TileManager and rasterises
#    them on the CPU with the viewport's shading maths.
node tools/render-qa.mjs --width 1600 --height 900
#   -> writes coast-day.png, planet.png, island-sculpt.png, render-qa.json

# 4. The application itself
npm run dev     # Vite on 0.0.0.0:5173
```

## Images in this folder

| File | What it shows |
| --- | --- |
| `planet.png` | The whole 900 km-radius planet at LOD 0–3: continents, gulfs, archipelagos, ocean lit from the local sun. 142 terrain + 478 water patches, 767 k triangles. |
| `coast-day.png` | A coastline from 1.65 km altitude, 2.6 km offshore: surf line, shallow-to-deep water, sky reflection at grazing angles. 1 397 terrain + 491 water patches, 917 k triangles. |
| `island-sculpt.png` | The same coast after one Island stamp and one Gulf carve (journal = 2 ops), camera re-seated on the sculpted terrain. The bay on the left and the raised headland are the authored result. |

`render-qa.json` records resolution, tile and triangle counts, camera positions, the
self-chosen coastline, and the render timings for both bundles.

## Measured results

**Streaming (this sandbox, 4096 tiles / 32 MB working set, `streaming-bench.json`)**

| Pattern | Reads | Measured MB/s | Modelled MB/s (8 ms seek) |
| --- | --- | --- | --- |
| Per-tile 8 KiB reads (naive) | 4 096 | 134.5 | 0.97 |
| Page reads, random order | 205 | 2 500.8 | 19.52 |
| Page reads, sorted (scheduler order) | 205 | 2 756.2 | **74.91** |

19.98× fewer syscalls; 20.5× measured throughput; 77.2× under the explicitly modelled
seek penalty; 3.84× sorted over random page order. The HDD requirement is met by
batching 16 bricks into each 160 KiB page (one seek per page, not per tile) plus a
page-ordered scheduler with readahead — the numbers above are what an HDD sees.

**Core suites (all passing)**

`noise` 6, `cube-sphere` 14, `planet-field + brushes` 16, `surface-nets` 8,
`narrow-band` 10, `tile-seams` 7, `ocean-model` 12, `tile-store` 10,
`stream-scheduler` 12, `tile-manager` 11, `water-tile` 6.

## Limitations, stated plainly

1. **No browser or GPU exists in this sandbox.** `npm run dev` builds and serves, and the
   module graph compiles under Vite, but the app has not been executed against a real
   WebGL context here. The renders above are CPU raster output from the same meshes and
   shading maths — they are not screenshots of the app.
2. The ocean is a 7-component Gerstner spectrum with deep-water dispersion,
   Beer-Lambert body colour over the mesher's real per-vertex water depth, footprint-based
   wave LOD, depth-driven surf foam and sun glitter. It is not a fluid solver and does not
   carry sediment.
3. Water art direction is unfinished: far water still reads brighter than a photograph at
   some sun angles.
4. No erosion layers are present by design — the first deliverable is authoring only.
5. Sculpting, journal save/load and OBJ export are wired to the UI but have not been
   exercised through a browser yet.
