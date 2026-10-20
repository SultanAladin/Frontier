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
| 011 | Scene renderer                        | Excluded    | Blank new Render directory by request.|
| 012 | Imported naming/formatting violations | Open        | Audit imported APIs before renaming.   |
| 013 | Source/artwork permissions            | Open        | Owner review before redistribution.   |
| 014 | Control Centre and quick panel        | CPU checked | Windows interaction validation next.  |
| 015 | Open fonts and catalogue              | Checked     | Four families, 30 faces, OFL retained. |
| 016 | Appearance persistence                | Checked     | Reload and invalid-save checks passed.|

## Verification

`Exhibits/ControlCentre/README.md` contains current commands, results, evidence, and limitations.
All six local tests passed in Debug and with address/undefined-behaviour sanitizers.
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

## Drawer and content correction

- Replaced the floating settings window with the reference top-edge trapezoidal drawer and inherited spring motion.
- Actual upward mouse swipe closes the sheet; added flick, axis lock, resize, focus-loss, and scaled-geometry tests.
- Renamed ContentDirectory and moved shared content into EngineContent/Icons and EngineContent/FontArchives.
- Native discovery reads metadata from TOML; Unicode file loading and corrupt-face rejection are exercised.
- GLFW 3.4 is pinned under ExternalPackages and used by the Windows host. toml++ remains the C++ format dependency.
- CLAUDE.md records Vulkan/Slang primacy, GLFW window ownership, content conventions, and TOML runtime formats.
- Windows runtime/GPU checks remain pending; no Actions are authorised.

#### Native Float32 vector surface map recovery (2026-10-19)

- Pattern scope remains approximately 68%; Stamp and full Pattern parity remain deferred/open.
- Added `VectorSurfaceCompute` with a typed `rgba32f` Vulkan storage image; the runtime derives format from SPIR-V,
  requires storage/sampling/linear-filter/transfer capabilities, and refuses unsupported devices rather than downgrading.
  Existing vector coverage and selection remain RGBA8.
- Compiles ordered layer/channel contributions from editable source, including visibility and layer masks. Separate
  RGBA32F maps cover colour, height, normal, roughness, metalness, opacity and occlusion. Contribution alpha is a
  separate mask; disabling a channel produces a neutral map and zero contribution. Layer-specific golden-thread
  metalness remains isolated from fabric metalness.
- Shared PFM encoding preserves IEEE Float32 bits, emits little-endian bottom-first `PF`/`Pf`, and does not clamp,
  gamma-transform or renormalize samples. The ZIP contains 14 actual GPU-readback fixture files, 32x32 each; it is
  not a document export. `Exhibits/TextureStudio/NativeSurfaceEvidence.html` presents the actual capture and GIF library.
- Seven real Vulkan channel dispatch/readback assertions pass strict and ASan/UBSan builds on SwiftShader. A cache-path
  regression now verifies 512x512 retired-slot submission and reuse of equivalent programs; it caught and fixed a reset
  that discarded the selected surface program before dispatch. The regression verifies authored gold/fabric separation,
  default neutral normals, finite Float32 samples, masks,
  PFM byte layout and strict/sanitized byte identity. Shader compiles with Slang WASM 2026.19 and reflects 24-byte
  surface push constants plus `Rgba32f` image storage.
- Native Texture Workspace now exposes the Pattern editor with persistent TOML, Vulkan coverage/selection, and a distinct
  dockspace. `PatternWorkspaceValidation` passes 606 checks on SwiftShader; its capture/result artifact is linked in the
  native evidence HTML. This runner did not execute a Windows desktop session.
- Float32-to-PBR bridge progress: 95%. `VectorSurfaceMaterialBridge` compiles five channels from a Pattern document,
  records their Float32 dispatches and image-to-host readbacks in one caller-owned submission, and exposes maps only after the
  caller's completion serial retires. No queue wait is inserted. Equivalent documents preserve the map cache; changed channel
  programs reconfigure and regenerate it. Five pyramids upload as RGBA32F authoring textures to the existing PBR shader.
- The native Texture Workspace host now routes its 512x512 Pattern document through that bridge on the shared Vulkan
  submission. Retired maps replace the selected material's live-preview channels; the material view shows the result without
  persisting source changes. The Windows x64 host cross-links, and packaging verifies all 13 surface/vector SPIR-V entries;
  native Windows GPU execution/capture remains a release gate.
