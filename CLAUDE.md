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

- The reusable panel expansion control is named **PanelExpansionTab**. Use this terminology for new work.
  It serves the Control Centre and Content Browser; hosting-specific content must not duplicate its mechanism.
  The extraction/rename and read-only Content Browser were separately approved and implemented.
  The scoped editable projectspace codec is approved; unrelated/binary Space codecs still require plan approval.
Preserve historical source paths as truthful references.

- Use **Content Directory**, `ContentDirectory`, and `EngineContent/`.
  Do not introduce `AssetDirectory` or `UI/Assets/`.
- Shared fonts belong in `EngineContent/FontArchives/<font name>/`, with the original licence beside each collection.
- Shared SVG content belongs in `EngineContent/Icons/`. Resolve bundled content relative to the executable first;
  launching from another working directory must not hide fonts or pick an unrelated partial collection.
- Editor/Authoring C++ configuration and editable serialization use **TOML**, decoded/encoded with **toml++**.
  Preserve established schemas and extensions; inspect existing codecs before creating another format.
  Project configuration uses one editable `.projectspace` container with embedded appearance/input/notification
  profiles and engine configuration; do not create project preference TOML sidecars. The editor container is UTF-8
  TOML internally, discriminated by `format = "frontier.projectspace.editor"`, version 1; binary FSPC is refused.
  `EngineContent/Defaults/BlankProject/Blank.projectspace` seeds new projects. Personal overrides use
  `Personal.projectspace`; project-scoped edits must not change it or installed defaults.
  Legacy explicit Appearance/Input TOML codecs remain compatibility APIs, not the new Windows default save path.
  FontCatalogue.toml and per-font archive TOMLs retain their existing font-metadata schemas.
  JSON lock manifests and exhibit reports are automation interchange, not new C++ runtime serialization formats.
  External file formats such as OpenType, SVG, and shaders are not converted into TOML.
- Use one shared style for native list/action dropdowns: `Engine/Interface/DropdownControls.h` owns the
  ImGui selection/menu surface and rows; `ControlKitTokens` owns their rounded/padded geometry. The PixelSpace
  dropdown renderer consumes those same tokens. Do not add panel-specific combo/list styling. Preserve solid
  backgrounds, internal padding, rounded rows, input/focus behaviour and caption fit. Colour pickers and modal
  dialogs have separate layouts; they are not alternative dropdown styles.
- All production range controls use the shared `ControlKit::Slider` styling: 28 px rail and 26 px thumb.
  Do not add thin/native/alternate-colour slider variants. Tab-page footers round only their bottom corners.
- Approved encoding B (2026-10-17): compiled runtime packages use a **binary** envelope and typed runtime records;
  they may embed textures, SVGs, images, fonts and other supported payloads. Retain editable authoring descriptors
  and original payload/source files separately. This policy approval does not authorise codec implementation or
  migration.
- Native retained vector authoring uses toml++ with `format = "frontier.vector_artwork"`, version 1.
  It retains source instructions, cubic controls, layer identity and masks; it is not a runtime pixel bake or
  an implemented importer for the HTML Pattern/Stamp JSON formats. History stores CPU descriptors only.
- Shared vector coverage uses Slang compute and caller-retired Vulkan buffers/images. Confirm cached-image
  sampling serials as well as dirty dispatches, and retire references before destruction. SwiftShader readback
  is software-Vulkan verification, not hardware timing or Windows execution. Coverage Boolean output must not
  be described as reconstructed editable Bézier contours or a completed native Pattern/Stamp workspace port.
- Native Pattern click selection uses shared Slang coverage and asynchronous four-byte token readback. A pending
  result is not a miss: preserve the source snapshot/release point and consume results only after caller-reported
  GPU retirement. No queue waits or CPU bounds fallback. Marquee remains bounds-intersection selection.
