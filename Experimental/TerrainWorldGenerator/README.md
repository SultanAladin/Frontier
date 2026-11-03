# Terrain World Generator — Blockout Prototype

Prototype of the planet-scale terrain / ocean block-out tool: an 8 km × 8 km flat
continent surrounded by ocean, an SDF-derived height field converted to a displaced
mesh, a stateful 2.5D water simulation, adaptive-tessellation ocean rings, an
Unreal-style WASD fly camera, and a separate world-editor pane.

## Where this lives

`Experimental/TerrainWorldGenerator/` — prototype per `Documentation/FolderStructure.md`
("Prototypes, experimental HTML, and related assets"). This is **not** the production
C++ renderer. The production engine keeps Vulkan as the primary API and Slang as the
primary shader language (`CLAUDE.md`); the scene renderer remains excluded from the
selective UI port on this branch, so no Windows GPU binary can be verified here
(no Vulkan SDK / MSVC host available in this sandbox).

## Run

Serve the repository root over HTTP and open the prototype:

```
python3 -m http.server 8000
# open http://localhost:8000/Experimental/TerrainWorldGenerator/index.html
```

Any static file server works; the page is plain HTML5 + WebGL2 vanilla JS, no build
step and no framework.

## Controls

| Action | Control |
|---|---|
| Fly camera | Click viewport → pointer lock; W/A/S/D translate, Q/E up/down, Shift sprint, mouse looks |
| Release mouse | Escape |
| Orbit (no lock) | Left-drag orbits; wheel zooms |
| Sculpt terrain | Editor → Sculpt; drag on terrain (wheel changes brush radius) |
| Place splash | Click in fly mode at the crosshair |
| City base | Editor → City Bases → Place base at crosshair |

## Features

- SDF height field: fractal plates + Africa-like continent stamps + gulf cut-outs →
  height map → displaced terrain mesh (textures: displacement from `R32F` height map).
- Sculpt brushes: Raise / Lower / Flatten / Smooth, editing the height map live.
- Sea level slider: floods continental margins into gulfs surrounded by water.
- 2.5D water simulation: explicit wave equation `∂²h/∂t² = c²∇²h − γ∂h/∂t` on a
  256×256 grid, stateful (height + previous height), real ripples from splashes — not
  a wave texture.
- Adaptive ocean tessellation: near ring (simulated, fine grid), mid ring (coarser,
  tapering to flat), far ring (coarsest tessellated mesh ocean to the 6 km horizon).
  Wireframe and LOD colour bands expose the tessellation tiers.
- Separate editor pane: block-out UI for sculpt, world stamps, city bases, water,
  camera.
- Kilometre-scale flat terrain with fogged horizon.

## Verification

- `node --check` passes on every JS module.
- `Exhibits/TerrainWorldGenerator/Code/` runs the height-map preview render and the
  water-simulation ripple validation under Node (results under `Exhibits/…/Results/`).
