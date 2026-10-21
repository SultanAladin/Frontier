# Fog · geometry-medium instruments

The Height fog and Atmospheric fog (aerial perspective) inspectors now use their own scoped dashboard. Local volumetric fog, shaders, accepted Stars/Atmosphere/Baking and original Moon previews are not redesigned.

## Categories

- **Visibility** — six receding split-luminance contrast targets, a draggable/keyboard-accessible distance ruler, editable probe distance and world height, and weighted RGB contrast retention. This is a fixed neutral-light reference, not the camera render. The scene on/off control remains independent of diagnostic visibility. Geometry fog applies in Final image view, not to the sky.
- **Medium / Height fog** — exponential density terraces indexed by actual world altitude, adaptive altitude range, sea-level density and falloff-height controls.
- **Medium / Atmospheric fog** — interactive clear-start gate plus actual aerial density and start distance controls.
- **Colour & sun / Height fog** — rounded tint picker with the existing sRGB-to-linear conversion and exact cos⁸ forward-weight reference. Sun scatter affects in-scattered light, not transmission.
- **Spectrum / Atmospheric fog** — RGB transmission shutters and the native Mie blend from spectral coefficients [0.45, 0.70, 1] toward equal grey extinction.

Scene controls retain native bounds and invoke the existing settings notification/persistence path. All numerical scene parameters retain paired editable numbers and slider tracks. Probe controls are local diagnostic state only.

## Math and performance boundaries

`FogPathTransmission` transcribes `Shader/FogMedium.wgsl`, including integrated exponential height, squared height optical depth, aerial clear distance, RGB mixture and the exponent cap. The six neutral targets use fixed bright/dark radiances 0.8/0.08, fixed in-scatter 0.16 and display gamma 2.2. They are explicitly not current Sun/tint scene radiance. Neutral contrast is the weighted RGB transmission, not an observed visibility-distance measurement.

Plot redraws use settings-key caching and hidden-pane guards; there is no animated sweep, extra GPU pass, texture asset or shader change. These are UI-work reductions, not a measured desktop FPS guarantee.

## Validation

- 192 unit/regression tests pass, including height-path reciprocity, spectral/grey extinction, clear starts, isolated diagnostics, cos⁸ and native editing bounds.
- The Vulkan Fog GPU proof executes the actual renderer function for 13 cases and compares output directly against the inspector's CPU math (tolerance 2e-5), with zero GPU errors.
- Browser proof: actual Fog density, falloff slider, tint, scatter, enable toggle, aerial density, keyboard clear-start gate, Mie blend, category keyboard navigation; no browser errors. Also reruns accepted Stars, categorized native Moon and approved atmospheric-column checks.

Sources are under `Experimental/VehicleHmiPrototype/Source/FogInspectorDashboard.js`, `Source/InspectorCategories.js`, and scoped `Style/FogDashboard.css` / `Style/InspectorCategories.css`.