- Native development-console policy is centralized in `Engine/Diagnostics/WorkspaceBuildConfiguration.h`
  and CMake `FRONTIER_DEVELOPMENT_CONSOLE` (default `ON`). Use the shared policy; do not add per-panel or
  projectspace runtime console switches. Windows allocation and diagnostic mirroring obey the same definition.
- **Vulkan is the primary rendering API. Slang (`.slang`) is the primary shader language.**
  DX11 is permitted for the isolated ImGui display exhibit; it is not a replacement engine-renderer architecture.
  Renderer implementation is authorized sequentially: 1 CAD, 2 MatCap, 3 preview PBR, 4 full materials/shaders.
  Use shared Engine Vulkan geometry/program/target ownership. CAD/MatCap offscreen cores are authorized; native
  presenter/Windows/validation-layer acceptance is still open, not implied by those cores. Preserve both painting
  apps/reference collections. Do not claim native viewport, Windows execution or document import from offscreen tests.
- **GLFW owns platform windows and input.** Use a no-OpenGL-context window for Vulkan or the DX11 ImGui exhibit.
- Third-party sources belong under the existing ignored `ExternalPackages/`, restored from pinned dependencies.
  Reuse the existing toml++, GLFW, ImGui, and ThorVG dependency setup; do not vendor duplicate copies elsewhere.
- Notifications use shared `NotificationSequence`/`NotificationCard` storage and geometry, not the expansion tab.
  Editor cards anchor top-right, reuse semantic theme colours/fonts/SVGs and retain bounded session history under
  Quick Controls as **Notification Stream**, split beside tiles on wide screens and stacked below on narrow screens.
  Telemetry & Notifications contains preferences only. Suppressed, expired and dismissed preference events remain in
  the stream; messages are not serialized into projectspace. Shared `ConfirmationDialogue` presents rounded and
  semantic-header variants. Preference Save/Apply uses a real Apply/Cancel modal; Cancel/Escape preserve drafts/files.
  Genuine notification action callbacks and independent severe-error UI remain pending.
- Both shared trapezoidal tabs are 200 × 18 logical pixels (half the former dimensions), including Content Browser.
  Invert Y-Axis is a standalone shared switch, without a surrounding field or duplicate description.
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

## Shared panel hosting

- Reuse patched native ImGui docking; do not add a second custom docking partition model.
- Engine/Interface/PanelDockspace hosts callbacks; UI/Editor/EditorWorkspace adapts original editor-owned content.
- Preserve one browser/stream instance and editing/browsing state across docked/detached/closed presentations.
- Detached OS windows are optional and disabled by default. Settings → Workspace stages a real Apply/Cancel save.
- Save hosting choices only in personal `.projectspace` workspace.hosting, format 1, as booleans.
- dockable_content_browser defaults false (draggable drawer); allow_detached_windows also defaults false.
- Workspace hosting changes must stage/confirm; changing browser presentation must retain its original content.
- Retain inherited ImGui patches A–F; correction G fixes the adjacent/centred add button without replacing docking.
- All trapezoidal tabs use the opaque darkest named theme surface, shared through the theme/control palette.
- Project settings must not enable machine-local windows or toggle unrelated personal appearance/input overrides.
- FrontierUI uses GLFW/Vulkan for editor UI and secondary windows; FrontierUIDX11 is the isolated comparison exhibit.
- VulkanPresentation is a UI display adapter, not a scene renderer. Load Vulkan through GLFW; no SDK loader dependency.
- Pinned Vulkan-Headers belongs in ExternalPackages; native runtime drivers remain system supplied.
- Frontier owns versioned, bounded TOML workspace.layout in personal `.projectspace`; keep IniFilename null.
- Archive stable panel keys, tab selection/order, split ratios, open states and floating geometry; restore via
  DockBuilder.