- `MaterialRasterizationValidation` passes 224 checks on SwiftShader using 512x512 Float32 authoring maps and 128x128 PBR
  evidence captures. It exercises a single-submit five-map transaction, rejects premature retrieval, verifies
  unchanged/changed document caching, uploads actual GPU output, and compares the PBR centre capture with an independent
  scattering reference. The integrated bridge tests separate fabric and gold-thread layers: changing fabric Roughness to
  0.61 leaves gold Roughness at 0.18 and gold Metalness at 1.0 in the Float32 maps; reducing fabric SurfaceRelief from
  0.8 to 0.35 changes its derived tangent normals and PBR capture while keeping the gold-thread normal response unchanged.
  The three-frame C++ Vulkan GIF and provenance were regenerated from this 512x512-map run and remain embedded in
  `Exhibits/TextureStudio/NativeSurfaceEvidence.html`.
- `MaterialAtlasValidation` confirms changed Float32 authoring maps invalidate and repaint the same PBR preview selection,
  and that stale preview readiness is hidden until the replacement submission retires.
- Native Pattern channel enable toggles and strength controls are active for selected shapes; multi-selection applies each
  edit to every selected shape, and the document history preserves channel toggle Undo/Redo. Strength accepts 0–1 for
  ordinary channels and 0–4 for Normal; stitch/weave Height and Normal strengths cannot be reduced to zero. `PatternWorkspaceValidation`
  passes 626 checks, including selected-shape roughness-strength and surface-relief edits that leave the unselected layer unchanged
  and reach compiled GPU contributions. `VectorSurfaceValidation` confirms a GPU roughness disable removes only the fabric contribution
  while preserving the independent golden-thread roughness response.
- Native layers now persist a `surface_structure` classification (`freeform`, `stitch`, or `weave`). Selecting stitch/weave
  in the Pattern inspector enables and locks both Height and Normal channel toggles; document validation rejects missing,
  disabled, or zero-strength required contributions. TOML round-trip and invalid-weave rejection are covered in the native
  workspace validation, which passes on SwiftShader. This is explicit layer metadata and enforcement; automatic stitch/weave
  geometry generation and Windows interactive capture remain outside this phase.
- Native TOML now maps the web textile keys directly: `stitch_role` values `holes`, `thread`, and `highlight` resolve to
  NeedleEntry, StitchThread, and UpperThreadPass; `weave_role = "yarn"` resolves to WeaveYarn. The Pattern inspector can
  author these classifications, and validation enforces their parent stitch/weave structure and required channels. TOML
  compatibility exercises all four mappings; full Pattern JSON geometry import is still open.
- `VectorSurfaceValidation` runs the weave-classified fabric through all seven actual RGBA32F Vulkan channel dispatches,
  rejects disabled Height before compilation, verifies a per-layer Roughness strength of 0.23, and scales fabric relief by
  0.5 in GPU readback while preserving separate gold-thread height and normal response. `PatternWorkspaceValidation` confirms the inspector edit reaches the compiled
  contribution program. Both SwiftShader CTests pass; native Windows interactive preview capture remains open.
- Native evidence gallery now includes a three-frame C++ Vulkan PBR GIF: baseline, actual GPU-authored Float32 fabric/gold
  maps, and revised fabric relief/roughness with the gold response isolated. The linked JSON records source, executable and frame hashes; the fixture is SwiftShader, not a
  Windows desktop session.
- Still open: execute and capture the interactive Pattern 1:1-to-material-preview workflow on the native Windows host,
  including a per-shape channel edit; convert web Pattern JSON geometry and construction into native vector instructions;
  calibrate SurfaceRelief against measured height units and profile performance.
  Stamp remains deferred. The persistent/scene-level Apply action stays disabled until integrated native GPU submission and
  capture pass.
  GIF display is 8-bit evidence only; numerical Float32 checks are authoritative. No pixel stream is used.
- Reproduce: install pinned UI dependencies via `python Automation/PrepareUI.py`; compile the surface shader with
  `node Automation/CompileVectorSurface.mjs <Slang-WASM-JS> <shader-directory>`; run
  `python Exhibits/TextureStudio/Code/VerifyVectorSurface.py --driver <Vulkan-driver> --shader <VectorSurfaceCompute.spv> --destination <scratch>`.

#### Session rebuild on `arena/db2f9953-frontier` (2026-10-20)

- Restored pinned UI deps + fonts in a fresh sandbox, configured with `-DFRONTIER_HOST_VERIFICATION=ON`,
  built all host targets (GCC 12.2, Debug) and ran the full CPU suite: **21/21 CTests passed** (~70 s).
- Repaired four stale validation harnesses (no production-code changes): font inventory 4/30 → 5/35
  (`ControlCentreValidation`), per-domain icon rasterization (`InterfaceValidation`, Texture/Fluid symbols are
  denied to the Editor domain by design), canvas hover before preview F/arrow keys plus a non-vacuous F check
  (`MaterialPreviewConfigurationValidation`), and the full `###`-suffixed dockspace window name plus a null guard
  (`DockingValidation`, previously a null-`ImGuiWindow` SEGV). Docking also passes under ASan/UBSan.
