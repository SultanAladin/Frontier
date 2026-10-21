> **Current cloud additions:** independent 128² projected ground shadows for sky/local clouds,
> a redesigned interactive layer-stack thickness card, and local Puff/Bank/Wisp placement
> controls are implemented. Details and measured limits: [CLOUDS_PORT.md](CLOUDS_PORT.md).

> **Cloud implementation superseded:** clouds have been rewritten independently. Earlier
> native-port/cache/history/shadow descriptions and performance results below are historical.
> See [CLOUDS_PORT.md](CLOUDS_PORT.md) for the current pipeline, limits and validation.
> Atmosphere, celestial bodies, fog and the non-cloud flare implementation remain intact.

# Native environment port

## Implementation

The existing exhibit is extended in place. Source references are preserved under `Reference/NativeEnvironment/`, from `SultanAladin/Frontier-`'s C++ environment inspectors and LensFlareKernel.h.

- Source-derived Sun, Atmosphere, Stars, Moon, Lens Flare and Fog inspectors, with native cards, icon assets, interactive diagrams, numerical controls, instance selection and renderer bindings. Aerial fog has its own selection. Moon settings/catalogue/phase use the native fixed card geometry and fitted preview coordinates. Responsive layouts use DOM/canvas rather than ImGui; this is not a claim of pixel-identical rendering across browser fonts, widths and platforms.
- Actual GPU texture readbacks for atmosphere previews, including transmittance and multiple-scattering LUTs; preview exposure is independent of scene exposure. Readback observes row alignment and visibility/dirty gating.
- Physical medium integration: 256×64 transmittance, 32×32 multiple scattering and 192×108 sky integration; 8–64 integration samples. Unchanged medium LUTs are cached. Multiple scattering is an enhancement to the reference's single-scattering atmosphere, not a claim that the original renderer used this chain. Disabling baked-atmosphere caching recomputes the chain; it does not select a separate analytic per-pixel renderer.
- Native twilight controls, solar positioning, temperature/tint, disc shape, direct-light extinction, lunar handover, height fog and geometry-only aerial perspective.
- Original 8,920-record star catalogue in an unfilterable RGBA32F texture, with magnitude, size, rotation and twinkle controls. Sharp celestial sources remain outside the diffuse atmosphere LUT.
- Six registered 2K body textures (Luna, Ember, Glacier, Sulfur, Shroud, Shard), four independently configured moon instances, orientation/phase controls and four visual planets. Planet positions are low-precision visual ephemerides, not observatory-grade astronomy. Moon brightness is an artistic multiplier, not calibrated lux.
- Native LFRender equations in CPU preview/HDR export and WGSL presentation, including layered diffraction, aperture, polygonal ghosts, spacing, halo and chromatic tint. Export validates the cache key and regenerates stale preview data. PFM export contains linear floating-point RGB.
- Solar flare visibility is calculated once per frame in a 1×1 compute pass using nine depth/transmission taps, then read once per presentation pixel. The cabin overlay supplies a multiplicative transmission mask: glass attenuates, opaque cabin displays/mirrors block. This is overlay-aware occlusion, not general scene-wide transparent-material ray tracing.
- Sanitized environment persistence in `localStorage` (`Frontier.Environment.v1`), including moon instances, settings changes and diagram edits; blocked/unavailable storage fails safely.
- Shared frame record: 1,072 bytes / 268 floats, including 140 environment floats.

Native controls that have no native renderer binding, such as the separate imported Sun source-image bake targets and baked-star playback, remain explicitly disabled. They do not fake a successful bake or import.

## Validation performed

