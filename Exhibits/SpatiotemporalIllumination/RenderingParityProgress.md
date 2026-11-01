# Rendering Parity Progress

This is a partial renderer change, not a 1:1 Webdemo port. The editor UI was not changed.

## Validation

| Check                         | Result                                                                                    |
|-------------------------------|-------------------------------------------------------------------------------------------|
| Build and shader compilation  | Passed: `SpatiotemporalIlluminationValidation` and its Slang dependencies                |
| Vulkan renderer test          | Passed on SwiftShader: 1/1, 148.42 seconds                                                |
| Prior-distance GPU fixture    | `5, 1, 0, 1`: distance, accepted match, rejected mismatch, accepted current-distance case  |
| Camera translation            | History inspection red mean: 0.577004 retained versus 0.127975 after reset               |
| Four SDF cascade headers      | Checked 16 entries and all four active cascade extents against the full packed table       |

The camera inspection is a history-retention diagnostic, not a measure of noise or visual parity. A comparison run
using the previous temporal integration shader produced 0.142950 retained and 0.127975 restarted. Its difference
(0.014975) is below the new regression threshold of 0.10; the current result exceeds it. The prior run used the
same scene and other current shaders, not a full before-and-after application benchmark.

Test platform: Linux, GCC 12.2.0, Vulkan SwiftShader CPU driver, Slang `slangc` 2026.14.1, CMake/Ninja; base revision
`d09d043d` plus the rendering and validation changes in this working tree. No Windows or Radeon runtime,
visual Webdemo comparison, or GPU performance claim was verified.

## Reproduction

Prerequisites: the repository's prepared external dependencies, SwiftShader and Slang, Python virtual environment,
Ninja, and the `slang-loader` WASM at `_ClaudeScratch/slang/node_modules/slang-loader/vendor/slang-wasm.js`.

```sh
export PATH="$PWD/.venv/bin:$PATH"
cmake -S . -B _ClaudeScratch/host -G Ninja \
  -DFRONTIER_HOST_VERIFICATION=ON -DGLFW_BUILD_WAYLAND=OFF -DGLFW_BUILD_X11=OFF \
  -DFRONTIER_SLANGC=FRONTIER_SLANGC-NOTFOUND \
  -DFRONTIER_SLANG_WASM="$PWD/_ClaudeScratch/slang/node_modules/slang-loader/vendor/slang-wasm.js" \
  -DFRONTIER_VULKAN_DRIVER="$PWD/ExternalPackages/VulkanCompute/SwiftShader/libvulkan.so.1"
cmake --build _ClaudeScratch/host --target SpatiotemporalIlluminationValidation -j 4
export VK_ICD_FILENAMES="$PWD/ExternalPackages/VulkanCompute/SwiftShader/vk_swiftshader_icd.json"
export LD_LIBRARY_PATH="$PWD/ExternalPackages/VulkanCompute/SwiftShader${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
ctest --test-dir _ClaudeScratch/host -R '^SpatiotemporalIlluminationValidation$' --output-on-failure -j1 -V
```

## Remaining Work

The reference's screen-depth traversal, ReSTIR GI reservoir reuse and surface-card atlas, display TAA and exposure,
and environment integration are not yet ported. The new card shadow uses Everest's existing SDF cone evaluator and
is not an exact port of the reference's shadow traversal. Matching the reference scene, material inputs, camera,
resolution and settings, followed by image and Radeon timing comparisons, remains necessary before claiming parity.
