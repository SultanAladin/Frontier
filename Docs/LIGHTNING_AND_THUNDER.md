# Lightning and thunder — first integrated weather pass

## Try it

Open the live prototype on port 8080. In the outliner choose **Environment → Lightning & thunder** (also available inside **Clouds**).

1. Click **Strike ahead**. Lightning becomes enabled; automatic strikes remain off by default.
2. Click **Enable thunder sound** once, then strike again. Browser audio requires this gesture. The initial sound is intentionally delayed, not missing.
3. Choose **Inside cloud** for an elevated discharge, or **Cloud → ground** for a descending channel. Change distance, **Horizontal cloud channel span**, and flash radius as needed. The default global channel spans about 3.15–5.25 km; local channels scale to their selected cloud volume. At nearby distances the cloud source may be above the chase camera's field of view; look upward to inspect it.
4. **Author lightning in Particle Editor** opens the actual existing Lightning inspector. Tune roughness, branches, detail, width, brightness and timing, then click the workspace header's **Apply lightning to scene**. Return to the scene and strike. Applying settings does not silently enable storms, replace other emitters, change the sun or modify exposure.

Bright-flash warning: storms are opt-in. Automatic intervals are at least two seconds; rapid manual requests are rate-limited. Several return strokes can occur within one discharge.

## Rendering

- Reuses `makeBolt` from **ReferenceApps/Working/ParticleEditor/js/lightning.js**, rather than recreating an unrelated lightning editor. The preserved **Upstream** copy is unchanged.
- Converts the editor's Y-up coordinates to the scene's Z-up system; scales its 15 m preview to weather scale. A channel has at most 1,024 segments and is generated once per strike, not every frame.
- A faint leader and three analytic return-stroke brightness peaks share the same channel over a 0.36 s lifetime. This is a stylized brightness envelope, not a physically simulated propagating leader.
- Rasterized depth-tested, current-camera particle ribbons provide a white core and cooler halo, without TAA smearing. A separate RGBA16F emission/depth target is composited in the existing presentation pass **after cloud integration and before tone mapping**. Otherwise a below-cloud bolt would incorrectly be extinguished by the entire cloud column behind it.
- Bolt/cloud attenuation uses a bounded, approximate optical-depth fraction in front of the channel. Local/fog bounds are approximated as boxes for this composition. Not a second exact volume integration, and not a guarantee of exact attenuation inside dense overlapping media.
- Cloud samples receive spatially bounded light from an extended channel capsule, with spatial falloff and three bounded cloud-density occlusion taps. Global clouds, local clouds and local fog can scatter it. The original cloud sampling/quality controls remain unchanged; the existing 544-byte cloud ABI and 1,072-byte frame ABI are preserved. A separate 48-byte flash uniform carries source, colour/radius and the elevated channel endpoint.
- One transient omni source enters the existing real scene-light/cluster buffer when capacity permits. It illuminates surfaces through the normal lighting path. No exposure, directional-sun or sky-brightness override is used. Temporal reconstruction can still soften/lag the surface lighting response.
- Only one discharge is active at a time. No terrain damage, fires, impact sparks, physical electrical simulation or terrain snapping: ground endpoints currently use world Z=0.

## Audio

Locally synthesized brown-noise rumble plus a nearby high-frequency crack. Actual 3D strike/listener distance determines onset (`distance / 343 m/s`), filtering, gain and stereo position. Ground strikes use their endpoint; in-cloud strikes use the cloud source. This is a point-source approximation, not full extended-channel acoustic propagation.

Web Audio resources are allocated only after an explicit sound gesture. At most 12 source voices may be scheduled/playing; mute, hidden-page transitions, disabling lightning and entering the particle workspace cancel pending sounds. Sound permission/enabled state is not persisted in particle documents. No external audio files or continuous audio-generation loop.

## Performance and logging

- Storms and automatic firing default off. Off-state renderer overhead is not claimed to be zero: there are lightweight branches and the additional resource binding/uniform.
- A full-resolution RGBA16F bolt target is allocated on the first active strike and reused (about 15.8 MiB at 1920 × 1080). Idle begins with a 1 × 1 target. It is cleared after the discharge; no continuing bolt render pass is submitted while idle. Presentation performs the bolt texture lookup only on active-strike frames.
- **Lightning** is a separate GPU timestamp metric. Cloud scattering cost remains in **Clouds**; surface lighting is in the usual lighting passes. Do not add nested CPU/UI timing fields or interpret a single pass as total GPU frame time.
- Strike events record mode, source position, segment count, actual listener distance, thunder delay and whether sound was scheduled.
- RAM capture and its existing row cap remain unchanged. CSV is serialized on explicit copy/export; there is no streaming disk logger.

## Validation

