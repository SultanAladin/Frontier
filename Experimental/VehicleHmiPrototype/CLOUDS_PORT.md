# Cloud renderer v2 — independent rewrite

This replaces the native-derived cloud port. The old cloud raymarch, density kernels,
3D shadow cache, temporal reconstruction, interleaving, and fullscreen composition pass
are **not** used. `Docs/CLOUDS_NATIVE_AUDIT.md` describes the retired implementation's
reference, not the current renderer. There was no pre-cloud Git commit in this checkout;
cloud integrations were removed at file level while keeping the earlier environment work.

## Current pipeline

- Original seeded, periodic, three-octave value noise baked into one 64³ R8 texture
  once at startup (256 KiB). No native hash, noise rotations, erosion, or profile kernels.
- One compute dispatch integrates a world-Z sky layer and an independent local ellipsoid.
  Analytic slab/ellipsoid intersections bound the work. Overlapping media share optical
  integration in sorted intervals rather than incorrectly drawing one on top of the other.
- Default sky budget **12 samples**, local **16**, maximum 64 per medium. Interval splitting
  can add a few rounding samples when both media overlap. Empty intervals are skipped,
  scene depth clips each ray, and nearly opaque rays terminate early.
- Default target scale **35% per axis** (12.25% of screen pixels). Resolution remains
  adjustable from 25–100%. Two low-resolution RGBA16F targets: optical data + depth guide.
- Existing display presentation reconstructs clouds before flare and tonemapping.
  Bilinear sampling handles the open sky; depth-compatible four-tap reconstruction handles
  geometry boundaries. Cabin transmission prevents clouds covering opaque cabin parts and
  weights their contribution through glass. There is no additional fullscreen cloud pass.
- Disabled/empty media perform no cloud dispatch; presentation branches around cloud reads.
  Targets shrink to 1×1 while disabled. Resizing/enable transitions rebuild presentation bindings.
- Direct ground sunlight now reads a separate 128² sun-projected optical-depth map (see below).
  Flare visibility remains independent of clouds.

## Controls and migration

Existing inspector layout, coverage/height diagnostics, density/scale/scattering, cloud-type
presets, local placement/bounds, wind and resolution controls remain. Diagnostics now sample
this renderer's baked field, not the retired field. Types are approximate profile presets,
not a reproduction of native weather. Local density fades at an ellipsoid, not an AABB.

Retired history/interleaving, native light-tap and 3D cache controls are removed.
The new shadow controls operate the independent 2D field, not the retired renderer. Loading a saved pre-v2 environment preserves its artistic settings
but resets cloud resolution/step budgets to the new defaults. Other environment settings
are unaffected. New saves retain v2 quality choices.

## Deliberate limits

Lighting is a height/forward-scattering approximation, **not** self-shadowed multiple
scattering. Ground cloud shadows are supported. There is no cloud attenuation of lens flare or temporal
cloud history. Exposure meters the underlying scene, not cloud-composited radiance.
Low sample counts can band, especially inside close local volumes; increase samples and
resolution there. The field is periodic. Clouds fade with distance and stop at 40 km.
This is a simpler performance-conscious starting point, not visual parity with native C++.

## Validation (rewrite, not retired-port results)

- `npm test`: **77 tests pass**.
- Production compute + presentation helper Vulkan proof: **0 validation errors**;
  sky/local opacity, foreground depth clipping, 25/100% resizing, disabled bypass,
  opaque cabin blocking and partial glass transmission pass.
- Full application, 12 frames at 320×180: **0 errors, 0 warnings**. Exercises both media,
  disabled/re-enabled clouds, both inspector panels, 25/50/100% resolution and type edits.
- Browser proof: four rendered frames, all nine environment panels, finite sliders,
  cloud resolution bindings and both fog numeric/slider bindings pass. Screenshots inspected.
  This is a paused software-rendering proof, not a desktop FPS test.

### Earlier rewrite-only software-GPU cost (excludes new shadows)

`CloudPerformanceProof.mjs` uses a 640×360 all-sky fixture at coverage .7, a warmed Vulkan
software adapter and median GPU timestamps. Its presentation probe calls the **production
AddClouds helper** against white input. It excludes scene rendering, ACES, flare and the
cabin transmission-generation pass. Therefore these numbers are not full frame times.

| Target scale | Trace target | Cloud compute | Presentation, enabled | Presentation, disabled |
| --- | --- | ---: | ---: | ---: |
| 50% | 320×180 | 34.80 ms | 17.83 ms | 18.74 ms |
| 35% default | 224×126 | 21.63 ms | 27.20 ms | 19.01 ms |

Presentation timings are noisy enough that the 50% probe measured slightly below its
baseline; do not interpret that as a negative cloud cost. At the default scale the measured
increment (compute + presentation difference) was approximately **29.82 ms on software**.
The earlier compute-only run measured 27.20 ms at 50%; workload/timing context matters.
No 120 FPS or zero-cost claim is made. Actual desktop GPU testing is still required.

Reproduce from repository root:

