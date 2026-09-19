# Project-Zero Telemetry Probe

Development/debug-only probe (compiled out of ship builds). All records were held in RAM and written once, at application close.

## Events

| Event | At [ms] |
|---|---:|
| Boot | 0.000 |
| StartupComplete | 7508.910 |
| FirstFrameComplete | 7586.785 |
| Shutdown | 369570.867 |

## Startup Phases

| Phase | Begin [ms] | End [ms] | Duration [ms] |
|---|---:|---:|---:|
| SceneDecode | 16.280 | 363.727 | 347.447 |
| TextureDecode | 363.728 | 1682.859 | 1319.130 |
| CwbvhBuild | 1700.091 | 2257.410 | 557.319 |
| VulkanBringUp | 2267.431 | 5081.044 | 2813.613 |
| ShadingTableBake | 5082.506 | 6598.075 | 1515.569 |
| SceneUpload | 6598.086 | 6754.479 | 156.393 |
| InterfaceBringUp | 7369.675 | 7508.908 | 139.233 |

## Shader Loads and Bring-up Stages

| Kind | Name | At [ms] | Duration [ms] |
|---|---|---:|---:|
| Stage | BringInstance | 3211.130 | 240.668 |
| Stage | BringSurface | 3211.203 | 0.069 |
| Stage | BringPhysicalDevice | 3225.548 | 14.338 |
| Stage | BringLogicalDevice | 3435.167 | 209.614 |
| Stage | BringSwapchain | 4888.125 | 1452.954 |
| Stage | BringStorageImage | 4925.737 | 37.605 |
| Stage | BringCommandRecording | 4926.538 | 0.793 |
| Shader | Engine/Shaders/ReSTIRViewport.spv | 4931.515 | 1.993 |
| Stage | BringComputePipeline | 4995.934 | 69.389 |
| Shader | Engine/Shaders/AtrousDenoise.spv | 4998.250 | 0.991 |
| Stage | BringDenoisePipeline | 4999.724 | 3.785 |
| Shader | Engine/Shaders/LuminanceReduce.spv | 5001.887 | 0.600 |
| Stage | BringLuminanceReduction | 5004.084 | 4.355 |
| Stage | BringSkyRecord | 5005.558 | 1.468 |
| Stage | BringMoonRecord | 5006.169 | 0.607 |
| Stage | BringPostRecord | 5006.684 | 0.512 |
| Stage | BringStarTables | 5007.212 | 0.525 |
| Stage | BringDescriptorSet | 5007.733 | 0.516 |
| Stage | BringCycleSlots | 5007.938 | 0.200 |
| Stage | BringImGui | 5010.918 | 2.978 |
| Shader | Engine/Shaders/ClusterCull.spv | 5016.365 | 1.668 |
| Shader | Engine/Shaders/HiZReduce.spv | 5018.622 | 0.857 |
| Shader | Engine/Shaders/SurfaceResolve.spv | 5030.209 | 3.483 |
| Shader | Engine/Shaders/VisibilityRaster.vert.spv | 5032.108 | 1.147 |
| Shader | Engine/Shaders/VisibilityRaster.frag.spv | 5033.350 | 1.237 |
| Shader | Engine/Shaders/ShadowRaster.vert.spv | 5043.697 | 8.219 |
| Shader | Engine/Shaders/ShadowRaster.frag.spv | 5048.318 | 4.616 |
| Shader | Engine/Shaders/ShadowResolve.spv | 5052.174 | 2.793 |
| Stage | BringVisibility | 5080.856 | 69.934 |
| Shader | Engine/Shaders/InterfaceRaster.vert.spv | 7375.707 | 2.499 |
| Shader | Engine/Shaders/InterfaceRaster.frag.spv | 7387.654 | 11.940 |

## Frames

4487 frames recorded. Full per-frame rows: ProjectZero_TelemetryProbe_Frames.csv

| Column | Mean [ms] | Peak [ms] |
|---|---:|---:|
| Frame Δτ | 61.1020 | 100.0000 |
| CPU InputAndUi | 0.9547 | 480.3117 |
| CPU CelestialTick | 0.0218 | 7.5657 |
| CPU EditorAndPanels | 4.7589 | 265.9386 |
| CPU SimulationAndInterface | 0.1039 | 17.0371 |
| CPU ScenePush | 0.0323 | 8.7847 |
| CPU RecordAndPresent | 69.8443 | 581.1262 |
| CPU FrameCapWait | 0.0019 | 0.5704 |
| GPU Cull | 0.0449 | 0.4029 |
| GPU Raster | 2.9185 | 11.4862 |
| GPU HiZ | 0.0509 | 1.1837 |
| GPU Resolve | 0.6220 | 2.8926 |
| GPU Kernel | 64.7088 | 227.5443 |
| GPU Shadow | 0.0000 | 0.0000 |
| GPU ReSTIR | 64.7088 | 227.5443 |
| GPU Post | 1.7245 | 5.8082 |
| GPU Sky | 0.0000 | 0.0000 |
| GPU Volume | 0.0000 | 0.0000 |

4485 of 4487 frames carried valid device timestamps.