- Prototype unit suite: 124 passing tests, including geometry limits, opt-in behavior, stable channels, thunder distance, typed editor transfer, light-buffer integration and voice cancellation.
- Dawn null-backend integration: 20 active frames, ground and in-cloud strikes, no validation errors/warnings; checks CPU phase fields and strike events. This validates commands/shaders, **not desktop FPS**.
- Real Chromium/WebGPU proof: opens the Particle Editor's Lightning inspector, changes brightness, explicitly applies it to the host, returns, draws a visible strike/cloud flash and checks recorder/UI A/B switching; zero page/console errors. Screenshots/results are kept outside Git in `/home/user/lightning-proof/`.
- Real Chromium/Web Audio graph: explicit gesture unlock, two voices scheduled one second ahead for 343 m, mute cancels both, zero page errors. Not a perceptual audio-quality evaluation.
- Preserved original particle handoff checks: 65 passing. CSV server tests: four passing.

Useful commands from `Experimental/VehicleHmiPrototype`:

```sh
npm test
SIZE=320x180 HOOKS=Tools/LightningHooks.mjs node Tools/HeadlessHarness.mjs . 20 '?capture=1&quality=low&stresspool=0&inspector=lightning'
node Tools/LightningBrowserProof.mjs
node Tools/ThunderBrowserProof.mjs
```

Browser proofs require the optional Playwright/Chromium packages under Tools and a running preview server. Software-GPU proof timing is not representative of the user's NVIDIA desktop.

## Cloud-body illumination follow-up

The lightning source now searches at most 27 candidate samples once per strike for occupied cloud density. It uses the renderer's baked field (shared on CPU to avoid rebaking during a strike) and the last encoded advection offsets. It no longer assumes that being within the global cloud layer's height range means being inside a cloud. Candidates stay near the requested global strike position, or within the selected local volume. The new **Cloud source** control selects automatic/global/local placement. Automatic prefers global clouds when enabled, otherwise local clouds. Explicitly selecting an absent/empty medium does not silently enable or densify it.

A bounded local diffusion lobe complements direct exponential light attenuation, so an embedded discharge can light the visible face of an optically thick cloud. This is an artistic multiple-scattering approximation, not physically solved transport. Falloff remains spatially bounded by flash radius and contribution is integrated only where the existing medium has density; no global exposure, sky brightness or cloud-opacity adjustment.

Status/CSV strike metadata reports `cloud_medium`, `cloud_density_at_source`, and `cloud_source_embedded`. Global anchors can shift horizontally within a bounded nearby search; distance controls therefore say **Approx. strike distance**. Thunder still uses actual 3D distance. Empty cloud coverage is reported rather than promised to glow. Existing bolt geometry, sound behavior and accepted cloud shape/quality settings are retained.

Validation: 127 unit tests pass, plus 20 active null-backend frames with zero errors/warnings. A real Chromium/WebGPU isolated cloud-light A/B test (`Tools/CloudLightningBrowserProof.mjs`) keeps geometry, density, camera and time fixed, toggling only the flash source. At a 3,240-pixel cloud target, 507 global-cloud pixels and 954 local-cloud pixels gain radiance; cloud transmittance is bit-identical before/after (maximum alpha difference zero); no page/console errors. These counts describe the test views, not a performance benchmark or a universal screen-coverage target. Raw results are outside Git at `/home/user/lightning-cloud-proof/result.json`.

Snowfall/accumulation is documented separately in `PARTICLE_SURFACE_GRID.md`.


## Cloud-spanning discharges (latest)

The previous in-cloud geometry ended only about 500 m from its origin, and illumination used one point source. Global in-cloud discharges now use a randomized kilometre-scale horizontal channel (`LightningSpread`, default 4,200 m, range 400–10,000 m). For the camera-targeted strike, the channel runs sideways/forward rather than randomly disappearing behind the viewer. Selected local volumes instead bound the endpoint to their dimensions. This does not guarantee every channel segment follows occupied cloud density: the start is searched in density; the remaining geometry is procedural.

Cloud-to-ground strikes retain their ground channel and add an elevated horizontal cloud branch. Both modes remain capped at 1,024 total ribbon segments. The cloud lighting approximates the discharge as a capsule from the embedded source to the elevated endpoint. Existing direct attenuation/diffusion and three density-occlusion taps now use the nearest point along that capsule. It adds no density, cloud-opacity change, full-screen brightness lift, extra cloud march, or light-cache volume. It can illuminate a broad cloud region, not an unbounded sky. Ground surface light and thunder remain point-source approximations. No desktop FPS claim is made.

Validation: 154 tests pass; active Dawn null lightning integration validates 25 frames with zero errors/warnings. Real Chromium/SwiftShader point-source versus channel A/B at fixed camera/time/density: global RGB sum 1064.9305 → 1392.7727, with 311 pixels brighter than the point source; local RGB sum 7323.2696 → 7370.5599, with 558 pixels brighter. Both retain bit-identical cloud transmittance (maximum alpha difference zero). Zero page/console errors. These fixture results do not guarantee whole-cloud coverage for every camera/cloud configuration and are not a speed benchmark. Proof: `Tools/LightningSpreadBrowserProof.mjs`, results outside Git in `/home/user/lightning-spread-proof/`.
