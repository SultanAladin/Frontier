## Current combined prototype update — 2026-10-20

Spring, XPBD, shell FEM and solid FEM are selectable in one application. New continuum implementations
were explicitly authorised after the donor audit; the earlier reuse blocker no longer applies.
Renderer regression fixes, matched c7 comparison and current validation are recorded in
[the repair report](../../../Exhibits/IlluminationConsolidation/RendererRepair.md).
87 tests, four-model stability and seven null scenarios pass. Actual Vulkan/browser evidence is retained.
Hardware acceptance and C++ conversion remain pending. Earlier sections below are historical baseline context.

# Illumination Consolidation

Status: **single browser prototype integrated and locally tested**. Hardware/Windows acceptance and C++ port remain open.
User authorised implementation, real IES files, XPBD additions, self-run comparisons and optional GitHub Actions.
Local tools sufficed; no Actions or remote push was performed.

[Application and launch instructions](../../../Experimental/IlluminationPrototype/README.md).
[Executed evidence](../../../Exhibits/IlluminationConsolidation/README.md).

## Delivered browser integration

| Mechanism | Implementation | Verification |
| --- | --- | --- |
| One app | `Experimental/IlluminationPrototype/` | Clean locked npm recovery; one entry point/server |
| ReSTIR GI | Existing temporal/spatial reuse, software multi-bounce paths | CPU ray tests and GPU command runs |
| Screen traces | Nearest-depth pyramid before GI/reflection world tracing | Odd extents, resize/resource tests; OFF/ON integration |
| Software BVH | One TLAS/BLAS implementation, including fracture and wheel damage | Source suite, explicit BVH run, deformed ray intersections |
| Actual-light grid | Shared conservative 32-bit masks; all-light fallback | 32-light overlap and sampled contributor checks |
| Emissive GI | Card capture/radiosity and emissive ray hits, separate from light masks | Retained renderer paths and command tests |
| Real IES | Bundled BEGA file, LM-63 decoder, uniform GPU LUT, local-file importer | Real-file, symmetry, multiplier, malformed-input tests |
| Tyres | Reduced-order radial XPBD carcass in suspension equilibrium | Constraint, volume, pressure, timestep and driving tests |
| Deformation | Shared raster/shadow/mirror/BVH wheel function | Exact lattice upload tests and shader/command validation |
| Glass/materials | Garden props, IOR-controlled Fresnel and two-interface transmission | Garden tests, shared material storage and GPU validation |
| Course/camera | Revised proving grounds; persistent chase orbit | Course retained; orbit and chase regression tests |
| Comparison | In-app OFF/ON timing capture and JSON export | Unit test, actual Chromium toggle captures, CPU benchmark |

Current default: software BVH; screen-first is opt-in and SDF remains an alternative in the same app.
This is WebGPU/WGSL, not WebGL; no hardware RTX API is used.
The native CMake/Vulkan/Slang editor and its appearance-only Control Centre remain untouched.

## Source decisions

Rank means suitability for the selected import scope, not measured speed.
The original plan proposed b55 as the physical starting tree. Implementation instead uses c7's already-integrated
vehicle/app/BVH tree and ports b55's screen/garden features into it, avoiding a second BVH implementation.

| Source | Retained scope | Integration role |
| --- | --- | --- |
| c7egoist `21530081`, revision `21c553b` | App, HMI, physics/effects, multi-bounce software tracing | 🥇 Physical starting tree |
| Slate `b55d93c0`, revision `8a70049` | Nearest-depth machinery, garden/materials and tests | 🥇 Screen/material donor |
| Slate `d984ba1b`, revision `c552cb2` | Compact masks and independent screen-signal gate approach | Focused mechanism imports |
| streamlinkinbox `85b411ac`, revision `dfc92a0` | Revised course and no chase-yaw decay | Behaviour preservation |
| Slate `c6cba543`, revision `dd045df` | Original screen-first semantics | Reference |

Full revision pins and exact baseline test recovery remain in the exhibit.
No entire external repository was merged; prototype files were selectively recovered without changing branches.

## Integration repairs

- Assigned screen tracing a distinct bit: 8192 was already software BVH and 4096 was already smoke.
- Added a separate integer identity attachment; screen traces no longer consume the effects' glow channel.
- Shared screen textures with ray buffers in descriptor group 3, staying within four bind groups.
- Packed light records, spatial masks and candela LUT into a sub-64-KiB uniform table to retain storage-buffer limits.
- Removed b55's 16-light-per-cell truncation; the current 32-light table retains every mask bit.
- Screen misses conservatively restart world tracing at the origin rather than skipping unproven travel.
- Removed the inherited arbitrary emissive-hit MIS attenuation, extra NEE pi factor and duplicate light cosine.
- Prevented terminated Russian-roulette paths from receiving a cache tail.
- Preserved glow during screen tracing, refraction index consistency and deformed wheel ray bounds.
- Corrected frame-time display to use wall time instead of capped physics delta; long GPU timings are retained.
- Reproduced direct-canvas loss on Chromium/SwiftShader; automatic software-adapter readback avoids that route.
- Device loss now halts submission rather than continuing to display an apparently healthy loop.

## XPBD and photometry boundaries

The five supplied browser variants did not contain an XPBD carcass; their solvers explicitly described point contact.
The new implementation is a 32-particle **quasi-static radial XPBD reduction**, not a dynamic 3D/FEM tyre.
It has compliant rim tethers, circumferential constraints, linearized volume compliance and unilateral road contact.
Cached pressure/compression curves supply suspension reaction, volume feeds the gas law, and the radial lattice
supplies rendered/triangle-ray deformation. MF6.1 remains the sole tangential friction model.
The distance field and surface-card geometry remain coarse/rest-shape approximations.

IES support is genuine type-C LM-63 decoding with `TILT=NONE`, not renamed analytic beams.
One imported profile drives the pole lights; a 181×37 angular LUT can lose very narrow angular structure.
Multiple per-light profiles, Type A/B, tilt tables and commercial calibration are not implemented.
Original profile notices and source licence provenance are retained; redistribution review remains open.
No independent emissive-triangle clustering or importance-sampling grid is claimed.

## Verification gates and remaining work

| Gate | Status | Next action |
| --- | --- | --- |
| Unit and vehicle regressions | Passed locally | Keep the combined fixture as the regression entry point |
| New toggle combinations | Passed on Dawn null | Continue on real GPU drivers |
| Actual rendered pixels/browser controls | Executed on software GPU | Retain captures; inspect target hardware/Windows |
| Performance comparison mechanism | Implemented and exercised | Repeat long paired captures on the target GPU |
| Universal fastest preset | Not established | Same resolution, route, seed, power state; at least 3 repetitions |
| Estimator correctness | Partial repairs, not certified unbiased | Compare finite-path reference, PDFs, reconnection and energy |
| Visual/driving acceptance | Open | Night, mirrors/glass, tyre failures, fracture, stress and moving views |
| Full 3D dynamic tyre carcass | Not supplied or implemented | Separate solver/geometry/force-coupling expansion if required |
| Native C++ version | Deferred as requested | Port only after browser feature and acceptance decisions |

Do not call short SwiftShader timings a target-hardware winner. Null elapsed seconds are not rendering FPS.
The delivered prototype integrates the requested mechanisms, but these remaining production/accuracy gates are not waived.
