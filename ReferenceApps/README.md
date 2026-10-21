# Preserved Frontier reference applications

Source supplied by the user: `https://github.com/unassignedinbox/Frontier/tree/arena%2Ff905b778-frontier`.

Pinned commit: **b299ec6903cfda8237e6bb2088bb720729d55aa7**. Copied 2026-10-21.

## Folder boundaries

| Folder | Purpose |
| --- | --- |
| `Upstream/` | **834 unchanged original files**: the complete `Exhibits/` tree, root `Docs/`, root launcher, and shared root font assets. Do not edit this copy to implement UI changes. |
| `BuildArchives/` | Byte-identical copies of supplied app distributions. These preserve the supplied builds when environments exclude directories literally named `dist` from snapshots. Manifest entries map them to their original paths. |
| `Working/ParticleEditor/` | Adapted WebGPU particle editor, original UI/renderer plus workspace persistence and telemetry bridge. |
| `Working/FluxParticleStudio/` | Adapted Flux source, original layer/timeline/renderer plus workspace bridge. Includes a lockfile for rebuilding. |
| `Published/FluxParticleStudio/` | Required static build of the adapted Flux app, served by the main preview. Kept outside a directory named `dist` so the running app survives workspace snapshots. |
| `EngineContent/` | Shared local fonts for the working particle editor. |

Run `node ReferenceApps/Verify.mjs` from the repository root to verify every original file against `MANIFEST.json`. No repository checkout/branch switch was needed. Attribution, upstream comments, documentation and bundled font licenses remain with the copied files.

## References for future UI replacements

All paths below are relative to `Upstream/Exhibits/`:

