# Vehicle telemetry validation

Generated 2026-11-03T06:16:44.575Z · Node v22.22.3 · jsdom 24.1.3 · 76/76 checks passed.

The harness builds the real `index.html` in jsdom, switches the dock to Telemetry, then drives the model by hand so
every painted value can be compared with the state it came from. `VehicleCatalogue.js`, `InspectorControls.js`,
`VehicleTelemetry.js` and `VehicleInspector.js` are evaluated in page order.

| Check | Result | Observed |
|-------|--------|----------|
| the lap table has one sample every ten metres | ✔️ pass | 432 samples over 4320 m |
| every sample carries a positive speed limit | ✔️ pass | 23.6–80.5 m/s |
| the hairpin is the slowest point of the lap | ✔️ pass | 85 km/h |
| the straight is the fastest point of the lap | ✔️ pass | 290 km/h |
| a session starts with the authored fuel load | ✔️ pass | 61.2 L of 68 L |
| a session lists four corners | ✔️ pass | 4 corners |
| a zero step advances nothing | ✔️ pass | 0 s |
| thirty seconds of driving stays inside the limiter | ✔️ pass | 6325 rpm |
| thirty seconds of driving stays inside the speed range | ✔️ pass | 200 km/h |
| the gear is one of the authored gears | ✔️ pass | gear 4 |
| the lap clock ran | ✔️ pass | 30.00 s |
| fuel burns while driving | ✔️ pass | 60.72 L |
| the burn leaves laps of fuel | ✔️ pass | 45.7 laps |
| the tyres warmed from the authored temperature | ✔️ pass | 79 °C |
| tyre pressure follows surface temperature | ✔️ pass | 186 kPa |
| wear accumulates, never falls | ✔️ pass | 18.18 % |
| the brake discs heat above ambient | ✔️ pass | 61 °C |
| the traces hold the last twenty seconds | ✔️ pass | 600 samples |
| the model is deterministic | ✔️ pass | 55.6636 m/s · 60.7210 L |
| a paused session does not advance | ✔️ pass | held |
| an empty tank stops the engine | ✔️ pass | 0.00 L · throttle 0 |
| an empty tank is logged once | ✔️ pass | 7 events |
| a snapshot cannot write the session | ✔️ pass | 60.72 L |
| the dock has its own tab beside the inspector | ✔️ pass | 2 tabs |
| switching tabs builds the live dock | ✔️ pass | .telemetry |
| the authoring sections are gone while driving | ✔️ pass | authoring view detached |
| the mode strip says telemetry | ✔️ pass | ModeTelemetry · liveRecordVehicleCurves6Revision0 |
| no uncaught error while building the dock | ✔️ pass | none |
| the dock owns one session | ✔️ pass | session attached |
| the dock starts sampling | ✔️ pass | running |
| the speed card carries its unit | ✔️ pass | 86km/h |
| the gear card is numeric | ✔️ pass | 2 |
| the shift strip has twelve lights | ✔️ pass | 12 lights |
| the pedal bars report a share | ✔️ pass | Throttle35 % |
| the g-force cells carry units | ✔️ pass | Steering0°Lateral0.00 gLongitudinal+0.00 g |
| the fuel card shows litres and the share of the tank | ✔️ pass | 61.2L · 90 % of 68 L |
| the fuel bar draws the level | ✔️ pass | 90% |
| the reserve marker sits at the authored share | ✔️ pass | 12% |
| the fuel card refuses to guess before a lap is measured | ✔️ pass | — measuring the first lap |
| six engine tiles carry units | ✔️ pass | 6 tiles |
| four tyre cards carry pressure, temperature and wear | ✔️ pass | 4 cards |
| the aid chips name the systems | ✔️ pass | TC · 4ABS · 3DRS · AvailableERS · 62 %BIAS · 62 % F |
| the brake temperatures are shown per axle | ✔️ pass | 2 cells |
| the trace card holds a chart | ✔️ pass | .series |
| the dock offers the way back | ✔️ pass | Vehicle inspector |
| the dock paints the speed the model holds | ✔️ pass | 242 shown · 242.5 held |
| the dock keeps sampling while it is shown | ✔️ pass | 10.0 s |
| the speed moved as the lap went on | ✔️ pass | 86 → 242 |
| the fuel bar follows the tank | ✔️ pass | 89.7% |
| the fuel card reports the state after a lap | ✔️ pass | — measuring the first lap |
| the trace chart follows the session | ✔️ pass | 2 paths |
| the speed card converts to miles per hour | ✔️ pass | 151mph |
| the fuel card converts to gallons | ✔️ pass | 90 % of 18 gal |
| the speed card converts back | ✔️ pass | 242km/h |
| pause stops the session | ✔️ pass | paused |
| pause is written on the chip | ✔️ pass | PAUSED · READ-ONLY |
| resume restarts the session | ✔️ pass | running |
| the inspector comes back | ✔️ pass | #Section-Curves |
| the live dock is detached | ✔️ pass | detached |
| the mode strip says inspector | ✔️ pass | Inspector |
| going back keeps the driven session | ✔️ pass | 60.98 L |
| the t shortcut returns to the inspector | ✔️ pass | inspector |
| the t shortcut returns to the dock | ✔️ pass | telemetry |
| watching the car never writes the record | ✔️ pass | record untouched |
| the page record matches the authored defaults | ✔️ pass | defaults unchanged |
| disabled telemetry detaches the dock | ✔️ pass | detached |
| the detached dock explains itself | ✔️ pass | The live dock is detachedVehicle · GT-04 has telemetry disab |
| enabling telemetry attaches the dock again | ✔️ pass | attached |
| detaching clears the timer | ✔️ pass | timer cleared |
| the speed cell carries its family | ✔️ pass | Speed |
| the gear cell carries its family | ✔️ pass | Ratio |
| the fuel level carries its family | ✔️ pass | Volume |
| the dock tyres colour their number by temperature | ✔️ pass | instrument-value tone-live \| instrument-value tone-live \| instrument-value tone-live \| instrument-value tone-live |
| a hot tyre turns the number red | ✔️ pass | instrument-value tone-danger |
| a cold tyre turns the number blue | ✔️ pass | instrument-value tone-cold |
| the dock pins the colour mode on the page | ✔️ pass | colour-labels |

**🟢 all checks passed**
