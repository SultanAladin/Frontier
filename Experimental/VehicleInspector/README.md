# Vehicle inspector prototype

The owner asked for the vehicle inspector in HTML before the C++ step: one vehicle record added to the scene — chassis
and its four tyres, no other geometry — the tyres enclosed by the vehicle and replaceable at a pit stop, cards that
carry units and coloured icons, and **editable curves** so a custom vehicle can be authored. The same dock also carries
the runtime view the owner asked for next: a **live, read-only telemetry dock** for when the car is on track. This is
that inspector. It
sits beside the Render settings inspector (`Experimental/RenderSettingsInspector/` on the render-settings branch) and
reuses its control kit, palette and card language, so one review covers both.

The look follows the owner's dashboard reference (`SourceImages/General UI/`): near-black glass cards, pill tabs with a
raised active pill, big readouts with dimmed decimals and a small unit, coloured status marks, dashed guides and thin
chart lines — plus the Frontier Editor's slider rows, toggle tiles and folded section cards.

## Open it

```sh
python3 -m http.server 8000 --directory Experimental/VehicleInspector
# then http://localhost:8000/
```

`index.html` also opens straight from disk (classic scripts, relative paths). DM Sans comes from the bundled woff2
files; the `EngineContent/FontArchives` path is tried first, for a checkout where `Automation/PrepareFonts.py` has run.

## The record the scene gains

| Record               | What it adds                                                    |
|----------------------|-----------------------------------------------------------------|
| `Vehicle · GT-04`    | One record: mass, dimensions, drivetrain, engine, thermal, aero |
| `Tyre · FL/FR/RL/RR` | Four records **enclosed** by the vehicle, each replaceable      |
| Everything else      | Unchanged. No body geometry, no accessories, no new lights      |

A tyre that is detached keeps its record and loses the enclosure; it moves to the **Pit lane** group in the Outliner and
is drawn apart in the viewport with a dashed link back to the axle it left. Fitting a set writes the four records back.

## Files

| File                   | Contents                                                                     |
|------------------------|------------------------------------------------------------------------------|
| `index.html`           | Outliner, viewport with the tyre layout, Log Stream and inspector            |
| `VehicleInspector.css` | Palette tokens and every card's look                                          |
| `InspectorControls.js` | Shared controls: rows, tiles, cards, series charts, the curve editor, scroll  |
| `VehicleCatalogue.js`  | Units, curve maths, tyre compounds, the specification, estimates, TOML record |
| `VehicleInspector.js`  | The page: outliner, tyre layout, hero, eight sections, the tyre inspector     |
| `VehicleTelemetry.js`  | The runtime dock: the lap model, the live cards, the sampling loop           |
| `Fonts/`               | DM Sans 300 / 400 / 500 (SIL OFL, licence beside the faces)                   |

## Sections

| Section                 | Drives                                                                     |
|-------------------------|----------------------------------------------------------------------------|
| Hero and quick controls | Dyno sheet, rated-torque status, six aid tiles, four summary cards          |
| Chassis and identity    | Kerb mass, wheelbase, tracks, centre of gravity, ride height, fuel, layout  |
| Engine and drivetrain   | Displacement, idle, limiter, rated torque, boost, inertia, fuel burn, gears |
| Curves                  | The curve library and editor: five authored curves, one derived power curve |
| Thermal                 | Coolant / oil / disc / tyre gauges, targets and the coolant curve editor    |
| Tyres                   | Four tyre cards, pit-stop actions, enclosure, compound, pressure            |
| Aero and brakes         | Cd, area, downforce, brake torque and balance, rolling resistance, wing trim |
| Performance estimate    | 0–100 km/h, top speed, 100–0 distance, peak power, launch trace chart        |
| Vehicle record          | The TOML record, reset, copy, torque CSV                                    |

## Curves

| Curve                    | Axes                      | Editable | Source                   |
|--------------------------|---------------------------|----------|--------------------------|
| Engine torque            | engine speed → torque     | yes      | authored points          |
| Engine power             | engine speed → power      | no       | torque × ω, sampled live |
| Brake torque             | road speed → brake torque | yes      | authored points          |
| Coolant temperature      | engine load → °C          | yes      | authored points          |
| Oil temperature          | engine speed → °C         | yes      | authored points          |
| Tyre surface temperature | slip angle → °C           | yes      | authored points          |

