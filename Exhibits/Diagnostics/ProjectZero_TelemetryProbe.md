# Project-Zero Telemetry Probe

Development/debug-only probe (compiled out of ship builds). All records were held in RAM and written once, at application close.

## Events

| Event | At [ms] |
|---|---:|
| Boot | 0.000 |
| StartupComplete | 275182.982 |
| FirstFrameComplete | 275722.577 |
| Shutdown | 409605.539 |

## Startup Phases

| Phase | Begin [ms] | End [ms] | Duration [ms] |
|---|---:|---:|---:|
| SceneDecode | 223.022 | 5217.002 | 4993.980 |
| TextureDecode | 5217.006 | 12146.842 | 6929.836 |
| CwbvhBuild | 13129.333 | 17795.948 | 4666.615 |
| VulkanBringUp | 17826.531 | 268327.434 | 250500.903 |
| ShadingTableBake | 268331.004 | 269566.491 | 1235.487 |
| SceneUpload | 269566.499 | 269726.769 | 160.269 |
| InterfaceBringUp | 274529.606 | 275182.980 | 653.374 |

## Shader Loads and Bring-up Stages

| Kind | Name | At [ms] | Duration [ms] |
|---|---|---:|---:|
| Stage | BringInstance | 22679.385 | 3037.726 |
| Stage | BringSurface | 22679.537 | 0.147 |
| Stage | BringPhysicalDevice | 22712.674 | 33.132 |
| Stage | BringLogicalDevice | 23455.119 | 742.437 |
| Stage | BringSwapchain | 27716.015 | 4260.884 |
| Stage | BringStorageImage | 27782.275 | 66.253 |
| Stage | BringCommandRecording | 27782.763 | 0.480 |
| Shader | Engine/Shaders/ReSTIRViewport.spv | 27964.551 | 176.114 |
| Stage | BringComputePipeline | 267068.099 | 239285.326 |
| Shader | Engine/Shaders/AtrousDenoise.spv | 267250.520 | 79.287 |
| Stage | BringDenoisePipeline | 267287.513 | 219.403 |
| Shader | Engine/Shaders/LuminanceReduce.spv | 267385.824 | 28.921 |
| Stage | BringLuminanceReduction | 267416.442 | 128.922 |
| Stage | BringSkyRecord | 267416.728 | 0.282 |
| Stage | BringMoonRecord | 267417.012 | 0.283 |
| Stage | BringPostRecord | 267417.232 | 0.218 |
| Stage | BringStarTables | 267417.475 | 0.242 |
| Stage | BringDescriptorSet | 267417.754 | 0.277 |
| Stage | BringCycleSlots | 267429.028 | 11.269 |
| Stage | BringImGui | 267624.595 | 195.562 |
| Shader | Engine/Shaders/ClusterCull.spv | 267698.939 | 69.683 |
| Shader | Engine/Shaders/HiZReduce.spv | 267754.271 | 26.427 |
| Shader | Engine/Shaders/SurfaceResolve.spv | 267799.586 | 40.775 |
| Shader | Engine/Shaders/VisibilityRaster.vert.spv | 267902.726 | 102.336 |
| Shader | Engine/Shaders/VisibilityRaster.frag.spv | 267989.767 | 87.036 |
| Shader | Engine/Shaders/ShadowRaster.vert.spv | 268055.030 | 60.387 |
| Shader | Engine/Shaders/ShadowRaster.frag.spv | 268129.742 | 74.706 |
| Shader | Engine/Shaders/ShadowResolve.spv | 268242.252 | 111.752 |
| Stage | BringVisibility | 268326.395 | 701.795 |
| Shader | Engine/Shaders/InterfaceRaster.vert.spv | 274584.790 | 40.175 |
| Shader | Engine/Shaders/InterfaceRaster.frag.spv | 274623.635 | 38.838 |

## Frames

3399 frames recorded. Full per-frame rows: ProjectZero_TelemetryProbe_Frames.csv

| Column | Mean [ms] | Peak [ms] |
|---|---:|---:|
| Frame Δτ | 38.0060 | 100.0000 |
| CPU InputAndUi | 0.4303 | 100.6717 |
| CPU CelestialTick | 0.0176 | 0.2719 |
| CPU EditorAndPanels | 2.6864 | 189.2709 |
| CPU SimulationAndInterface | 0.0649 | 1.1169 |
| CPU ScenePush | 0.0122 | 0.3110 |
| CPU RecordAndPresent | 35.7464 | 316.4447 |
| CPU FrameCapWait | 0.0013 | 0.0457 |
| GPU Cull | 0.0926 | 0.3478 |
| GPU Raster | 7.9316 | 17.0847 |
| GPU HiZ | 0.0404 | 0.5271 |
| GPU Resolve | 0.7224 | 2.4802 |
| GPU Kernel | 28.6640 | 281.5732 |
| GPU Shadow | 0.0000 | 0.0000 |
| GPU ReSTIR | 28.6640 | 281.5732 |
| GPU Post | 0.5229 | 1.1917 |
| GPU Sky | 0.0000 | 0.0000 |
| GPU Volume | 0.0000 | 0.0000 |

3397 of 3399 frames carried valid device timestamps.
