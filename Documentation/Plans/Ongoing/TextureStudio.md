# Texture Studio application plan

## Authorized direction

Texture Studio is the first material-focused standalone application built on Frontier's shared Engine code.
Implement the Material Library first; Frontier embedding follows using the same library and UI, not a second app.
The single progress tracker is `Documentation/Tracker.md`. This document defines scope and acceptance,
not another tracker.

## File format

The current authored material extension is **`.material`**. Its contents are versioned **TOML**, not JSON or a binary
shader archive. Reuse `Engine/Render/SurfaceMaterial.{h,cpp}` and pinned toml++. `Format = 1` describes the existing
opaque/coating contract. Do not silently interpret the reference app's `.texture.json` projects as native materials.
Texture paths remain relative to an explicitly selected `ContentDirectory`. Subfolders do not change that root.
Library indexing must not require a new database format or migrate existing project formats.

## Reference update and ownership

- Inspect and selectively import `Experimental/TextureStudio` from PR #1, without merging unrelated repository work.
- Pin the imported source revision and record local overlays. Preserve both painting apps and their existing projects.
- Keep the React/Three app as an executable interaction reference, including its GPU painting improvements.
- Reference WebGL shaders are not the native Vulkan renderer; unsupported optical/painting features are not ported
  merely because they exist in the reference app.
- `Experimental/TextureStudio`: reference app and initial standalone application composition.
- `Engine`: reusable catalogue, material loading/compilation/saving and application startup/presentation mechanisms.
- `UI/TextureStudio`: reusable native library UI, usable standalone and eventually inside Frontier.
- `Exhibits/TextureStudio`: validation programs, reports and actual captures.
- Existing renderer evidence remains in `Exhibits/Renderer`; remove validation modes from the Project Browser HTML
  and expose them in the separate native/ImGui evidence review instead. Retain tests and original browser samples.

## Delivery sequence

| Sequence | Implementation                              | Acceptance                                |
| -------- | ------------------------------------------- | ----------------------------------------- |
| 1        | Update reference; document native contracts | Pinned source and reproducible checks     |
| 2        | Shared catalogue and edit transactions      | Scan/filter/open/apply/revert/save tests  |
| 3        | Shared startup and standalone window        | Shared GLFW/Vulkan/ImGui initialization   |
| 4        | HTML-matched docked grid and inspector      | Visual parity and real shared controls    |
| 5        | Preview and GPU thumbnail atlas             | Live preview; resident GPU atlas tiles    |
| 6        | Windows delivery and lifecycle coverage     | Runtime, DPI, resize and resource checks  |
| Later    | Embed the same library in Frontier          | No duplicated UI, operations or rendering |

## Native presentation contract — corrected 2026-10-18

The HTML Material workspace is the visual specification, not loose inspiration. The first source/list editor is
only an IO/interaction increment; its default ImGui appearance and fixed two-column layout are not the approved UI.

- Match the reference Material workspace: library card grid, centre material viewport and right property inspector.
- Match typography, colours, spacing, card geometry, selection/hover treatments, icons and property-row proportions
  at the same viewport size, DPI and theme. Reuse shared fonts, themes and semantic icons.
- Reuse `ControlKit::Slider`, `SectionCard`, property pills, dropdowns and other existing shared controls.
  Extend a reusable control only where reference parity requires it; do not create TextureStudio-only copies.
- Host library, viewport and inspector as actual registered dockable panels through the inherited `PanelDockspace`.
  Default docking must reproduce the HTML arrangement, not a permanently fixed `BeginChild` split.
- Reuse the patched tab geometry and existing docking/resize behaviour. Native detached windows remain an explicit
  preference, disabled by default; do not equate internal docking with enabling multiple OS windows.
- Persist layout through existing `.projectspace` workspace conventions, not a new ImGui-owned settings file.
- Keep raw TOML as a secondary source view, not the primary material inspector.
- Material Library remains the current scope. Preserve the reference painting workspaces without adding inert
  painting/baking controls to the native app.

