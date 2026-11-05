# Vehicle Physics

Project Drive (the GRIT vehicle translation) on a Jolt chassis in Frontier, with three load-carrying tyre
carcasses, a 240 Hz physics thread, a Jolt adapter surveyed through a world register, C++ inspectors, and
the car driving the proving ground in the viewport.

## What it is

| Part                 | Role                                                                                          |
|----------------------|-----------------------------------------------------------------------------------------------|
| `Vehicle/` solver    | Project Drive renamed to the naming rules: Pacejka MF5.2/MF6.1, transient slip, drivetrain,   |
|                      | brakes + ABS, aero, thermal gas law, struts; plus wear, cold window, charge air, fuel,        |
|                      | fluids, traction control, Ackermann steering                                                  |
| Tyre carcasses       | One seam in the contact step, three formulations: Spring (radial spring on the contact fan),  |
|                      | XPBD (84-particle lattice with enclosed gas volume), 3D FEM (Hex8 Neo-Hookean + prestress +   |
|                      | Coulomb tangential shear); each returns normal load and enclosed volume                       |
| `PhysicsThread`      | Fixed 240 Hz steps, one Jolt update per step, catch-up cap, input mailbox, published          |
|                      | snapshot the editor reads without waiting                                                     |
| `PhysicsAdapter`     | The solver interface with `JoltAdapter` (forces, torques, ray queries, hulls, mass override)  |
| `WorldRegister`      | Every record with its classification and physics part, written beside the level as             |
|                      | `<Level>.WorldRegister.md`                                                                    |
| Viewport             | Body and wheels follow the physics through occupant relocation (occupants 0–4);                |
|                      | `VehicleVisual.slang` deforms the tyres (analytic path for Spring, 32 × 5 nodal field for     |
|                      | XPBD and FEM); driving input (WASD + arrows) and a chase camera                               |
| Inspectors           | `VehicleInspector` (overview, chassis, engine, drivetrain, brakes, aero, fluids, tyres,       |
|                      | telemetry) and `TyreInspector` (fitting, compound, pressure, wear, carcass), bound to the     |
|                      | live configuration with interactive curve authoring and a four-corner pit bay                  |

## Sources

| Source                                               | What it holds                                                     |
|------------------------------------------------------|-------------------------------------------------------------------|
| `SultanAladin/GRIT` `source-only`, `VehicleFramework/` | The original specifications and the solver's documents          |
| `SultanAladin/Frontier-` `3f14808`, `Engine/PhysicalDynamics` | Project Drive: the GRIT translation and its thread       |
| `streamlinkinbox/Frontier` `arena/5ec53579-frontier`  | Project Drive imported, the VehicleHMI tyre solvers               |
| `SultanAladin/Frontier` `arena/ca09b37f-frontier`     | `Experimental/VehicleInspector/`, the HTML inspector prototype    |

## Validation

| Suite                        | Checks                                                                            | Ledger                        |
|------------------------------|-----------------------------------------------------------------------------------|-------------------------------|
| Project Drive identity       | The 7 upstream suites (126 checks) plus a 169,380-number force/slip/drivetrain    | `Results/ProjectDriveIdentity.txt` |
|                              | trace print bit-identical numbers before and after the rename                     |                               |
| `VehicleDynamicsValidation`  | 45/45: Pacejka peaks/stiffness/symmetry, MF6.1 combined slip, relaxation,         | `Results/VehicleDynamics.txt` |
|                              | launch/braking/cornering on the Jolt chassis, supercharger, gas law, wear,       |                               |
|                              | fuel, fluids, traction control, Ackermann, thread cadence, register               |                               |
| `TyreCarcassValidation`      | 17/17: load–deflection for all three carcasses, pressure ordering, XPBD gas       | `Results/TyreCarcass.txt`     |
|                              | volume, FEM contact/shear/objectivity, the whole car on each carcass              |                               |

Curve and trace data: `Results/PacejkaCurves.csv`, `Results/LaunchTrace.csv`, `Results/TyreCarcassCurves.csv`,
`Results/TyreCarcassTiming.csv`, `Results/WorldRegisterSample.md`.

