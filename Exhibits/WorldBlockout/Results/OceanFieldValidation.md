# Ocean field validation

Generated 2026-11-03T09:48:50.575Z · Node v22.22.3 · 54/54 checks passed.

The harness requires `Experimental/WorldBlockout/OceanField.js` and exercises the sea state, the Saint-Venant patch, the
Courant limit, volume and energy, wave propagation away from a seeded bump, the swell forcing, the probes and the
near/far blend. Depth comes from the terrain SDF, so the patch is water only where the planet says water is.

| Check | Result | Observed |
|-------|--------|----------|
| the ocean record is named after the planet | ✔️ pass | Terra-01-Ocean |
| gravity is real | ✔️ pass | 9.81 m/s² |
| the sea level matches the planet | ✔️ pass | 0 m |
| the sea state has nine trains | ✔️ pass | 9 waves |
| the swell is a couple of metres | ✔️ pass | 2.08 m |
| wavelengths run long to short | ✔️ pass | 44 → 821 m |
| amplitudes fall away with wavelength | ✔️ pass | 0.35 → 0.11 m |
| the sea state is deterministic | ✔️ pass | identical trains |
| the spectral field is finite | ✔️ pass | -0.465 m |
| the spectral field stays inside its envelope | ✔️ pass | -0.465 m of 2.08 m |
| the spectral field repeats in time | ✔️ pass | equal |
| the spectral field has a slope | ✔️ pass | 0.035 |
| the longest train follows deep-water dispersion | ✔️ pass | period 5.29 s |
| the spectral field moved after a period | ✔️ pass | oscillates |
| the patch has the cells it was asked for | ✔️ pass | 128 |
| the patch cell is in metres | ✔️ pass | 12.50 m |
| the patch is square in kilometres | ✔️ pass | 1.6 km |
| the patch reads its depth from the terrain SDF | ✔️ pass | 0–29 m |
| every cell is either wet or dry | ✔️ pass | binary |
| the patch counts its wet cells | ✔️ pass | 14138 cells |
| dry cells have no depth | ✔️ pass | no wet water |
| celerity clamps to the solvable depth | ✔️ pass | 29.71 m/s at 4800 m |
| the Courant step follows the celerity | ✔️ pass | 0.3362 s |
| settling advances the clock | ✔️ pass | 200.0 s |
| the swell drives the surface | ✔️ pass | peak 2.37 m |
| the surface is finite everywhere | ✔️ pass | finite |
| the water has energy | ✔️ pass | 2007.7 MJ |
| the volume holds through the swell forcing | ✔️ pass | 0.085% drift |
| the volume is the depth times the area | ✔️ pass | 0.025 km³ |
| a frame is one substep at this scale | ✔️ pass | 1 substeps |
| a frame advances by the frame time | ✔️ pass | 0.0333 s |
| the solver reports its step | ✔️ pass | 0.0333 s |
| four hundred steps stay finite | ✔️ pass | finite |
| the surface keeps moving | ✔️ pass | peak 1.92 m |
| a long run does not gain or lose water | ✔️ pass | 0.064% drift |
| a bump spreads outwards | ✔️ pass | neighbours at 0.014 m after 40 steps |
| the spread is symmetric | ✔️ pass | -0.007 / 0.000 |
| a deep-water run conserves volume | ✔️ pass | 8.9e-4% drift |
| probes are recorded | ✔️ pass | 2 probes |
| probes hold a history | ✔️ pass | 32 samples |
| probes report their own height | ✔️ pass | finite |
| a probe reports whether it is wet | ✔️ pass | 2 wet |
| the blend is closed near the viewer | ✔️ pass | weight 0 |
| the blend is open past the far radius | ✔️ pass | weight 1 |
| the blend rises monotonically | ✔️ pass | monotonic |
| the blend is smooth at the seam | ✔️ pass | continuous |
| the surface is near field at the focus | ✔️ pass | weight 0, -0.02 m |
| the surface is far field away from the focus | ✔️ pass | weight 1 |
| the surface blends between the two | ✔️ pass | weight 0.58 |
| the record names the solver | ✔️ pass | saint-venant 2.5D |
| the record carries the sea state | ✔️ pass | 9 wave rows |
| the record has no NaN or undefined | ✔️ pass | clean |
| the summary reports the patch | ✔️ pass | 128² cells |
| the summary reports the celerity | ✔️ pass | 16.7 m/s |

**🟢 all checks passed**
