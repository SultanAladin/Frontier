# Vehicle HMI Editor — Full C++ Engine Port Plan

## Scope (user direction, 2026-10-22)

Port the **entire** HTML Vehicle HMI Editor to C++ — not only the virtualized-geometry feature:
the ReSTIR path-tracing pipeline, RTX (hardware ray tracing) + GTX (compute ray tracing) over one
BVH, the virtualized geometry (Nanite-class) feature, sun + sky + clouds, vehicle simulation,
weather, and the editor UI on top. The earlier geometry-only plan is superseded; its completed
K1a kernel work stands as the first delivered piece of Phase 2 below.

## What exists to port (measured, HTML reference)

| Layer | Files | Lines | Notes |
|---|---|---|---|
| `Source/*.js` | 132 | 25,490 | Engine + editor + simulation |
| `Shader/*.wgsl` | 80 | 8,891 | All GPU work; WGSL → Slang (repo rule) |
| `index.html` + styles | — | — | Editor chrome; C++ side uses ImGui foundation |

Behavioural reference = the running HTML editor (test suite 233/234; the one failure is the
sandbox-missing `jolt-physics` npm package, environmental only).

## Target architecture (C++, per CLAUDE.md: Vulkan primary, Slang shaders, GLFW windows)

| # | Division (Engine/) | HTML source it ports | GPU/API form |
|---|---|---|---|
| 0 | `Render/` platform | `DeviceExchange.js` (165), `ShaderInterchange.js` (163), `PreviewStartup.js` | GLFW window, Vulkan instance/device/queues, swapchain; **capability split: RTX** = `VK_KHR_acceleration_structure` + ray pipelines present, **GTX** = compute traversal fallback — one scene, both paths |
| 1 | Shader translation | All 80 `.wgsl` via `AssembleSource` include graph | `.slang` modules with the same include structure (`FrameDescriptor` etc.); each shader ports with its host code, never ahead of it |
| 2 | `Geometry/` kernel | `GeometryHierarchy.js` (1312), `GeometryHierarchyRuntime.js` (835), `VirtualizedGeometry.js` (606), `VisibilityResolve.js` (268), `GeometryClusters.js`, `ClusteredGeometry.js` | CPU build (weld/partition/QEM tiers — **K1a done**: `FacetTierBuild`, validation green) + GPU cull/Hi-Z/visibility-resolve compute in Slang |
| 3 | Scene construction | `FacetStructure.js` (568), `FacetCodec.js`, `LevelStructure.js` (460), `CourseStructure.js`, `BoundingStructure.js` | Facet sets, occupants, placements → kernel + BVH inputs |
| 4 | Ray tracing (GTX path) | `RayTracingSpace.js` (1085): BVH build (per-mesh BLAS, scene TLAS) | Compute traversal (the WGSL `RayTraversal.wgsl` 1104 lines is already a software traverser — it ports to Slang compute and runs on any GPU) |
| 5 | Ray tracing (RTX path) | Same BVH, hardware form | Vulkan acceleration structures built from the same scene records; ray-query in the same Slang shaders (`RayQuery` vs software walk behind one interface) |
| 6 | ReSTIR + denoise | `ReservoirRecords/Temporal/Spatial.wgsl`, `DirectRadiance.wgsl`, `ReflectionTraversal.wgsl`, `VarianceTemporal/Wavelet/Upsample.wgsl`, `ResamplingSequence.js`, `VarianceIntegrator.js` | ReSTIR DI reservoirs, temporal+spatial reuse, à-trous variance denoise — the frame's direct/indirect split of the HTML inspection modes 1–5 |
| 7 | Surface-cache GI | `SurfaceCardDepot.js` (384), `CardCapture/Projection/Direct/Radiosity/Final/Sampling.wgsl`, `ClusteredLights.wgsl`, `ShadowProjection.js` + `ShadowRaster.wgsl` | Texel-space cards, clustered light assignment, shadow maps |
| 8 | Distance fields | `SceneDistanceSpace.js` (263), `OccupantDistanceSpace.js` (253), `DistanceRecords/Traversal/Union/Transfer.wgsl`, `InspectionMarch.wgsl` | SDF BVH for inspection mode 6 and GTX glossy/secondary rays |
| 9 | Sun, sky, atmosphere | `CelestialSolver.js`, `CelestialAtlas.js`, `StarCatalogue.js`, `AtmosphereIntegrator.js`, `AtmosphereMediumLight.js` (221), `LightingSystem.js` (437), `Atmosphere*.wgsl`, `CelestialSky.wgsl`, `NightStars.wgsl` | Sun disc + sky LUTs, atmosphere scattering, stars/moon — inspection-visible exactly as the HTML sun/sky panels drive them |
| 10 | Clouds + weather | `CloudField.js`, `CloudSequence.js`, `CloudShadow.js`, `CloudLayerCard.js`, `CloudTrace/Present.wgsl`, fog, `Precipitation*.js`, `Snowfall*.js`, `Lightning*.js`, `WindSequence.js`, `SurfaceDeposit*.js` | Volumetric cloud trace + shadow, precipitation/snow/deposit, lightning |
| 11 | Vehicle + world sim | `VehicleSolver.js` (1143), `RigidBodyWorld.js` (976), `JoltWorld.js`, `Tyre*.js` (4 solvers), `Fracture*.js`, `Cabin*.js`, `TrainingSequence.js`, `EffectSequence.js` (494), `PlumeSpace.js` + plume shaders, `SparkSequence.js` | CPU simulation at HTML rates; fracture/plume/sparks feed the renderer as in HTML |
| 12 | Raster surface path | `SurfaceProjection.js` (151), `SurfaceRaster.wgsl`, `ForwardRaster.wgsl`, `VehicleVisual.wgsl`, `EffectRaster.wgsl`, `CabinOverlay.wgsl` | The surface pass the virtualized path and forward objects share |
| 13 | Editor UI | `EditorShell.js` (460), `InspectorPages.js` (1074), all `Native*Panel.js`, dashboards, `TransformGizmo.js`, `EditorTransforms.js`, `VehiclePicking.js`, overlays | Onto the existing C++ `FrontierInterface`/ImGui foundation (not a second UI stack); Display inspection modes 0–14 preserved |

