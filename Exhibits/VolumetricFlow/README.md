# Volumetric flow port evidence

The retained reference is `Documentation/Plans/References/InheritedEngine/Prototypes/VolumetricFlowSolver.html`.
The native code in `Engine/Render/VolumetricFlow*` is an independent authoring model and **dispatch
planner**, while the Slang files in `Engine/Render/Shader/VolumetricFlow*` are standalone shaders.
They are not wired into the product renderer or a native Fluid Editor. The browser reference remains
the working editor and execution path.

Validated on Linux x64 at base revision `e1ce5629e892` with the accompanying uncommitted changes,
GCC 12.2, Node 22.22.3, Python 3.11.2, Slang 2026.19 and SwiftShader.

## What is covered

- Authoring defaults/catalogue: five presets, 18 unit types and the 17-unit/16-link starting graph;
  native behavior was checked against 15 snapshots of the retained JavaScript.
- Simulation: all **12** retained compute entries are translated into projection and transport Slang.
  The host derives the substep uniform, multigrid levels and every pass/volume swap for a complete solver
  step. Eight direct JavaScript comparisons covered 1,870 pressure and full-step dispatches, odd grids,
  bounded substeps and nonzero initial ping-pong slots. Alias checks reject sampled/storage overlap.
- Projection and transport compute entries were executed individually on SwiftShader. Their outputs matched the
  original WGSL across 4,774 and 15,824 scalar values respectively. Chained one- and two-cycle pressure
  solves matched 45,056 values and reduced the test residual. Two complete native-planned 8³ and 16³
  steps matched the retained WGSL on 98,816 values (47 and 55 dispatches).
- Irradiance seeding and propagation use a reflected 80-byte uniform, 32³ default cache and four default
  flood passes. Five direct JavaScript staging/dispatch comparisons covered 100 uniform values and 20
  dispatches. Individual compute fixtures matched 2,728 values; four native-planned cache chains with
  different scalar/cache resolutions and swap states matched 60,448 values.
- The fullscreen vertex and raymarch fragment entries compile with a reflected 208-byte view uniform.
  Five orbit-camera staging cases matched 260 float slots. Four offscreen Vulkan raster fixtures exercised
  emission/scattering, turbulence, velocity inspection and an empty medium: 6,912 output components matched
  the retained WGSL, using native C++ view staging for the Slang render. These are small numerical fixtures,
  not visual approval of a complete fluid editor or native viewport.

The seven root `VolumetricFlow` CTests pass; the solver and view comparisons also pass with address and
undefined-behavior sanitizers. Four validation executables cross-compiled as Windows x64 PE files, but
were not executed on Windows. On the software device used here, all the numerical comparisons above
had maximum absolute difference zero. This does not guarantee bit identity across different hardware
or drivers. The separate `PlumePressure.slang` and `PlumeVorticity.slang` translate the vehicle prototype's smaller smoke solver,
not this retained solver.

## Reproduce

Run from the repository root. The root CMake build needs the repository's pinned UI dependencies and
fonts (see `Automation/PrepareUI.py` and `Automation/PrepareFonts.py`). Its CPU tests include catalogue,
authoring, solver, irradiance and view parity when Node is available:

```sh
cmake -S . -B /tmp/frontier-root -DBUILD_TESTING=ON
cmake --build /tmp/frontier-root --target VolumetricFlowValidation FlowSolverValidation \
  FlowIrradianceValidation FlowViewValidation
ctest --test-dir /tmp/frontier-root -R VolumetricFlow --output-on-failure
```

The GPU probes additionally need Node 22.18+, `webgpu` from the prototype tools, Python `vulkan`
bindings and a Vulkan device. The following installs test dependencies outside tracked source;
`RunOnSoftwareGpu.mjs` fetches SwiftShader on Linux x64 if necessary. `slang-loader@0.0.7` contains
Slang WASM 2026.19, which is the compiler used for these results.

```sh
npm ci --prefix Experimental/VehicleHmiPrototype/Tools
npm install --prefix /tmp/frontier-slang slang-loader@0.0.7
python3 -m pip install --target /tmp/frontier-vulkan vulkan==1.3.275.1
SLANG=/tmp/frontier-slang/node_modules/slang-loader/vendor/slang-wasm.js

node Exhibits/VolumetricFlow/Code/CompileFlowProjection.mjs "$SLANG" /tmp/flow-projection
node Exhibits/VolumetricFlow/Code/CompileFlowTransport.mjs "$SLANG" /tmp/flow-transport
node Exhibits/VolumetricFlow/Code/CompileFlowIrradiance.mjs "$SLANG" /tmp/flow-irradiance
node Exhibits/VolumetricFlow/Code/CompileFlowRaymarch.mjs "$SLANG" /tmp/flow-raymarch

export PYTHONPATH=/tmp/frontier-vulkan
GPU='Experimental/VehicleHmiPrototype/Tools/RunOnSoftwareGpu.mjs'
node "$GPU" node Exhibits/VolumetricFlow/Code/CompareFlowProjection.mjs /tmp/flow-projection
node "$GPU" node Exhibits/VolumetricFlow/Code/CompareFlowTransport.mjs /tmp/flow-transport
node "$GPU" node Exhibits/VolumetricFlow/Code/CompareFlowMultigrid.mjs \
  /tmp/frontier-root/FlowSolverValidation /tmp/flow-projection
node "$GPU" node Exhibits/VolumetricFlow/Code/CompareFlowStep.mjs \
  /tmp/frontier-root/FlowSolverValidation /tmp/flow-projection /tmp/flow-transport
node "$GPU" node Exhibits/VolumetricFlow/Code/CompareFlowIrradiance.mjs /tmp/flow-irradiance
node "$GPU" node Exhibits/VolumetricFlow/Code/CompareFlowIrradianceChain.mjs \
  /tmp/frontier-root/FlowIrradianceValidation /tmp/flow-irradiance /tmp/flow-projection
node "$GPU" node Exhibits/VolumetricFlow/Code/CompareFlowRaymarch.mjs \
  /tmp/flow-raymarch /tmp/frontier-root/FlowViewValidation
```

The GPU scripts create temporary fixtures and readbacks; compiled SPIR-V and build outputs are not
committed. The chained cache validation reuses the generic Vulkan dispatch validation with an unused,
disjoint storage binding.

## Remaining gaps

A native runtime must still own GPU resources, pipeline creation, descriptor bindings, synchronization,
resize/lifetime handling and actual frame submission for simulation, irradiance and raymarch. The native
Fluid Editor UI, controls, graph interactions, palette authoring and save/load integration are not ported.
The probes use CPU software Vulkan and tiny synthetic scenes; no Windows runtime, hardware GPU, live editor
interaction, performance, long-term stability or end-to-end visual parity has been certified.
