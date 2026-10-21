# Celestial precipitation — GPU behavior port (current)

This **replaces** the rejected cloud-height / 50 m plane shower and connected accumulation/coating renderer. Those are not alternative runtime modes anymore. The app no longer constructs `SurfaceDepositSequence` and no longer renders its manual grid/coating. `PrecipitationSurface.wgsl` now draws opaque, closed octahedral mesh grains, not a white height-field, coverage mask, decal or texture.

## Source and rollback scope

Reference: <https://sultanaladin.github.io/Frontier-/celestial/>. GitHub Pages reports `SultanAladin/Frontier-`, branch `arena/01a08c57-frontier`, `/docs`. The deployed source is commit **da3720b353dc0e4d2421f064f65cbfeb41cdb5f2**, `docs/celestial/index.html`. Its precipitation section (`PR_TYPES`, `PR_BUDGET`, `prStep`) was read directly, not inferred from screenshots. The exact inspected section is preserved in `ReferenceApps/Working/Precipitation/CelestialOriginal.txt`; ported constants and emission laws are in `CelestialReference.js`.

The original uses **CPU particles / Canvas2D**, not GPU precipitation. We port its behavior to WebGPU compute/raster and convert Y-up to Z-up, retaining the accepted scene/cloud/wind infrastructure. This does not import its whole sky renderer or erase accepted lightning/editor work. The local Git history has no separate precipitation commits: that work was uncommitted on top of `6b153d1`; resetting two commits would also discard unrelated accepted work. Only the precipitation implementation was replaced; no branch reset or history rewrite was performed.

## Controls / try it

Refresh the app, then **Effects → Particles → Precipitation · celestial GPU port**. The Particle Editor workspace toolbar's **Precipitation** button also opens this component after saving editor edits.

- Choose **Rain / Drizzle / Hail / Snow / Sleet**, then **Start precipitation**.
- **Budget:** Low 4,000 / Medium 12,000 / High 25,000, matching the reference. Default High; reserved moving allocation remains 32,768 slots.
- **Spawn from clouds:** on uses the current cloud footprint and the demo's camera-height cap; off emits a uniform camera-local volume up to 60 m above the camera. No waiting for flakes to fall from 1,500 m.
- Reference controls: Intensity, Density, Particle size, Wind drift, Restitution, Rest time, Accumulation, Motion streak, Opacity. Shared scene wind remains the only runtime wind clock/direction source.
- **Stop precipitation** stops births and drains existing particles through collision/bounce/rest. Settled meshes remain. **Clear precipitation & settled meshes** explicitly clears both pools.

The old plane-height, physical-cloud-altitude, initial prewarm, and accelerated coating-time controls have been removed from the inspector. Emission continuously fills the falling volume as the demo does, not just the initial frame.

## Behavior actually ported

- Spawn disc radius **45 m**, camera-forward bias **0.45 R**, uniform disc radius via `sqrt(random)`. Heights use `floor + random^0.8 × (top-floor)` with floor `max(ground, cameraZ−40)`. Cloud mode top is capped at camera+400 m; uniform mode at camera+60 m. Camera altitude fade follows the reference.
- Per-m² birth law: snow `rate × .9`, drizzle `rate × 2.2`, others `rate × .55`, multiplied by disc area and Density, capped at `budget × 1.5` attempts/s. The GPU uses bounded stochastic slot admission rather than the CPU array's exact accumulator/order.
- Type constants preserved: Rain **8.5 m/s / .25 drag**, Drizzle **3.2 / .6**, Hail **22 / .08**, Snow **1.4 / 1.2**, Sleet **5.5 / .4**. Snow flutter uses phase rate 2.2 and horizontal accelerations 1.4 / 1.2, as in the source.
- Vertical velocity relaxes toward terminal speed; horizontal velocity drags toward shared wind. GPU motion/contact substeps improve stability; results are not bit-identical to the reference's one Euler update.
- Reference cloud-footprint threshold margin **0.12**, cover cutoff **0.04**, and acceptance `min(1, .25 + cover × 1.2)` are applied to this renderer's current baked/advection field. We do not replace accepted clouds with a separate reference noise renderer. Local volumes use their actual local density. This intentionally uses a wider raining footprint than the visible dense core, as the demo does.
- Rendering: rain/drizzle motion streaks, round snow, shaded round hail/sleet, depth-tested in the existing weather compositor. Snow is no longer a six-spoke sprite. Resting liquid has a short ripple representation. These are analytic GPU billboards, not the reference's Canvas overlay.
- Hail restitution is **user Restitution × .6**; sleet uses **× .15**. Bounce only if restitution > .05, incoming speed > 1.5 m/s and bounce count < **3**. Horizontal speed is multiplied by .8 at impact. The particle remains in simulation while rebounding. The ground plane is extended to the course terrain/kerbs/ramp and the existing upper-surface vehicle receiver.

