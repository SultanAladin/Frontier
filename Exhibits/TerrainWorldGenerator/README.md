# Exhibit: Terrain World Generator — Blockout Prototype

## What was built

A browser-based terrain / ocean block-out prototype under `Experimental/TerrainWorldGenerator/`, addressing the user's request: planet-scale block-out terrain (8 km × 8 km flat continent surrounded by ocean), SDF-derived height field converted to a displaced mesh, a separate editor pane, a stateful 2.5D water simulation (not a wave texture), adaptive-tessellation ocean ("nanite for water": simulated fine grid nearby, coarser tessellated mesh ocean farther out), and Unreal-style WASD fly camera controls.

This is a prototype in `Experimental/` per `Documentation/FolderStructure.md`. The production engine target is Vulkan + Slang with GLFW (`CLAUDE.md`); the scene renderer remains excluded from this branch's selective port, and no Vulkan SDK/MSVC host is available in this sandbox, so a Windows GPU binary cannot be built or verified here. The prototype de-risks the design and is fully interactive in the live preview.

## Contents

```
Experimental/TerrainWorldGenerator/
├── index.html                      # Fullscreen WebGL2 viewport + editor pane
├── css/Style.css                   # Editor pane UI
├── js/
│   ├── TerrainField.js             # SDF height field, continent/gulf stamps, sculpt brushes (pure math, node-runnable)
│   ├── WaterSimulation.js          # 2.5D wave equation on a 256x256 grid (CPU, stateful)
│   ├── FlyCamera.js                # Unreal-style WASD fly camera + orbit fallback
│   ├── TerrainRenderer.js          # Height-map-displaced terrain mesh (WebGL2, R32F displacement)
│   ├── OceanRenderer.js            # Adaptive tessellation rings + base markers
│   └── WorldGeneratorApp.js        # App shell, input, render loop, overlays
│   └── WorldEditor.js              # Separate editor pane: sculpt / world / bases / water / camera
└── README.md
```

## Controls

| Action | Control |
|---|---|
| Fly camera | Click viewport → pointer lock; W/A/S/D, Q/E up/down, Shift sprint; mouse look |
| Release mouse | Escape |
| Orbit / zoom | Drag / wheel (no pointer lock) |
| Sculpt | Editor → Sculpt, drag on terrain; wheel adjusts brush radius |
| Splash water | Click in fly mode at the crosshair |
| City base | Editor → City Bases → Place base at crosshair |

## Verification performed

- `node --check` passes on all seven JS modules (syntax + module-level sanity).
- `Exhibits/TerrainWorldGenerator/Code/WaterSimulationValidation.mjs` — numeric validation of the 2.5D simulation: a 4 m splash propagates to neighbours (2.30 m at 24 m distance), damps to rest in 3.54 s, and total mass never amplifies beyond the initial splash mass (`Pass: true`). Result: `Results/WaterValidation.json`.
- `Exhibits/TerrainWorldGenerator/Code/RenderHeightmapPreview.mjs` — renders the prototype's own SDF height field (Africa-like continent stamps, gulfs, fractal plates, sculpt edits) to `Results/TerrainHeightmapPreview.png` (valid PNG, 29,559 bytes).
- Local HTTP serve: `python3 -m http.server 8000` from the repo root; `GET /Experimental/TerrainWorldGenerator/index.html → 200 OK`. Live preview on port 8000.

## Limitations

- No WebGL2 → no render (any modern browser works).
- Pointer lock may be unavailable inside some iframe previews; the orbit fallback (drag / wheel) remains active.
- Kilometre depth precision uses a standard 24-bit depth buffer with fog; a log-depth buffer is the production path for true planetary scale.
- City bases are block-out markers; no city generation (per the user's note: cities need the base first).
