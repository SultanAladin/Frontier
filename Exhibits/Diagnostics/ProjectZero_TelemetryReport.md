# ProjectZero_TelemetryReport — Telemetry Log

| Timestamp | Severity | Category | Record Description |
|:---|:---:|:---|:---|
| 2026-09-20 14:44:43.939 | INFO | Bootstrap | Project-Zero windowed ReSTIR renderer starting. |
| 2026-09-20 14:44:49.148 | INFO | Scene | Showcase: 229506 triangles, 499 instances, 2045 clusters, 241 materials, 3368 luminaires, bounds [-40.00 -40.00 0.00]..[40.00 40.00 6.00] m |
| 2026-09-20 14:44:56.078 | INFO | Textures | Textures: 6 resident (0 placeholder), 64.0 MB with mips, decoded in 6927 ms |
| 2026-09-20 14:44:56.080 | INFO | Materials | Materials: 241 descriptors -> 241 records, 241 slabs (limit 1, 0 folded), 499 placements, 0 cameras, 0 punctual lights |
| 2026-09-20 14:44:56.544 | INFO | Interface | Panel light Low: rgb (0.000 0.008 0.042) from 4 figures, 5% coverage, 3370 luminaires now. |
| 2026-09-20 14:45:01.730 | INFO | Traversal | CWBVH: 229508 triangles → 39627 nodes, 3095.9 KB nodes + 16137.3 KB leaves (85.8 B/tri), SAH 13.84, built in 4653.6 ms (spatial splits) |
| 2026-09-20 14:49:12.262 | INFO | Bootstrap | Window and Vulkan swapchain ready. |
| 2026-09-20 14:49:14.186 | INFO | Traversal | Two-level: 500 instances -> 500 BLASes over 229508 triangles, top level 598 nodes, shared blobs 2631.4 KB + 10758.2 KB, built in 507.9 ms |
| 2026-09-20 14:49:14.279 | INFO | Moons | 6 textures resident, moon slots 0..5. |
| 2026-09-20 14:49:14.281 | INFO | Stars | 9683 stars in 1024 cells uploaded to binding 23. |
| 2026-09-20 14:49:18.462 | INFO | Bootstrap | Entering render loop. |
| 2026-09-20 14:49:18.562 | INFO | Interface | Director ready: TAB switches screens. The card carries 2 converted vector segments. |
| 2026-09-20 14:49:19.115 | INFO | Audio | Panel bound to audio: drag the progress bar to change the engine note. |
| 2026-09-20 14:49:19.116 | INFO | Interface | Spatial interface ready: 14 figures, depth test off. |
| 2026-09-20 14:49:19.166 | INFO | Shadows | Shadow path: rasterised maps (GI off) - Hard @ 1024 px, 1 taps. |
| 2026-09-20 14:49:25.120 | INFO | Performance | CPU 52.77 ms/frame (18.9 fps, worst 100.00 ms over 95 frames), RSS 814 MiB |
| 2026-09-20 14:49:25.121 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 52.7741 [ms] |
| 2026-09-20 14:49:25.122 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:49:25.122 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 18.9487 [fps] |
| 2026-09-20 14:49:25.124 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 95 [count] |
| 2026-09-20 14:49:25.126 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 813.867 [MiB] |
| 2026-09-20 14:49:25.128 | INFO | GpuTiming | GPU 56.02 ms total | cull 0.08 · raster 6.83 · HiZ 0.05 · resolve 0.77 · ReSTIR 48.29 · shadow 0.00 · post 0.88 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:49:25.130 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 56.0151 [ms] |
| 2026-09-20 14:49:25.132 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.078368 [ms] |
| 2026-09-20 14:49:25.133 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 6.8295 [ms] |
| 2026-09-20 14:49:25.137 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045216 [ms] |
| 2026-09-20 14:49:25.138 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.772032 [ms] |
| 2026-09-20 14:49:25.141 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 48.29 [ms] |
| 2026-09-20 14:49:25.148 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:49:25.150 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.882557 [ms] |
| 2026-09-20 14:49:25.151 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:49:25.153 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:49:25.155 | INFO | Visibility | Clusters 2046 tested -> 1649 frustum, 1649 cone, 1649 visible | draws 1649+0, 185548 triangles |
| 2026-09-20 14:49:25.161 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:49:25.162 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1649 [count] |
| 2026-09-20 14:49:25.164 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1649 [count] |
| 2026-09-20 14:49:25.166 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1649 [count] |
| 2026-09-20 14:49:25.168 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 185548 [count] |
| 2026-09-20 14:49:25.169 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1649 [count] |
| 2026-09-20 14:49:25.171 | INFO | Performance | GPU-BOUND | 922 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 86% of GPU frame |
| 2026-09-20 14:49:25.176 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-20 14:49:25.178 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:49:25.180 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:49:25.183 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:49:25.184 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:49:25.186 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:49:25.187 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 86.2089 [percent] |
| 2026-09-20 14:49:30.512 | INFO | Performance | CPU 51.62 ms/frame (18.7 fps, worst 100.00 ms over 97 frames), RSS 753 MiB |
| 2026-09-20 14:49:30.514 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 51.6189 [ms] |
| 2026-09-20 14:49:30.517 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:49:30.519 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 18.7299 [fps] |
| 2026-09-20 14:49:30.520 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 97 [count] |
| 2026-09-20 14:49:30.522 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 753.145 [MiB] |
| 2026-09-20 14:49:30.524 | INFO | GpuTiming | GPU 47.66 ms total | cull 0.09 · raster 9.19 · HiZ 0.05 · resolve 1.20 · ReSTIR 37.13 · shadow 0.00 · post 0.72 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:49:30.527 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 47.6618 [ms] |
| 2026-09-20 14:49:30.528 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.092864 [ms] |
| 2026-09-20 14:49:30.530 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.19347 [ms] |
| 2026-09-20 14:49:30.536 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.046336 [ms] |
| 2026-09-20 14:49:30.539 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.20218 [ms] |
| 2026-09-20 14:49:30.541 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 37.1269 [ms] |
| 2026-09-20 14:49:30.544 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:49:30.547 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.724224 [ms] |
| 2026-09-20 14:49:30.551 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:49:30.553 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:49:30.555 | INFO | Visibility | Clusters 2046 tested -> 1949 frustum, 1949 cone, 1949 visible | draws 1949+0, 218760 triangles |
| 2026-09-20 14:49:30.556 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:49:30.557 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1949 [count] |
| 2026-09-20 14:49:30.559 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1949 [count] |
| 2026-09-20 14:49:30.560 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1949 [count] |
| 2026-09-20 14:49:30.562 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 218760 [count] |
| 2026-09-20 14:49:30.563 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1949 [count] |
| 2026-09-20 14:49:30.568 | INFO | Performance | GPU-BOUND | 922 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 78% of GPU frame |
| 2026-09-20 14:49:30.569 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-20 14:49:30.570 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:49:30.571 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:49:30.572 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:49:30.574 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:49:30.575 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:49:30.576 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 77.8966 [percent] |
| 2026-09-20 14:49:35.561 | INFO | Performance | CPU 43.48 ms/frame (23.1 fps, worst 100.00 ms over 115 frames), RSS 788 MiB |
| 2026-09-20 14:49:35.563 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 43.4798 [ms] |
| 2026-09-20 14:49:35.566 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:49:35.568 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 23.0994 [fps] |
| 2026-09-20 14:49:35.569 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 115 [count] |
| 2026-09-20 14:49:35.571 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 788.477 [MiB] |
| 2026-09-20 14:49:35.573 | INFO | GpuTiming | GPU 40.20 ms total | cull 0.09 · raster 8.65 · HiZ 0.05 · resolve 1.35 · ReSTIR 30.07 · shadow 0.00 · post 0.65 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:49:35.575 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 40.2029 [ms] |
| 2026-09-20 14:49:35.576 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.085728 [ms] |
| 2026-09-20 14:49:35.578 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.65114 [ms] |
| 2026-09-20 14:49:35.580 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045088 [ms] |
| 2026-09-20 14:49:35.586 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.35066 [ms] |
| 2026-09-20 14:49:35.588 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 30.0703 [ms] |
| 2026-09-20 14:49:35.590 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:49:35.592 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.654816 [ms] |
| 2026-09-20 14:49:35.593 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:49:35.595 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:49:35.602 | INFO | Visibility | Clusters 2046 tested -> 1949 frustum, 1949 cone, 1949 visible | draws 1949+0, 218760 triangles |
| 2026-09-20 14:49:35.604 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:49:35.606 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1949 [count] |
| 2026-09-20 14:49:35.608 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1949 [count] |
| 2026-09-20 14:49:35.609 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1949 [count] |
| 2026-09-20 14:49:35.611 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 218760 [count] |
| 2026-09-20 14:49:35.617 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1949 [count] |
| 2026-09-20 14:49:35.618 | INFO | Performance | GPU-BOUND | 922 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 75% of GPU frame |
| 2026-09-20 14:49:35.620 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-20 14:49:35.622 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:49:35.624 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:49:35.625 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:49:35.627 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:49:35.632 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:49:35.634 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 74.7963 [percent] |
| 2026-09-20 14:49:40.630 | INFO | Performance | CPU 43.59 ms/frame (23.0 fps, worst 100.00 ms over 115 frames), RSS 820 MiB |
| 2026-09-20 14:49:40.632 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 43.5852 [ms] |
| 2026-09-20 14:49:40.634 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:49:40.636 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 22.993 [fps] |
| 2026-09-20 14:49:40.638 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 115 [count] |
| 2026-09-20 14:49:40.640 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 820.012 [MiB] |
| 2026-09-20 14:49:40.642 | INFO | GpuTiming | GPU 39.67 ms total | cull 0.09 · raster 9.01 · HiZ 0.05 · resolve 0.90 · ReSTIR 29.62 · shadow 0.00 · post 0.63 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:49:40.644 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 39.669 [ms] |
| 2026-09-20 14:49:40.645 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.085856 [ms] |
| 2026-09-20 14:49:40.647 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.0105 [ms] |
| 2026-09-20 14:49:40.649 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.04592 [ms] |
| 2026-09-20 14:49:40.651 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.901792 [ms] |
| 2026-09-20 14:49:40.653 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 29.6249 [ms] |
| 2026-09-20 14:49:40.661 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:49:40.664 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.631264 [ms] |
| 2026-09-20 14:49:40.668 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:49:40.673 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:49:40.675 | INFO | Visibility | Clusters 2046 tested -> 1949 frustum, 1949 cone, 1949 visible | draws 1949+0, 218760 triangles |
| 2026-09-20 14:49:40.677 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:49:40.679 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1949 [count] |
| 2026-09-20 14:49:40.681 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1949 [count] |
| 2026-09-20 14:49:40.685 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1949 [count] |
| 2026-09-20 14:49:40.690 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 218760 [count] |
| 2026-09-20 14:49:40.692 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1949 [count] |
| 2026-09-20 14:49:40.694 | INFO | Performance | GPU-BOUND | 922 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 75% of GPU frame |
| 2026-09-20 14:49:40.695 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-20 14:49:40.697 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:49:40.699 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:49:40.704 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:49:40.706 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:49:40.708 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:49:40.710 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 74.6803 [percent] |
| 2026-09-20 14:49:45.639 | INFO | Performance | CPU 35.22 ms/frame (29.1 fps, worst 100.00 ms over 142 frames), RSS 737 MiB |
| 2026-09-20 14:49:45.641 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 35.2178 [ms] |
| 2026-09-20 14:49:45.644 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:49:45.646 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 29.132 [fps] |
| 2026-09-20 14:49:45.649 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 142 [count] |
| 2026-09-20 14:49:45.651 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 736.836 [MiB] |
| 2026-09-20 14:49:45.653 | INFO | GpuTiming | GPU 24.89 ms total | cull 0.09 · raster 8.37 · HiZ 0.05 · resolve 1.01 · ReSTIR 15.37 · shadow 0.00 · post 0.61 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:49:45.656 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 24.8873 [ms] |
| 2026-09-20 14:49:45.659 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.092608 [ms] |
| 2026-09-20 14:49:45.661 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.36723 [ms] |
| 2026-09-20 14:49:45.663 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-20 14:49:45.672 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.00678 [ms] |
| 2026-09-20 14:49:45.674 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 15.3736 [ms] |
| 2026-09-20 14:49:45.677 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:49:45.678 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.605728 [ms] |
| 2026-09-20 14:49:45.680 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:49:45.683 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:49:45.685 | INFO | Visibility | Clusters 2046 tested -> 1949 frustum, 1949 cone, 1949 visible | draws 1949+0, 218760 triangles |
| 2026-09-20 14:49:45.688 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:49:45.690 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1949 [count] |
| 2026-09-20 14:49:45.692 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1949 [count] |
| 2026-09-20 14:49:45.695 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1949 [count] |
| 2026-09-20 14:49:45.702 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 218760 [count] |
| 2026-09-20 14:49:45.706 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1949 [count] |
| 2026-09-20 14:49:45.708 | INFO | Performance | CPU-BOUND or presenting-limited | 922 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 62% of GPU frame |
| 2026-09-20 14:49:45.709 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-20 14:49:45.712 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:49:45.715 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:49:45.717 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:49:45.719 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:49:45.722 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-20 14:49:45.724 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 61.7728 [percent] |
| 2026-09-20 14:49:50.722 | INFO | Performance | CPU 36.83 ms/frame (26.5 fps, worst 100.00 ms over 136 frames), RSS 724 MiB |
| 2026-09-20 14:49:50.724 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 36.8253 [ms] |
| 2026-09-20 14:49:50.727 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:49:50.731 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 26.5004 [fps] |
| 2026-09-20 14:49:50.733 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 136 [count] |
| 2026-09-20 14:49:50.736 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 723.574 [MiB] |
| 2026-09-20 14:49:50.745 | INFO | GpuTiming | GPU 40.57 ms total | cull 0.09 · raster 8.35 · HiZ 0.05 · resolve 1.06 · ReSTIR 31.03 · shadow 0.00 · post 0.59 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:49:50.747 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 40.5715 [ms] |
| 2026-09-20 14:49:50.750 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.086176 [ms] |
| 2026-09-20 14:49:50.758 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.35018 [ms] |
| 2026-09-20 14:49:50.760 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-20 14:49:50.762 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.05587 [ms] |
| 2026-09-20 14:49:50.765 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 31.0322 [ms] |
| 2026-09-20 14:49:50.768 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:49:50.775 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.587744 [ms] |
| 2026-09-20 14:49:50.778 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:49:50.781 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:49:50.785 | INFO | Visibility | Clusters 2046 tested -> 1949 frustum, 1949 cone, 1949 visible | draws 1949+0, 218760 triangles |
| 2026-09-20 14:49:50.791 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:49:50.793 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1949 [count] |
| 2026-09-20 14:49:50.796 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1949 [count] |
| 2026-09-20 14:49:50.798 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1949 [count] |
| 2026-09-20 14:49:50.800 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 218760 [count] |
| 2026-09-20 14:49:50.803 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1949 [count] |
| 2026-09-20 14:49:50.810 | INFO | Performance | GPU-BOUND | 922 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 76% of GPU frame |
| 2026-09-20 14:49:50.812 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-20 14:49:50.815 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:49:50.817 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:49:50.820 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:49:50.822 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:49:50.824 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:49:50.827 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 76.4876 [percent] |
| 2026-09-20 14:49:55.759 | INFO | Performance | CPU 35.48 ms/frame (29.1 fps, worst 100.00 ms over 141 frames), RSS 724 MiB |
| 2026-09-20 14:49:55.761 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 35.4772 [ms] |
| 2026-09-20 14:49:55.763 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:49:55.765 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 29.088 [fps] |
| 2026-09-20 14:49:55.766 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 141 [count] |
| 2026-09-20 14:49:55.768 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 723.59 [MiB] |
| 2026-09-20 14:49:55.770 | INFO | GpuTiming | GPU 26.65 ms total | cull 0.10 · raster 8.36 · HiZ 0.05 · resolve 0.91 · ReSTIR 17.23 · shadow 0.00 · post 0.57 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:49:55.772 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 26.6459 [ms] |
| 2026-09-20 14:49:55.776 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.098752 [ms] |
| 2026-09-20 14:49:55.778 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.35901 [ms] |
| 2026-09-20 14:49:55.779 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.046656 [ms] |
| 2026-09-20 14:49:55.781 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.90832 [ms] |
| 2026-09-20 14:49:55.790 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 17.2332 [ms] |
| 2026-09-20 14:49:55.792 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:49:55.793 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.567007 [ms] |
| 2026-09-20 14:49:55.795 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:49:55.797 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:49:55.801 | INFO | Visibility | Clusters 2046 tested -> 1949 frustum, 1949 cone, 1949 visible | draws 1949+0, 218760 triangles |
| 2026-09-20 14:49:55.804 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:49:55.806 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1949 [count] |
| 2026-09-20 14:49:55.808 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1949 [count] |
| 2026-09-20 14:49:55.810 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1949 [count] |
| 2026-09-20 14:49:55.812 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 218760 [count] |
| 2026-09-20 14:49:55.813 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1949 [count] |
| 2026-09-20 14:49:55.820 | INFO | Performance | CPU-BOUND or presenting-limited | 922 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 65% of GPU frame |
| 2026-09-20 14:49:55.822 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-20 14:49:55.824 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:49:55.825 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:49:55.827 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:49:55.828 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:49:55.832 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-20 14:49:55.834 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 64.6747 [percent] |
| 2026-09-20 14:50:00.806 | INFO | Performance | CPU 34.75 ms/frame (27.8 fps, worst 100.00 ms over 145 frames), RSS 724 MiB |
| 2026-09-20 14:50:00.810 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 34.7529 [ms] |
| 2026-09-20 14:50:00.813 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:50:00.814 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 27.8078 [fps] |
| 2026-09-20 14:50:00.817 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 145 [count] |
| 2026-09-20 14:50:00.818 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 723.602 [MiB] |
| 2026-09-20 14:50:00.825 | INFO | GpuTiming | GPU 28.36 ms total | cull 0.13 · raster 8.58 · HiZ 0.05 · resolve 1.19 · ReSTIR 18.42 · shadow 0.00 · post 0.55 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:50:00.827 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 28.3587 [ms] |
| 2026-09-20 14:50:00.828 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.129984 [ms] |
| 2026-09-20 14:50:00.830 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.57597 [ms] |
| 2026-09-20 14:50:00.832 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.04848 [ms] |
| 2026-09-20 14:50:00.833 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.18838 [ms] |
| 2026-09-20 14:50:00.838 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 18.4159 [ms] |
| 2026-09-20 14:50:00.840 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:50:00.842 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.54653 [ms] |
| 2026-09-20 14:50:00.844 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:50:00.848 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:50:00.850 | INFO | Visibility | Clusters 2046 tested -> 1949 frustum, 1949 cone, 1949 visible | draws 1949+0, 218760 triangles |
| 2026-09-20 14:50:00.856 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:50:00.857 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1949 [count] |
| 2026-09-20 14:50:00.859 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1949 [count] |
| 2026-09-20 14:50:00.861 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1949 [count] |
| 2026-09-20 14:50:00.862 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 218760 [count] |
| 2026-09-20 14:50:00.864 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1949 [count] |
| 2026-09-20 14:50:00.865 | INFO | Performance | CPU-BOUND or presenting-limited | 922 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 65% of GPU frame |
| 2026-09-20 14:50:00.870 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-20 14:50:00.872 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:50:00.874 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:50:00.876 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:50:00.878 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:50:00.879 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-20 14:50:00.880 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 64.9391 [percent] |
| 2026-09-20 14:50:06.092 | INFO | Performance | CPU 56.08 ms/frame (19.7 fps, worst 100.00 ms over 90 frames), RSS 790 MiB |
| 2026-09-20 14:50:06.093 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 56.0805 [ms] |
| 2026-09-20 14:50:06.096 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:50:06.098 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 19.676 [fps] |
| 2026-09-20 14:50:06.099 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 90 [count] |
| 2026-09-20 14:50:06.101 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 790.039 [MiB] |
| 2026-09-20 14:50:06.106 | INFO | GpuTiming | GPU 74.25 ms total | cull 0.08 · raster 4.45 · HiZ 0.04 · resolve 0.87 · ReSTIR 68.82 · shadow 0.00 · post 0.74 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:50:06.108 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 74.247 [ms] |
| 2026-09-20 14:50:06.110 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.076128 [ms] |
| 2026-09-20 14:50:06.112 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 4.44525 [ms] |
| 2026-09-20 14:50:06.114 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.043968 [ms] |
| 2026-09-20 14:50:06.120 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.865312 [ms] |
| 2026-09-20 14:50:06.125 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 68.8164 [ms] |
| 2026-09-20 14:50:06.127 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:50:06.130 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.742882 [ms] |
| 2026-09-20 14:50:06.133 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:50:06.135 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:50:06.138 | INFO | Visibility | Clusters 2046 tested -> 989 frustum, 989 cone, 989 visible | draws 985+4, 110872 triangles |
| 2026-09-20 14:50:06.142 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:50:06.144 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 989 [count] |
| 2026-09-20 14:50:06.149 | INFO | TelemetryMetrics | Measurement: ClustersCone = 989 [count] |
| 2026-09-20 14:50:06.151 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 989 [count] |
| 2026-09-20 14:50:06.154 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 110872 [count] |
| 2026-09-20 14:50:06.156 | INFO | TelemetryMetrics | Measurement: DrawCalls = 989 [count] |
| 2026-09-20 14:50:06.158 | INFO | Performance | GPU-BOUND | 922 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-20 14:50:06.160 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-20 14:50:06.162 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:50:06.167 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:50:06.170 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:50:06.171 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:50:06.173 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:50:06.175 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.6857 [percent] |
| 2026-09-20 14:50:12.681 | INFO | Performance | CPU 75.99 ms/frame (14.8 fps, worst 100.00 ms over 66 frames), RSS 739 MiB |
| 2026-09-20 14:50:12.682 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 75.9856 [ms] |
| 2026-09-20 14:50:12.685 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:50:12.687 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.7849 [fps] |
| 2026-09-20 14:50:12.689 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 66 [count] |
| 2026-09-20 14:50:12.690 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 738.664 [MiB] |
| 2026-09-20 14:50:12.692 | INFO | GpuTiming | GPU 125.88 ms total | cull 0.07 · raster 2.02 · HiZ 0.05 · resolve 0.34 · ReSTIR 123.41 · shadow 0.00 · post 0.75 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:50:12.693 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 125.883 [ms] |
| 2026-09-20 14:50:12.700 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.072608 [ms] |
| 2026-09-20 14:50:12.703 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 2.01712 [ms] |
| 2026-09-20 14:50:12.704 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.046816 [ms] |
| 2026-09-20 14:50:12.707 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.339424 [ms] |
| 2026-09-20 14:50:12.708 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 123.407 [ms] |
| 2026-09-20 14:50:12.714 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:50:12.716 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.747139 [ms] |
| 2026-09-20 14:50:12.718 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:50:12.719 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:50:12.721 | INFO | Visibility | Clusters 2046 tested -> 500 frustum, 500 cone, 496 visible | draws 496+0, 55552 triangles |
| 2026-09-20 14:50:12.722 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:50:12.725 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 500 [count] |
| 2026-09-20 14:50:12.726 | INFO | TelemetryMetrics | Measurement: ClustersCone = 500 [count] |
| 2026-09-20 14:50:12.733 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 496 [count] |
| 2026-09-20 14:50:12.735 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 55552 [count] |
| 2026-09-20 14:50:12.736 | INFO | TelemetryMetrics | Measurement: DrawCalls = 496 [count] |
| 2026-09-20 14:50:12.739 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 98% of GPU frame |
| 2026-09-20 14:50:12.740 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:50:12.745 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:50:12.749 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:50:12.751 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:50:12.753 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:50:12.754 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:50:12.756 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 98.0331 [percent] |
| 2026-09-20 14:50:18.817 | INFO | Performance | CPU 65.47 ms/frame (14.5 fps, worst 100.00 ms over 77 frames), RSS 796 MiB |
| 2026-09-20 14:50:18.821 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 65.4706 [ms] |
| 2026-09-20 14:50:18.824 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:50:18.826 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 14.5309 [fps] |
| 2026-09-20 14:50:18.828 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 77 [count] |
| 2026-09-20 14:50:18.830 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 796.492 [MiB] |
| 2026-09-20 14:50:18.832 | INFO | GpuTiming | GPU 61.21 ms total | cull 0.10 · raster 10.09 · HiZ 0.05 · resolve 1.30 · ReSTIR 49.67 · shadow 0.00 · post 0.80 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:50:18.833 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 61.2082 [ms] |
| 2026-09-20 14:50:18.836 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.099328 [ms] |
| 2026-09-20 14:50:18.838 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 10.0926 [ms] |
| 2026-09-20 14:50:18.842 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.0472 [ms] |
| 2026-09-20 14:50:18.848 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.29565 [ms] |
| 2026-09-20 14:50:18.851 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 49.6734 [ms] |
| 2026-09-20 14:50:18.855 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:50:18.857 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.801311 [ms] |
| 2026-09-20 14:50:18.862 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:50:18.863 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:50:18.865 | INFO | Visibility | Clusters 2046 tested -> 2000 frustum, 2000 cone, 2000 visible | draws 2000+0, 225248 triangles |
| 2026-09-20 14:50:18.867 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:50:18.870 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 2000 [count] |
| 2026-09-20 14:50:18.871 | INFO | TelemetryMetrics | Measurement: ClustersCone = 2000 [count] |
| 2026-09-20 14:50:18.878 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 2000 [count] |
| 2026-09-20 14:50:18.881 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 225248 [count] |
| 2026-09-20 14:50:18.883 | INFO | TelemetryMetrics | Measurement: DrawCalls = 2000 [count] |
| 2026-09-20 14:50:18.885 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 81% of GPU frame |
| 2026-09-20 14:50:18.887 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:50:18.894 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:50:18.896 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:50:18.899 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:50:18.900 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:50:18.902 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:50:18.904 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 81.1548 [percent] |
| 2026-09-20 14:50:20.996 | INFO | Shadows | Shadow path: rasterised maps (GI off) - Hard @ 1024 px, 1 taps. |
| 2026-09-20 14:50:21.448 | INFO | Shadows | Shadow path: rasterised maps (GI off) - Hard @ 1024 px, 1 taps. |
| 2026-09-20 14:50:23.989 | INFO | Performance | CPU 48.65 ms/frame (19.9 fps, worst 100.00 ms over 103 frames), RSS 743 MiB |
| 2026-09-20 14:50:23.991 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 48.6467 [ms] |
| 2026-09-20 14:50:23.993 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:50:23.995 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 19.9118 [fps] |
| 2026-09-20 14:50:23.997 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 103 [count] |
| 2026-09-20 14:50:23.998 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 743.137 [MiB] |
| 2026-09-20 14:50:24.000 | INFO | GpuTiming | GPU 36.75 ms total | cull 0.09 · raster 8.71 · HiZ 0.04 · resolve 0.72 · ReSTIR 27.19 · shadow 0.00 · post 0.44 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:50:24.002 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 36.7468 [ms] |
| 2026-09-20 14:50:24.011 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.087168 [ms] |
| 2026-09-20 14:50:24.014 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.71405 [ms] |
| 2026-09-20 14:50:24.015 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.035328 [ms] |
| 2026-09-20 14:50:24.019 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.71904 [ms] |
| 2026-09-20 14:50:24.025 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 27.1912 [ms] |
| 2026-09-20 14:50:24.027 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:50:24.030 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.438368 [ms] |
| 2026-09-20 14:50:24.032 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:50:24.033 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:50:24.035 | INFO | Visibility | Clusters 2046 tested -> 2000 frustum, 2000 cone, 2000 visible | draws 2000+0, 225248 triangles |
| 2026-09-20 14:50:24.041 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:50:24.043 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 2000 [count] |
| 2026-09-20 14:50:24.045 | INFO | TelemetryMetrics | Measurement: ClustersCone = 2000 [count] |
| 2026-09-20 14:50:24.047 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 2000 [count] |
| 2026-09-20 14:50:24.049 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 225248 [count] |
| 2026-09-20 14:50:24.051 | INFO | TelemetryMetrics | Measurement: DrawCalls = 2000 [count] |
| 2026-09-20 14:50:24.057 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 74% of GPU frame |
| 2026-09-20 14:50:24.063 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:50:24.064 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:50:24.069 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:50:24.075 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:50:24.077 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:50:24.079 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-20 14:50:24.080 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 73.9961 [percent] |
| 2026-09-20 14:50:29.187 | INFO | Performance | CPU 51.74 ms/frame (20.1 fps, worst 100.00 ms over 97 frames), RSS 778 MiB |
| 2026-09-20 14:50:29.189 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 51.7373 [ms] |
| 2026-09-20 14:50:29.191 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:50:29.193 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 20.094 [fps] |
| 2026-09-20 14:50:29.201 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 97 [count] |
| 2026-09-20 14:50:29.203 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 778.426 [MiB] |
| 2026-09-20 14:50:29.206 | INFO | GpuTiming | GPU 50.89 ms total | cull 0.09 · raster 8.80 · HiZ 0.04 · resolve 0.43 · ReSTIR 41.53 · shadow 0.00 · post 0.47 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:50:29.207 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 50.8857 [ms] |
| 2026-09-20 14:50:29.210 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.089472 [ms] |
| 2026-09-20 14:50:29.212 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.8032 [ms] |
| 2026-09-20 14:50:29.217 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.035168 [ms] |
| 2026-09-20 14:50:29.221 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.4304 [ms] |
| 2026-09-20 14:50:29.223 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 41.5275 [ms] |
| 2026-09-20 14:50:29.225 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:50:29.228 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.472931 [ms] |
| 2026-09-20 14:50:29.229 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:50:29.237 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:50:29.241 | INFO | Visibility | Clusters 2046 tested -> 2015 frustum, 2015 cone, 2015 visible | draws 2015+0, 226580 triangles |
| 2026-09-20 14:50:29.243 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:50:29.244 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 2015 [count] |
| 2026-09-20 14:50:29.250 | INFO | TelemetryMetrics | Measurement: ClustersCone = 2015 [count] |
| 2026-09-20 14:50:29.252 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 2015 [count] |
| 2026-09-20 14:50:29.254 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 226580 [count] |
| 2026-09-20 14:50:29.256 | INFO | TelemetryMetrics | Measurement: DrawCalls = 2015 [count] |
| 2026-09-20 14:50:29.259 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 82% of GPU frame |
| 2026-09-20 14:50:29.264 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:50:29.266 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:50:29.270 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:50:29.275 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:50:29.277 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:50:29.283 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:50:29.285 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 81.6093 [percent] |
| 2026-09-20 14:50:34.308 | INFO | Performance | CPU 48.16 ms/frame (20.3 fps, worst 100.00 ms over 104 frames), RSS 739 MiB |
| 2026-09-20 14:50:34.310 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 48.1557 [ms] |
| 2026-09-20 14:50:34.312 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:50:34.314 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 20.3331 [fps] |
| 2026-09-20 14:50:34.316 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 104 [count] |
| 2026-09-20 14:50:34.317 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 739.375 [MiB] |
| 2026-09-20 14:50:34.319 | INFO | GpuTiming | GPU 43.65 ms total | cull 0.08 · raster 9.15 · HiZ 0.03 · resolve 0.99 · ReSTIR 33.39 · shadow 0.00 · post 0.45 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:50:34.321 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 43.6476 [ms] |
| 2026-09-20 14:50:34.322 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.083616 [ms] |
| 2026-09-20 14:50:34.325 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.14925 [ms] |
| 2026-09-20 14:50:34.331 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.034816 [ms] |
| 2026-09-20 14:50:34.333 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.9856 [ms] |
| 2026-09-20 14:50:34.335 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 33.3943 [ms] |
| 2026-09-20 14:50:34.336 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:50:34.338 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.451008 [ms] |
| 2026-09-20 14:50:34.344 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:50:34.346 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:50:34.348 | INFO | Visibility | Clusters 2046 tested -> 1955 frustum, 1955 cone, 1955 visible | draws 1955+0, 220654 triangles |
| 2026-09-20 14:50:34.350 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:50:34.351 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1955 [count] |
| 2026-09-20 14:50:34.353 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1955 [count] |
| 2026-09-20 14:50:34.354 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1955 [count] |
| 2026-09-20 14:50:34.356 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 220654 [count] |
| 2026-09-20 14:50:34.362 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1955 [count] |
| 2026-09-20 14:50:34.364 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 77% of GPU frame |
| 2026-09-20 14:50:34.366 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:50:34.368 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:50:34.375 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:50:34.377 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:50:34.380 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:50:34.381 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:50:34.383 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 76.5089 [percent] |
| 2026-09-20 14:50:39.355 | INFO | Performance | CPU 36.33 ms/frame (28.0 fps, worst 100.00 ms over 138 frames), RSS 824 MiB |
| 2026-09-20 14:50:39.356 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 36.3346 [ms] |
| 2026-09-20 14:50:39.359 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:50:39.361 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 28.0141 [fps] |
| 2026-09-20 14:50:39.362 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 138 [count] |
| 2026-09-20 14:50:39.364 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 823.891 [MiB] |
| 2026-09-20 14:50:39.366 | INFO | GpuTiming | GPU 28.41 ms total | cull 0.08 · raster 8.23 · HiZ 0.03 · resolve 0.70 · ReSTIR 19.36 · shadow 0.00 · post 0.37 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:50:39.370 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 28.412 [ms] |
| 2026-09-20 14:50:39.372 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.08 [ms] |
| 2026-09-20 14:50:39.374 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.23053 [ms] |
| 2026-09-20 14:50:39.376 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.03488 [ms] |
| 2026-09-20 14:50:39.377 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.704512 [ms] |
| 2026-09-20 14:50:39.380 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 19.3621 [ms] |
| 2026-09-20 14:50:39.381 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:50:39.386 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.371105 [ms] |
| 2026-09-20 14:50:39.387 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:50:39.389 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:50:39.390 | INFO | Visibility | Clusters 2046 tested -> 1712 frustum, 1712 cone, 1712 visible | draws 1712+0, 193132 triangles |
| 2026-09-20 14:50:39.392 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:50:39.394 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1712 [count] |
| 2026-09-20 14:50:39.395 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1712 [count] |
| 2026-09-20 14:50:39.396 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1712 [count] |
| 2026-09-20 14:50:39.398 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 193132 [count] |
| 2026-09-20 14:50:39.402 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1712 [count] |
| 2026-09-20 14:50:39.403 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 68% of GPU frame |
| 2026-09-20 14:50:39.405 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:50:39.407 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:50:39.408 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:50:39.410 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:50:39.412 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:50:39.413 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-20 14:50:39.418 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 68.1475 [percent] |
| 2026-09-20 14:50:44.393 | INFO | Performance | CPU 33.32 ms/frame (29.7 fps, worst 100.00 ms over 152 frames), RSS 800 MiB |
| 2026-09-20 14:50:44.395 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 33.3241 [ms] |
| 2026-09-20 14:50:44.397 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:50:44.399 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 29.6513 [fps] |
| 2026-09-20 14:50:44.400 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 152 [count] |
| 2026-09-20 14:50:44.403 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 799.785 [MiB] |
| 2026-09-20 14:50:44.404 | INFO | GpuTiming | GPU 34.26 ms total | cull 0.06 · raster 2.06 · HiZ 0.04 · resolve 0.49 · ReSTIR 31.61 · shadow 0.00 · post 0.52 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:50:44.406 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 34.263 [ms] |
| 2026-09-20 14:50:44.407 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.060544 [ms] |
| 2026-09-20 14:50:44.411 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 2.06061 [ms] |
| 2026-09-20 14:50:44.413 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.035424 [ms] |
| 2026-09-20 14:50:44.415 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.493728 [ms] |
| 2026-09-20 14:50:44.417 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 31.6127 [ms] |
| 2026-09-20 14:50:44.423 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:50:44.426 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.519489 [ms] |
| 2026-09-20 14:50:44.427 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:50:44.429 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:50:44.431 | INFO | Visibility | Clusters 2046 tested -> 550 frustum, 550 cone, 478 visible | draws 479+0, 53688 triangles |
| 2026-09-20 14:50:44.432 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:50:44.437 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 550 [count] |
| 2026-09-20 14:50:44.439 | INFO | TelemetryMetrics | Measurement: ClustersCone = 550 [count] |
| 2026-09-20 14:50:44.441 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 478 [count] |
| 2026-09-20 14:50:44.443 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 53688 [count] |
| 2026-09-20 14:50:44.445 | INFO | TelemetryMetrics | Measurement: DrawCalls = 479 [count] |
| 2026-09-20 14:50:44.447 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 92% of GPU frame |
| 2026-09-20 14:50:44.449 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:50:44.455 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:50:44.457 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:50:44.462 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:50:44.464 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:50:44.471 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:50:44.473 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.2648 [percent] |
| 2026-09-20 14:50:49.497 | INFO | Performance | CPU 40.81 ms/frame (24.6 fps, worst 100.00 ms over 123 frames), RSS 741 MiB |
| 2026-09-20 14:50:49.499 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 40.8107 [ms] |
| 2026-09-20 14:50:49.501 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:50:49.503 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 24.5639 [fps] |
| 2026-09-20 14:50:49.505 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 123 [count] |
| 2026-09-20 14:50:49.509 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 740.719 [MiB] |
| 2026-09-20 14:50:49.518 | INFO | GpuTiming | GPU 61.11 ms total | cull 0.07 · raster 3.92 · HiZ 0.03 · resolve 0.54 · ReSTIR 56.55 · shadow 0.00 · post 0.56 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:50:49.520 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 61.1076 [ms] |
| 2026-09-20 14:50:49.522 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.069856 [ms] |
| 2026-09-20 14:50:49.523 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.92054 [ms] |
| 2026-09-20 14:50:49.525 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.034816 [ms] |
| 2026-09-20 14:50:49.527 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.536288 [ms] |
| 2026-09-20 14:50:49.530 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 56.5461 [ms] |
| 2026-09-20 14:50:49.534 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:50:49.537 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.560928 [ms] |
| 2026-09-20 14:50:49.539 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:50:49.540 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:50:49.542 | INFO | Visibility | Clusters 2046 tested -> 888 frustum, 888 cone, 888 visible | draws 883+5, 99326 triangles |
| 2026-09-20 14:50:49.544 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:50:49.549 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 888 [count] |
| 2026-09-20 14:50:49.551 | INFO | TelemetryMetrics | Measurement: ClustersCone = 888 [count] |
| 2026-09-20 14:50:49.552 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 888 [count] |
| 2026-09-20 14:50:49.554 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 99326 [count] |
| 2026-09-20 14:50:49.556 | INFO | TelemetryMetrics | Measurement: DrawCalls = 888 [count] |
| 2026-09-20 14:50:49.557 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-20 14:50:49.559 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:50:49.560 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:50:49.566 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:50:49.568 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:50:49.571 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:50:49.572 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:50:49.575 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.5353 [percent] |
| 2026-09-20 14:50:54.582 | INFO | Performance | CPU 51.83 ms/frame (19.5 fps, worst 100.00 ms over 97 frames), RSS 741 MiB |
| 2026-09-20 14:50:54.584 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 51.8339 [ms] |
| 2026-09-20 14:50:54.589 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:50:54.591 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 19.5383 [fps] |
| 2026-09-20 14:50:54.594 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 97 [count] |
| 2026-09-20 14:50:54.600 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 740.719 [MiB] |
| 2026-09-20 14:50:54.603 | INFO | GpuTiming | GPU 40.83 ms total | cull 0.08 · raster 5.32 · HiZ 0.04 · resolve 0.40 · ReSTIR 35.00 · shadow 0.00 · post 0.53 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:50:54.604 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 40.8346 [ms] |
| 2026-09-20 14:50:54.607 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.077184 [ms] |
| 2026-09-20 14:50:54.608 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 5.32237 [ms] |
| 2026-09-20 14:50:54.613 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.035616 [ms] |
| 2026-09-20 14:50:54.615 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.401792 [ms] |
| 2026-09-20 14:50:54.616 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 34.9977 [ms] |
| 2026-09-20 14:50:54.617 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:50:54.618 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.526207 [ms] |
| 2026-09-20 14:50:54.619 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:50:54.620 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:50:54.622 | INFO | Visibility | Clusters 2046 tested -> 1210 frustum, 1210 cone, 1210 visible | draws 1210+0, 135722 triangles |
| 2026-09-20 14:50:54.623 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:50:54.624 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1210 [count] |
| 2026-09-20 14:50:54.631 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1210 [count] |
| 2026-09-20 14:50:54.632 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1210 [count] |
| 2026-09-20 14:50:54.634 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 135722 [count] |
| 2026-09-20 14:50:54.635 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1210 [count] |
| 2026-09-20 14:50:54.637 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 86% of GPU frame |
| 2026-09-20 14:50:54.638 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:50:54.643 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:50:54.645 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:50:54.647 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:50:54.649 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:50:54.650 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-20 14:50:54.651 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 85.7059 [percent] |
| 2026-09-20 14:50:59.603 | INFO | Performance | CPU 36.52 ms/frame (27.1 fps, worst 100.00 ms over 138 frames), RSS 778 MiB |
| 2026-09-20 14:50:59.605 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 36.5183 [ms] |
| 2026-09-20 14:50:59.608 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:50:59.610 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 27.0924 [fps] |
| 2026-09-20 14:50:59.612 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 138 [count] |
| 2026-09-20 14:50:59.614 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 778.363 [MiB] |
| 2026-09-20 14:50:59.616 | INFO | GpuTiming | GPU 27.77 ms total | cull 0.09 · raster 7.09 · HiZ 0.03 · resolve 0.40 · ReSTIR 20.15 · shadow 0.00 · post 0.57 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:50:59.622 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 27.773 [ms] |
| 2026-09-20 14:50:59.624 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.094144 [ms] |
| 2026-09-20 14:50:59.625 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.09382 [ms] |
| 2026-09-20 14:50:59.627 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.034816 [ms] |
| 2026-09-20 14:50:59.629 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.400928 [ms] |
| 2026-09-20 14:50:59.632 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 20.1493 [ms] |
| 2026-09-20 14:50:59.638 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:50:59.640 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.569057 [ms] |
| 2026-09-20 14:50:59.642 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:50:59.644 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:50:59.646 | INFO | Visibility | Clusters 2046 tested -> 1425 frustum, 1425 cone, 1424 visible | draws 1424+0, 160020 triangles |
| 2026-09-20 14:50:59.652 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:50:59.654 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1425 [count] |
| 2026-09-20 14:50:59.656 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1425 [count] |
| 2026-09-20 14:50:59.658 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1424 [count] |
| 2026-09-20 14:50:59.662 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 160020 [count] |
| 2026-09-20 14:50:59.669 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1424 [count] |
| 2026-09-20 14:50:59.670 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 73% of GPU frame |
| 2026-09-20 14:50:59.672 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:50:59.674 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:50:59.676 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:50:59.677 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:50:59.683 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:50:59.685 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-20 14:50:59.687 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 72.5499 [percent] |
| 2026-09-20 14:51:04.623 | INFO | Performance | CPU 25.32 ms/frame (44.3 fps, worst 95.90 ms over 198 frames), RSS 741 MiB |
| 2026-09-20 14:51:04.625 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 25.317 [ms] |
| 2026-09-20 14:51:04.628 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 95.9036 [ms] |
| 2026-09-20 14:51:04.629 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 44.2601 [fps] |
| 2026-09-20 14:51:04.631 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 198 [count] |
| 2026-09-20 14:51:04.633 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 741.059 [MiB] |
| 2026-09-20 14:51:04.635 | INFO | GpuTiming | GPU 16.13 ms total | cull 0.13 · raster 6.34 · HiZ 0.04 · resolve 0.38 · ReSTIR 9.24 · shadow 0.00 · post 0.51 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:51:04.636 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 16.1303 [ms] |
| 2026-09-20 14:51:04.638 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.128384 [ms] |
| 2026-09-20 14:51:04.640 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 6.34125 [ms] |
| 2026-09-20 14:51:04.641 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.037216 [ms] |
| 2026-09-20 14:51:04.643 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.38048 [ms] |
| 2026-09-20 14:51:04.644 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 9.24294 [ms] |
| 2026-09-20 14:51:04.651 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:51:04.653 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.505984 [ms] |
| 2026-09-20 14:51:04.655 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:51:04.656 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:51:04.658 | INFO | Visibility | Clusters 2046 tested -> 1425 frustum, 1425 cone, 1424 visible | draws 1424+0, 160020 triangles |
| 2026-09-20 14:51:04.660 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:51:04.661 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1425 [count] |
| 2026-09-20 14:51:04.666 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1425 [count] |
| 2026-09-20 14:51:04.668 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1424 [count] |
| 2026-09-20 14:51:04.669 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 160020 [count] |
| 2026-09-20 14:51:04.671 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1424 [count] |
| 2026-09-20 14:51:04.672 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 57% of GPU frame |
| 2026-09-20 14:51:04.674 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:51:04.675 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:51:04.677 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:51:04.681 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:51:04.684 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:51:04.685 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-20 14:51:04.687 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 57.3018 [percent] |
| 2026-09-20 14:51:09.694 | INFO | Performance | CPU 25.86 ms/frame (32.7 fps, worst 83.74 ms over 195 frames), RSS 741 MiB |
| 2026-09-20 14:51:09.695 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 25.858 [ms] |
| 2026-09-20 14:51:09.698 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 83.7399 [ms] |
| 2026-09-20 14:51:09.700 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 32.6679 [fps] |
| 2026-09-20 14:51:09.702 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 195 [count] |
| 2026-09-20 14:51:09.705 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 741.059 [MiB] |
| 2026-09-20 14:51:09.706 | INFO | GpuTiming | GPU 44.41 ms total | cull 0.12 · raster 8.50 · HiZ 0.04 · resolve 0.90 · ReSTIR 34.85 · shadow 0.00 · post 0.60 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:51:09.709 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 44.4092 [ms] |
| 2026-09-20 14:51:09.715 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.118304 [ms] |
| 2026-09-20 14:51:09.719 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.50157 [ms] |
| 2026-09-20 14:51:09.721 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.037312 [ms] |
| 2026-09-20 14:51:09.723 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.903456 [ms] |
| 2026-09-20 14:51:09.725 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 34.8486 [ms] |
| 2026-09-20 14:51:09.726 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:51:09.731 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.60173 [ms] |
| 2026-09-20 14:51:09.735 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:51:09.737 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:51:09.740 | INFO | Visibility | Clusters 2046 tested -> 1949 frustum, 1949 cone, 1949 visible | draws 1948+1, 219364 triangles |
| 2026-09-20 14:51:09.741 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:51:09.745 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1949 [count] |
| 2026-09-20 14:51:09.747 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1949 [count] |
| 2026-09-20 14:51:09.750 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1949 [count] |
| 2026-09-20 14:51:09.752 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 219364 [count] |
| 2026-09-20 14:51:09.754 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1949 [count] |
| 2026-09-20 14:51:09.756 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 78% of GPU frame |
| 2026-09-20 14:51:09.760 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:51:09.762 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:51:09.766 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:51:09.768 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:51:09.770 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:51:09.772 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:51:09.778 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 78.4715 [percent] |
| 2026-09-20 14:51:14.730 | INFO | Performance | CPU 34.57 ms/frame (28.9 fps, worst 100.00 ms over 145 frames), RSS 741 MiB |
| 2026-09-20 14:51:14.732 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 34.5701 [ms] |
| 2026-09-20 14:51:14.734 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:51:14.736 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 28.8961 [fps] |
| 2026-09-20 14:51:14.737 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 145 [count] |
| 2026-09-20 14:51:14.739 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 741.125 [MiB] |
| 2026-09-20 14:51:14.741 | INFO | GpuTiming | GPU 36.59 ms total | cull 0.09 · raster 9.91 · HiZ 0.04 · resolve 0.44 · ReSTIR 26.11 · shadow 0.00 · post 0.41 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:51:14.743 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 36.5926 [ms] |
| 2026-09-20 14:51:14.745 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.09472 [ms] |
| 2026-09-20 14:51:14.746 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.90986 [ms] |
| 2026-09-20 14:51:14.748 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.035232 [ms] |
| 2026-09-20 14:51:14.748 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.438272 [ms] |
| 2026-09-20 14:51:14.750 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 26.1145 [ms] |
| 2026-09-20 14:51:14.752 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:51:14.753 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.4112 [ms] |
| 2026-09-20 14:51:14.754 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:51:14.758 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:51:14.761 | INFO | Visibility | Clusters 2046 tested -> 2037 frustum, 2037 cone, 2037 visible | draws 2037+0, 228644 triangles |
| 2026-09-20 14:51:14.762 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:51:14.763 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 2037 [count] |
| 2026-09-20 14:51:14.764 | INFO | TelemetryMetrics | Measurement: ClustersCone = 2037 [count] |
| 2026-09-20 14:51:14.765 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 2037 [count] |
| 2026-09-20 14:51:14.766 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 228644 [count] |
| 2026-09-20 14:51:14.767 | INFO | TelemetryMetrics | Measurement: DrawCalls = 2037 [count] |
| 2026-09-20 14:51:14.768 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 71% of GPU frame |
| 2026-09-20 14:51:14.769 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:51:14.770 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:51:14.773 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:51:14.774 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:51:14.775 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:51:14.777 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:51:14.777 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 71.3656 [percent] |
| 2026-09-20 14:51:19.727 | INFO | Performance | CPU 28.96 ms/frame (37.5 fps, worst 86.51 ms over 173 frames), RSS 741 MiB |
| 2026-09-20 14:51:19.729 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 28.9602 [ms] |
| 2026-09-20 14:51:19.732 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 86.5113 [ms] |
| 2026-09-20 14:51:19.733 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 37.4702 [fps] |
| 2026-09-20 14:51:19.737 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 173 [count] |
| 2026-09-20 14:51:19.739 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 741.16 [MiB] |
| 2026-09-20 14:51:19.740 | INFO | GpuTiming | GPU 24.01 ms total | cull 0.13 · raster 10.16 · HiZ 0.04 · resolve 0.42 · ReSTIR 13.27 · shadow 0.00 · post 0.29 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:51:19.742 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 24.014 [ms] |
| 2026-09-20 14:51:19.744 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.126016 [ms] |
| 2026-09-20 14:51:19.749 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 10.1608 [ms] |
| 2026-09-20 14:51:19.751 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.038912 [ms] |
| 2026-09-20 14:51:19.753 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.415008 [ms] |
| 2026-09-20 14:51:19.755 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 13.2733 [ms] |
| 2026-09-20 14:51:19.756 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:51:19.757 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.287232 [ms] |
| 2026-09-20 14:51:19.758 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:51:19.762 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:51:19.763 | INFO | Visibility | Clusters 2046 tested -> 2037 frustum, 2037 cone, 2037 visible | draws 2037+0, 228644 triangles |
| 2026-09-20 14:51:19.764 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:51:19.766 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 2037 [count] |
| 2026-09-20 14:51:19.767 | INFO | TelemetryMetrics | Measurement: ClustersCone = 2037 [count] |
| 2026-09-20 14:51:19.770 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 2037 [count] |
| 2026-09-20 14:51:19.771 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 228644 [count] |
| 2026-09-20 14:51:19.773 | INFO | TelemetryMetrics | Measurement: DrawCalls = 2037 [count] |
| 2026-09-20 14:51:19.774 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 55% of GPU frame |
| 2026-09-20 14:51:19.779 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:51:19.780 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:51:19.781 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:51:19.782 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:51:19.783 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:51:19.784 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-20 14:51:19.786 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 55.2732 [percent] |
| 2026-09-20 14:51:24.761 | INFO | Performance | CPU 25.20 ms/frame (40.4 fps, worst 84.61 ms over 199 frames), RSS 741 MiB |
| 2026-09-20 14:51:24.763 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 25.1968 [ms] |
| 2026-09-20 14:51:24.765 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 84.6136 [ms] |
| 2026-09-20 14:51:24.767 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 40.3912 [fps] |
| 2026-09-20 14:51:24.768 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 199 [count] |
| 2026-09-20 14:51:24.772 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 741.125 [MiB] |
| 2026-09-20 14:51:24.777 | INFO | GpuTiming | GPU 23.31 ms total | cull 0.10 · raster 9.22 · HiZ 0.04 · resolve 0.39 · ReSTIR 13.56 · shadow 0.00 · post 0.28 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:51:24.779 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 23.3115 [ms] |
| 2026-09-20 14:51:24.783 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.101728 [ms] |
| 2026-09-20 14:51:24.785 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.22426 [ms] |
| 2026-09-20 14:51:24.790 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.03664 [ms] |
| 2026-09-20 14:51:24.792 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.387072 [ms] |
| 2026-09-20 14:51:24.794 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 13.5618 [ms] |
| 2026-09-20 14:51:24.795 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:51:24.797 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.280384 [ms] |
| 2026-09-20 14:51:24.798 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:51:24.799 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:51:24.801 | INFO | Visibility | Clusters 2046 tested -> 2037 frustum, 2037 cone, 2037 visible | draws 2037+0, 228644 triangles |
| 2026-09-20 14:51:24.802 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:51:24.808 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 2037 [count] |
| 2026-09-20 14:51:24.810 | INFO | TelemetryMetrics | Measurement: ClustersCone = 2037 [count] |
| 2026-09-20 14:51:24.812 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 2037 [count] |
| 2026-09-20 14:51:24.814 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 228644 [count] |
| 2026-09-20 14:51:24.815 | INFO | TelemetryMetrics | Measurement: DrawCalls = 2037 [count] |
| 2026-09-20 14:51:24.817 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 58% of GPU frame |
| 2026-09-20 14:51:24.819 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:51:24.823 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:51:24.825 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:51:24.826 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:51:24.828 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:51:24.829 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:51:24.831 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 58.1764 [percent] |
| 2026-09-20 14:51:29.774 | INFO | Performance | CPU 30.53 ms/frame (30.8 fps, worst 100.00 ms over 164 frames), RSS 741 MiB |
| 2026-09-20 14:51:29.776 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 30.5294 [ms] |
| 2026-09-20 14:51:29.779 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-20 14:51:29.780 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 30.816 [fps] |
| 2026-09-20 14:51:29.782 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 164 [count] |
| 2026-09-20 14:51:29.785 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 741.188 [MiB] |
| 2026-09-20 14:51:29.786 | INFO | GpuTiming | GPU 33.92 ms total | cull 0.08 · raster 9.54 · HiZ 0.04 · resolve 0.49 · ReSTIR 23.77 · shadow 0.00 · post 0.40 · sky 0.00 · volume 0.00 |
| 2026-09-20 14:51:29.788 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 33.9232 [ms] |
| 2026-09-20 14:51:29.790 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.084288 [ms] |
| 2026-09-20 14:51:29.791 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.54029 [ms] |
| 2026-09-20 14:51:29.797 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.036512 [ms] |
| 2026-09-20 14:51:29.799 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.489472 [ms] |
| 2026-09-20 14:51:29.801 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 23.7726 [ms] |
| 2026-09-20 14:51:29.803 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-20 14:51:29.804 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.397024 [ms] |
| 2026-09-20 14:51:29.807 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-20 14:51:29.813 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-20 14:51:29.816 | INFO | Visibility | Clusters 2046 tested -> 2030 frustum, 2030 cone, 2030 visible | draws 2030+0, 227724 triangles |
| 2026-09-20 14:51:29.818 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-20 14:51:29.820 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 2030 [count] |
| 2026-09-20 14:51:29.822 | INFO | TelemetryMetrics | Measurement: ClustersCone = 2030 [count] |
| 2026-09-20 14:51:29.827 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 2030 [count] |
| 2026-09-20 14:51:29.829 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 227724 [count] |
| 2026-09-20 14:51:29.831 | INFO | TelemetryMetrics | Measurement: DrawCalls = 2030 [count] |
| 2026-09-20 14:51:29.832 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 70% of GPU frame |
| 2026-09-20 14:51:29.834 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-20 14:51:29.835 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-20 14:51:29.836 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-20 14:51:29.837 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-20 14:51:29.838 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-20 14:51:29.842 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-20 14:51:29.843 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 70.0778 [percent] |
| 2026-09-20 14:51:34.289 | INFO | Shutdown | Render loop exited cleanly. |
