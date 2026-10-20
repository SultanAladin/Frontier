# Instructions for another agent: reproduce actual-light clustering and XPBD

Use these instructions to reproduce the additions in another browser prototype. Treat the current
implementation as the reference, not as a claim of optimal performance or calibrated tyre physics.
Reference repository: `SultanAladin/Frontier`, branch `arena/8cbe5e29-frontier`.
Paths below are relative to `Experimental/IlluminationPrototype/` unless stated otherwise.
Read the destination repository's agent rules before editing. Inspect its architecture and port the
required interfaces; do not overwrite unrelated work or replace its entire vehicle/rendering stack.

## Required outcome

1. Keep one combined application and the existing spring model. Add selectable XPBD without removing it.
2. Cluster **actual point/spot/IES light records**, independently of emissive material GI.
3. Add a visible demonstration of two point lights and two spotlights, with emission disabled for isolation.
4. Match tyre shape across raster, shadows, mirrors and software-BVH intersections without evaluating
   deformation inside every ray/triangle candidate.
5. Test the behaviour and preserve results/screenshots. Do not declare hardware optimality from null tests.

## A. Actual-light clustering

### Reference files to inspect first

| File | Responsibility |
| --- | --- |
| `Source/LightingSystem.js` | Compact actual-light records, CPU spatial masks, photometry, four test lights |
| `Shader/ClusteredLights.wgsl` | Light-table layout, mask lookup, light arrival and IES evaluation |
| `Shader/DirectRadiance.wgsl` | Masked surface direct lighting and BVH visibility |
| `Shader/CardDirect.wgsl` | Masked card direct lighting, separate from captured emission |
| `Shader/RayTraversal.wgsl` | Masked direct lighting at secondary BVH hits |
| `Source/IlluminationHost.js` | Settings, per-frame light updates and uploads |
| `Source/InspectorPages.js` | Actual-light controls, night demonstration and count readout |

### Implement in this order

1. Use a maximum of 32 compact active light records. Each record is 16 floats / 64 bytes:
   position/type, direction/reach, colour/intensity, inner cosine/outer cosine/profile parameter/active.
   Pack active records before inactive ones, because consumers may stop at the first inactive record.
2. Use a 16×16×4 world-space grid, minimum `[-160,-80,-2]`, extent `[340,160,30]` in the reference scene.
   Adapt bounds to the destination course, including its initial vehicle position. Each cell stores one
   **u32 bit mask**, not a truncated list of indices. Bit 31 must survive signed JavaScript bitwise operations.
   Do not silently drop a 33rd light: retain an explicit ceiling or implement/test a larger representation.
3. Conservatively assign lights by sphere-versus-cell-AABB intersection using their finite reach.
   Zero masks before rebuilding. Upload integer mask bits, not numeric conversions to float values.
4. Keep CPU/GPU buffer layouts identical. Reference float offsets: lights 0–511; grid descriptors 512–519;
   1,024 u32 masks at 520; photometric metadata at 1544; photometric samples at 1548.
   Preserve vec4 alignment in WGSL; reference masks are `array<vec4u,256>`.
5. When culling is disabled or a shading point is outside the grid, return `0xffffffffu` and evaluate all
   active lights. Outside-grid fallback is correctness protection, not proof that culling is active there.
6. Select the mask once **per shading point**, outside the light loop. Apply it to direct surface lighting,
   card direct lighting, and lighting evaluated at secondary BVH hits. Preserve attenuation and visibility.
7. Do not put emissive triangles/materials into this actual-light table. Keep emission capture/radiosity
   and ray-hit emission as a separate contribution; avoid double-counting direct specular in diffuse GI history.
8. Preserve a `Cull actual lights` switch. ON/OFF should preserve illumination; culling is not a brightness
   effect. Use matched-view paired timing rather than a brighter image as evidence of effectiveness.

### Add the visible actual-light demonstration

