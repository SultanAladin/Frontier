# Atmosphere Editor validation

## Evidence

- [`Images/orbit.png`](Images/orbit.png) — actual Earth preview, cloud atlas, rim and docked card at 1440 × 900.
- [`Images/mars.png`](Images/mars.png) and [`Images/venus.png`](Images/venus.png) — approximate planetary presets.
- [`Images/surface.png`](Images/surface.png) and [`Images/surface-twilight.png`](Images/surface-twilight.png) — the
  surface sky and locally projected clouds at default and below-horizon sun elevations.
- [`Images/mobile.png`](Images/mobile.png) — actual 390 px responsive page, full height.
- [`Results.json`](Results.json) — numeric probes and pixel differences from the latest local run.
- [`Images/atmosphere-all-controls.png`](Images/atmosphere-all-controls.png) and
  [`Images/geometry-all-controls.png`](Images/geometry-all-controls.png) — all 12 infographic controls.
- [`Images/atmosphere-dock.png`](Images/atmosphere-dock.png) and [`Images/geometry-dock.png`](Images/geometry-dock.png)
  — closer captures of the controls.
- [`VisualControlsResults.json`](VisualControlsResults.json) — distinctive visuals and interaction checks.

The captured prototype uses the uncommitted checkout based on `4bde2fe993e9bc6ab07abcddaca836fcab0b28d1`.
The latest visual references are `SourceImages/General UI/146a70d9e465a1a9def640bca5aa68f8.jpg`,
`0bab27674eea6c04ccaf172f194ce6ef.jpg` and `14d2fdb22c3e179f6ad5fa4152d663ee.jpg`. The HTML is a new experimental
design; these images are browser captures, not production C++ evidence.

## Local verification

Run the server described in the [prototype README](../../Experimental/AtmosphereEditor/README.md). With Node.js,
Playwright Core and Chromium installed, run:

```sh
node Exhibits/AtmosphereEditor/AtmosphereEditorValidation.cjs
node Exhibits/AtmosphereEditor/VisualControlsValidation.cjs
```

The local capture ran on Linux using Chromium 153.0.8010.0, Playwright Core 1.64.1, WebGL 2 and SwiftShader. The
main atmosphere window was 1440 × 900, with a 1070 × 900 drawing buffer after docking; the infographic
captures used 1440 × 1250 to show the full docks. This sandbox used
`NODE_PATH=_ClaudeScratch/browser/node_modules` and its NSS/NSPR libraries via `LD_LIBRARY_PATH`.

The validations exercise all 12 individually styled chart controls, including pointer drag, keyboard input,
marker updates, accessible range semantics and reset. They also cover dock geometry, cloud coverage and switch,
rim enhancement, planet presets and per-planet reset, camera lag and its convergence, orbital/surface views, twilight
and mobile layout. It compares actual `readPixels` samples after edits, tests that one card is visible and editor
navigation is available, and checks WebGL/uncaught browser errors. See the [geometry exhibit](../GeometryDetail/README.md)
for two-way navigation and split-layout checks. The camera-lag test observed a 0.6 rad target while the
camera remained at 0 rad immediately after a pointer move, then verified that it settled to the target.

For a physical sanity check, the Earth shader reports upward RGB transmission from 100 m and a downward planetary
shadow. The measured transmission `[0.945, 0.875, 0.773]` is within 0.025 of the analytic vertical-profile result
`[0.941, 0.869, 0.765]`; downward sunlight is occluded. This is an approximation check, not a full validation of all
view angles, the other planets, multi-scattering or measured radiance. Rim enhancement and the cloud projection are
explicit visual approximations. A hardware GPU, other browsers and production renderer integration were not tested.
