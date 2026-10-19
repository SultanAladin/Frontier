# Project Tracker

Single progress and violation tracker. Detailed evidence belongs in Exhibits, not competing progress reports.

## EngineUpdate status

| ID  | Finding or task                        | Status      | Next action                            |
|-----|----------------------------------------|-------------|----------------------------------------|
| 001 | Checkout differs from source baseline  | Partial     | UI imported; rest deliberately absent. |
| 002 | Root agent rules used obsolete workflow| Implemented | Keep entry instructions consistent.    |
| 003 | Prototypes live in Documentation       | Open        | Inventory paths and launcher links.    |
| 004 | Inherited Internal tree remains        | Open        | Audit ownership before further ports.  |
| 005 | Old maps/backlog conflict              | Partial     | Historical pointers added; reconcile.  |
| 006 | Windows binary and GPU checks         | Partial     | Cross-built; Windows runtime pending.  |
| 007 | Four skills retained at old paths      | Intentional | Preserve until approved relocation.    |
| 008 | UI foundations and SVGs ported         | CPU checked | Windows integration still unverified.  |
| 009 | Six ImGui patches                     | Checked     | Applied, verified, reverted/reapplied. |
| 010 | Shared activation lifecycle           | CPU checked | Integrate into further scene classes.  |
| 011 | Shared Vulkan renderer                | Partial     | CAD/MatCap offscreen; native presenter pending.|
| 012 | Imported naming/formatting violations | Open        | Audit imported APIs before renaming.   |
| 013 | Source/artwork permissions            | Open        | Owner review before redistribution.   |
| 014 | Control Centre and quick panel        | CPU checked | Windows interaction validation next.  |
| 015 | Open fonts and catalogue              | Checked     | Four families, 30 faces, OFL retained. |
| 016 | Appearance persistence                | Checked     | Reload and invalid-save checks passed.|
| 017 | Input configuration and key capture   | CPU checked | Windows interaction validation next.  |
| 018 | Shared range style / footer contour   | CPU checked | One style; native footer pixel checks.|
| 019 | Shared PanelExpansionTab extraction   | CPU checked | Top/bottom gesture parity; Windows next.|
| 020 | Read-only native Content Browser      | CPU checked | Windows runtime next.                  |
| 021 | Top-right cards / Notification Stream| CPU checked | Responsive split in Quick Controls.    |
| 022 | Apply/Cancel confirmation dialogues   | CPU checked | Windows interaction validation next.   |
| 023 | Half-size tabs / plain Invert Y switch| CPU checked | Shared geometry and native pixel checks.|
| 024 | Shared docked/detached panel hosting  | Partial     | Vulkan/recovery implemented; Windows runtime pending.|

## Projectspace persistence and notification cards — implemented

- Projectspace/workspace profiles: scoped requirements in `Plans/Ongoing/SpaceFormats.md`.
- `WorkspaceAppearanceProfile` is the requested portable project appearance name.
- Legacy Appearance/Input TOML APIs are retained; modern preference defaults are embedded in projectspace.
- Six uploaded UI references inspected and pinned in `Exhibits/SpaceFormats/Results/ConfirmationReferences.json`.
- Approved: project appearance/input/notification defaults, optional personal overrides and explicit save scope.
- Default/template: EngineContent/Defaults/BlankProject/Blank.projectspace; codec: Engine/ContentInterchange.
- Blank projects snapshot validated defaults; missing themes/fonts fall back without overwriting saved choices.
- Single editable projectspace container implemented; no preference sidecars or implicit binary FSPC migration.
- Explicit scoped saves, personal overrides and notification persistence are covered by ProjectSpaceValidation.
- Binary runtime layouts and unrelated formats remain gated; no wholesale migration is authorised.
- Top-right semantic notification cards and bounded Control Centre history are implemented and CPU checked.
- Notification Stream moved from Telemetry & Notifications to Quick Controls: split wide, stacked narrow.
- Apply/Cancel uses shared modal confirmation; cancellation preserves drafts/files and captures underlying input.
- Rounded dialogue is wired to real saves; semantic-header variant is exercised by an explicitly labelled fixture.
- Shared tabs are halved to 200 × 18 logical pixels; Invert Y-Axis is a plain switch without a field.
- Genuine notification action callbacks and general severe-error fallback remain pending.
- Severe-error reporting is not Vulkan-specific and cannot guarantee recovery/display after a hard PC/OS/driver hang.
- Current host is DX11; an actual Vulkan device-loss fallback integration is not claimed.

## Shared panel hosting and layout persistence

- Approved sequence and remaining phases: `Plans/Ongoing/EngineUpdate.md`.
- Native patched ImGui DockSpace; original browser/stream content; no duplicate content.
- Workspace offers Draggable / Dockable Content Browser; Draggable is default and persists personally.
- Correction G places the centred `+` directly after the last tab; original A–F remain intact.
- Shared top/bottom and native trapezoidal tabs use the opaque darkest named theme surface.
- Actual pixel checks: plus centring/adjacency; both drawers across eight themes at 100% and 125%.
- Real native pointer tab/catalogue/close/reorder checks, builder split, original browser keyboard/state checks.
- Default-off **Settings → Workspace → Allow detached OS windows**, real Apply/Cancel, personal-only projectspace.
- GLFW/DX11 exhibit callbacks wired for secondary windows, minimized-main frames and shutdown.
- Personal projectspace workspace.layout now restores native tabs/order/selection, splits, floating geometry and open panels.
- Frontier validates bounded TOML, rebuilds native docking and keeps ImGui INI disabled; other preferences are preserved.
- Settled autosave, final flush, invalid-layout fallback/write protection and native Reset saved layout are implemented.
- Current Debug and ASan/UBSan: 14/14 each; Windows cross-build/package inspection passes; 43 CPU captures retained.
- Vulkan default editor and separate DX11 exhibit now cross-build; monitor recovery has geometry/callback checks.
- Physical Windows GPU/mixed-DPI/hotplug runtime remains unavailable, not established by synthetic tests.
- Windows OS-window/GPU/multi-monitor execution unavailable; cross-build is not runtime verification.
- Evidence, reproduction and explicit limitations: `../Exhibits/Docking/README.md`.