- Autosave only settled layouts; final flush precedes platform-window teardown. Merge the latest personal container.
- Preserve unrelated sections, drafts and override flags. Refuse invalid/unsupported layout writes until explicit Reset.
- Missing layouts default normally. Native rectangles retain optional monitor work-area/density metadata.
- Recover offscreen rectangles at restore and topology changes; defer minimized windows; avoid double live DPI scaling.
- Monitor/DPI geometry and synthetic callback checks do not establish physical Windows multi-monitor behaviour.
- Runtime disable destroys secondary platform windows only; preserve the main GLFW viewport for re-enabling.
- Read Exhibits/Docking/README.md for native evidence and the unavailable Windows/multi-monitor checks.


### Authored surface materials

- `Engine/Render/SurfaceMaterial.{h,cpp}` owns the bounded material contract, strict TOML codec and PNG/JPEG
  texture/mip compilation. Use `.material` for these authored TOML documents; JSON is HTTP transport only.
- Reuse pinned toml++ and stb_image from `Automation/UIDependencies.json`; do not introduce another TOML/image parser.
- Material texture paths are portable and relative to `ContentDirectory`, including symlink-containment checks.
- `ScatteringEvaluation.slang` is shared by preview and authored-material rasterization. Do not fork BRDF equations.
- Authored materials use a 1216-byte uniform and four fixed Texture2DArray descriptors. Preserve independent
  per-stratum textures/UVs/normals, rotated anisotropic GGX and exact dielectric Fresnel; radiance stays material-wide.
- See `Documentation/Rules/SurfaceMaterials.md` for channel spaces, units, supported categories and explicit limits.


### Texture Studio Material Library

- Plan: `Documentation/Plans/Ongoing/TextureStudio.md`; one progress tracker remains `Documentation/Tracker.md`.
- `.material` is TOML, not the reference app's `.texture.json`. No implicit migration.
- Reuse `Engine/ContentInterchange/MaterialLibrary` for discovery/edit/apply/save and
  `UI/TextureStudio/MaterialLibraryPanel` for the native UI. Do not create separate embedded material semantics.
- Native applications share `Engine/Interface/WindowsApplication`. Keep application-specific composition outside Engine.
- Renderer validation belongs in Exhibits, not the application Project Browser catalogue.
- Follow execution boundaries in `Exhibits/TextureStudio/README.md`: CPU ImGui capture, Windows objects,
  linked Windows executable and Windows/hardware runtime are distinct gates.

### Native evidence presentation preference

- User reviews native GIFs, not the interactive pixel stream. Do not refresh, start or maintain pixel-stream
  previews by default; only do so if explicitly requested. Keep native implementation and automated local tests.
- Present actual C++ capture GIFs in the existing HTML native-evidence review page
  (`Exhibits/SpaceFormats/index.html`, the earlier native UI evidence gallery), rather than building a new
  pixel-stream presentation. Keep earlier evidence available; use Frontier / TextureWorkspace naming.

### Native Pen joins

- Retained Pen continuation joins another open endpoint through shared `IntegrateCubicPaths` in one history edit.
  Ctrl bypasses target joining. Keep existing compatibility/stacking guards; refused joins preserve the draft.
  Hidden, closed and compound contours must not expose endpoint targets. Broader paint/mask joins remain pending.
- Evidence capture must honor ImGui dynamic font textures without rebuilding the font atlas after recording UVs.

### Pattern completion directive

- The user requested completing Pattern Studio, not starting Stamp or stopping at another Pen-only milestone.
  Continue the remaining approved feature groups; do not claim full parity while disabled/unported functions remain.
- Native selection Copy/Cut/Paste uses a session-local document fragment, matching the HTML editor rather than
  the OS clipboard. Copy retains source metadata; pasted groups/layers have fresh identities and editable locks.
  Default paste/duplicate offset is 18 design units; paste in place preserves coordinates. Do not rename originals.
- Reuse shared extraction/integration/stack operations; keep masks, curves, Boolean references and channel settings
  editable. Enforce storage/identity budgets atomically. Keep exact source/selection Undo/Redo and no-op history.
