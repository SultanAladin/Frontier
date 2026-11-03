# World blockout evidence

Everything in this folder is generated. `Code/` holds four validation harnesses and one renderer; `Results/` holds their
reports and the six figures the editor draws.

| Script | What it proves |
|--------|----------------|
| `Code/TerrainFieldValidation.mjs` | the planet record, sampling and the vertical SDF, the authored stamps, the brushes and their undo stack, the city pad rules, mesh extraction and the TOML record |
| `Code/OceanFieldValidation.mjs` | the spectral sea state, the Saint-Venant patch, the Courant limit, volume and energy, wave propagation, the swell forcing, the probes and the near/far blend |
| `Code/OceanMeshValidation.mjs` | the screen-space-error plan, the triangle budget, the near field at the solve's resolution, resolution falling with distance, skirts against cracks, refresh against rebuild |
| `Code/Renderer3DValidation.mjs` | the 3D viewport: the projection its field of view implies, an orthogonal basis, the aim point on the centre pixel, globe framing, disc rings that reach the horizon and grow outwards, the sea never over dry land, a deterministic frame and a frame inside its budget |
| `Code/WorldBlockoutPageValidation.mjs` | the real `index.html` in jsdom: tools, chips, sliders, map strokes, a refused pad and an accepted one, the ocean panel and the far-field toggle |
| `Code/RenderEvidence.mjs` | the seven PNGs, rendered from the same scene builders and the same 3D camera the page uses |

## Running

```
node Exhibits/WorldBlockout/Code/TerrainFieldValidation.mjs
node Exhibits/WorldBlockout/Code/OceanFieldValidation.mjs
node Exhibits/WorldBlockout/Code/OceanMeshValidation.mjs
NODE_PATH=/tmp/vehicle-checks/node_modules node Exhibits/WorldBlockout/Code/WorldBlockoutPageValidation.mjs
NODE_PATH=/tmp/vehicle-checks/node_modules node Exhibits/WorldBlockout/Code/RenderEvidence.mjs
```

The page harness and the renderer need `jsdom` and `@resvg/resvg-js`, installed outside the repository:

```
npm install --prefix /tmp/vehicle-checks jsdom @resvg/resvg-js
```

## Results

| Report | Result |
|--------|--------|
| `Results/TerrainFieldValidation.md` | 82/82 checks passed |
| `Results/OceanFieldValidation.md` | 54/54 checks passed |
| `Results/OceanMeshValidation.md` | 35/35 checks passed |
| `Results/PageValidation.md` | 65/65 checks passed |

Measured at the evidence settings (Terra-01, 12 km adaptive span, 2 km camera, 2.5 px pixel budget, 120,000 triangle
budget):

| Quantity | Value |
|----------|-------|
| Land share after the authored pass | 17.9% |
| Peak | 1,319 m |
| Ocean patch | 112² cells, 14.3 m, 1.6 km across |
| Ocean mesh | 119,936 triangles — 63,232 near, 49,536 mid, 7,168 far |
| Uniform grid at the near cell | 5,644,800 triangles |
| Saving | 47× |
| Worst accepted screen error | 57.8 px, budget-limited, mid field only |

## Figures

| Figure | What it shows |
|--------|----------------|
| `Results/WorldMap.png` | Terra-01 after the authored pass: four continents, three carved gulfs, the Sunda isles, three city pads |
| `Results/SculptTimeline.png` | The workflow in three frames: sculpt a flat shelf, stamp a continent, carve the gulfs and sink a pad |
| `Results/CityPads.png` | The placed pads with their radius and levelled height |
| `Results/OceanResolution.png` | The adaptive plan: 1,762 tiles coloured by cell size, the near footprint, the fan rings, and the near field magnified with both blend radii |
| `Results/OceanSection.png` | A cross-section through the solve: the solved surface, the spectral field behind it, the bed, the probes and a labelled vertical scale |
| `Results/View3D.png` | The blockout in three dimensions at three scales: the globe from 9,000 km, a 520 km coast, and the 2.5D patch at 2.4 km |
| `Results/OceanBudget.png` | Where the triangles went against a uniform grid, on two scales so the tiers are legible |

## What the checks are actually guarding

- **The SDF, not the heightmap, is the interface.** Depth, waterline tests and the band classifier all read
  `SampleDistance`; the terrain harness asserts the identity and the ocean harness reads its bathymetry through it.
- **The solve is a solve.** Volume is conserved to a hundredth of a percent on an unforced patch, a seeded bump spreads
  symmetrically, and a four-hundred-step run stays finite.
- **The near field is never sacrificed.** The plan exempts the solve footprint from the triangle budget, and the harness
  asserts the near tiles reach the patch cell size even when the budget is spent.
- **The far field is not a texture.** Every far vertex is evaluated from the spectral sum; there is no normal map
  anywhere in
  this folder, and the far-field-only toggle is proved to leave no vertex on the near solve.
- **The page is the product.** The page harness clicks the real chips and tools, drags the real map, and reads the
  real record
  back — a refused pad must say why it was refused, and the export must carry both stanzas.