## Format specification review — planning only

- `Documentation/Plans/Ongoing/SpaceFormats.md` is the single pending format plan, not an approved implementation.
- Original brief: `Documentation/Plans/References/SpaceFormatsOriginalBrief.txt`, with a retained SHA-256 pin.
- Audit baseline remains Frontier- at `338293863d7df4e585dec0c4acae45fe9c524ed5`, not this checkout.
- `.fontspace` is proposed; baseline still has 13 registered Space types. Existing `.arc` CAD persistence is retained.
- Selected control terminology: PanelExpansionTab; approved production extraction and Content Browser reuse implemented.
- Live evidence/review entry: `Exhibits/SpaceFormats/index.html`; includes new native CPU navigation captures.
- B approved: editable authoring sources retained; compiled binary runtime packages may embed payload files.
- Format codec implementation remains gated. UI navigation fixes are separately authorised; no commit, push or Actions.

## Verification

`Exhibits/ControlCentre/README.md` contains current commands, results, evidence, and limitations.
All twelve local tests passed in Debug and with address/undefined-behaviour sanitizers.
Forty-one native captures and Windows cross-build/package inspection passed in the full driver.
All 151 SVG symbols remain ready. Native Control Centre images are CPU raster output, not Windows GPU captures.
Windows x64 cross-compilation succeeded using Zig 0.13.0 / Clang 18.1.6 obtained through PyPI.
The unsigned Windows executable is packaged with fonts, SVGs, licences, and integrity metadata under ignored Binaries.
Native MSVC and Windows runtime/GPU execution are still unavailable. No Linux product binary is delivered.
No GitHub Actions are authorised.

## Known rule findings

The port preserves existing APIs for this bounded batch; it is not a clean naming/formatting certification.
Examples requiring repair: `VectorGlyphRecord`, short local names, comment vocabulary,
namespace brace layout, header banners, and non-approved decorative glyphs in inherited source comments.
The earlier official Windows example is now GLFW-hosted; its original display-adapter provenance remains explicit.
The four mandatory skill documents have not been weakened or edited to waive these findings.

## Scope history

The earlier documentation-only validation script was removed when that phase was clarified.
Subsequent user instructions authorise source porting, local validation code, proof images, and commit/push.
No full baseline replacement or inherited C++ relocation has occurred.

The selected theme/control configuration APIs now use `Configure...` rather than the banned `Assign...` verbs.
The new appearance files use Allman braces, flat namespace indentation, and descriptive file headers.

## Content Browser and inherited ownership

- `Exhibits/ContentBrowser/README.md` maps the pinned React prototype to the scoped native browsing implementation.
- Browsing is read-only; previews are metadata/type glyphs. No materials, painting runtime or format migration.
- `Engine/Interface` owns the catalogue and shared expansion control; `UI/Editor` owns editor panels.
- `Applications`, `Executables` and `Internal` are inherited, not linked by current CMake, but old scripts use them.
- No deletion or relocation is approved; keep editor-specific panels outside the shared engine.

## Drawer and content correction

- Replaced the floating settings window with the reference top-edge trapezoidal drawer and inherited spring motion.
- Actual upward mouse swipe closes the sheet; added flick, axis lock, resize, focus-loss, and scaled-geometry tests.
- Renamed ContentDirectory and moved shared content into EngineContent/Icons and EngineContent/FontArchives.
- Native discovery reads metadata from TOML; Unicode file loading and corrupt-face rejection are exercised.
- GLFW 3.4 is pinned under ExternalPackages and used by the Windows host. toml++ remains the C++ format dependency.
- CLAUDE.md records Vulkan/Slang, GLFW, editable TOML authoring, and the approved binary runtime-package exception.
- Windows runtime/GPU checks remain pending; no Actions are authorised.

## Authorised editor continuation

Milestones and pending scope are in `Plans/Ongoing/EngineUpdate.md`; implementation evidence is in
`../Exhibits/Docking/README.md`. First batch adds Vulkan editor UI display and saved/live monitor recovery.
Named presets, notification actions, supplied severe-error UI, workspace keyboard commands and remaining Space-format
choices/next codec remain pending. Project Browser follows those milestones. Content Browser preview work is skipped.

## Project Browser HTML redesign — current user priority

Prototype lives in `Experimental/Project browser/`, before native startup integration. Promotional intro/footer text
removed per correction. The pinned reference HTML's EON/GGX WebGL renderer, actuator and drafting contours now supply
live previews and captured thumbnails. History/header versions select real sample geometry revisions; no disk history
or native version restore is claimed. Browser-level rendered evidence: `../Exhibits/ProjectBrowser/Results/BrowserChecks.json`.
Project/CAD/geometry browser design is the current priority; earlier editor continuation milestones remain pending.
The latest correction removes the global branding header, aligns one header per shared fixed column, adds independent
side-panel collapse, a pill version selector and complete A4-fit Markdown sheets. Real Chromium panel-boundary/A4/
collapse checks and render/version regression checks pass; evidence remains under `Exhibits/ProjectBrowser/`.

Preview cache correction: asset URLs now carry content revisions to prevent new markup from using an older cached
script. Embedded-width catalogue checks verify six cards, all open/back routes, filtering and reload; evidence is
`Exhibits/ProjectBrowser/Results/CatalogueLoadingChecks.json`. Layout/render regression checks also remain passing.

## Native Project Browser conversion — started

User authorised C++ conversion with **Blank Project only** as a creation preset. `UI/Editor/ProjectBrowserPanel`
now provides real projectspace browsing/selection/creation, three aligned columns, collapsible sides and shared palette
input. The Windows host starts in the browser without project arguments, offers native open/save dialogs and wires a
validated selection into editor ownership. Existing CLI project routes remain. No sample scenes become native presets.

Bounded CPU checks compile against pinned ImGui and the existing project/preference/directory codecs; creation,
overwrite refusal, fresh identity, read-only browsing, hand-off and desktop/narrow draw/layout checks are exercised.
See `../Exhibits/ProjectBrowser/README.md` for implementation boundaries and remaining conversion. Native Windows
compilation/execution, scene/thumbnail rendering, A4 Markdown and complete inspector/version parity remain pending.

