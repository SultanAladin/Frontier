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

## Pinned landscape / landform reference — 2026-11-06

- Requested source: `https://github.com/c7egoist/Frontier/tree/arena%2F68c84c0d-frontier`.
- Immutable revision: `09b7064f282f4df5c5352f2bc97c82fb1c06a098`.
- Source roles: `src/erosion/terrain-stack-presets.js` defines operation defaults and bottom-to-top stack conventions;
  `src/erosion/pipeline-shaders.js` includes height-texture operations; `src/geology/polyhedron.js` supplies volumetric
  half-space / ellipsoid geometry. Do not mislabel the entire reference pipeline as volumetric-only.
- Verified source snapshots and Git blob identities: `Experimental/LandscapeWorkspace/ReferenceSnapshot/SourcePin.json`.
  These are read-only reference files, not native engine dependencies. Preserve upstream names and notices there.
- Landscape prototype remains volumetric SDF/CSG. Adapt landforms to signed 3D geometry; do not replace it with a
  heightmap or claim that geological presets alone constitute a physical erosion simulation.
- Each mask layer owns its independent `Layer.Landforms` stack, applied within all of that mask’s boundaries.
  Boundaries own edge profiles, not the primary stack. Keep imported `Region.Landforms` as legacy refinements; never
  silently discard them. Active-mask edits and explicit selected-mask batches must not alter unselected mask stacks.
- User clarified landforms mean terrain generators (fractal, multifractal, ridged multifractal, badlands, ridge noise,
  dunes, craters), not arches or a gallery-only preset showcase. Examples must be already applied to editable terrain.
  Keep arches/cuts in a separate optional SDF category. Preserve mask-owned operation stacks and true volumetric fields.
- Source UI alignment uses pinned `index.html` and `src/app/style.css`: neutral charcoal, squared outliner entries,
  cyan selection, generator properties. Do not claim pixel/feature-complete parity. Pan and opt-in WASD are available.
- Current evidence: 44 local tests; 9 terrain/navigation, 15 mask-stack, and 12 general browser checks pass. See
  `Exhibits/LandscapeWorkspace/TerrainGenerators.md`; older preset-first captures are not the current primary examples.


## Layer-stack reference correction — 2026-11-06

- The user explicitly rejected a theme-only approximation: use the same layer-stack UI as the pinned c7egoist source.
- The main outliner now uses the actual source row structure, mechanically extracted stylesheet cascade, source Lucide
  glyphs, numbered steps, eye/mask controls, selection-only actions, add menu, search and drag ordering. The active mask
  selector switches independent stacks; do not revert to the former tiny nested custom operation cards.
- Preserve the final source overrides: square layer entries, coloured glyphs without tiles, rounded surrounding controls.
  `ReferenceSnapshot/` now retains six upstream files; `ReferenceStack.css` is the extracted runtime stack styling.
- Current verification: 46 local tests; 11 reference-stack + 15 mask-stack + 9 terrain/navigation + 12 general browser
  checks pass. All 24 checked computed-style values match an original-template/full-source-stylesheet fixture.
- Actual current UI captures and reproduction: `Exhibits/LandscapeWorkspace/ReferenceLayerStack.md`. Earlier colour-only
  UI captures are superseded. This changes the UI, not the volumetric geometry into a heightmap or fake simulation.

## Separate workspace stacks and radial influence — 2026-11-06

- Mask editor's primary stack is mask documents; Landscape's primary stack is the active mask's landform operations.
  Never show the same operation outliner in both tabs. Preserve source-style operation rows and independent ownership.
- Terrain generators must start from the flat foundation and introduce relief through bounded radial influence.
  Strength, radius, falloff and centre offsets are serialized per operation, validated and included in compilation keys.
- Continuous terrain profiles use an analytic 3D signed field with bounded vertical noise distortion; no height textures.
  Normalize conservatively after world-unit terrain blending. Preserve explicit cave/arch/structure CSG separately.
- Current focused evidence: 54 local tests; 8 new workspace/radial browser checks; 540 CPU/GPU points, maximum error
  0.000054973. Reference UI (11), mask ownership (15) and general browser/bake (12) suites also pass.
- Current mechanism, actual two-stack/dune captures and reproduction:
  `Exhibits/LandscapeWorkspace/RadialTerrain.md`. Older scattered-generator captures are superseded.

## Sea-level foundation correction — 2026-11-06

- The user rejected the raised slab / thick sole. Foundation top is exactly ocean mean level Y = 0, not Depth × 0.2.
  Its floor is −Depth; underwater depth must never lift terrain. Anchor generator profiles and explicit structures at zero.
- CPU field, GPU extrusion, clipping, compile bounds and export all use the corrected vertical convention. Existing saved
  documents receive the correction through evaluation, without destructive operation migration.
- Current verification: 56 local tests, 9 focused browser checks, 540 CPU/GPU points (max error 0.000057776).
  Current actual captures: `SeaLevelDuneInfluence.png`, `SeaLevelFlatFoundation.png` and `SeaLevelMaskStack.png` in
  `Exhibits/LandscapeWorkspace/`. Earlier broad-suite results are historical, not reruns of this vertical correction.