## Execution order (each phase ends demonstrable, screen-first as early as possible)

| Phase | Content | Demonstrable result |
|---|---|---|
| **P0** | Division 0 platform + one Slang triangle | **Window with a triangle on screen** (Windows build) |
| **P1** | Kernel K1b (QEM tiers) + Division 3 minimal scene (ground + sphere) + Division 12 raster | Cluster-drawn sphere on screen; silhouette simplifies with distance |
| **P2** | Division 4 GTX path: BVH + compute primary-ray traversal | Ray-traced image of the same scene, no RT hardware needed |
| **P3** | Division 5 RTX path: hardware AS + ray query, same shaders | Same image via RTX; path auto-selected by capability, GTX fallback proven |
| **P4** | Division 9 sun/sky/atmosphere + Division 7 shadows | Lit scene, moving sun, sky — first recognisably "editor" frame |
| **P5** | Division 6 ReSTIR + denoise + Division 8 distance fields | The full GI frame: modes 1–6 equivalents |
| **P6** | Division 7 surface cards + Division 2 GPU streaming/debug views | Cards GI + cluster debug views 10–14 on screen |
| **P7** | Division 10 clouds/weather | Clouds, fog, precipitation over the scene |
| **P8** | Division 11 vehicle/world simulation | Drivable vehicle, fracture, tyres, plume |
| **P9** | Division 13 editor UI complete | Inspector panels, gizmo, picking — feature parity with the HTML editor |

## Rules of the port

