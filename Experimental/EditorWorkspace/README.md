# Editor Workspace — docked prototype

A general-purpose editor shell in the browser: a dark, glass-and-mint workspace with exactly three docked panels —
**outliner · live WebGL2 viewport · inspector** — built to the owner's supplied references
(`SourceImages/General UI/13edfbc6a1110a5abd8ec9099429cf07.jpg` and
`SourceImages/General UI/0bab27674eea6c04ccaf172f194ce6ef.jpg`).

Everything in the viewport is real geometry drawn by a hand-written WebGL2 pipeline: the hero is a parametric
(2, 3) torus knot at 4 480 triangles, with a halo ring, a procedural ground grid, selection brackets, a scan-band
surface shader and an orbit camera. No Three.js, no model files, no textures.

## Run

```bash
cd Experimental/EditorWorkspace
python3 -m http.server 8130 --bind 0.0.0.0
# open http://localhost:8130/
```

ES modules and the vendored `woff2` faces need HTTP — `file://` will not load them. There is no build step, no
package install and no network access at runtime. `Exhibits/AppLauncher.html` links straight to this page.

## Structure

| Path                                     | Role                                                                             |
|------------------------------------------|----------------------------------------------------------------------------------|
| `index.html`                             | Workspace chrome, icon sprite (32 `<symbol>`s), panel markup, inspector controls   |
| `styles/WorkspaceTheme.css`              | Tokens, dock layout, glass cards, splitters, collapsed rails, grain/vignette       |
| `scripts/ViewportRasterization.js`       | WebGL2 surface/grid/axis programs, orbit camera, ray picking, selection bounds     |
| `scripts/PolygonSurfaceConstruction.js`  | Procedural knot, sphere, torus, cube, cylinder, halo triangle soup + winding pass   |
| `scripts/SpatialMathematics.js`          | Column-major placement algebra, normal recovery, projection, ray recovery          |
| `scripts/WorkspaceHost.js`               | Dock state, directory rendering, inspector binding, tools, transport               |
| `tools/ValidateWorkspaceGeometry.mjs`    | Headless geometry/math harness (60 checks)                                        |
| `tools/ValidateWorkspaceHost.mjs`        | Headless DOM/interaction harness in jsdom (39 checks)                             |
| `tools/CaptureWorkspaceScreens.mjs`      | Retained screenshot capture — regenerates `evidence/` from a real Chromium session |
| `evidence/`                              | Committed captures of the delivered state                                          |
| `assets/fonts/`                          | DM Sans + Space Grotesk, latin subsets, OFL licence retained                       |

## Docking behaviour

Three flex columns (`Outliner` / `Viewport` / `Inspector`) separated by draggable splitters; nothing is absolutely
positioned over another panel, so the three panels can never overlap. Widths are CSS variables with clamped drag
ranges (236–438 px outliner, 268–470 px inspector, viewport floor 380 px), double-clicking a splitter restores the
default, and either side panel collapses to a 54 px rail. Every floating card, the tool ribbon, the scan timeline
and the selection brackets live inside the viewport stage only.

Below 1360 px the floating KPI stack steps aside so the record annotation keeps its room; the dock still reports
zero overlap and zero horizontal scroll at 1600, 1280 and 1024 px wide (measured, see *Verification*).

## Controls

| Input                        | Action                                                |
|------------------------------|-------------------------------------------------------|
| Drag / wheel / middle-drag   | Orbit, dolly, pan                                     |
| Click record or viewport     | Select (dashed brackets + inspector binding)          |
| `Q` `W` `E` `R`              | Select · Move · Rotate · Scale                        |
| Drag selected record         | Apply the active transform                            |
| `F` / double-click           | Frame selection                                       |
| `G` / `Space`                | Ground grid / scan timeline play-pause                |
| `Ctrl+D` / `Delete`          | Duplicate / remove selected record                    |
| `Ctrl+K`                     | Focus directory search                                |

