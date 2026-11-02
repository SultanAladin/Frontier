# Water Simulation — 2.5D grid water, grid foam, GPU spray, and three switchable 3D models

Browser prototype of the AAA-targeted water stack: a global 2.5D grid layer for ripples, a global grid foam
layer, GPU-resident 3D spray, and three selectable 3D water simulations. WebGL2 + GLSL ES 3.00, no libraries,
no build step.

## Run

```bash
node Launch.mjs 8770 127.0.0.1      # or: Launch.bat on Windows
python3 -m http.server 8770 --bind 127.0.0.1 --directory .
```

Open the printed URL. ES modules and shader fetches require HTTP — `file://` will not work.
Requires `EXT_color_buffer_float` (every WebGL2 desktop GPU; the page reports a clear notice if absent).

## Model comparison

Cost/VRAM = share of a 16.6 ms frame at 1080p-class resolution (more = worse); Accuracy, Foam, Interaction
more = better. Ratings are design targets from the implemented algorithms, not measurements taken on this host.

| Model | Algorithm | Cost (GPU) | VRAM | Accuracy | Foam | Interaction | Determinism | Rank |
|---|---|---|---|---|---|---|---|---|
| 1 · Spectral FFT ocean | 3 tiled cascades, 256² FFT, JONSWAP + swell ring | 🔴 | 🚩 | 🔴 | 🚩 | ✔️ | 🔴 | 🥇 🏆 |
| 2 · Gerstner crest sum | 16 trochoidal components, analytic | ✔️ | ✔️ | 🚩 | 🚩 | ✔️ | 🔴 | 🥈 |
| 3 · Global grid wave solve | 512² finite-difference wave equation, depth-scaled celerity | 🚩 | 🚩 | 🚩 | 🔴 | 🔴 | 🚩 | 🥉 |

- 🥇 **Default is model 1** for open-water realism; it is the only one with statistically correct short-crested seas.
- Model 3 is the only surface that reacts to the vessel, impacts, and shoaling, so it owns the interactive feel.
- Model 2 is the fallback for low-end GPUs and the clearest control over crest sharpness.

## What each layer does

| Layer | Grid / domain | Resolution | Job |
|---|---|---|---|
| 2.5D ripple field | 400 m global grid | 1024² | Vessel wake, spray impacts, interactive ripples |
| Grid foam | 400 m global grid | 1024² | Advects with surface flow, breaks on crest curvature, shoreline band, spray deposits |
| Global sea solve | 400 m global grid | 512² | Model 3 swell, wind forcing, sponge boundary, shoaling |
| Spectral cascades | 46 / 148 / 470 m tiles | 3 × 256² | Model 1 displacement + slope + crest foam |
| Terrain atlas | 900 m bake | 1024² | Height + normal for shoreline, solvers, spray collision |
| Height atlas | 400 m | 256² | Surface heights for buoyancy readback and spray collision |
| Spay particles | world space | 65 536 | Transform-feedback 3D droplets, soft depth-faded points |

All grids are world-space and camera-independent, so foam, ripples, and heights stay registered while you orbit.
Only the surface tessellation follows the camera.

## Controls

| Input | Action |
|---|---|
| `1` / `2` / `3` | Switch 3D water model (panel cards do the same) |
| Drag / right-drag / wheel | Orbit / pan / zoom |
| `G` | Global grid overlay |
| `F` | Grid foam on/off |
| `P` | 3D spray particles on/off |
| `B` | Vessel on/off |
| `Space` | Freeze simulation |
| `H` | Collapse the settings drawer |
| `V` | Runtime validation panel |
| `C` | Capture PNG |

Panel toggles: ripple grid, ripple strength, grid foam, foam intensity, spray particles, spray emission,
vessel + throttle, clarity, sun elevation/bearing, cloud cover, haze, quality (Perf → Ultra), mirror reflection,
screen refraction, crest scattering, bloom, exposure, edge resolve, micro ripple detail, grid overlay,
field view (Shaded / Height / Foam / Normal / Depth).

## Rendering path per frame

1. Terrain atlas bake (once) and spectral initialization (on sea-state change only).
2. Active model update: FFT cascade chain, Gerstner table refresh, or grid wave step.
3. Spray transform feedback → deposit into ripple grid and foam grid.
4. Ripple + sea grid steps, foam step, height atlas.
5. Vessel buoyancy from a 28² height readback window.
6. Mirrored reflection pass, opaque scene + depth copy, deferred water (refraction, reflection, GGX sun, foam,
   crest scattering, aerial haze), soft spray rasterization.
7. Two-tier bloom, ACES tonemap, edge resolve, vignette.

## Verification

See `Exhibits/WaterSimulation/README.md` for commands, transcripts, and limitations. NumPy-free Node scripts
validate shader stages, uniforms, wave mathematics, grids, vessel kinematics, and the interface contract.

## Boundaries

- Prototype scope: `Experimental/` only; no production engine code depends on it.
- The native Vulkan/Slang renderer is untouched; this is a WebGL2 reference implementation of the design.
- Tested by shader compilation and CPU reference validation on Linux; not executed on a GPU or on Windows here.
- Ocean spectrum is a bounded approximation (one wind sea + one swell ring), not a measured spectrum database.
- Foam is a 2D advected coverage grid shaded with procedural bubble detail; it is not volumetric foam geometry.
