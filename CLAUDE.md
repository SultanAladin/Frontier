# Frontier — Agent Instructions

Frontier is a general-purpose, cross-platform editor with reusable engine code.
The legacy migration is complete. Do not treat this as an unfinished 1:1 migration target.

## Mandatory reading — every session

Read all four documents at session start, before naming, formatting, editing, or presenting work.
They are authoritative and override agent defaults within their scope:

- `EngineDocs/AgenticInstuctions/SKILL-Naming.md` — all naming, banned words, and name construction.
- `EngineDocs/AgenticInstuctions/SKILL-formatting.md` — source and document formatting.
- `EngineDocs/AgenticInstuctions/SKILL-Emoji.md` — the only approved emojis and permitted usage.
- `EngineDocs/AgenticInstuctions/SKILL-CompactOutputProtocol.md` — compact output; tables and diagrams over prose.

Report missing rules before affected work. Do not silently skip them or invent exceptions.
These paths remain authoritative until an approved relocation updates every reference.

## Content, formats, and rendering

- Use **Content Directory**, `ContentDirectory`, and `EngineContent/`.
  Do not introduce `AssetDirectory` or `UI/Assets/`.
- Shared fonts belong in `EngineContent/FontArchives/<font name>/`, with the original licence beside each collection.
- Shared SVG content belongs in `EngineContent/Icons/`. Resolve bundled content relative to the executable first;
  launching from another working directory must not hide fonts or pick an unrelated partial collection.
- All engine-authored C++ configuration and serialization formats use **TOML**, decoded/encoded with **toml++**.
  Preserve established schemas and extensions; inspect existing codecs before creating another format.
  `Appearance.toml` stores applied theme/typography; `FontCatalogue.toml` and per-font archive TOMLs describe fonts.
  JSON lock manifests and exhibit reports are automation interchange, not new C++ runtime serialization formats.
  External file formats such as OpenType, SVG, and shaders are not converted into TOML.
- **Vulkan is the primary rendering API. Slang (`.slang`) is the primary shader language.**
  DX11 is permitted for the isolated ImGui display exhibit; it is not a replacement engine-renderer architecture.
  Keep the scene renderer excluded from the current selective UI port.
- **GLFW owns platform windows and input.** Use a no-OpenGL-context window for Vulkan or the DX11 ImGui exhibit.
- Third-party sources belong under the existing ignored `ExternalPackages/`, restored from pinned dependencies.
  Reuse the existing toml++, GLFW, ImGui, and ThorVG dependency setup; do not vendor duplicate copies elsewhere.
- The Control Centre is a top-edge drawer, not a separate floating settings window. Retain the reference's
  trapezoid, travelling sheet, bottom grip, axis-locked drag, and upward swipe/flick dismissal.
  Typography sliders and scroll content retain their pointer ownership; drag the header, quick-card blank space,
  notch, or bottom grip to carry the sheet. GI and renderer settings remain excluded.

## Work and verification

Complete the requested work, run relevant local verification, then deliver results with evidence.
Local builds, tests, focused validation programs, and actual application captures need no separate approval.
Never deliver an implementation as complete without testing it.

| Work                  | Required verification                                             |
|-----------------------|-------------------------------------------------------------------|
| C++ or shaders        | Relevant build and tests; runtime validation where applicable.     |
| UI                    | Relevant tests and actual rendered or interaction evidence.        |
| Behaviour repairs     | Reproduce the defect and verify the repaired behaviour.            |
| Performance claims    | Reproducible measurements, conditions, and comparison results.      |
| Documentation         | Paths, links, structure, consistency, and applicable rule checks.   |
| Plans and analysis    | Verify factual claims against inspected sources; show a report.    |

- Exercise the changed behaviour, not merely unrelated passing tests.
- Add focused validation when existing coverage is insufficient.
- Compilation alone does not establish runtime correctness.
- Progress messages must not imply unfinished work passed verification.
- When blocked, report the command, observed result, missing prerequisites, and remaining checks.
- Mark unavailable checks blocked or not run, never passing. A blocked report is not completed implementation.

## Visible evidence — required with delivery