- `npm test --prefix Experimental/VehicleHmiPrototype`: **67 tests passed, zero failed**. Includes environment packing, medium extinction, lunar handover, original compiled C++ flare-kernel numerical references, persistence sanitization/roundtrip/storage denial, and stale-export regression.
- Expanded application null-backend run: **12 frames, zero errors and warnings**, exercising all seven inspector selections, day/night, fog, multiple scattering and multiple moons. Null validation alone is not image proof.
- Executed full application on Dawn Vulkan/SwiftShader: **2 frames, zero errors and warnings** (320×180 reduced workload, 44 seconds total). This includes the final celestial and flare visibility shaders.
- Executed numerical Vulkan LUT proof: finite nonnegative radiance, transmittance bounded by one, multi-scattering energy increase, medium dirty gating, switch behavior, dusk/night separation and disabled-atmosphere output. Zero validation errors. Mean sky radiance: single 0.409336, multiple 0.678094, dusk 0.060102, night 2.10e-8.
- Actual Chromium WebGPU browser: all seven inspectors opened; Moon catalogue selection and a 7.38-day phase change exercised; numeric sliders valid; one scene frame rendered; no page/GPU errors or failure panel. Inspected actual screenshots, including the textured half-Moon, body thumbnails and updated illumination/phase metrics. The browser proof intentionally pauses requestAnimationFrame after the first rendered frame to keep software-GPU UI interactions responsive.

These are correctness and visual-interaction checks on a software GPU, **not hardware FPS measurements**. No zero-FPS-loss, sub-millisecond cost, hardware performance, or pixel-perfect cross-platform parity guarantee is made.

## Reproduction

```sh
npm test --prefix Experimental/VehicleHmiPrototype
HOOKS=Experimental/VehicleHmiPrototype/Tools/EnvironmentHooks.mjs node Experimental/VehicleHmiPrototype/Tools/HeadlessHarness.mjs Experimental/VehicleHmiPrototype 12 '?capture=1&quality=low'
BACKEND=vulkan node Experimental/VehicleHmiPrototype/Tools/AtmosphereGpuProof.mjs
```

The GPU proof requires Dawn/webgpu and an installed Vulkan ICD; a SwiftShader setup may need `VK_ICD_FILENAMES` and `LD_LIBRARY_PATH`. `Tools/NativeBrowserProof.mjs` additionally requires its documented optional Playwright/Sparticuz installation and the exhibit server on port 8080. Its captured screenshots and machine-specific proof logs are kept outside the repository, not shipped as runtime assets.

## FPS / slider / isolated fog follow-up

Following the reported 60–90 FPS regression on a previously 120 FPS PC:

- Moved native LFRender out of the full-resolution presentation fragment shader into a cached RGBA16F optical image, half resolution on each axis and capped at 960 pixels per axis. At 1920×1080 this evaluates the expensive kernel at 960×540 rather than 1920×1080 (4× fewer evaluations on camera/optics changes). A stationary camera/light/optics key reuses it. Linear upsampling can soften the finest streaks; the kernel equations and live depth/glass visibility are preserved. Visibility is multiplied during presentation, not frozen in the shape cache.
- Added optional GPU timestamp categories for cabin transmission, flare visibility and flare optics, exposed through the existing hardware metrics. Cache hits do not issue an optics timing query.
- Branches now skip celestial work for rough reflections whose celestial multiplier is exactly zero. Disabled moons are rejected before acos. These preserve the intended image.
- Static native inspector diagrams/catalogue thumbnails no longer repaint every telemetry tick. Twinkle remains live. Setting edits and asynchronous image readiness still refresh previews.
- Slider values snap relative to their minimum without losing small-density precision. Numeric fields update valid edits immediately, do not turn temporarily empty text into zero, and normalize on blur.
- Fog UI explicitly reports disabled state and aerial start distance. The probe is explicitly an isolated medium diagnostic, not an enabled scene-camera render.

