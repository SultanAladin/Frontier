# ProjectZero_TelemetryReport — Telemetry Log

| Timestamp | Severity | Category | Record Description |
|:---|:---:|:---|:---|
| 2026-09-19 13:39:49.445 | INFO | Bootstrap | Project-Zero windowed ReSTIR renderer starting. |
| 2026-09-19 13:39:49.805 | INFO | Scene | Showcase: 92842 triangles, 117 instances, 790 clusters, 50 materials, 2212 luminaires, bounds [-30.00 -30.00 0.00]..[30.00 30.00 6.00] m |
| 2026-09-19 13:39:51.124 | INFO | Textures | Textures: 6 resident (0 placeholder), 64.0 MB with mips, decoded in 1317 ms |
| 2026-09-19 13:39:51.125 | INFO | Materials | Materials: 50 descriptors -> 50 records, 50 slabs (limit 1, 0 folded), 117 placements, 0 cameras, 0 punctual lights |
| 2026-09-19 13:39:51.142 | INFO | Interface | Panel light Low: rgb (0.000 0.008 0.042) from 4 figures, 5% coverage, 2214 luminaires now. |
| 2026-09-19 13:39:51.700 | INFO | Traversal | CWBVH: 92844 triangles → 15470 nodes, 1208.6 KB nodes + 6528.1 KB leaves (85.3 B/tri), SAH 12.95, built in 550.9 ms (spatial splits) |
| 2026-09-19 13:39:54.524 | INFO | Bootstrap | Window and Vulkan swapchain ready. |
| 2026-09-19 13:39:56.676 | INFO | Traversal | Two-level: 118 instances -> 118 BLASes over 92844 triangles, top level 180 nodes, shared blobs 1519.5 KB + 4352.1 KB, built in 464.8 ms |
| 2026-09-19 13:39:56.694 | INFO | Moons | 6 textures resident, moon slots 0..5. |
| 2026-09-19 13:39:56.697 | INFO | Stars | 9683 stars in 1024 cells uploaded to binding 23. |
| 2026-09-19 13:39:56.811 | INFO | Bootstrap | Entering render loop. |
| 2026-09-19 13:39:56.835 | INFO | Interface | Director ready: TAB switches screens. The card carries 2 converted vector segments. |
| 2026-09-19 13:39:56.949 | INFO | Audio | Panel bound to audio: drag the progress bar to change the engine note. |
| 2026-09-19 13:39:56.950 | INFO | Interface | Spatial interface ready: 14 figures, depth test off. |
| 2026-09-19 13:39:56.965 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 7 taps. |
| 2026-09-19 13:40:05.741 | INFO | Performance | CPU 83.91 ms/frame (11.9 fps, worst 100.00 ms over 60 frames), RSS 527 MiB |
| 2026-09-19 13:40:05.748 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 83.9064 [ms] |
| 2026-09-19 13:40:05.759 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:40:05.784 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 11.918 [fps] |
| 2026-09-19 13:40:05.794 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 60 [count] |
| 2026-09-19 13:40:05.824 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 527.043 [MiB] |
| 2026-09-19 13:40:05.832 | INFO | GpuTiming | GPU 139.56 ms total | cull 0.04 · raster 3.01 · HiZ 0.08 · resolve 0.76 · ReSTIR 135.66 · shadow 0.00 · post 4.30 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:40:05.858 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 139.559 [ms] |
| 2026-09-19 13:40:05.862 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.043008 [ms] |
| 2026-09-19 13:40:05.865 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.01251 [ms] |
| 2026-09-19 13:40:05.866 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.081952 [ms] |
| 2026-09-19 13:40:05.876 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.760512 [ms] |
| 2026-09-19 13:40:05.882 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 135.661 [ms] |
| 2026-09-19 13:40:05.885 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:40:05.890 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 4.29823 [ms] |
| 2026-09-19 13:40:05.892 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:40:05.894 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:40:05.896 | INFO | Visibility | Clusters 791 tested -> 717 frustum, 717 cone, 717 visible | draws 717+0, 85224 triangles |
| 2026-09-19 13:40:05.901 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:40:05.913 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 717 [count] |
| 2026-09-19 13:40:05.915 | INFO | TelemetryMetrics | Measurement: ClustersCone = 717 [count] |
| 2026-09-19 13:40:05.918 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 717 [count] |
| 2026-09-19 13:40:05.923 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 85224 [count] |
| 2026-09-19 13:40:05.925 | INFO | TelemetryMetrics | Measurement: DrawCalls = 717 [count] |
| 2026-09-19 13:40:05.928 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 97% of GPU frame |
| 2026-09-19 13:40:05.943 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 13:40:05.976 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:40:06.000 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:40:06.074 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:40:06.302 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:40:06.313 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:40:06.316 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 97.2069 [percent] |
| 2026-09-19 13:40:14.923 | INFO | Performance | CPU 80.37 ms/frame (12.1 fps, worst 100.00 ms over 63 frames), RSS 476 MiB |
| 2026-09-19 13:40:15.262 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 80.3695 [ms] |
| 2026-09-19 13:40:15.265 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:40:15.268 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 12.0971 [fps] |
| 2026-09-19 13:40:15.274 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 63 [count] |
| 2026-09-19 13:40:15.278 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 475.746 [MiB] |
| 2026-09-19 13:40:15.281 | INFO | GpuTiming | GPU 126.95 ms total | cull 0.05 · raster 3.57 · HiZ 0.09 · resolve 0.78 · ReSTIR 122.48 · shadow 0.00 · post 3.98 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:40:15.285 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 126.953 [ms] |
| 2026-09-19 13:40:15.297 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.045312 [ms] |
| 2026-09-19 13:40:15.299 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.5672 [ms] |
| 2026-09-19 13:40:15.302 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.085088 [ms] |
| 2026-09-19 13:40:15.307 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.77776 [ms] |
| 2026-09-19 13:40:15.312 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 122.478 [ms] |
| 2026-09-19 13:40:15.315 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:40:15.317 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 3.97667 [ms] |
| 2026-09-19 13:40:15.326 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:40:15.327 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:40:15.330 | INFO | Visibility | Clusters 791 tested -> 717 frustum, 717 cone, 717 visible | draws 717+0, 85062 triangles |
| 2026-09-19 13:40:15.332 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:40:15.334 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 717 [count] |
| 2026-09-19 13:40:15.335 | INFO | TelemetryMetrics | Measurement: ClustersCone = 717 [count] |
| 2026-09-19 13:40:15.342 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 717 [count] |
| 2026-09-19 13:40:15.345 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 85062 [count] |
| 2026-09-19 13:40:15.347 | INFO | TelemetryMetrics | Measurement: DrawCalls = 717 [count] |
| 2026-09-19 13:40:15.348 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 96% of GPU frame |
| 2026-09-19 13:40:15.352 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 13:40:15.359 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:40:15.362 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:40:15.364 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:40:15.367 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:40:15.380 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:40:15.424 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 96.4748 [percent] |
| 2026-09-19 13:40:25.164 | INFO | Performance | CPU 82.18 ms/frame (12.2 fps, worst 100.00 ms over 61 frames), RSS 479 MiB |
| 2026-09-19 13:40:25.580 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 82.1846 [ms] |
| 2026-09-19 13:40:25.583 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:40:25.586 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 12.1773 [fps] |
| 2026-09-19 13:40:25.593 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 61 [count] |
| 2026-09-19 13:40:25.596 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 479.18 [MiB] |
| 2026-09-19 13:40:25.604 | INFO | GpuTiming | GPU 144.71 ms total | cull 0.04 · raster 3.82 · HiZ 0.08 · resolve 0.77 · ReSTIR 139.99 · shadow 0.00 · post 3.43 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:40:25.618 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 144.706 [ms] |
| 2026-09-19 13:40:25.620 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.044992 [ms] |
| 2026-09-19 13:40:25.622 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.81997 [ms] |
| 2026-09-19 13:40:25.627 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.084096 [ms] |
| 2026-09-19 13:40:25.629 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.772032 [ms] |
| 2026-09-19 13:40:25.634 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 139.985 [ms] |
| 2026-09-19 13:40:25.636 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:40:25.639 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 3.4335 [ms] |
| 2026-09-19 13:40:25.644 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:40:25.649 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:40:25.652 | INFO | Visibility | Clusters 791 tested -> 717 frustum, 717 cone, 717 visible | draws 717+0, 85062 triangles |
| 2026-09-19 13:40:25.655 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:40:25.660 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 717 [count] |
| 2026-09-19 13:40:25.665 | INFO | TelemetryMetrics | Measurement: ClustersCone = 717 [count] |
| 2026-09-19 13:40:25.668 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 717 [count] |
| 2026-09-19 13:40:25.670 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 85062 [count] |
| 2026-09-19 13:40:25.672 | INFO | TelemetryMetrics | Measurement: DrawCalls = 717 [count] |
| 2026-09-19 13:40:25.679 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 97% of GPU frame |
| 2026-09-19 13:40:25.680 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 13:40:25.684 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:40:26.267 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:40:26.454 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:40:26.457 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:40:26.461 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:40:26.469 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 96.7375 [percent] |
| 2026-09-19 13:40:36.220 | INFO | Performance | CPU 78.43 ms/frame (12.4 fps, worst 100.00 ms over 65 frames), RSS 509 MiB |
| 2026-09-19 13:40:36.842 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 78.4343 [ms] |
| 2026-09-19 13:40:36.854 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:40:36.857 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 12.3879 [fps] |
| 2026-09-19 13:40:36.859 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 65 [count] |
| 2026-09-19 13:40:36.875 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 508.684 [MiB] |
| 2026-09-19 13:40:36.890 | INFO | GpuTiming | GPU 136.98 ms total | cull 0.05 · raster 3.11 · HiZ 0.08 · resolve 0.79 · ReSTIR 132.95 · shadow 0.00 · post 4.33 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:40:36.895 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 136.98 [ms] |
| 2026-09-19 13:40:36.905 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.047072 [ms] |
| 2026-09-19 13:40:36.907 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.10832 [ms] |
| 2026-09-19 13:40:36.909 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.084064 [ms] |
| 2026-09-19 13:40:36.916 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.790528 [ms] |
| 2026-09-19 13:40:36.925 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 132.95 [ms] |
| 2026-09-19 13:40:36.927 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:40:36.936 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 4.33484 [ms] |
| 2026-09-19 13:40:36.938 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:40:36.942 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:40:36.953 | INFO | Visibility | Clusters 791 tested -> 742 frustum, 742 cone, 741 visible | draws 742+0, 87780 triangles |
| 2026-09-19 13:40:36.958 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:40:36.961 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 742 [count] |
| 2026-09-19 13:40:36.972 | INFO | TelemetryMetrics | Measurement: ClustersCone = 742 [count] |
| 2026-09-19 13:40:36.974 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:40:36.977 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87780 [count] |
| 2026-09-19 13:40:37.022 | INFO | TelemetryMetrics | Measurement: DrawCalls = 742 [count] |
| 2026-09-19 13:40:37.025 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 97% of GPU frame |
| 2026-09-19 13:40:37.122 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 13:40:37.156 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:40:37.172 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:40:37.177 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:40:37.179 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:40:37.188 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:40:37.190 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 97.058 [percent] |
| 2026-09-19 13:40:47.245 | INFO | Performance | CPU 85.08 ms/frame (12.1 fps, worst 100.00 ms over 59 frames), RSS 651 MiB |
| 2026-09-19 13:40:48.564 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 85.0847 [ms] |
| 2026-09-19 13:40:48.585 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:40:48.771 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 12.1427 [fps] |
| 2026-09-19 13:40:48.778 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 59 [count] |
| 2026-09-19 13:40:48.780 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 651.059 [MiB] |
| 2026-09-19 13:40:48.782 | INFO | GpuTiming | GPU 196.76 ms total | cull 0.04 · raster 2.44 · HiZ 0.09 · resolve 0.74 · ReSTIR 193.44 · shadow 0.00 · post 4.81 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:40:48.786 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 196.756 [ms] |
| 2026-09-19 13:40:48.795 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.044352 [ms] |
| 2026-09-19 13:40:48.797 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 2.43718 [ms] |
| 2026-09-19 13:40:48.799 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.087968 [ms] |
| 2026-09-19 13:40:48.801 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.742208 [ms] |
| 2026-09-19 13:40:48.814 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 193.444 [ms] |
| 2026-09-19 13:40:48.816 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:40:48.819 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 4.81261 [ms] |
| 2026-09-19 13:40:48.821 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:40:48.828 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:40:48.830 | INFO | Visibility | Clusters 791 tested -> 584 frustum, 584 cone, 584 visible | draws 584+0, 69274 triangles |
| 2026-09-19 13:40:48.833 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:40:48.835 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 584 [count] |
| 2026-09-19 13:40:48.837 | INFO | TelemetryMetrics | Measurement: ClustersCone = 584 [count] |
| 2026-09-19 13:40:48.845 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 584 [count] |
| 2026-09-19 13:40:48.847 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 69274 [count] |
| 2026-09-19 13:40:48.849 | INFO | TelemetryMetrics | Measurement: DrawCalls = 584 [count] |
| 2026-09-19 13:40:48.851 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 98% of GPU frame |
| 2026-09-19 13:40:48.861 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 13:40:48.863 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:40:48.865 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:40:48.867 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:40:48.869 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:40:48.880 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:40:48.881 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 98.3168 [percent] |
| 2026-09-19 13:40:57.882 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 7 taps. |
| 2026-09-19 13:40:59.444 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 7 taps. |
| 2026-09-19 13:41:02.327 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 7 taps. |
| 2026-09-19 13:41:02.765 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 7 taps. |
| 2026-09-19 13:41:02.953 | INFO | Performance | CPU 80.88 ms/frame (12.1 fps, worst 100.00 ms over 62 frames), RSS 694 MiB |
| 2026-09-19 13:41:02.987 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 80.8797 [ms] |
| 2026-09-19 13:41:02.990 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:41:02.992 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 12.0791 [fps] |
| 2026-09-19 13:41:02.995 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 62 [count] |
| 2026-09-19 13:41:02.997 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 694.328 [MiB] |
| 2026-09-19 13:41:03.004 | INFO | GpuTiming | GPU 212.87 ms total | cull 0.04 · raster 2.48 · HiZ 0.09 · resolve 0.77 · ReSTIR 209.48 · shadow 0.00 · post 4.63 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:41:03.009 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 212.866 [ms] |
| 2026-09-19 13:41:03.011 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.043392 [ms] |
| 2026-09-19 13:41:03.014 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 2.4825 [ms] |
| 2026-09-19 13:41:03.020 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.088064 [ms] |
| 2026-09-19 13:41:03.023 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.774592 [ms] |
| 2026-09-19 13:41:03.027 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 209.478 [ms] |
| 2026-09-19 13:41:03.029 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:41:03.035 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 4.63231 [ms] |
| 2026-09-19 13:41:03.037 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:41:03.042 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:41:03.072 | INFO | Visibility | Clusters 791 tested -> 584 frustum, 584 cone, 584 visible | draws 584+0, 69274 triangles |
| 2026-09-19 13:41:03.081 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:41:03.087 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 584 [count] |
| 2026-09-19 13:41:03.090 | INFO | TelemetryMetrics | Measurement: ClustersCone = 584 [count] |
| 2026-09-19 13:41:03.092 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 584 [count] |
| 2026-09-19 13:41:03.097 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 69274 [count] |
| 2026-09-19 13:41:03.100 | INFO | TelemetryMetrics | Measurement: DrawCalls = 584 [count] |
| 2026-09-19 13:41:03.103 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 98% of GPU frame |
| 2026-09-19 13:41:03.107 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 13:41:03.114 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:41:03.117 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:41:03.120 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:41:03.122 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:41:03.130 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:41:03.133 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 98.4081 [percent] |
| 2026-09-19 13:41:03.136 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 7 taps. |
| 2026-09-19 13:41:03.664 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 7 taps. |
| 2026-09-19 13:41:09.399 | INFO | Performance | CPU 71.60 ms/frame (13.2 fps, worst 100.00 ms over 70 frames), RSS 705 MiB |
| 2026-09-19 13:41:09.401 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 71.6023 [ms] |
| 2026-09-19 13:41:09.404 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:41:09.406 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 13.2087 [fps] |
| 2026-09-19 13:41:09.408 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 70 [count] |
| 2026-09-19 13:41:09.410 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 704.914 [MiB] |
| 2026-09-19 13:41:09.412 | INFO | GpuTiming | GPU 52.25 ms total | cull 0.05 · raster 2.58 · HiZ 0.05 · resolve 0.69 · ReSTIR 48.89 · shadow 0.00 · post 1.36 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:41:09.417 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 52.2515 [ms] |
| 2026-09-19 13:41:09.419 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.046464 [ms] |
| 2026-09-19 13:41:09.422 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 2.5808 [ms] |
| 2026-09-19 13:41:09.428 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.0456 [ms] |
| 2026-09-19 13:41:09.432 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.687232 [ms] |
| 2026-09-19 13:41:09.435 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 48.8914 [ms] |
| 2026-09-19 13:41:09.437 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:41:09.442 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.35929 [ms] |
| 2026-09-19 13:41:09.444 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:41:09.448 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:41:09.450 | INFO | Visibility | Clusters 791 tested -> 488 frustum, 488 cone, 488 visible | draws 477+11, 57438 triangles |
| 2026-09-19 13:41:09.455 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:41:09.462 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 488 [count] |
| 2026-09-19 13:41:09.471 | INFO | TelemetryMetrics | Measurement: ClustersCone = 488 [count] |
| 2026-09-19 13:41:09.475 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 488 [count] |
| 2026-09-19 13:41:09.478 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 57438 [count] |
| 2026-09-19 13:41:09.480 | INFO | TelemetryMetrics | Measurement: DrawCalls = 488 [count] |
| 2026-09-19 13:41:09.483 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 94% of GPU frame |
| 2026-09-19 13:41:09.487 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:41:09.493 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:41:09.495 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:41:09.498 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:41:09.501 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:41:09.506 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 13:41:09.509 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.5694 [percent] |
| 2026-09-19 13:41:15.129 | INFO | Performance | CPU 59.04 ms/frame (15.6 fps, worst 100.00 ms over 86 frames), RSS 722 MiB |
| 2026-09-19 13:41:15.131 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 59.0367 [ms] |
| 2026-09-19 13:41:15.138 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:41:15.140 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.6146 [fps] |
| 2026-09-19 13:41:15.143 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 86 [count] |
| 2026-09-19 13:41:15.145 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 721.902 [MiB] |
| 2026-09-19 13:41:15.149 | INFO | GpuTiming | GPU 65.24 ms total | cull 0.04 · raster 3.11 · HiZ 0.05 · resolve 0.38 · ReSTIR 61.67 · shadow 0.00 · post 1.69 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:41:15.154 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 65.2435 [ms] |
| 2026-09-19 13:41:15.160 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.04416 [ms] |
| 2026-09-19 13:41:15.172 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.10608 [ms] |
| 2026-09-19 13:41:15.173 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047008 [ms] |
| 2026-09-19 13:41:15.175 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.380928 [ms] |
| 2026-09-19 13:41:15.178 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 61.6653 [ms] |
| 2026-09-19 13:41:15.179 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:41:15.187 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.69363 [ms] |
| 2026-09-19 13:41:15.192 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:41:15.195 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:41:15.197 | INFO | Visibility | Clusters 791 tested -> 734 frustum, 734 cone, 723 visible | draws 723+0, 85696 triangles |
| 2026-09-19 13:41:15.205 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:41:15.206 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 734 [count] |
| 2026-09-19 13:41:15.208 | INFO | TelemetryMetrics | Measurement: ClustersCone = 734 [count] |
| 2026-09-19 13:41:15.210 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 723 [count] |
| 2026-09-19 13:41:15.212 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 85696 [count] |
| 2026-09-19 13:41:15.221 | INFO | TelemetryMetrics | Measurement: DrawCalls = 723 [count] |
| 2026-09-19 13:41:15.223 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 95% of GPU frame |
| 2026-09-19 13:41:15.235 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:41:15.241 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:41:15.243 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:41:15.246 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:41:15.257 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:41:15.260 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:41:15.262 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 94.5157 [percent] |
| 2026-09-19 13:41:21.092 | INFO | Performance | CPU 62.86 ms/frame (16.0 fps, worst 100.00 ms over 80 frames), RSS 746 MiB |
| 2026-09-19 13:41:21.392 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 62.8594 [ms] |
| 2026-09-19 13:41:21.395 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:41:21.398 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.962 [fps] |
| 2026-09-19 13:41:21.400 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 80 [count] |
| 2026-09-19 13:41:21.403 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 745.762 [MiB] |
| 2026-09-19 13:41:21.410 | INFO | GpuTiming | GPU 57.36 ms total | cull 0.04 · raster 3.62 · HiZ 0.05 · resolve 0.34 · ReSTIR 53.30 · shadow 0.00 · post 1.78 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:41:21.425 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 57.3553 [ms] |
| 2026-09-19 13:41:21.428 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.044448 [ms] |
| 2026-09-19 13:41:21.432 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.61914 [ms] |
| 2026-09-19 13:41:21.435 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.04832 [ms] |
| 2026-09-19 13:41:21.437 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.338464 [ms] |
| 2026-09-19 13:41:21.443 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 53.305 [ms] |
| 2026-09-19 13:41:21.448 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:41:21.451 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.78192 [ms] |
| 2026-09-19 13:41:21.452 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:41:21.454 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:41:21.469 | INFO | Visibility | Clusters 791 tested -> 713 frustum, 713 cone, 705 visible | draws 705+0, 83948 triangles |
| 2026-09-19 13:41:21.477 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:41:21.480 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 713 [count] |
| 2026-09-19 13:41:21.482 | INFO | TelemetryMetrics | Measurement: ClustersCone = 713 [count] |
| 2026-09-19 13:41:21.484 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 705 [count] |
| 2026-09-19 13:41:21.486 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 83948 [count] |
| 2026-09-19 13:41:21.488 | INFO | TelemetryMetrics | Measurement: DrawCalls = 705 [count] |
| 2026-09-19 13:41:21.497 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:41:21.502 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:41:21.504 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:41:21.513 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:41:21.515 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:41:21.517 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:41:21.519 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:41:21.522 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.9381 [percent] |
| 2026-09-19 13:41:26.781 | INFO | Performance | CPU 58.90 ms/frame (16.9 fps, worst 100.00 ms over 85 frames), RSS 773 MiB |
| 2026-09-19 13:41:27.263 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 58.8966 [ms] |
| 2026-09-19 13:41:27.271 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:41:27.274 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.9234 [fps] |
| 2026-09-19 13:41:27.283 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 85 [count] |
| 2026-09-19 13:41:27.286 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 772.797 [MiB] |
| 2026-09-19 13:41:27.288 | INFO | GpuTiming | GPU 54.84 ms total | cull 0.05 · raster 2.92 · HiZ 0.05 · resolve 0.12 · ReSTIR 51.72 · shadow 0.00 · post 1.89 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:41:27.297 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 54.8442 [ms] |
| 2026-09-19 13:41:27.299 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.045248 [ms] |
| 2026-09-19 13:41:27.301 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 2.91574 [ms] |
| 2026-09-19 13:41:27.303 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.049152 [ms] |
| 2026-09-19 13:41:27.305 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.115712 [ms] |
| 2026-09-19 13:41:27.316 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 51.7183 [ms] |
| 2026-09-19 13:41:27.319 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:41:27.321 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.8881 [ms] |
| 2026-09-19 13:41:27.323 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:41:27.333 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:41:27.336 | INFO | Visibility | Clusters 791 tested -> 652 frustum, 652 cone, 515 visible | draws 573+9, 71194 triangles |
| 2026-09-19 13:41:27.353 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:41:27.371 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 652 [count] |
| 2026-09-19 13:41:27.382 | INFO | TelemetryMetrics | Measurement: ClustersCone = 652 [count] |
| 2026-09-19 13:41:27.384 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 515 [count] |
| 2026-09-19 13:41:27.388 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 71194 [count] |
| 2026-09-19 13:41:27.391 | INFO | TelemetryMetrics | Measurement: DrawCalls = 582 [count] |
| 2026-09-19 13:41:27.403 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 94% of GPU frame |
| 2026-09-19 13:41:27.407 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:41:27.415 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:41:27.421 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:41:27.423 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:41:27.443 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:41:27.457 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:41:27.459 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 94.3005 [percent] |
| 2026-09-19 13:41:32.867 | INFO | Performance | CPU 51.13 ms/frame (19.1 fps, worst 100.00 ms over 98 frames), RSS 772 MiB |
| 2026-09-19 13:41:33.195 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 51.1265 [ms] |
| 2026-09-19 13:41:33.206 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:41:33.209 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 19.0999 [fps] |
| 2026-09-19 13:41:33.212 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 98 [count] |
| 2026-09-19 13:41:33.223 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 771.891 [MiB] |
| 2026-09-19 13:41:33.227 | INFO | GpuTiming | GPU 37.42 ms total | cull 0.05 · raster 0.03 · HiZ 0.05 · resolve 0.06 · ReSTIR 37.23 · shadow 0.00 · post 1.92 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:41:33.230 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 37.4237 [ms] |
| 2026-09-19 13:41:33.239 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.05024 [ms] |
| 2026-09-19 13:41:33.241 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 0.031424 [ms] |
| 2026-09-19 13:41:33.244 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045408 [ms] |
| 2026-09-19 13:41:33.251 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.063424 [ms] |
| 2026-09-19 13:41:33.254 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 37.2332 [ms] |
| 2026-09-19 13:41:33.257 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:41:33.263 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.92237 [ms] |
| 2026-09-19 13:41:33.269 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:41:33.306 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:41:33.308 | INFO | Visibility | Clusters 791 tested -> 3 frustum, 3 cone, 3 visible | draws 3+0, 94 triangles |
| 2026-09-19 13:41:33.315 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:41:33.320 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 3 [count] |
| 2026-09-19 13:41:33.322 | INFO | TelemetryMetrics | Measurement: ClustersCone = 3 [count] |
| 2026-09-19 13:41:33.325 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 3 [count] |
| 2026-09-19 13:41:33.339 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 94 [count] |
| 2026-09-19 13:41:33.352 | INFO | TelemetryMetrics | Measurement: DrawCalls = 3 [count] |
| 2026-09-19 13:41:33.356 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 99% of GPU frame |
| 2026-09-19 13:41:33.369 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:41:33.371 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:41:33.379 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:41:33.393 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:41:33.397 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:41:33.406 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 13:41:33.411 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 99.491 [percent] |
| 2026-09-19 13:41:38.396 | INFO | Performance | CPU 38.21 ms/frame (26.2 fps, worst 100.00 ms over 131 frames), RSS 796 MiB |
| 2026-09-19 13:41:39.567 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 38.2067 [ms] |
| 2026-09-19 13:41:39.574 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:41:39.577 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 26.1599 [fps] |
| 2026-09-19 13:41:39.579 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 131 [count] |
| 2026-09-19 13:41:39.593 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 795.793 [MiB] |
| 2026-09-19 13:41:39.596 | INFO | GpuTiming | GPU 33.21 ms total | cull 0.04 · raster 0.11 · HiZ 0.05 · resolve 0.06 · ReSTIR 32.96 · shadow 0.00 · post 1.46 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:41:39.616 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 33.2112 [ms] |
| 2026-09-19 13:41:39.627 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.036256 [ms] |
| 2026-09-19 13:41:39.632 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 0.10816 [ms] |
| 2026-09-19 13:41:39.640 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045056 [ms] |
| 2026-09-19 13:41:39.645 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.059136 [ms] |
| 2026-09-19 13:41:39.649 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 32.9626 [ms] |
| 2026-09-19 13:41:39.663 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:41:39.666 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.46045 [ms] |
| 2026-09-19 13:41:39.680 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:41:39.683 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:41:39.685 | INFO | Visibility | Clusters 791 tested -> 19 frustum, 19 cone, 19 visible | draws 19+0, 1922 triangles |
| 2026-09-19 13:41:39.694 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:41:39.704 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 19 [count] |
| 2026-09-19 13:41:39.711 | INFO | TelemetryMetrics | Measurement: ClustersCone = 19 [count] |
| 2026-09-19 13:41:39.713 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 19 [count] |
| 2026-09-19 13:41:39.732 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 1922 [count] |
| 2026-09-19 13:41:39.735 | INFO | TelemetryMetrics | Measurement: DrawCalls = 19 [count] |
| 2026-09-19 13:41:39.746 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 99% of GPU frame |
| 2026-09-19 13:41:39.750 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:41:39.758 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:41:39.761 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:41:39.763 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:41:39.765 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:41:39.767 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:41:39.779 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 99.2514 [percent] |
| 2026-09-19 13:41:44.740 | INFO | Performance | CPU 30.60 ms/frame (35.5 fps, worst 100.00 ms over 165 frames), RSS 796 MiB |
| 2026-09-19 13:41:44.750 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 30.5953 [ms] |
| 2026-09-19 13:41:44.770 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:41:44.776 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 35.5444 [fps] |
| 2026-09-19 13:41:44.779 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 165 [count] |
| 2026-09-19 13:41:44.782 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 795.871 [MiB] |
| 2026-09-19 13:41:44.802 | INFO | GpuTiming | GPU 30.09 ms total | cull 0.04 · raster 1.77 · HiZ 0.05 · resolve 0.14 · ReSTIR 28.10 · shadow 0.00 · post 1.19 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:41:44.815 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 30.0903 [ms] |
| 2026-09-19 13:41:44.820 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.040896 [ms] |
| 2026-09-19 13:41:44.829 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 1.77242 [ms] |
| 2026-09-19 13:41:44.832 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045056 [ms] |
| 2026-09-19 13:41:44.851 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.13536 [ms] |
| 2026-09-19 13:41:44.854 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 28.0966 [ms] |
| 2026-09-19 13:41:44.866 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:41:44.869 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.1895 [ms] |
| 2026-09-19 13:41:44.871 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:41:44.880 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:41:44.882 | INFO | Visibility | Clusters 791 tested -> 341 frustum, 341 cone, 331 visible | draws 331+0, 39442 triangles |
| 2026-09-19 13:41:44.900 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:41:44.903 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 341 [count] |
| 2026-09-19 13:41:44.917 | INFO | TelemetryMetrics | Measurement: ClustersCone = 341 [count] |
| 2026-09-19 13:41:44.920 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 331 [count] |
| 2026-09-19 13:41:44.931 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 39442 [count] |
| 2026-09-19 13:41:44.934 | INFO | TelemetryMetrics | Measurement: DrawCalls = 331 [count] |
| 2026-09-19 13:41:44.936 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:41:44.944 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:41:44.950 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:41:44.952 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:41:44.954 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:41:44.968 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:41:44.970 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:41:44.981 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.3742 [percent] |
| 2026-09-19 13:41:50.383 | INFO | Performance | CPU 49.06 ms/frame (21.3 fps, worst 100.00 ms over 102 frames), RSS 800 MiB |
| 2026-09-19 13:41:50.416 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 49.0579 [ms] |
| 2026-09-19 13:41:50.420 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:41:50.422 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 21.3027 [fps] |
| 2026-09-19 13:41:50.431 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 102 [count] |
| 2026-09-19 13:41:50.434 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 799.781 [MiB] |
| 2026-09-19 13:41:50.436 | INFO | GpuTiming | GPU 58.29 ms total | cull 0.05 · raster 3.67 · HiZ 0.05 · resolve 0.80 · ReSTIR 53.72 · shadow 0.00 · post 1.84 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:41:50.439 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 58.2858 [ms] |
| 2026-09-19 13:41:50.446 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.047712 [ms] |
| 2026-09-19 13:41:50.448 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.67117 [ms] |
| 2026-09-19 13:41:50.451 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045344 [ms] |
| 2026-09-19 13:41:50.453 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.800768 [ms] |
| 2026-09-19 13:41:50.455 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 53.7208 [ms] |
| 2026-09-19 13:41:50.457 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:41:50.469 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.83926 [ms] |
| 2026-09-19 13:41:50.536 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:41:50.538 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:41:50.540 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:41:50.547 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:41:50.549 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:41:50.551 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:41:50.553 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:41:50.555 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:41:50.557 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:41:50.563 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 92% of GPU frame |
| 2026-09-19 13:41:50.566 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:41:50.568 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:41:50.570 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:41:50.573 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:41:50.575 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:41:50.579 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:41:50.581 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.1679 [percent] |
| 2026-09-19 13:41:56.493 | INFO | Performance | CPU 67.53 ms/frame (15.2 fps, worst 100.00 ms over 75 frames), RSS 800 MiB |
| 2026-09-19 13:41:56.495 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 67.5335 [ms] |
| 2026-09-19 13:41:56.501 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:41:56.503 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.2212 [fps] |
| 2026-09-19 13:41:56.505 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 75 [count] |
| 2026-09-19 13:41:56.507 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 799.93 [MiB] |
| 2026-09-19 13:41:56.509 | INFO | GpuTiming | GPU 71.47 ms total | cull 0.05 · raster 4.63 · HiZ 0.05 · resolve 1.09 · ReSTIR 65.66 · shadow 0.00 · post 1.64 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:41:56.512 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 71.4699 [ms] |
| 2026-09-19 13:41:56.520 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.046144 [ms] |
| 2026-09-19 13:41:56.522 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 4.63075 [ms] |
| 2026-09-19 13:41:56.525 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047328 [ms] |
| 2026-09-19 13:41:56.529 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.08547 [ms] |
| 2026-09-19 13:41:56.536 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 65.6602 [ms] |
| 2026-09-19 13:41:56.538 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:41:56.540 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.64048 [ms] |
| 2026-09-19 13:41:56.542 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:41:56.544 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:41:56.546 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:41:56.553 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:41:56.556 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:41:56.558 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:41:56.560 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:41:56.573 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:41:56.575 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:41:56.578 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 92% of GPU frame |
| 2026-09-19 13:41:56.590 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:41:56.592 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:41:56.608 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:41:56.622 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:41:56.628 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:41:56.637 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:41:56.639 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 91.8711 [percent] |
| 2026-09-19 13:42:02.161 | INFO | Performance | CPU 70.48 ms/frame (14.5 fps, worst 100.00 ms over 72 frames), RSS 800 MiB |
| 2026-09-19 13:42:02.163 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 70.4761 [ms] |
| 2026-09-19 13:42:02.167 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:42:02.173 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.467 [fps] |
| 2026-09-19 13:42:02.176 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 72 [count] |
| 2026-09-19 13:42:02.178 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 800.082 [MiB] |
| 2026-09-19 13:42:02.180 | INFO | GpuTiming | GPU 69.28 ms total | cull 0.05 · raster 3.63 · HiZ 0.05 · resolve 0.81 · ReSTIR 64.75 · shadow 0.00 · post 1.61 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:42:02.196 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 69.2843 [ms] |
| 2026-09-19 13:42:02.198 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.04576 [ms] |
| 2026-09-19 13:42:02.206 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.6271 [ms] |
| 2026-09-19 13:42:02.207 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 13:42:02.209 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.811008 [ms] |
| 2026-09-19 13:42:02.212 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 64.7534 [ms] |
| 2026-09-19 13:42:02.214 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:42:02.224 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.60909 [ms] |
| 2026-09-19 13:42:02.227 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:42:02.229 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:42:02.232 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:42:02.234 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:42:02.240 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:42:02.242 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:42:02.245 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:42:02.259 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:42:02.261 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:42:02.264 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:42:02.271 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:42:02.272 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:42:02.274 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:42:02.276 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:42:02.283 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:42:02.290 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:42:02.292 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.4603 [percent] |
| 2026-09-19 13:42:08.333 | INFO | Performance | CPU 69.28 ms/frame (14.4 fps, worst 100.00 ms over 73 frames), RSS 800 MiB |
| 2026-09-19 13:42:08.698 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 69.2843 [ms] |
| 2026-09-19 13:42:08.919 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:42:09.222 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.375 [fps] |
| 2026-09-19 13:42:09.286 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 73 [count] |
| 2026-09-19 13:42:09.339 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 800.031 [MiB] |
| 2026-09-19 13:42:09.435 | INFO | GpuTiming | GPU 70.18 ms total | cull 0.04 · raster 3.75 · HiZ 0.05 · resolve 0.89 · ReSTIR 65.44 · shadow 0.00 · post 1.56 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:42:09.534 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 70.1775 [ms] |
| 2026-09-19 13:42:09.539 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.044928 [ms] |
| 2026-09-19 13:42:09.543 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.75389 [ms] |
| 2026-09-19 13:42:09.556 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.04816 [ms] |
| 2026-09-19 13:42:09.559 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.893152 [ms] |
| 2026-09-19 13:42:09.588 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 65.4374 [ms] |
| 2026-09-19 13:42:09.592 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:42:09.608 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.56477 [ms] |
| 2026-09-19 13:42:09.610 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:42:09.636 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:42:09.643 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:42:09.655 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:42:09.657 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:42:09.675 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:42:09.688 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:42:09.702 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:42:09.719 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:42:09.805 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:42:09.822 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:42:09.824 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:42:09.834 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:42:09.837 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:42:09.839 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:42:09.840 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:42:09.843 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.2455 [percent] |
| 2026-09-19 13:42:15.975 | INFO | Performance | CPU 66.44 ms/frame (14.6 fps, worst 100.00 ms over 76 frames), RSS 800 MiB |
| 2026-09-19 13:42:15.977 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 66.4401 [ms] |
| 2026-09-19 13:42:15.980 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:42:15.982 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.5962 [fps] |
| 2026-09-19 13:42:15.985 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 76 [count] |
| 2026-09-19 13:42:15.987 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 800.262 [MiB] |
| 2026-09-19 13:42:15.990 | INFO | GpuTiming | GPU 70.09 ms total | cull 0.05 · raster 4.20 · HiZ 0.05 · resolve 0.93 · ReSTIR 64.86 · shadow 0.00 · post 1.83 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:42:15.998 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 70.0867 [ms] |
| 2026-09-19 13:42:16.001 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.0472 [ms] |
| 2026-09-19 13:42:16.003 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 4.19651 [ms] |
| 2026-09-19 13:42:16.005 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.0456 [ms] |
| 2026-09-19 13:42:16.010 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.933728 [ms] |
| 2026-09-19 13:42:16.012 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 64.8636 [ms] |
| 2026-09-19 13:42:16.015 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:42:16.019 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.83168 [ms] |
| 2026-09-19 13:42:16.021 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:42:16.027 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:42:16.029 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:42:16.031 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:42:16.033 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:42:16.036 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:42:16.040 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:42:16.042 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:42:16.044 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:42:16.048 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:42:16.051 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:42:16.056 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:42:16.058 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:42:16.060 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:42:16.062 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:42:16.065 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:42:16.068 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.5477 [percent] |
| 2026-09-19 13:42:21.750 | INFO | Performance | CPU 64.83 ms/frame (15.2 fps, worst 100.00 ms over 78 frames), RSS 800 MiB |
| 2026-09-19 13:42:21.752 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 64.8318 [ms] |
| 2026-09-19 13:42:21.760 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:42:21.763 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.2093 [fps] |
| 2026-09-19 13:42:21.777 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 78 [count] |
| 2026-09-19 13:42:21.778 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 800.367 [MiB] |
| 2026-09-19 13:42:21.783 | INFO | GpuTiming | GPU 69.31 ms total | cull 0.04 · raster 3.98 · HiZ 0.05 · resolve 1.00 · ReSTIR 64.24 · shadow 0.00 · post 1.50 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:42:21.799 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 69.3088 [ms] |
| 2026-09-19 13:42:21.803 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.044096 [ms] |
| 2026-09-19 13:42:21.979 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.98134 [ms] |
| 2026-09-19 13:42:22.001 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.04688 [ms] |
| 2026-09-19 13:42:22.003 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.999424 [ms] |
| 2026-09-19 13:42:22.010 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 64.2371 [ms] |
| 2026-09-19 13:42:22.013 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:42:22.019 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.50121 [ms] |
| 2026-09-19 13:42:22.026 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:42:22.032 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:42:22.036 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:42:22.045 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:42:22.050 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:42:22.053 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:42:22.060 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:42:22.066 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:42:22.069 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:42:22.076 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:42:22.083 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:42:22.085 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:42:22.092 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:42:22.097 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:42:22.100 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:42:22.102 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:42:22.108 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.6824 [percent] |
| 2026-09-19 13:42:28.438 | INFO | Performance | CPU 63.67 ms/frame (15.6 fps, worst 100.00 ms over 79 frames), RSS 807 MiB |
| 2026-09-19 13:42:28.440 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 63.6719 [ms] |
| 2026-09-19 13:42:28.443 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:42:28.447 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.6395 [fps] |
| 2026-09-19 13:42:28.452 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 79 [count] |
| 2026-09-19 13:42:28.455 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 806.969 [MiB] |
| 2026-09-19 13:42:28.457 | INFO | GpuTiming | GPU 65.76 ms total | cull 0.04 · raster 3.96 · HiZ 0.05 · resolve 0.66 · ReSTIR 61.05 · shadow 0.00 · post 1.48 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:42:28.475 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 65.7633 [ms] |
| 2026-09-19 13:42:28.482 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.043456 [ms] |
| 2026-09-19 13:42:28.484 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.95808 [ms] |
| 2026-09-19 13:42:28.487 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 13:42:28.489 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.663968 [ms] |
| 2026-09-19 13:42:28.499 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 61.0507 [ms] |
| 2026-09-19 13:42:28.500 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:42:28.500 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.48477 [ms] |
| 2026-09-19 13:42:28.505 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:42:28.512 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:42:28.516 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:42:28.519 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:42:28.523 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:42:28.526 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:42:28.532 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:42:28.533 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:42:28.535 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:42:28.541 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:42:28.547 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:42:28.549 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:42:28.550 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:42:28.552 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:42:28.558 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:42:28.560 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:42:28.562 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.834 [percent] |
| 2026-09-19 13:42:33.908 | INFO | Performance | CPU 63.36 ms/frame (15.8 fps, worst 100.00 ms over 79 frames), RSS 994 MiB |
| 2026-09-19 13:42:33.910 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 63.3564 [ms] |
| 2026-09-19 13:42:33.915 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:42:33.917 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.7979 [fps] |
| 2026-09-19 13:42:33.920 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 79 [count] |
| 2026-09-19 13:42:33.922 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 993.863 [MiB] |
| 2026-09-19 13:42:33.929 | INFO | GpuTiming | GPU 64.56 ms total | cull 0.04 · raster 3.28 · HiZ 0.05 · resolve 0.69 · ReSTIR 60.50 · shadow 0.00 · post 1.45 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:42:33.934 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 64.561 [ms] |
| 2026-09-19 13:42:33.937 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.043776 [ms] |
| 2026-09-19 13:42:33.942 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.28333 [ms] |
| 2026-09-19 13:42:33.944 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047264 [ms] |
| 2026-09-19 13:42:33.951 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.68608 [ms] |
| 2026-09-19 13:42:33.953 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 60.5005 [ms] |
| 2026-09-19 13:42:33.959 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:42:33.962 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.45351 [ms] |
| 2026-09-19 13:42:33.967 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:42:33.969 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:42:33.974 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:42:33.977 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:42:33.979 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:42:33.984 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:42:33.989 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:42:33.992 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:42:33.995 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:42:34.006 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 94% of GPU frame |
| 2026-09-19 13:42:34.010 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:42:34.012 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:42:34.017 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:42:34.021 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:42:34.025 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:42:34.027 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:42:34.029 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.7107 [percent] |
| 2026-09-19 13:42:40.099 | INFO | Performance | CPU 63.15 ms/frame (15.9 fps, worst 100.00 ms over 80 frames), RSS 1163 MiB |
| 2026-09-19 13:42:40.102 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 63.1501 [ms] |
| 2026-09-19 13:42:40.110 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:42:40.111 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.9059 [fps] |
| 2026-09-19 13:42:40.134 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 80 [count] |
| 2026-09-19 13:42:40.141 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1163.12 [MiB] |
| 2026-09-19 13:42:40.143 | INFO | GpuTiming | GPU 67.73 ms total | cull 0.04 · raster 3.38 · HiZ 0.05 · resolve 1.00 · ReSTIR 63.27 · shadow 0.00 · post 1.44 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:42:40.151 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 67.7347 [ms] |
| 2026-09-19 13:42:40.157 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.044992 [ms] |
| 2026-09-19 13:42:40.163 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.37603 [ms] |
| 2026-09-19 13:42:40.166 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.046784 [ms] |
| 2026-09-19 13:42:40.176 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.00061 [ms] |
| 2026-09-19 13:42:40.180 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 63.2662 [ms] |
| 2026-09-19 13:42:40.183 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:42:40.185 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.44061 [ms] |
| 2026-09-19 13:42:40.198 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:42:40.200 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:42:40.210 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:42:40.213 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:42:40.216 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:42:40.232 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:42:40.232 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:42:40.245 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:42:40.250 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:42:40.260 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:42:40.265 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:42:40.267 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:42:40.275 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:42:40.278 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:42:40.281 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:42:40.283 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:42:40.295 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.4031 [percent] |
| 2026-09-19 13:42:48.453 | INFO | Performance | CPU 71.14 ms/frame (14.6 fps, worst 100.00 ms over 71 frames), RSS 1163 MiB |
| 2026-09-19 13:42:48.696 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 71.1419 [ms] |
| 2026-09-19 13:42:49.026 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:42:49.097 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.5759 [fps] |
| 2026-09-19 13:42:49.100 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 71 [count] |
| 2026-09-19 13:42:49.117 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1163.13 [MiB] |
| 2026-09-19 13:42:49.129 | INFO | GpuTiming | GPU 69.31 ms total | cull 0.04 · raster 3.58 · HiZ 0.05 · resolve 1.09 · ReSTIR 64.54 · shadow 0.00 · post 1.82 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:42:49.133 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 69.3098 [ms] |
| 2026-09-19 13:42:49.148 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.044448 [ms] |
| 2026-09-19 13:42:49.149 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.58406 [ms] |
| 2026-09-19 13:42:49.166 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047168 [ms] |
| 2026-09-19 13:42:49.167 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.09424 [ms] |
| 2026-09-19 13:42:49.180 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 64.5399 [ms] |
| 2026-09-19 13:42:49.183 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:42:49.211 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.82119 [ms] |
| 2026-09-19 13:42:49.301 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:42:49.316 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:42:49.332 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:42:49.334 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:42:49.347 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:42:49.348 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:42:49.350 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:42:49.364 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:42:49.367 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:42:49.380 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:42:49.383 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:42:49.398 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:42:49.400 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:42:49.414 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:42:49.416 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:42:49.418 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:42:49.434 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.118 [percent] |
| 2026-09-19 13:42:55.449 | INFO | Performance | CPU 69.45 ms/frame (14.1 fps, worst 100.00 ms over 73 frames), RSS 1163 MiB |
| 2026-09-19 13:42:55.604 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 69.4462 [ms] |
| 2026-09-19 13:42:55.607 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:42:55.621 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.0877 [fps] |
| 2026-09-19 13:42:55.656 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 73 [count] |
| 2026-09-19 13:42:55.669 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1163.14 [MiB] |
| 2026-09-19 13:42:55.672 | INFO | GpuTiming | GPU 67.05 ms total | cull 0.04 · raster 3.29 · HiZ 0.05 · resolve 0.69 · ReSTIR 62.98 · shadow 0.00 · post 1.43 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:42:55.680 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 67.0492 [ms] |
| 2026-09-19 13:42:55.681 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.043456 [ms] |
| 2026-09-19 13:42:55.691 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.29126 [ms] |
| 2026-09-19 13:42:55.833 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.04608 [ms] |
| 2026-09-19 13:42:55.836 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.686272 [ms] |
| 2026-09-19 13:42:55.839 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 62.9821 [ms] |
| 2026-09-19 13:42:55.847 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:42:55.851 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.42899 [ms] |
| 2026-09-19 13:42:55.854 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:42:55.856 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:42:55.858 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:42:55.865 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:42:55.868 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:42:55.870 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:42:55.872 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:42:55.874 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:42:55.884 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:42:55.887 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 94% of GPU frame |
| 2026-09-19 13:42:55.890 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:42:55.897 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:42:55.901 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:42:55.905 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:42:55.907 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:42:55.916 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:42:55.919 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.9342 [percent] |
| 2026-09-19 13:43:01.602 | INFO | Performance | CPU 66.04 ms/frame (14.8 fps, worst 100.00 ms over 76 frames), RSS 1163 MiB |
| 2026-09-19 13:43:01.604 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 66.0439 [ms] |
| 2026-09-19 13:43:01.606 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:43:01.609 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.8248 [fps] |
| 2026-09-19 13:43:01.622 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 76 [count] |
| 2026-09-19 13:43:01.633 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1163.15 [MiB] |
| 2026-09-19 13:43:01.651 | INFO | GpuTiming | GPU 70.15 ms total | cull 0.05 · raster 3.88 · HiZ 0.05 · resolve 0.90 · ReSTIR 65.27 · shadow 0.00 · post 1.72 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:43:01.654 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 70.145 [ms] |
| 2026-09-19 13:43:01.662 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.046304 [ms] |
| 2026-09-19 13:43:01.669 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.88067 [ms] |
| 2026-09-19 13:43:01.671 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 13:43:01.709 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.90112 [ms] |
| 2026-09-19 13:43:01.722 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 65.2698 [ms] |
| 2026-09-19 13:43:01.726 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:43:01.743 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.72372 [ms] |
| 2026-09-19 13:43:01.773 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:43:01.775 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:43:01.778 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:43:01.793 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:43:01.810 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:43:01.823 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:43:01.827 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:43:01.836 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:43:01.838 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:43:01.841 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:43:01.855 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:43:01.857 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:43:01.862 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:43:01.867 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:43:01.870 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:43:01.872 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:43:01.874 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.0498 [percent] |
| 2026-09-19 13:43:07.482 | INFO | Performance | CPU 64.20 ms/frame (15.3 fps, worst 100.00 ms over 78 frames), RSS 1163 MiB |
| 2026-09-19 13:43:07.490 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 64.1955 [ms] |
| 2026-09-19 13:43:07.492 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:43:07.495 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.3111 [fps] |
| 2026-09-19 13:43:07.498 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 78 [count] |
| 2026-09-19 13:43:07.508 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1163.16 [MiB] |
| 2026-09-19 13:43:07.510 | INFO | GpuTiming | GPU 69.22 ms total | cull 0.05 · raster 4.06 · HiZ 0.05 · resolve 0.89 · ReSTIR 64.18 · shadow 0.00 · post 1.41 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:43:07.514 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 69.22 [ms] |
| 2026-09-19 13:43:07.526 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.047488 [ms] |
| 2026-09-19 13:43:07.773 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 4.06429 [ms] |
| 2026-09-19 13:43:07.778 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045888 [ms] |
| 2026-09-19 13:43:07.780 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.885248 [ms] |
| 2026-09-19 13:43:07.782 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 64.1771 [ms] |
| 2026-09-19 13:43:07.793 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:43:07.797 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.41056 [ms] |
| 2026-09-19 13:43:07.799 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:43:07.808 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:43:07.813 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:43:07.814 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:43:07.825 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:43:07.828 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:43:07.828 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:43:07.831 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:43:07.833 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:43:07.840 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:43:07.843 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:43:07.845 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:43:07.855 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:43:07.860 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:43:07.861 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:43:07.865 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:43:07.867 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.7147 [percent] |
| 2026-09-19 13:43:14.649 | INFO | Performance | CPU 72.10 ms/frame (14.5 fps, worst 100.00 ms over 70 frames), RSS 1163 MiB |
| 2026-09-19 13:43:15.408 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 72.0997 [ms] |
| 2026-09-19 13:43:15.603 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:43:15.625 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.5111 [fps] |
| 2026-09-19 13:43:15.635 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 70 [count] |
| 2026-09-19 13:43:15.638 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1163.16 [MiB] |
| 2026-09-19 13:43:15.641 | INFO | GpuTiming | GPU 68.20 ms total | cull 0.05 · raster 3.41 · HiZ 0.05 · resolve 0.87 · ReSTIR 63.82 · shadow 0.00 · post 1.40 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:43:15.659 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 68.198 [ms] |
| 2026-09-19 13:43:15.662 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.046848 [ms] |
| 2026-09-19 13:43:15.671 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.40918 [ms] |
| 2026-09-19 13:43:15.674 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.0472 [ms] |
| 2026-09-19 13:43:15.677 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.873472 [ms] |
| 2026-09-19 13:43:15.687 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 63.8213 [ms] |
| 2026-09-19 13:43:15.692 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:43:15.693 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.39555 [ms] |
| 2026-09-19 13:43:15.702 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:43:15.712 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:43:15.726 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:43:15.841 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:43:16.393 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:43:16.397 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:43:16.421 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:43:16.425 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:43:16.442 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:43:16.447 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 94% of GPU frame |
| 2026-09-19 13:43:16.529 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:43:16.665 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:43:16.943 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:43:16.982 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:43:17.011 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:43:17.191 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:43:17.196 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.5824 [percent] |
| 2026-09-19 13:43:24.521 | INFO | Performance | CPU 70.71 ms/frame (13.9 fps, worst 100.00 ms over 72 frames), RSS 1163 MiB |
| 2026-09-19 13:43:24.575 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 70.7058 [ms] |
| 2026-09-19 13:43:25.209 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:43:25.224 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 13.9346 [fps] |
| 2026-09-19 13:43:25.278 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 72 [count] |
| 2026-09-19 13:43:25.284 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1163.17 [MiB] |
| 2026-09-19 13:43:25.286 | INFO | GpuTiming | GPU 67.35 ms total | cull 0.04 · raster 3.29 · HiZ 0.05 · resolve 0.72 · ReSTIR 63.25 · shadow 0.00 · post 1.40 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:43:25.289 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 67.3472 [ms] |
| 2026-09-19 13:43:25.291 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.043264 [ms] |
| 2026-09-19 13:43:25.293 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.29318 [ms] |
| 2026-09-19 13:43:25.295 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045824 [ms] |
| 2026-09-19 13:43:25.304 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.718848 [ms] |
| 2026-09-19 13:43:25.306 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 63.2461 [ms] |
| 2026-09-19 13:43:25.308 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:43:25.310 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.39904 [ms] |
| 2026-09-19 13:43:25.320 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:43:25.323 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:43:25.324 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:43:25.343 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:43:25.345 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:43:25.352 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:43:25.354 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:43:25.356 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:43:25.358 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:43:25.360 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 94% of GPU frame |
| 2026-09-19 13:43:25.372 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:43:25.375 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:43:25.377 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:43:25.379 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:43:25.385 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:43:25.388 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:43:25.390 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.9105 [percent] |
| 2026-09-19 13:43:31.359 | INFO | Performance | CPU 62.93 ms/frame (15.4 fps, worst 100.00 ms over 80 frames), RSS 1163 MiB |
| 2026-09-19 13:43:31.361 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 62.9252 [ms] |
| 2026-09-19 13:43:31.365 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:43:31.368 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.4077 [fps] |
| 2026-09-19 13:43:31.373 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 80 [count] |
| 2026-09-19 13:43:31.379 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1163.24 [MiB] |
| 2026-09-19 13:43:31.382 | INFO | GpuTiming | GPU 63.30 ms total | cull 0.04 · raster 3.32 · HiZ 0.05 · resolve 0.68 · ReSTIR 59.21 · shadow 0.00 · post 1.40 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:43:31.388 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 63.295 [ms] |
| 2026-09-19 13:43:31.395 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.042592 [ms] |
| 2026-09-19 13:43:31.397 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.3175 [ms] |
| 2026-09-19 13:43:31.400 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045728 [ms] |
| 2026-09-19 13:43:31.402 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.684096 [ms] |
| 2026-09-19 13:43:31.406 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 59.2051 [ms] |
| 2026-09-19 13:43:31.412 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:43:31.415 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.39651 [ms] |
| 2026-09-19 13:43:31.417 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:43:31.421 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:43:31.426 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87660 triangles |
| 2026-09-19 13:43:31.431 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:43:31.433 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:43:31.436 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:43:31.442 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:43:31.444 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87660 [count] |
| 2026-09-19 13:43:31.447 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:43:31.449 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 94% of GPU frame |
| 2026-09-19 13:43:31.455 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:43:31.460 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:43:31.463 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:43:31.467 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:43:31.470 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:43:31.474 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:43:31.477 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.5383 [percent] |
| 2026-09-19 13:43:37.252 | INFO | Performance | CPU 58.59 ms/frame (16.8 fps, worst 100.00 ms over 86 frames), RSS 1165 MiB |
| 2026-09-19 13:43:37.254 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 58.593 [ms] |
| 2026-09-19 13:43:37.256 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:43:37.258 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.8255 [fps] |
| 2026-09-19 13:43:37.261 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 86 [count] |
| 2026-09-19 13:43:37.263 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1164.56 [MiB] |
| 2026-09-19 13:43:37.265 | INFO | GpuTiming | GPU 60.20 ms total | cull 0.05 · raster 3.63 · HiZ 0.05 · resolve 1.04 · ReSTIR 55.44 · shadow 0.00 · post 1.45 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:43:37.268 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 60.2029 [ms] |
| 2026-09-19 13:43:37.271 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.045504 [ms] |
| 2026-09-19 13:43:37.278 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.62701 [ms] |
| 2026-09-19 13:43:37.281 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.048384 [ms] |
| 2026-09-19 13:43:37.284 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.03997 [ms] |
| 2026-09-19 13:43:37.286 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 55.4421 [ms] |
| 2026-09-19 13:43:37.289 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:43:37.295 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.44889 [ms] |
| 2026-09-19 13:43:37.297 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:43:37.299 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:43:37.302 | INFO | Visibility | Clusters 791 tested -> 757 frustum, 757 cone, 757 visible | draws 757+0, 89640 triangles |
| 2026-09-19 13:43:37.304 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:43:37.311 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 757 [count] |
| 2026-09-19 13:43:37.314 | INFO | TelemetryMetrics | Measurement: ClustersCone = 757 [count] |
| 2026-09-19 13:43:37.315 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 757 [count] |
| 2026-09-19 13:43:37.317 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 89640 [count] |
| 2026-09-19 13:43:37.319 | INFO | TelemetryMetrics | Measurement: DrawCalls = 757 [count] |
| 2026-09-19 13:43:37.325 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 92% of GPU frame |
| 2026-09-19 13:43:37.329 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:43:37.331 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:43:37.333 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:43:37.335 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:43:37.340 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:43:37.342 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:43:37.344 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.092 [percent] |
| 2026-09-19 13:43:42.674 | INFO | Performance | CPU 58.92 ms/frame (16.8 fps, worst 100.00 ms over 85 frames), RSS 1165 MiB |
| 2026-09-19 13:43:42.678 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 58.9212 [ms] |
| 2026-09-19 13:43:42.681 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:43:42.684 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.7731 [fps] |
| 2026-09-19 13:43:42.685 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 85 [count] |
| 2026-09-19 13:43:42.688 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1164.56 [MiB] |
| 2026-09-19 13:43:42.691 | INFO | GpuTiming | GPU 58.72 ms total | cull 0.04 · raster 3.39 · HiZ 0.05 · resolve 0.80 · ReSTIR 54.44 · shadow 0.00 · post 1.38 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:43:42.703 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 58.7196 [ms] |
| 2026-09-19 13:43:42.708 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.043936 [ms] |
| 2026-09-19 13:43:42.712 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.3944 [ms] |
| 2026-09-19 13:43:42.715 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045184 [ms] |
| 2026-09-19 13:43:42.719 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.79872 [ms] |
| 2026-09-19 13:43:42.721 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 54.4374 [ms] |
| 2026-09-19 13:43:42.730 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:43:42.733 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.38292 [ms] |
| 2026-09-19 13:43:42.736 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:43:42.739 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:43:42.741 | INFO | Visibility | Clusters 791 tested -> 757 frustum, 757 cone, 757 visible | draws 757+0, 89640 triangles |
| 2026-09-19 13:43:42.746 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:43:42.747 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 757 [count] |
| 2026-09-19 13:43:42.750 | INFO | TelemetryMetrics | Measurement: ClustersCone = 757 [count] |
| 2026-09-19 13:43:42.754 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 757 [count] |
| 2026-09-19 13:43:42.761 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 89640 [count] |
| 2026-09-19 13:43:42.765 | INFO | TelemetryMetrics | Measurement: DrawCalls = 757 [count] |
| 2026-09-19 13:43:42.767 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:43:42.776 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:43:42.779 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:43:42.782 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:43:42.784 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:43:42.791 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:43:42.796 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:43:42.798 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.7073 [percent] |
| 2026-09-19 13:43:48.161 | INFO | Performance | CPU 58.51 ms/frame (17.0 fps, worst 100.00 ms over 86 frames), RSS 1165 MiB |
| 2026-09-19 13:43:48.164 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 58.5108 [ms] |
| 2026-09-19 13:43:48.167 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:43:48.170 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 17.0496 [fps] |
| 2026-09-19 13:43:48.173 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 86 [count] |
| 2026-09-19 13:43:48.175 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1164.56 [MiB] |
| 2026-09-19 13:43:48.185 | INFO | GpuTiming | GPU 59.19 ms total | cull 0.04 · raster 3.40 · HiZ 0.05 · resolve 0.92 · ReSTIR 54.78 · shadow 0.00 · post 1.35 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:43:48.201 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 59.1901 [ms] |
| 2026-09-19 13:43:48.205 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.044128 [ms] |
| 2026-09-19 13:43:48.208 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.4008 [ms] |
| 2026-09-19 13:43:48.210 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045056 [ms] |
| 2026-09-19 13:43:48.218 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.915776 [ms] |
| 2026-09-19 13:43:48.223 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 54.7843 [ms] |
| 2026-09-19 13:43:48.233 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:43:48.236 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.35104 [ms] |
| 2026-09-19 13:43:48.238 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:43:48.240 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:43:48.246 | INFO | Visibility | Clusters 791 tested -> 757 frustum, 757 cone, 757 visible | draws 757+0, 89640 triangles |
| 2026-09-19 13:43:48.249 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:43:48.251 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 757 [count] |
| 2026-09-19 13:43:48.254 | INFO | TelemetryMetrics | Measurement: ClustersCone = 757 [count] |
| 2026-09-19 13:43:48.256 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 757 [count] |
| 2026-09-19 13:43:48.261 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 89640 [count] |
| 2026-09-19 13:43:48.264 | INFO | TelemetryMetrics | Measurement: DrawCalls = 757 [count] |
| 2026-09-19 13:43:48.267 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:43:48.271 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:43:48.276 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:43:48.278 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:43:48.281 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:43:48.283 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:43:48.286 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:43:48.291 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.5566 [percent] |
| 2026-09-19 13:43:53.900 | INFO | Performance | CPU 59.59 ms/frame (16.9 fps, worst 100.00 ms over 85 frames), RSS 1165 MiB |
| 2026-09-19 13:43:53.912 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 59.5934 [ms] |
| 2026-09-19 13:43:53.914 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:43:53.922 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.8686 [fps] |
| 2026-09-19 13:43:53.927 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 85 [count] |
| 2026-09-19 13:43:53.939 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1164.56 [MiB] |
| 2026-09-19 13:43:53.942 | INFO | GpuTiming | GPU 61.96 ms total | cull 0.04 · raster 3.74 · HiZ 0.05 · resolve 0.85 · ReSTIR 57.28 · shadow 0.00 · post 1.31 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:43:53.947 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 61.9613 [ms] |
| 2026-09-19 13:43:53.955 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.042944 [ms] |
| 2026-09-19 13:43:53.957 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.74243 [ms] |
| 2026-09-19 13:43:53.964 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045056 [ms] |
| 2026-09-19 13:43:53.970 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.847776 [ms] |
| 2026-09-19 13:43:53.972 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 57.2831 [ms] |
| 2026-09-19 13:43:53.979 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:43:53.981 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.31248 [ms] |
| 2026-09-19 13:43:53.988 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:43:53.990 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:43:53.996 | INFO | Visibility | Clusters 791 tested -> 757 frustum, 757 cone, 757 visible | draws 757+0, 89640 triangles |
| 2026-09-19 13:43:54.004 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:43:54.006 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 757 [count] |
| 2026-09-19 13:43:54.012 | INFO | TelemetryMetrics | Measurement: ClustersCone = 757 [count] |
| 2026-09-19 13:43:54.015 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 757 [count] |
| 2026-09-19 13:43:54.027 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 89640 [count] |
| 2026-09-19 13:43:54.030 | INFO | TelemetryMetrics | Measurement: DrawCalls = 757 [count] |
| 2026-09-19 13:43:54.037 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 92% of GPU frame |
| 2026-09-19 13:43:54.063 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:43:54.065 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:43:54.076 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:43:54.080 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:43:54.090 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:43:54.093 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:43:54.096 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.4498 [percent] |
| 2026-09-19 13:43:59.659 | INFO | Performance | CPU 61.17 ms/frame (16.3 fps, worst 100.00 ms over 82 frames), RSS 1165 MiB |
| 2026-09-19 13:43:59.661 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 61.1736 [ms] |
| 2026-09-19 13:43:59.663 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:43:59.665 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.3403 [fps] |
| 2026-09-19 13:43:59.668 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 82 [count] |
| 2026-09-19 13:43:59.672 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1164.62 [MiB] |
| 2026-09-19 13:43:59.677 | INFO | GpuTiming | GPU 59.07 ms total | cull 0.04 · raster 3.38 · HiZ 0.05 · resolve 0.70 · ReSTIR 54.89 · shadow 0.00 · post 1.48 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:43:59.682 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 59.0701 [ms] |
| 2026-09-19 13:43:59.684 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.043264 [ms] |
| 2026-09-19 13:43:59.687 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.38394 [ms] |
| 2026-09-19 13:43:59.693 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045632 [ms] |
| 2026-09-19 13:43:59.696 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.704448 [ms] |
| 2026-09-19 13:43:59.699 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 54.8929 [ms] |
| 2026-09-19 13:43:59.701 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:43:59.704 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.48394 [ms] |
| 2026-09-19 13:43:59.710 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:43:59.713 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:43:59.715 | INFO | Visibility | Clusters 791 tested -> 757 frustum, 757 cone, 757 visible | draws 757+0, 89640 triangles |
| 2026-09-19 13:43:59.718 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:43:59.726 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 757 [count] |
| 2026-09-19 13:43:59.731 | INFO | TelemetryMetrics | Measurement: ClustersCone = 757 [count] |
| 2026-09-19 13:43:59.733 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 757 [count] |
| 2026-09-19 13:43:59.736 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 89640 [count] |
| 2026-09-19 13:43:59.741 | INFO | TelemetryMetrics | Measurement: DrawCalls = 757 [count] |
| 2026-09-19 13:43:59.744 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:43:59.748 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:43:59.751 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:43:59.759 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:43:59.762 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:43:59.764 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:43:59.767 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:43:59.774 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.9283 [percent] |
| 2026-09-19 13:44:05.580 | INFO | Performance | CPU 60.00 ms/frame (16.8 fps, worst 100.00 ms over 84 frames), RSS 1165 MiB |
| 2026-09-19 13:44:05.697 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 60.0032 [ms] |
| 2026-09-19 13:44:05.703 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:44:05.707 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.7729 [fps] |
| 2026-09-19 13:44:05.714 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 84 [count] |
| 2026-09-19 13:44:05.740 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1164.77 [MiB] |
| 2026-09-19 13:44:05.753 | INFO | GpuTiming | GPU 58.91 ms total | cull 0.04 · raster 3.37 · HiZ 0.05 · resolve 0.69 · ReSTIR 54.77 · shadow 0.00 · post 1.28 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:44:05.771 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 58.9112 [ms] |
| 2026-09-19 13:44:05.772 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.042944 [ms] |
| 2026-09-19 13:44:05.785 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.36634 [ms] |
| 2026-09-19 13:44:05.801 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045056 [ms] |
| 2026-09-19 13:44:05.829 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.687712 [ms] |
| 2026-09-19 13:44:05.833 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 54.7692 [ms] |
| 2026-09-19 13:44:05.837 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:44:05.846 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.28138 [ms] |
| 2026-09-19 13:44:05.848 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:44:05.850 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:44:05.856 | INFO | Visibility | Clusters 791 tested -> 757 frustum, 757 cone, 757 visible | draws 757+0, 89640 triangles |
| 2026-09-19 13:44:05.860 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:44:05.862 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 757 [count] |
| 2026-09-19 13:44:05.865 | INFO | TelemetryMetrics | Measurement: ClustersCone = 757 [count] |
| 2026-09-19 13:44:05.871 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 757 [count] |
| 2026-09-19 13:44:05.875 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 89640 [count] |
| 2026-09-19 13:44:05.877 | INFO | TelemetryMetrics | Measurement: DrawCalls = 757 [count] |
| 2026-09-19 13:44:05.881 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:44:05.890 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:44:05.896 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:44:05.897 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:44:05.902 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:44:05.905 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:44:05.907 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:44:05.911 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.969 [percent] |
| 2026-09-19 13:44:11.284 | INFO | Performance | CPU 58.33 ms/frame (17.0 fps, worst 100.00 ms over 87 frames), RSS 478 MiB |
| 2026-09-19 13:44:11.285 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 58.3344 [ms] |
| 2026-09-19 13:44:11.287 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:44:11.288 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 17.0055 [fps] |
| 2026-09-19 13:44:11.290 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 87 [count] |
| 2026-09-19 13:44:11.291 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 478.172 [MiB] |
| 2026-09-19 13:44:11.293 | INFO | GpuTiming | GPU 58.74 ms total | cull 0.05 · raster 3.46 · HiZ 0.05 · resolve 0.77 · ReSTIR 54.41 · shadow 0.00 · post 1.26 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:44:11.299 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 58.7449 [ms] |
| 2026-09-19 13:44:11.304 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.054144 [ms] |
| 2026-09-19 13:44:11.306 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.46317 [ms] |
| 2026-09-19 13:44:11.308 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045056 [ms] |
| 2026-09-19 13:44:11.309 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.770048 [ms] |
| 2026-09-19 13:44:11.311 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 54.4125 [ms] |
| 2026-09-19 13:44:11.313 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:44:11.315 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.2583 [ms] |
| 2026-09-19 13:44:11.321 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:44:11.322 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:44:11.324 | INFO | Visibility | Clusters 791 tested -> 757 frustum, 757 cone, 757 visible | draws 757+0, 89640 triangles |
| 2026-09-19 13:44:11.326 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:44:11.329 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 757 [count] |
| 2026-09-19 13:44:11.330 | INFO | TelemetryMetrics | Measurement: ClustersCone = 757 [count] |
| 2026-09-19 13:44:11.335 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 757 [count] |
| 2026-09-19 13:44:11.337 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 89640 [count] |
| 2026-09-19 13:44:11.338 | INFO | TelemetryMetrics | Measurement: DrawCalls = 757 [count] |
| 2026-09-19 13:44:11.340 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:44:11.342 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:44:11.344 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:44:11.353 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:44:11.355 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:44:11.357 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:44:11.358 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:44:11.360 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.625 [percent] |
| 2026-09-19 13:44:16.594 | INFO | Performance | CPU 60.37 ms/frame (16.7 fps, worst 100.00 ms over 83 frames), RSS 501 MiB |
| 2026-09-19 13:44:16.597 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 60.3692 [ms] |
| 2026-09-19 13:44:16.599 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:44:16.602 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.6773 [fps] |
| 2026-09-19 13:44:16.604 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 83 [count] |
| 2026-09-19 13:44:16.610 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 501.395 [MiB] |
| 2026-09-19 13:44:16.612 | INFO | GpuTiming | GPU 61.34 ms total | cull 0.05 · raster 3.78 · HiZ 0.04 · resolve 0.80 · ReSTIR 56.66 · shadow 0.00 · post 1.38 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:44:16.616 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 61.3382 [ms] |
| 2026-09-19 13:44:16.620 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.049824 [ms] |
| 2026-09-19 13:44:16.625 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.77811 [ms] |
| 2026-09-19 13:44:16.626 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.044896 [ms] |
| 2026-09-19 13:44:16.629 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.802464 [ms] |
| 2026-09-19 13:44:16.632 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 56.6629 [ms] |
| 2026-09-19 13:44:16.634 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:44:16.636 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.38186 [ms] |
| 2026-09-19 13:44:16.643 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:44:16.646 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:44:16.650 | INFO | Visibility | Clusters 791 tested -> 757 frustum, 757 cone, 757 visible | draws 757+0, 89640 triangles |
| 2026-09-19 13:44:16.652 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:44:16.657 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 757 [count] |
| 2026-09-19 13:44:16.659 | INFO | TelemetryMetrics | Measurement: ClustersCone = 757 [count] |
| 2026-09-19 13:44:16.662 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 757 [count] |
| 2026-09-19 13:44:16.664 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 89640 [count] |
| 2026-09-19 13:44:16.666 | INFO | TelemetryMetrics | Measurement: DrawCalls = 757 [count] |
| 2026-09-19 13:44:16.676 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 92% of GPU frame |
| 2026-09-19 13:44:16.680 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:44:16.682 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:44:16.691 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:44:16.694 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:44:16.701 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:44:16.712 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:44:16.719 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.3778 [percent] |
| 2026-09-19 13:44:22.191 | INFO | Performance | CPU 60.40 ms/frame (16.5 fps, worst 100.00 ms over 84 frames), RSS 496 MiB |
| 2026-09-19 13:44:22.195 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 60.4027 [ms] |
| 2026-09-19 13:44:22.196 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:44:22.229 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.4933 [fps] |
| 2026-09-19 13:44:22.230 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 84 [count] |
| 2026-09-19 13:44:22.243 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 496.137 [MiB] |
| 2026-09-19 13:44:22.247 | INFO | GpuTiming | GPU 62.24 ms total | cull 0.05 · raster 3.58 · HiZ 0.05 · resolve 0.85 · ReSTIR 57.72 · shadow 0.00 · post 1.25 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:44:22.262 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 62.2412 [ms] |
| 2026-09-19 13:44:22.263 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.045088 [ms] |
| 2026-09-19 13:44:22.266 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.57824 [ms] |
| 2026-09-19 13:44:22.281 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045184 [ms] |
| 2026-09-19 13:44:22.295 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.854112 [ms] |
| 2026-09-19 13:44:22.296 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 57.7186 [ms] |
| 2026-09-19 13:44:22.299 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:44:22.310 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.24742 [ms] |
| 2026-09-19 13:44:22.312 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:44:22.320 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:44:22.327 | INFO | Visibility | Clusters 791 tested -> 757 frustum, 757 cone, 757 visible | draws 757+0, 89640 triangles |
| 2026-09-19 13:44:22.330 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:44:22.332 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 757 [count] |
| 2026-09-19 13:44:22.344 | INFO | TelemetryMetrics | Measurement: ClustersCone = 757 [count] |
| 2026-09-19 13:44:22.346 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 757 [count] |
| 2026-09-19 13:44:22.348 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 89640 [count] |
| 2026-09-19 13:44:22.359 | INFO | TelemetryMetrics | Measurement: DrawCalls = 757 [count] |
| 2026-09-19 13:44:22.360 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:44:22.363 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:44:22.365 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:44:22.512 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:44:22.579 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:44:22.587 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:44:22.592 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:44:22.599 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.7337 [percent] |
| 2026-09-19 13:44:28.817 | INFO | Performance | CPU 64.56 ms/frame (15.8 fps, worst 100.00 ms over 78 frames), RSS 498 MiB |
| 2026-09-19 13:44:28.818 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 64.564 [ms] |
| 2026-09-19 13:44:28.821 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:44:28.822 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.7751 [fps] |
| 2026-09-19 13:44:28.828 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 78 [count] |
| 2026-09-19 13:44:28.829 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 497.957 [MiB] |
| 2026-09-19 13:44:28.831 | INFO | GpuTiming | GPU 60.71 ms total | cull 0.04 · raster 3.43 · HiZ 0.05 · resolve 0.84 · ReSTIR 56.36 · shadow 0.00 · post 1.24 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:44:28.835 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 60.7146 [ms] |
| 2026-09-19 13:44:28.836 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.043424 [ms] |
| 2026-09-19 13:44:28.844 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.43206 [ms] |
| 2026-09-19 13:44:28.848 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045056 [ms] |
| 2026-09-19 13:44:28.852 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.838528 [ms] |
| 2026-09-19 13:44:28.862 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 56.3555 [ms] |
| 2026-09-19 13:44:28.864 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:44:28.866 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.24042 [ms] |
| 2026-09-19 13:44:28.868 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:44:28.879 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:44:28.880 | INFO | Visibility | Clusters 791 tested -> 757 frustum, 757 cone, 757 visible | draws 757+0, 89640 triangles |
| 2026-09-19 13:44:28.881 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:44:28.883 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 757 [count] |
| 2026-09-19 13:44:28.886 | INFO | TelemetryMetrics | Measurement: ClustersCone = 757 [count] |
| 2026-09-19 13:44:28.893 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 757 [count] |
| 2026-09-19 13:44:28.897 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 89640 [count] |
| 2026-09-19 13:44:28.898 | INFO | TelemetryMetrics | Measurement: DrawCalls = 757 [count] |
| 2026-09-19 13:44:28.900 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:44:28.910 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:44:28.911 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:44:28.913 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:44:28.915 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:44:28.917 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:44:28.921 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:44:28.927 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.8204 [percent] |
| 2026-09-19 13:44:34.274 | INFO | Performance | CPU 60.53 ms/frame (16.4 fps, worst 100.00 ms over 83 frames), RSS 500 MiB |
| 2026-09-19 13:44:34.555 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 60.5315 [ms] |
| 2026-09-19 13:44:34.559 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:44:34.570 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.3746 [fps] |
| 2026-09-19 13:44:34.572 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 83 [count] |
| 2026-09-19 13:44:34.624 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 500.426 [MiB] |
| 2026-09-19 13:44:34.626 | INFO | GpuTiming | GPU 61.68 ms total | cull 0.04 · raster 3.31 · HiZ 0.05 · resolve 0.88 · ReSTIR 57.40 · shadow 0.00 · post 1.23 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:44:34.642 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 61.6814 [ms] |
| 2026-09-19 13:44:34.705 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.043712 [ms] |
| 2026-09-19 13:44:34.707 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.31206 [ms] |
| 2026-09-19 13:44:34.709 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.046176 [ms] |
| 2026-09-19 13:44:34.717 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.876576 [ms] |
| 2026-09-19 13:44:34.721 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 57.4029 [ms] |
| 2026-09-19 13:44:34.722 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:44:34.724 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.23277 [ms] |
| 2026-09-19 13:44:34.727 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:44:34.736 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:44:34.738 | INFO | Visibility | Clusters 791 tested -> 757 frustum, 757 cone, 757 visible | draws 757+0, 89640 triangles |
| 2026-09-19 13:44:34.741 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:44:34.743 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 757 [count] |
| 2026-09-19 13:44:34.751 | INFO | TelemetryMetrics | Measurement: ClustersCone = 757 [count] |
| 2026-09-19 13:44:34.754 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 757 [count] |
| 2026-09-19 13:44:34.756 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 89640 [count] |
| 2026-09-19 13:44:34.758 | INFO | TelemetryMetrics | Measurement: DrawCalls = 757 [count] |
| 2026-09-19 13:44:34.775 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:44:34.788 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:44:34.792 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:44:34.807 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:44:34.820 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:44:34.824 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:44:34.826 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:44:34.835 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.0635 [percent] |
| 2026-09-19 13:44:40.228 | INFO | Performance | CPU 61.81 ms/frame (16.1 fps, worst 100.00 ms over 81 frames), RSS 527 MiB |
| 2026-09-19 13:44:40.240 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 61.8084 [ms] |
| 2026-09-19 13:44:40.242 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:44:40.244 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.1213 [fps] |
| 2026-09-19 13:44:40.260 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 81 [count] |
| 2026-09-19 13:44:40.263 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 526.523 [MiB] |
| 2026-09-19 13:44:40.275 | INFO | GpuTiming | GPU 63.33 ms total | cull 0.05 · raster 3.37 · HiZ 0.05 · resolve 0.71 · ReSTIR 59.15 · shadow 0.00 · post 1.23 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:44:40.279 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 63.3309 [ms] |
| 2026-09-19 13:44:40.292 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.045152 [ms] |
| 2026-09-19 13:44:40.294 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.37114 [ms] |
| 2026-09-19 13:44:40.297 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045184 [ms] |
| 2026-09-19 13:44:40.305 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.714752 [ms] |
| 2026-09-19 13:44:40.308 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 59.1547 [ms] |
| 2026-09-19 13:44:40.310 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:44:40.312 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.22678 [ms] |
| 2026-09-19 13:44:40.321 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:44:40.324 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:44:40.327 | INFO | Visibility | Clusters 791 tested -> 757 frustum, 757 cone, 757 visible | draws 757+0, 89640 triangles |
| 2026-09-19 13:44:40.329 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:44:40.340 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 757 [count] |
| 2026-09-19 13:44:40.342 | INFO | TelemetryMetrics | Measurement: ClustersCone = 757 [count] |
| 2026-09-19 13:44:40.345 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 757 [count] |
| 2026-09-19 13:44:40.358 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 89640 [count] |
| 2026-09-19 13:44:40.360 | INFO | TelemetryMetrics | Measurement: DrawCalls = 757 [count] |
| 2026-09-19 13:44:40.362 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:44:40.375 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:44:40.376 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:44:40.378 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:44:40.380 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:44:40.387 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:44:40.389 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:44:40.392 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.4057 [percent] |
| 2026-09-19 13:44:46.167 | INFO | Performance | CPU 60.25 ms/frame (16.6 fps, worst 100.00 ms over 84 frames), RSS 538 MiB |
| 2026-09-19 13:44:46.171 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 60.2517 [ms] |
| 2026-09-19 13:44:46.173 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:44:46.175 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.5823 [fps] |
| 2026-09-19 13:44:46.177 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 84 [count] |
| 2026-09-19 13:44:46.181 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 537.625 [MiB] |
| 2026-09-19 13:44:46.182 | INFO | GpuTiming | GPU 57.74 ms total | cull 0.05 · raster 3.89 · HiZ 0.05 · resolve 0.95 · ReSTIR 52.82 · shadow 0.00 · post 1.61 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:44:46.191 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 57.7412 [ms] |
| 2026-09-19 13:44:46.193 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.046464 [ms] |
| 2026-09-19 13:44:46.197 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.88506 [ms] |
| 2026-09-19 13:44:46.199 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.04576 [ms] |
| 2026-09-19 13:44:46.200 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.9472 [ms] |
| 2026-09-19 13:44:46.204 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 52.8167 [ms] |
| 2026-09-19 13:44:46.206 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:44:46.209 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.6095 [ms] |
| 2026-09-19 13:44:46.210 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:44:46.214 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:44:46.216 | INFO | Visibility | Clusters 791 tested -> 742 frustum, 742 cone, 742 visible | draws 742+0, 88284 triangles |
| 2026-09-19 13:44:46.218 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:44:46.221 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 742 [count] |
| 2026-09-19 13:44:46.223 | INFO | TelemetryMetrics | Measurement: ClustersCone = 742 [count] |
| 2026-09-19 13:44:46.225 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 742 [count] |
| 2026-09-19 13:44:46.229 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 88284 [count] |
| 2026-09-19 13:44:46.231 | INFO | TelemetryMetrics | Measurement: DrawCalls = 742 [count] |
| 2026-09-19 13:44:46.232 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 91% of GPU frame |
| 2026-09-19 13:44:46.238 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:44:46.240 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:44:46.244 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:44:46.246 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:44:46.251 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:44:46.255 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:44:46.256 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 91.4715 [percent] |
| 2026-09-19 13:44:51.775 | INFO | Performance | CPU 60.09 ms/frame (16.9 fps, worst 100.00 ms over 84 frames), RSS 677 MiB |
| 2026-09-19 13:44:51.777 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 60.089 [ms] |
| 2026-09-19 13:44:51.779 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:44:51.784 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.8999 [fps] |
| 2026-09-19 13:44:51.786 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 84 [count] |
| 2026-09-19 13:44:51.790 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 677.242 [MiB] |
| 2026-09-19 13:44:51.793 | INFO | GpuTiming | GPU 75.44 ms total | cull 0.05 · raster 4.01 · HiZ 0.05 · resolve 0.91 · ReSTIR 70.42 · shadow 0.00 · post 1.59 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:44:51.796 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 75.4418 [ms] |
| 2026-09-19 13:44:51.801 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.04928 [ms] |
| 2026-09-19 13:44:51.803 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 4.00893 [ms] |
| 2026-09-19 13:44:51.807 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.046976 [ms] |
| 2026-09-19 13:44:51.809 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.913088 [ms] |
| 2026-09-19 13:44:51.811 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 70.4235 [ms] |
| 2026-09-19 13:44:51.816 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:44:51.818 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.59318 [ms] |
| 2026-09-19 13:44:51.825 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:44:51.827 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:44:51.831 | INFO | Visibility | Clusters 791 tested -> 741 frustum, 741 cone, 741 visible | draws 741+0, 87692 triangles |
| 2026-09-19 13:44:51.833 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:44:51.835 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 741 [count] |
| 2026-09-19 13:44:51.838 | INFO | TelemetryMetrics | Measurement: ClustersCone = 741 [count] |
| 2026-09-19 13:44:51.842 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 741 [count] |
| 2026-09-19 13:44:51.847 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 87692 [count] |
| 2026-09-19 13:44:51.850 | INFO | TelemetryMetrics | Measurement: DrawCalls = 741 [count] |
| 2026-09-19 13:44:51.852 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 13:44:51.858 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:44:51.864 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:44:51.866 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:44:51.868 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:44:51.869 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:44:51.871 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:44:51.875 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.3482 [percent] |
| 2026-09-19 13:44:57.817 | INFO | Performance | CPU 70.28 ms/frame (14.8 fps, worst 100.00 ms over 72 frames), RSS 717 MiB |
| 2026-09-19 13:44:57.819 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 70.2802 [ms] |
| 2026-09-19 13:44:57.821 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:44:57.822 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.8089 [fps] |
| 2026-09-19 13:44:57.824 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 72 [count] |
| 2026-09-19 13:44:57.826 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 716.984 [MiB] |
| 2026-09-19 13:44:57.830 | INFO | GpuTiming | GPU 99.34 ms total | cull 0.04 · raster 1.71 · HiZ 0.05 · resolve 0.27 · ReSTIR 97.26 · shadow 0.00 · post 1.70 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:44:57.834 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 99.3404 [ms] |
| 2026-09-19 13:44:57.836 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.040288 [ms] |
| 2026-09-19 13:44:57.840 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 1.71466 [ms] |
| 2026-09-19 13:44:57.842 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.050752 [ms] |
| 2026-09-19 13:44:57.846 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.272992 [ms] |
| 2026-09-19 13:44:57.848 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 97.2617 [ms] |
| 2026-09-19 13:44:57.850 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:44:57.852 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.69962 [ms] |
| 2026-09-19 13:44:57.856 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:44:57.857 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:44:57.862 | INFO | Visibility | Clusters 791 tested -> 303 frustum, 303 cone, 303 visible | draws 303+0, 35906 triangles |
| 2026-09-19 13:44:57.864 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:44:57.866 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 303 [count] |
| 2026-09-19 13:44:57.871 | INFO | TelemetryMetrics | Measurement: ClustersCone = 303 [count] |
| 2026-09-19 13:44:57.873 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 303 [count] |
| 2026-09-19 13:44:57.876 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 35906 [count] |
| 2026-09-19 13:44:57.880 | INFO | TelemetryMetrics | Measurement: DrawCalls = 303 [count] |
| 2026-09-19 13:44:57.881 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 98% of GPU frame |
| 2026-09-19 13:44:57.889 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:44:57.892 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:44:57.896 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:44:57.898 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:44:57.903 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:44:57.905 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:44:57.909 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 97.9075 [percent] |
| 2026-09-19 13:45:04.347 | INFO | Performance | CPU 78.01 ms/frame (13.2 fps, worst 100.00 ms over 65 frames), RSS 741 MiB |
| 2026-09-19 13:45:04.349 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 78.0147 [ms] |
| 2026-09-19 13:45:04.353 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:45:04.355 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 13.192 [fps] |
| 2026-09-19 13:45:04.357 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 65 [count] |
| 2026-09-19 13:45:04.359 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 740.672 [MiB] |
| 2026-09-19 13:45:04.361 | INFO | GpuTiming | GPU 70.08 ms total | cull 0.05 · raster 1.31 · HiZ 0.05 · resolve 0.26 · ReSTIR 68.41 · shadow 0.00 · post 1.95 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:45:04.364 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 70.0779 [ms] |
| 2026-09-19 13:45:04.373 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.048704 [ms] |
| 2026-09-19 13:45:04.375 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 1.31078 [ms] |
| 2026-09-19 13:45:04.378 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.049024 [ms] |
| 2026-09-19 13:45:04.379 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.25984 [ms] |
| 2026-09-19 13:45:04.382 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 68.4095 [ms] |
| 2026-09-19 13:45:04.384 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:45:04.389 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.94768 [ms] |
| 2026-09-19 13:45:04.391 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:45:04.393 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:45:04.395 | INFO | Visibility | Clusters 791 tested -> 171 frustum, 171 cone, 171 visible | draws 170+1, 19686 triangles |
| 2026-09-19 13:45:04.397 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:45:04.406 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 171 [count] |
| 2026-09-19 13:45:04.408 | INFO | TelemetryMetrics | Measurement: ClustersCone = 171 [count] |
| 2026-09-19 13:45:04.410 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 171 [count] |
| 2026-09-19 13:45:04.412 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 19686 [count] |
| 2026-09-19 13:45:04.413 | INFO | TelemetryMetrics | Measurement: DrawCalls = 171 [count] |
| 2026-09-19 13:45:04.420 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 98% of GPU frame |
| 2026-09-19 13:45:04.423 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:45:04.427 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:45:04.429 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:45:04.430 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:45:04.439 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:45:04.441 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:45:04.443 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 97.6193 [percent] |
| 2026-09-19 13:45:11.170 | INFO | Performance | CPU 80.30 ms/frame (12.5 fps, worst 100.00 ms over 63 frames), RSS 802 MiB |
| 2026-09-19 13:45:11.172 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 80.3001 [ms] |
| 2026-09-19 13:45:11.177 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:45:11.179 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 12.4954 [fps] |
| 2026-09-19 13:45:11.180 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 63 [count] |
| 2026-09-19 13:45:11.185 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 801.668 [MiB] |
| 2026-09-19 13:45:11.187 | INFO | GpuTiming | GPU 100.99 ms total | cull 0.04 · raster 1.57 · HiZ 0.05 · resolve 0.30 · ReSTIR 99.03 · shadow 0.00 · post 1.70 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:45:11.190 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 100.99 [ms] |
| 2026-09-19 13:45:11.195 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.041504 [ms] |
| 2026-09-19 13:45:11.196 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 1.56806 [ms] |
| 2026-09-19 13:45:11.201 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.049408 [ms] |
| 2026-09-19 13:45:11.203 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.302848 [ms] |
| 2026-09-19 13:45:11.206 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 99.0281 [ms] |
| 2026-09-19 13:45:11.210 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:45:11.216 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.69664 [ms] |
| 2026-09-19 13:45:11.219 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:45:11.221 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:45:11.223 | INFO | Visibility | Clusters 791 tested -> 356 frustum, 356 cone, 356 visible | draws 356+0, 42430 triangles |
| 2026-09-19 13:45:11.227 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:45:11.232 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 356 [count] |
| 2026-09-19 13:45:11.234 | INFO | TelemetryMetrics | Measurement: ClustersCone = 356 [count] |
| 2026-09-19 13:45:11.236 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 356 [count] |
| 2026-09-19 13:45:11.238 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 42430 [count] |
| 2026-09-19 13:45:11.244 | INFO | TelemetryMetrics | Measurement: DrawCalls = 356 [count] |
| 2026-09-19 13:45:11.249 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 98% of GPU frame |
| 2026-09-19 13:45:11.254 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:45:11.255 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:45:11.263 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:45:11.265 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:45:11.267 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:45:11.269 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:45:11.271 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 98.0574 [percent] |
| 2026-09-19 13:45:17.353 | INFO | Performance | CPU 74.56 ms/frame (12.9 fps, worst 100.00 ms over 68 frames), RSS 826 MiB |
| 2026-09-19 13:45:17.455 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 74.5577 [ms] |
| 2026-09-19 13:45:17.462 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:45:17.473 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 12.8829 [fps] |
| 2026-09-19 13:45:17.513 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 68 [count] |
| 2026-09-19 13:45:17.519 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 825.863 [MiB] |
| 2026-09-19 13:45:17.526 | INFO | GpuTiming | GPU 84.11 ms total | cull 0.04 · raster 1.16 · HiZ 0.05 · resolve 0.23 · ReSTIR 82.64 · shadow 0.00 · post 1.55 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:45:17.542 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 84.1109 [ms] |
| 2026-09-19 13:45:17.543 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.038816 [ms] |
| 2026-09-19 13:45:17.543 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 1.15568 [ms] |
| 2026-09-19 13:45:17.544 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 13:45:17.545 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.229376 [ms] |
| 2026-09-19 13:45:17.545 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 82.6399 [ms] |
| 2026-09-19 13:45:17.546 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:45:17.551 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.55318 [ms] |
| 2026-09-19 13:45:17.552 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:45:17.553 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:45:17.554 | INFO | Visibility | Clusters 791 tested -> 234 frustum, 234 cone, 234 visible | draws 234+0, 27584 triangles |
| 2026-09-19 13:45:17.555 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:45:17.555 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 234 [count] |
| 2026-09-19 13:45:17.556 | INFO | TelemetryMetrics | Measurement: ClustersCone = 234 [count] |
| 2026-09-19 13:45:17.558 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 234 [count] |
| 2026-09-19 13:45:17.570 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 27584 [count] |
| 2026-09-19 13:45:17.570 | INFO | TelemetryMetrics | Measurement: DrawCalls = 234 [count] |
| 2026-09-19 13:45:17.571 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 98% of GPU frame |
| 2026-09-19 13:45:17.571 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:45:17.572 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:45:17.573 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:45:17.574 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:45:17.577 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:45:17.579 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:45:17.589 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 98.2512 [percent] |
| 2026-09-19 13:45:23.390 | INFO | Performance | CPU 64.31 ms/frame (14.5 fps, worst 100.00 ms over 78 frames), RSS 826 MiB |
| 2026-09-19 13:45:23.391 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 64.3075 [ms] |
| 2026-09-19 13:45:23.393 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:45:23.394 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.5048 [fps] |
| 2026-09-19 13:45:23.397 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 78 [count] |
| 2026-09-19 13:45:23.398 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 825.863 [MiB] |
| 2026-09-19 13:45:23.400 | INFO | GpuTiming | GPU 65.29 ms total | cull 0.05 · raster 2.32 · HiZ 0.05 · resolve 0.39 · ReSTIR 62.49 · shadow 0.00 · post 1.85 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:45:23.404 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 65.2948 [ms] |
| 2026-09-19 13:45:23.405 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.048128 [ms] |
| 2026-09-19 13:45:23.408 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 2.32128 [ms] |
| 2026-09-19 13:45:23.409 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 13:45:23.414 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.386048 [ms] |
| 2026-09-19 13:45:23.416 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 62.4923 [ms] |
| 2026-09-19 13:45:23.422 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:45:23.423 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.85363 [ms] |
| 2026-09-19 13:45:23.429 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:45:23.431 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:45:23.433 | INFO | Visibility | Clusters 791 tested -> 520 frustum, 520 cone, 518 visible | draws 520+0, 61692 triangles |
| 2026-09-19 13:45:23.438 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:45:23.440 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 520 [count] |
| 2026-09-19 13:45:23.445 | INFO | TelemetryMetrics | Measurement: ClustersCone = 520 [count] |
| 2026-09-19 13:45:23.448 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 518 [count] |
| 2026-09-19 13:45:23.450 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 61692 [count] |
| 2026-09-19 13:45:23.457 | INFO | TelemetryMetrics | Measurement: DrawCalls = 520 [count] |
| 2026-09-19 13:45:23.463 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 96% of GPU frame |
| 2026-09-19 13:45:23.475 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:45:23.482 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:45:23.494 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:45:23.498 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:45:23.500 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:45:23.506 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:45:23.512 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 95.7078 [percent] |
| 2026-09-19 13:45:28.585 | INFO | Performance | CPU 51.13 ms/frame (18.7 fps, worst 100.00 ms over 98 frames), RSS 830 MiB |
| 2026-09-19 13:45:28.617 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 51.1268 [ms] |
| 2026-09-19 13:45:28.630 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:45:28.631 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 18.6963 [fps] |
| 2026-09-19 13:45:28.634 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 98 [count] |
| 2026-09-19 13:45:28.641 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 829.969 [MiB] |
| 2026-09-19 13:45:28.644 | INFO | GpuTiming | GPU 42.28 ms total | cull 0.04 · raster 1.38 · HiZ 0.05 · resolve 0.06 · ReSTIR 40.74 · shadow 0.00 · post 1.26 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:45:28.648 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 42.2785 [ms] |
| 2026-09-19 13:45:28.649 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.04224 [ms] |
| 2026-09-19 13:45:28.662 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 1.38406 [ms] |
| 2026-09-19 13:45:28.666 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045248 [ms] |
| 2026-09-19 13:45:28.669 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.063424 [ms] |
| 2026-09-19 13:45:28.677 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 40.7435 [ms] |
| 2026-09-19 13:45:28.681 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:45:28.685 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.26016 [ms] |
| 2026-09-19 13:45:28.690 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:45:28.693 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:45:28.695 | INFO | Visibility | Clusters 791 tested -> 287 frustum, 287 cone, 207 visible | draws 260+0, 30968 triangles |
| 2026-09-19 13:45:28.700 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:45:28.703 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 287 [count] |
| 2026-09-19 13:45:28.709 | INFO | TelemetryMetrics | Measurement: ClustersCone = 287 [count] |
| 2026-09-19 13:45:28.712 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 207 [count] |
| 2026-09-19 13:45:28.713 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 30968 [count] |
| 2026-09-19 13:45:28.719 | INFO | TelemetryMetrics | Measurement: DrawCalls = 260 [count] |
| 2026-09-19 13:45:28.726 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 96% of GPU frame |
| 2026-09-19 13:45:28.730 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:45:28.742 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:45:28.745 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:45:28.751 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:45:28.758 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:45:28.759 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 13:45:28.764 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 96.3694 [percent] |
| 2026-09-19 13:45:33.708 | INFO | Performance | CPU 38.98 ms/frame (25.7 fps, worst 100.00 ms over 129 frames), RSS 837 MiB |
| 2026-09-19 13:45:33.710 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 38.9766 [ms] |
| 2026-09-19 13:45:33.713 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:45:33.715 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 25.7095 [fps] |
| 2026-09-19 13:45:33.717 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 129 [count] |
| 2026-09-19 13:45:33.718 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 836.676 [MiB] |
| 2026-09-19 13:45:33.725 | INFO | GpuTiming | GPU 27.23 ms total | cull 0.04 · raster 0.09 · HiZ 0.05 · resolve 0.06 · ReSTIR 27.00 · shadow 0.00 · post 1.48 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:45:33.729 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 27.2288 [ms] |
| 2026-09-19 13:45:33.733 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.038208 [ms] |
| 2026-09-19 13:45:33.741 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 0.086016 [ms] |
| 2026-09-19 13:45:33.743 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.049152 [ms] |
| 2026-09-19 13:45:33.745 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.057344 [ms] |
| 2026-09-19 13:45:33.747 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 26.9981 [ms] |
| 2026-09-19 13:45:33.752 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:45:33.753 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.47728 [ms] |
| 2026-09-19 13:45:33.758 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:45:33.761 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:45:33.763 | INFO | Visibility | Clusters 791 tested -> 16 frustum, 16 cone, 16 visible | draws 16+0, 1722 triangles |
| 2026-09-19 13:45:33.767 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:45:33.769 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 16 [count] |
| 2026-09-19 13:45:33.776 | INFO | TelemetryMetrics | Measurement: ClustersCone = 16 [count] |
| 2026-09-19 13:45:33.778 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 16 [count] |
| 2026-09-19 13:45:33.780 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 1722 [count] |
| 2026-09-19 13:45:33.784 | INFO | TelemetryMetrics | Measurement: DrawCalls = 16 [count] |
| 2026-09-19 13:45:33.786 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 99% of GPU frame |
| 2026-09-19 13:45:33.791 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:45:33.793 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:45:33.795 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:45:33.799 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:45:33.801 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:45:33.803 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 13:45:33.808 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 99.1527 [percent] |
| 2026-09-19 13:45:38.812 | INFO | Performance | CPU 44.08 ms/frame (23.0 fps, worst 100.00 ms over 114 frames), RSS 1146 MiB |
| 2026-09-19 13:45:38.813 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 44.0798 [ms] |
| 2026-09-19 13:45:38.815 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:45:38.817 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 22.9536 [fps] |
| 2026-09-19 13:45:38.819 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 114 [count] |
| 2026-09-19 13:45:38.820 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1145.97 [MiB] |
| 2026-09-19 13:45:38.822 | INFO | GpuTiming | GPU 31.96 ms total | cull 0.05 · raster 4.13 · HiZ 0.04 · resolve 0.24 · ReSTIR 27.49 · shadow 0.00 · post 1.36 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:45:38.829 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 31.9586 [ms] |
| 2026-09-19 13:45:38.831 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.049376 [ms] |
| 2026-09-19 13:45:38.833 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 4.13491 [ms] |
| 2026-09-19 13:45:38.835 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.043648 [ms] |
| 2026-09-19 13:45:38.836 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.236864 [ms] |
| 2026-09-19 13:45:38.837 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 27.4938 [ms] |
| 2026-09-19 13:45:38.845 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:45:38.847 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.3631 [ms] |
| 2026-09-19 13:45:38.849 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:45:38.851 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:45:38.853 | INFO | Visibility | Clusters 791 tested -> 751 frustum, 751 cone, 728 visible | draws 732+1, 86020 triangles |
| 2026-09-19 13:45:38.859 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:45:38.861 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 751 [count] |
| 2026-09-19 13:45:38.862 | INFO | TelemetryMetrics | Measurement: ClustersCone = 751 [count] |
| 2026-09-19 13:45:38.864 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 728 [count] |
| 2026-09-19 13:45:38.866 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 86020 [count] |
| 2026-09-19 13:45:38.867 | INFO | TelemetryMetrics | Measurement: DrawCalls = 733 [count] |
| 2026-09-19 13:45:38.869 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 86% of GPU frame |
| 2026-09-19 13:45:38.880 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:45:38.882 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:45:38.884 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:45:38.887 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:45:38.889 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:45:38.893 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 13:45:38.895 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 86.0294 [percent] |
| 2026-09-19 13:45:44.167 | INFO | Performance | CPU 46.03 ms/frame (22.0 fps, worst 100.00 ms over 109 frames), RSS 1189 MiB |
| 2026-09-19 13:45:44.169 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 46.0251 [ms] |
| 2026-09-19 13:45:44.171 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:45:44.172 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 22.0441 [fps] |
| 2026-09-19 13:45:44.174 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 109 [count] |
| 2026-09-19 13:45:44.180 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1188.9 [MiB] |
| 2026-09-19 13:45:44.181 | INFO | GpuTiming | GPU 71.90 ms total | cull 0.05 · raster 3.13 · HiZ 0.05 · resolve 0.66 · ReSTIR 68.02 · shadow 0.00 · post 1.73 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:45:44.185 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 71.9003 [ms] |
| 2026-09-19 13:45:44.190 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.045536 [ms] |
| 2026-09-19 13:45:44.195 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.13018 [ms] |
| 2026-09-19 13:45:44.197 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 13:45:44.198 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.65536 [ms] |
| 2026-09-19 13:45:44.204 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 68.0221 [ms] |
| 2026-09-19 13:45:44.205 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:45:44.208 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.72662 [ms] |
| 2026-09-19 13:45:44.212 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:45:44.214 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:45:44.219 | INFO | Visibility | Clusters 791 tested -> 680 frustum, 680 cone, 680 visible | draws 642+38, 81216 triangles |
| 2026-09-19 13:45:44.220 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:45:44.224 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 680 [count] |
| 2026-09-19 13:45:44.229 | INFO | TelemetryMetrics | Measurement: ClustersCone = 680 [count] |
| 2026-09-19 13:45:44.234 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 680 [count] |
| 2026-09-19 13:45:44.235 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 81216 [count] |
| 2026-09-19 13:45:44.237 | INFO | TelemetryMetrics | Measurement: DrawCalls = 680 [count] |
| 2026-09-19 13:45:44.239 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 95% of GPU frame |
| 2026-09-19 13:45:44.249 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:45:44.251 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:45:44.254 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:45:44.256 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:45:44.264 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:45:44.266 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:45:44.267 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 94.6062 [percent] |
| 2026-09-19 13:45:49.541 | INFO | Performance | CPU 53.34 ms/frame (18.2 fps, worst 100.00 ms over 94 frames), RSS 1190 MiB |
| 2026-09-19 13:45:49.642 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 53.3416 [ms] |
| 2026-09-19 13:45:49.643 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:45:49.660 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 18.2275 [fps] |
| 2026-09-19 13:45:49.671 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 94 [count] |
| 2026-09-19 13:45:49.673 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1190.23 [MiB] |
| 2026-09-19 13:45:49.676 | INFO | GpuTiming | GPU 61.94 ms total | cull 0.05 · raster 3.12 · HiZ 0.05 · resolve 0.56 · ReSTIR 58.18 · shadow 0.00 · post 1.63 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:45:49.688 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 61.9413 [ms] |
| 2026-09-19 13:45:49.691 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.047328 [ms] |
| 2026-09-19 13:45:49.693 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.11616 [ms] |
| 2026-09-19 13:45:49.701 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045312 [ms] |
| 2026-09-19 13:45:49.721 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.556704 [ms] |
| 2026-09-19 13:45:49.726 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 58.1758 [ms] |
| 2026-09-19 13:45:49.759 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:45:49.772 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.63197 [ms] |
| 2026-09-19 13:45:49.787 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:45:49.790 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:45:49.794 | INFO | Visibility | Clusters 791 tested -> 607 frustum, 607 cone, 607 visible | draws 607+0, 72030 triangles |
| 2026-09-19 13:45:49.804 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:45:49.808 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 607 [count] |
| 2026-09-19 13:45:49.810 | INFO | TelemetryMetrics | Measurement: ClustersCone = 607 [count] |
| 2026-09-19 13:45:49.823 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 607 [count] |
| 2026-09-19 13:45:49.827 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 72030 [count] |
| 2026-09-19 13:45:49.836 | INFO | TelemetryMetrics | Measurement: DrawCalls = 607 [count] |
| 2026-09-19 13:45:49.839 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 94% of GPU frame |
| 2026-09-19 13:45:49.843 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:45:49.854 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:45:49.857 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:45:49.859 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:45:49.861 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:45:49.867 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:45:49.870 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.9209 [percent] |
| 2026-09-19 13:45:56.024 | INFO | Performance | CPU 68.15 ms/frame (16.0 fps, worst 100.00 ms over 74 frames), RSS 1190 MiB |
| 2026-09-19 13:45:56.027 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 68.1454 [ms] |
| 2026-09-19 13:45:56.031 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 13:45:56.032 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.9704 [fps] |
| 2026-09-19 13:45:56.039 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 74 [count] |
| 2026-09-19 13:45:56.042 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1190.24 [MiB] |
| 2026-09-19 13:45:56.044 | INFO | GpuTiming | GPU 69.62 ms total | cull 0.04 · raster 2.61 · HiZ 0.05 · resolve 0.57 · ReSTIR 66.35 · shadow 0.00 · post 1.47 · sky 0.00 · volume 0.00 |
| 2026-09-19 13:45:56.056 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 69.6203 [ms] |
| 2026-09-19 13:45:56.059 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.041408 [ms] |
| 2026-09-19 13:45:56.063 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 2.60755 [ms] |
| 2026-09-19 13:45:56.079 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047136 [ms] |
| 2026-09-19 13:45:56.084 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.569664 [ms] |
| 2026-09-19 13:45:56.091 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 66.3545 [ms] |
| 2026-09-19 13:45:56.096 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 13:45:56.099 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.46688 [ms] |
| 2026-09-19 13:45:56.107 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 13:45:56.112 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 13:45:56.114 | INFO | Visibility | Clusters 791 tested -> 607 frustum, 607 cone, 607 visible | draws 607+0, 72030 triangles |
| 2026-09-19 13:45:56.117 | INFO | TelemetryMetrics | Measurement: ClustersTested = 791 [count] |
| 2026-09-19 13:45:56.124 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 607 [count] |
| 2026-09-19 13:45:56.131 | INFO | TelemetryMetrics | Measurement: ClustersCone = 607 [count] |
| 2026-09-19 13:45:56.145 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 607 [count] |
| 2026-09-19 13:45:56.148 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 72030 [count] |
| 2026-09-19 13:45:56.163 | INFO | TelemetryMetrics | Measurement: DrawCalls = 607 [count] |
| 2026-09-19 13:45:56.164 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 95% of GPU frame |
| 2026-09-19 13:45:56.167 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 13:45:56.677 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 13:45:56.682 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 13:45:56.683 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 13:45:56.761 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 13:45:56.765 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 13:45:56.767 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 95.3092 [percent] |
| 2026-09-19 13:46:15.281 | INFO | Shutdown | Render loop exited cleanly. |
