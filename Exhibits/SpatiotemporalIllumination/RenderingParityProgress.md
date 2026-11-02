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

## Card Sunlight Follow-up

| Check                         | Result                                                                        |
|-------------------------------|-------------------------------------------------------------------------------|
| Build and Slang compilation   | Passed: `SpatiotemporalIlluminationValidation` and production shader entries |
| Vulkan renderer test          | Passed on SwiftShader: 1/1, 305.52 seconds                                     |
| Card-sun GPU fixture          | `0, 1, 0, 1`: sphere blocked, clear, far-field down, far-field up             |
| Camera translation diagnostic | Retained red mean 0.577004 versus 0.127975 after reset                        |

The fixture dispatches the production card solar march over a synthetic sphere and ground plane. The march follows
Webdemo's voxel bias, hit fraction, softness, far-field behavior and medium-grade 80-step limit. Everest's distance
volume contains untruncated distances, unlike the reference's band-limited toroidal cascades, so the reference's
saturated-band coarser-level optimization does not apply. Single-volume fallback still uses the previous cone evaluator.
Hybrid local lights no longer use SDF visibility in the primary direct or card paths, matching the reference's unshadowed
clustered lights. Full RT remains unchanged. The synthetic fixture checks the march, not visual parity or local-light
image parity.

Test platform and commands are as above; tested source was `e7a0aaf` plus this follow-up's uncommitted changes, with
CMake 4.4.4 and Ninja 1.13.2. The test ran concurrently with a cross-build, so its elapsed time is not a performance
comparison. No Windows or Radeon runtime, Webdemo image comparison, or FPS was measured.

Windows x64 cross-build of the `Everest` and `Frontier` targets passed on Linux using Zig 0.13.0 and the Slang WASM
compiler. This verifies compilation and linking, not Windows startup, driver compatibility, signature, malware scanning,
or Radeon performance. Reproduce after the prerequisites above and `pip install ziglang==0.13.0`:

```sh
export PATH="$PWD/.venv/bin:$PATH"
cmake -S . -B _ClaudeScratch/windows -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/Automation/WindowsToolchain.cmake" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DFRONTIER_SLANGC=FRONTIER_SLANGC-NOTFOUND \
  -DFRONTIER_SLANG_WASM="$PWD/_ClaudeScratch/slang/node_modules/slang-loader/vendor/slang-wasm.js"
cmake --build _ClaudeScratch/windows --target Everest Frontier -j 3
# Commit the tested sources first; the package records the committed revision.
python Exhibits/SpatiotemporalIllumination/Code/PackageEverestPreview.py \
  --build _ClaudeScratch/windows --output Binaries/Everest-Parity-Preview-Windows-x64.zip
```

The package retains the previous checked-in archive's content as a base, then overlays the new executables and staged
project content. The packager checks PE32+ x64 headers, shader equality, and archive CRC, and records file hashes.

## Remaining Work

The reference's screen-depth traversal, ReSTIR GI reservoir reuse and surface-card atlas, display TAA and exposure,
and environment integration are not yet ported. The SDF representations and quality-dependent step budgets still
differ. Matching the reference scene, material inputs, camera, resolution and settings, followed by image and Radeon
timing comparisons, remains necessary before claiming parity.
