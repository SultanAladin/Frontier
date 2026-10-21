# Wind · shared weather force instruments

The Wind Inspector remains **Wind / ForceField / Forcefield** in the Outliner. Its new, Wind-only design has **Flow / Variability / Altitude / Receivers** categories with the common accessible tab navigation. Other approved inspectors, wind simulation, weather shaders, placement and accumulated density travel are unchanged.

## Instruments

- **Flow:** speed readout, source-bearing dot and parallel travel streamlines on an interactive compass. The stored `CloudWindBearing` is direction of travel; the exposed meteorological bearing is `(travel + 180) mod 360`. Pointer drag changes source bearing and speed. Left/right edit bearing, up/down edit speed; Home restores a north-source 7 m/s reference. Paired number/slider controls retain direct native settings bindings and the canonical cloud range **0–40 m/s** (the old Wind card unnecessarily capped this at 32).
- **Variability:** exact two-sine gust formula with its conservative ±gust envelope; fixed-settings/zero-start-phase reference rather than invented history. Separate normalized X/Y/Z turbulence traces show the actual frequencies and vertical 12% factor, with base-speed velocity bounds. Gusts scale actual turbulence in the running field.
- **Altitude:** steady speed/shear/veer vector ladder at six heights through 3 km. Direction glyphs are plan views at each labelled altitude, not trajectories tilted vertically. Gust and turbulent components are explicitly excluded from this reference.
- **Receivers:** actual follow-wind flags for Clouds, Local cloud, Local fog and **GPU precipitation**, including precipitation drift strength. Receiver enable state is separate from linking. Local fog's unlinked authored drift is stated explicitly. Static height fog is not presented as an advected medium; tyre smoke/fire uses its separate gas-wind controls.

The old card's “Precipitation is deferred” statement was incorrect for the current implementation; `PrecipitationSequence.Prepare` already uploads shared wind plus shear/veer/turbulence and follow/drift settings to its GPU field.

## Honesty and performance

Configured references remain visible while Wind is off. The **Live base vector** is a separate readout of `Host.Clouds.Wind.Velocity`, including the current gust clock but excluding altitude/turbulence; it is not substituted with the base-speed slider. Diagnostic plots neither advance the shared clock nor rewrite offsets. Live steady profiles and integrated offsets remain owned by the existing `WindSequence`.

Reference plots cache relevant settings and skip hidden panes. Only the compact live base-vector readout follows runtime wind changes. The Wind inspector's refresh key also includes actual precipitation component settings, so external component edits are not masked by the environment-settings cache. No animated stream sweep, new worker, shader, GPU pass or claimed desktop FPS improvement is introduced.

## Validation

- **208 tests pass**, including direct diagnostic-versus-`WindSequence` velocity comparisons for all three following density receivers, bearing conversion, gust frequency/envelope, altitude shear/veer, disabled-field isolation and canonical editing bounds.
- Browser proof: all four categories; numeric and pointer/keyboard force edits; setting conversions; gust/turbulence/shear/veer; all receiver follow switches; precipitation drift; real GPU parameter follow bit; zero diagnostic clock advancement; keyboard tabs; no page/console errors. A real exhibit frame and submitted GPU work complete after editing.
- Flow, altitude and receiver screenshots visually reviewed. Browser screenshots/results stay outside Git in `/home/user/wind-cards-proof/`.

Implementation: `Experimental/VehicleHmiPrototype/Source/NativeWindPanel.js`, scoped `Style/WindInstruments.css`, `Tools/WindInspector.test.mjs` and `Tools/WindInspectorBrowserProof.mjs`.