Smooth interpolation is Fritsch–Carlson monotone, so the line never overshoots between the points the owner placed.
Every derived readout — hero dyno, peak power and its speed, launch trace, top speed, braking distance, the exported
record — reads these same curves, so moving one point changes all of them.

| Gesture                                     | Effect                                           |
|---------------------------------------------|--------------------------------------------------|
| Drag a control point                        | Move it; X stays between its neighbours          |
| Shift-click or double-click empty space     | Add a point there                                |
| Right-click a point, or `Delete`            | Remove it (a curve keeps at least two points)    |
| Arrow keys on the focused chart             | Nudge the selected point; `Shift` nudges further |
| `Point X` / `Point Y` pills, or their track | Type or drag an exact value with its unit        |
| `Add point`                                 | Insert at the middle of the widest gap           |
| `Interpolation`                             | Smooth (monotone) or straight segments           |

## The live dock (runtime)

While the car is on track the owner does not author anything: they watch. The same dock therefore carries a second
view, **Telemetry**, opened from the tab above the inspector or with `t`. It is a read-only component parented to the
vehicle the way the tyres are, attached to the UI at runtime for debugging: it reads the record, never writes it, and
switching back to **Inspector** returns to the same session where it was left.

| Card              | Shows                                                                     |
|-------------------|---------------------------------------------------------------------------|
| Header            | `GT-04 · live`, `DEBUG · READ-ONLY`, the pause control                    |
| Speed and gear    | Speed with its unit, the gear, the twelve-light shift strip, rpm, limiter  |
| Pedals and g-force| Throttle and brake shares, steering angle, lateral and longitudinal g     |
| Fuel              | Level, share of the tank, bar with the reserve marker, flow, laps of fuel  |
| Engine and fluids | Coolant, oil, oil pressure, boost, power, torque — six cards with units    |
| Tyres and brakes  | One card per corner: pressure, surface temperature, wear, compound colour  |
| Aids              | TC and ABS working states, DRS, ERS charge, brake bias                     |
| Speed trace       | The last twenty seconds, drawn by the same chart control as the inspector  |

The dock reads three things from the record and nothing else:

| Record key                    | Meaning                                                      |
|-------------------------------|--------------------------------------------------------------|
| `[vehicle] fuel_capacity_l`   | The tank, shared with the inspector's fuel card               |
| `[vehicle.fuel] load_l`       | What the session starts with; `reserve_percent` puts the mark |
| `[vehicle.telemetry] rate_hz` | Sampling rate, 30 Hz by default                               |
| `aids.telemetry`              | The gate: off detaches the dock and offers to attach it again |

### The model behind the numbers

| Signal                  | How it is produced                                                       |
|-------------------------|--------------------------------------------------------------------------|
| Speed and lap           | A 4 320 m loop of straights and fixed-radius corners, turned into a speed |
|                         | table by a braking pass and a traction pass through the wheel force, the  |
|                         | traction circle and drag; the car follows it through the same forces      |
| Gear and rpm            | The lowest gear that stays under the limiter, through the authored ratios |
| Fuel                    | The record's full-load burn at the modelled load, integrated; reserve at  |
|                         | the authored share, range printed only after a lap has been measured      |
| Coolant, oil            | The authored temperature curves at the modelled load and speed            |
| Tyre surface and core   | The authored surface curve at the modelled slip, with a lagging core      |
| Pressure and wear       | Pressure from surface temperature, wear from lateral load and compound    |
| Brakes                  | Heat from pedal and speed, cooling with airflow, per axle and per corner  |
| Aids and wing           | TC and ABS from load and slip thresholds, DRS from the aid and the        |
|                         | straight, ERS from throttle and braking                                   |

The dock is a debugging instrument, not a tyre model: it has no slip solver, no carcass and no measured data. Its
consumption comes from the record's own burn figure, so a record with a different engine reads differently.

## Colour coding

Text is colour coded so a reading can be recognised before it is read: **one hue per quantity family**, and **one tone
per state**. The switch in the inspector header — `Labels`, `Units`, `Plain` — and the `c` key decide what wears the
family hue. Values keep their state tone in every mode.

