# World Forge — planet-scale crust and ocean blocking editor

Standalone WebGL2 editor prototype: start from a flat vase, stamp SDF continents / gulfs / island arcs,
sculpt the heightmap, convert the crust SDF to a polygon surface, and fly over an ocean solved with
camera-anchored, multi-resolution 2.5D shallow-water physics. WASD/Unreal camera included.

> This is a **browser prototype** under `Experimental/`, not the production C++/Vulkan/ImGui editor.
> It is deliberately self-contained (zero runtime dependencies) so the algorithms can be exercised
> and inspected before a native port. See [native port path](#native-port-path).

## Decision

| Option | GPU cost | Accuracy | Effort | Quality | Rank |
|---|---|---|---|---|---|
| Adaptive 2.5D SWE clipmap + spectral tessellated shell | ✔️ | 🔴 | 🚩 | ⭐⭐⭐⭐⭐ | 🥇 🏆 |
| Single uniform-resolution water grid | 🔴 | 🔴 | ✔️ | ⭐⭐ | 🥉 |
| Scrolling wave/normal texture (the rejected approach) | ✔️ | ✔️ | ✔️ | ⭐ | 🏁 |
| Full 3D fluid volume | 🔴 | 🔴 | 🔴 | ⭐⭐⭐⭐ | 🥈 |

Accuracy: 🔴 = best. GPU cost / Effort: 🔴 = highest. More ⭐ = better.

⭐ **Recommendation:** the adaptive tier layout. A uniform 6.25 m grid over a 409.6 km domain needs
4.29 × 10⁹ cells; 7 tiers of 256² need 458,752 cells — **9,362×** fewer, with no wave texture anywhere.
🏷️ *tagged as prototype; production port is a separate batch.*

## Run

```bash
cd Experimental/WorldForge
npm start                     # node serve.mjs → http://0.0.0.0:5174/
npm test                      # headless validation (writes Exhibits/WorldForge/)
```

Requires a WebGL2 browser with `EXT_color_buffer_float` (falls back to
`EXT_color_buffer_half_float` / `RGBA16F`). No build step, no CDN, no npm install needed to run it.

## Controls

| Input | Action |
|---|---|
| `W A S D` | fly forward / left / back / right |
| `Q` / `E` | descend / ascend |
| RMB + drag | look (yaw / pitch) |
| MMB + drag | pan |
| Mouse wheel | camera fly speed (Unreal behaviour) |
| `Shift` / `Ctrl` | 4× boost / 0.22× fine |
| LMB / LMB drag | sculpt (or inject water with Drop / Tsunami) |
| `Ctrl`+wheel | brush radius |
| `F` | focus the last brush point |
| `1`…`8` | Raise, Lower, Smooth, Flatten, Erode, Roughen, Drop, Tsunami |
| `G` / `L` | finest simulation cells / tier boundaries |
| `V` / `P` | cycle view mode / toggle planet projection |
| `Space` / `R` | drop water / reset solver fields |
| `Ctrl+Z` / `Ctrl+Y` | undo / redo sculpt strokes |
| Minimap click | teleport camera X/Z |

Camera speed also scales with altitude above the crust (`∝ (altitude/400)^0.62`), so a continent
crossing does not require a separate speed preset.

## Request → implementation

| Request | Mechanism | Source |
|---|---|---|
| Flat vase, then continents / gulfs | `CrustSignedDistanceField`: warped, filled capsule-chain outlines, even-odd interior, smooth-min union, smooth-max gulf carve; blank vase has zero relief | `src/SdfCrustField.js` |
| Heightmap sculpting | Cached `Float32Array` elevation raster + replayable stroke commands (raise / lower / smooth / flatten / erode / roughen), dirty-rect GPU upload, undo/redo | `src/CrustHeightfield.js` |
| Original terrain is SDF, converted to mesh | Volumetric field `F = max(y − H, lateral, basal)`; dual-contouring (surface nets) with gradient normals; result added as its own object, OBJ export | `src/SdfSurfaceExtraction.js` |
| Ocean everywhere below sea level | Water plane at `SeaLevel`, wet/dry mask from bathymetry; land above sea level occludes water naturally | `src/Shaders.js` |
| Real 2.5D water, not a wave texture | Depth-integrated shallow water: Lax–Friedrichs continuity flux, pressure gradient, upwind advection, bed friction, wind shear, wet/dry, shoaling | `src/GpuShallowWater.js`, `src/Shaders.js`, `src/ShallowWaterReference.js` |
| Adaptive ("Nanite but for water") | 7 camera-anchored rolling 256² simulation windows, toroidal world-anchored texel addressing, CFL-limited Δτ per tier, two-way tier coupling, coarse spectral tessellated shell beyond | `src/WaterClipmapLayout.js`, `src/GpuShallowWater.js` |
| Far = tessellated mesh ocean | 96×96 geometric shell with a 5-harmonic Gerstner displacement in the vertex shader (no normal map) | `src/Shaders.js`, `src/ViewportRenderer.js` |
| Kilometre-scale flat continent | 204.8 km … 4096 km configurable domain, geometry clipmap crust rings (6 tiers, 64² patches), logarithmic depth, camera-relative coordinates | `src/ClipmapSurface.js`, `src/ViewportRenderer.js` |
| City "vase" first | `CityPad` SDF layer flattens a footprint to a target elevation; also the Flatten brush | `src/SdfCrustField.js` |
| Planet-scale generation | Planet projection wraps the **same** SDF field onto a sphere (equirectangular), with a spectral ocean shell | `src/ViewportRenderer.js` |
| Separate editor | Own workspace, layer stack, minimap, diagnostics, export menu, save/load — independent of the main editor | `index.html`, `styles.css`, `src/EditorWorkspace.js` |

## Adaptive water detail

```text
camera
  │
  ├─ L0  1.6 km   Δx 6.25 m   Δτ 0.0075 s   ← 2.5D solver, finest
  ├─ L1  3.2 km   Δx 12.5 m   Δτ 0.0150 s   ← 2.5D solver
  ├─ L2  6.4 km   Δx 25 m     Δτ 0.0300 s   ← 2.5D solver
  ├─ L3 12.8 km   Δx 50 m     ...
  ├─ L4 25.6 km   Δx 100 m
  ├─ L5 51.2 km   Δx 200 m
  ├─ L6 102.4 km  Δx 400 m    ← 2.5D solver, coarsest
  └─ shell 819 km 96×96 tessellated spectral displacement (geometric)
```

Each tier owns two `RGBA32F` ping-pong fields: `.r = η`, `.g = u`, `.b = v`, `.a = h`.
Texel `t` represents world cell `c` with `mod(c, N) = t`, so the window rolls with the camera without
copying data; newly exposed cells are seeded from the coarser tier (or analytic swell at L6).
Outer 4-cell bands blend toward the coarse field; coarse tiers inject the fine field into their
covered centre. All tiers run the same equations, so far water is still simulated — just cheaper.

View modes prove the state: `η field` (diverging heatmap), `Velocity` (speed heatmap),
`LOD tiers` (per-tier tint), `Bathymetry` (depth ramp + contours). The live probe reads back an
actual state texel near the camera and displays η, |velocity| and depth.

## SDF → polygon surface

`Convert SDF → mesh` contour-extracts a bounded volume around the last brush point using surface
nets, computes gradient normals, enforces outward winding, and adds the result to the viewport as a
separate object with a wireframe toggle. Export produces Wavefront OBJ. Validation confirms a
watertight genus-0 solid (Euler characteristic `V − E + F = 2`) — see
[Exhibits/WorldForge](../../Exhibits/WorldForge/README.md).

## Exports

| File | Contents |
|---|---|
| `*.project.json` | crust configuration, SDF layer stack, stroke commands, water/solver settings, camera, view |
| `*.heightfield.png` | 16-bit grayscale elevation raster with min/max metadata in the project |
| `*.crust.obj` | converted polygon surface (vertices, normals, faces) |
| `*.diagnostics.json` | land/ocean statistics, adaptivity report, solver probe, mesh Euler characteristic, frame counters |

## Known limitations

- Browser prototype with GLSL ES 3.00 shaders; the production C++ editor uses Vulkan/Slang. A browser
  capture does **not** prove the native editor renders identically.
- Automated GPU screenshot capture was blocked in the authoring sandbox (no browser binary,
  Playwright browser download blocked by egress rules). The live preview is the GPU-rendered evidence;
  headless numerical evidence and mock-device render smoke tests are retained under `Exhibits/WorldForge/`.
- Continents are stylized SDF blockouts (warped outlines + gulfs), not GIS coastlines.
- Crust clipmap rings overlap with polygon-offset bias instead of stitched transition strips; a native
  port should add proper skirts/stitching.
- Planet projection is a preview of the same equirectangular field; the 2.5D solver runs in flat mode
  and the planet ocean uses the spectral shell.
- Coarse tiers use their own CFL Δτ (temporal LOD), so tier physical times are not perfectly
  synchronised at the seam; coupling bands keep the visible surface continuous.

## Native port path

| Prototype unit | Native counterpart |
|---|---|
| `SolverFragmentShader` fullscreen pass | Vulkan compute shader, per-tier dispatch, `rgba32f` storage images |
| `StateField` ping-pong `RGBA32F` | Two `VkImage` per tier with `STORAGE_IMAGE` usage, barriers between dispatches |
| `EvaluateElevationAtWorld` `R32F` lookup | Sampled `VkImage` (bathymetry clipmap), `texelFetch` bilinear in Slang |
| Geometry clipmap crust rings | Indirect-drawn `DisplayPolygons` patches with stitched skirts |
| Spectral shell | Tessellated `PolygonSurface` with vertex-stage Gerstner displacement |
| `ProjectCodec` JSON | TOML via toml++ (per `CLAUDE.md`); JSON stays automation interchange only |
| `EditorWorkspace` HTML/CSS | ImGui `ViewportPanel` / `SurfacePanel` / `SceneDirectoryPanel` under `UI/Editor/` |