Acceptance requires side-by-side reference/native captures at matched dimensions and DPI, plus real docking,
resizing and shared-control interaction checks. The existing CPU source-editor capture does not satisfy this gate.

## GPU-resident thumbnail atlas

Use one shared GPU-resident colour atlas for resident material thumbnails, not one texture per card and not a
CPU-rendered or read-back/uploaded thumbnail path. This is separate from the material compiler's source-texture
arrays and from the full-size interactive material viewport.

```text
material / texture / lighting / shader revision changes
    → invalidate affected atlas slots
    → prioritize visible cards, then bounded prefetch
    → shared Vulkan material evaluation renders dirty tiles on GPU
    → image synchronization makes atlas writes available to UI sampling
    → shared card UI samples the atlas using tile UV rectangles
```

- Engine owns atlas allocation, slot allocation/reclamation, revision keys, GPU rendering and synchronization.
  UI owns catalogue visibility/selection and supplies visible-thumbnail requests, not Vulkan resource ownership.
- Bake with shared material rasterization into a GPU scratch target, then GPU-copy only the dirty tile and its
  gutters into the atlas. Preserve unaffected tiles; do not create a renderer or pipeline per material.
- Use one sampled atlas image/descriptor for cards. CPU metadata may contain slot tokens, UV rectangles, revisions
  and scheduling information; thumbnail pixels stay on GPU during normal application operation.
- Bound atlas dimensions and GPU memory against device capabilities. A large library uses a bounded resident cache
  with slot reuse, not an unbounded atlas containing every material at once.
- Recycle slots only after their last GPU use retires. Reject stale slot tokens and preserve valid thumbnails while
  edited materials fail compilation. A failed new material gets a diagnostic, not another material's recycled tile.
- Include material/texture contents and preview configuration revisions in invalidation. Apply refreshes affected
  tiles; Save alone must not rebake an unchanged applied material. Scrolling must not rebake clean resident tiles.
- Provide tile gutters and safe UV filtering; do not generate a whole-atlas mip chain that blends adjacent cards.
  Keep colour encoding correct between material output, atlas sampling and UI presentation.
- Budget dirty-tile work per frame and use explicit write/sample dependencies. Do not block the entire device for
  every thumbnail. Atlas replacement and shutdown must retire in-flight resources safely.
- GPU residency is not disk persistence. Rebuild thumbnails after application/device recreation; any future disk
  cache is a separate explicit feature, not an implicit CPU copy of every atlas update.
- GPU readback is permitted for Exhibits validation/captures, not the normal card-rendering path.

Verification must cover dirty-slot isolation, stale-slot rejection, eviction/reuse, unchanged resident tiles,
filtering at tile edges, correct colour, in-flight resource safety and material/texture invalidation. Record actual
GPU execution separately from CPU scheduling checks, compilation and Windows/hardware execution.

## First standalone workflow

```text
Texture Studio executable
    → shared initialization: window/input, Vulkan, ImGui, content, fonts/themes
    → HTML preset catalogue copied to personal application content, or explicitly supplied library directory
    → scan .material files beneath ContentDirectory
    → select → decode → compile textures/parameters → render preview
    → edit → Apply → Save, or Revert
    → shared shutdown and safe resource retirement
```

Do not launch the CAD Project Browser or insert renderer evidence specimens into user libraries.
The user explicitly requested the HTML library: ship its actual 127 presets, not invented replacements.
Use Frontier's shared lifecycle and settings/preferences conventions. Application integration must not create a
parallel settings format. Persistent last-library restoration needs explicit integration with existing projectspace
preferences; until then accept an explicit directory rather than claiming persistence.

## Material Library scope