Carcass calibration (110 kPa, 15 mm operating point):

| Model  | Formulation                                              | Of spring load | Car at rest | Step cost |
|--------|----------------------------------------------------------|----------------|-------------|-----------|
| Spring | Radial spring on the contact fan, 250 kN/m, air share 75 % | 1.00          | 12.3 mm     | 0.09 ms   |
| XPBD   | 84 particles, spokes, belt, shear, air volume            | 0.95           | 12.8 mm     | 0.69 ms   |
| 3D FEM | Hex8, K_m + κ (p / p_ref) K_σ, tension-only prestress    | 0.96           | 13.8 mm     | 0.20 ms   |

## Captures

`Code/VehicleWorkspaceCapture.cpp` (the `VehicleWorkspaceCapture` target) drives an offscreen
`EverestWorkspace` with a SwiftShader Vulkan `SceneViewport`: it authors the level, screenshots the
workspace, then renders the frame sequences the GIFs below are assembled from.

| Media                                  | What it shows                                                                |
|----------------------------------------|------------------------------------------------------------------------------|
| `VehicleRunningProvingGround.gif`      | 32 frames: full-throttle launch, slalom weave, counter-steer, ABS braking    |
| `TyreDeformationAllThree.gif`          | 16 synchronized frames: the front-left contact patch on Spring, XPBD, 3D FEM |
| `Results/TyreCarcassComparison.png`    | The three carcasses side by side under cornering load (frame 09/16)          |
| `Results/EverestVehicleOverview.png`   | The workspace with Control vehicle selected (Outliner, viewport, Inspector)  |
| `Results/EverestVehicleEngineCurves.png` | Engine tab with the torque/power curve editor                              |
| `Results/EverestVehicleTyreBay.png`    | Tyres tab: the four-corner bay and pit lane                                  |
| `Results/EverestTyreInspector.png`     | `TyreInspector` on the front-left tyre                                       |
| `Results/EverestVehicleTelemetryLive.png` | Telemetry tab while driving: speed, rpm, pressures, four-corner cards     |
| `Results/VehicleInspector-Section-*.png` | Full-height Inspector sections 0, 2, 4, 8                                 |

Levels: `Projects/Everest/Content/Levels/ProvingGroundVehicle.levelspace` (Control vehicle + 4 enclosed
Tyre records, `ThreadedPhysics = true`) with `ProvingGroundVehicle.WorldRegister.md`, and
`Default.WorldRegister.md` (the 67 baseline records; `Default.levelspace` untouched).

## Run it

```sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DFRONTIER_HOST_VERIFICATION=ON \
  -DFRONTIER_BUILD_SOLIDARC=OFF -DGLFW_BUILD_WAYLAND=OFF -DGLFW_BUILD_X11=OFF
cmake --build build/linux --target VehicleDynamicsValidation TyreCarcassValidation VehicleWorkspaceCapture
ctest --test-dir build/linux -R "VehicleDynamicsValidation|TyreCarcassValidation" --output-on-failure
bash Exhibits/VehiclePhysics/RunProjectDriveIdentity.sh
```

The capture needs a SwiftShader Vulkan device:

```sh
export VK_ICD_FILENAMES="$PWD/ExternalPackages/VulkanCompute/SwiftShader/vk_swiftshader_icd.json"
export LD_LIBRARY_PATH="$PWD/ExternalPackages/VulkanCompute/SwiftShader:${LD_LIBRARY_PATH:-}"
./build/linux/VehicleWorkspaceCapture "$PWD/EngineContent" "$PWD/Projects/Everest" \
  "$PWD/build/linux/ShaderArchives" "$PWD/Exhibits/VehiclePhysics"
```

Append `--ui-only` to re-render the workspace screenshots without the animation frames.

## Later

- Electric motor and battery (GRIT), aquaplaning, the GPU tyre solvers, triangle colliders for every static object.
- Fracture: fractured objects stay static bounds until the fracture work.