## Renderer and painting integration — CAD, MatCap and preview PBR offscreen

Current order and source comparison are in `Plans/Ongoing/RendererIntegration.md`. User authorized sequential
1 CAD → 2 MatCap → 3 preview PBR → 4 full materials/shaders, preserving both painting apps. CAD is first, using
Vulkan/Slang surface depth and explicit topology edges; no full visibility-buffer dependency. Painting, ray visibility
and ReSTIR retain their later acceptance gates.
A is pinned at Slate `8a7004983b10c079564becb932ae9b444716b9c4`; B at `c552cb26bf2be710baaefe64b830fe6db28f7ae7`.
Pinned copies under `Experimental/SlateReference` preserve the full painting UI app, both exhibits and shared native
C++ reference. B is the proposed structural base, A a feature/test donor; no hardware-performance winner is claimed.
Local Node replay passes A 64/64 and B 46/46. Source fingerprints and a reproduced light-bin capacity comparison are
in `Exhibits/RendererPlanning`. Both exhibit ray paths are software traversal, and their GI remains distance-field/card
based. The existing narrow binary `.pigment` does not persist a complete painting session; compatibility needs a separate
format decision under the existing TOML authoring rules. TextureStudio's format remains separately identified below.

The first CAD offscreen slice is now authored in `Engine/Render`: shared device programs, independent geometry
residency and sized image targets. Four Slang entries compile; reflection verifies push constants and vertex locations.
Actual native C++/SwiftShader readback passes camera/thumbnail, edge occlusion, near clipping, cancelled-transfer retry
and retained-resource tests; one geometry upload serves both camera captures and thumbnail, with one shared pipeline
pair. This is software Vulkan execution, not Windows/hardware or validation-layer verification. Evidence and exact
reproduction: `../Exhibits/Renderer/README.md`. Native presenter, interaction/DPI, document geometry/cache, broader
precision/topology tests and Windows/validation-layer execution still gate native acceptance.

User explicitly requested MatCap next. MatCap now shares geometry/targets with CAD and adds resident texture/sampler
lookup plus a bounded 128-byte projection/normal ABI. CPU bilinear reference agrees with Vulkan pixels for rotation,
image orientation, filtering, tint, scale/shear/reflection and opacity/depth. Mode/texture round trips, thumbnail reuse,
cancelled transfer retry and retained source lifetimes are checked. Analytical normal calibration is test geometry,
not a second CAD document/preset. CAD/MatCap offscreen code is covered by the shared verification runner; evidence
and execution boundaries remain in `../Exhibits/Renderer/README.md`.

Preview PBR now shares geometry, targets and texture residency with those modes. `ScatteringStratum` and the bounded
`PreviewScattering` sequence support ordered opaque GGX/diffuse evaluations and dielectric coatings with Fresnel/Beer
attenuation, rather than parameter blending. Slang reflection checks the 128-byte projection and 672-byte uniform ABI.
Direct light, bounded analytical-sky integration, planar sRGB albedo sampling, exposure, tone mapping and display encoding
are implemented. The earlier `Experimental/Project browser` exposes actual software-Vulkan readbacks through a same-origin
bridge, with CAD/MatCap/PBR selection and roughness/metallic/coating/exposure controls. This is not WebGL evidence for
Vulkan and not native editor presentation. Authored opaque/coating materials, UVs and normal inputs are now implemented
as described below. Advanced transport, GI/RT and native Windows/hardware execution remain later acceptance gates.
Both painting apps remain unchanged. Reproduction and current check reports: `../Exhibits/Renderer/README.md`.

## Correct painting app reference — TextureStudio

User corrected the app identity to `streamlinkinbox/Frontier` commit
`773b2f5a2672ae530accff000d65d779e9059b66`. The complete 300-file source/artifact tree is preserved unchanged in
`Experimental/TextureStudio`; its original self-contained build is served on port 8774 with Texture selected.
Previous incorrect previews on 8772/8773 are stopped. Chromium checks pass: 25,440-triangle teapot, renderer ready,
Lit/Wireframe switching, no page errors or external runtime requests. Evidence: `Exhibits/TextureStudio`.
The integration plan now uses this as the primary painter reference; its project export is `.texture.json`.


## Authored surface materials — Vulkan offscreen and earlier HTML

The bounded opaque/coating material path is implemented: strict versioned TOML `.material` authoring with pinned
TOML++, contained PNG/JPEG texture ingestion with pinned stb_image, typed colour-correct/normal-aware mip pyramids,
authored UV/tangent geometry, ORM/normal/radiance inputs and per-stratum texture selection. Preview and authored
materials share `ScatteringEvaluation.slang`. Up to eight ordered opaque/coating strata compile into an immutable
1504-byte material uniform (preserving the original 1216-byte prefix) and four typed resident texture arrays. One additional shared pipeline serves all materials.

The earlier HTML now provides material selection, TOML Apply, Import and Export, actual native Vulkan readbacks,
compiler diagnostics and last-valid-image retention. Material parameter changes reuse textures and geometry.
Four authored evidence specimens are not native project presets. Exact native, sanitizer, cross-build and browser
checks remain under `Exhibits/Renderer/Results`; implementation rules are `Rules/SurfaceMaterials.md`.

This completes the bounded authored opaque/coating material slice, not every possible PBR model or renderer feature.
Native editor/document assignment and presentation, Windows/hardware/validation-layer execution, arbitrary shader
expressions, transmission/refraction, SSS, complex-IOR conductor Fresnel, multiple scattering,
HDR environment prefiltering and GI/RT remain open. No painting app sources were changed.


## Directional layered PBR extension — 2026-10-18

Implemented independent per-stratum albedo/ORM/normal sources, UV transforms and normal strengths; source-deduplicated
Vulkan texture arrays; signed/rotated anisotropic GGX distribution, correlated visibility and matched environment
sampling/PDFs; optional exact dielectric Fresnel. Root texture/UV defaults and existing CAD/MatCap/preview routes remain.
BrushedCopper now exercises anisotropy; CoatedCeramic exercises independent coating maps, UVs, normals and exact Fresnel.
The full advanced-material request is **not closed**: native editor assignment/presentation, transmission/refraction,
SSS, multiple scattering, complex-IOR conductors, HDR environment transport and Windows/hardware acceptance remain open.
No GI/RT/ReSTIR or painting integration is claimed. Current verification is recorded in Exhibits, not inferred from
shader compilation; see `Exhibits/Renderer/Results/Verification.json` and browser reports there.


