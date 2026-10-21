# Shared gas-grid addressing and particle-to-surface deposits

> **Historical foundation / superseded local shower.** The current application uses the celestial GPU precipitation port and opaque stopped mesh grains, not this grid/coating renderer. See [`GPU_PRECIPITATION.md`](GPU_PRECIPITATION.md). The CPU shower and its inspector described below are no longer constructed or run by the application. Manual grid/debug APIs and historical tests remain.

## Scope

This now includes **visible falling snow and terrain accumulation**, not just the manual patch. A bounded CPU snowfall model uses the settling pool, draws instanced flakes, and transfers their volume into shared surface chunks on terrain contact. Lightning/thunder and accepted clouds are retained. Rain/hail and generic Particle Editor emitter injection are not implemented.

The existing gas system uses GPU toroidal lattices that follow the car and disappear after the plume fades. Reusing those *buffers/lifecycle* would erase deposited snow. Instead:

- `SpatialGrid.js` factors the real gas world-cell/guard-band recentering code out of `PlumeSpace.RecentreWindow`.
- The gas path now calls that shared helper, with regression checks against its previous implementation.
- The deposit path uses the same floor-to-cell / positive-modulo convention. Its default 0.4 m edge is the existing low-grade gas fine-cell size (`PlumeGrades[0].CellExtent`).
- Gas pressure, velocity, temperature, transport shaders and GPU buffer layout are unchanged. There is no gas-velocity readback or claim that airborne particles already follow the fluid solver.
- Deposits live in separate sparse, world-fixed **surface** chunks. They are not toroidally aliased, faded or recentered with the gas window.

## Try it

In the running prototype: **Effects → Particles → Snowfall & accumulation**.

- **Start snowfall here** starts a world-anchored shower ahead and to the side of the camera; **Move snowfall here** reanchors it. Looking down steeply places it closer.
- **Stop snowfall** stops emission; existing flakes finish landing, and accumulated meshes remain. **Clear all snow** explicitly cancels owned airborne/pending flakes and clears accumulated meshes.
- Sliders control rate, fall speed, spawn height, shower radius and representative-flake buildup. Default: 100 flakes/s, 6 m height, 3 m radius, 2.4 m/s fall, 1.5 mm equivalent cell depth per flake. This is accelerated representative accumulation, not calibrated real precipitation.

- **Deposit test patch (2 cm)** manually transfers 81 representative particle deposits into a roughly 3.6 × 3.6 m terrain patch, ahead and to the side of the camera so the car does not hide it. The depth is an accelerated test deposit, not a physical single-flake volume.
- Click repeatedly: the retained volume increases in the same cells/chunks, without one object or mesh per deposited particle.
- **Hide settled mesh** changes visibility only; accumulated data remains.
- **Clear accumulated surface mesh** explicitly discards deposited volume and destroys chunk GPU buffers. It does not discard pending/unaccepted particle volume.
- **Retry retained particle deposits** attempts to settle those pending remainders after space becomes available.

Everything is RAM-backed. Moving away does not clear deposits; reloading does. No persistent-disk/save-file format is promised yet.

## Transfer and capacity rules

`SurfaceDepositGrid` stores packed deposit volume (m³) in Float64 cells, with separate surface IDs. `SettlingParticlePool` is a fixed-size handoff pool. Allocation produces an index + generation handle. Settling transfers only the volume accepted by the cell:

1. The snowfall model detects downward contact with course terrain plus the smoothed accumulated depth, using up to four substeps per update. It supplies the terrain receiving surface; other callers must resolve their own landing.
2. Deposit into the surface grid.
3. If all volume is accepted, clear the particle's record and recycle its slot; do not retain a per-flake mesh or history.
4. If the cell's depth limit or sparse chunk budget is reached, retain the unaccepted volume in that slot. Do not silently lose it or evict existing snow. Pending slots retain their receiving-surface ID.
5. Generation checks reject stale handles and double settlement, including after a slot is reused.

The backing pool allocation remains reserved for reuse: “released particle” means its data is cleared and its slot is free, not that a separate buffer allocation is destroyed for each flake. Existing Particle Editor/Flux GPU preview pools are **not** connected to this handoff yet; other scene emitters must use the handoff or explicitly retire their own particle only after accepted transfer.

Default budgets: 1,024 handoff slots; 128 chunks; 16 × 16 cells/chunk; 2 m maximum deposited depth per cell. No automatic eviction. CPU cell storage is at most about 256 KiB at these defaults, excluding maps/pool/other bookkeeping. Chunk vertices are bounded to 1,536 vertices / 42 KiB per chunk (about 5.25 MiB GPU vertex capacity if all 128 chunks reach the maximum). Snow uses one 32 KiB instanced sprite vertex buffer for at most 1,024 records, plus fixed-capacity CPU state. These are capacity calculations, not total application memory measurements.

When capacity prevents landing, the remaining volume stays in its particle slot at contact. Emission pauses while such remainders exist; retries are spaced by 0.5 simulation seconds. No automatic lifetime expiry deletes snow. Updates clamp elapsed time to 0.1 s (no catch-up spiral) and stop advancing while the tab is hidden. Wind coupling uses the existing atmospheric wind parameter, not a GPU gas-velocity readback.

