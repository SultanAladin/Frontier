# Inspector parity — HTML (pinned) vs native C++

Source of truth: `Experimental/VehicleHmiPrototype` @ `6afb6f0` on
`arena/e9bd7db5-frontier` — `Source/EditorShell.js` (`ConstructInspector`),
`Style/FrontierEditor.css` (`#inspector` … `.inspector-footer`). Every value
below is transcribed from that CSS; no value was invented.

| HTML | CSS value | Native (`SceneInspectorPanelPresentation.cpp`) | Status |
|---|---|---|---|
| `#inspector` background | radial `#1b1b1b → #171717 42% → #141414`, `border-left 1px #2a2a2a` | flat mid-stop `#171717` + 1 px `#2a2a2a` left edge | 🚧 gradient approximated (ImDrawList has no radial fill) |
| `.inspector-top` | 44 px, padding `0 18px`, bottom hairline, crumbs 11 px `#bfbfbf`, group `#717171`, `.save-status` 10 px `#7c7c7c` "Changes apply live" | same heights, insets, sizes, colours, captions | 🟢 |
| `.inspector-content` | padding `20px 16px 18px`, scroll | same insets inside a scroll child | 🟢 |
| `.eyebrow` | 9 px, uppercase, 1.7 px tracking, `#898989` | classification upper-cased, tracked draw | 🟢 |
| `.object-header h1` | 19 px weight 400 | 19 px name | 🟢 |
| `.enabled-pill` | radius 25, `#252525`/`#3a3a3a`, 7/11 px padding, 5 px dot (`#3ec46d` on, `#818181` off), 10 px caption | same geometry; toggles the record's tile "Visible" binding | 🟢 |
| `.section-label` | "PROPERTIES" 9 px/1.5 px tracking `#8d8d8d`; description right 10 px `#696969` | same | 🟢 |
| `.property-switch` tiles | flex row gap 8, radius 16, `#202020`/`#323232`, padding 12/6/10, icon 30 px radius 10 (`#2f6d45` on / `#cc7673` off) with a 17 px EditorIcons glyph, name 10 px, state 8 px/1.2 px tracking | same geometry and colours; glyph subset ported (Eye, Zap, Sparkles, Aperture, Layers, Scan, Monitor, Activity, Bug; other names fall back to the Sparkles star) | 🚧 glyph subset |
| `.cards` / `.card` | 1 column gap 12; gradient 125° `#222→#1f1f1f`, 1 px `#2f2f2f`, radius 18, padding 18; heading glyph 16 px + title 12 px `#c9c9c9`, explicit card index 9 px `#4f4f4f`; subtitle 9 px/1.4 tracking `#737373` | same incl. heading glyph and explicit per-card index; gradient drawn as flat `#222` start stop | 🚧 gradient approximated |
| Slider (`SliderControl.css .frontier-slider`, the control `EditorControls.Slider` actually builds) | margin-top 22; head margin-bottom 12 (label 11 px `#a5a5a5`; value capsule `#080808` radius 20, height 24, min-width 78; number 11 px `#e0e0d8` right over 54 px; unit 10 px `#b2b2aa`); track 22 px radius 20, fill `#555650` over `#171818`; thumb 20 px `#e2e4d8`; optional `.range-labels` 9 px `#848484` | same geometry and colours, digits from the declaration, custom range labels, drag writes the bound record value (clamped/stepped in the view-model). 🔴 An earlier revision of this panel wrongly used the legacy `input[type=range]` CSS from FrontierEditor.css — corrected 2026-10-28 after owner review | 🟢 |
| `.metric` / `.small-pill` | value 38 px Light `#dfdfdf`, letter-spacing −1.8 px, + unit 14 px `#7d7d7d`; pill 9 px, padding 5/9, radius 20 (`live` `#9be3b5` on `#1d2c22`/`#2f6d45`, `warm` `#f0c27a` on `#2c2416`/`#6a5124`) | same, incl. per-glyph tracked draw (−1.8 px) so `3B GI · 2B Refl · M=2` fits before the pill; value clipped before the pill as the card's `overflow: hidden` clips it in the browser | 🟢 |
| `.readout` (`<pre>`) | mono 10 px/1.6 `#8f8f8f`, margin-top 12, padding-top 12, hairline above | `TextBlock` control: same offsets, hairline and line height; Regular face stands in for mono (no mono face resolved in this panel) | 🚧 typeface only |
| `.muted` | 10 px `#7d7d7d`, line-height 1.55, margin-top 8 | same, word-wrapped | 🟢 |
| `.stats` | margin-top 16, padding-top 14, hairline; label 9 px `#8a8a8a`, value 17 px Light `#dedede`, unit 10 px `#858585`, gap 12 | same | 🟢 |
| `.select-field` | full width, `#191919`/`#393939`, radius 9, padding 9, 11 px `#c8c8c8`, label above | field pixels same; a click **cycles** the choice instead of opening a dropdown menu (no dropdown surface wired to this panel yet) | 🚧 interaction only |
| `.switch-row` + `.toggle` | label 11 px `#c4c4c4`, note 9 px `#727272`; toggle 40×23 radius 23, on `#3ec46d`/`#53d684` knob `#f4f4f4` at +17, off `#3a3a3a`/`#484848` knob `#9c9c9c` | same | 🟢 |
| `.segmented` | padding 4, gap 4, radius 11, `#121212`/`#262626`; button 10 px `#8b8b8b`, active `#2e2e2e`/`#f0f0f0` radius 8 | same | 🟢 |
| `.action-button` | radius 10, `#262626`/`#383838`, 10 px `#d0d0d0`, padding 9/8 | same | 🟢 |
| `.receipt-list` readout | 10 px `#8e8e8e` label, `#d4d4d4` value right | same | 🟢 |
| `.inspector-footer` | 9 px `#7a7a7a`, 4 px dot, "Changes apply in real time" · "`Name` / `Type`", margin-top 18 | same | 🟢 |
| Empty selection | HTML always has a selection (defaults to first record) | "Select a record in the Outliner." placeholder | 🚩 deliberate: no record is auto-selected natively |
| Category tabs (`InspectorCategories.js`) | tablist over per-page panes, roving tabindex, ←/→/Home/End | not in the generic Inspector: the HTML only uses categories inside the native-environment pages, which port with those pages (plan §10.3) | ⏳ deferred with the environment pages |
| Fonts | DM Sans (300/400/500) 13 px base | SDK host: DM Sans Light/Regular/Medium via `ResolveKitFaces()`. CPU capture: DMSans-Regular TTF (googlefonts/dm-fonts) as the atlas face — all weights render Regular in the capture only | 🚧 capture weights only |
| Header identity | crumbs second item = entity **Group**; eyebrow = entity **Type** | `DeclareEditorIdentity(Type, Group)`; classification is only the fallback | 🟢 |
| Enabled pill | only for entities with a `Toggle` | only when the record declares an Eye visibility tile | 🟢 |