| Family               | Hue                | Appears on                                   |
|----------------------|--------------------|----------------------------------------------|
| Volume, litres, flow | Blue `#7aa7e0`     | Fuel capacity, loaded fuel, flow, range      |
| Temperature          | Amber `#e8a84a`    | Coolant, oil, disc temperatures              |
| Pressure, boost      | Teal `#8fc7b8`     | Tyre pressures, oil pressure, boost          |
| Speed                | Mint `#6fc7a8`     | Speed, top speed, 0–100                      |
| Torque               | Amber `#e8a84a`    | Torque cards, the dyno readout               |
| Power                | Steel `#9fb4d6`    | Power, power-to-weight, downforce            |
| Mass, length         | Violet `#b9a6e8`   | Kerb mass, wheelbase, ride height            |
| Engine speed         | Blue-grey `#8fb0d6`| Limiter, peak-power speed, rpm               |

Values that carry a state move along one scale — the owner's "orange to red to yellow to green":

| Tone   | Colour             | Reads as                                              |
|--------|--------------------|-------------------------------------------------------|
| Cold   | Blue `#7aa7e0`     | Below the tyre's window, or a fluid below target       |
| Live   | Green `#93dcae`    | In the window, in the target band                      |
| Warm   | Yellow `#e8c24a`   | Approaching the top of the window                     |
| Hot    | Orange `#e8894a`   | Over the window, still inside the safe band            |
| Danger | Red `#e58b8b`      | Over the safe band                                     |

A tyre's number is its pressure, and its colour is its surface temperature against its own compound window — so a
fresh set from the pit lane reads cold, a scrubbed set reads green, and a set cooked over the window walks to orange
and red. Colour is a reading aid, never a record field: switching it writes nothing.

## Units

One pill switches the whole inspector between **Metric** (kg · kW · Nm · kPa · °C) and **Imperial**
(lb · hp · lb·ft · psi · °F); `u` does the same. Every card, slider, chart axis, tooltip and the CSV carry the
active unit. The record always stores the canonical metric quantity, so a unit switch is a display concern and never
edits the vehicle.

## What is real and what is estimated

| Shown                               | Source                                                                    |
|-------------------------------------|---------------------------------------------------------------------------|
| Cards, sliders, tiles, curve editor | The prototype's own controls; values are the record's                      |
| Dyno, launch trace, curve charts    | Drawn from the curve maths in `VehicleCatalogue.js`                        |
| 0–100, top speed, braking distance  | A longitudinal model: torque curve, gearing, traction circle with weight   |
|                                     | transfer, drag, rolling resistance — a prototype estimate, not the C++     |
|                                     | tyre solver                                                                |
| Viewport "Top · layout"             | A schematic of the authored wheelbase, track, tyre width and compound.     |
|                                     | **No C++ capture of a vehicle exists**, so nothing pretends to be a render |
| Log Stream                          | The prototype's own event log                                              |
| Live dock, speed and lap            | The lap model above: a synthetic circuit, not a captured session           |
| Live dock, fuel and temperatures    | Integrated from the record's burn figure and the authored curves           |
| Live dock, range in laps            | Measured by the dock after a completed lap; the dock says so before that   |

## Next step

The tyre editor is still the next authored surface; the live dock is the runtime half of the same component family.

### In C++

1. `VehicleTelemetry` as a UI component parented to the vehicle record, sampling `VehicleState` each frame at the rate
   the record asks for, drawn with the same control kit — never a scene object, never a record write.
2. The dock's kill switch is the record's own `aids.telemetry`, so a build can ship without it.

### The tyre editor

Not in this prototype, on purpose: the owner asked for the inspector first.

1. Per-compound carcass, thermal and wear curves, with the temperature window as an editable band, not two numbers.
2. Pit-stop sets with stint history, scrub and blanket temperature, so a fitted set arrives with a past.
3. Tyre pressure and camber per corner interacting with the thermal model.
4. Then the C++ step: `VehicleSpecification` (TOML), the tyres as `SceneDirectory` records with an enclosure token, and
   these sections as units of one `VehicleInspector` built from the shared controls.

No GitHub Actions are authorised for this work.