## Texture Studio standalone Material Library — first implementation

Plan: `Plans/Ongoing/TextureStudio.md`. Native material documents use `.material` containing TOML; the reference
`.texture.json` format remains separate. Experimental TextureStudio was selectively updated from PR #1 at
`85be136ba213be6768b3acdd4c0a68cbe24c6bd2` (47 changed files), with both painting apps and local additions preserved.

Implemented shared material catalogue/filtering, source/apply/save separation, contained texture compilation,
exclusive create/duplicate, external-edit conflict detection and source preservation. The native lifecycle-controlled
ImGui library/source panel has real Apply/Revert/Save actions and dirty-edit protection. Frontier's startup loop is
now shared through `Engine/Interface/WindowsApplication`; the standalone composition and CMake `TextureStudio`
target use the same window/input/Vulkan/ImGui/appearance foundation. No CAD Project Browser is launched by it.

Local strict and ASan/UBSan checks execute the library and ImGui interactions; Windows object compilation includes
both Vulkan applications and the retained DX11 host. Exact checks/commands/digests are in
`Exhibits/TextureStudio/Results/MaterialLibraryChecks.json`. The CPU ImGui capture is not a Windows screenshot.
The standalone executable is not yet linked or executed here; native preview/thumbnail registration, structured
layer editing, import/save-as UI, OS folder picking and last-library persistence remain pending. CMake configuration,
Windows execution and hardware acceptance are not inferred from object compilation.

Vulkan validation was removed from the application Project Browser HTML. Its dedicated review is now
`Exhibits/Renderer/index.html`, linked alongside native/ImGui evidence in `Exhibits/SpaceFormats/index.html` and
`Exhibits/ProjectBrowser/index.html`.
The original Project Browser catalogue/reference routes remain available separately. No PR merge, commit or push.

Final local verification: 77 strict checks and 77 ASan/UBSan checks; seven Vulkan Windows x64 objects plus two
retained DX11 objects compile. The updated reference production build and all four Node paint checks pass.
Existing CAD/MatCap/PBR/material renderer regressions pass; these do not establish Windows or hardware execution.
The standalone Control Centre suppresses the general Content Browser; material-only startup remains the scope.
Final browser checks also pass: renderer editing/readbacks, original six-file catalogue/filter/reload, five ImGui
evidence tabs, embedded library/original-brief bytes and private-path refusal. Reports remain in their Exhibits folders.

## Texture Studio presentation correction — 2026-10-18

The user requires the native Material workspace to match the HTML visually, using existing shared sliders/cards
and real dockable windows. The current fixed list/TOML panel is only a functional IO increment, not accepted visual
parity. Next UI work must use `ControlKit` and the inherited `PanelDockspace`; source editing becomes secondary.

Thumbnail direction: one shared GPU-resident atlas, GPU-baked and GPU-sampled by cards. Use bounded residency,
revision-driven dirty tiles, safe slot retirement and tile-edge filtering; no normal-path CPU pixel round trips.
This architecture is recorded in `Plans/Ongoing/TextureStudio.md`; atlas rendering and corrected docked UI are not
implemented or verified yet. Existing strict/sanitized results remain evidence for the preceding IO increment only.

## Docked Material workspace and GPU atlas implementation — 2026-10-18

Replaced the fixed list/source split with three registered `PanelDockspace` windows: card library, material viewport
and inspector. Defaults follow the HTML side widths; inherited docking and shared theme/typeface settings remain.
Cards reuse `ControlKit::SectionCard`; roughness/metallic/anisotropy use shared sliders and comment-preserving scalar
source edits. Apply/Revert/Save remain transactional; raw TOML is secondary. Layout uses a distinct personal
`.projectspace` workspace key so Frontier's saved layout is not overwritten.

Added shared Engine `MaterialLibraryRendering`, `MaterialThumbnailAtlas` and preview-sphere geometry. Native Vulkan
composition now connects the preview and one sampled thumbnail atlas to ImGui. Tiles are rendered on GPU into a
scratch target and copied on GPU with replicated gutters. Visible requests pin slots before eviction, generations
reject stale references, and immutable baking resources retire against completion serials. One dirty tile is budgeted
per submission, with preview work separate to avoid repeated first-use transfers. No normal-path thumbnail readback.
Apply/Refresh invalidate affected data; unchanged applied content does not rebake. Filesystem watching is deferred.

Not closed: complete HTML visual parity, remaining inspector/reference interactions, preview shape/orbit/zoom,
OS folder picking, last-library restoration, Windows executable linking/launch and multi-window runtime acceptance.
Evidence commands and execution boundaries are documented in `Exhibits/TextureStudio/README.md`.

Current-source verification passed: **101 strict + 101 sanitized** library/source/ImGui checks and Windows COFF
object compilation; strict/sanitized Vulkan atlas/residency checks; 1440 × 1000 CPU-composed native UI with actual
Vulkan atlas/preview pixels; existing CAD/MatCap/PBR/material renderer regressions. The refreshed ImGui evidence
viewer passes full-browser navigation, embedded-image byte checks and private-path refusal. The HTML Material
workspace was captured at the same dimensions for an explicitly non-parity comparison. All three native material
reports have matching current-source hashes. These results do not establish Windows linking/execution, hardware
GPU execution, complete shared appearance presentation or pixel parity; the remaining scope above stays open.

## HTML catalogue and material styling correction — 2026-10-18

Replaced renderer specimens as the native default with the actual 127 HTML presets. Source-generated typed TOML
retains original normalized recipes, order, categories, labels and controls; native physical definitions remain a
supported subset, not procedural shader equivalence. Windows default content copying skips existing personal files;
explicit library folders are not populated. Both painting references remain unchanged.

