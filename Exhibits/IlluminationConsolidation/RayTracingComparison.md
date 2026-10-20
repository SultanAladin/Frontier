# Five-source tracing comparison and actual lights

Checked pinned GitHub source trees and host/GI/reflection shaders on 2026-10-20.
Revisions and repositories: [Sources.json](Code/Sources.json).
This is a capability comparison, not a five-way hardware performance ranking.

| Source branch | World-space GI rays | Screen-first GI/reflection | Triangle software BVH |
| --- | --- | --- | --- |
| Slate d984ba1b | SDF tracing + reservoir reuse | Available, default OFF | Optional reflections via TriangleHierarchyTraversal |
| Slate b55d93c0 | SDF tracing + reservoir reuse | Available, opt-in | Optional reflections via RayTraversal |
| Slate c6cba543 | SDF tracing + reservoir reuse | Default ON | Not present in supplied browser app |
| streamlinkinbox 85b411ac | SDF tracing + reservoir reuse | No nearest-depth screen tracer | Not present in supplied browser app |
| c7egoist 21530081 | SDF or BVH, multibounce controls | No nearest-depth screen tracer | GI, shadows and reflections |

**Five trace GI rays; three support triangle BVH; none uses a hardware RTX API.**
SDF ray marching is a ray-tracing technique, not triangle traversal.
Screen-space tracing cannot see off-screen geometry and needs world-space continuation.
Do not mistake differently named `TriangleHierarchyTraversal` for absence of triangle ray tracing.

## What “best” means here

- Broadest triangle-BVH feature foundation: c7. This is not a measured universal speed/quality win.
- Actual-light culling: d984's 32-bit masks retain all 32 lights, unlike b55's overflowing 16-entry lists.
  Integrated masks cover direct shading, card lighting and BVH hit lighting.
- Screen-first/depth pyramid: Slate provides the missing machinery; use opt-in until target-GPU
  timings and artifact review justify enabling it. SDF is still useful as a lower-detail alternative.
- Emissive GI: card emission/radiosity complements point/spot/IES direct lights; it does not replace them.
- Proposed combination is implemented in one app, not multiple renderers stacked on every ray:
  c7 world traversal + Slate optional screen-first + conservative actual-light masks + card radiance/emission.
  Alternative donor reflection-only BVHs need not execute alongside the general BVH.

## Current normal/default mode

Without a renderer URL override: software BVH, ray-traced shadows/reflections, GI with temporal/spatial
reservoir reuse, visibility checks, radiance refresh, card radiosity, emissive GI, actual-light clustering,
and real IES enabled. Screen-first is intentionally OFF. SDF remains selectable. Sampling is finite;
quality settings, clamps, cache approximations and limited bounces mean this is not an unbiased reference.
“Normal” does not mean every optional algorithm runs at once. The SDF alternative does not provide all
triangle-BVH accuracy/visibility behaviour; it uses a different representation and shadow path.

Not proven optimal: repaired GI/reflections remain costlier than the smaller c7 scene, CPU wheel refits
are measurable, and the fixed 32-light limit/grid is not a scalable many-thousand-light solution.
Hardware benchmarking, GPU refit, cache/update budgets and image-error comparisons remain worthwhile.
See [matched renderer measurements and limitations](RendererRepair.md).

## Actual point / spot light demonstration

Hard-refresh port 8080 → Render settings → **Actual point / spot lights** → **Show lights at night**.
Adds two coloured point lights and two coloured spotlights to the real 32-record GPU light table,
sets emission to zero and dims sun/sky. No emissive geometry is used for these four lights.
They are fixed in world space near the car's location when enabled; toggle off/on to place them again.
The scene's existing poles, headlights and roof lights are also actual light records.

Toggle **Cull actual lights** below: ON rejects noncontributing lights using the spatial mask;
OFF evaluates all records. Correct culling should preserve the appearance, not create a new lighting effect.
Use paired timings rather than judging culling by brightness. Outside-grid lookup safely checks all lights.
The grid now covers X −160…180, including initial car X −100; previously that location used the fallback.
Four demonstration slots are reserved while enabled so pole lights cannot displace them.

### Executed validation

- Updated combined suite: 88 tests, four-model stability and seven Dawn-null scenarios passed.
- Tests verify real point/spot types, world-space anchoring, disable/re-enable behaviour, spatial masks,
  and conservative coverage including the extended grid. No threshold was relaxed.
- Browser validation source: `Experimental/IlluminationPrototype/Tools/BrowserActualLights.mjs`.
  Run with `BROWSER_VALIDATION=BrowserActualLights.mjs node BrowserEnvironment.mjs` from app Tools.
  Browser run passed: four added records, emission zero, actual button/culling-switch interaction,
  rendered frames, and zero browser errors. Inspected screenshot shows warm/cool light pools. Evidence: [ActualLights.json](Results/ActualLights/ActualLights.json) and
  [actual rendered screenshot](Results/ActualLights/ActualLights.png).
- No new five-source performance winner is claimed. Earlier donor tests and repaired c7 comparison remain
  separately recorded; this change does not convert those into a five-way FPS result.
