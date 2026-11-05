# Vehicle Physics

Started 2026-11-04 (21st request). The owner asked for Jolt with vehicle physics, tested so the slip and tyre dynamics
are correct, the missing pieces completed (supercharger, tyre pressure, tyre temperature and the rest), three tyre
soft-body models on the vehicle (spring, XPBD, 3D FEM) shown deforming while the car runs, the vehicle inspector, a
register of every world object, an adapter that brings every object into Jolt (fractures stay static bounds), and a
proper physics thread with sub-steps.

## Sources

| Source                                                        | What it holds                                                    |
|---------------------------------------------------------------|------------------------------------------------------------------|
| `SultanAladin/GRIT` `source-only`, `VehicleFramework/`        | The original specifications (tyre, engine, supercharger, …) and  |
|                                                               | the solver's documents; `VehicleSolver.cpp` itself is not stored |
| `SultanAladin/Frontier-` `3f14808`, `Engine/PhysicalDynamics` | Project Drive: the GRIT translation (solver, Pacejka, transient  |
|                                                               | slip, drivetrain, brakes, aero, thermal, struts), its thread     |
| `streamlinkinbox/Frontier` `arena/5ec53579-frontier`          | Project Drive imported (126 upstream checks identical), the      |
|                                                               | VehicleHMI tyre solvers and their validation exhibits            |
| `SultanAladin/Frontier` `arena/ca09b37f-frontier`             | `Experimental/VehicleInspector/`, the HTML inspector prototype   |

## Audit

| Area                    | Found                                                                                   |
|-------------------------|-----------------------------------------------------------------------------------------|
| GRIT solver             | Only documents and specifications; Project Drive is the reference translation          |
| Supercharger            | Ported (twin-screw, bypass valve, parasitic drag); no charge-air temperature            |
| Tyre temperature        | Three heat capacities and a gas law, but off by default and with no cold-grip window    |
| Tyre pressure           | Stiffness follows the gas pressure, but the carcass volume is rigid; no wear            |
| Fuel, fluids, aids      | Fuel deferred (constant mass); no coolant or oil temperature; no traction control       |
| Steering                | Both front wheels take one angle (no Ackermann geometry)                                |
| Tyre soft bodies        | Demo XPBD projection and Hex8 solid return a prescribed spring force (shape only); the  |
|                         | 84-particle XPBD lattice carries load through its constraint reactions                  |
| Renderer                | `VehicleVisual.slang` already deforms tyres (analytic spring path and a 32 × 5 nodal    |
|                         | field); body and wheels are occupants 0–4, switched off by `OmitVehicle`                 |
| Physics stepping        | `PhysicsExtension::Step` runs on the caller's thread; there is no physics thread       |

## Decisions

| Question           | Answer                                                                                         |
|--------------------|------------------------------------------------------------------------------------------------|
| Vehicle core       | Project Drive in `Engine/PhysicalDynamics/Vehicle/`, renamed to the naming rules; the upstream |
|                    | suites and a trace prove the renamed build behaves exactly like the original                   |
| Host               | Jolt: the chassis is a Jolt body (sprung mass, authored inertia, hull of the body facets); the |
|                    | wheels stay the solver's (contact fan through the adapter's ground query)                      |
| Tyre carcass       | One seam in the contact step and three load-carrying models: Spring (the radial spring), XPBD  |
|                    | (the lattice) and FEM (Hex8 Neo-Hookean carcass); each returns its normal load and enclosed    |
|                    | volume, so the gas pressure follows deformation and temperature                                |
| Rendering          | The deformation reaches `VehicleProfile`: the analytic path for Spring, the 32 × 5 nodal field |
|                    | for XPBD and FEM; the body and wheels follow the physics through occupant relocation          |
| Physics thread     | `PhysicsThread`: fixed 240 Hz steps, one Jolt update per step, a catch-up cap, an input        |
|                    | mailbox and a published snapshot the editor reads without waiting                              |
| Adapter            | `PhysicsAdapter` (the solver interface) with `JoltAdapter`; `PhysicsSpace` holds one; the      |
|                    | extension takes every world object through the register: moving, fixed, held, fracture        |
|                    | (static bounds), vehicle                                                                        |
| Register           | `WorldRegister`: every record with its classification and physics part, written as a Markdown  |
|                    | document beside the level (`<Level>.WorldRegister.md`)                                          |
| Inspector          | The prototype in C++, bound to the live configuration, plus the variables it lacked            |

## Steps

