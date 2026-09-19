# Project-Zero Telemetry Probe

Development/debug-only probe (compiled out of ship builds). All records were held in RAM and written once, at application close.

## Events

| Event | At [ms] |
|---|---:|
| Boot | 0.000 |
| StartupComplete | 6329.295 |
| FirstFrameComplete | 6383.619 |
| Shutdown | 22968.544 |

## Startup Phases

| Phase | Begin [ms] | End [ms] | Duration [ms] |
|---|---:|---:|---:|
| SceneDecode | 9.206 | 569.407 | 560.201 |
| TextureDecode | 569.408 | 1475.243 | 905.835 |
| CwbvhBuild | 1505.041 | 2194.914 | 689.873 |
| VulkanBringUp | 2195.975 | 4740.243 | 2544.268 |
| ShadingTableBake | 4740.721 | 5543.541 | 802.820 |
| SceneUpload | 5543.547 | 5678.484 | 134.937 |
| InterfaceBringUp | 6215.215 | 6329.293 | 114.079 |

## Shader Loads and Bring-up Stages

| Kind | Name | At [ms] | Duration [ms] |
|---|---|---:|---:|
| Stage | BringInstance | 3596.868 | 1155.137 |
| Stage | BringSurface | 3596.944 | 0.073 |
| Stage | BringPhysicalDevice | 3601.933 | 4.986 |
| Stage | BringLogicalDevice | 3835.955 | 234.018 |
| Stage | BringSwapchain | 4583.967 | 748.007 |
| Stage | BringStorageImage | 4602.718 | 18.746 |
| Stage | BringCommandRecording | 4603.007 | 0.284 |
| Shader | Engine/Shaders/ReSTIRViewport.spv | 4616.446 | 11.568 |
| Stage | BringComputePipeline | 4650.729 | 47.718 |
| Shader | Engine/Shaders/AtrousDenoise.spv | 4662.167 | 10.373 |
| Stage | BringDenoisePipeline | 4663.165 | 12.430 |
| Shader | Engine/Shaders/LuminanceReduce.spv | 4666.266 | 1.840 |
| Stage | BringLuminanceReduction | 4666.744 | 3.577 |
| Stage | BringSkyRecord | 4667.177 | 0.429 |
| Stage | BringMoonRecord | 4667.509 | 0.330 |
| Stage | BringPostRecord | 4667.816 | 0.306 |
| Stage | BringStarTables | 4668.099 | 0.280 |
| Stage | BringDescriptorSet | 4668.364 | 0.263 |
| Stage | BringCycleSlots | 4668.459 | 0.091 |
| Stage | BringImGui | 4671.003 | 2.543 |
| Shader | Engine/Shaders/ClusterCull.spv | 4687.739 | 14.000 |
| Shader | Engine/Shaders/HiZReduce.spv | 4691.445 | 2.238 |
| Shader | Engine/Shaders/SurfaceResolve.spv | 4709.660 | 17.567 |
| Shader | Engine/Shaders/VisibilityRaster.vert.spv | 4712.311 | 2.030 |
| Shader | Engine/Shaders/VisibilityRaster.frag.spv | 4714.414 | 2.100 |
| Shader | Engine/Shaders/ShadowRaster.vert.spv | 4718.186 | 2.395 |
| Shader | Engine/Shaders/ShadowRaster.frag.spv | 4720.714 | 2.524 |
| Shader | Engine/Shaders/ShadowResolve.spv | 4732.919 | 11.550 |
| Stage | BringVisibility | 4740.150 | 69.144 |
| Shader | Engine/Shaders/InterfaceRaster.vert.spv | 6220.886 | 3.641 |
| Shader | Engine/Shaders/InterfaceRaster.frag.spv | 6237.685 | 16.793 |

## Frames

146 frames recorded. Full per-frame rows: ProjectZero_TelemetryProbe_Frames.csv

| Column | Mean [ms] | Peak [ms] |
|---|---:|---:|
| Frame Δτ | 82.2400 | 100.0000 |
| CPU InputAndUi | 1.0008 | 69.7685 |
| CPU CelestialTick | 0.0184 | 0.0561 |
| CPU EditorAndPanels | 2.8940 | 22.2007 |
| CPU SimulationAndInterface | 0.0746 | 0.1610 |
| CPU ScenePush | 0.0132 | 0.0569 |
| CPU RecordAndPresent | 108.8039 | 277.0937 |
| CPU FrameCapWait | 0.0021 | 0.0348 |
| GPU Cull | 0.0913 | 0.2104 |
| GPU Raster | 7.2839 | 12.9397 |
| GPU HiZ | 0.0863 | 0.3306 |
| GPU Resolve | 1.5826 | 3.1643 |
| GPU Kernel | 101.0101 | 131.4042 |
| GPU Shadow | 0.0000 | 0.0000 |
| GPU ReSTIR | 101.0101 | 131.4042 |
| GPU Post | 2.7804 | 3.9701 |
| GPU Sky | 0.0000 | 0.0000 |
| GPU Volume | 0.0000 | 0.0000 |

144 of 146 frames carried valid device timestamps.
