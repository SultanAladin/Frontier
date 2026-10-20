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
No GitHub Actions were authorised for that earlier native UI batch.

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
- Windows runtime/GPU checks remain pending; that earlier UI batch did not authorise Actions.

## Illumination consolidation — 2026-10-20

- One combined [browser prototype](../Experimental/IlluminationPrototype/README.md) is implemented, not five apps.
- [Active plan](Plans/Ongoing/IlluminationConsolidation.md) records c7 app/BVH foundation plus focused source ports.
- ReSTIR GI, software BVH, screen-first depth/SDF, actual-light masks, emissive GI, real IES import and reduced XPBD
  coexist with runtime comparison controls and timing export. This is WebGPU/WGSL, not WebGL or hardware RTX.
- [Executed evidence](../Exhibits/IlluminationConsolidation/RendererRepair.md): 87 combined tests, four-model
  vehicle stability and seven null scenarios (178 frames) pass. Actual Vulkan and Chromium captures pass.
- Matched software-GPU probes repair Direct/GI/reflection regressions by 7.42×/3.17×/3.45× respectively;
  these are pass timings, not frame-rate claims. GI/reflections remain costlier than the smaller donor scene.
- Earlier source audit: 199 upstream tests, four stability programs, nine null runs and six claim checks passed.
- Source gaps were addressed: real LM-63 import, reduced radial XPBD, light-mask overflow and feature-bit collision.
  Full dynamic 3D tyres and emissive-triangle clustering are not claimed.
- Remaining: hardware/Windows acceptance, visual/driving review, estimator accuracy audit and the later C++ port.
- User explicitly authorised GitHub Actions for this phase; local tests sufficed. No Actions or push performed.

### Tyre FEM reuse clarification

- User requests retaining XPBD and adding selectable **3D shell FEM**, preferably reusing existing FEM code.
- [Executed source audit](../Exhibits/IlluminationConsolidation/TyreContinuumAudit.md) confirms the four donors'
  “PB-FEM” README wording describes analytic vertex deformation plus scalar spring dynamics, not shell/solid FEM.
  Five pinned vehicle solvers and four wheel shaders were inspected directly; current remote heads match pins.
- User subsequently authorised new shell AND solid implementations, retaining spring and XPBD.
  All four are selectable and tested. Shell/solid are coarse linear quasi-static continuum models with
  inflation/contact, not calibrated nonlinear dynamic rubber. The historical reuse blocker is superseded.
- Symmetric FEM axle updates fix solid-model donut mirror stability without loosening its threshold.
- Moving-wheel CPU BLAS refits remain a measured cost: approximately 8–9 ms spring/XPBD, 17 ms shell/solid
  for four wheels on this host; GPU upload/rendering excluded. Hardware performance remains open.

### Actual lights and five-source tracing clarification

- [Capability comparison](../Exhibits/IlluminationConsolidation/RayTracingComparison.md): all five trace GI rays;
  three have software triangle BVHs (d984/b55 reflections, c7 broader GI/shadows/reflections).
- Added two point and two spot lights with a night/emission-off demonstration button and active-light readout.
  Extended cluster coverage to include the initial car position; outside-grid safe fallback remains.
- Latest combined run: 88 tests, four-model stability and seven null scenarios pass.
  Chromium actual-light rendering and real controls pass with zero errors; warm/cool pools inspected.
  This is not a five-way hardware speed ranking or an optimality claim.

### Preview startup failure repair

- [Startup evidence](../Exhibits/IlluminationConsolidation/StartupRepair.md): missing Jolt static import
  caused indefinite Starting overlay. Server now restores locked app dependencies before serving;
  dependency-free bootstrap displays module-load failures. Port 8080 restarted, module HTTP 200.
- Three Python tests and a real clean-directory npm restore passed. Chromium verifies deliberate missing-module
  error display, then successful rendering and light controls with zero errors. No hardware FPS claim.

- Startup follow-up: remote preview still reported Jolt node_modules 404 although direct server was 200.
  Browser now uses generated public Runtime/Jolt.js; server copies exact locked module/licence on startup.
  Four Python + four Jolt tests pass. Chromium renders with all node_modules requests blocked; deliberate
  public-module failure remains visible. Preview r3 loading caption distinguishes stale HTML; remote user
  confirmation remains pending. Arena network/session failures cannot be repaired within the app.

- Current startup delivery (r4) supersedes ignored Runtime routing: retained standalone Archive/Application.js
  bundles the app + locked Jolt WASM; esbuild build script, source/output hashes and Jolt licence retained.
  Server restores build tools and rebuilds before serving. Loader reports bundle resource/status/MIME.
  Four Python and four Jolt tests pass; remote confirmation remains pending.

### User hardware slowdown and diagnostic copying

- User reports original demos at 60 FPS versus combined BVH at 19 FPS. No optimality or matched-settings
  performance claim is justified; target-PC bottleneck remains unresolved pending its diagnostics.
- Added Render settings → Copy diagnostic log: settings, adapter, pixel sizes, CPU frame/refit timings,
  asynchronous GPU passes, scene counts and GPU errors; denied clipboard opens selectable JSON.
- [Evidence](../Exhibits/IlluminationConsolidation/DiagnosticLog.md): 90 Node tests pass; real Chromium UI
  verifies successful copy and denied-permission fallback, rendered frames and zero errors.

### Reproduction instructions for another agent

- Added [actual-light clustering and XPBD instructions](Instructions/CombinedLightsAndXpbd.md): file map,
  exact layouts/constraints, visible light demonstration, suspension/rendering coupling, regression traps,
  test commands, provenance and unresolved performance limitations.