- Recursive, deterministic catalogue of `.material` files, with names, relative locations, layer counts and diagnostics.
- Search by name/path; invalid documents remain discoverable, with errors rather than silently disappearing.
- Root-contained paths, no symlink escapes, bounded reads and catalogue traversal.
- Open a document without modifying it. Keep authored source, applied material and disk contents distinct.
- Apply validates material syntax and texture compilation before replacing the last valid applied material.
- Revert restores the last successfully loaded/saved source, not an invented default.
- Create and duplicate refuse overwrites. Save verifies the on-disk source has not changed externally.
- Use the existing `.material` schema; preserve source comments during ordinary source editing/saving.
- Structured layer controls reuse the same edit/apply path; no separate UI-only material meaning.
- Preview sphere/plane/bracket, orbit/zoom and exposure; thumbnails use shared renderer resources and invalidation.
- Library catalogue operations and IO must be usable without ImGui or a window.

## Verification gates

Local CPU checks cover invalid documents, nested directories, Unicode, filters, absent roots, symlink escapes,
texture compilation, apply failure retaining the last valid material, source/disk conflicts, create/duplicate safety,
save/revert and source preservation. Add ASan/UBSan runs and native ImGui draw/interaction tests.

Retain existing material/Vulkan regressions. Browser review, CPU ImGui execution, Windows cross-compilation,
Windows execution and hardware GPU execution are distinct results. Never describe a captured HTML page as a native
window or a cross-built executable as executed on Windows. Do not run GitHub Actions without permission.

## Deferred work

Painting, baking, procedural families other than automotive paint, general asset browsing, Frontier embedding, complex-IOR conductors,
transmission/refraction, SSS, multiple scattering, HDR environment transport, GI/RT/ReSTIR and renderer performance
claims are outside the first Material Library milestone. Preserve reference features without displaying inert controls.

## HTML library and presentation correction — 2026-10-18

The native default is now the actual 127-entry HTML library. Complete normalized recipes and control definitions
are retained in typed TOML; native `.material` definitions compile physical coefficients and the executable automotive paint block. This does not
close procedural shader parity. An explicit directory remains untouched by default content installation.

Native styling now uses the HTML panel sizes, 79px header region, dark 30px dock strips, collection/category pills,
125px cards, rounded inspector sections, shared 24px/22px sliders and matching DM Sans source faces. Shader-ball
profile equations and pose follow the HTML. The shader ball now has a rendered pedestal, foot and ring; shared/scoped semantic icons and scrollable inspector cards are implemented. Remaining reference interactions, viewport tools, other procedural shader families, complete reference shading and true Windows captures still block 1:1 visual acceptance. Do not close the
user's 1:1 requirement based only on the improved layout, exported catalogue or portable test results.


## Automotive paint and scoped icon correction — 2026-10-18

- Port the actual HTML cellular flake/binder/orange-peel equations into shared native Slang, not metadata-only recipes.
- Preserve all automotive fields through strict `.material` codec, Apply/Revert/Save/Duplicate and immutable GPU parameters.
- Render the original-dimension tapered stand, lower foot and metallic trim as classified GPU geometry; paint edits must not colour the base.
- Inspector cards use shared surfaces/sliders, wrap descriptions, collapse and scroll without nested opaque rectangles. Correct numeric units/log scales, disabled alpha and tooltip hit regions.
- Shared editor symbols live in `Engine/Interface/Icons/EditorSymbols.inc`; Texture and Fluid have separate scoped catalogues. `IconArt::Catalogue`, `Available`, rasterization and presentation enforce domain access, including cache hits. Editor icons remain reusable everywhere.
- New local automated checks: `VerifyProceduralPaint.py` (strict/ASan/UBSan actual GPU sensitivity and scoped SVG rasterization), plus the existing library/workspace/atlas/renderer regressions. CTest registers icon checks automatically and GPU paint checks when shaders/driver are explicitly provisioned.
- Package a linked Windows x64 executable, fonts, icons, 127 presets, ten compiled shaders, notices and file fingerprints with `Automation/BuildTextureStudioWindows.py`. A linked cross-build is not Windows execution; the user's runtime/hardware test remains open.


