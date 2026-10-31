# Experimental environment inspectors

Status: **reference-led prototypes, not production-integrated or proven pixel-identical**.
Sun, Clouds, Atmosphere and Lens Flare live here; the optional CMake target does not replace
production inspectors or modify the renderer. Numeric controls use the `ControlKit::Slider`
primitive and palette from Material Workspace, as requested.

## Host contract

Configure with `-DFRONTIER_EXPERIMENTAL_ENVIRONMENT=ON`, link
`ExperimentalEnvironmentInspectors`, and include `SolarInspector.h` and
`EnvironmentInspectors.h`. Create and own one settings instance per edited object; call the
appropriate `Construct*Inspector` in a scrollable ImGui window. The structs are authoring
models, not the scene schema. `Tab` is UI state; `Revision` changes on edits and can be
observed by the host. There is no scene adapter, TOML serialization, undo integration or GPU
bridge here. Do not memcpy these structs to GPU buffers.

- Call `AdvanceSun(settings, deltaSeconds)` once per frame. It advances the optional day cycle
  and solves automatic position using the demo's low-precision J2000 Sun solver. Azimuth is
  counterclockwise from east, elevation is above the horizon, both in degrees. Time wraps
  without incrementing the date, as in the demo. This is not a scientific ephemeris.
- Map the dimensionless Sun multiplier, RGB/temperature choice, angular **radius** in degrees,
  glow, disc toggle and direction to the renderer. Frontier's existing illumination in kilolux
  and angular diameter require explicit conversion. Pass `OpenFlare` for inspector navigation.
- Enable `ImGuiConfigFlags_NavEnableKeyboard` in the host and forward key events to use the
  focusable diagrams and authoring slider arrow/Home controls. Diagram Shift-arrows use the
  reference's tenfold step. Complete browser tab-key/accessibility parity is still pending.
- The Clouds atlas uses a CPU port of `CloudSettings.js`/`CloudField.js`, including the demo's
  seeded field, height profile and diagnostic sampling; it is not a lit render. Optional
  `GpuMilliseconds` is a host metric; negative means unavailable. `CloudActions` supplies wind,
  local-cloud placement, lightning, sound and particle-editor actions. Lightning settings are
  authoring values; the host must implement their scene effect. Formation includes the
  collapsible layer-stack thickness editor.
- Atmosphere density and 64-sample RGB sunlight transmission use the demo's formulas. The
  probe altitude is diagnostic only. `AtmosphereActions` supplies a bake callback and optional
  drawing callbacks for real host-provided GPU sky/baked textures. Without those callbacks the
  GPU preview explicitly says it is unavailable; no baked image or operation is fabricated.
- The Lens Flare composite uses a CPU port of `NativeLensFlareKernel.js`. Its preview is
  tone-mapped; `RenderFlareImage` produces linear HDR RGB at the reference 640 × 280 size, and
  `EncodeFlarePfm` encodes bottom-up little-endian PFM. `FlareExport` supplies the host's file
  dialog/write operation. Without that callback, export is disabled. Flare preview position is
  an authoring parameter; in a scene the renderer should track the Sun. `GhostShape` uses 0,
  6 or 8, as in the demo.
- Cloud coverage, anvil, erosion and absorption are fractions; cloud heights and distances are
  metres; resolution is percent per axis. `Icons/SolarReference.svg` is byte-identical to the
  demo's Sun outliner icon. The adjacent 96 px transparent PNG is a Chromium raster because the
  pinned ThorVG does not implement its filters. Neither is installed in the production atlas.

## Remaining work

- The independent settings and actions still need scene adapters, undo/redo, persistence,
  outliner artwork registration, renderer wiring and host file-dialog handling. Real sky LUTs,
  baking and production lighting cannot be verified in the standalone prototype.
- Layouts and instruments are closer to the reference, but **1:1 parity is not established**:
  typography, some diagrams, native combo/date widgets, tab keyboard semantics, footers and
  specific states still differ, including content below the visible viewport. The requested shared Material Workspace slider deliberately differs from the web
  demo's slider. Cloud and flare previews are lower-resolution CPU diagnostics on screen;
  the flare export is full-resolution. No Windows or GPU/window-system capture is available.

See [`Exhibits/SunInspector`](../../Exhibits/SunInspector/README.md) for reference provenance,
real C++ captures, comparison images, interaction checks, reproduction and limitations.