Every delivery must include an actual image, graph, results table, or concise verification report.
UI appearance claims require actual rendered evidence when capture is supported.
Mockups, generated illustrations, and invented measurements are not validation evidence.
A browser prototype does not prove that the production C++ editor renders the same result.

Retain evidence and its generating code together under `Exhibits/`.
Include reproduction commands, prerequisites, platform, compiler, configuration, tested revision,
and any relevant uncommitted changes. Explain limitations and present the evidence, not just its location.

## Folder ownership and documentation

Follow `Documentation/FolderStructure.md`. Do not invent alternative top-level folders.

| Location                         | Responsibility                                        |
|----------------------------------|-------------------------------------------------------|
| `Engine/`                        | Reusable production code shared across applications.  |
| `UI/Editor/`                     | Production editor UI.                                 |
| `Exhibits/`                      | Validation code, retained results, and proof images.   |
| `Exhibits/EditorMirror/`          | Comparison mirror of the C++ editor UI.                |
| `Experimental/`                  | Prototypes, experimental HTML, and related assets.     |
| `Documentation/Tracker.md`       | Single project progress and violation tracker.        |
| `Documentation/Rules/`           | Project rules; four retained skills remain above.     |
| `Documentation/Plans/Ongoing/`   | Approved active plans.                                |
| `Documentation/Plans/Completed/` | Completed plans, outcomes, and evidence links.         |
| `_ClaudeScratch/`                | Disposable local work; ignored and never committed.    |

Do not put prototypes, captures, reports, or validation-only code in `Engine/`.
Exhibits must reuse production engine code, not independent copies of it.
Keep requested prototype source, assets, and launchers together in `Experimental/`.
Do not delete inherited build, dependency, content, or application folders without an approved migration.
Update the folder map and affected references whenever approved moves change paths.
Maintain one tracker, not per-agent progress reports or competing backlogs.
Keep detailed results in exhibits and link them from the tracker.
Do not silently weaken rules to make a scan pass.

## Scratchpad

Use `_ClaudeScratch/logs/`, `_ClaudeScratch/build/`, and `_ClaudeScratch/tmp/` for disposable material.
Never commit scratch contents or leave the only copy of a deliverable or retained evidence there.
Permanent prototypes go in `Experimental/`; retained validation code and results go in `Exhibits/`.
Respect existing ignored build-output conventions when tools require their own directories.
Do not leave generated output next to source or loose at the root.
Incomplete HTML fragments use a non-`.html` extension such as `.part`.

## Platforms, binaries, and tooling

The user is on Windows. Deliver Windows binaries by default when a binary is requested or part of the task.
Do not build, package, or deliver Linux binaries unless requested.
Never substitute a Linux executable for a Windows deliverable.
If Windows compilation is unavailable, report the blocker; do not claim a Windows binary was verified.

Preserve Windows, Linux, and macOS portability. Android is a future consideration; no Android app is in scope.
Do not download macOS binaries or set up macOS during current work.
MSVC is the user's current compiler, not the only permitted compiler.
GCC/g++, MinGW-w64, or Clang may be used where the repository supports them.
Distinguish locally tested configurations from untested portability expectations.

Use actual repository build instructions and available tooling; do not hard-code developer installation paths.
Isolate platform-specific code from reusable code. Avoid unnecessary Windows-only assumptions.
Use the appropriate host interpreter: Windows command tooling for `.bat`, PowerShell for scripts requiring it,
and Bash for appropriate Linux/macOS commands. Python need not live at a fixed path.

## GitHub Actions — explicit approval required

Always ask before causing a GitHub Actions run unless the user explicitly authorised that run.
This includes dispatches, reruns, and remote operations such as pushes or pull requests that trigger workflows.
Inspect triggers before an authorised remote operation. Never use Actions as an automatic local-test fallback.
Permission to edit or commit locally is not permission to run Actions.
Follow the user's current commit and push instructions; do not infer permission to commit or push.

## Delivery

Follow the compact output skill: short outcome, verification table, visible evidence, exhibit paths,
blockers, untested configurations, and remaining work. Do not substitute confidence or compilation for testing.

### Recovered native Float32 surface maps (2026-10-19)