- HTML is the behavioural reference; concepts and numbers port, code is newly written C++/Slang.
  Every phase's acceptance numbers come from the HTML tests/evidence, not from taste.
- C++ naming per `SKILL-Naming.md` (no Mesh/Node/Graph/Hierarchy/Parent/Family/Kernel/Pass…);
  the HTML identifiers never cross over.
- Sandbox limits stay honest: here = g++/CPU proofs; Windows/Vulkan runs are the user's machine.
  Blocked checks are reported blocked, never passing.
- No merge until the user says so; work stays on `arena/e9bd7db5-frontier`, additive new folders
  (`Engine/Geometry/`, `Engine/Render/`) to minimise collision with the other agent's branch.

## Frame architecture (added 2026-10-22, user direction — was missing)

A renderer of this class is not a linear sequence of passes; three structures own the frame:

| Structure | What it is here | Why |
|---|---|---|
| **Frame graph (render graph)** | Every GPU unit (cluster cull, Hi-Z, BVH traversal, ReSTIR reservoirs, cards, clouds, resolve, present…) declares its reads/writes on named resources; the graph topologically orders units, derives barriers/transient lifetimes, and culls units nothing consumes. Debug views (Display modes 0–14) are just alternate terminal units of the same graph. | Pass order, sync and dead-work elimination stop being hand-maintained in `SurfaceProjection`-style orchestration; the HTML host's manual ordering becomes graph declarations |
| **Multithreaded frame pipeline** | Three frame slots in flight: **simulation thread** (vehicle/world/kernel streaming decisions) → **record threads** (a worker pool recording graph units into secondary command buffers in parallel) → **render/submission thread** (graph execution, present). Per-frame data flows through ring buffers with explicit fences; no thread touches another slot's data. | CPU recording of a graph this wide cannot sit on one thread at 60 Hz; the HTML single-threaded frame is the prototype simplification, not the model |
| **Log + telemetry stream** | One lock-free event channel: every division emits structured records (level, division, frame, µs timings, counters) consumed by (a) the in-editor Debug/Telemetry inspector, (b) file journal. This is the panel being prototyped **first** (see below), because every later phase debugs through it. | The Project Browser crash hunt showed the cost of debugging blind: traces first, features second |

Division boundaries in the table above are the graph's unit groups and the telemetry channel names,
so the inspector's log filters and frame-graph view light up division by division as phases land.

## Debug/Telemetry inspector (first panel, HTML prototype before C++)

One panel, three instruments, styled after the SourceImages/Telemetry references (dark cards,
accent ticks, big thin numerals, dot-matrix charts, heat grids) under the top-notch Control Centre
(font + theme plumbing) inherited from the texturing workspace UI:

- **Log stream** — live structured records: level filters, division filters, text search, pause,
  frame tagging; the stream every division writes to from day one.
- **Telemetry** — frame time history, per-unit GPU timings drawn as the frame graph executed
  (node bars in execution order), CPU thread utilisation (simulation / record pool / submission),
  counters (draws, clusters culled/resident, rays, reservoir reuse).
- **Infographics** — dot-matrix activity grids per division, heat grid of unit timings, stacked
  frame-budget bars: readable at a glance across the room, per the reference images.

Prototype: `Experimental/DebugInspectorPrototype/` (HTML, simulated live data). The C++ version
follows onto the FrontierInterface/ImGui foundation once the design is approved; the panel is the
debugging surface for the whole port, so it lands before the renderer phases it will debug.

## Already delivered against this plan

- Phase 2 kernel step K1a: `Engine/Geometry/FacetTierBuild.{h,cpp}` + `FacetTierValidation`
  (weld/partition/validator; sphere 5120→63 clusters, heightfield 8192→87 clusters, invariants
  green; `Exhibits/VirtualizedGeometry/README.md`). Next: K1b QEM tier construction, in parallel
  with P0 platform code (window + triangle) so the screen milestone is not blocked by the kernel.
