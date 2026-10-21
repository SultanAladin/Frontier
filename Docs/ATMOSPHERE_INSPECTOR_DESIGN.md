# Atmosphere inspector · optical workbench

The atmosphere is a distinct blue-grey instrument workspace, not another Clouds dashboard. It shares only navigation/input primitives. Film-like GPU imagery, open metric strips, a square composition grid, tall rounded altitude column, segmented RGB strips and differently shaped appearance controls give each function its own visual treatment.

## Sections

- **Overview:** actual decoded GPU sky-view lookup via the existing `SkyLutPreview`, its real revision number, open coefficient readouts, a draggable molecular/aerosol composition pad, and paired sliders/numeric editors for Rayleigh, Mie and ozone.
- **Medium:** relative exponential density-versus-altitude curves with separate Rayleigh/Mie scale-height sliders; bounded medium multiplier and anisotropy controls.
- **Light response:** RGB transmission strips calculated with the exact existing `ResolveSunTransmittance` function, air-mass and multiple-scattering toggles, sky brightness/tint, ground albedo, horizon glow, white line, civil-twilight gate and sample budget.
- **Baking:** the original Atmosphere bake, Baked atmosphere and GPU lookup-table diagnostics are moved here without redesigning their controls, content or helper implementation. They are built once on first visit.

All existing atmosphere settings are available. No shaders, render pipelines, light model, shell or unrelated inspector are replaced.

## Data semantics and bounded work

- The atlas is tone-mapped GPU LUT data, not a camera preview or generated substitute. It excludes the solar disc, stars, Moon, fog and scene geometry. The existing readback caches by LUT revision, refuses concurrent copies and releases its staging buffer. The new Overview preview requests/redraws at **at most 4 Hz**, and only while its tab is active; no new rendering pass is added. This is an implementation cap, not a measured FPS claim.
- Relative density follows `exp(-altitude / scaleHeight)`, normalised separately for each medium at sea level, matching the existing atmosphere density model. It is not a pressure/air-quality measurement.
- The transmission strips are isolated atmospheric extinction at the actual current Sun direction and the existing 100 m observer. They deliberately remain diagnostic when atmosphere/air-mass visibility toggles are off. Source intensity/tint, clouds and scene luminance are excluded. Results are cached by direction, multipliers and scale heights so the three strips share one calculation.
- RGB coefficients and user-facing multipliers remain unchanged; no fabricated physical units are added.
- Paired sliders/numeric editors reuse the existing `ConstructSlider` mechanics through `ObservatorySlider`, with bounded setters, integer sample budgets and the existing Notify/Synchronise path.

## Verification

- Full unit suite: **175 tests pass**. New tests cover bounds, integer budgets, non-finite rejection, exact exponential density, equality with renderer transmission, cache invalidation, zero-extinction identity and below-horizon occlusion.
- Dawn bring-up with Atmosphere selected: **3 frames, 0 errors, 0 warnings**.
- `Tools/AtmosphereInspectorBrowserProof.mjs`: real pointer drags change molecular scattering and sky brightness; keyboard editing changes composition; numeric editing changes scale height; all three transmission strips and original baking cards/tabs remain present. Also exercises Cloud coverage/erosion slider percentage mapping. **0 browser errors**.
- Sun refinement proof: tint pill updates linear RGB and selects RGB source, spectrum renders, sliders accept keyboard input, and original quick toggles remain directly after the Overview metrics. **0 browser errors**.

Screenshots are intentionally outside Git at `/home/user/atmosphere-inspector-proof/` and `/home/user/sun-inspector-proof/`. Proof scripts require the existing Tools dependencies and the prototype server on port 8080.

## Reference-led redo (current design)

The preceding blue optical-workbench treatment was rejected. This pass returns to the actual supplied reference images, especially `13edfbc6a1110a5abd8ec9099429cf07.jpg` (annotated specimen with floating metrics), `146a70d9e465a1a9def640bca5aa68f8.jpg` (quiet dot field, charcoal surfaces, restrained lime), and `0bab27674eea6c04ccaf172f194ce6ef.jpg` (compact navigation and large lightweight measurements).

