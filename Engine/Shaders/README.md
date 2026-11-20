# 🧩 `Engine/Shaders/`

> **TL;DR**: Subsystem umbrella grouping 15 child modules under `Engine/Shaders/`.

---

## Subdirectories

| Directory | Summary |
| :--- | :--- |
| [`Atmosphere/`](Atmosphere/README.md) | Neckel & Labs (1994) Limb-Darkened Sun Disc + erfc PSF + K/F Corona + Radiant Glare + Sub-Pixel SolarErf, 6x Multi-Moon & Starfield |
| [`Baking/`](Baking/README.md) | Unquantized RGBA32F -> RGBA8 channel synthesis for all 17 surface targets: |
| [`Colour/`](Colour/README.md) | Shared pure .slang GPU colour ramp LUT sampling, wrap/curve math, Oklab/LinearSrgb conversions, Kelvin blackbody & 3D lattice noise |
| [`ComponentEditor/`](ComponentEditor/README.md) | Compiled geometry shader supporting OpenPbrDirect (Zero GI) plus all 7 Component Editor inspection channels & MRT PickID |
| [`Compute/`](Compute/README.md) | Runtime & Editor Vulkan 1.3 Compute shader evaluating cascaded 3D velocity + curl lattice voxels from ForceSourceShaderEntry SSBO |
| [`DirectLighting/`](DirectLighting/README.md) | Coplanar Plane-Aware Shadow Denoiser, Deterministic Vogel Solar Frustum Raytracer + Chassis AO, & Shared 4-Wide BVH4 Query Library |
| [`Fluid/`](Fluid/README.md) | MacCormack (BFECC + 6-neighbor min/max clamp) advection, Arrhenius combustion, cooling & soot generation |
| [`GeometricRaster/`](GeometricRaster/README.md) | Unified 2-Engine (`MatCap` & `OpenPbrDirect`) World Viewport Compute, Camera Visual Effects, Surface/UV/Gizmo Rasterization, & Pick Reduction |
| [`GlobalIllumination/`](GlobalIllumination/README.md) | Tier 2 RTX Coherent VNDF Specular Reflection & Abbe-Dispersed Dielectric Glass Refraction Compute Pass |
| [`MaterialSpace/`](MaterialSpace/README.md) | Fast 5-ALU 32-byte BounceSlabEntry evaluation for secondary OpenPBR specular & refractive rays |
| [`Particles/`](Particles/README.md) | Vulkan 1.3 particle advection compute kernel importing ColourRampSampling & ForceFieldEvaluationShared (zero duplicated physics/colour) |
| [`Sketcher/`](Sketcher/README.md) | Shared CAD stroke expansion: homogeneous edge clipping, the six-corner pixel-width quad and its coverage ramp. |
| [`Vector/`](Vector/README.md) | Resolves 2x SSAA SurfacePreviewImage + StackedPick into FinalColorImage (RGBA8) & FinalPickImage (RGBA32UI), plus GPU vector guides/knots/pins/marquee. |
| [`VectorField/`](VectorField/README.md) | Editor-only orthogonal XY/XZ/YZ cross-section scalar heatmap & analytical isolines rendered over Lit geometry |
| [`Visibility/`](Visibility/README.md) | Zero-G-Buffer Analytic Ray-Differential Barycentric Resolver, UV Footprint Jacobians, Motion Vectors, & Wave-Uniform OpenPBR Shade |

## Build & Verification

- Built as part of `FrontierEngine` in root `CMakeLists.txt` (ISO C++20).
- Validated by the repository's CTest suites under `Exhibits/` (`ctest --test-dir build-editor --output-on-failure`).
