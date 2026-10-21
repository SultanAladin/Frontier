# Stars and Moon · distinct celestial infographics

**Current scope:** Stars was approved and remains unchanged. The Moon infographic pass documented below was rejected and is superseded by the preview-first card editor in [MOON_CARD_EDITOR.md](MOON_CARD_EDITOR.md).

These inspector-only designs follow the approved infographic direction, without reusing the atmosphere column, transmission dial or composition pad. Existing renderers, catalogue/texture assets, Moon defaults and lighting behaviour are retained.

## Stars: cartography, population, time domain

- **Star map:** a north-pole-centred Lambert equal-area all-sky planisphere, plotted from the actual 8,920-entry SLAT catalogue. Real catalogue RGB colours and flux cutoff drive the points. Right-ascension labels rotate with the configured catalogue rotation. Drag/keyboard edits bind to StarRotation.
- **Population:** real one-magnitude-bin counts, with separate muted total and bright eligible populations, an editable limiting-magnitude marker, exact cutoff counts and paired sliders/numbers. Counts exclude daylight, clouds and local-horizon visibility; they are not rendered pixel counts. Histogram edge bins clip the range to −2…8.
- **Scintillation:** four-second reference trace reproducing the current `NightStars.wgsl` three-frequency signal, direction-derived phase and depth scaling for the brightest actual catalogue entry. It uses the UI clock and samples at at most 8 Hz; it is not GPU telemetry or a frame capture. Disabling twinkle produces unity gain.
- All original controls remain available: star-field toggle, magnitude, brightness, point size, halo, catalogue rotation, Milky Way, twinkle toggle/depth/rate and clear-night preview. Night preview retains moonlight and explicitly explains its environment changes.

The planisphere omits atmosphere and is not the local observer's sky. It stays a catalogue reference when the star renderer is disabled. Static plots cache relevant inputs, and histogram counts are recomputed only when the cutoff changes.

## Moon: textured specimen, phase rail, horizon coordinates, angular tape

- **Phase:** original `NativeMoonPreview` texture sampling and registered 2K source, not an illustrated replacement. Geometric illuminated fraction, phase name, four phase stops and a draggable synodic timeline explain the 29.53-day cycle.
- **Placement:** a rectangular horizon-coordinate diagram edits azimuth/elevation directly; an independent textured surface frame edits roll/pitch. Spin is also exposed through the existing renderer setting.
- **Disc & light:** logarithmically spaced angular measuring tape supports the full 0.1–180° artistic range and the explicit Earth Moon 0.52° reference. Current size is never claimed to be physical display scale. Brightness, glow and primary Moon lighting controls retain existing semantics.
- **Bodies:** six original registered texture swatches, loaded lazily when the tab is opened. Body selection assigns the original body's size preset. Existing planet controls remain available under Orbiting neighbours.
- Four scene Moon instances remain independently editable. The default primary Luna remains **25°**, with moonlight unchanged. Additional moons remain visual-only and do not expose a misleading scene-light power control.
- Manual phase/azimuth/elevation edits disable solver follow. Orientation, brightness and size do not disable follow. Reset and reference actions notify the same host/persistence path as numeric edits.

Textured-sphere redraws are capped at 4 Hz during automatic updates, retain the existing pixel cache and stop while hidden. Image-ready callbacks are disposal-guarded. No additional render pass, shader or GPU resource is introduced. These are implementation bounds, not desktop FPS claims.

## Scope and verification

New modules: `CelestialInspectorUI.js`, `StarsInspectorInfographics.js`, `MoonInspectorInfographics.js`; stylesheet `CelestialInfographics.css`. Shared code is limited to navigation, paired input controls, switches and cached-plot plumbing. Diagram geometries/data and inspector compositions are independent. Sun, Clouds, approved Atmosphere and Baking are not redesigned by this work.

- **186 unit tests pass**, including projection geometry/rotation, exact catalogue cutoff counts, histogram totals, bounded edits, shader-equation scintillation, Moon cycle conventions, manual/follow linkage, instance independence, preserved defaults and angular-ruler monotonicity.
- Dawn bring-up: **3 frames each for Stars and Moon, 0 errors/warnings**.
- `Tools/CelestialInfographicsBrowserProof.mjs`: actual Chromium/WebGPU checks map keyboard/pointer input, magnitude edits, twinkle controls, original Moon texture colour variation, phase changes, horizon keyboard/pointer input, orientation, size/reference, moonlight power, all six body choices present, body selection, independent additional instances, visual-only lighting scope, subsequent GPU frame, and preservation of the approved atmosphere column. Completed with **0 browser errors**.

Screenshots/logs are outside Git at `/home/user/celestial-infographics-proof/` and `/home/user/stars-moon-*.log`.