- **FrontierEditor/** — main HTML/JSX editor, icons, graphics and inspector components.
- **ProjectZeroEditor/** — additional editor shell/reference app.
- **FractureEditor/** — fracture application, panels and assets.
- **Fluid/** — gas/fluid application and example scenes.
- **WebFluidLab/** — fluid simulation application.
- **FluidNodeEditor/**, **NodeFlowEditor/** — graph editor references.
- **ParticleEditor/** — WebGPU particle scene editor, many preset kinds, forces and lightning.
- **FluxParticleStudio/** — layer/timeline particle composition editor, Three.js/WebGL renderer.
- **NativeSurfaces/** — inspector/component design references.
- Other copied apps include CliffSequence, SolidArc, RoadEditor, Ocean, SpatialHud, and the distance/radiance/deformation integrator/projection exhibits.
- **Docs/** — original integration and editor notes. Some original documentation still uses historical `Experimental/` paths; this fork stores the applications under `Exhibits/`.

These are reference copies, not a claim that every application's source can run without its own build/install step. Supplied compiled distributions are also preserved in `BuildArchives/`.

## Particle tools in one app

Start the normal preview:

```sh
cd Experimental/VehicleHmiPrototype
npm install --ignore-scripts
python3 Tools/Serve.py
```

On port 8080, select **Effects → Particles**, then **Open Particle Editor** / **Open Flux Particle Studio**, or use the viewport's **Particles** button. Both original interfaces are embedded in a single workspace with a shared toolbar, tool tabs, project import/export and a return-to-scene action.

Only one child tool is mounted at a time. The driving renderer/physics loop is suspended during authoring, rather than rendering another heavy scene behind it. Returning removes the child frame and resets the driving frame clock to avoid a giant catch-up timestep. No particle GPU engine is started while the workspace is closed.

This is an **authoring integration**. The tools render real editable particles in their own previews; authored emitters are **not yet injected into the vehicle scene's renderer**. Existing Derez controls/effects and the current fracture/gas/environment/diagnostics work remain intact. Arbitrary conversions between WebGPU Particle Editor presets and Flux compositions are not fabricated: they retain separate documents inside one project.

### Save, reload and telemetry

- Particle scenes persist systems, nested parameters, force fields, wind, camera, display and transport controls. Simulation buffers and runtime clocks are not saved; simulation restarts when remounting.
- Flux compositions persist layers and composition settings using the original Flux codec. Flux's own PNG/WebM/JSON export and other controls remain available.
- Working copies post changes after a short settle and periodically catch non-slider edits. Separate origin-local storage keys retain each document. Browser storage/quota denial is visible; export is the portable backup.
- Tool switching/back/export requests an acknowledged save before unmounting. A missing acknowledgement keeps the editor open with an error instead of silently dropping edits.
- **Export project** downloads both documents in one `frontier-particles.json`; import accepts that bundle, a particle-scene document, or Flux's `.flux.json` format. If clipboard/storage/browser persistence is unavailable, file export still works.
- Host Diagnostics records workspace events, full authored-document revisions and separate child preview statistics, including canvas dimensions/render API. These are not mixed with driving-scene GPU-pass timings. The particle preview's raw frame interval is separate from its capped simulation timestep. No matched desktop FPS/overhead benchmark is claimed.
- Message receivers verify the exact child/parent window, origin, workspace token, protocol version and editor identity. Documents are bounded; nested parameter restoration only admits fields already defined by the preset.

### Rebuild Flux after source edits

The published build is included, so normal preview startup needs no separate Vite server or CDN:

```sh
node ReferenceApps/BuildParticles.mjs --install  # reproducible install + static rebuild
# subsequently:
node ReferenceApps/BuildParticles.mjs
```

### Validation

- Main prototype unit suite: **113 passing tests**, including workspace protocol/data guards, nested parameter restoration and first-document receipt.
- Flux source suite: **13 passing tests**.
- Particle source checks: original force taxonomy checks, panel checks and adapted handoff checks pass. Four optional jsdom DOM checks remain skipped when jsdom is absent; the real browser check below exercises a real inspector instead.
- Main app null-backend bring-up with the Particles inspector: **3 frames, 0 errors/warnings**.
- Real Chromium software-GPU integration: WebGPU particle rendering, live Rate change, Flux layer rendering/name edit, tab switch + restoration, combined export/import, wrong-window rejection, single mounted iframe, and driving pause/resume all pass with **0 page or console errors**. Artifacts are written outside the repository to `/home/user/particle-workspace-proof/` by default. This is functional validation, not a desktop performance benchmark.
- Confined preview reference route and encoded path-traversal rejection checked; both particle app entrypoints and local fonts return HTTP 200.

Browser test: `node Tools/ParticleWorkspaceBrowserProof.mjs` from the prototype folder, with optional Playwright/Chromium tooling installed and the preview running.


### Fracture editor tab

The same authoring workspace now has a **Fracture Editor** tab. Open it directly from **Effects → Particles → Open Fracture Editor**, or switch to it from the existing workspace toolbar. It embeds the original self-contained `Upstream/Exhibits/FractureEditor/index.html` without modifying the preserved upstream files or adding a dependency/build. The driving renderer pauses as it does for the particle tools.

This tool is standalone: it retains its own browser-local per-object recipes/baked fragments and **Export fracture** action. Particle project import/export and Apply lightning are hidden for this tab; fracture assets are not silently put into a particle document or injected into the driving scene. Back to scene works without waiting for a particle-editor bridge the original fracture tool does not implement. Use the editor's export to keep its generated assets.

Browser proof (`Tools/FractureBrowserProof.mjs` in the prototype) opens the real native entry button, renders the source specimen, bakes **48 closed pieces / 200.7 KiB**, and returns to the scene with zero page/console errors. Screenshot/results are outside Git at `/home/user/fracture-proof/`.


### Precipitation component

The authoring toolbar now has **Precipitation**. It saves the active particle tool and opens the scene precipitation component, rather than spawning another editor-only particle preset. The supplied `ProjectZeroEditor/NativePanels.json` precipitation control schema is adapted in `Working/Precipitation/Component.js`, with cloud-only GPU snow, shared scene wind and geometric terrain/vehicle accumulation. See `Docs/GPU_PRECIPITATION.md` for explicit budgets and limits. The upstream copy remains unchanged.


### Current precipitation replacement: celestial GPU port

The rejected plane shower / connected accumulation coating has been replaced, not just retuned. Runtime behavior is adapted from the deployed celestial demo (`da3720b353dc0e4d2421f064f65cbfeb41cdb5f2`): `Working/Precipitation/CelestialReference.js` and preserved `CelestialOriginal.txt`. Its original `prStep` is CPU/Canvas; our port is GPU compute/raster. Hail rebounds up to three times before rest, then solid precipitation converts into opaque triangle mesh grains. No continuous snow coating is rendered. See `Docs/GPU_PRECIPITATION.md` for verified behavior and explicit scope limits.

The celestial panel's actual 3D gizmo source excerpt is preserved at `Working/Gizmo/CelestialOriginal.txt`; its native-editor port and reference constants are documented in `Docs/CELESTIAL_GIZMO.md`. The Three.js 0.160.0 runtime is locally vendored with its MIT license.