`VectorSurfaceCompute` writes typed RGBA32F channel maps and separate per-channel contribution masks; coverage/selection
stay RGBA8. `VectorCoverageRasterization` derives the actual storage format from SPIR-V and refuses unsupported Float32
capabilities instead of silently quantizing. The shared PFM writer preserves float bits and emits little-endian,
bottom-first scalar/RGB output. Seven GPU channel assertions pass strict and sanitizer builds; the archive contains
14 actual 32x32 GPU fixture maps/masks with separately authored gold thread and fabric metalness. See
`Exhibits/TextureStudio/NativeSurfaceEvidence.html` and `NativeMaterialApplication.md`.

The retired-slot cache path dispatches and samples the 512×512 Float32 surface output. Texture Workspace now toggles
to the native Pattern panel, persists vector TOML, and submits GPU coverage/selection. Its C++ runner passes 606 checks;
the recorded evidence explicitly does not represent a Windows desktop session. Pattern Float32 outputs remain
unconnected to the PBR material renderer; native 3D application, physical height calibration, user-document export and
Pattern 1:1 parity remain open. GIF previews are 8-bit evidence, not precision checks. Scope remains approximately 68%.

## Landscape workspace — user-pinned references

The isolated browser prototype is `Experimental/LandscapeWorkspace/`; reproducible checks and actual captures are
under `Exhibits/LandscapeWorkspace/`. Do not mistake browser evidence for native-editor verification.

| Reference              | Immutable revision                         | Purpose                          |
|------------------------|--------------------------------------------|----------------------------------|
| `c7egoist/Frontier`     | `09b7064f282f4df5c5352f2bc97c82fb1c06a098` | Requested SDF/geology/stack source |
| `SultanAladin/Frontier-` | `28ec0657a23ed6bbf680f17cc06b464a53639dae` | ProjectZero / FrontierEditor UI   |
| Previous prototype     | `4b26efcdaa15e5df09e8a756c184e881e6f8566e` | Imported starting point to repair |

- **Requested SDF reference:** <https://github.com/c7egoist/Frontier/tree/arena%2F68c84c0d-frontier>.
  Keep its immutable pin and original source fingerprints in
  `Experimental/LandscapeWorkspace/ReferenceSnapshot/SourcePin.json`.
- **Requested editor source:** <https://github.com/SultanAladin/Frontier->,
  `Experimental/FrontierEditor/style.css` and `src.jsx`, pinned in `ReferenceSnapshot/ProjectZeroPin.json`.
  Use its final neutral-graphite theme and typography, not the erosion reference's coloured UI.
- Keep separate Mask and Landscape authoring: each mask owns its independent operation stack, with optional
  boundary-only operations and explicit scope. The left stack, central viewport and right inspector are intentional.
- Authoritative masks remain world-space vector strokes. Only painted, explicitly allocated regions create terrain.
  Keep genuine signed **3D** geometry; never substitute terrain height textures or a preallocated infinite solid.
- The foundation starts at **Y = 0**, with thickness below sea level. Terrain generators add radial/local relief and
  fade inward from the coast; do not reintroduce elevated soles or platforms.
- Show already-applied landform generators, not only thumbnails. Keep arches/tunnels separate as volume operations.
  Ocean waves must remain world-space procedural, without obvious repeating texture tiles.
- Full exports now support approximate metric redistancing: sparse anisotropic 3D fast sweeping, analytic crossing
  refinement, convergence/sign checks and V2 provenance. The live viewport rasterizes an approximate surface sampled
  from the canonical XYZ field.
  Do not call this exact Euclidean distance, exact subcell topology or simulation-ready erosion.
- The full reference erosion/simulation engine, baked-volume loading and native integration are not implemented.
  Never imply that retaining source snapshots, scoped ownership or metric export implements those solvers.
- Require OffscreenCanvas and a dedicated graphics thread; missing APIs stop Landscape, never create a UI-thread
  GL/CPU renderer. Preserve one context, bounded view/GPU queues, always-high geometry and Mask suspension.
  Context loss stops the preview without automatic retries. No unbounded images or hidden Mask uploads.
