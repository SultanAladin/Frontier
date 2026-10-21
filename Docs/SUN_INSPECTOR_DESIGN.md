# Sun inspector · solar observatory

The Sun inspector now follows the approved Clouds observatory design: large lightweight values, compact data rows, instrument diagrams, pill navigation and rounded charcoal cards. Sun-specific styles add a restrained warm amber colourway. No scene-renderer or shader changes are introduced.

## Preserved quick toggles

The original native `Tiles` component, keys, labels and classes are retained for **Sunlight**, **Sun disc** and **Day cycle**. They now sit directly below the Overview Azimuth / Source tint / Sunlight metrics, as requested, and keep their native appearance and on/off feedback. No new layout/theme rules target those tiles.

## New sections

- **Overview:** interactive world-space alt/az vector diagram, current elevation, azimuth, unit direction vector, source tint setting, intensity multiplier, manual/time-linked mode switch, paired drag sliders and numeric direction inputs, lighting presets and reset.
- **Daylight:** daily altitude curve from the existing `SolveCelestial` solver, geometric horizon-rise/set times, hours above horizon, editable local control time, real-hour cycle duration, date, latitude, longitude and UTC offset.
- **Light & colour:** intensity, RGB/temperature mode, a rounded custom-tint pill, colour temperature and an enhanced blackbody-reference instrument with a filled spectral plot, full-spectrum Wien peak and visible-window readouts.
- **Disc & optics:** interactive exaggerated angular-size diagram, direct diameter/aureole inputs and the existing Lens Flare navigation.

All previous Sun controls remain accessible. Manual angle edits disable automatic position and stop the day cycle so the renderer does not immediately overwrite the edit. The native day-cycle tile updates accordingly. Time/date/location edits reconnect to the existing solver. The original engine convention is retained: **+X east, +Y north, +Z up**; azimuth is counterclockwise from +X. Presets are the existing host presets, not new lighting implementations.

## Honest data semantics

- The alt/az diagram uses actual configured angles and a polar editing projection: centre = +90°, inner horizon ring = 0°, outer ring = −90°; it is not a rendered sky.
- The day curve samples the existing low-precision celestial solver at 15-minute intervals and caches by date/location/timezone, not by animation time. The bounded four-entry cache avoids recalculating the whole day every UI tick.
- Horizon rise/set times linearly interpolate geometric 0° crossings. They exclude atmospheric refraction and terrain, handle polar day/night with unavailable crossing markers, and are not navigation-grade ephemerides. With manual direction, the curve is labelled a reference rather than active telemetry.
- Intensity remains the exhibit's multiplier, not invented lux or watts.
- The spectrum is a normalised Planck reference for the chosen Kelvin value. The scene still uses its existing approximate RGB temperature tint or explicit RGB colour, not spectral rendering.
- Angular diameter is twice the existing radius setting. The disc widget is explicitly exaggerated for editing.

## Implementation and verification

`Source/SunInspectorDashboard.js` builds the inspector; `BuildNativeSun` delegates to it. Existing observatory styles now opt in only `.cloud-observatory` and `.sun-observatory`; `Style/SunInspector.css` applies the solar colourway. Atmosphere, baking, Moon, stars, clouds, outliner and viewport are otherwise unchanged.

- **175 unit tests pass**, including vector conventions, bounded setters, manual/automatic mode switching, real solver correspondence, day-path cache invalidation, polar day/night, clock wrapping and finite normalised spectra.
- Dawn application validation: Sun inspector selected, 3 frames, 0 errors/warnings.
- `Tools/SunInspectorBrowserProof.mjs`: real Chromium/WebGPU checks all three quick toggles, disc toggle round-trip, direct/keyboard vector edits, manual edits stopping the day cycle, time/intensity/temperature/diameter changes, tab navigation, the preserved Clouds inspector and the untouched Atmosphere bake card. Completed with 0 browser errors.

Optional proof uses the same tooling/server setup as `CloudInspectorBrowserProof.mjs`. Screenshots are written outside Git to `/home/user/sun-inspector-proof/`.

## Input and card refinements

Clouds, Sun and Atmosphere share the existing `ConstructSlider` input mechanics through `ObservatorySlider`, not duplicated ad-hoc range logic. Each control pairs a numeric editor with a full-width rounded track, supports pointer/keyboard edits, applies range/step/scale semantics and uses the existing notify/synchronisation path. Card surfaces are instrument-specific rather than uniformly repeated: solar aim controls are an open section, daylight is a bordered chart, the disc has a radial field, and connected optics uses a dashed navigation panel.

Sun browser proof also verifies the relocated tile DOM order, slider keyboard input, RGB tint pill writeback and spectrum rendering.
