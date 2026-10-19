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

**Frontier test executable opens only the Project Browser.** Opening/selecting a project is validated and logged,
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
6. Produce one test artifact with Frontier.exe, TextureWorkspace.exe, shared content, licences, SHA manifest,
   launch instructions and diagnostic collection instructions. CI logs are uploaded even if compilation fails.
7. User acceptance: Windows startup, browser-only Frontier, material-only Texture Workspace, all four shapes,
   zoom/reset, all four environments/exposure, green paint and Apply/Revert/Save; collect logs on any failure.

### Next after this build

- Fix defects from user Windows/GPU-driver logs before adding more tools.
- Close remaining HTML/native lighting, viewport and inspector differences; port the remaining procedural recipes.
- Define extension availability and `.projectspace` persistence/missing-extension diagnostics, then embed the same
  Texture UI in Frontier behind an explicit enable toggle.
- Implement UV standalone/embedded parity, then parametric sketching; begin Fluid/Fracture/VFX as independently
  tested applications with domain-specific icons, content and GPU budgets. These future applications are planned,
  not represented as implemented by the current CI build.


### Windows Actions acceptance record — 2026-10-18

- **Passed:** [Windows Actions run 39142811899](https://github.com/SultanAladin/Frontier/actions/runs/39142811899).
- Built revision: `c586fd707283afea78462f8e467d6c85ba4c44e5` on `arena/73e163a3-frontier`.
- [Download Windows test applications](https://github.com/SultanAladin/Frontier/actions/runs/39142811899/artifacts/12142541200).
- [Build diagnostics](https://github.com/SultanAladin/Frontier/actions/runs/39142811899/artifacts/12141379773).
- Both native Windows x64 executables linked; all ten Slang entries compiled and packaged in the runtime directory.
- Five Windows CPU/native ImGui acceptance tests passed. Local GPU shape/environment/zoom/exposure, stable image
  views and thumbnail isolation passed; strict/sanitized material transactions each passed 259 checks, and actual
  preview pointer input passed both strict and sanitized execution.
- CI exposed and corrected Windows checkout line endings, native path separators after lexical normalization,
  and a binary-preservation test fixture. Authored material comments, conflict refusal and last-valid Apply remain tested.
- Runtime logs: `%LOCALAPPDATA%/Frontier/Logs`. `CollectDiagnostics.cmd` copies logs beside the test executables.
- Interactive Windows Vulkan startup and hardware-driver execution are **not** certified by the CPU CI tests.
  Use the packaged README checklist and send logs on failure. GitHub artifacts are retained for fourteen days.
- Evidence-only documentation is recorded after the compiled revision; no application code is changed by that record.


### Correction and distribution hold — 2026-10-18

The application names are **Frontier** and **TextureWorkspace**. The browser-only entry point is now under
`Experimental/Frontier/Native`; the incorrectly named application target/directory has been removed.
Prior binary downloads linked above are superseded and are not recommended for testing.

Before another binary is offered: verify pointer zoom/pan/orbit, wheel/keyboard input, reset/fit, pointer release,
widget/popup isolation, shared sensitivity/invert-pitch and GPU camera/normal transforms. Atlas thumbnails must
remain unaffected. Remove archive command scripts. Require valid publisher signatures, current clean Defender
results and executable hashes matching the security report. Unavailable signing/scanning means **hold distribution**,
not a fabricated safe result. Chrome reputation itself cannot be guaranteed or bypassed by this work.


### Navigation correction acceptance

- [Windows build and test run 39146539736](https://github.com/SultanAladin/Frontier/actions/runs/39146539736)
  passed for `dcfe53f`: Frontier and TextureWorkspace compiled, including Windows version resources and all ten
  Slang shader entries; all five Windows CPU/native ImGui tests and four fail-closed packaging tests passed.
- Local strict and address/undefined-sanitizer navigation tests passed with actual button, drag, wheel and keyboard
  events. Software-Vulkan readbacks verified pan and orbit alter the preview while keeping thumbnails and views stable.
- The native host delegates pointer sensitivity and pitch inversion to the shared applied input preferences.
- Updated visible evidence: `Exhibits/TextureStudio/Images/MaterialInspectionControls.png`.
- **No new executable download offered:** CI withheld packaging/upload because distribution trust requirements
  were not satisfied. Chrome warning resolution is not claimed. Next: obtain exact Chrome warning, configure
  legitimate trusted CI signing, obtain a current clean antivirus result, then reconsider distribution. Never request
  signing private keys in chat or recommend disabling protection. Interactive Windows/GPU startup remains unverified.


### Reference distribution investigation

- Inspected `SultanAladin/Frontier-`, not this repository, at historical revisions
  `7dd674484d7387cd360cc892b5a113414b37f78f` and `61c5be6de9df9c3a8a0cd28bece730d8693a3bd4`.
  The original UI import revision predates these workflows. No new Actions run was started.
- The desktop packager also used Python ZIP compression and included a diagnostic `.cmd`; its inspected
  workflow/toolchain/packager contains no application signing step. Neither scripts nor missing signing
  has been established as the cause of Chrome blocking this delivery.
- Standalone binaries used MSVC Release/static CRT on Windows 2022 and GitHub artifact/prerelease delivery.
- Networking used Epic's official configured bootstrapper, checked against the vendor binary, and direct
  GitHub releases with checksums and extracted GUI tests. This does not sign its custom application and
  is not an appropriate substitute launcher for Frontier/TextureWorkspace. Certificates were not inspected.
- User screenshot identifies arena.ai as the blocked download origin. Direct GitHub delivery is a meaningful
  provenance difference, not evidence of the cause or a reason to rehost a blocked archive unchanged.
- Evidence: `Exhibits/TextureStudio/Results/ReferenceDistributionComparison.json`. Next: identify the
  user's exact previously accepted reference artifact, compare actual archive contents/PE signatures and
  antivirus results against the blocked archive, then use traceable GitHub distribution for a verified build.
  Existing release hold remains. No runtime changes made for this investigation.


### Direct GitHub distribution correction

- Google documents separate dangerous, suspicious, unverified and insecure download categories:
  <https://support.google.com/chrome/answer/6261569?hl=en>. Enterprise documentation distinguishes
  file, URL and host verdicts: <https://support.google.com/chrome/a/answer/7579271?hl=en>.
  The screenshot alone does not expose the exact Safe Browsing verdict or detection reason.
- Added an opt-in GitHub prerelease job following the reference's direct-download provenance. It needs
  successful Windows trust verification, validates the original archive SHA-256 again, and publishes
  the original ZIP/checksum without Arena rehosting. Normal pushes do not create releases.
- Existing signature/antivirus hold remains; no self-signing, vendor launcher substitution, renamed
  blocked archive or protection bypass. A direct GitHub URL is not evidence that Chrome will accept it.
- Added per-application signature statuses and Defender readiness to check annotations and the Actions
  summary. Full scanner details remain in DistributionChecks.json.
- Local checks: workflow YAML parses; publication is opt-in, trust-dependent and least-privilege;
  checksum mismatch refusal is configured; four packaging refusal cases pass. Windows execution of
  the new summary/publication steps still requires CI; publication remains untested until trust is ready.

- Windows run <https://github.com/SultanAladin/Frontier/actions/runs/39148623800> passed at `9671944`.
  Its verification annotation reports `Frontier=NotSigned; TextureWorkspace=NotSigned; Defender=passed; ready=False`.
  This is a clean scan of the rebuilt output, not the original blocked ZIP. Packaging/upload remain skipped
  under the newly added signed-only policy; optional release publication was not invoked. Evidence:
  `Exhibits/TextureStudio/Results/DownloadTrustChecks.json`. Chrome verdict is still unverified.


### Authorized unsigned GitHub preview

- User explicitly selected **Scanned unsigned preview** after comparing it with the signed-only hold.
  This supersedes the earlier unconditional signed-only distribution rule for explicitly authorized previews.
- Both the CI report and packager must explicitly authorize unsigned output; default remains signed-only.
  Only `Valid` or explicitly authorized `NotSigned` statuses are accepted; invalid/untrusted signatures
  remain blocked. A fresh current clean Defender scan and executable hash match remain mandatory.
- Unsigned output is disclosed in the GitHub release title, `UNSIGNED-PREVIEW.txt`, signature report and
  package manifest. No browser block override is recommended. Chrome acceptance remains unverified.
- Publication can be explicitly requested by workflow inputs or the approved push marker
  `[publish-windows-unsigned-preview]`; ordinary pushes do not create releases. Only this session branch
  is used. No master push/merge or Linux package is involved.

- First authorized run `39149413532`: Windows build, five acceptance tests, 72 signature/scan policy
  combinations and Defender scan passed; unsigned readiness was true. Packaging refused a PowerShell
  single-string splat that expanded the option into characters. No binary was published. Replaced it
  with an explicitly typed string array and added an actual Python argument-boundary assertion in CI.

- Final run <https://github.com/SultanAladin/Frontier/actions/runs/39149823906> passed at `e412b34`:
  both applications, ten shader entries, five acceptance tests, four packaging refusals, 72 signature/scan
  policy combinations, fresh Defender scan, packaging, archive checksum verification and GitHub publication.
- Published the explicitly authorized **UNSIGNED** Windows preview directly on GitHub:
  <https://github.com/SultanAladin/Frontier/releases/tag/frontier-windows-39149823906>.
  ZIP and SHA-256 asset are available. No Arena-rehosted binary is offered. No signing is claimed.
- Remaining: user verifies Chrome accepts the direct GitHub download without overriding protection;
  only then perform the documented interactive Windows/GPU/input checklist. If blocked, stop distribution
  handoff and investigate/report the precise verdict instead of changing names/hosts repeatedly.
- Defender command uses a custom scan with `-DisableRemediation`; Microsoft documents that this ignores
  file exclusions and scans archives, while leaving detections unremediated for the gate to reject:
  <https://learn.microsoft.com/en-us/defender-endpoint/command-line-arguments-microsoft-defender-antivirus>.


### HTML-first camera lag and independent Pattern Layers

- User requested camera lag in both HTML and C++, starting with HTML. This iteration changes only
  the HTML prototype; native viewport clipping, material/shading parity, console/build switches and
  Frontier browser/notch/Control Centre verification remain outstanding. No Actions or binary release.
- Shared HTML camera navigation separates the input destination from the rendered camera. Orbit and
  pan converge in spherical/target coordinates; optical zoom converges logarithmically. Default time
  constant is 120 ms, elapsed-time based, without overshoot; reduced-motion preference disables lag.
- Blender gestures: MMB orbit, Shift+MMB pan, Ctrl+MMB optical zoom, wheel zoom and Numpad decimal
  frame. Frame/reset clears residual motion. Painting/point edits freeze the rendered camera and discard
  outstanding navigation, rather than moving under a stroke. The 2D authoring pointer stays precise.
- Pattern Layers is now an independent panel under the library, not embedded in the library/inspector.
  Pattern and Texture Painting share `LayerStackPanel` presentation, retaining their separate document
  operations and existing formats. This is not a claim of nested-folder/mask parity or draggable HTML docking.
- Browser evidence: `Exhibits/TextureStudio/Images/HtmlPatternLayers.png` and `HtmlPatternMaterial.png`,
  generated by `Exhibits/TextureStudio/Code/HtmlNavigationEvidence.mjs` from real DOM/software-WebGL.
- Native follow-up: reuse the same destination/displayed-camera separation and 120 ms convergence in
  the shared preview input/configuration code, preserving sensitivity/invert preferences, render-only
  dirtying, atlas residency and pointer/widget isolation. Validate native clipping/aspect and lighting
  independently before another binary; do not mark C++ parity implemented from HTML evidence.
- Verification: production build passed (existing chunk-size warning only); 4 camera/navigation tests,
  4 responsive layer-panel tests, 2 real rendered viewport tests, 1 five-workspace panel-layout regression
  and 1 real digital-painting/undo/cancel/erase/save-reload regression passed. Camera comparisons include
  5/30/60/144 Hz, reduced motion and paused auto-rotation. Final visual evidence also confirms orbit
  changes actual canvas pixels, not just telemetry. Results: `Exhibits/TextureStudio/Results/HtmlNavigationChecks.json`.
- Final visual capture inspected: independent Layers is visible in both design and 3D modes, with actual
  patterned mesh rendering. Browser evidence is Chromium/software-WebGL, not native Windows execution.
- Preservation: 40 unrelated backed-up paths remain byte-identical; no Engine/UI/native/workflow changes,
  commit, push or Actions run in this HTML iteration. Preview remains available on port 5173.

### C++ control surfaces, live coefficients and navigation cost diagnosis

- New user Windows screenshots confirm TextureWorkspace and show the remaining native control styling problems.
  This iteration addresses C++ control presentation/live edits, not native camera lag or viewport/shading parity.
- Material dropdowns now explicitly use opaque surfaces, rounded popup boundaries, padded 28 px option rows
  and 6 px separation. Workspace document menus/dialogues also receive padding and rounded boundaries.
- Procedural paint uses the existing shared ControlKit switch, not an ImGui checkbox. Colour controls open a
  padded, opaque, rounded hue-wheel panel with RGB/HSV/hex inputs and retained current/original comparison.
- Palette exposes every retained tint; Ramp shows the actual linear-colour interpolant plus editable tint stops;
  Single exposes only the first tint, retaining other entries for subsequent mode changes. Ramp rendering is
  attached to the property scroll-region draw list, not the obscured outer inspector draw list.
- Supported inspector sliders, numeric fields, colours, toggle and colour mode now publish validated live
  coefficients once per UI iteration. Save remains explicit; Revert restores saved coefficients. Manual source
  editing retains explicit Apply. Unsupported procedural recipe controls remain disabled and labelled.
- Live coefficient application reuses decoded texture pyramids and preserves compiled array slice assignments;
  changed texture declarations/stratum counts fall back to full compilation. Derived unresolved flake tint is
  recomputed using the same CPU tint evaluator as the visible ramp. Roughness/metallic interval edits retain
  ordered endpoints rather than becoming unresponsive when the endpoints cross.
- GPU coefficient revisions share unchanged texture residence. Preview parameter buffers/descriptor sets are
  reused only after their submission serial has completed. Navigation logging is coalesced to 250 ms samples;
  discrete shape/environment/exposure changes remain logged. No shader quality reduction was introduced.

| Confirmed cost | Motion impact | Follow-up |
| --- | --- | --- |
| Studio: 32 samples/proposal; 96 for paint + coat | Every changed view shades again | GPU timing; cached lighting |
| Three flake layers: 27 cells each | 81 searches/pixel, plus noise/normals | Derivative-safe flake LOD |
| Per-motion allocation/log flushing | CPU/driver/file I/O churn | Retired reuse and throttling implemented |
| Cached idle preview; FIFO present | Idle FPS does not measure shader cost | Profile CPU/GPU/present on Windows |

- No shader compilation or pipeline construction occurs per orbit event. Camera-only changes do not rebake
  clean atlas tiles; GPU regression checks cover that separation. Zoom can increase shaded pixel coverage in
  the existing fixed-size target; it does not imply that all 127 materials are rendered again.
- Evidence code: `Exhibits/TextureStudio/Code/MaterialControlSurfaceEvidence.cpp` and
  `VerifyNativeControlSurfaces.py`. Captures combine actual C++ ImGui commands and software-Vulkan readbacks;
  they are not Windows window screenshots or hardware-FPS measurements.
- Remaining: Windows CPU/GPU profiling, cached lighting/flake shader optimisation, prior full-panel/aspect
  correction, Blender navigation/camera lag in C++, full material detail parity and native Frontier verification.
  The previously published scanned unsigned ZIP is unchanged; these edits require a future authorised Windows
  build/scan/publication before they reach that downloaded executable. No Actions, push or release this iteration.
- Verification: final local CTests 5/5 passed; 343 material/library/native-control assertions and 72,751
  preview/geometry/input assertions passed. Strict and address/undefined-sanitizer regression executions passed.
  Software-Vulkan atlas/view regression passed, including no clean-tile rebakes, bounded retired parameter
  reuse and no new texture allocation on coefficient-only revisions. Actual hue-wheel input changed both
  applied coefficients and rendered preview pixels. Five native-control captures were generated/inspected.
- Results: `Exhibits/TextureStudio/Results/NativeMaterialControlChecks.json` and
  `NativeControlSurfaceChecks.json`; captures: `NativeDropdown.png`, `NativeFlakePalette.png`,
  `NativeFlakeRamp.png`, `NativeFlakeSingle.png`, `NativeColourWheel.png`. No hardware FPS gain is claimed.
  The 40 preserved unrelated paths remain byte-identical; docking implementation and native entry points
  are untouched. The HTML-first preview and both painting applications are retained.

### HTML review iteration — shared tooltips and Pattern paint sources (2026-10-18)

- Scope explicitly changed by the user to **HTML first**. Review/fix this prototype before any C++ port.
  No native Pattern implementation, new executable, publication or GitHub Actions is authorised by this step.
- One delegated, reusable tooltip surface covers existing tool titles and labelled icon buttons across the
  HTML workspaces. Rounded opaque cards inherit the hovered control's active theme, with directional pointers,
  viewport-aware placement, optional shortcut chips, hover delay, keyboard focus/Escape and retained accessible
  descriptions. Pattern tools supply readable task-specific body copy rather than only browser title bubbles.
- Pattern keeps its independent shared `LayerStackPanel`. Shared visible/hidden counters, search and filter
  presentation now serve both Texture Painting and Pattern; document operations remain workspace-owned.
  Pattern adds a locked filter and a real “+” for six shapes and three editable presets. Adding preserves the
  existing document, selects the new layer, clears filters that would hide it and respects the 64-layer limit.
  Filtering retains original document indices for selection, locks, visibility and drag reordering.
- Pattern motif/drawing fill and stroke, plus ground, accept Solid, Linear gradient, Radial gradient and embedded
  raster Image sources. Gradients expose editable stops/positions and linear angle. Raster imports decode and
  embed original PNG/JPEG/WebP files without resizing or flattening, cap source files at 4 MB / 16 megapixels,
  and validate a six-million-character embedded limit; invalid imports leave the previous source intact.
  Pattern JSON imports accept up to 12 MB; browser draft quota failures still require an explicit document save.
  Solid colours remain legacy-compatible fallbacks. Raster/SVG motif imports retain their original colours.
- Sources are validated authoring-document fields, retained through history, JSON and local drafts. SVG canvas,
  repeated previews and colour export use real SVG paint servers; numeric finish/surface maps do not receive
  RGB gradient/image tints. Flat stroked paths use user-space paint coordinates to avoid zero-height SVG bounds.
  Inspector cards separate transforms, fill, stroke/geometry and material coefficients; drawing defaults are
  grouped independently. Runtime raster generation consumes the same exported SVG without replacing source data.
- Final validation: production Vite build passed (existing large-chunk warning remains). **10 focused tests
  passed**: paint validation/round-trip/security, actual raster colour/stroke/flat-line/ground pixels, additive
  layer presets and stable filtering, locks, bulk drawing sources, undo/redo, JSON/draft recovery, original
  embedded image bytes, active-theme/keyboard/edge tooltips, and Texture layer filter/reorder/menu regression.
  Escape dismisses a tooltip without closing Pattern; open menus retain their own Escape/focus-return handling.
- An extended graphics run passed 21 tests, including Pattern transforms/live material preview and dock layouts
  at 1440/1024/780 px plus phone, before its six-minute global budget expired. Five cases were unfinished/not
  run; this is **not** a complete graphics-suite pass. The larger standalone shader-export run was stopped;
  no Windows/hardware performance result or full standalone-export certification is claimed.
- Actual HTML captures were refreshed from the production review server and visually inspected:
  `PatternColourSources.png`, `PatternLayerAdditions.png`, `PatternImageSource.png`. All nine add choices were
  hit-tested unobscured; browser capture errors: zero. Evidence code: `CapturePatternColourTooltips.mjs`.
  Results: `PatternHtmlControlChecks.json`, `PatternHtmlBrowserChecks.json`, `SharedLayerFilterChecks.json`
  and `PatternHtmlCaptureChecks.json` under `Exhibits/TextureStudio/Results/`. The saved evidence pattern remains
  editable. Live review opens Pattern on port 5174. All 40 preserved unrelated paths remain byte-identical.
  No C++ port, commit, push, Actions or executable publication occurred in this iteration.

### Deferred — editable authoring → runtime texture baking / material performance

- **Deferred by user request.** Keep full procedural parameters, source images, gradients, layer order and
  history editable during authoring. Do not flatten or destroy the authoring document to speed up the preview.
- Plan a separate derived runtime bake/cache with invalidation on relevant source/coefficient revisions,
  reproducible channel outputs, resolution/colour-space declarations and a reversible link to the authoring
  material. Bake suitable view-independent colour/masks/normal/height/roughness/flake distributions; retain
  view-dependent lighting, Fresnel and specular response in runtime shading. Filter flake detail across LODs
  rather than baking one camera's sparkles or reducing the user's editable parameters.
- Measure Windows CPU, GPU and presentation timings before selecting lighting-cache/flake-LOD work. Current
  shader counts describe work, **not bytes per pixel**: 32 lighting proposals per stratum (96 for paint + coat),
  and 27 searched cells × 3 flake layers (81 neighbour searches). Earlier CPU allocation/logging reductions are
  separate; no measured shader/FPS improvement or completed runtime optimisation is claimed here.

### HTML review — visible Booleans and additive Stamp vector artwork (2026-10-18)

- Pattern now exposes labelled Union, Difference, Intersection and Exclude buttons instead of hiding them
  behind the collapsed Path operations menu. Eligibility excludes locked, hidden, open and unfilled selections.
  Difference subtracts upper shapes from the bottom shape and retains the bottom appearance.
- Stamp retains its original Composition editor and adds a Vector canvas mode: finite transparent 512 × 512
  artwork, ruler markings, zoom/pan, grid/snap controls, shared shape/pen/curve/point tools, multi-selection,
  alignment, vector grouping, duplication and Boolean geometry. Six labelled primitive insertion buttons
  supplement the shared toolbar. Existing rich text, SVG and image components retain their original editors.
- Shared fill/stroke cards support Solid, Linear gradient, Radial gradient and original embedded Image sources.
  Selected unlocked vectors receive bulk paint changes. Native rich font runs, source images/SVG, transforms,
  PBR channel targets/coefficient settings, project/preset persistence, SVG/PNG export and Use in Texture remain.
  The existing `alloy.stamp.v1` schema gains additive vector components; 24-component / 600 KB limits remain.
- Fixed a real shared Boolean precision defect: serializing input Béziers to rounded SVG and truncated ellipse
  constants could invert tangent rectangle-minus-ellipse results. Paper now receives original double-precision
  segments; ellipse/rounded-rectangle constants use the full analytic expression. Regression checks assert
  filled area, corner/centre membership, holes, transformed geometry, serialization and actual exported pixels.
- Final focused validation: **43/43 passed**, including geometry/model regressions, actual Stamp shape/pen drawing,
  shared gradients, project export, native artwork preservation, undo/redo, editable presets and Use in Texture
  handoff with vector artwork, raster PNG and original material-channel settings retained.
  Results: `Exhibits/TextureStudio/Results/StampVectorChecks.json`; reproduce with
  `Exhibits/TextureStudio/Code/StampVectorValidation.mjs` using the configured local Chromium runtime.
- Original editing regression: **4/4 passed**, covering desktop/phone/cached workspace return, safe rich-text
  edits, modal transforms/constraints/cancellation/locking, captured drags and rotated corner resize.
  Earlier extended checks also passed eight shared Pattern SVG/tool tests and two original Stamp rich-text,
  SVG/image, preset/project and SVG/PNG export cases. Logs are retained under `Exhibits/TextureStudio/Results/`.
- **Not a full 3D painting-suite certification:** the extended run failed one existing decal placement case
  because the scene canvas had no bounding box after adding a Decal layer. One subsequent 3D case was
  interrupted and 16 cases did not run. Cause remains unconfirmed; this must be investigated before claiming
  end-to-end surface placement/transform/mask coverage. The authoring-to-Texture handoff itself passed.
- Production Vite build passed in 18.83 seconds, with the existing large-chunk warning. Production review on
  port 5175 was exercised without browser errors. Actual captures: `StampVectorSelection.png`,
  `StampVectorBoolean.png`, `StampOriginalComposition.png` in `Exhibits/TextureStudio/Images/`;
  reproducible capture code and results sit beside the existing evidence. The Boolean image shows retained
  rectangle corners and a transparent circular hole, not the incorrect pre-fix circle.
- All 40 preserved unrelated paths remain byte-identical. HTML only: no new native implementation, commit,
  push, Actions run or executable publication. Runtime baking/performance work remains deferred.

### HTML review — shared Stamp stack and canvas background preferences (2026-10-18)

- Replaced the permanently exposed component/shape button lists with a heading-level **+** menu. All ten
  choices remain available: Text, SVG, Import SVG, Image and six editable vector primitives. The reusable
  `LayerAdditionMenu` supplies keyboard navigation, Escape/focus return, outside dismissal and viewport-bounded
  positioning. Native text/image/SVG controls and component order/duplicate/delete operations remain intact.
- Stamp now uses the shared `LayerStackPanel` and `LayerStackFilters`: visible/hidden/locked count cards,
  search by component name/type, toggleable filters, matching/total count and a clearable empty-results state.
  Filtering never changes document identities or order. Successful additions clear filters so the new component
  is visible. Count-card labels/numbers were fitted to Stamp's narrower panel and checked for overlap.
- Vector canvas adds **Transparent**, **UI background** and **Fill colour** modes with an editable colour swatch.
  These are explicitly view-only preferences, retained across canvas-mode changes and browser reloads in a
  separate bounded local preference. Authoring documents, channel targets, undo history and exported sticker
  transparency do not change. Transparent mode keeps the checkerboard; UI mode uses the active panel colour.
- Final validation: **45 focused geometry/model/browser checks passed**, **six original Stamp editing tests
  passed**, and **two final stack/background/layout checks passed** after the spacing refinement. Original
  coverage includes mixed rich fonts, SVG/image imports, project/preset and SVG/PNG exports, desktop/phone
  layouts, typing shortcuts, transforms and resizing. The two final cases repeat tests included in the 45;
  they are not additional unique cases. No new 3D surface-placement certification is claimed.
- Production build passed in 12.18 seconds (existing chunk-size warning only). Production preview on port 5175
  was refreshed and exercised with zero browser errors. Actual evidence: `StampStackCanvasBackground.png`,
  `StampStackUiBackground.png`, `StampComponentAdditions.png` plus refreshed Boolean/composition images in
  `Exhibits/TextureStudio/Images/`. Capture code: `Code/CaptureStampVectorReview.mjs`; reproducible checks:
  `Code/StampVectorValidation.mjs`. Results: `StampVectorChecks.json`, `StampStackAppearanceChecks.json`,
  `StampStackOriginalEditingChecks.json`, `StampStackBuildChecks.txt` and `StampVectorCaptureChecks.json`.
- All 40 preserved unrelated paths remain byte-identical. HTML-only revision; no native port, commit, push,
  GitHub Actions or executable publication.

### Boolean icons and masks — HTML verified, native integration still open (2026-10-18)

- Shared HTML Boolean actions are icon-only and retain accessible names/tooltips in Pattern and Stamp.
- Shared local masks retain source descriptors through document edits/history/export; exported alpha/luminance,
  inversion, embedded images and zero opacity have actual pixel coverage. Eighteen focused/regression tests pass.
- Native reusable GPU evaluation, source-only TOML/history and shared Boolean/mask controls now compile and execute
  in local validation. GPU coverage has 35 readback assertions on SwiftShader, not hardware timing or Windows launch.
- CMake owns the new compute shader and validation target. Explicit SPIR-V entry validation avoids driver entry-name
  traps; Slang's Vulkan entry is `main`, independently of the source function's name.
- Delayed material navigation now has destination/displayed 120 ms exponential convergence, Blender gestures,
  full-panel preview drawing and aspect correction. The existing 512-pixel attachment has not become adaptive.
- Next integration must host both complete native workspaces, retain all existing Stamp/PBR/vector/paint tools,
  upload image masks to native GPU residence, and compare against the approved HTML. The native shared controls
  currently remain unhosted; do not advertise this continuation as a 1:1 native port or an updated Windows binary.

### HTML authoring-first correction (2026-10-18)

This supersedes the next-native-integration ordering above. **Keep native Pattern/Stamp work paused until the
cleaned HTML is reviewed.** Shared native controls are not a completed workspace port.

- Keep frequent tools visible; move additional drawing tools, canvas options, arrangement and document actions
  into compact disclosures. Show Boolean icons only for multiple selections. Preserve original functionality.
- Match Pattern layer typography, icons, metadata and selection treatment to Stamp's component rows. Keep the
  pattern library available through its disclosure so the layer panel can use the full side column.
- Masks are selectable entries directly beneath their owning artwork, not independent document layers.
  Selecting the artwork or Done returns to normal editing. Keep unrelated inspector fields out of mask editing.
- Retain embedded images and parametric sources, then add black/white/grey painting, erasing, opacity/softness,
  filled or outlined shapes, polygonal paths and gradients. Each completed gesture is one document-history edit.
- Preserve existing artwork, text, PBR targets, component transforms, projects/presets and transparent exports.
  Painting remains bounded to 256 retained operations, 2,048 points per operation and 16,384 points per mask;
  existing document-size limits still apply. Do not infer native paint-mask support from the HTML addition.
- Acceptance requires actual exported pixels, fractional image coverage beneath paint, reveal-all stroke
  preservation, flat-path coverage, cancellation, locks, undo/redo, save/reopen and embedded-width screenshots.
- Evidence and reproduction entry points belong in `Exhibits/TextureStudio`; prototype code and its tests remain
  in `Experimental/TextureStudio`. Record actual validation outcomes in the single tracker, not this scope plan.

### Selection-aware Inspector correction (2026-10-19)

- Supersede arrangement/Boolean placement in the canvas toolbar: Inspector owns selection operations.
- One selection: align to canvas and edit individual properties, including original Stamp composition artwork.
- Two selections: show eligible Boolean icons, explicit mask directions and common properties. More selections
  retain bulk editing; distribution requires three. Locked artwork must not be modified through shared paint.
- Keep original native text, SVG/image sources and PBR targets. Present unsupported bulk properties honestly.
- Use accessible themed switches, not visible checkboxes. Preserve keyboard and disabled behaviour.
- Selection-to-mask currently uses an alpha snapshot with a hidden retained source and atomic undo. A live-linked
  vector mask is separate future work; do not describe this snapshot as editable source contours.
- Native implementation remains paused until the HTML review is accepted.

### Editable arrays and caption uniqueness (2026-10-19)

- Shared Pattern/Stamp Inspector array controls replace the old Pattern-only grid controls; radial-copy tools remain.
- Layouts: linear, grid, circular/arc, radial rings. Scale: uniform, linear, circular wave, radial distance, seeded noise.
- Noise is repeatable per-copy variation, not a coherent Perlin field. Start/end scale remains within 1–400 percent.
- Generate replaces selected sources with individually editable copies in one undoable edit; no live modifier schema.
- Retain group offsets/masks, original native Stamp SVG/text and PBR channel targets; reject protected sources,
  entry-limit overflow and invalid geometry. Existing document budgets remain Pattern 64 and Stamp 24.
- Normalize duplicate outliner captions case-insensitively without changing IDs/order/geometry or unique custom names.
  Owned mask captions include their artwork caption. Remove the superseded, unreachable Pattern-only array action.
- Evidence and reproduction: `Exhibits/TextureStudio/Results/ArtworkArraysChecks.json` and
  `Exhibits/TextureStudio/Code/ArtworkArrayValidation.mjs`. Native C++ remains paused for HTML review.

### Pattern channels, nested containers and linked group editing (2026-10-19)

- HTML first: expose Colour/Height/Normal/Roughness/Metalness/Opacity and an additive supported-channel selector;
  ambient occlusion is the first additional channel. Do not present unimplemented arbitrary shader outputs as supported.
- Multi-edit unlocked selections, show mixed amounts and use themed switches. Support embedded normal maps, repeat-aware
  height-derived normals, actual raster inspection/PNG export and live material shading on fabric or other surfaces.
- Keep independent per-channel coverage, original masks and existing material channels. Height remains shading relief,
  not silhouette displacement. Normal-map convention is OpenGL +Y; scalar/normal outputs are not colour-managed albedo.
- Provide real nested group/folder membership with cycle/depth guards and visible navigation. Keep folders organisational.
  Existing flat groups are adopted into the container list. Existing array/duplicate operations retain independent groups.
- Separate default-off group-wide appearance/channel edits from linked corresponding-feature edits. Preserve unrelated
  channel settings and locked artwork. Linked dimensions change proportionally; placement/rotation remain local.
- Store searchable thumbnail-bearing group definitions inside the pattern document. Insert independent or linked copies,
  detach selected links, and remove unused unlinked definitions. Existing layer/document budgets remain enforced.
- Snapshot definitions update only changed linked properties, not incidental local overrides during navigation/toggle edits.
  Group additions/removals and container restructuring are not yet live-template changes across stored placements.
- Preserve save/reopen and one-edit undo. Reuse raster sources for name/group/link metadata changes.
- Evidence: `Exhibits/TextureStudio/Results/PatternChannelsGroupChecks.json`; reproduction:
  `Exhibits/TextureStudio/Code/PatternChannelsGroupValidation.mjs`. Native C++ remains paused for HTML review.

### Native authoring resumption — shared Engine increment (2026-10-19)

The user's latest instruction **supersedes the native pauses above**: retest the approved HTML, then resume
Pattern/Stamp ports and deferred native fixes. The fresh selected HTML regression passed 57/57 before native edits.

- `FrontierVectorAuthoring` is an Engine-only library, not part of an application-specific panel library. Both native
  workspaces and future Texture Painting reuse its source operations; no Engine dependency on ImGui or editor UI.
- `VectorAuthoringArrayPlacement` exposes linear/grid/circular/radial placements and all five scale profiles independently
  of any vector document, renderer or UI. Native retained-vector adaptation clones expression references and
  normalized cubic controls, transforms owned masks and issues independent enclosure/layer tokens atomically.
- `VectorAuthoringProperties` exposes shared seven-channel contributions, optional changed-field patches and
  group/folder properties independently of vector rendering. These are **authoring contracts**, not completed
  native Height/Normal/PBR evaluation. Do not enable unsupported native rendering controls on that basis.
- Native vector TOML remains `frontier.vector_artwork`, version 1, with optional additive channel/enclosure fields.
  Existing version-1 documents remain readable. Reject unknown fields and wrong types rather than silently losing
  authored properties. Preserve existing layer/source/instruction/history budgets; GPU pixels are never serialized.
- Native source operations validate nested visibility/protection, cycles and 12-tier nesting; group-wide channel
  editing defaults off and touches only explicitly edited fields, skipping locked members. Arrays retain GPU
  Boolean expressions, masks and individually editable copies. Limits may reject complex arrays before 64 layers
  because the existing GPU instruction budget is 128; do not claim unrestricted HTML/native geometry parity.
- Repair native no-op history edits erasing redo, workspace-contract changes through history and pointer edits of
  locked artwork masks. Preserve unselected source coordinates bit-for-bit during array adaptation; an identity
  transform must not subtract/re-add a distant pivot. Retain negative reproductions and GPU readbacks.

#### Next native acceptance gates — not completed by this increment

| Work | Boundary |
| --- | --- |
| Hosted Pattern and Stamp panels | Preserve approved layout, docking, original Stamp/PBR/text/image/vector tools. |
| GPU authoring channels | Shared channel targets, height/normal generation, imported normals, inspection/export. |
| Paint/image masks | Owned selection, retained strokes and image upload into shared GPU residence. |
| Stored groups and corresponding-feature links | Definitions, thumbnails, insertion/detach and proportional changed-key edits. |
| Caption parity | Current native folding is ASCII; full Unicode case folding remains open. |
| Vector placement parity | Current native pivot is derived from retained primitive bounds, not HTML text/image bounds. |
| Material native backlog | Hardware profiling, lighting/flake cost, detail parity and actual Windows input/render review. |
| Delivery | Full Windows application build/run, Defender scan and publication need their own acceptance; no Actions run. |

Verification and exact commands: `Exhibits/TextureStudio/Results/NativeArtworkEditingChecks.json` and
`Exhibits/TextureStudio/Code/VerifyVectorAuthoringEditing.py`. Full native workspace completion must not be inferred from
headless source checks, CPU ImGui captures, a Windows cross-link or software-Vulkan image readbacks.

### Pattern-first 1:1 native UI port (2026-10-19)

The user explicitly ordered **Pattern UI first, then Stamp**. Approved HTML is the interaction/layout target;
this is not permission to replace it with a different native arrangement. Keep Stamp UI unstarted until Pattern's
parity gate is satisfied. Shared mechanisms may be prepared for both and for future Texture Painting.

- `UI/TextureStudio/PatternWorkspacePanel` hosts the native Pattern presentation. It uses shared lifecycle hooks,
  the inherited patched native dockspace and shared authoring controls, not another docking partition algorithm.
- Measure the live HTML at a fixed viewport before comparison. The first reference is 1440 × 900, with a 43 px
  header, 278 px left column, 820 px centre, 338 px inspector and 2 px separators; collapsed library occupies 32 px.
- Native default stacked panels and independent workspace identities must preserve existing default editor layouts.
  Inactive workspaces keep their dockspaces alive; switching must retain document, panel and selection ownership.
- Host Pattern in TextureWorkspace alongside Material, preserving both painting reference apps. Bind native source
  Save/Open to the existing TOML codec and atomic text replacement. Native source is not the HTML JSON format.
- The native tile and repeat views sample actual Slang/Vulkan coverage. GPU output slots are reused only after all
  tracked main-viewport submissions retire. Secondary-window GPU sampling remains gated until its serials are tracked.
- Reuse rounded shared fields/dropdowns/colour pickers, ControlKit switches/sliders, editable arrays and selected-source
  operations. One canvas gesture is one source-history edit; cancellation and protected artwork must remain safe.

#### Pattern parity gate — still open

- Complete the original tool inventory: pen/points/curves/text/SVG/image sources, drawing defaults, presets, hit testing,
  clipboard/arrangement operations and full mixed-selection behaviour. Marquee and interactive resize/rotation
  are implemented in the checkpoint below; exact coverage hit testing remains open.
- Finish exact typography, icons, menu structure, row metadata, spacing, responsive/detached behaviour and disclosures.
- Finish groups/folders navigation and full membership controls, stored definitions and linked-instance editing.
- Finish painted/image masks, independent stroke sources, GPU surface channels, normal maps, material view and exports.
- Unsupported commands stay visibly unavailable with an explanation, never falsely working buttons or a CPU pixel fallback.
- Compare actual native captures and interactions with the approved HTML; a few matching panel boundaries do not certify
  full 1:1 parity. Windows object compilation is not Windows window execution or a complete application handoff.

Only after that Pattern gate is verified should the same shared UI mechanisms be used to port Stamp's preserved
composition/vector/PBR workflows. Reproduction and retained evidence belong under `Exhibits/TextureStudio`.


#### Selection/transform checkpoint (2026-10-19)

- Shared `VectorAuthoringSelectionEditing` now supplies region selection, rotated selection extents and local-axis
  resize/rotation patches. Native Pattern exposes eight resize grips and a rotation grip, with Shift/Alt modifiers.
- Mixed rotated sources and differently oriented owned masks constrain scaling to uniform when required to avoid
  shear. Identity edits preserve exact descriptors, unselected aliases remain independent and masks follow owners.
- Actual ImGui pointer tests cover marquee direction/addition/protection, local-axis resizing, shared-pivot rotation,
  zoom, off-board continuation, one-edit undo/redo, Escape/button-up cancellation and focus-loss cancellation.
- Strict plus ASan/UBSan/leak verification: 1,360 new geometry assertions; 2,269 existing source assertions;
  42 existing control assertions; 81 Pattern input/dispatch checks plus 47 gesture assertions, including actual
  transformed-pixel checks. Source/docking/residency counts are 9/4/8; software Vulkan executed 36 dispatches.
- Twenty current Windows translation units compiled, including the application host. No full application link,
  Windows execution, hardware performance measurement, Actions or publication.
- Evidence: `Exhibits/TextureStudio/NativePatternWorkspace.md` and its linked current reports/captures.
  Click selection and marquee still use bounds; group-aware selection expansion and original tools remain open.
  Keep Stamp UI unstarted until the full Pattern gate is verified.


#### GPU contour selection checkpoint (2026-10-19)

- Native clicks now query shared Slang coverage at continuous document coordinates, not source bounds. Display
  and point selection share primitive/cubic/Boolean/mask evaluation. Highest visible unlocked source wins;
  holes, stroke-only interiors, masks, opacity and enclosing visibility/protection participate.
- Engine `VectorSelectionEvaluation` records one-texel compute and four-byte token readback on the caller's
  submission. It consumes only retired results, never waits on a queue and never falls back to CPU bounds.
  CPU retains only source descriptors, selection metadata, pending input and authenticated returned tokens.
- Native pending gestures retain source snapshots and release coordinates. Escape/focus loss/document replacement
  prevent late selection changes; a fast drag released before the GPU reply still finalizes one source-history edit.
- Two vector shader archives are installed and checked through `VectorCompilation.json`; packaging rejects
  missing, corrupt, stale or duplicate sets without weakening signature/scan authorization.
- Current normal and ASan/UBSan/leak native runs: 158 picking assertions, 47 gesture assertions, 85 UI/input/
  coverage-dispatch assertions, 9 source / 4 docking / 8 GPU-residency assertions. Coverage/picking dispatches: 38/61.
  Existing 1,360 selection and 2,269 authoring assertions passed strict plus sanitizers; 42 native controls and
  54 GPU assertions / 13 dispatches passed. Twenty-one Windows translation units compiled, not linked/executed.
- Six vector archive validation cases and 4/72 distribution refusal/policy cases passed. No package, Actions,
  Defender scan, hardware timing, commit, push or publication. Current report/captures:
  `Exhibits/TextureStudio/NativePatternWorkspace.md`.
- This supersedes earlier bounds-only click-selection notes. Marquee remains HTML-style bounds intersection.
  Selection follows the shader's antialias threshold and cubic-distance approximation, not exact analytic
  intersections. Original pen/point/text/SVG/image authoring, complete groups/clipboard/channels/export,
  visual parity and Windows application acceptance remain open. Stamp UI remains unstarted.


#### Retained cubic geometry foundation (2026-10-19)

- Added reusable `CubicPathEditing` world-coordinate anchors, tangent operations, de Casteljau subdivision,
  anchor removal and primitive conversion. Ellipses use an approximation, not exact analytic conversion.
- Atomic source replacement preserves aliases and owned masks; unreachable cubic segment ranges are reclaimed.
  Added production CMake integration and focused validation. Native Point/Pen controls remain unavailable.
- Current focused verification: 25,449 strict and sanitized cubic assertions; 1,360 strict and sanitized
  selection regression checks. New Windows source compiled to an object only, not linked or executed.
- Evidence/commands/provenance: `Exhibits/TextureStudio/CubicPathEditing.md` and linked JSON reports.
  Earlier native GPU reports remain historical; their broad hashes do not verify this updated tree.
- Next: native pen/point pointer interactions, guides, one-operation history and actual GPU/input captures.
  No new UI/render evidence or full Pattern parity claimed. Stamp remains deferred; no publication.


#### Native retained Point interaction checkpoint (2026-10-19)

- Enabled native Point (A), with selected-path anchor/control grips; dragging previews shared retained cubic
  geometry through existing Vulkan coverage. Mirrored/Alt-independent tangents, corner/smooth controls,
  point nudges, minimum-safe anchor deletion and explicit primitive conversion are implemented.
- One history edit per completed drag, including press/move/release in adjacent frames; Escape and focus loss
  cancel. Undo/redo restores exact documents and clears stale edit diagnostics. Ellipse conversion is approximate.
- Normal and ASan/UBSan/leak runs passed: 26 point, 158 picking, 58 gesture and 116 UI/input/GPU checks;
  9 source, 4 docking and 8 GPU-residency checks. Display/selection dispatches: 59/63. Windows compilation:
  22 translation units, including host and shared cubic source; no full Windows link or execution.
- Actual capture: `Exhibits/TextureStudio/Images/NativePatternPointEditing.png`; current native provenance/report:
  `Exhibits/TextureStudio/NativePatternWorkspace.md`. Focused shared regression reports remain earlier runs.
- Pen placement remains unavailable and is next. Automatic curves, continuation/joining, multiple-point editing,
  subdivision controls and segment bending remain open. Full Pattern parity is not claimed; Stamp stays deferred.
- No Actions, scan, package, commit, push or release.


#### Native Pen and interactive pixel-stream checkpoint (2026-10-19)

- Enabled native Pen (P): place anchors, drag mirrored/Alt-independent tangents, first-anchor closure, Enter
  finish open, Backspace removal, Escape/focus cancellation and one finalized source-history operation.
  Pending paths preview through GPU coverage, not CPU raster authoring. Save/tool switches finish valid paths
  or refuse invalid ones. Ctrl+Z cancels the draft without undoing completed source history.
- Current normal and ASan/UBSan/leak runs: 21 Pen, 26 Point, 158 picking, 59 gesture and 141 native UI/input/GPU
  checks, plus 9 source / 4 docking / 8 GPU-residency checks. Display/selection dispatches: 81/63. Twenty-two
  Windows objects compiled; no complete Windows application link/execution.
- Actual C++ ImGui pixel stream is running on port 8776. Browser only relays input and shows native captures;
  it is not the HTML Pattern prototype, an OS window or a durable editing/file-saving session. CPU captures
  native ImGui triangles; authored canvas and repeats use real software Vulkan. Shared ephemeral validation only.
- HTTP stream checks: 14 passed, including pen-driven native history changes and bounded route/origin refusals.
  Actual Chromium pointer/key transport also passed. Reports/images/commands:
  `Exhibits/TextureStudio/NativePatternWorkspace.md`. Native, stream and browser source fingerprints refreshed.
- Automatic curves, continued/joined paths, multiple-point editing, segment bending, complete placement modifiers
  and broader Pattern gates remain open. Stamp stays deferred. No Actions, scan, package, commit/push/release.


#### Native refinement, subtle contours and mouse GIF (2026-10-19)

- Added native following-segment subdivision via shared de Casteljau geometry, including closing segments;
  nonexistent open-end segments and the anchor budget disable the action. One history edit; exact source undo.
- Shift constrains pen placement and Pen/Point tangents to 45-degree directions while preserving direction
  length. Constrained clicks remain corner anchors, rather than accidentally acquiring release-point tangents.
- Native shared cards now use 8 px corners and rectangular text actions up to 6 px, UI-scaled. Deliberate pills,
  disclosure/counter pills and glyph discs remain rounded. Native layer cards/repeat surface are also subtler.
  HTML applications are preserved, not restyled as a substitute for native changes.
- Normal and ASan/UBSan/leak checks passed: 12 refinement, 6 triangle/style contour checks across three scales,
  21 Pen, 26 Point, 158 picking, 64 gesture and 162 UI/input/GPU checks; 9 source / 4 docking / 8 residency.
  Display/selection dispatches: 94/63. Shared cubic assertions: 26,170 strict and sanitized. Twenty-two Windows
  translation units and the focused cubic object compiled; no Windows application linking/execution.
- Real C++ pointer/cursor recording: 202 raw native frames, GIF encoded to 70 changing/duration-merged frames.
  `Exhibits/TextureStudio/Images/NativePatternRefinement.gif` is primary review evidence. Python only encodes
  palette/compression, not fake mouse or authored pixels. Raw frames remain ignored; GIF is approximately 366 KB.
- Optional native stream refreshed on port 8776; HTTP 14-check and real Chromium input regressions passed again.
  Current source/header/GIF provenance and limitations: `Exhibits/TextureStudio/NativePatternWorkspace.md`.
- Remaining: automatic curves, continuation/joining, multiple-point editing, direct segment splitting/bending,
  remaining placement modifiers and broader Pattern parity. Stamp stays deferred. No Actions, scan, package,
  commit, push or release.


#### Retained path continuation checkpoint (2026-10-19)

- Native Pen endpoint grips and Point Inspector actions resume open paths from either endpoint. Preview
  replaces the retained source, not a duplicate layer. Source token/name/paint/stroke and owned descriptors stay
  attached. Start continuation reverses a working copy and restores original orientation before replacement.
- Existing used controls are preserved; dragging the resumed endpoint only edits its newly usable outgoing
  control. No-op finish, Escape/focus cancellation and Backspace at the original-anchor floor preserve source.
  Extension is one history edit; undo/redo restores exact documents. Two-anchor closure is visibly refused.
- Normal and ASan/UBSan/leak runs: 40 continuation, 12 refinement, 6 contour, 21 Pen, 26 Point, 158 picking,
  78 gesture and 207 native UI/input/GPU checks; 9 source / 4 docking / 8 residency. Display/selection: 121/63.
  Shared cubic/reversal checks: 26,491 strict and sanitized. Windows: 22 translation units plus focused cubic
  object compiled, not a full application link or execution.
- C++ mouse GIF now includes end/start continuation: 408 raw captures, encoded to 154 changing/duration-merged
  frames, approximately 792 KB. Source/header/executable/GIF provenance refreshed. HTTP 14-case and actual
  Chromium pointer/key regressions passed using an isolated current-build native instance.
- Existing port-8776 stream intentionally remains on the earlier refinement build to preserve its temporary
  session; it is not current continuation evidence. Isolated verification instance on 8777 was stopped.
  Current report, commands, capture and GIF: `Exhibits/TextureStudio/NativePatternWorkspace.md`.
- Joining separate paths, automatic curves, multiple-point edits, direct segment splitting/bending and broader
  Pattern parity remain open. Stamp deferred. No Actions, scan, package, commit, push or release.


#### Retained path joining checkpoint (2026-10-19)

- Added shared atomic `IntegrateCubicPaths` and native Inspector actions for all four start/end pairings.
  Keeps the lower selected layer's identity/appearance; consumes the other; one undo restores both/selection.
  Existing cubic segments retain their geometry under the chosen orientation. Disjoint endpoints receive a
  straight bridge; coincident endpoints merge their meaningful controls. Valid closed results remain unfilled.
- Requires adjacent layers, matching opaque solid styles and matching group/channel descriptors, with no
  selected owned masks. Gradients/transparency, independent masks, mismatched metadata, protected/hidden or
  non-adjacent sources are refused, not flattened. Aliases and unselected owned masks are preserved.
- Normal and ASan/UBSan/leak runs: 52 joining, 40 continuation, 12 refinement, 6 contour, 21 Pen, 26 Point,
  158 picking, 78 gesture and 295 UI/input/GPU checks; 9 source / 4 docking / 8 residency. Dispatches: 149/63.
  Shared cubic assertions: 27,363 strict and sanitized. Windows: 22 translation units plus focused cubic object
  compiled; no complete application link/execution. Storage-budget refusal and original curve sampling tested.
- Current C++ mouse GIF: `Exhibits/TextureStudio/Images/NativePatternJoining.gif` (207 raw native captures,
  71 changing/duration-merged frames, approximately 433 KB), with source/executable/frame/GIF fingerprints in
  `Results/NativePatternJoiningMotionChecks.json`. Earlier refinement GIF/report are historical, not current
  source verification. Actual Vulkan bridge pixels and coincident joins were checked in native pointer tests.
- HTTP 14-case and actual Chromium transport regressions passed in an isolated current-build native instance,
  then stopped. Existing port-8776 refinement session was not reset or relabelled as current joining evidence.
- Remaining: direct segment splitting/bending, multiple-point editing, automatic curves, broader join semantics,
  remaining modifiers and full Pattern parity. Stamp deferred. No Actions, scan, package, commit/push/release.


#### Direct segment editing checkpoint (2026-10-19)

- Shared `ProjectCubicSegment` and `BendCubicSegment`; native Points mode click-to-split / drag-to-bend.
  Shape-preserving split at the pointer, fixed-endpoint bend, one history edit and Escape rollback.
  Six-screen-pixel hit tolerance; grip precedence; interior 2–98% parameter restriction. Projection is bounded
  numerical approximation, not exact polynomial root isolation. Bending does not smooth adjoining tangents.
- Native: 326 UI/input/GPU assertions including 13 segment-specific checks; coverage/selection dispatches 171/63.
  Shared geometry: 27,646 strict and sanitized assertions. Windows: 22 native translation units plus focused
  cubic object compile, not full link/runtime. Native sanitizer report is separate from normal verification.
- Real C++ cursor/input GIF `Exhibits/TextureStudio/Images/NativePatternSegmentEditing.gif`: 366 raw captures,
  68 duration-merged frames. Source/executable/frame/GIF provenance in `NativePatternSegmentEditingMotionChecks.json`.
  Earlier joining/refinement motion reports are historical, not current-source provenance.
- Isolated current-build HTTP 14-case and actual Chromium pointer/key transport regressions passed; stopped
  isolated port 8777 afterward. Existing older refinement session at 8776 remains untouched.
- Remaining: multipoint editing, automatic curves, advanced placement/segment constraints, broader joins,
  remaining Pattern layout/features and Windows application acceptance. Stamp deferred; full parity false.
- No Actions, package, scan, commit, push or release.


#### Automatic curves and multipoint checkpoint (2026-10-19)

- Shared atomic anchor translation/deletion and neighbour-derived automatic tangents; new Pen paths support
  default-off live Automatic Pen curves. Existing retained continuation stays manual, preserving its segments.
- Native Shift-click anchor toggle, select all, shared drag/nudge, corner/smooth/automatic selected tangents and
  batch Delete. Selected grips are brighter. Topology Undo/Redo invalidates stale point indices; drag Escape
  restores source. Minimum counts and invalid index/nonfinite inputs are refused atomically.
- Normal and native sanitizer runs: 367 UI/input/GPU checks, including 24 automatic/multipoint; 196 coverage /
  63 selection dispatches. Shared geometry: 27,708 strict and sanitized assertions. Windows: 22 native translation
  units and focused cubic object compilation, not full application linking/execution.
- Real C++ cursor/input/Vulkan GIF `Exhibits/TextureStudio/Images/NativePatternMultipoint.gif`: 388 raw captures,
  70 duration-merged frames. `NativePatternMultipointMotionChecks.json` fingerprints current source/media.
  Older GIFs/reports are historical checkpoints, not current-build verification.
- Automatic controls are ordinary retained cubic controls, not a persistent spline solver. No overshoot-free or
  curvature-continuous claim. Multipoint marquee and cross-path point selection remain open.
- User approved continuing the remaining Pattern work: advanced Pen placement/continuation/join modifiers;
  semantically preserving broader joins; segment constraints/smoothing; text/SVG/images; clipboard/presets/
  arrangement/mixed selection; complete groups/links; painted/image masks and independent strokes; GPU channels,
  inspection/3D/material/export; final responsive/native/detached-window parity. These are queued, not completed.
- Material layers/channel components remain a separate requested capability. User will supply the flake/fibre
  research next phase; document and compare that actual paper then, without guessing its identity/performance.
- Pattern first, Stamp deferred. Full parity false. No Actions, package, commit/push or release.


#### Glint research supplied — implementation held (2026-10-19)

- Reviewed the user-provided Glinty (2025), Loubet slope-space (2020), and Chermain real-time glint (2020)
  references. Findings, primary links, published timings, current source audit and deferred validation are in
  `Documentation/Plans/Ongoing/MaterialGlintResearch.md`.
- Requested future configuration: particle-microfacet default for new flake components, existing cellular
  evaluation retained, proposed 25 MB shared glint-resource cap. This is not yet an implemented setting.
- Important correction: Glinty uses an implicit point process, not a mandatory stored particle/sprite cloud;
  the original Chermain implementation uses a 384 KiB dictionary. Memory budget is not an FPS guarantee.
- Published Chermain RTX 2080/OpenGL 1080p examples range from 1.7 to 9.2 ms/frame. These are not Frontier
  timings. Full Glinty PDF retrieval failed; do not invent its detailed benchmark figures.
- Existing paint costs 27 cell visits per flake layer; light/view-dependent glint evaluation could add work
  inside our lighting sample loop. No faster-than-current or exact-appearance-equivalence claim is established.
- Material layers/channel components and fibre/sheen remain separately pending. User explicitly said not to
  start this implementation yet. No C++/Slang change, new particle allocation or runtime benchmark this turn.


#### Isolated glint tests and native Pen placement (2026-10-19)

- New authorization: test glint performance while continuing Pattern. Hair/fibres explicitly skipped.
  This supersedes the research-only benchmark restriction, not the production shader/default hold.
- Executed two software-Vulkan comparison runs: three shader adaptations plus a raster floor, two extents,
  two densities, two warmups and seven measured draws per case. Timestamp queries, finite/nonconstant
  readbacks and source/image fingerprints passed. Raw samples and limitations:
  `Exhibits/TextureStudio/GlintComparison.md` and `Results/GlintComparison{,Repeat}Checks.json` beneath it.
- Cellular has the lowest retained medians; Chermain follows, then the third-party Glinty adaptation.
  Chermain/cellular margins can be close. No desktop-GPU, equal-quality or full-engine speed verdict.
  Glinty source is an isolated GPL-3.0 third-party adaptation, not the authenticated authors' reference.
  Loubet transport was not executed/ranked. No production glint/default/preset or 25 MB setting was changed.
- Native Pattern: holding Space during Pen placement translates the newest anchor and its controls;
  releasing Space restores tangent shaping. Earlier anchors and retained resume endpoints are protected.
  One finalized history edit, Undo/Redo and cancelled continuation are tested; tooltips explain the gesture.
- Current validation: 383 strict and 383 ASan/UBSan native UI/input/GPU checks, including 10 placement checks;
  22 Windows translation units compile, not application linking or Windows execution. Live native transport:
  14 HTTP checks and actual browser pointer/key/history verification passed on port 8777; 8776 preserved.
- Actual native cursor/input/Vulkan motion: `Exhibits/TextureStudio/Images/NativePatternPenPlacement.gif`,
  167 raw captures merged into 42 GIF frames. Current fingerprints are in the placement motion report and
  current workspace/stream/browser reports; earlier motion checkpoints remain historical.
- Next Pattern increments remain advanced continuation/join modifiers and semantically preserving broader
  joins, followed by the queued 1:1 authoring work. Full parity remains false; Stamp stays deferred.
  No Actions, package, commit, push or release.


#### Native independent Pen continuation (2026-10-19)

- Next approved Pattern increment: Alt-click the final pending anchor to break only its outgoing tangent;
  Alt-drag shapes it independently while preserving the incoming segment. Releasing Alt mid-drag remains
  independent; placing another anchor returns to ordinary tangent behaviour. No duplicate anchor is added.
- Automatic Pen mode refuses this manual corner edit without changing source, with explanatory guidance.
  Finalization stays one history edit; Undo/Redo and cancelled resumed continuation preserve retained source.
- 416 native strict and 416 ASan/UBSan checks passed, including 17 focused continuation and 10 placement checks.
  All 22 Windows translation units compile; application linking and Windows execution were not performed.
  Final native HTTP/browser input checks passed on isolated temporary port 8778, then that stream was stopped.
  Updated native preview 8777 remains running; inherited 8776 remains untouched.
- Evidence/reproduction: `Exhibits/TextureStudio/PenContinuation.md`; actual native cursor/input/Vulkan GIF
  `Images/NativePatternPenContinuation.gif` beneath that exhibit: 370 raw captures, 100 encoded frames.
  Current workspace, sanitizer, continuation-motion and transport source fingerprints were checked.
  Older motion/placement reports are historical checkpoints, not current-source verification.
- Remaining continuation/join modifiers and semantically preserving broader joins remain open, followed by
  the existing Pattern 1:1 backlog. Full parity remains false; Stamp deferred. Glint production/defaults
  unchanged; hair skipped. No Actions, package, commit, push or release.


#### Native independent endpoint start (2026-10-19)

- Ctrl-click either selected retained open endpoint with Pen to begin a separate path there; ordinary
  endpoint clicks still resume the original. Releasing Ctrl after the starting click is supported.
- Original controls, layer identity and appearance stay unchanged; new source uses an independent token.
  Preview history, single finalization, Undo/Redo, Escape and ordinary resume are tested at both ends.
- 445 strict and 445 ASan/UBSan native checks passed, including 22 focused endpoint checks. All 22 Windows
  translation units compile; no application linking or Windows execution. Current HTTP/browser input
  checks passed; source fingerprints match. Updated native preview runs on 8778; 8776/8777 were preserved.
- Actual native capture and reproduction: `Exhibits/TextureStudio/IndependentEndpoint.md` and its linked
  `Images/NativePatternIndependentEndpoint.png`. Prior motion checkpoints are historical, not current builds.
- Broader joins remain queued; this adds separate-path authoring, not masked/gradient/translucent joins.
  Full Pattern parity remains false, Stamp deferred, hair skipped and glint defaults unchanged.
  No Actions, package, commit, push or release.


#### Shared native dropdown style (2026-10-19)

- Selection lists, action menus and context menus now use one rounded, internally padded style.
  Shared ControlKitTokens own 8 px popup / 6 px row rounding, 12 × 10 px padding and 28 px rows / 4 px gaps.
  Native ImGui and existing PixelSpace paths consume the same geometry; no per-panel native combo styling.
- Material, vector masks and Pattern use the shared surface; PixelSpace editor consumers inherit the tokens.
  Fixed action-menu toggle caption clipping. Colour pickers/modals and intentional pills remain distinct.
- 454 strict and 454 ASan/UBSan Pattern checks passed, including 33 dropdown checks; Material passed
  343 normal and 343 sanitizer checks. All 22 Windows translation units compile; no Windows execution.
  Shared-style audit, 14 HTTP checks and browser transport passed with current source fingerprints.
- Actual captures and reproduction: `Exhibits/TextureStudio/DropdownStyle.md`. New native preview 8779;
  earlier 8776–8778 previews preserved. Software-Vulkan/CPU UI evidence, not physical Windows/hardware GPU.
- Full Pattern parity remains open; broader joins next, Stamp deferred. Glint defaults/hair scope unchanged.
  No Actions, package, commit, push or release.


#### Native Pen endpoint joining (2026-10-19)

- While continuing a retained open path, click another open endpoint to join; Ctrl-click bypasses joining.
  Direct joining and joining after extension support all four endpoint pairings in one history edit.
  Shared IntegrateCubicPaths retains token/selection and merges coincident endpoints or adds a straight bridge.
- Protected/incompatible targets refuse atomically, keeping the pending draft. Hidden, closed and compound
  contours are excluded. Broader masked/gradient/translucent/non-adjacent semantics remain open, not flattened.
- 585 native UI/input/GPU checks and 102 focused endpoint-join checks passed in each strict and ASan/UBSan run;
  existing 52 Inspector-join checks remain covered. All 22 Windows translation units compile, not link/run.
- Actual C++ cursor/input/Vulkan GIF: 185 raw captures → 45 encoded frames. Fixed the capture fixture's dynamic
  font-texture sampling, which previously lost letters in late captures; production typography is unchanged.
- User presentation preference applied: GIFs embedded in the earlier Exhibits/SpaceFormats/index.html, along
  with two labelled historical GIFs. Five existing tabs and 43 capture choices remain intact and validated.
  Static HTML review on 8765; no pixel-stream update. Chromium decoded and hashed all three GIFs.
- Reproduction and evidence: Exhibits/TextureStudio/EndpointJoining.md. Full Pattern parity remains open;
  broader join semantics next, Stamp deferred. Glint defaults/hair scope unchanged. No Actions, package,
  commit, push or release.


#### Pattern completion work — clipboard and stack order (2026-10-19)

- User requested completion of Pattern Studio; Stamp stays paused. This checkpoint does not claim full parity.
- Native session-local Copy/Cut/Paste, paste in place, independent duplicate, Select All and all four stack
  commands are wired to document actions and keyboard shortcuts, including Command-key equivalents.
- Shared extraction/integration retain editable primitives/curves/Booleans, owned masks/modes, channels and
  nested enclosure ancestry. New layer/group identities and unlocked copies; originals/captions stay unchanged.
  Paste/duplicate offset matches the HTML reference's 18 units. No OS clipboard or persisted clipboard file.
- Budget/identity failures are atomic. Copy/no-op stacking add no history; edits retain exact source/selection
  Undo/Redo. Transfer reclaims unused cubic storage; repeated cut/paste is tested without curve-storage leakage.
- 606 native checks and 16 focused clipboard checks passed in both strict and ASan/UBSan runs. Shared selection,
  transfer and stack tests: 1,431 each normal/sanitized. All 22 Windows translation units compile, not link/run.
- Actual native cursor/input/Vulkan GIF: 222 raw captures → 26 encoded frames. Updated the earlier HTML evidence
  page/template with this GIF and three explicitly historical GIFs. Chromium decoded/hashed all four; five tabs,
  43 original native capture choices and original brief/plan downloads remain verified. No pixel-stream update.
- Current evidence/reproduction: Exhibits/TextureStudio/ClipboardEditing.md. Shared dropdown audit remains clean.
  No Actions, package, commit, push or release. Glint defaults and hair scope unchanged.

##### Remaining completion gates

- [ ] Advanced segment constraints/smoothing, cross-path point selection and broader appearance-preserving joins.
- [ ] Text, SVG, image and freehand authoring.
- [ ] Presets/defaults and complete group-aware arrangement/distribution and selection semantics.
- [ ] Complete groups, stored/linked definitions and their browser/editing workflows.
- [ ] Mask isolation, painted/image masks and independent strokes.
- [ ] GPU material channels, height/normal outputs, 3D/material application and export.
- [ ] Final responsive/icon/typography parity, detached GPU safety and linked Windows/native acceptance.