`Tools/FogGpuProof.mjs` executes the exact `FogMedium.wgsl` function now shared by final-image recombination. Thirteen isolated Vulkan cases matched independent CPU expectations within 2e-5 with zero errors. At camera/target height 2 m and distance 50 m, height densities .002/.02/.05 yielded transmissions .991286/.416790/.004212. Aerial start=100 m yielded exactly 1 at 50 m. At 1,000 m, density .001 and start=100 m yielded RGB transmission [.520742,.465334,.406570]. Height, spectral/Mie blend, disabled, and combined-medium cases also passed. This verifies the web renderer's medium, not an untested native C++ executable.

Final checks: 67 unit tests pass; 12-frame null validation and 2-frame executed Vulkan application validation report zero errors/warnings. Actual browser tests explicitly exercise both fog range and number fields, restoration, and temporary-empty numeric input, without page/GPU errors. Browser rendering is still deliberately paused after one frame; the isolated fog test supplies GPU numerical evidence, not a scene screenshot comparison.

The identified expensive paths are credible contributors, not a hardware-timed attribution of the user's entire regression. No hardware FPS recovery is asserted. At 120 FPS the frame budget is 8.33 ms; 90 FPS is 11.11 ms and 60 FPS is 16.67 ms. Different hardware need not suffer the same percentage change.

## Sky and local clouds

Sky/local clouds are now implemented with native-derived panels, live field controls, worker-cached density diagnostics and a shared **25–100% per-axis cloud resolution slider (50% default)**. The optimized path uses a noise texture, cached sunlight, separate wind-aware cloud history and depth-aware composition. Cloud shadows also attenuate direct sunlight and flare visibility. See [CLOUDS_PORT.md](CLOUDS_PORT.md) for architecture, source-parity limits and executed checks. The current full unit suite has **73 passing tests**.

Cloud regression follow-up: fixed the world-Z=0 shadow discontinuity; added valid-history interleaved tracing, empty-ray/cabin composition rejection and a fast sky reconstruction path without lowering the requested resolution. Current suite: 74 passing tests. See the FPS/ground-shadow repair section of `CLOUDS_PORT.md` for measured software-Vulkan comparisons, actual four-frame browser inspection and limits; hardware FPS recovery remains unmeasured.

## Celestial visibility rebuild (current)

Cloud shadows are **cloud-density-derived projected sunlight transmission**, not painted
screen-space decals. They use a finite 128² ground projection and approximate receivers
inside a medium; they are not volumetric self-shadowing or full per-surface light marches.
The detailed limitations remain in `CLOUDS_PORT.md`.

### Stars

The star shading was rewritten in `NightStars.wgsl`, retaining the actual 8,920-star catalogue
and its spatial acceleration. It now uses stable angular difference vectors, a pixel-filtered
core, compact bright-star halos, compressed magnitude response and a less aggressive horizon
fade. The catalogue is not replaced with random dots. Colour, rotation, magnitude filtering,
brightness, size and optional twinkle remain renderer-bound. Stars disappear in daylight;
clouds and opaque lunar discs occlude them. The subtle Milky Way remains an artistic band,
not a photographic galactic panorama.