The inspector binds placement (position / rotation / scale, drag-scrub or wheel), surface optics (volume tint, rim
radiance, rim glow, topology wire, scan band, sweep toggle), the three surface presets (Holographic · Wireframe ·
Solid), tessellation readout and viewport settings (field of view, orbit radius, grid, turntable, axes) live to the
WebGL uniforms. Records are added from the primitive popover, the Library tab, or duplicated with one click.

## Verification (local, 2026-10-31)

```bash
cd Experimental/EditorWorkspace
node tools/ValidateWorkspaceGeometry.mjs
npm install --no-save jsdom && node tools/ValidateWorkspaceHost.mjs
CHROMIUM_EXECUTABLE=<chromium> node tools/CaptureWorkspaceScreens.mjs
```

| Check                                                                   | Result                     |
|-------------------------------------------------------------------------|----------------------------|
| `ValidateWorkspaceGeometry.mjs` — projection, framing, six surfaces, winding/basis consistency | **60 checks, 0 failures** |
| `ValidateWorkspaceHost.mjs` — init, directory, filters, add/duplicate/delete, tabs, sliders, presets, splitter, transport, shortcuts | **39 checks, 0 failures** |
| GLSL sources parsed with `@shaderfrog/glsl-parser`                        | 6 sources, 0 failures      |
| Headless Chromium layout probe at 1600×1000 / 1280×800 / 1024×720         | overlap `false`, horizontal scroll `false`; page errors: none |
| Element-id uniqueness in `index.html`                                    | 133 ids, 133 unique        |

The host harness runs with `HTMLCanvasElement.prototype.getContext` returning `null`, so it also proves the dock,
directory and inspector stay usable when rasterization is unavailable (the page shows the WebGL2 fallback notice
instead of failing silently).

The geometry harness pins the hero framing (`min -0.85`, `max 0.64`, height fraction `0.74` at `distance 9.81`),
the orthographic NDC mapping, per-triangle corner bases and the winding-normal agreement of every emitted surface.

## Evidence

`evidence/` holds captures produced by `tools/CaptureWorkspaceScreens.mjs` (real Chromium, software GL, 1× DPR):

| File                       | Shows                                                                    |
|----------------------------|--------------------------------------------------------------------------|
| `WorkspaceOverview.jpg`    | Full 1600×1000 workspace: dock, live knot, inspector, timeline            |
| `ViewportSelection.jpg`    | Viewport close-up: brackets, annotation, scan band, telemetry cards       |
| `InspectorSurface.jpg`     | Surface tab: presets, tint, rim/wire/scan sliders, tessellation           |
| `InspectorPreset.jpg`      | Inspector after choosing the Wireframe preset (control → pipeline proof)  |
| `RecordAdded.jpg`          | A second surface added from the primitive popover and selected            |
| `NarrowDock.jpg`           | 1280×800 — panels still docked, nothing overlapping                       |

## Limitations

- **Browser prototype, not engine parity.** This proves the shell, the interaction model and the raster pipeline in
  a browser; it makes no claim about the C++ editor, and `Exhibits/EditorMirror` remains the C++ comparison mirror.
- **Frame rates in the captures are software-rasterizer numbers.** The capture host runs headless Chromium through
  SwiftShader, so the status bar reads single/double-digit FPS there. It is not a GPU benchmark.
- **Fonts are web subsets** (`DM Sans` 300–600, `Space Grotesk` 300–600), resolved through the same Fontsource
  packaging used by `Exhibits/LogStream/PARITY.md`; weight 700 is intentionally absent, so the CSS never asks for it.
- **No persistence, no undo stack, no multi-scene files.** Records live in memory for the session.
- **Parametric winding.** The generated grids sweep in a left-handed order, so `FinalizeSurface` normalizes the
  winding once at construction (explicitly face-listed primitives such as the cube arrive already consistent); the
  raster stage then trusts `gl_FrontFacing` and dims interior faces instead of the surface flipping normals.
- **Not verified on Windows.** Validation above is from this Linux build host only.
