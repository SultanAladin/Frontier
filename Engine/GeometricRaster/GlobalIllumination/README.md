# Frontier :: GeometricRaster/GlobalIllumination

World direct-lighting, shadows, specular reflections, and GPU Vulkan compute showcase pipeline.

## Key Files

| File | Description |
|------|-------------|
| `ShowcaseGalleryScene.cpp` | Cornell-Studio Showcase Gallery Scene with Outliner Synchronization & Interactive Asset Placement |
| `ShowcaseGalleryScene.h` | Cornell-Studio Showcase Gallery Scene with Outliner Synchronization & Interactive Asset Placement |
| `SpecularReflectionSequence.cpp` | BVH4 specular raytracing, analytical sky dome fallback, and chromatic dielectric refraction |
| `SpecularReflectionSequence.h` | BVH4 specular raytracing, analytical sky dome fallback, and chromatic dielectric refraction |
| `TieredWorldRenderSequence.cpp` | World Render Sequence (MatCap & OpenPBR Direct + Shadow Lab + BVH4 Reflections + Atmosphere/Fluid) |
| `TieredWorldRenderSequence.h` | World Render Sequence (MatCap & OpenPBR Direct + Shadow Lab + BVH4 Reflections + Atmosphere/Fluid) |
| `VulkanWorldRenderSequence.cpp` | Vulkan .slang GPU Compute World Render Sequence (MatCap, OpenPBR Direct, Sky LUT, Clouds, Cascaded Shadows) |
| `VulkanWorldRenderSequence.h` | Vulkan .slang GPU Compute World Render Sequence (MatCap, OpenPBR Direct, Sky LUT, Clouds, Cascaded Shadows) |

## Architecture Overview

- **Namespaces**: `Frontier::GeometricRaster`
- **Primary Types**: `WorldRenderEngine`, `WorldRenderTier`, `WorldRenderConfiguration`, `WorldSceneSpecification`, `TieredWorldRenderSequence`, `VulkanWorldRenderSequence`, `ShowcaseWorldSceneBundle`

## Usage Example

```cpp
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"

Frontier::GeometricRaster::TieredWorldRenderSequence WorldRenderer {};
```