## Mesh representation and work limits

One combined triangle-list mesh per chunk, not one mesh per flake and not repeated merging of an ever-growing world mesh. Shared corner-height sampling smooths the surface and matches seams across existing neighboring chunks; deposits dirty adjacent existing chunks too. Smoothed visual mesh volume is an approximation; the authoritative retained volume is the grid, not an integral of the display mesh. The preview has no closed/watertight underside/edge mesh requirement.

At most **two dirty chunks per rebuild tick**, no more than once per 0.2 s of simulation time. A large dirty backlog therefore takes multiple ticks; no universal update-latency guarantee. Clean frames do not rebuild or reupload chunk meshes. No per-particle scene objects are introduced. Clip-space bounds cull chunk draws without deleting data. GPU buffers are reused unless capacity needs to grow, and destroyed on explicit clear.

A simple forward-lit, depth-tested surface preview draws before temporal antialiasing and cloud composition, updating the visible depth buffer. It now uses the same jittered projection as the scene depth rather than an unjittered matrix; that mismatch could make thin deposits flicker. Per-vertex thickness gives soft coverage from 0–4 mm instead of opaque square cells at tiny amounts. Instanced six-spoke flakes use the existing after-cloud weather radiance/depth composite, shared safely with lightning. It is **not** inserted into the ray-tracing BVH, GI surface cache, mirror scene, shadow geometry or vehicle collision model. It has not received final snow materials or full artificial-light/weather lighting. Default demo receiving surface is the course terrain/kerbs/ramp; roofs, moving vehicles, overhang collision selection, ploughing, melting and physical snow interaction are not implemented.

The API reserves separate surface IDs and static height-function registration so overlapping terrain/roof layers need not merge incorrectly later. This is not yet an arbitrary moving-object receiver/transform system.

## Observability

The inspector reports retained volume, chunks, dirty backlog, recycled/pending slots, mesh draw/vertex counts and this-frame mesh rebuild CPU time. CSV records `deposit_chunks`, `deposit_volume_m3`, `deposit_pending_particles`, `deposit_recycled_particles`, `deposit_rebuild_ms`, `deposit_draws`; manual deposits/clear/failures are events. Snowfall adds `snow_falling`, `snow_landed`, `snow_retained_at_contact`, and `snow_cpu_ms`, plus start/stop/clear/settings events and setting snapshots. **Surface deposits** and **Snowflakes** are separate GPU pass metrics. Rebuild work is inside the existing `scene_uploads_weather` CPU aggregate, not an additional frame cost to add on top.

## Validation

- 149 prototype tests pass, including nine new snowfall movement/mass/conservation/capacity/stop-drain/cancel/landing-depth/projection checks, plus including shared gas recenter regression, negative coordinates, volume retention, slot recycling, stale handles, duplicate settlement, full-budget/partial acceptance, receiver isolation, world anchoring, mesh seams, bounds culling and throttled rebuilds.
- Dawn null backend: 35 active deposit frames, zero errors/warnings. Two test patches transfer 162 particle records into 2 mesh chunks; all particle slots are recycled. Shaders/commands validated, not desktop FPS measured.
- Real Chromium/WebGPU test clicks actual inspector buttons: 81 deposits produce 2 mesh chunks and zero active slots; a repeated patch doubles retained volume while remaining at 2 chunks / 2 meshes. Moving the gas window does not alter retained volume. Hide preserves data; clear destroys meshes. Zero page/console errors.
- Active snowfall Dawn null test: 120 frames, zero errors/warnings; 59 emitted and 59 landed, zero occupied slots, 0.01416 m³ retained in one chunk.
- Real Chromium/SwiftShader snowfall proof: 491 falling and 149 landed at the first snapshot, with 474 nonzero weather-texture pixels. After Stop/drain: 640 emitted = 640 landed, zero occupied slots, 0.1536 m³ retained, three chunks / meshes / draws. Clear-all and native Start/Stop buttons passed; zero page/console errors. CPU simulation is advanced explicitly for fast deterministic proof; this is not a real-time FPS benchmark or a quantified temporal-flicker test.
- Browser screenshot/results live outside Git: `/home/user/snowfall-proof/` (new) and `/home/user/surface-deposit-proof/` (historical foundation).

From `Experimental/VehicleHmiPrototype`:

```sh
npm test
SIZE=320x180 HOOKS=Tools/SurfaceDepositHooks.mjs node Tools/HeadlessHarness.mjs . 35 '?capture=1&quality=low&stresspool=0&inspector=particles'
SIZE=320x180 HOOKS=Tools/SnowfallHooks.mjs node Tools/HeadlessHarness.mjs . 120 '?capture=1&quality=low&stresspool=0&inspector=particles'
node Tools/SnowfallBrowserProof.mjs
```

The browser proof needs optional Playwright/Chromium tools and the preview server. Its software-GPU results must not be presented as NVIDIA desktop performance measurements.