Verification completed: 250 strict/sanitized library/UI checks per mode; 58 paint and 90 icon checks per mode;
strict/sanitized atlas and existing renderer regressions; actual linked Windows x64 GUI test ZIP with verified
payloads and fresh source/shader fingerprints. See `Exhibits/TextureStudio/Results/DeliveryChecks.json` and
`WindowsReadme.md`. User Windows launch/hardware testing is still required; other procedural families and
1:1 appearance remain open rather than being closed by the successful automotive-family implementation.


## Standalone applications and optional Frontier workspaces — authorised 2026-10-18

Each application must run independently and expose the same reusable native UI/rendering implementation when
its Frontier extension is enabled. Disabling an extension removes its workspace, commands and specialised icons;
it does not delete authored content. Standalone and embedded execution share initialize/activate/loop/finalize
contracts, formats, diagnostics, theme/fonts and GPU resources. No React wrapper or duplicated engine is planned.

| Application / extension | Standalone scope | Frontier scope when enabled | Delivery order |
|---|---|---|---|
| UV Workspace | UV islands, seams, packing and distortion inspection | UV workspace and Texture-scoped tools | After material acceptance |
| Texture Workspace | Current build: Material Library, Preview and Inspector only | Same docked material UI; painting later | Current delivery |
| Parametric Sketch Workspace | Constraints, dimensions and sketch solving | Drafting/sketch workspace | After shared extension contracts |
| Fluid Editor | Water/smoke authoring and simulation inspection | Fluid workspace; Fluid-only icons | Planned |
| Fracture Editor | Partitioning, fracture constraints and inspection | Fracture workspace | Planned |
| VFX / Particle Workspace | Particle sources, motion and visual-effect inspection | VFX/particle workspace | Planned |

**SolidArc test executable opens only the Project Browser.** Opening/selecting a project is validated and logged,
but this test executable must not silently enter the general editor or a CAD viewport. Texture Workspace exposes
only the three material panels, not Pattern/Texture/Stamp/Baking tabs. Existing painting prototypes remain intact.

### Current implementation and CI sequence

1. Add interactive zoom out/reset/in pill, wheel zoom, preview geometry selection and environment/exposure controls.
2. Keep thumbnail geometry/lighting fixed; preview changes dirty only the full-size GPU target. Reuse programs,
   uploaded geometry, resident material textures and stable preview/atlas descriptors.
3. Supply Studio Softbox, Warm Courtyard, Cool Rim and Overcast lighting presets with real GPU parameter changes.
4. Add bounded per-run diagnostic files: startup/content roots, fonts/icons, Vulkan device/driver, shaders,
   selected material/revisions, preview settings, atlas work, persistence and severe exceptions. Never log source
   documents, credentials or every pointer movement. Flush actionable events; retain eight runs, 8 MiB per run.
5. Publish the required sources on this session branch (user explicitly approved), dispatch Windows GitHub Actions,
   compile shaders from `Engine/Render/Shader` and place runtime SPIR-V in `EngineContent/ShaderArchives/Surface`.
6. Produce one test artifact with SolidArc.exe, TextureWorkspace.exe, shared content, licences, SHA manifest,
   launch instructions and diagnostic collection instructions. CI logs are uploaded even if compilation fails.
7. User acceptance: Windows startup, browser-only SolidArc, material-only Texture Workspace, all four shapes,
   zoom/reset, all four environments/exposure, green paint and Apply/Revert/Save; collect logs on any failure.

### Next after this build

- Fix defects from user Windows/GPU-driver logs before adding more tools.
- Close remaining HTML/native lighting, viewport and inspector differences; port the remaining procedural recipes.
- Define extension availability and `.projectspace` persistence/missing-extension diagnostics, then embed the same
  Texture UI in Frontier behind an explicit enable toggle.
- Implement UV standalone/embedded parity, then parametric sketching; begin Fluid/Fracture/VFX as independently
  tested applications with domain-specific icons, content and GPU budgets. These future applications are planned,
  not represented as implemented by the current CI build.