Native presentation now follows reference header/panel measurements, dark dock tabs, collection/category pills,
card geometry, rounded property sections and shared slider dimensions. DM Sans faces are recovered from the same
pinned fontsource package as the HTML. Colour/coefficient edits preserve source formatting. Preview/atlas geometry
uses the reference shader-ball profile. Current-source checks passed **237 strict + 237 sanitized** catalogue/source/ImGui checks and Windows object
compilation. Atlas strict/sanitized checks, the composed 1440 × 1000 capture and the existing renderer regression
suite passed. The HTML capture waits for all reference thumbnails to complete. No 1:1 signoff: semantic icons,
remaining inspector/viewport interactions, pedestal and full procedural/environment shading still differ.

Final evidence refresh: all current-source material reports and renderer regression reports have matching hashes.
The side-by-side HTML/native supplement passes browser image-byte/navigation checks, and the reference capture
waits until all 127 thumbnails finish. Font recovery from the pinned npm WOFF archive also passed in isolated
scratch. Windows execution/hardware-GPU acceptance and full 1:1 parity remain explicitly unverified/open.


## Automotive shader, specimen base, inspector and scoped icons — 2026-10-18

Implemented the actual native automotive family for all seven reference paint presets, particularly Racing Green:
object-space cellular particles, 1–4 buried layers, binder depth, 12-stop palette/ramp/single colours, 48-sample
linear-space unresolved tint, derivative filtering, roughness/metallic variation, particle tilt and independent
orange-peel clearcoat normals. Strict `.material` codec and Apply/Revert/Save/Duplicate retain these fields.

The shared specimen now contains the grooved shader ball, tapered pedestal, lower foot and metallic trim ring.
GPU sensitivity checks prove that paint edits do not alter pedestal pixels. Inspector cards scroll/collapse,
wrap captions, render shared sliders with disabled alpha and correct tooltip hit regions, and support numeric
units, logarithmic particle size/repetition and integer layer counts. Native roughness endpoints are bounded to
0.045 to prevent invalid gloss drafts. Shared editor SVG symbols are inherited across workspaces; Texture-only
UV/paint/decals and Fluid-only smoke/water have separate catalogues and enforced raster/presentation domain guards.

Final local results: **250 strict + 250 ASan/UBSan library/UI checks**, including the real green-paint gloss endpoint;
**58 + 58 paint CPU/GPU checks** and **90 + 90 production SVG/domain checks**; strict/sanitized atlas checks;
CAD/MatCap/PBR/authored-material regression and Windows cross-compilation; 35 font faces/5 families verified.
The 1440×1000 workspace capture combines actual SwiftShader Vulkan images with CPU-rasterized ImGui/ThorVG art.
Macro control tests use 128px/1800 µm for bounded software-driver cost; the workspace uses the actual 450 µm preset
at the production 512px preview resolution. None of these is a Windows OS screenshot or hardware proof.

A real linked **Windows x64 GUI executable** is packaged in `Binaries/TextureStudio-Windows-x64.zip` with fonts,
icons, 127 presets, ten verified compiled Slang entries, notices, instructions and complete payload/source hashes.
`Exhibits/TextureStudio/Results/DeliveryChecks.json` verifies current test/source and ZIP/PE/shader fingerprints.
**Windows launch/hardware/multi-window acceptance remain unexecuted here. Other procedural families and exact
HTML/native visual parity remain open.** No commit, push, branch switch, PR merge or CI workflow was made.

## Pattern / Stamp Boolean icons, masks and native GPU continuation — 2026-10-18

**Partial implementation, not a completed 1:1 native port.** HTML was changed and tested before native controls.

| Scope                                      | Current result                                                        |
|--------------------------------------------|-----------------------------------------------------------------------|
| HTML Pattern and Stamp Boolean actions     | Four icon-only buttons; accessible names and descriptive tooltips kept.|
| HTML masks                                 | Ellipse, rectangle and embedded PNG/JPEG/WebP; alpha/luminance, invert, |
|                                            | opacity, position/extent, disable/remove, history and alpha export.     |
| Native GPU expression evaluation           | Shared Slang compute + persistent Vulkan output, explicit retirement. |
| Native retained authoring                  | Bounded source instructions, masks, TOML round-trip and undo/redo.     |
| Native reusable Boolean/mask controls      | Constructed and pointer-tested; **not hosted by native workspaces yet**.|
| Native camera and viewport                 | Destination/displayed 120 ms motion; Blender MMB/Shift-MMB/Ctrl-MMB;   |
|                                            | full available preview rectangle and aspect-correct projection.       |
| Native development console                 | Default-on central CMake/header policy, Win32 allocation and log tee; |
|                                            | default/disabled policy compiled; Windows execution **not verified**. |

Local HTML verification: **18/18 browser tests passed**, including actual exported mask pixels, embedded image
alpha/luminance, inversion, zero opacity, Pattern inspector history/save/reopen, existing Stamp Boolean holes,
original text preservation, gradients, component stack and view-only canvas backgrounds. Production build passed.
Captures: `Exhibits/TextureStudio/Images/PatternMaskIcons.png` and `StampMaskIcons.png`.

Native verification: Vulkan readback **35 assertions / 10 dispatches** on **SwiftShader software Vulkan**, not a
hardware GPU run. CMake integrates the renderer, compute compilation/reflection and readback validation. Native
source/control, camera input/motion and existing material-library checks pass locally. The native control specimen
is a CPU-rasterized ImGui draw-command capture, not a Windows screenshot. `NativeFullViewport.png` combines actual
Vulkan material pixels with the existing ImGui evidence rasterizer; docking remains present.

GPU Boolean output is non-destructive coverage/CSG, **not reconstructed editable Bézier contours**. Cubic fill uses
analytic monotonic partitions; edge/stroke distance remains a 32-interval approximation. Translucent Boolean
coverage semantics are not yet signed off against geometry-first HTML results. No performance improvement claim.

**Still open:** actual native Pattern/Stamp workspace hosting and full approved HTML layout/tool parity; native
image-mask residency/import; native text/brush/image/PBR-target preservation and authoring import/export integration;
editable contour reconstruction if required; complete viewport-resolution allocation rather than the retained
512-pixel material attachment; softbox/normals and full material-detail parity; Frontier Project Browser/notch/tabs/
Control Centre Windows acceptance. Shared native controls are not a substitute for the missing full workspace port.
No new Windows binary, commit, push, GitHub Actions run or publication was produced by this continuation.

