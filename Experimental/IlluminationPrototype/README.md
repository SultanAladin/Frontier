# Combined Illumination Prototype

One browser app: screen-first ReSTIR GI, software triangle BVH and distance-field tracing, actual-light clustering,
real IES photometry, vehicle/HMI/effects and four tyre models: spring, XPBD, shell FEM and solid FEM.
**WebGPU/WGSL, not WebGL.** This is the browser integration prototype, not the future C++ renderer.

## Run

Windows: install Node.js 22+ and Python 3, then run `Launch.bat` and open the printed port manually:
`http://localhost:8080/`. The launcher restores the locked Jolt dependency if absent.
The Windows launcher source is supplied; Windows execution was not available for verification.

Other editing hosts, from this folder:

```sh
npm ci --ignore-scripts
python Tools/Serve.py
```

Use a current Chrome/Edge with WebGPU enabled and a secure context (localhost or the Arena HTTPS preview).
The server binds `0.0.0.0:8080`, uses relative resource URLs and sends no-store headers.
No external CDN, build bundler, native binary or C++ toolchain is required at runtime.
The browser loads `jolt-physics@1.1.0` from the restored local `node_modules/` directory.

## Controls and comparison toggles

Select **Render settings** in the outliner. Changes apply to the same running scene; no duplicate app is launched.

| Control | ON | OFF |
| --- | --- | --- |
| Screen-first tracing | Current nearest-depth pyramid before world tracing | World tracing only |
| Screen traces: GI / reflections | Independent use of screen hits | Bypass that signal's screen walk |
| Cull actual lights | Conservative 32-bit spatial masks | Evaluate all 32 light slots |
| Real IES photometry | Loaded LM-63 candela distribution on pole lights | Authored analytic beams |
| Renderer architecture | Software TLAS/BLAS multi-bounce triangle tracing | Scene distance field |

The first **Tyre model comparison** card offers **Spring + analytic**, **XPBD radial**, **3D shell FEM** and
**3D solid FEM** without removing any model. **Inspect front tyre** gives the close-up camera.
Vehicle → Tyres has the same selector and pressure controls. Albedo inspection is useful for comparing silhouettes
without exposure/GI noise. The four models intentionally have different stiffness responses; they are not calibrated
as equivalent physical tyres.

**Import .ies** reads an actual local LM-63 file. The bundled BEGA profile is loaded automatically.
**Garden glass IOR** drives the same Fresnel index used for entry/exit refraction.
**Paired timing comparison** captures OFF then ON for renderer toggles, or your chosen tyre model A then B, with 60 warm-up and 180 CPU samples per side, and exports JSON.
Do not interact with the camera during capture. GPU results are named-workload subtotals, not whole-frame FPS.
For the tyre comparison the vehicle is respawned to the same pose per side with zero scripted input; other comparisons
freeze the vehicle. Repeat runs and compare quality before selecting a preset.
**Finite path reference** disables screen/reuse/cache-tail/firefly approximations, retaining finite-bounce software paths.
It is a diagnostic setting, not a claim of an unbiased infinite-bounce reference renderer.

Other controls remain: WASD/arrows drive, Space handbrake, R recovery, camera selection and tablet/Garage.
The inherited training drills, course barriers, cabin displays, mirrors, Jolt debris, fracture, tyre wear/fire/smoke,
sparks, marks, stress population and appearance controls remain in this app.
Chase-camera orbit is preserved instead of automatically decaying behind the vehicle.

## Reproducible URL settings

| Parameter | Meaning |
| --- | --- |
| `renderer=sdf` | Optional distance-field GI continuation |
| `renderer=bvh` | Default, matching c7: software triangle paths; legacy `rtx` alias accepted but no hardware RTX API is used |
| `screen=1` / `screen=0` | Enable / disable nearest-depth tracing; disabled by default for baseline comparison |
| `cluster=0` | Disable actual-light spatial culling |
| `ies=0` | Use analytic beam profiles instead of imported pole photometry |
| `tyres=spring\|xpbd\|shell\|solid` | Select a tyre model; XPBD remains the default |
| `tyres=point` | Legacy alias for spring/contact behaviour |
| `quality=low\|medium\|high` | Existing field/card/resolution budgets |
| `scale=0.25…2` | Render scale override |
| `bounces=1…4`, `reflbounces=1…4`, `candidates=1…4` | Software path budgets |
| `stress=256&stressmotion=1` | Seeded stress population with moving occupants |
| `capture=1` | Readback/2D presentation; selected automatically on SwiftShader |
| `capture=0` | Force direct WebGPU-canvas presentation, including on software adapters |

The readback route works around a Chromium/SwiftShader direct-canvas device-loss failure reproduced locally.
Device loss now stops submission rather than continuing to report fictitious healthy rendering.
Physics delta is capped for stability; displayed frame time uses uncapped wall time, not the physics cap.

