# Complete CPU reference frame

This exhibit answers “get something rendering” at the portable/core level, while preserving the later SDK phase as the test of real window/GPU execution.

## What runs end-to-end

`ReferenceRenderer` consumes triangle meshes + lights + a camera, and executes: cluster hierarchy construction → screen-error cut → frustum cull → clipped perspective rasterisation/G-buffer → multi-candidate RIS with temporal and spatial reservoir reuse → shadow visibility → optional distance-field irradiance/card radiosity → temporal moments and 5 à-trous SVGF steps → atmosphere/sun disc/cloud compositing for misses → height fog → HDR linear output + exposure/display encoding. Fixed seed, scene and camera produce a bit-deterministic image. The complete GPU frame schedule is independently unified in `CompleteFrameUnits`: **38 units, 77 derived barriers, 33 targets, 54% transient-atlas saving at 1920×1080**; only previous-frame reservoirs and lighting histories remain imported.

## Run

Build with CMake 3.20+ and a C++17 compiler:

```sh
cmake -S Exhibits/CompleteRenderer -B build/complete-renderer -DCMAKE_BUILD_TYPE=Release
cmake --build build/complete-renderer --parallel
ctest --test-dir build/complete-renderer --output-on-failure
./build/complete-renderer/CompleteRendererCapture Exhibits/CompleteRenderer/Capture/my-frame.ppm
```

The capture is a 320×200 low-resolution reference frame, 24 temporal accumulation frames and 8 RIS candidates (CPU runtime is intentionally high; it is a numerical/reference renderer, not the real-time target). `--direct-only` disables distance-field indirect. Output is binary PPM; convert to PNG for review. The checked-in `Capture/reference-frame.png` is the captured image.

## Validation

`CompleteRendererValidation`: **13/13**. The full declaration resolves, starts with surface raster and contains present, barriers and atlas resolve, targets safely alias; a 64×40 scene renders more than 500 surface pixels, the geometry cut/cull path selects triangles, two independently prepared renders hash identically, and HDR encodes to visible RGB8. `-Wall -Wextra -Werror` clean.

The project-level verification suites also passed from fresh builds: SceneDirectory 45 Development + 45 Editor + 17 Shipping; LogStream 69; RenderInfrastructure 63; Geometry 57; IlluminationChain 55; IndirectIllumination 60; EnvironmentPipeline 73.

## Honest boundary

This is an intentionally small software reference: no Vulkan device, swapchain, shader dispatch, native editor window or real-time performance is claimed. Its implementation is the CPU semantic oracle for the SDK/GPU work, not a substitute for shader parity. Lighting uses a deliberately small scene and simplified Lambertian material; the optional card path selects a nearby texel; environment and local world are a local flat-ground approximation. Demo golden-image parity and runtime performance await the SDK build. Cabin HMI screens, plume/fire/sparks and particle workspace are **deferred** per owner direction; Phase 7 physics remains gated.