- New `Exhibits/index.html` hub embeds the recorded HTML/native captures, per-workspace port status vs the Alloy
  reference (`Experimental/TextureStudio`), and this rebuild log; served from `Exhibits/` for preview.
- Still blocked in sandbox: SwiftShader/Vulkan readbacks (no `slangc`/driver), Windows link/run, Alloy `npm`
  verification. Carried UI defects (Project Browser 1:1, zoom FPS, layer cards, toggles, dropdown reuse,
  slider/ramp/gradient editors, rotation math, drag latency) remain queued after this rebuild.


## Windows preview 39483329514 (2026-10-20, branch arena/db2f9953-frontier)
- Commits: ba9b105 (settle-gated Pattern Float32 bridge, preview clip rects, dark-slate default stroke) + 42c9e54 (CI trigger for this branch).
- CI run 39483329514: Windows + Publish jobs success; Release unsigned preview at frontier-windows-39483329514.
- Linux host verification on ba9b105: full ctest 21/21 green; VectorSelectionValidation rotation/resize 1431/1431 pass (CPU).
- Confirmed open shader gap: PreviewPbrSurface samples albedo only; height/normal have no bindings (next round).

## Windows preview round 2 (branch arena/db2f9953-frontier, commits ba9b105..2fd6ea4)
- ba9b105: settle-gated Pattern Float32 bridge (drag perf), preview clip rects, dark-slate default stroke.
- 6b13015: parked material drafts across selection, full-bleed cover-fit preview + title scrim, exposure card, auto-size layer cards.
- 2fd6ea4: draped cloth preview shape, six-seamless-preset Pattern gallery, redesigned close dialogs.
- Linux host verification: full ctest 22/22 green (incl. 638 pattern-preset checks, cloth fold assertions).

## Windows preview round 3 (branch arena/db2f9953-frontier, commit e8755ee)
- Gesture-gated Pattern bridge (no regen while a gesture/pen stroke is in flight).
- Single-axis orbit rewrite (yaw about world-up, sway-tested old-fail/new-pass).
- Styled tooltip cards everywhere in Material; Open Library dialog restyled.
- TOML-backed preset gallery (six shipped .vector.toml files, built-in fallback).
- Apply-to-material gating with persistent snapshot per material.
- Linux host verification: full ctest 22/22 green (669 preset checks).

## Windows preview round 4, batch 1 (branch arena/db2f9953-frontier, unreleased)
- Pattern gallery preset thumbnails: CPU-drawn ImDrawList vector previews replaying each preset's
  instructions with VectorCoverage.slang corner/rotation conventions; click-to-open + hover tooltip.
- Project Browser Details rail: full projectspace metadata (identity/template/format/version, content
  roots, level/catalogue/pin counts, startup level, workspace theme/accent/font/scale) + Open button.
- Editor activation: opening a project launches EditorWorkspace (proven WindowsUI pattern ported into
  WindowsFrontier) with a top bar (back, project name/stats, Control centre / Theme / Settings buttons
  driving the ControlCentre drawer), a Viewport summary tab, and return-to-browser teardown.
- Materials: procedural detail textures (Automation/GenerateSurfaceDetailTextures.py) wired into five
  presets — tour/practice golf-ball dimple normal+ORM (TextureMask 6, 4x repeat), three solar-module
  cell/busbar albedo+ORM+normal sets (TextureMask 7, 2x repeat, shared ORM/normal).
- Correction to the round-2 note: the material preview already renders through MaterialSurface with full
  texture bindings (normal/height included) whenever a material residence exists; PreviewPbrSurface is
  only the albedo-only fallback. Golf/solar relief was a data gap, not a shader gap — no shader port needed.
- Height remains pattern-authored (AuthoringMaps[2] via Apply-to-material); .material files have no height slot.
- Linux host verification: full ctest 22/22 green (Docking close-all updated for the Viewport tab;
  MaterialLibraryValidation now compiles all five texture-wired presets incl. PNG decode).
- Still open: rotation-axis mapping vs user expectation, asset-browser HTML 1:1 (HTML file not in repo),
  remaining surface-detail ports (orange-peel, scratches, weaves need shader/UBO targets), zoom-FPS check.

## Windows preview round 4, batch 2: orbit fix (branch arena/db2f9953-frontier, unreleased)
- User report: yaw spins fine, but vertical drags roll "as if on Z" and the pivot feels off-centre.
- Root cause: orbit composed yaw-after-pitch (Ry*Rx), so pitch increments rotated about the yawed local
  X axis (a view-axis roll at 90 yaw) and yaw leaned the object up-axis sideways by sin(yaw)*sin(pitch).