The primary composition is now an **interactive atmospheric cross-section**, not a GPU screenshot followed by forms. `AtmosphereColumnInstrument.js` draws the exaggerated shell diagram from the actual exponential density model. The diagnostic altitude probe supports direct shell clicks/drags, arrow keys and a paired slider/numeric editor. Floating molecular/aerosol density readouts are exact relative fractions at that altitude. The probe never changes rendering settings. Schematic geometry is explicitly labelled, with no claim of a rendered Earth or physical shell scale.

The sky-view GPU capture remains below the scan and coefficient strip, with its existing 4 Hz cap. Density scan pixels are retained unless the probe or relevant medium changes, avoiding redraws just because the day cycle advances. Original baking cards remain unchanged in Baking.

Across the three inspectors, excessive observatory headings and coloured technical framing are removed in favour of compact titles, neutral surfaces and visual-first hierarchy. Cloud coverage now overlays a larger actual density atlas. The Sun elevation readout floats over the vector plot rather than consuming a separate block above it. Sliders, editable numbers, Sun tint pill, improved spectrum and exact native quick tiles remain intact.

Validation for this redo: **176 unit tests**, clean 3-frame Atmosphere GPU bring-up, and all three browser proofs. The atmosphere proof checks both keyboard and pointer altitude probes and verifies that the scene settings remain unchanged by those diagnostic interactions.

## Medium / Light response replacement

The atmospheric-column design was explicitly approved. Its implementation and styles are retained; the original Baking content and builder remain unchanged. The rejected Overview XY composition pad and the previous Medium/Light card bodies are removed, rather than reskinned.

`Source/AtmosphereMediumLight.js` now builds distinct instruments:

- **Overview medium recipe:** open R / M / O₃ coefficient channels with paired sliders and editable values. No XY pad.
- **Medium falloff comparator:** mirrored density ribbons around a shared altitude ruler. Handles mark each medium’s 1/e height and directly edit its real scale height; left/right arrows select a medium and up/down arrows adjust it. Curves remain separately normalised to sea-level density. Sliders/numbers are retained below the diagram.
- **Constituents:** a compact coefficient rail instead of a second graph-card clone.
- **Directional response:** a separate polar Henyey–Greenstein reference using actual Mie anisotropy, normalised to peak strength. Squared radius represents relative phase. This is not a rendered sky or measured radiance.
- **Light response:** three source-to-transmitted optical paths through a shared medium, with real RGB extinction values below. Zero transmission hides the output beam. A live badge distinguishes linked attenuation from bypassed/off diagnostic reference mode.
- **Sky & surface:** separate colour wells bind SkyTint and GroundAlbedo, show the gamma-encoded picker value, and preserve linear RGB storage. Brightness stays a paired slider/numeric input.
- **Horizon shaping:** collapsible dedicated glow/white-line/civil controls. Integration sample budget is a separate compact section.

Charts cache their relevant inputs to avoid recomputing just because unrelated settings or day-cycle time change. No new render pass, shader or texture readback is introduced.

Validation: **177 unit tests pass**, including isotropic/forward/backward phase-shape bounds and normalisation. The updated browser proof verifies removal of the composition pad; recipe slider editing; comparator keyboard and pointer editing; anisotropy; linear-RGB colour writeback; horizon and sample-budget edits; a subsequent real GPU frame; and preserved column, Cloud controls and native Baking, with **0 browser errors**. Three-frame Dawn bring-up is clean.

## Local Light-through-air revision

Only the section before **Sky & surface** was revised after the remainder was approved. The beam illustration is replaced with a compact nested RGB transmission dial, exact channel readouts and a central neutral-light transmission value. That central percentage is computed from linear RGB extinction using luminance weights 0.2126 / 0.7152 / 0.0722; it is not scene telemetry. The two large toggle tiles are now small switch rows with the same original bindings and accessible pressed states. Bypassed/off reference status remains explicit.

Sky/surface colour wells, horizon controls, Medium, the approved atmospheric column and Baking remain unchanged. Full suite: **178 tests pass**; updated browser interaction proof: **0 errors**.