Stars → **Preview clear night** sets solar elevation to −18°, stops automatic/day animation,
and disables global/local clouds (explicitly described by the button's note). It does not
reposition the camera or disable moonlight. Daylight visibility status is now shown instead
of leaving an apparently broken empty sky unexplained. Sun-off also enables night star
visibility. No new render passes or GPU resources were added for stars.

### Earth Moon and scene light

- Default body: **Luna (Earth's Moon)**; authored angular diameter **25°**, as requested.
  Catalogue selection also uses 25° for Luna. The natural-size 0.52° button remains available.
- A one-time celestial settings migration applies the new Moon/star defaults to old saves;
  later user adjustments are retained.
- Primary moonlight uses the existing shadowed directional-light and surface-card lighting
  paths when the Sun is below the horizon or disabled. It is not merely a glowing sky disc.
- Added **Illuminate scene** toggle and **Moonlight power** control. Power is independent
  of solar power; setting solar illuminance to zero no longer disables moonlight.
- Phase, brightness, enable state, horizon and atmospheric extinction still govern the
  contribution. New moon, hidden/disabled moon or a below-horizon moon cannot light the scene.
- The 25° artistic diameter does **not** multiply light power by its exaggerated area.
  Additional moon instances remain visual only. There is one dominant directional key,
  not simultaneous Sun + multiple independently shadowed moons. Cloud shadows still project
  sunlight, not a second moon-shadow field.
- Moon extinction is cached while direction/medium remain unchanged. No new shadow maps,
  lighting dispatches, or per-pixel lunar-light loops were added. Larger visible discs still
  shade more sky pixels; zero performance impact is not claimed or guaranteed.

### Executed checks

- **81 unit tests pass**, including Moon defaults, independent lunar power, phase/horizon
  gates, power scaling, cached extinction and one-time settings migration.
- `StarsGpuProof.mjs`: production shader on software Vulkan, 8,920 catalogue records,
  **879 HDR pixels above 0.2**, peak **7.0117**, daylight/disabled peak **0**, twinkle-off
  time invariance, zero validation errors (512×256 hemispherical test fixture).
- Full application clear-night/Moon test: four frames, **0 errors / 0 warnings**. Earth Moon
  at 25° and lunar scene lighting remain active with solar illuminance zero. Rendered capture
  inspected: resolved Moon texture and clearly visible star points.
- Browser checks: clear-night button, default 25° diameter and actual moonlight-power binding
  pass. These software-device checks are not a desktop FPS benchmark.

GPU proof command (repository root, existing sandbox Vulkan setup):

```sh
VK_ICD_FILENAMES=/tmp/vk_swiftshader_icd.json LD_LIBRARY_PATH=/tmp BACKEND=vulkan \
  node Experimental/VehicleHmiPrototype/Tools/StarsGpuProof.mjs
```

## Shared weather / optics upgrade (2026-10-21)

- Editor and native environment panels now call **one** `SliderControl` implementation: label/value capsule above a broad rounded track, native keyboard range input, quantised editable number, scaled units and binding refresh. Removed the unavailable Sun-bake tiles, star-bake placeholders and duplicate day-cycle switch. Existing atmosphere LUT previews remain renderer-bound.
- Shared `WindSequence` integrates displacement instead of multiplying edited speed by elapsed time. Gust, altitude shear/veer and turbulence affect global clouds, local clouds and finite fog; follow switches freeze independently. Shadow invalidation excludes animated displacement, retaining its 8 Hz schedule.
- Weather remains one low-resolution pass and two RGBA16F targets, using the existing baked 64³ R8 noise. Added edge erosion, 0–4 bounded sun-light taps (default 2), absorption/powder response, local box/ellipsoid bounds and puff/layer/bank formations. CPU density previews mirror the new static shapes.
- **Fog → Local volumetric fog** adds an independent box/ellipsoid, soft shell, bottom-heavy/noisy density, extinction, scattering albedo, absorption, anisotropy and sample budget. Its fog-only path does not activate cloud shadows. CPU selection uploads at most four relevant scene lights (vehicle beams included); fog scattering is bounded analytic lighting, **not** shadowed volumetric transport. Camera-inside and cloud/fog overlap use sorted entry/exit intervals. Scene depth and cabin/glass transmission are preserved.
- Stars retain the real catalogue; added multiband scintillation, multiscale galactic structure/dust, horizon extinction and a halo control. Sun adds stronger limb darkening and adjustable aureole. Cached atmosphere LUT gains a subtle anti-solar twilight belt; no replacement full-screen atmosphere marcher. Lens-flare ghosts now have wavelength-dependent radii in both GPU optics and CPU preview/export; existing cabin/depth visibility remains intact.
- Moon textures, phase shading, **25° default**, independent moonlight and multiple bodies are unchanged. **No hail** was added.

### Validation and limitations

`npm test`: **85 passed, 0 failed**. Null-backend full application: **3 frames, 0 errors / warnings**. Executed Vulkan full application: **3 frames, 0 errors / warnings**, 480×270 requested / 288×162 internal, **83.1 s total** on the sandbox software adapter; this is not desktop FPS. Production cloud/fog proof covers independent fog, both shapes, inside camera, overlap, real uploaded point-light contribution, foreground clipping, cabin masking and glass. Projected shadows and stars execute with **0 GPU errors**. Stars: 8,920 catalogue records, 774 HDR pixels above .2, peak 6.3398; day/off output zero and twinkle-off stable.

Real-browser proof visited clouds, local clouds, fog, Sun, atmosphere, flare, stars, Moon and generic render settings: every range uses the common component; label/value is above a ≥20 px track. Fractional range and numeric edits, placement, clear-night fog disabling and Moon bindings pass. Screenshots/logs are outside Git under `/home/user/weather-controls` and `/home/user/weather-*.log`.

An isolated 640×360 / 35%-axis trace (224×126) software-Vulkan run measured **82.44 ms** weather compute with two sun taps vs **69.66 ms** with zero taps, across separate short runs. Production `AddClouds` presentation probe medians were 39.52 / 36.31 ms enabled and 32.11 / 31.06 ms bypassed respectively; these probes exclude the scene, ACES, flare and cabin-mask generation. Results vary in this shared software environment and are **not a matched baseline or hardware benchmark**. The richer lighting has real cost; no zero-FPS-loss claim or 120 FPS guarantee is made. Controls permit reducing light taps/resolution/march budgets.

Uniform payload grew from 128 to **544 bytes** (+416 B). No additional cloud/fog texture target, full-screen composite, fog geometry or light cache was introduced. Work still needed for parity: cloud-to-flare attenuation, shadowed local-light fog, more than one editable local fog volume, matched-hardware timings and visual tuning against the reference at matched camera/exposure. The small full-app capture proves execution, not visual parity.

Repeatable proofs (from prototype directory; browser tool launched from repository root):

```sh
npm test
SIZE=320x180 HOOKS=Tools/WeatherHooks.mjs node Tools/HeadlessHarness.mjs . 3 '?capture=1&quality=low&stresspool=0'
LD_LIBRARY_PATH=/tmp VK_ICD_FILENAMES=/tmp/vk_swiftshader_icd.json node Tools/CloudGpuProof.mjs
LD_LIBRARY_PATH=/tmp VK_ICD_FILENAMES=/tmp/vk_swiftshader_icd.json node Tools/CloudShadowGpuProof.mjs
LD_LIBRARY_PATH=/tmp VK_ICD_FILENAMES=/tmp/vk_swiftshader_icd.json node Tools/StarsGpuProof.mjs
# From repo root, with preview server on 8080:
PROOF_FRAMES=1 OUT_DIR=/home/user/weather-controls node Experimental/VehicleHmiPrototype/Tools/WeatherBrowserProof.mjs
```

### Thick-layer sampling correction (2026-10-21)

User screenshots exposed a diagonal hatch at 6,000 m layer thickness, absent around 595 m. The primary march stretched the same 12 samples across either depth, while its `(x*3+y*5)&7` phase repeated diagonally every eight pixels. This was undersampling made visibly structured by the jitter, not cloud detail. Global base samples now scale with **effective, ceiling-clipped depth / 1,200 m**, never below the selected base and never above **96**. At base 12 this means 12 samples for 595/1,200 m and 60 for 6,000 m. The UI labels the base count and reports the effective count. Per-step avalanche-hashed offsets replace the eight-position tiled pattern; offsets do not change with frame time, avoiding added temporal shimmer. Thin layers keep their original sample count. Thick layers incur additional bounded compute; this is a quality correction, not a free performance improvement. Extreme low-budget/horizon views can still undersample.

Production Vulkan regression (64×32 full-resolution isolated probe, base 1,163 m, thickness 6,000 m, coverage .55): mean absolute transmittance error versus a 96-sample reference was **.00135524** at adaptive 60 samples versus **.01080879** at fixed 12 samples (about 8× lower). Also verifies 595 m retains 12 samples, maximum selected base remains capped at 96, depth/cabin/glass/fog tests pass, and **0 GPU errors**. This quantitative check is not a matched desktop visual/FPS benchmark.

Replaced the outliner/header Stars asset with a purpose-drawn constellation SVG and used it consistently across Stars cards and tiles. Star rendering itself was not changed.

### Requested startup rollback and icon repair

Reverted the most recent shader-retrieval retry/concurrency change to the prior `Promise.all` loader, and removed its dedicated tests. Earlier weather, sliders and thickness correction were left untouched. All existing environment SVG artwork is preserved, including the new constellation Stars icon. Embedded exact SVG copies in `EnvironmentIconAssets.js` for the outliner, native cards, headers and flare link so those icons no longer depend on individual HTTP image requests through the preview proxy. `EnvironmentIcons.test.mjs` checks every embedded asset against its source file and explicitly checks all environment categories. Full unit suite: 87 passed, 0 failed. No renderer changes in this rollback/icon repair.

### Local cloud envelope, repetition and discoverable local fog

Local clouds no longer expose a box boundary; obsolete saved box selections are ignored/discarded. They have an irregular, rounded density envelope that fades out inside the enclosing intersection ellipsoid. The bounds are still finite for efficient ray intersection, not a visible box. Follow wind now translates the entire cloud rather than sliding noise through stationary bounds. Editing its placement origin or re-enabling/re-placing it resets accumulated displacement; Follow wind off freezes the current position. Shadow edit keys exclude animated centres but retain explicit origin edits, preserving the 8 Hz schedule.

Local-cloud noise blends the existing texture with oblique, incommensurate coordinates, breaking the old axis-aligned one-tile repetition without adding textures. CPU previews use the same density formulas. This is repeat suppression, not an assertion of mathematically aperiodic noise. Small local features also adapt the march count up to 96 samples to reduce undersampling (default extents/120 m features retain 16; 10 m features use 40). Added texture fetches and adaptive samples have a cost; no hardware FPS guarantee.

**Local Volumetric Fog** now has its own Environment outliner entry, independent enable toggle, full inspector and camera-placement button. Removed duplicate fog controls buried under Height Fog. Existing finite fog rendering, noise, albedo, density, local lights, shared wind, depth and cabin masking remain available. Box bounds remain intentional for fog volumes.

Validation: 92 unit tests pass, including old-period noise checks on all axes, ignored legacy box settings, adaptive sample caps, whole-cloud translation/freeze/re-placement, and independent local-fog entry. Production Vulkan cloud/fog proof passes with zero GPU errors, including fog-only, inside volume, overlap, scene lamp, foreground clipping and cabin/glass masking. Null-backend full-app bring-up/three frames passes without errors or warnings. Visual parity and desktop FPS have not been established by these probes.

### Demo-style replacement fog and Wind ForceField (2026-10-21)

Replaced the previous local-fog inspector/model with a source-informed adaptation of the published celestial demo, pinned at `da3720b353dc0e4d2421f064f65cbfeb41cdb5f2`, `docs/celestial/index.html` (fog density/shapes around 964–982; fog and Wind card definitions around 2600–2613). This is not the demo's full-screen WebGL pipeline pasted into WebGPU.

- Fog cards: **Volume / Medium / Lighting**. Box, Ellipsoid, Sphere and Dome; shape-specific extents/radius/height; core-to-shell density falloff or a soft rim; three-scale heterogeneous density; bottom weighting; albedo, absorption, forward anisotropy; independent wind or shared-field binding; four-tap sun self-shadowing; Draft/Balanced/High = 16/28/48 base samples. Dome height is Z-up, with position Z defining the base. Old custom fog settings migrate once via `FogModelVersion=1`, resetting to the new model defaults (disabled initially).
- Wind entity: **Forcefield → Wind**, type **ForceField**. Cards follow the demo: **Force** (interactive wind rose, speed, bearing-from), **Steadiness** (gust/turbulence), **Shear** (speed/veer per km), **Driving** (supported medium links). Meteorological bearing-from is converted to the existing Z-up direction-of-travel setting. Clouds and fog link to this inspector instead of duplicating Wind sliders in every panel. Existing integrated shared motion remains; this does not claim a full spatial curl-noise force solver or rigid-body force integration.
- No fake precipitation or static-height-fog wind controls: those systems do not currently support an advected wind binding. No hail added. Environment icons and the separate Environment folder are preserved; Forcefield uses the existing folder artwork.
- Existing two low-resolution targets, scene depth, cabin/glass masking, local-light upload and low-frequency projected shadows remain. Fog self-shadowing adds bounded texture-sampling work; no zero-cost guarantee. Geometry-shadowed volumetric local lights are still unsupported.

Validation: **94 unit tests pass**. Production Vulkan probes execute all four fog shapes, compare self-shadow on/off, test fog-only/inside/overlap, real lamp contribution and cabin/depth masking: **0 GPU errors**. Null-backend full app brings up the new fog inspector and Wind inspector across three frames with **0 errors / warnings**. No desktop FPS or pixel-identical reference parity claim.

### Demo local cloud replacement and initial scene translation gizmo

The previous custom local density/envelope has been replaced with a direct Z-up adaptation of the pinned demo's `hash13`, trilinear `vnoise`, four-octave shape noise, height profiles and two-octave erosion (`docs/celestial/index.html`, lines 1041–1051 at `da3720b353dc0e4d2421f064f65cbfeb41cdb5f2`). Box/Ellipsoid bounds and Stratiform/Cumuliform/Wispy forms return; placed bounds remain stationary while wind advects the procedural density, as in that demo. Local extinction is .06/m times density. The two-octave erosion is omitted for light taps. CPU density diagnostics mirror the authored formulas at time zero (CPU double vs GPU float arithmetic can differ). Existing low-resolution depth/cabin composite and global clouds remain. This replaces the local density model, not every optical formula or the entire WebGL architecture; pixel-identical appearance is not claimed. Procedural noise has a real ALU cost.

A demo-inspired **vector-fallback translation gizmo** is available for local cloud/fog, vehicle (cabin/marks select the parent vehicle), individual cones, breakable props and active scene mesh entries in a new collapsed **Objects** folder. RGB Z-up axes and a free screen-plane centre handle; **G** modal move, **X/Y/Z** constraint, **Ctrl** metre snap, **Esc** cancel, **Enter** confirm, **F** frame selection. **S** scales local cloud/fog extents. Renderer settings/folders/infinite backgrounds have no fake world-position handles. Meshes update actual scene records; static collision bounds rebuild on release, while cones call the physics placement API and vehicle moves through its respawn API. Fragment bodies are resolved through their owning physics objects. Existing special terrain-height/course functions are not a general movable terrain solver.

**This is not completion of unrestricted all-object transform editing:** no general rotation gizmo, mesh scale/collider rebuilding, viewport ray picking, undo stack, or transform persistence for arbitrary meshes yet. Camera/smoke domains/abstract effects and infinite celestial layers do not yet have transform adapters. Those limitations are explicit rather than moving dummy proxies. The reference itself limits transform targets; its optional Three.js 3D handles were not imported. No new graphics dependency or WebGL overlay context was added.

Validation: **97 unit tests pass** (projection, real volume settings, cone physics placement included). Production Vulkan cloud/fog proof passes with **0 GPU errors** after the local-cloud replacement. Full app null-backend initialization and three frames pass without errors/warnings. Browser interaction proof with a fixed test camera and **zero rendered frames** verifies an actual fog X-axis drag changes the bound renderer setting and Escape restores the previous transform. This is a UI interaction test, not a rendered GPU/performance/visual-parity benchmark. Normal camera nearly parallel to an axis hides that degenerate axis rather than presenting an unusable handle.

## Local fog contours and clustered geometry

See `Docs/GEOMETRY_CLUSTERING.md` in the repository root for the new production cluster path, research provenance, fog-guide cutoff/anchored-march corrections, tests and explicit Nanite limitations. The feature is GPU frustum culling of lossless static triangle clusters with actual indexed indirect rendering, not full virtualized geometry.

### Cloud banding follow-up (2026-10-21)

Applied the stable-lattice correction to **both global and local clouds**, not only local fog. The old cloud rates divided a fixed budget by the surface-clipped intersection span, so geometry depth re-positioned every sample. Global spacing now derives from ceiling-clipped layer thickness/base samples and feature scale; local spacing derives from volume extents/base samples and local feature scale. All media use anchored fixed spacing with a final partial segment. Cloud intervals stay capped at 96 samples; fog-bearing intervals retain their 192 cap. Long/slanted rays may use more cloud samples than before. Early optical termination residual is .0005 for clouds as well, avoiding a visible 1.5%-transmission cutoff. No extra render targets or history were introduced; no zero-cost guarantee.

Extended full-resolution intersection guards to global cloud slabs and local Box/Ellipsoid bounds, including mixed weather. Continuous depth weighting is retained. This prevents cloudy low-resolution neighbours painting over geometry lying entirely before every active medium.

Executed production GPU regression now checks the old guide cutoff independently for **fog, global cloud, and local cloud**: measured centre-pixel colour jump **0** in each fixture; the cloud contribution persists on both sides. Both cloud foreground guards pass even with deliberately cloudy neighbouring target texels. Thick-layer mean absolute transmittance error versus the 96-base-sample reference is .00002548 at adaptive base 60 vs .00026708 at base 12 in the isolated fixture. Existing cabin/glass, clipping, four fog shapes, overlap and scene-light tests pass with **0 GPU errors**. Unit suite: **99 passed**. Null-backend full-app bring-up / three frames: **0 errors or warnings**. These targeted regressions do not establish that all visual banding is absent at every setting, or measure desktop FPS.

### Diagnostics, object movement and cluster inspection (2026-10-21)

Added Render-outliner Diagnostics and Geometry clusters inspectors: optional session recording, raw timestamp charts/heatmap, partial editor CPU measurements, settings revisions and complete CSV export; real indirect-command visibility overlay, leaf-budget repartition/rebuild; corrected baked-shape gizmo anchors, plane movement and numeric world positions. Usage, measurement limits, reference provenance and executed checks: [`../../Docs/DIAGNOSTICS_TELEMETRY.md`](../../Docs/DIAGNOSTICS_TELEMETRY.md). No cloud quality/performance tuning was applied in this round.

### Preserved reference apps + particle authoring workspace (2026-10-21)

Copied the supplied `unassignedinbox/Frontier` branch at `b299ec6903cfda8237e6bb2088bb720729d55aa7` into a separate repository-root `ReferenceApps/` library: 834 immutable source/reference files, manifest and preserved supplied builds. Working particle copies are separate from originals. Added **Effects → Particles** / viewport **Particles**, embedding the original WebGPU Particle Editor and Flux Particle Studio in one lazy-loaded authoring workspace, with tool switching, acknowledged saves, local persistence, combined JSON import/export and child preview telemetry/document revisions. The driving renderer/physics pauses while authoring; only one child tool is mounted. These are actual editable particle previews, not emitters yet rendered in the driving scene. Existing Derez, fracture/gas, environment and diagnostics work is preserved. Usage, source provenance, rebuild and validation: [`../../ReferenceApps/README.md`](../../ReferenceApps/README.md).