## Further pages (2026-10-28)

The same capture harness now renders the remaining HTML inspector pages
as native records (`InspectorCapture <out> [font] [height] [page]`).
Control lists, ranges, units and defaults are transcribed from the
pinned sources; the bespoke HTML dashboards for the environment pages
(`NativeEnvironmentPanels` and friends) use their own CSS families and
canvas instruments, which the generic card vocabulary does not draw yet
— the controls below are exact, the instruments are the deviation.

| Page (HTML source) | Cards / controls native | Capture | Missing bespoke pieces |
|---|---|---|---|
| Sun (`BuildSunInspectorDashboard` — the entity's actual builder) | bespoke `sun-observatory` dashboard drawn natively in the harness (`DrawSunDashboard`, all four panes): Overview (Solar-vector compass instrument, KPI readouts, quick tiles, Direction-controls sliders, preset row), Daylight (Solar-day curve + rise/set/above-horizon computed from the seeded date/location, Time & location), Light & colour (Light source, Sun Tint pill, Source-spectrum Planck curve with λ-max + stats), Disc & optics (Apparent-disc diagram, Connected-optics link) | `Capture/inspector-sun.png`, `inspector-sun-daylight.png`, `inspector-sun-light.png`, `inspector-sun-disc.png` | the day curve uses a spherical solar model (the HTML uses its `SolveCelestial` ephemeris); diagram glow gradients are approximated with flat alpha circles |
| Moons (`BuildMoonInspectorInfographics`, `moon-cards` CSS) | bespoke dashboard drawn natively (`DrawMoonDashboard`): ne-header, Moon settings card (instance select + Visible/Follow-sky tiles at the CSS positions), InspectorCategories tablist (Phase active), Lunar phase card (29 px age summary, Full-moon stop pressed as seeded, phase slider, note) | `Capture/inspector-moon.png` | the phase Diagram is the HTML's registered-texture sphere; the capture shades a phase disc instead (texture not in the harness). Placement/Light & size/Bodies panes not yet drawn. An earlier generic-vocabulary Moon page was a deviation — removed |
| Lens Flare (`BuildNativeFlare`, `lens-optics` CSS) | bespoke dashboard drawn natively (`DrawFlareDashboard`): lo-header + Enabled/Export actions, Optical composite instrument (plate shaded from the HTML's source position and ghost-layout law), InspectorCategories (Compose active), Light stack (preset select + Intensity/Spread/Chromatic sliders + note) | `Capture/inspector-flare.png` | the HTML composite is rendered by a worker from the real flare kernel; the capture plate is a geometric stand-in. Ghosts/Halo/Rays & streaks panes not yet drawn. An earlier generic-vocabulary Flare page was a deviation — removed |
| Wind — force field (`BuildWindField`, `wind-instruments` CSS) | bespoke dashboard drawn natively (`DrawWindDashboard`): wi-header + ● On power, InspectorCategories (Flow active), Direction-of-travel card (53 px `7.0` summary, 640×480 compass Plot replicated from the HTML draw code, Base speed/Bearing-from sliders, note), live-vector line | `Capture/inspector-wind.png` | Variability/Altitude/Receivers panes not yet drawn. An earlier generic-vocabulary Wind page was a deviation — removed |
| BVH & distance field (`InspectorPages.BuildField`) | 2 cards, 9 controls (metric/stats/muted/toggles/select/slider) — this page genuinely uses the generic EditorControls vocabulary | `Capture/inspector-field.png` | field readout lines (host telemetry) |
| Surface cache (`InspectorPages.BuildCards`) | 1 card, 3 controls — generic vocabulary page | `Capture/inspector-cards.png` | depot readout lines (host telemetry) |
| Particles (`ParticleWorkspace.BuildParticleInspector` + `PrecipitationSequence`) | 2 cards, 28 controls: precipitation sliders/toggles/actions + authoring actions/readouts — generic vocabulary page | `Capture/inspector-particles.png` | precipitation Category/Budget selects — their option lists live in `ReferenceApps/…/CelestialReference.js`, not committed in the pinned tree |
| Height Fog (`BuildFogInspectorDashboard`, bespoke `fog-dashboard`) | bespoke dashboard drawn natively (`DrawFogDashboard`, Visibility pane): fg-header + Off power, categories, "A walk into the fog" transect (86.9 % retained readout and target fading computed from the HTML's `FogPathTransmission` law on the seeded settings), probe sliders, status line | `Capture/inspector-fog.png` | Medium / Colour & sun panes and the Atmospheric-fog variant not yet drawn |
| Derez · VFX (`BuildDerez`) | 4 cards, 28 controls (preset segmented, 9 cube sliders, glow/recovery) — generic-vocabulary page | `Capture/inspector-derez.png` | colour pickers render as hex readouts (no colour control in the generic vocabulary yet); ActionRow buttons stack vertically |
| Fracture (`BuildFractureGlobal`) | 3 cards, 21 controls (props metric/stats, debris sliders, bake actions) | `Capture/inspector-fracture.png` | per-prop pages (`BuildProp`, material-chip grid) not yet ported; ActionRow buttons stack |
| Training arena (`BuildTraining`) | 2 cards, 13 controls (the pinned drill list: Skidpad/Jump/Smash alley/Slalom) | `Capture/inspector-training.png` | drill rows render as readout + Start action, not the HTML's `.drill-row` |
| Cones (`BuildCones`) | 1 card, 4 controls | `Capture/inspector-cones.png` | live metrics seeded at rest (0 displaced/moving) |
| Proving grounds (`BuildLevel`) | 2 cards, 9 controls (six zone link-buttons as actions, scene-budget stats) | `Capture/inspector-level.png` | link buttons render as action buttons |
| Stress test (`BuildStress`) | 1 card, 8 controls (pool actions, motion segmented) | `Capture/inspector-stress.png` | pool counts seeded at rest |
| Diagnostics (`BuildDiagnostics`) | 6 cards, 18 controls (recorder actions/readouts, frame/CPU/GPU/context/events) | `Capture/inspector-diagnostics.png` | canvas Plots (frame charts, GPU heatmap) not drawn; readout lines seeded with the HTML's empty-state texts ("Waiting for frame samples", "No settings snapshot") |
| Geometry clusters (`BuildClusterInspector`) | 1 card, 6 controls (culling toggle, leaf-budget segmented, overlay, rebuild, readout) | `Capture/inspector-clusters.png` | GPU readback values seeded at rest |
| Not yet ported | Vehicle (`BuildVehicleSystemsInspector` + Cabin/Tyre-marks sub-pages), Smoke & fire (`BuildGas`, catalogue-generated), per-prop Fracture pages, Clouds / Local Cloud / Local Volumetric Fog / Atmosphere / Stars / Lightning & thunder (bespoke environment dashboards), Moon Placement/Light & size/Bodies panes, Flare Ghosts/Halo/Rays & streaks panes, Wind Variability/Altitude/Receivers panes, Fog Medium/Colour & sun panes + Atmospheric-fog variant | — | each lands as its own dashboard draw / record, following the Sun pattern |

## Evidence

- Portable: `SceneDirectoryValidation` 56/56 Development, 56/56 Editor
  profile, 17/17 Shipping; Shipping binary has zero
  `SceneInspector`/`RecordEditor` symbols (`nm`).
- SDK compile: `SceneInspectorPanelPresentation.cpp` clean under
  `g++ -std=c++20 -Wall -Wextra -Werror -DFRONTIER_WITH_INTERFACE_SDK`
  against upstream `Engine/Interface` + ImGui 1.92.7 + toml++ 3.4.0.
- Rendered: `InspectorCapture.cpp` (CPU rasterizer, same recipe as
  `Exhibits/LogStream/PanelCapture.cpp`) at the HTML column width 380 px,
  rendering the HTML **Render settings** entity (all eight `BuildRender`
  cards — Clustered geometry, RTX·ReSTIR 01, Ray-traced reflections 02,
  Resolution & quality 03, Global illumination·ReSTIR 04, Exposure &
  image 05, Debug views 06, Frame timing 07 — 53 controls,
  `IlluminationHost` defaults) declared as a native record —
  `Capture/inspector-render.png` (first 800 px) and
  `Capture/inspector-render-full.png` (full 4 080 px scroll height);
  `Capture/inspector-vehicle.png` keeps the generic `SceneRecord` page.
  Production draw code; only the rasterizer and font weights are
  substituted. The capture atlas extends ImGui's default glyph ranges
  with the symbols the HTML text uses (≤ → –), which the production
  host's `TypefaceRegistry` must likewise cover.
- Not yet: a browser screenshot of the HTML for pixel-diffing (no
  Chromium in this sandbox — npm Chromium download is outside the
  reachable hosts), OS-window run (Windows SDK build), per-page inspector
  families from plan §10.3 (Atmosphere, Vehicle Systems, …) — each lands
  as its record type ports, declaring its own specification in its
  constructor.
