# Vehicle catalogue validation

Generated 2026-11-03T19:28:09.712Z · Node v22.22.3 · 70/70 checks passed.

| Check | Result | Observed |
|-------|--------|----------|
| unit round trip · Metric | ✔️ pass | largest round-trip error 0.00e+0 |
| unit round trip · Imperial | ✔️ pass | largest round-trip error 9.09e-13 |
| Torque 620 Nm → lb·ft | ✔️ pass | 457.3 (expected 457.3) |
| Power 371 kW → hp | ✔️ pass | 498 (expected 498) |
| Speed 100 km/h → mph | ✔️ pass | 62 (expected 62) |
| Mass 1284 kg → lb | ✔️ pass | 2831 (expected 2831) |
| Pressure 220 kPa → psi | ✔️ pass | 31.9 (expected 31.9) |
| Temperature 92 °C → °F | ✔️ pass | 197.6 (expected 197.6) |
| Temperature 0 °C → °F | ✔️ pass | 32 (expected 32) |
| Length 2720 mm → in | ✔️ pass | 107 (expected 107) |
| linear midpoint | ✔️ pass | evaluated 300 |
| linear clamps below the first point | ✔️ pass | returns the first Y |
| linear clamps above the last point | ✔️ pass | returns the last Y |
| smooth curve passes through every authored point | ✔️ pass | 8 points |
| smooth curve does not overshoot between points | ✔️ pass | largest overshoot 0.00e+0 |
| tangents are finite | ✔️ pass | 8 tangents |
| power curve equals torque × engine speed | ✔️ pass | largest error 0.00e+0 kW |
| peak power is inside the curve range | ✔️ pass | 6575 rpm · 371.0 kW |
| insert returns the new index | ✔️ pass | index 4 |
| insert keeps X ordering | ✔️ pass | 950, 1500, 2500, 3500, 4000, 4500, 5500, 6500, 7800 |
| insert adds one point | ✔️ pass | 8 → 9 |
| insert too close to a neighbour is refused | ✔️ pass | refused within the minimum gap |
| insert clamps Y to the axis | ✔️ pass | clamped to the 800 Nm ceiling |
| relocate clamps X between neighbours | ✔️ pass | X = 3572.0 between 3500 and 4500 |
| relocate clamps to the axis bounds | ✔️ pass | X = 800, Y = 0 |
| relocate never adds a point | ✔️ pass | 10 points |
| remove refuses the last two points | ✔️ pass | 2 points remain |
| remove refuses an out-of-range index | ✔️ pass | index 9 refused |
| CSV has one header and one row per point | ✔️ pass | 9 rows |
| CSV names its axes in the active units | ✔️ pass | enginetorque,rpm,Nm |
| 0–100 km/h is reached | ✔️ pass | 3.06 s |
| 0–100 km/h is plausible for the record | ✔️ pass | 3.06 s · 41.8 m |
| top speed is redline and drag limited | ✔️ pass | 325 km/h |
| braking distance 100–0 is plausible | ✔️ pass | 27.3 m |
| power to weight matches peak power and mass | ✔️ pass | 289 kW/t |
| peak torque matches the authored curve | ✔️ pass | 640 Nm at 4500 rpm |
| launch trace is time ordered and starts at rest | ✔️ pass | 134 samples |
| more torque lowers the 0–100 time | ✔️ pass | 3.06 s → 2.98 s |
| more torque raises the top speed | ✔️ pass | 325 → 342 km/h |
| more mass raises the 0–100 time | ✔️ pass | 3.06 s → 3.32 s |
| more mass lowers the power-to-weight ratio | ✔️ pass | 289 → 234 kW/t |
| four tyres, each enclosed at the start | ✔️ pass | 4 enclosed |
| front and rear pressures differ | ✔️ pass | 186 / 174 kPa |
| detach clears the enclosure and keeps the record | ✔️ pass | TyreFrontRight detached |
| the detached tyre leaves the enclosed list | ✔️ pass | 1 detached |
| attach restores the enclosure | ✔️ pass | all four enclosed again |
| fitting a set replaces the compound | ✔️ pass | Soft · Set B |
| fitting a set resets wear, laps and enclosure | ✔️ pass | wear 0 %, 0 laps, enclosed |
| fitting a set restores the cold pressure | ✔️ pass | 186 / 174 kPa |
| temperature window classifies below, inside and above | ✔️ pass | window 68–96 °C |
| wear status escalates | ✔️ pass | Fresh / Degraded / Worn |
| record contains [vehicle] | ✔️ pass | [vehicle] |
| record contains identity = "GT-04" | ✔️ pass | identity = "GT-04" |
| record contains kerb_mass_kg = 1284 | ✔️ pass | kerb_mass_kg = 1284 |
| record contains [vehicle.drivetrain] | ✔️ pass | [vehicle.drivetrain] |
| record contains gear_ratios = [3.62 | ✔️ pass | gear_ratios = [3.62 |
| record contains [vehicle.engine] | ✔️ pass | [vehicle.engine] |
| record contains redline_rpm = 7800 | ✔️ pass | redline_rpm = 7800 |
| record contains [vehicle.thermal] | ✔️ pass | [vehicle.thermal] |
| record contains [vehicle.aids] | ✔️ pass | [vehicle.aids] |
| record contains [vehicle.curves] | ✔️ pass | [vehicle.curves] |
| record contains engine_torque_points = [[ | ✔️ pass | engine_torque_points = [[ |
| record contains [vehicle.tyres.tyre_front_left] | ✔️ pass | [vehicle.tyres.tyre_front_left] |
| record contains [vehicle.tyres.tyre_rear_right] | ✔️ pass | [vehicle.tyres.tyre_rear_right] |
| record contains enclosed = true | ✔️ pass | enclosed = true |
| record contains [vehicle.pit_lane] | ✔️ pass | [vehicle.pit_lane] |
| record contains detached = ["TyreRearLeft"] | ✔️ pass | detached = ["TyreRearLeft"] |
| the record omits the derived power curve | ✔️ pass | only authored curves are written |
| the record has no NaN or undefined | ✔️ pass | clean numbers |
| torque points travel as an array of pairs | ✔️ pass | engine_torque_points = [[950, 300], [1500, 430], [2500, 560], [3500, 620], [… |

**🟢 all checks passed**