## What the XPBD implementation is

`Source/TyreCarcassProjection.js` projects a 32-particle radial lattice with compliant rim tethers,
circumferential coupling, linearized gas-volume compliance and unilateral road constraints.
Constraint multipliers accumulate through each solve using timestep-scaled compliance.
The quasi-static response curves are shared/cached at 5 kPa pressure intervals and interpolated across 29 compressions.
The coupled suspension uses their normal reaction instead of the legacy spring law. MF6.1 alone supplies tangential
friction, avoiding a duplicate particle-friction force. The thermal solver uses enclosed volume in its gas law.

The same radial displacements are uploaded to raster/shadow/mirror rendering and software BVH triangle intersections.
This is **not** a dynamic 3D tetrahedral carcass, FEM rubber model or validated real-vehicle calibration.
The signed-distance and card geometry remain coarse/rest-shape approximations; they are not rebuilt from the lattice.
The existing footprint support and suspension remain reduced-order. Deformation does not establish full tyre realism.

## Shell and solid FEM additions

`Source/TyreContinuumElasticity.js` implements genuine finite-element elasticity, newly written after the donor audit:

| Model | Discretisation | Mechanics |
| --- | --- | --- |
| Shell FEM | 112 points, 192 surface triangles | CST plane-stress membrane terms + linearised dihedral bending |
| Solid FEM | 224 points, 576 tetrahedra | Constant-strain tetrahedra through a single 9 mm thickness layer |

Both use 16 circumferential samples × 7 cross-section stations, fixed beads, 3D displacement degrees of freedom,
and unilateral frictionless plane contact. The crown is stiffer than the sidewalls. Material parameters are illustrative:
7 MPa sidewalls, 4× crown modulus, Poisson ratio 0.35. They are **not calibrated rubber/cord data**.
An active-set contact solve uses diagonal-preconditioned conjugate gradients. Constant-strain energy patches,
rigid-motion null modes, contact constraints, symmetry and convergence are tested.

The reference is assumed already inflated. Pressure contributes approximate edge initial-stress stiffness and a
linearised cavity-volume gas term; there is no nonlinear follower-pressure inflation solve.
Shared quasi-static response tables have 15 compression stations (0–140 mm) and 20 kPa pressure stations;
both coordinates are interpolated continuously. First visits to new pressure stations may pause during construction.
The displayed residual is the weighted **table-node** solve residual, not a newly solved intermediate-pose KKT residual.
Interpolated contact patches and the 34 L reference cavity are modelling approximations.

Normal reaction participates in suspension equilibrium; volume feeds the thermal gas law. FEM axle equilibrium uses
symmetric simultaneous updates to avoid a left-first bias. Tangential force remains MF6.1, not a nodal friction solve.
There is no dynamic carcass inertia, nonlinear hyperelasticity, cord anisotropy, calibrated tyre validation or refinement
study establishing load-curve accuracy. The coarse, single-layer tetrahedral model can be artificially stiff.

All three solved displacement components reach raster/shadow/mirror vertices. BVH wheel instances are deformed
once on the CPU when shape changes and their existing BLAS topology is refitted to tight bounds. Rays read these
triangles directly rather than repeatedly evaluating deformation in every candidate intersection.
CPU/GPU geometry is checked within explicit floating-point tolerances, not claimed bit-identical.
The SDF and card geometry remain coarse/rest-shape representations.

## Renderer regression repair

The integrated version had introduced expensive per-ray tyre deformation and blanket-expanded wheel BVH bounds.
Those have been replaced with per-wheel geometry updates and tight bottom-up BLAS refits.
Default BVH and opt-in screen tracing now avoid silently comparing a different default architecture against c7.
Screen hits without valid radiance fall back to world tracing; diffuse history excludes direct specular peaks.
Timestamp collection no longer reports stale queries for passes that returned without recording work.
See [measured regression evidence](../../Exhibits/IlluminationConsolidation/RendererRepair.md).

## Photometry scope and provenance

- LM-63 type-C, `TILT=NONE`; candela multiplier and ballast factor, nonuniform angle interpolation,
  axial/quadrant/bilateral/full-azimuth coverage and rejection of unsupported/truncated input.
- GPU resampling: 181 polar samples × 37 azimuth samples, bilinear interpolation, absolute candela and inverse-square
  attenuation with the exhibit's finite-reach window. Angular detail narrower than this LUT can be lost.
- One imported distribution currently drives the pole lights. Automotive beams retain analytic profiles;
  this is not a per-light multi-profile photometric authoring tool. No Type A/B or tilt-table support is claimed.
- `Archive/Photometry/BEGA-50975.ies` is BEGA 50975.6K3 **preliminary** photometry, mirrored by three.js from IES Library.
  Original `Copyright LUMCat V` text is retained. See [photometry notice](Archive/Photometry/NOTICE.md).