- Latest user correction: **do not use software rendering fallback**. Two physical PCs freeze or lose context.
  Hardware-acceleration status does not certify driver stability, but assume the app workload needs correction.
  No CPU pixel/ray renderer, no automatic fallback or context-recreation loop in the live viewport.
- GPU preview must not run the complete procedural SDF stack in a long per-pixel marching loop. Prepare a bounded
  surface from the same genuine XYZ field off-thread, then rasterize that surface on the GPU. Keep authoring and
  exports volumetric; never substitute a heightmap. Surface preparation is geometry work, not software frame rendering.
- Keep context loss fatal/paused until explicit user action. Preserve Mask/save and report actual GPU failure;
  do not silently launch expensive CPU rendering or claim that terminating a worker cancels submitted GPU work.
- Prior software/deadline reports are historical, not proof that the new GPU-only path works. Refresh relevant
  checks and actual captures before claiming completion. Physical Windows/driver behaviour remains unverified here.

- Latest quality direction: leave actual geometry high by default, not merely increase image pixels. Preserve
  shared-edge field-projected adaptive refinement within 120k triangles/1.1M samples; do not restore the ordinary
  16→48→64 upload sequence or per-pixel SDF loop. Image width stays at the 1440 ceiling with bounded measured density;
  the hard 1.5-million-pixel cap remains. Adaptive diagnostic quality 96 is a method, not a uniform target lattice.
- Camera distance must never retune ocean wave amplitude, wavelength or phase, or resize the world-space water grid.
  Footprint filtering may affect unresolved reflection slopes, not physical wave displacement. The shore texture
  stores signed mask distance, not terrain height. Preserve current GPU-uniform zoom regression and XYZ arch checks.

- Latest working-resolution correction: each allocated landscape region owns optional XYZ counts, e.g.
  **512 × 96 × 512** for 512 × 512 horizontally and 96 vertically. `VolumeLattice.js` is shared by region controls,
  working detail and normal export. Legacy layer defaults remain a fallback, not shared mutable region settings.
- Do not equate target counts, screen pixels or overview quality with resident detail. **View detail** / surface
  double-click prepares at most 64³ cells at authored XYZ pitch; show actual pitch/coverage and budget limitations.
  Preserve independent regions, anisotropic normals, matching dyadic tetrahedral transition tests and cancellation.
  Keep 120k triangles, 1.1M samples, one graphics context and unchanged GPU deadlines; never allocate the whole
  requested lattice densely just to make its label true.
- Region-owned `SculptStrokes` now provide actual XYZ Clay/Carve capsules, world radius/strength, canonical picks,
  validation, atomic release-only history, save/import and sparse export. Protect all other regions/layers. Keep
  256 strokes/region, 2,048 XYZ points/document and 256 points/gesture. Smooth/Flatten, adjustable falloff,
  continuous-drag geometry, complete paging, simulation and baked-volume editing are not implemented.
- Sculpt opens native detail; close zoom requests it automatically. Alt-drag orbits while sculpting and negative
  pitch permits underside work. Keep unchanged cameras during actual stroke application and atomic undo/redo.
- The original low-poly screenshot exposed the old 64-step whole-view limit, not a proven server/cache/fallback
  cause. Current shared-edge refinement is genuine geometry but not fully resident authored-resolution whole worlds.
- Keep target and **GPU-fence-completed** sampling readouts separate, including narrow layouts. Prepared/uploaded
  telemetry cannot certify the image already displayed. Preserve the high/working badge, separate image dimensions,
  driver/revision diagnostics and 0.5-metre minimum orbit. Automatic focus can clamp to empty region-edge coverage;
  explicit surface focus remains necessary there; do not imply complete camera-driven paging.
- Current focused sculpt checks pass 10/10 in dev/built mode; 183 units and the build pass. The broad editor still
  hits the unchanged four-second GPU fence while hiding all terrain. Keep `GpuEditorSculptBatchFailure.json`, the
  latest broad failure, and the pre-sculpt two-page resume failure separate from passing isolated resume checks.
  Do not claim all-green stability, weaken deadlines or diagnose physical Windows GPUs from these lab observations.