## HTML authoring-first cleanup and owned paint masks — 2026-10-18

**Native Pattern/Stamp work is paused for HTML review.** The previous native foundation is unchanged; none of the
HTML improvements below constitutes a native workspace port or new Windows acceptance.

| HTML area              | Implemented and locally checked                                                   |
|------------------------|-----------------------------------------------------------------------------------|
| Pattern and Stamp bars | Seven primary drawing tools; additional tools, canvas options and arrangement in  |
|                        | disclosures. Contextual Boolean icons; less-frequent document actions in one menu. |
| Pattern layers         | Stamp-style icons/name/type/opacity rows; library can collapse to give layers room.|
| Mask selection         | Indented selectable entries beneath their artwork; owner selection or Done exits. |
| Paint and fills        | Black/white/grey brush, erase, opacity/softness, shapes/outlines, paths and gradient.|
| Existing masks         | Embedded images, rectangle/ellipse sources, inversion, opacity, enable and remove.|
| History and integrity  | One completed gesture per undo; cancellation, locked editing protection, JSON     |
|                        | save/reopen, original text/components/channel targets and transparent exports.     |

Final focused/artwork regression run: **29/29 passed (2.4 minutes)**. Original Stamp composition/rich-text/import/
transform/phone checks: **12/12 passed (2.0 minutes)**. These are **41 selected checks**, not certification of the
entire Texture surface-placement suite. Production Vite build passed in **12.36 seconds**, with its existing
bundle-size warning. Normal toolbar captures completed without browser page errors; 900 × 680 mask layouts keep
the canvas and Done visible. Original Stamp phone checks also covered 414 × 896 and 390 × 844.

Actual pixel assertions include fractional image alpha/luminance beneath paint and full-white mask preservation
of stroke outside fill bounds. A real SVG bounding-box defect was found and corrected: a non-interactive geometric
enclosure preserves stroke and prevents flat-path mask bounds from collapsing. Painting retains at most 256
operations, 2,048 points per operation and 16,384 points per mask; existing document-size limits remain.

Evidence: `Exhibits/TextureStudio/Results/ArtworkMaskAuthoringChecks.json`, `ArtworkMaskAuthoringChecks.log`,
`ArtworkMaskOriginalStampEditing.log`, `ArtworkMaskBuildChecks.log` and `ArtworkToolbarCaptures.log`.
Actual screenshots: `PatternCompactToolsNormal.png`, `StampCompactTools.png`, `PatternMaskPainting.png`,
`StampMaskPainting.png`, `PatternMaskCompactViewport.png` and `StampMaskCompactViewport.png` in the Images directory.
Reproduction: `Exhibits/TextureStudio/Code/ArtworkMaskAuthoringValidation.mjs` and `CaptureArtworkToolbars.mjs`.
Sixteen native source fingerprints still match the preceding report; native tests were not rerun for this HTML-only
revision. No commit, push, GitHub Actions run, executable publication or Windows execution.

## Inspector-based selection operations and switches — 2026-10-19

HTML Pattern and Stamp now keep selection operations in the Inspector, not above the canvas.

| Selection | Inspector behaviour                                                        |
|-----------|----------------------------------------------------------------------------|
| One       | Canvas alignment and existing individual properties. Stamp composition      |
|           | alignment preserves native rich text rather than converting it to vectors. |
| Two       | Relative alignment, eligible Boolean icons, either explicit mask direction, |
|           | shared colour/fill/stroke for unlocked vectors and shared opacity.           |
| Multiple  | Shared properties and alignment; distribution becomes available from three. |

Stamp modifier-click selection also works from the composition stack and enters the shared vector workbench.
Changing shared opacity retains the multi-selection. Unsupported native artwork keeps its own original editing
controls; shared vector paint does not overwrite native font runs or image sources. Mixed paint controls show an
editing reference, while mixed opacity uses an empty Mixed field. Visible checkbox presentation was replaced by
rounded, keyboard-accessible switches in these workspaces and their shared drawing controls.

Use-as-mask creates a **512-pixel alpha snapshot**, retaining the original source hidden. It is **not live-linked
editable mask geometry**. Target-local coordinates account for rotation/scaling; locked or hidden sources/targets
and an existing receiving mask are protected. Creation and source visibility changes are one reversible edit.

Verification: **31 regression checks passed**, then **12 final interaction/original Stamp transform checks passed**.
Two selection cases overlap: **41 unique selected checks**, not 43. The final checks include single native-text
alignment, shared colour/opacity, actual Boolean execution, either-direction controls, mask raster alpha, source
retention, undo and rotated-coordinate/protection cases. The final production build passed in **9.83 seconds**
with the existing bundle-size warning. Port 5173 remains live and responds to the preview host.

Evidence: `Exhibits/TextureStudio/Images/PatternSelectionInspector.png`, `StampSelectionInspector.png` and
`Exhibits/TextureStudio/Results/SelectionInspector{Checks,FinalChecks,Build}.log`.
Reproduction and source/capture fingerprints: `Code/SelectionInspectorValidation.mjs` and
`Results/SelectionInspectorChecks.json` in the same exhibit. Native C++ remains paused; no commit, push,
GitHub Actions run, Windows execution or executable publication.

## Shared artwork arrays and unique outliner captions — 2026-10-19

HTML Pattern and Stamp now share **Inspector → Array / pattern**. Layouts include linear, grid, circular/arc and
radial rings. Scale profiles include uniform, linear progression, circular wave, radial distance and seeded noise.
Generation replaces selected sources with editable copies in one undoable edit, not a live modifier. Group offsets,
masks, original native Stamp SVG/text and PBR channel targets are retained. Limits remain Pattern 64 / Stamp 24.

Both document normalizers resolve case-insensitive caption collisions with numbered suffixes while preserving unique
custom names. Owned mask captions include their artwork caption. The superseded unused Pattern array action was removed.

Verification: **46 unique checks succeeded** (5 array cases plus 41 retained Inspector/artwork regressions), and **6
additional overlapping hover repetitions succeeded**. The production build succeeded in **9.14 seconds**, with the
existing bundle-size warning. Preliminary runs each had one strict PNG-byte hover comparison failure: captures showed
small SVG rasterization differences with unchanged geometry. The test now compares decoded pixels with bounded
antialiasing tolerance and an injected opaque-overlay negative control; the final complete regression succeeded.

