# Project-Zero Telemetry Probe

Development/debug-only probe (compiled out of ship builds). All records were held in RAM and written once, at application close.

## Events

| Event | At [ms] |
|---|---:|
| Boot | 0.000 |
| StartupComplete | 110255.526 |
| FirstFrameComplete | 110688.396 |
| Shutdown | 447416.007 |

## Startup Phases

| Phase | Begin [ms] | End [ms] | Duration [ms] |
|---|---:|---:|---:|
| SceneDecode | 40.636 | 643.525 | 602.889 |
| TextureDecode | 643.526 | 1724.635 | 1081.109 |
| CwbvhBuild | 1758.221 | 3165.817 | 1407.596 |
| VulkanBringUp | 3167.114 | 103257.052 | 100089.937 |
| ShadingTableBake | 103258.159 | 104515.321 | 1257.163 |
| SceneUpload | 104515.334 | 104685.331 | 169.997 |
| InterfaceBringUp | 110000.826 | 110255.524 | 254.698 |

## Shader Loads and Bring-up Stages

| Kind | Name | At [ms] | Duration [ms] |
|---|---|---:|---:|
| Stage | BringInstance | 3751.315 | 165.549 |
| Stage | BringSurface | 3751.382 | 0.064 |
| Stage | BringPhysicalDevice | 3756.012 | 4.627 |
| Stage | BringLogicalDevice | 3938.293 | 182.276 |
| Stage | BringSwapchain | 6390.572 | 2452.271 |
| Stage | BringStorageImage | 6434.338 | 43.759 |
| Stage | BringCommandRecording | 6434.969 | 0.623 |
| Shader | Engine/Shaders/ReSTIRViewport.spv | 6484.191 | 43.510 |
| Stage | BringComputePipeline | 102438.906 | 96003.932 |
| Shader | Engine/Shaders/AtrousDenoise.spv | 102525.620 | 41.941 |
| Stage | BringDenoisePipeline | 102557.429 | 118.515 |
| Shader | Engine/Shaders/LuminanceReduce.spv | 102631.825 | 24.443 |
| Stage | BringLuminanceReduction | 102661.346 | 103.912 |
| Stage | BringSkyRecord | 102661.903 | 0.552 |
| Stage | BringMoonRecord | 102662.242 | 0.336 |
| Stage | BringPostRecord | 102662.581 | 0.337 |
| Stage | BringStarTables | 102662.943 | 0.360 |
| Stage | BringDescriptorSet | 102663.342 | 0.395 |
| Stage | BringCycleSlots | 102683.330 | 19.982 |
| Stage | BringImGui | 102844.687 | 161.353 |
| Shader | Engine/Shaders/ClusterCull.spv | 102888.283 | 39.673 |
| Shader | Engine/Shaders/HiZReduce.spv | 102930.462 | 24.712 |
| Shader | Engine/Shaders/SurfaceResolve.spv | 102954.727 | 22.326 |
| Shader | Engine/Shaders/VisibilityRaster.vert.spv | 102975.435 | 20.083 |
| Shader | Engine/Shaders/VisibilityRaster.frag.spv | 102991.097 | 15.658 |
| Shader | Engine/Shaders/ShadowRaster.vert.spv | 103023.676 | 31.031 |
| Shader | Engine/Shaders/ShadowRaster.frag.spv | 103099.952 | 76.269 |
| Shader | Engine/Shaders/ShadowResolve.spv | 103200.769 | 96.795 |
| Stage | BringVisibility | 103256.904 | 412.212 |
| Shader | Engine/Shaders/InterfaceRaster.vert.spv | 110056.652 | 42.092 |
| Shader | Engine/Shaders/InterfaceRaster.frag.spv | 110092.076 | 35.416 |

## Frames

10329 frames recorded. Full per-frame rows: ProjectZero_TelemetryProbe_Frames.csv

| Column | Mean [ms] | Peak [ms] |
|---|---:|---:|
| Frame Δτ | 31.7990 | 100.0000 |
| CPU InputAndUi | 0.4261 | 205.4924 |
| CPU CelestialTick | 0.0192 | 3.4885 |
| CPU EditorAndPanels | 3.0930 | 144.9689 |
| CPU SimulationAndInterface | 0.0710 | 21.0047 |
| CPU ScenePush | 0.0124 | 0.4274 |
| CPU RecordAndPresent | 28.5043 | 316.4254 |
| CPU FrameCapWait | 0.0019 | 0.2451 |
| GPU Cull | 0.0851 | 0.3494 |
| GPU Raster | 7.1083 | 26.6473 |
| GPU HiZ | 0.0361 | 0.4917 |
| GPU Resolve | 0.6447 | 4.3481 |
| GPU Kernel | 22.3324 | 80.5675 |
| GPU Shadow | 0.0000 | 0.0000 |
| GPU ReSTIR | 22.3324 | 80.5675 |
| GPU Post | 0.4429 | 1.7063 |
| GPU Sky | 0.0000 | 0.0000 |
| GPU Volume | 0.0000 | 0.0000 |

10327 of 10329 frames carried valid device timestamps.