- Fix: LookAt-style orbit (Blender/three.js convention) — yaw about screen-vertical world-up through the
  pivot, pitch about the fixed screen-right axis. Pure-yaw and pure-pitch poses are bit-identical to
  before, so the approved feels are untouched; only combined yaw+pitch changes.
- Correction to the round-3 sway test: zero-ellipse turntable yaw is incompatible with a screen-vertical
  object up-axis (proven: fixed front height + vertical up forces no yaw). Replaced with three
  old-fail/new-pass assertions: up-axis never leans, pitched tips never roll, pitched yaw traces the
  standard screen ellipse; plus a pivot-exactness guard across all five shapes.
- Linux host verification: full ctest 22/22 green; old matrix fails the new lean/roll/ellipse assertions.

## Windows preview round 4, batch 3: Alloy preset parity (branch arena/db2f9953-frontier, unreleased)
- User rejection: the six native-invented presets (and their gallery) did not match the Alloy Studio
  HTML. "Alloy Studio" is the web TextureStudio itself (schema alloy.pattern.v1); source of truth is
  Experimental/TextureStudio/src (PatternEditor.jsx gallery, patternDocument.js catalog of 103 families).
- Converter upgrades (Automation/ConvertWebPattern.mjs): per-copy instructions stay lean when they fit
  the 128-instruction budget, else tile-wrapped copies concatenate into one tile-space contour range
  (identical union under the same fill rule); minimal adjacent same-paint merging rescues the 65-layer
  Graduated lattice; half-drop/single-axis tiling accepted as documented straight-XY equivalents (baked
  tile pixel-identical, neighbour arrangement differs); miter/bevel joins and butt/square caps substitute
  round; anisotropic strokes use the larger axis; non-sized flowers import. All 178 family x palette
  variants convert (was 143 OK / 35 fail); deviations attach to each conversion for the sidecar.
- GPU/host budget raise: cubic segments 4,000 -> 32,768 (128 KB -> 1 MB storage buffer; shader needs no
  change since segments are an indexed buffer), vector source 600 KB -> 4 MB (codec, gallery loader,
  workspace save/load, import validation). Instruction cap stays 128 (shader stack array). Catalog peak
  is 18,954 segments (weaves); 25,600 is the editing-API maximum, so the 32,768 boundary is pinned by a
  struct-level coverage-validation test instead of the join fixture.
- Batch export (Automation/ExportPatternLibrary.mjs): 103 families / 178 palette TOMLs (37 MB) +
  Library.toml sidecar (families, 8 collection groups, 4 palettes, per-palette files, 24 deviation
  notes) under EngineContent/PatternArchives/TextureStudio/starters/. Serialized floats round to 7
  decimals (float32-parse within 1 ulp, invisible). The six invented TOMLs are deleted; the C++ fallback
  builders remain only for a missing preset directory.
- Gallery ported 1:1 from PatternEditor.jsx: kicker, two collection shortcuts, collection dropdown,
  search (name/group/description/study), conditional print-palette dropdown, heritage note, result
  count, two-column cards (76px cover-fit previews, wrapped names, description tooltips, selected
  ring), 24/page paging, trailing notes. Decoding is lazy per page (96-entry FIFO cache). Two
  documented deviations: DM Sans has no arrow glyphs, so paging/shortcut arrows render as ASCII
  < >; "Generate a layout" generator section is deferred (self-contained follow-up, not presets).
- Card tessellation: paint-order layer walk resolving Over/boolean composition; cubics flatten,
  RDP-simplify (closed-loop farthest-point split) and ear-clip to triangles once per preset; strokes
  replay as single polylines; fills submit batched with off-thumb culling. Fixed two real bugs found
  by headless harnesses: RDP collapsed closed loops (degenerate chord), and 16-bit ImDrawIdx overflowed
  past 64K verts per window draw list — 32-bit indices are now a PUBLIC compile definition on
  FrontierImGui (tracked; the vendored imconfig.h is untouched because CI recovers imgui fresh).
- Validation: PatternLibraryPresetValidation is Library.toml-driven (pins 103/178, decodes and
  coverage-validates every file, 1954 checks); new PatternLibraryGalleryValidation pins collection
  counts (30/16/10/10/10/10/10/7) and search counts, tessellates all 178 variants into finite geometry
  (830K triangles); headless card-render harness draws all 103 cards with draw-list invariants intact.
- Linux host verification: full ctest 23/23 green; converter node --test 7/7; ASan/UBSan clean on
  decode, tessellation and card rendering.
- Still open: rotation-axis follow-up already shipped in batch 2; asset-browser HTML 1:1 (HTML file
  still not in repo); "Generate a layout" section; remaining surface-detail ports; zoom-FPS check.
