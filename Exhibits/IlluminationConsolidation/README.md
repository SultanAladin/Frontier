# Illumination Consolidation Verification

Single combined browser prototype implemented and tested.
[Application / launch / controls](../../Experimental/IlluminationPrototype/README.md).
[Integration plan](../../Documentation/Plans/Ongoing/IlluminationConsolidation.md).
Executed on 2026-10-20, Linux x86_64 editing host, Node v22.22.3, Dawn `webgpu@0.6.2` null backend.
No native product binary was built, and no GitHub Actions were run.

[Five-source tracing comparison and actual-light controls](RayTracingComparison.md).

## Combined application results

Final combined verification: **88 Node tests**, four-model vehicle stability, and seven Dawn-null scenarios
(178 frames total), all passing with zero command errors/warnings.
See `Results/Combined/Summary.json` and the adjacent raw logs.
Null runs validate commands/resources, not rendered pixels.

Actual pixel execution was also performed:

- Dawn Vulkan/SwiftShader: SDF mode, 4 frames, 320×180 output, zero errors/warnings,
  152.8 seconds including setup. `Results/CombinedScene.png` and `Results/SoftwareVulkan.txt`.
- Dawn Vulkan/SwiftShader: software BVH, 4 frames, 64×64 output, zero errors/warnings,
  69.3 seconds including setup. `Results/SoftwareBvhScene.png` and `Results/SoftwareBvhVulkan.txt`.
  This tiny output is a shader-execution smoke test, not image-quality evidence.
- Chromium 153/SwiftShader: rendered scene, real switches, XPBD state transitions and actual IES file-picker import.
  `Results/Browser/BrowserValidation.json` has an empty errors list; screenshots and console are alongside it.
  `CombinedBrowser.png` and `CombinedControls.png` are successful captures, not mockups.

The software-adapter direct WebGPU canvas route lost its device in earlier attempts.
`Results/DirectCanvasFailure.txt` preserves that failure; automatic readback presentation resolves this tested route.
Neither that workaround nor software Vulkan establishes hardware-driver or Windows compatibility.

### Current renderer repair and four-model evidence

[Renderer regression investigation, fixes, measurements and limitations](RendererRepair.md).
Final `Results/RendererRepair/Browser/CombinedControls.png` shows the actual combined browser scene.
`Results/TyreModels/BrowserTyreModels.json` records all four models rendered with software BVH.
`Results/TyreModels/CpuTyreModels.json` and `TyreModels.svg` retain the refreshed seven-repetition CPU comparison.
Earlier `TyreTiming.json`, paired browser subtotals and startup captures are historical, not current speed claims.
The renderer report records current matched pass timings and CPU geometry work separately.

### Reproduce the combined checks

From repository root:

```sh
python Experimental/IlluminationPrototype/Tools/VerifyCombined.py --install
```

See the application README for browser validation, actual Vulkan rendering and interactive comparison commands.
The Windows source launcher is supplied but has not been exercised on Windows.

## Original five-source audit results

| Source | Pinned revision | Unit tests | Vehicle stability | Null frames | Explicit trace-selection run |
| --- | --- | --- | --- | --- | --- |
| d984ba1b-slate | c552cb2 | 46/46 | Passed | 12 | Screen GI/reflection + BVH |
| b55d93c0-slate | 8a70049 | 64/64 | Passed | 12 | Screen GI/reflection + BVH |
| c6cba543-slate | dd045df | Not bundled | Not bundled | 12 | Default original screen path |
| 85b411ac-frontier | dfc92a0 | 42/42 | Passed | 12 | Default |
| 21530081-frontier | 21c553b | 47/47 | Passed | 12 | RTX-labelled path + SDF mode |
| Cross-source claims | Working changes | 6/6 | Not applicable | — | Masks, overflow, gaps, bits |

All nine 12-frame runs report **zero errors and zero warnings** at 320×180, `quality=low`.
There are five default runs, two explicit screen/BVH runs, and two explicit c7 renderer-mode runs.
The c6 application has no bundled fixture; its source is unchanged and uses b55's DOM/Dawn test fixture.
Null timing is not rendering performance: commands are validated but pixels and physics realism are not established.
The branches' unit suites mix behavioural assertions and source-pattern checks; counts are not quality rankings.

## Important confirmed findings

- The browser apps explicitly use point-contact tyres instead of XPBD carcasses.
- d984 and b55 implement spatial culling for actual light records, not emissive surfaces.
- Emissive radiance is captured into surface cards and added to reflected direct/indirect lighting.
- b55's 16-light cell cap loses entries under a 32-co-located-light stress case; d984's 32-bit mask retains all 32.
- The two Frontier variants lack nearest-depth screen-trace source files; inspected direct-light loops scan lights.
- The c7 RTX-labelled implementation traverses a software BVH in WGSL, not a native hardware RTX ray-tracing API.
- Feature bit 8192 collides between b55 screen traversal and c7 ray tracing.
- Analytic “IES” beam profiles do not establish support for importing real photometric `.ies` files.

## Reproduce

Prerequisites: git, Python 3, Node 22/npm, GitHub access, npm registry access, Linux Dawn-compatible Node environment.
From repository root:

```sh
python Exhibits/IlluminationConsolidation/Code/VerifySources.py --fetch
node --test Exhibits/IlluminationConsolidation/Code/SourceClaims.test.mjs
```

Omit `--fetch` when the pinned git objects already exist. The script reads exact commits without switching branches,
extracts only exhibit files into ignored `_ClaudeScratch/tmp/`, restores locked npm dependencies, and executes the
original unit suites, stability programs and headless fixtures. `TraceSelection.mjs` explicitly selects the relevant
settings and asserts twelve ready frames; d984 does not parse the same screen-toggle URL as b55.

`Code/Sources.json` retains full revisions/repositories/paths. `Results/Summary.json` records the executed matrix.
Per-source `*-tests.txt`, `*-stability.txt`, `*-gpu.txt`, `*-screen.txt`, `*-rtx.txt` and `*-gtx.txt` retain raw output.
`Results/SourceClaims.txt` retains the six cross-source assertions. Install transcripts are retained separately.
The fixture's elapsed seconds are setup/null-validation time, not FPS or a GPU benchmark.

## Scope and limitations

The integrated app is isolated under `Experimental/IlluminationPrototype/`; the native C++ renderer/UI is unchanged.
It uses WebGPU/WGSL, not WebGL, and software BVH traversal, not hardware RTX.
Spring, reduced XPBD, new shell FEM and new solid FEM remain selectable. FEM is coarse linear/quasi-static,
not a calibrated nonlinear dynamic tyre. See the repair report and app README for limitations.
Real IES support currently covers Type C, `TILT=NONE`, one shared imported profile; see its retained asset notice.
Emissive GI uses cards/radiosity and ray hits, not an emissive-triangle clustering grid.
Finite paths, caches, screen signals and clamps remain approximations; the renderer is not certified unbiased.

Target-hardware performance, Windows runtime, subjective image/driving acceptance and the later C++ port remain open.
No GitHub Actions were needed or executed. No native product binary was built for this phase.