- Add `DemonstrationLights` setting and a `Show lights at night` button.
- On enable, save the vehicle transform once. Place two point and two spot/flood records near it; keep
  them fixed in world space after the vehicle moves. Disable/re-enable clears and replaces the anchor.
- Reference offsets: forward 3 m for points / 8 m for spots; lateral ±3 m; height 3 m relative to the vehicle.
  Colours alternate warm `[1,.38,.12]` / cool `[.25,.5,1]`; intensity 120; reach 12 m; spots point down.
  These intensities are illustrative scene settings, not calibrated luminaires.
- Reserve four record slots for these lights in addition to up to four vehicle-light slots. Do not displace
  test lights with pole records. Existing poles/headlights/roof lights remain actual records too.
- For isolation set emission scale 0, solar illuminance 0, sky scale .03 and sun elevation −8°.
  Clearly disclose these changed settings; this button is a demonstration, not an unbiased benchmark preset.
- Show active-light count out of 32. Explain that lights have no emissive fixture geometry in this demonstration.

### Keep real IES distinct from analytic beams

Inspect `Source/PhotometricDistribution.js` and `Archive/Photometry/NOTICE.md` if reproducing photometry.
Keep the imported BEGA profile and its notices/licences. Supported import is LM-63 Type C, TILT=NONE,
resampled to 181×37 candela values. Reject unsupported input; do not call an analytic spotlight “real IES”.
One shared imported profile drives poles; automotive beams remain analytic. Do not claim per-light profile authoring.

## B. Reduced-order XPBD tyre addition

### Reference files to inspect first

| File | Responsibility |
| --- | --- |
| `Source/TyreCarcassProjection.js` | XPBD constraints, pressure-dependent response cache, reaction and volume |
| `Source/VehicleSolver.js` | Coupled suspension force balance, model selection, contact and force integration |
| `Source/TyreThermalSolver.js` | Thermal gas-volume coupling |
| `Source/VehicleVisualState.js` | Shared encoded wheel shape/model record |
| `Shader/VehicleVisual.wgsl` | Raster/shadow/mirror wheel deformation |
| `Source/WheelVertexDeformation.js` | CPU counterpart and cached rest projections |
| `Source/RayTracingSpace.js` | Independent wheel BLAS, shape-change detection and tight bottom-up refit |

### Implement in this order

1. Retain spring behaviour as an explicit selectable model; add XPBD as another selection. In the reference,
   `Vehicle.TyreModel = 'spring' | 'xpbd' | 'shell' | 'solid'` coexists with the legacy XPBD enable flag.
   Do not remove shell/solid if already present. Do not relabel spring deformation as XPBD or FEM.
2. Implement the reference **32-particle radial reduction**, not a pretend full 3D tyre:
   compliant rim attachments, circumferential coupling, linearised gas-volume compliance, unilateral road contact.
   Use accumulated constraint multipliers during each projection and timestep-scaled compliance:
   `deltaLambda = (-C - alphaTilde * lambda) / (sum(w * |gradient|^2) + alphaTilde)`.
3. Reference projection step is 1/120 s, nominal radius .50855 m, rim radius .3448 m, rest volume .034 m³.
   Copy reviewed reference equations and constants rather than guessing new equivalent stiffnesses.
4. Build quasi-static response tables over 29 compression stations from 0 to .14 m, with 80 projection iterations
   in the default solve. Cache/share tables at quantized **5 kPa pressure bins**, maximum 64 cached curves.
   XPBD pressure bins are quantized, not the separate FEM implementation's continuous pressure interpolation.
   Enforce a nondecreasing reaction envelope before using the curve in the coupled suspension solve.
5. Use **Pa internally**, kPa only in UI. Reject negative/nonfinite pressure or compression. Interpolate response
   across compression stations; expose reaction, enclosed volume and radial displacement consistently.
6. Replace only the selected model's normal force law in suspension equilibrium. Avoid a second independent
   spring force added on top of XPBD. Preserve shredded/no-contact behaviour and the existing damping model.
   Keep MF6.1 as the tangential/friction force source; do not add duplicate particle-friction forces.