Evidence: `Exhibits/TextureStudio/Images/{Pattern,Stamp}ArtworkArrays.png` and
`Exhibits/TextureStudio/Results/ArtworkArraysChecks.json`, with source/capture fingerprints and retained logs.
Reproduction: `node Exhibits/TextureStudio/Code/ArtworkArrayValidation.mjs`. Preview host responds HTTP 200 on port 5173.
Native C++ remains paused; no native changes, commit/push, GitHub Actions run, Windows execution or publication.

## HTML Pattern channels, nested groups/folders and linked editing — 2026-10-19

Pattern now exposes Colour, Height, Normal, Roughness, Metalness and Opacity in **Inspector → Surface channels**,
with **+ Channel** for ambient occlusion. Unlocked multi-selection supports mixed amounts, switches and supplied
normal maps. Actual channel PNGs can be inspected/exported. Independent scalar-channel coverage preserves disabled
contributions beneath other layers. Normal maps handle native/legacy UV extents, rotation, flips and strength; export
normalizes tangent vectors. A real PBR framebuffer test confirms OpenGL +Y green-channel lighting direction.
Height remains shaded relief, not silhouette displacement or a certified physically calibrated bake.

**Layers → Groups & folders** provides named nested containers, destination moves, visibility/locking, collapse and
rename/dissolve. Separate document-wide group-property and linked-instance switches default off. Group-wide edits
apply the changed appearance/channel property through the nearest group; linked edits update existing matching feature
slots, keeping position/rotation local and changing feature dimensions proportionally. Locked artwork and unrelated
channels are protected. New artwork enters the active container; arrays retain independent copied enclosures.

**Stored groups** provides search, actual thumbnails, independent/linked insertion, detach, and removal of unused
unlinked definitions. Definitions and containers are document-owned and survive save/reopen/history. Structural member
additions/deletions or container restructuring do not automatically rewrite other linked placements. Normal-map files
are embedded PNG/JPEG/WebP under 1.5 MB; existing 64-layer/12 MB budgets remain, with 64 containers, 12 nesting levels
and 24 stored definitions.

Verification: a frozen-source **55-case regression succeeded**, followed by **16 final checks** after normal-map
orientation/framebuffer refinements. Fourteen overlap: **57 unique selected checks**, including nine new feature cases.
The final build succeeded in **12.13 seconds**, with the existing bundle-size warning. Earlier browser checks were
invalidated by live source hot reload during edits; the frozen-source/final checks succeeded. Two historical Pattern
browser cases still use the superseded toolbar and are explicitly excluded; current interaction specifications replace
their UI coverage. Browser validation used headless Chromium/SwiftShader, not native hardware execution.

Evidence: `Exhibits/TextureStudio/Images/Pattern{SurfaceChannelsAndGroups,WeaveChannelMaterial}.png`,
`Exhibits/TextureStudio/Results/PatternChannelsGroupChecks.json`, associated logs and source/capture fingerprints.
Reproduction: `node Exhibits/TextureStudio/Code/PatternChannelsGroupValidation.mjs`. Preview host responds HTTP 200
on port 5173. Native C++ remains paused; no native changes, commit/push, GitHub Actions or executable publication.

### 2026-10-19 — HTML retest passed; modular native authoring resumed

- Latest user instruction supersedes the earlier native pause. Fresh frozen-source HTML verification:
  **57/57 Pattern/Stamp checks passed (5.5 min)**; **48/48 additional painting/document/Stamp model checks passed**;
  production build **passed (12.39 s)** with the existing bundle-size warning. These are selected regressions,
  not the entire application suite. Port 5173 remains available.
- Added Engine-only `FrontierArtwork`. Extracted renderer/document-independent `ArtworkArrayPlacement` and
  `ArtworkAuthoringProperties` for future Texture Painting reuse; workspace presentation remains outside Engine.
  Retained-vector adaptation ports all four array layouts/five scale profiles, unique captions, source Boolean/cubic
  expressions, owned masks and independent enclosures. Native nested visibility/protection and changed-field
  group-wide channel patches retain source-only TOML/history. Channel rendering and painting integration remain open.
- Repaired no-op native edits erasing redo, workspace-limit changes through history, unknown/wrong-typed TOML
  properties silently disappearing/defaulting, and real pointer edits of locked masks. Also reproduced/repaired
  identity array adaptation losing small unselected coordinates; retained source descriptors now remain exact.
- Final source verification: **2,269 strict C++ assertions passed**, repeated under address/undefined sanitizers
  with leak detection; **42 native control assertions passed**; **54 actual software-Vulkan assertions across
  13 dispatches passed**. Shader reflection retains the 96/32-byte instruction/segment ABI. Actual GPU array/mask
  readbacks and CPU ImGui capture retained. Windows x64 **headless validation cross-link passed** with PE/machine
  inspection using reviewed Zig 0.13.0; not executed on Windows, not an application build or new downloadable ZIP.
- Updated CMake ownership, compute-validation linkage and the explicit Windows source enumerator for the extracted
  library. No full CMake/application build, Windows/hardware runtime, Defender scan, commit, push, Actions or release.
- Remaining: hosted 1:1 Pattern/Stamp panels and original tools; native GPU channels/normal-height/export; painted/image
  masks and GPU residence; stored/linked group definitions; native Painting integration, Unicode caption/placement parity;
  hardware-dependent material detail/performance fixes and Windows acceptance. Native existing limits remain enforced.
- Evidence/report: `Exhibits/TextureStudio/NativeAuthoringResume.md`, `Results/NativeArtworkEditingChecks.json`,
  `Results/NativeAuthoringResumeChecks.json`; reproduction: `Code/VerifyArtworkEditing.py`. Fresh HTML/build logs and
  two repaired-defect negative reproductions are retained under the same exhibit Results directory.

## 2026-10-19 — Pattern native UI checkpoint and technical naming

- Pattern native hosting, docked library/layers/viewport/Inspector, shape/array/mask controls and TOML Open/Save
  are implemented. Full 1:1 parity remains open; Stamp UI is not started.
