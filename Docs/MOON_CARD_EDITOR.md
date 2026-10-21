# Moon · preview-first cards

The user approved Stars but preferred the previous Moon cards to the infographic pass. This revision affects **Moon only**. Stars source, shared celestial helpers and their stylesheet were hash-checked unchanged, and the browser proof still runs the accepted Stars interactions.

## What changed

- Removed the rejected rectangular horizon plot, logarithmic angular ruler and flat atlas swatches.
- On the subsequent request, grouped the preferred native cards into **Phase / Placement / Light & size / Bodies**, matching Stars’ category-navigation behaviour without copying its infographics.
- Restored the native card editor with the original settings dropdown and Visible / Follow sky tiles.
- **Lunar phase:** a larger original textured-sphere preview, phase-age and illuminated-fraction readouts, an editable slider/number, and compact New / First / Full / Last shortcuts.
- **Moon catalogue:** the previous horizontally scrolling **spherical thumbnails**, using the original registered surfaces and full-phase native sampler. Body choice still assigns its original size preset.
- **Moonlight / Moon size:** separate preview cards with the existing brightness, glow, angular diameter and primary-only light controls. Size preview is explicitly compressed, not physical display scale.
- **Lunar position:** the original celestial globe and direct azimuth/elevation controls. Manual input disables solver follow.
- **Moon rotation:** original textured orientation guide with roll, pitch, spin and reset controls.
- Existing planet controls remain in a final disclosure.

Paired sliders and numeric inputs are retained. The primary Luna default remains **25°**, all four instances remain independent, and additional moons remain visual-only. No shaders, scene lighting algorithms, texture assets, Stars, approved Atmosphere or Baking are replaced.

## Implementation

`Source/MoonInspectorInfographics.js` retains its exported helper/builder API for compatibility but now builds the card editor. `Style/MoonCards.css` is scoped to `.native-environment.moon-cards`, including overrides of the previous fixed phase-card positioning. The accepted celestial stylesheet and Stars module are unchanged.

The real `NativeMoonPreview` sphere sampler and `LunarSphere` placement globe are used. Texture-dependent chart updates are capped at 4 Hz, while manual drag handlers still update immediately. Ready callbacks are disposal-guarded. No extra GPU pass or readback is added; no unmeasured FPS claim is made.

## Verification

- **186 unit tests pass**.
- Moon-selected Dawn bring-up: **3 frames, 0 errors, 0 warnings**.
- Updated `Tools/CelestialInfographicsBrowserProof.mjs`: accepted Stars regression, removed Moon tabs, actual Moon texture colour variation, phase presets, globe keyboard/pointer editing, rotation, angular size/reference, moonlight power, six spherical body choices present, body selection, independent additional instances, primary-only light controls, planets, subsequent GPU frame and approved atmosphere preservation. **0 browser errors**.
- Screenshots outside Git: `/home/user/celestial-infographics-proof/moon-cards-{overview,position,light}.png`.

## Category update validation

192 tests pass. The browser proof exercises all four categories, native globe pointer/keyboard editing, rotation, original textured phase (4,807 distinct sampled colours), preset body selection, independent moon instances and planets. Tabs implement arrow keys, Home/End and roving focus; hidden plot updates are skipped and catalogue thumbnails load when their category is opened. Stars’ three accepted source/style files retain their recorded hashes.