7. Pass enclosed volume into the existing thermal/gas law and feed updated pressure back into the carcass.
   Test pressure changes, volume bounds, load support, settling, symmetry and model switching.
8. Encode the same wheel shape for raster/shadows/mirrors and BVH. Reference current record is 7,888 bytes;
   it includes later FEM data, so inspect the layout rather than assuming all bytes are XPBD-only.
9. For BVH, clone independent wheel BLAS geometry, cache unique rest vertices/normals/projections, deform each
   unique vertex once when shape changes, then tightly refit bounds bottom-up. Skip unchanged uploads.
   **Do not** deform per ray candidate or blanket-expand all wheel bounds by 25 cm; those caused a measured regression.
10. Preserve tyre-model selectors and front-wheel inspection, plus matched spring/XPBD A/B timing controls.

### State the limitations honestly

This XPBD addition is a cached **quasi-static radial lattice**, not a dynamic volumetric/3D FEM rubber/cord tyre.
Do not claim calibrated tyre realism or that render deformation improves physical accuracy.
SDF and card geometry remain coarse/rest-shape approximations; BVH and raster get the detailed shape.
Target-PC performance is unresolved: user reports original demos at 60 FPS versus combined BVH at 19 FPS.
The additional work is not free; collect diagnostics rather than declaring the integration optimal.

## C. Required verification and delivery

From repository root, after restoring the app and Tools lockfile dependencies:

```sh
node --test Experimental/IlluminationPrototype/Tools/ClusteredLighting.test.mjs Experimental/IlluminationPrototype/Tools/PhotometricDistribution.test.mjs Experimental/IlluminationPrototype/Tools/TyreCarcassProjection.test.mjs Experimental/IlluminationPrototype/Tools/WheelBlasRefit.test.mjs
python Experimental/IlluminationPrototype/Tools/VerifyCombined.py --install
python Experimental/IlluminationPrototype/Tools/Serve.py --port 8080
# Separate terminal, from the app Tools directory:
BROWSER_VALIDATION=BrowserActualLights.mjs node BrowserEnvironment.mjs
BROWSER_VALIDATION=BrowserTyreModels.mjs node BrowserEnvironment.mjs
```

- Test all 32 co-located lights including bit 31, conservative coverage, outside-grid fallback, active-record
  capacity, demonstration anchoring and disable/re-enable. Inspect actual coloured light pools with emission 0.
- Test XPBD constraint/force/volume behaviour, pressure response, original spring preservation and vehicle stability.
- Run real GPU/CPU wheel-deformation parity using `Tools/ValidateWheelDeformation.mjs` where Vulkan is available.
  State actual numeric tolerance; null-backend success alone does not establish matching geometry or image quality.
- Use `Tools/ProfileWheelRefit.mjs` and physics comparisons to report CPU costs separately from GPU pass times.
  Compare same camera, pixel sizes, scene, bounces and candidates on target hardware; do not infer hardware FPS
  from tiny SwiftShader probes or sum asynchronous named GPU passes into a whole-frame total.
- Keep the public browser bundle current: `node Tools/BuildBrowser.mjs` from the app directory, or restart
  Tools/Serve.py. Retain required Archive/Application.js, its source/output hash manifest and Jolt licence;
  do not rely on ignored node_modules/Runtime files being available in a remote preview.
- Expose Copy diagnostic log in Render settings. It includes settings, actual pixel sizes, CPU/GPU measurements,
  scene counts and GPU errors; clipboard denial must offer selectable text. No credentials/session URLs.
- Preserve test logs, screenshots, exact settings, limitations and remaining hardware checks under Exhibits/.
  Report blocked checks as blocked. Do not perform C++ conversion as part of this browser reproduction.

Further evidence: `Exhibits/IlluminationConsolidation/{README,RendererRepair,RayTracingComparison,DiagnosticLog}.md`.