- Renamed shared native `Artwork` identifiers/files/targets to `VectorAuthoring`; Inspector mask controls use
  `VectorMaskControls`. Build lists and current plan references follow the rename. Retained serialization
  discriminator `frontier.vector_artwork` and historical reports remain unchanged for compatibility/provenance.
- Fresh post-rename verification: 2,269 strict source assertions, 2,269 address/undefined sanitizer assertions
  with leak detection, 42 native control assertions and 12 strict translation-unit syntax checks passed.
- Retained pre-rename Pattern verification: 52 interaction checks, 9 source checks, 4 docking checks,
  8 GPU-residency checks, 17 software-Vulkan dispatches; matching sanitizer run passed. Twenty Windows objects
  compiled, without application linking or Windows execution. These do not verify the subsequent rename.
- Restored checksummed native dependencies. Ignored fonts, software-Vulkan/shader artifacts and Windows tools
  did not survive session restoration; full post-rename Pattern GPU/capture and Windows checks remain blocked.
- Evidence/reproduction: `Exhibits/TextureStudio/NativePatternWorkspace.md`,
  `Results/NativeVectorAuthoringEditingChecks.json`, `Results/VectorAuthoringNamingChecks.json` under that exhibit.
  No commit, push, Actions or publication. Next: restore remaining verification prerequisites and close the
  Pattern selection/transform and original-tool parity gaps before Stamp UI.


## 2026-10-19 — Pattern selection and transform gestures

- Continued Pattern-first native UI work; Stamp UI remains unstarted. Added bounds-intersection marquee in either
  direction with Shift-additive selection, eight resize grips and a rotation grip. Shift preserves aspect or snaps
  rotation to 15°; Alt resizes about the centre. Single rotated primitives use local axes; mixed orientations/masks
  constrain scaling to uniform when the retained source cannot express shear.
- Shared geometry remains in Engine `VectorAuthoringSelectionEditing`, reused by the native panel. Independent
  aliases and attached-mask geometry are preserved. Identity source edits now avoid source reordering; Escape has
  priority over same-tick release, focus loss cancels, and completed gestures retain one-edit undo/redo.
- Restored ignored pinned native dependencies, five hash-verified DM Sans faces/licence, software Vulkan,
  Slang WASM and the reviewed Windows cross-toolchain. The previous post-rename GPU/capture blocker is cleared.
- Current frozen-source verification: 1,360 new selection assertions and 2,269 existing authoring assertions,
  each repeated under ASan/UBSan with leak detection; 42 existing native control assertions. Pattern runtime:
  81 input/dispatch checks plus 47 gesture assertions, 9 source / 4 docking / 8 GPU-residency assertions,
  36 actual software-Vulkan dispatches; normal and sanitizer runs passed, including three transformed-pixel checks.
- Twenty Windows translation units compiled, including TextureWorkspace host. Not a full application link,
  Windows execution, hardware timing, downloadable package or publication. No Actions, commit or push.
- Refreshed native captures and added actual marquee/resize/rotation images. Generating code, source hashes,
  commands and limitations: `Exhibits/TextureStudio/NativePatternWorkspace.md`, `Results/VectorSelectionChecks.json`,
  `Results/NativeVectorAuthoringEditingChecks.json`, `Results/NativePatternWorkspace{,Sanitized}Checks.json`.
- Remaining Pattern gate: exact coverage/holes hit testing, group-aware selection expansion, original pen/point/
  text/SVG/image tools/defaults/presets, clipboard/arrangement, complete groups/links/masks, channel/3D/export,
  visual/responsive parity and Windows acceptance. Native mask-isolation canvas gestures remain gated.


## 2026-10-19 — Pattern GPU contour selection

- Replaced native bounds-only click selection with asynchronous shared Slang coverage queries. Rendering and
  picking use the same evaluator for primitives, cubic winding, Boolean holes, strokes and owned masks.
  Highest visible editable token is returned; locked/hidden enclosures, transparency and tile clipping apply.
- Added reusable Engine `VectorSelectionEvaluation`, one-texel output and four-byte token readback with caller-
  reported submission retirement. No queue waits, CPU contour rasterization or silent bounds fallback.
  Source snapshots and authenticated tokens prevent stale results changing cancelled/replaced documents.
- Added pending fast-drag handling: remember release coordinates before the GPU reply and finalize one undoable
  edit. Reproduced the delayed-release risk; verified expected movement using ImGui's quantized input coordinates.
- Both current normal and ASan/UBSan/leak native runs passed: 158 picking, 47 gesture, 85 input/coverage-dispatch,
  9 source, 4 docking and 8 GPU-residency assertions. Coverage/picking GPU dispatches: 38/61. Cases include all six
  primitives, cubic winding, Boolean holes, mask modes, high-bit tokens, 64 candidates, delayed retirement,
  changed source, actual native click-through, cancellation and release-before-reply.
- Existing regressions rerun: 1,360 selection and 2,269 authoring assertions strict plus sanitizers; 42 native
  Boolean/mask controls; 54 actual GPU coverage assertions across 13 dispatches. Twenty-one Windows translation
  units compiled including the application host; no complete application link or Windows execution.
- Added both vector entries to shader build/install and explicit Windows source enumeration. Vector archive
  compilation manifest and source/binary hashes are required by packaging; six valid/negative archive cases
  passed. Existing 4 incomplete-report refusals and 72 distribution-policy cases passed; no package was produced.
- Actual native Boolean-hole selection capture, refreshed prior captures, current hashes/commands/toolchain
  and limitations: `Exhibits/TextureStudio/NativePatternWorkspace.md`, linked `Results/NativePatternWorkspace`
  normal/sanitizer reports and `Results/VectorShaderArchiveChecks.json`. All retained report source/header
  hashes were checked against the final files. No Actions, Defender scan, hardware timing, commit/push/release.
- Remaining: original pen/point/text/SVG/image tools/defaults/presets, group-aware expansion, full arrangement/
  clipboard/groups/links, painted masks, GPU channels/3D/export, exact visuals and Windows acceptance. Marquee
  remains bounds-based; click picking shares rendered AA/cubic approximations, not exact analytic geometry.
  Pattern first; Stamp UI remains unstarted.


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