| Step | Work                                                                                             | Status |
|------|--------------------------------------------------------------------------------------------------|--------|
| 1    | `PhysicsAdapter` and `JoltAdapter` (forces, torques, ray queries, hulls, mass override)          | ✔️     |
| 2    | `PhysicsThread` with the input mailbox and the snapshot                                          | ✔️     |
| 3    | `WorldRegister` and its Markdown document                                                        | ✔️     |
| 4    | Project Drive import, naming pass, behaviour identity against the original                       | ✔️     |
| 5    | Missing pieces: tyre wear, cold window, charge-air temperature, fuel, fluids, traction control, | ✔️     |
|      | Ackermann steering                                                                              |        |
| 6    | The vehicle on Jolt (`VehicleRecord`, chassis body, hooks, ground query)                        | ✔️     |
| 7    | Tyre carcass seam with Spring, XPBD and FEM                                                     | ✔️     |
| 8    | The car in the viewport: occupants 0–4, `VehicleProfile`, driving input, chase camera           | ✔️     |
| 9    | `VehicleInspector`, tyre inspector, telemetry dock                                               | ✔️     |
| 10   | Validation, captures (car running, tyre deformation for all three models), documents, Windows   | ✔️     |

## Validation

| Suite                         | Checks                                                                                  |
|-------------------------------|-----------------------------------------------------------------------------------------|
| Project Drive identity        | The upstream suites and a force, slip and drivetrain trace match the original build     |
| Slip and tyre dynamics        | Pacejka peaks, slip stiffness, symmetry, combined-slip ellipse, aligning moment sign,   |
|                               | relaxation length, free rolling, skid-pad against the bicycle model, braking, launch    |
| Pressure and temperature      | Gas law, stiffness against pressure, heat balance, cold and hot windows, wear           |
| Powertrain                    | Supercharger boost without lag, bypass, parasitic drag, charge-air temperature, fuel    |
| Tyre carcass                  | Load against deflection for all three models, pressure ordering, volume, settling car   |
| Jolt host                     | Ride height at rest, straight launch, braking, cornering, cones and walls               |
| Physics thread and register   | Fixed cadence, snapshot hand-off, every record classified, document written             |

## Findings in the source

| Finding (VehicleDynamicsValidation, section A)                       | Change (a switch; off keeps Project Drive exactly)          |
|----------------------------------------------------------------------|-------------------------------------------------------------|
| The load factor 1 − s \|dfz\| raises μ with load below Fz0          | `LoadSensitivityAboveReference`: the factor above Fz0 only   |
| Combined slip normalises by the stiffnesses in kN, so G ≈ 1 and the  | `CombinedSlipMf61`: MF6.1's G functions (eq. 4.E50–4.E67)    |
| slips combine to 1.23 × the larger peak                              |                                                             |
| The sheet's longitudinal peak sits at κ ≈ 0.31                       | Traction control aims at 0.25                               |
| Fuel and mass in binary32 lose a step's burn (micrograms)            | Fuel, burnt total and mass derived in binary64              |

## Carcass calibration

| Model  | Formulation                                                         | Calibrated                         | Car at rest |
|--------|---------------------------------------------------------------------|------------------------------------|-------------|
| Spring | Project Drive's radial spring on the contact fan                     | 250 kN/m at 110 kPa, air share 75 % | 12.3 mm     |
| XPBD   | 84 particles, spokes, belt, shear, air volume, inflation as force    | η 0.35, one sub-step: 0.95 at 15 mm | 12.8 mm     |
| 3D FEM | Hex8, K_m + κ (p / p_ref) K_σ, tension-only prestress, LCP contact   | E 3.3 MPa, κ 9.5: 0.96 at 15 mm     | 13.8 mm     |

## Evidence (2026-11-04, `arena/c8d71a98-frontier`)

- `Exhibits/VehiclePhysics/README.md`: the exhibit ledger with the calibration table and run commands.
- `Exhibits/VehiclePhysics/VehicleRunningProvingGround.gif` (32 frames) and
  `Exhibits/VehiclePhysics/TyreDeformationAllThree.gif` (16 synchronized frames, all three carcasses).
- `Exhibits/VehiclePhysics/Results/`: workspace screenshots, full-height Inspector sections,
  `TyreCarcassComparison.png`, and the `VehicleDynamics.txt` (45/45) / `TyreCarcass.txt` (17/17) ledgers.
- `Projects/Everest/Content/Levels/ProvingGroundVehicle.levelspace` (+ `.WorldRegister.md`) and
  `Default.WorldRegister.md` (67 baseline records; `Default.levelspace` untouched).
- Suites: `RunProjectDriveIdentity.sh` 126/126 PASS with 169,380 trace numbers identical;
  `SceneGeometryValidation`, `SceneViewportPickValidation`, `SceneViewportFlightValidation`,
  `EverestInterfaceValidation`, `ForceFieldCatalogueValidation`, `EverestShippingValidation` and
  `StoredShaderArchiveValidation` pass; `EverestProjectEditor` / `EverestProjectRuntime` build on Linux
  and cross-build to Windows x64 (Zig 0.13.0, `Automation/WindowsToolchain.cmake`; not launched there).

## Later

- Electric motor and battery (GRIT), aquaplaning, the GPU tyre solvers, triangle colliders for every static object.
- Fracture: fractured objects stay static bounds until the fracture work.