```sh
npm test --prefix Experimental/VehicleHmiPrototype
VK_ICD_FILENAMES=/tmp/vk_swiftshader_icd.json LD_LIBRARY_PATH=/tmp BACKEND=vulkan \
  node Experimental/VehicleHmiPrototype/Tools/CloudGpuProof.mjs
VK_ICD_FILENAMES=/tmp/vk_swiftshader_icd.json LD_LIBRARY_PATH=/tmp BACKEND=vulkan RESOLUTION=35 \
  node Experimental/VehicleHmiPrototype/Tools/CloudPerformanceProof.mjs
```

The Vulkan commands require the sandbox's existing `webgpu` tool dependency and software
adapter. Browser proof uses its existing Playwright/Chromium tool dependencies.


## Ground shadows, redesigned thickness card and local-cloud quick start

### Shadows

`CloudShadow.js`, `CloudShadowProject.wgsl` and `CloudShadowSample.wgsl` implement a new
**128×128 2D sun-projected field** (128 KiB RGBA16F). This is not the retired 3D light cache.
Global and local optical depth are stored separately; local entry/exit heights let the
receiver approximate the remaining depth inside the volume and stop shadowing above it.
The sky layer similarly uses a remaining-height fraction for receivers inside the layer.
These interior fractions are approximations, not per-receiver light marches.

- Reuses the exact current v2 noise/density/intersections used by visible clouds.
- Six samples per enabled medium by default, adjustable 4–12. Global and local clouds both
  cast shadows, even when the other medium is disabled. No raymarch in the surface shader.
- Default strength .65; softness 35 m; camera-centred, texel-snapped 6 km field span.
  Shadow strength, softness, field span (1–16 km) and samples have renderer-bound controls.
- Static maps are reused. Wind-driven refresh is limited to 8 Hz. Edits, sun changes,
  re-enabling and map-centre changes refresh immediately. Receiver lookup uses the matching
  frozen sun/map parameters, preventing projection mismatch between updates.
- Shadows fade smoothly at field edges and near the horizon; there is **no world-Z=0 gate**.
  Below-zero local volumes can shadow receivers below them without darkening objects above.
- Only direct sunlight is attenuated: sky/ambient light and artificial lights are unchanged.
  This does not add cloud self-shadowing, indirect-light cloud occlusion, or flare attenuation.
  Low field resolution can soften/miss tiny local volumes; reduce field span for close work.

`CloudShadowGpuProof.mjs` executes production map generation and receiver lookup. Checks
pass for independent global/local shadows, slanted sun projection, above-volume receivers,
field boundaries, softness, negative altitudes and Z=0 continuity, static reuse, off/night
bypass. **Zero validation errors**. At strength 1 the fixture's local ground transmission
is .0412, above-cloud transmission 1; Z=-.1/0/+.1 global transmission is identical.
Software Vulkan map updates measured approximately **1.11–1.64 ms after the first update**
(first update 3.74 ms) in these fixtures. This excludes the surface receiver lookup and is
not desktop GPU cost or an FPS guarantee. Wind updates are amortized, not free.

### Layer thickness card

The old horizontal profile plot is replaced by an axonometric translucent layer stack,
a draggable top handle, large depth readout, separate base/effective-top readouts and an
explicit ceiling-limited badge. Pointer drag, arrow keys and the numeric/slider inputs all
edit the renderer's CloudThickness. The visual stack is vertically exaggerated for editing;
readouts use actual metres. The card uses normal document flow instead of the old absolute
positioned graph layout. Static editor drawing does not add work to the GPU scene renderer.

### Local clouds

There is one independent editable **ellipsoidal local cloud**, not billboards or a second
sky layer. Existing local rendering remains; added Puff / Bank / Wisp presets, automatic
placement/enabling and a shortcut from the global cloud inspector. Centre XYZ and half-size
XYZ remain editable. Local noise is now anchored to the volume centre, so repositioning
preserves its shape instead of cutting a different shape from world-space noise.

Select **Local Cloud → Puff / Bank / Wisp**, or **Place in front of camera**. Placement
follows the view with a minimum centre altitude of 5 m; downward-looking chase cameras no
longer lift the volume out of view. It may intersect the ground—raise Centre Z to float it.
Turning off sky clouds does not disable this local volume or its shadows.

Validation additions:
- **77 unit tests pass**, including quality persistence, actual/clipped layer readouts,
  local presets/placement and shadow update scheduling.
- Full-app 12-frame run: zero errors/warnings; both media and 25/50/100% targets exercised.
- Browser checks: all nine environment panels remain functional; new thickness-stack arrow
  edit, shadow strength, all local presets and placement bindings pass.
- `LocalCloudHooks.mjs`: real scene, local-only Puff placed through the same helper as the
  inspector. Cloud target maximum opacity .7302. Comparing actual production DisplayPresent
  with clouds enabled/disabled changes 5,049 colour channels at 320×180, max byte delta 98,
  proving the local volume reaches final presentation, not only an inspector diagnostic.
- Browser local-only volume readback: max opacity .7385 at 560×591 screen / 196×207 trace.

Run the shadow proof:

```sh
VK_ICD_FILENAMES=/tmp/vk_swiftshader_icd.json LD_LIBRARY_PATH=/tmp BACKEND=vulkan \
  node Experimental/VehicleHmiPrototype/Tools/CloudShadowGpuProof.mjs
```
