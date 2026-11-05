# Vehicle HMI and scan evidence

This exhibit records the prototype and native **scaffolding**, not a completed native HMI renderer.
`Experimental/VehicleHmiPrototype` contains the browser/Dawn renderer and cabin interface;
`Frontier/` contains the C++ session model, effect state, shader declarations and an approximate
software presenter. The configurator's supplied `Truck.tsx` is the scan and paint timing reference.
It is not included in this checkout; pass its path to the comparison tool.

Validated on Linux x64 at base revision `e1ce5629e892` with the accompanying uncommitted changes,
GCC 12.2, Node 22.22.3 and SwiftShader/Dawn. Windows execution has not been verified.

## Evidence

- [Solid](Images/Solid.png), [paint transition](Images/PaintRipple.png), and [X-ray scan](Images/XRayScan.png)
  are 512×320 readbacks from the prototype's dedicated HMI preview rendered by SwiftShader/Dawn.
  The three-frame Lobby capture completed with zero reported GPU errors or warnings. They are early
  individual frames, not screenshots of the tablet controls or pixel-perfect reference comparisons.
  Temporal lighting, exposure and stochastic sampling also affect differences between frames.
- The extracted reference animation and native `VehicleInspection` matched **6,324** state/timing values.
  The 4.6 s sweep, 1 s hold, 1 s fade, capped animation step, disable/re-enable behavior and global-time
  sparkle motion are checked. The prototype's sparkle motion, pixel blending/depth and paint ripple have
  separate executed WGSL probes.
- All eight native scan entry points compile to SPIR-V with a reflected 208-byte frame uniform. The
  native sparkle raster probe passed **36 component comparisons over 12 pixels**, plus size scaling and
  depth occlusion. SwiftShader reports unsupported capability 4427 for draw parameters; the probe does
  not establish nonzero-base draw support or warning-free execution.
- `Phase3Validation` passed 161 checks and `SessionValidation` 62. The standalone `LobbySession --xray
  --render` produces an approximate 640×400 CPU-rendered image; five players fit and a sixth is refused.
  Its X-ray frame differed from the solid frame at 6,869 pixels. This is not a native GPU capture.

## Reproduce

Run from the repository root. Node 22.18+, a C++20 compiler, CMake and Python 3 are needed. GPU probes
use the prototype's pinned `webgpu` package, Python `vulkan` bindings, and a Vulkan implementation;
`RunOnSoftwareGpu.mjs` can fetch SwiftShader on Linux x64. Slang compilation below uses
`slang-loader@0.0.7` (Slang WASM 2026.19), installed outside the tracked tree.

```sh
npm ci --prefix Experimental/VehicleHmiPrototype
npm ci --prefix Experimental/VehicleHmiPrototype/Tools
npm install --prefix /tmp/frontier-slang slang-loader@0.0.7
python3 -m pip install --target /tmp/frontier-vulkan vulkan==1.3.275.1

cmake -S Frontier -B /tmp/frontier-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/frontier-build
ctest --test-dir /tmp/frontier-build --output-on-failure
/tmp/frontier-build/LobbySession --guests 4 --xray --render /tmp/lobby-scan.ppm

node --test Experimental/VehicleHmiPrototype/Tools/HmiConfiguratorEffects.test.mjs \
  Experimental/VehicleHmiPrototype/Tools/VehicleAppearance.test.mjs
node Experimental/VehicleHmiPrototype/Tools/RunOnSoftwareGpu.mjs \
  node Exhibits/VehicleHmi/Code/ValidateScanParticlesGpu.mjs
node Experimental/VehicleHmiPrototype/Tools/RunOnSoftwareGpu.mjs \
  node Exhibits/VehicleHmi/Code/ValidatePaintTransitionGpu.mjs

node Exhibits/VehicleHmi/Code/CompileNativeScan.mjs \
  /tmp/frontier-slang/node_modules/slang-loader/vendor/slang-wasm.js /tmp/native-scan
PYTHONPATH=/tmp/frontier-vulkan node Experimental/VehicleHmiPrototype/Tools/RunOnSoftwareGpu.mjs \
  python3 Exhibits/VehicleHmi/Code/ValidateNativeScanGpu.py /tmp/native-scan

g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -I. -IFrontier \
  Exhibits/VehicleHmi/Code/NativeScanValidation.cpp -o /tmp/native-scan-validation
node Exhibits/VehicleHmi/Code/CompareScanReference.mjs \
  /path/to/configurator/src/components/Truck.tsx /tmp/native-scan-validation
```

To refresh the preview PNGs, set `HMI_CAPTURE_DIR` to an existing output directory and run:

```sh
HMI_CAPTURE_DIR="$PWD/Exhibits/VehicleHmi/Images" \
HOOKS="$PWD/Exhibits/VehicleHmi/Code/GarageCapture.mjs" HARNESS_TIMEOUT=900000 SIZE=320x180 \
node Experimental/VehicleHmiPrototype/Tools/RunOnSoftwareGpu.mjs \
  node Experimental/VehicleHmiPrototype/Tools/HeadlessHarness.mjs \
  Experimental/VehicleHmiPrototype 3 '?capture=1&quality=low&hmi=garage&lobby=1'
```

This software-GPU render is slow. The headless validation host stubs the browser DOM and does not render the tablet's
2D UI; a real-browser interactive session and hardware GPU coverage are still required.

## Remaining gaps

The native scan shader and frame declarations are **not connected to a native renderer**, and the
native tablet remains empty. The software presenter shades per facet and approximates the particles,
curtains and body. Scan and paint anchors are vehicle-local here, whereas the supplied configurator's
reference anchors are fixed in world space. The prototype's sparkle blending happens before its
exposure/filmic pass rather than in the same stage as Three.js. These differences, lighting, camera,
material response and live drag/drop interactions still need visual and interactive comparison on the
target GPU; the evidence above does not certify full visual parity.
