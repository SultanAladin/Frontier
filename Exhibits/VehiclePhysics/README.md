# Vehicle physics evidence

This exhibit records a scripted simulation, not a demonstration of an interactive editor session.
The simulation uses Jolt 5.2.0 with a vehicle rig at 240 Hz and the Spring, XPBD and 3D FEM tyre
formulations. The viewport images were rendered through Vulkan using SwiftShader.

## Visual evidence

- [Vehicle manoeuvre](Images/VehicleManoeuvre.gif): 36 viewport frames from the 3D FEM run, at
  approximately 0.11 simulated seconds per frame. The camera follows the vehicle during launch,
  steering and braking.
- [Vehicle viewport frame](Images/VehicleViewport.png): one frame from that capture.
- [Wheel close-ups](Images/vulkan_wheel_macro_fem3d.png): the FEM tyre under a scripted static
  load; matching [Spring](Images/vulkan_wheel_macro_spring.png) and
  [XPBD](Images/vulkan_wheel_macro_xpbd.png) captures use the same scene. These pictures alone do
  not establish real-time performance or quantify deformation differences.
- [Telemetry graph](Images/VehicleTelemetry.png): speed, front-left tyre deflection and normal load
  for all three formulations. The underlying recorded data is in `Results/telemetry_*.csv`.
- [Engine inspector](Images/VehicleInspectorEngine.png): headless rasterization of the native
  inspector, not an interactive editor screenshot.

## Validation status

The current physics executable reports 94 passing checks and one failing check. Vehicle coverage
includes force calculations against a local equation oracle, tyre pressure and thermal behaviour,
three soft-body formulations, vehicle launch, steering, braking, a 12-degree braked slope hold,
supercharger response, turbo spool and release, explicit gears, reverse, neutral, and automatic
upshift and downshift. These are bounded simulation checks, not independent experimental
calibration or proof of production readiness. The slope test checks two seconds after settling;
it is not a long-duration parking or brake-fade test.

The remaining failing check involves a dynamic crate falling through a Jolt ground plane and its
fracture threshold. Fracture functionality is outside the present priority, but the observed
rigid-body collision issue is relevant to general world interactions and remains unresolved.
The vehicle tests use tyre ground queries and do not establish that all rigid-body collisions work.

The physics library, scene publisher and threaded adapter are exercised by the test and capture
executables. They have not yet been connected end-to-end to the main editor runtime: inspector
values and scene records are not proof that editing a vehicle controls a live simulation. Further
work is required for interactive operation, full configuration mapping, independent reference-data
comparison, expanded gear and braking scenarios, longer slope holds and performance profiling.

## Reproduce

Configure with the repository's dependency preparation scripts and build the
`VehiclePhysicsValidation`, `NativeVehicleInspectorValidation`, and `VehicleTyreAnimationCapture`
targets. Run the first two through CTest. On headless Linux, configure SwiftShader through
`VK_ICD_FILENAMES` and `LD_LIBRARY_PATH`, then run:

```sh
./_ClaudeScratch/build-linux/VehicleTyreAnimationCapture \
  "$PWD" "$PWD/EngineContent/ShaderArchives" "$PWD/_ClaudeScratch/vehicle-animation-raw"
```

The capture produces viewport frames and per-formulation telemetry and tyre point positions.
The stored GIF and graph were assembled from those outputs.