Actual-light masks and emissive surface GI are separate. Emissive surfaces feed card capture/radiosity and ray hits;
no separate emissive-triangle sampling grid is claimed. The current hard light ceiling is 32, with no per-cell truncation.

## Verification

```sh
npm --prefix Tools ci --ignore-scripts
python Tools/VerifyCombined.py
```

This runs unit tests, the vehicle stability program and seven Dawn-null integration configurations, including all new
feature toggles, all four tyre models, software BVH, effects, plume/HMI, orbit and a 256-occupant stress population.
The vehicle stability program also runs separately for each tyre model.
Null validation does not execute pixels. For real Chromium/SwiftShader execution, with this server running:

```sh
cd Tools
npm run validate:browser
```

The browser fixture requires Chromium's platform libraries. On this minimal Linux host, the pinned
`@sparticuz/chromium` AL2023 support libraries and `LD_LIBRARY_PATH` were used; see the evidence reproduction notes.
`BENCHMARK=1` adds short smoke-test paired captures. Use the in-app longer defaults on the target GPU for decisions.

[Retained results and screenshots](../../Exhibits/IlluminationConsolidation/README.md).
No performance winner is inferred from unit-test counts or null-backend elapsed time.
Hybrid screen radiance, cache tail, firefly clamps and finite-bounce truncation remain approximations.
Hardware GPU, Windows browser and subjective driving/visual acceptance remain unverified.

## Source integration

| Source | Pinned revision | Imported scope |
| --- | --- | --- |
| c7egoist/Frontier `arena/21530081-frontier` | `21c553b` | App/vehicle/course foundation, software multi-bounce BVH |
| SultanAladin/Slate `arena/b55d93c0-slate` | `8a70049` | Screen-depth machinery, material garden and tests |
| SultanAladin/Slate `arena/d984ba1b-slate` | `c552cb2` | Compact mask approach and separate screen-signal gates |
| streamlinkinbox/Frontier `arena/85b411ac-frontier` | `dfc92a0` | Preserved course revision; restored persistent chase orbit |
| SultanAladin/Slate `arena/c6cba543-slate` | `dd045df` | Original screen-first tracing reference |

Full source pins remain in the verification exhibit. This implementation uses c7's already-integrated app as its
physical starting tree rather than duplicating and reimporting its BVH into b55. The planned b55 features are ported
into that single tree. Earlier inherited notes (`PLAN.md`, `SLATE_PORT.md`) are provenance/history, not current claims.
New integration adds actual-file photometry, reduced-order XPBD, shell/solid FEM, noncolliding feature bits, separate integer screen
identities preserving effect glow, shared light selection, two-interface showcase refraction and timing controls.
It also removes the inherited heuristic emissive-hit MIS attenuation, extra NEE pi factor and duplicate light cosine.
These repairs do not certify the remaining ReSTIR estimator as unbiased.

DM Sans/OFL and lucide/ISC notices remain with the imported assets/source. Third-party npm packages are locked,
restored locally and ignored, not vendored. Imported naming/formatting is retained; this is not a naming-rule audit.

## Actual lights, not emissive geometry

Render settings → Actual point / spot lights → Show lights at night adds two coloured points and two
spots near the vehicle, disables emission and dims sun/sky. Disable/re-enable to re-anchor them.
Cull actual lights is an acceleration toggle: ON/OFF should preserve the image, not change brightness.
The readout shows active records out of the current 32-light limit.
See [five-source capability comparison](../../Exhibits/IlluminationConsolidation/RayTracingComparison.md).

### Browser distribution and startup protection

The preview loads retained `Archive/Application.js`, a standalone bundled application including the
locked Jolt WASM implementation. It does not fetch the source import graph, node_modules or Runtime/.
`Archive/ApplicationBuild.json` records source and output hashes; `Application-Jolt-LICENSE.txt` retains
Jolt's licence. This 4.3 MiB browser distribution is intentionally retained for preview delivery.

`Tools/Serve.py` restores missing locked app/build-tool dependencies and rebuilds this distribution
before opening port 8080. Rebuild after source changes, or restart that server:

```sh
node Tools/BuildBrowser.mjs
```

The dependency-free loader reports application URL, HTTP status or unexpected MIME type if loading fails,
rather than leaving Starting indefinitely. The current initial caption identifies **preview r4**.
See [startup verification](../../Exhibits/IlluminationConsolidation/StartupRepair.md).

### Copy a slowdown report

Render settings → Rendering diagnostics → Copy diagnostic log. Capture after warm-up while slow,
then paste it into the conversation. If clipboard permission is denied, a selected-text dialog opens
for Ctrl+C / Cmd+C. Includes actual pixel dimensions, settings, CPU/GPU timings, counts and GPU errors.
See [diagnostic verification and interpretation](../../Exhibits/IlluminationConsolidation/DiagnosticLog.md).