## Stop first, mesh second — no coating

State flow: **falling/rebounding → final contact → zero velocity / rest → mesh conversion → moving slot free**.

After final contact the reference rest interval (default **1.6 s**) runs. Only then can snow/hail/sleet append to the persistent GPU mesh pool. A flying or rebounding particle cannot append mesh geometry. Water retires after its rest effect instead of creating ice. Unlike the demo's fade/delete, stopped solid precipitation persists as actual **24-vertex, eight-triangle closed grains**. Their world/body-local positions and radii are retained independently of recycled moving slots.

The settled draw is **indirect**, driven by the GPU mesh count. Its fragment output is opaque; there is no blanket surface, thickness-to-alpha coverage or sampled snow texture. Vehicle grains store body-local contact coordinates and transform with the body. The static receiver is baked once from actual vehicle upper facets. Terrain/vehicle packed-volume records remain as collision/accounting metadata; they are **not rendered as a height field**.

This is a bounded batched mesh-grain representation, **not a boolean union / arbitrary mound remesher**. Adjacent grains can form clusters visually, but there is no claim of a watertight union, physically packed grains, avalanche/compaction/sliding, or arbitrary snow overhang simulation. Existing-volume receiver height is approximate; individual stopped grain meshes are not separately ray-tested by airborne grains. Grain geometry uses a low-poly approximation; its geometric integral is not calibrated to the quantized volume ledger.

## Budgets and limits

- Moving allocation: **32,768 × 64 B = 2 MiB**; active tier ≤25,000. GPU-only birth/motion/contact/rest/conversion; no CPU particle-position uploads or per-particle scene objects.
- Settled allocation: **65,536 × 32 B = 2 MiB**, plus 16 B indirect args. At most **1,572,864** settled vertices per draw when full. This is bounded, not claimed free or benchmarked on the user's desktop.
- Packed receiver map: 80 KiB; static vehicle heights: 16 KiB. Ground map is world-fixed **128 × 128 m** around first Start, vehicle map **8 × 8 m** at 12.5 cm cells. Outside-map contacts retain records rather than silently losing solid volume. The moving spawn disc follows the camera, but durable storage is not toroidally erased.
- At mesh capacity, stopped grains retain their moving slots and retry conversion. No automatic eviction of existing meshes. Clear is explicit.
- CPU updates a **384-byte** parameter record and polls **64-byte** counters at most once/s. GPU timestep ≤.1 s with up to 32 contact substeps per slot; append reservation attempts are bounded.
- No arbitrary object/roof collision, upside-down body receiver, detached-part collection, melting, wet-surface material, full splash-particle system, rainbow port, disk persistence, RT/GI/shadow-geometry injection or exact motion vectors for settled grains. Rain ripples are simplified. These are stated omissions, not silently labelled complete ports.
- No whole-app FPS claim. `Precipitation simulation`, `Snow mounds` (now opaque grains; historical metric name), and `Cloud precipitation` timers remain separate.

## Validation

`Tools/CelestialPrecipitationBrowserProof.mjs` executes the actual WebGPU compute and raster paths in Chromium/SwiftShader. It checks the unmodified default scene emits; then uses a low-cloud, no-wind, 1,024-slot hail fixture to inspect contacts quickly. It forces only the test birth-admission probability, never CPU particle positions.

Measured GPU fixture:

- **1,024 hail births**, **3,072 bounce events**, maximum **3** rebounds per record;
- hundreds of records sampled moving upward after contact;
- **0 settled meshes before the rest interval**;
- after draining: **1,024** opaque mesh grains, **0** live moving slots;
- terrain and vehicle conversion both observed; quantized solid volume conserved exactly;
- a subsequent rain batch adds **0** ice meshes;
- isolated real mesh raster against scene depth produces nonzero opaque pixels (millimetre-scale grains, not a screen coating);
- **0 page/console errors**.

Artifacts outside Git: `/home/user/celestial-gpu-precipitation-proof/`. The source port is not a claim of pixel-identical reference rendering or desktop GPU performance. Historical shower/plane tests/results are superseded; their compatibility entry points now run this proof.

```sh
cd Experimental/VehicleHmiPrototype
npm test
SIZE=320x180 HOOKS=Tools/PrecipitationHooks.mjs node Tools/HeadlessHarness.mjs . 25 '?capture=1&quality=low&stresspool=0&inspector=particles'
node Tools/CelestialPrecipitationBrowserProof.mjs
```
