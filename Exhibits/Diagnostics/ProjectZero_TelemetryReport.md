# ProjectZero_TelemetryReport — Telemetry Log

| Timestamp | Severity | Category | Record Description |
|:---|:---:|:---|:---|
| 2026-09-19 17:52:59.639 | INFO | Bootstrap | Project-Zero windowed ReSTIR renderer starting. |
| 2026-09-19 17:53:00.280 | INFO | Scene | Showcase: 229506 triangles, 499 instances, 2045 clusters, 241 materials, 3368 luminaires, bounds [-40.00 -40.00 0.00]..[40.00 40.00 6.00] m |
| 2026-09-19 17:53:01.360 | INFO | Textures | Textures: 6 resident (0 placeholder), 64.0 MB with mips, decoded in 1080 ms |
| 2026-09-19 17:53:01.361 | INFO | Materials | Materials: 241 descriptors -> 241 records, 241 slabs (limit 1, 0 folded), 499 placements, 0 cameras, 0 punctual lights |
| 2026-09-19 17:53:01.394 | INFO | Interface | Panel light Low: rgb (0.000 0.008 0.042) from 4 figures, 5% coverage, 3370 luminaires now. |
| 2026-09-19 17:53:02.803 | INFO | Traversal | CWBVH: 229508 triangles → 39627 nodes, 3095.9 KB nodes + 16137.3 KB leaves (85.8 B/tri), SAH 13.84, built in 1398.8 ms (spatial splits) |
| 2026-09-19 17:54:42.894 | INFO | Bootstrap | Window and Vulkan swapchain ready. |
| 2026-09-19 17:54:44.984 | INFO | Traversal | Two-level: 500 instances -> 500 BLASes over 229508 triangles, top level 598 nodes, shared blobs 2631.4 KB + 10758.2 KB, built in 650.6 ms |
| 2026-09-19 17:54:45.169 | INFO | Moons | 6 textures resident, moon slots 0..5. |
| 2026-09-19 17:54:45.171 | INFO | Stars | 9683 stars in 1024 cells uploaded to binding 23. |
| 2026-09-19 17:54:49.636 | INFO | Bootstrap | Entering render loop. |
| 2026-09-19 17:54:49.734 | INFO | Interface | Director ready: TAB switches screens. The card carries 2 converted vector segments. |
| 2026-09-19 17:54:49.889 | INFO | Audio | Panel bound to audio: drag the progress bar to change the engine note. |
| 2026-09-19 17:54:49.890 | INFO | Interface | Spatial interface ready: 14 figures, depth test off. |
| 2026-09-19 17:54:49.925 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 7 taps. |
| 2026-09-19 17:54:55.428 | INFO | Performance | CPU 49.91 ms/frame (20.0 fps, worst 100.00 ms over 101 frames), RSS 844 MiB |
| 2026-09-19 17:54:55.429 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 49.914 [ms] |
| 2026-09-19 17:54:55.432 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:54:55.434 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 20.0345 [fps] |
| 2026-09-19 17:54:55.435 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 101 [count] |
| 2026-09-19 17:54:55.438 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 844.496 [MiB] |
| 2026-09-19 17:54:55.441 | INFO | GpuTiming | GPU 41.10 ms total | cull 0.08 · raster 7.29 · HiZ 0.05 · resolve 0.83 · ReSTIR 32.86 · shadow 0.00 · post 1.00 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:54:55.445 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 41.0973 [ms] |
| 2026-09-19 17:54:55.446 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.080736 [ms] |
| 2026-09-19 17:54:55.449 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.28752 [ms] |
| 2026-09-19 17:54:55.450 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045056 [ms] |
| 2026-09-19 17:54:55.454 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.826784 [ms] |
| 2026-09-19 17:54:55.458 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 32.8572 [ms] |
| 2026-09-19 17:54:55.459 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:54:55.461 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.00093 [ms] |
| 2026-09-19 17:54:55.463 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:54:55.467 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:54:55.468 | INFO | Visibility | Clusters 2046 tested -> 1579 frustum, 1579 cone, 1579 visible | draws 1579+0, 177668 triangles |
| 2026-09-19 17:54:55.472 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:54:55.474 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1579 [count] |
| 2026-09-19 17:54:55.475 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1579 [count] |
| 2026-09-19 17:54:55.477 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1579 [count] |
| 2026-09-19 17:54:55.478 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 177668 [count] |
| 2026-09-19 17:54:55.480 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1579 [count] |
| 2026-09-19 17:54:55.483 | INFO | Performance | CPU-BOUND or presenting-limited | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 80% of GPU frame |
| 2026-09-19 17:54:55.485 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:54:55.492 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:54:55.493 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:54:55.495 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:54:55.497 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:54:55.499 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 17:54:55.501 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 79.9498 [percent] |
| 2026-09-19 17:55:00.492 | INFO | Performance | CPU 46.50 ms/frame (21.7 fps, worst 100.00 ms over 108 frames), RSS 836 MiB |
| 2026-09-19 17:55:00.496 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 46.5008 [ms] |
| 2026-09-19 17:55:00.504 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:55:00.508 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 21.7056 [fps] |
| 2026-09-19 17:55:00.510 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 108 [count] |
| 2026-09-19 17:55:00.513 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 835.992 [MiB] |
| 2026-09-19 17:55:00.520 | INFO | GpuTiming | GPU 46.49 ms total | cull 0.08 · raster 7.10 · HiZ 0.05 · resolve 0.54 · ReSTIR 38.73 · shadow 0.00 · post 1.21 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:55:00.525 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 46.4853 [ms] |
| 2026-09-19 17:55:00.528 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.081952 [ms] |
| 2026-09-19 17:55:00.537 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.09648 [ms] |
| 2026-09-19 17:55:00.539 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.045792 [ms] |
| 2026-09-19 17:55:00.541 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.535936 [ms] |
| 2026-09-19 17:55:00.545 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 38.7252 [ms] |
| 2026-09-19 17:55:00.547 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:55:00.555 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.2137 [ms] |
| 2026-09-19 17:55:00.558 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:55:00.560 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:55:00.563 | INFO | Visibility | Clusters 2046 tested -> 1710 frustum, 1710 cone, 1705 visible | draws 1705+4, 192368 triangles |
| 2026-09-19 17:55:00.570 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:55:00.572 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1710 [count] |
| 2026-09-19 17:55:00.576 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1710 [count] |
| 2026-09-19 17:55:00.578 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1705 [count] |
| 2026-09-19 17:55:00.585 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 192368 [count] |
| 2026-09-19 17:55:00.587 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1709 [count] |
| 2026-09-19 17:55:00.591 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 83% of GPU frame |
| 2026-09-19 17:55:00.596 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:55:00.601 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:55:00.604 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:55:00.607 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:55:00.610 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:55:00.615 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:55:00.618 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 83.3062 [percent] |
| 2026-09-19 17:55:05.583 | INFO | Performance | CPU 50.20 ms/frame (20.4 fps, worst 100.00 ms over 100 frames), RSS 908 MiB |
| 2026-09-19 17:55:05.586 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 50.2017 [ms] |
| 2026-09-19 17:55:05.589 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:55:05.592 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 20.407 [fps] |
| 2026-09-19 17:55:05.595 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 100 [count] |
| 2026-09-19 17:55:05.596 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 907.547 [MiB] |
| 2026-09-19 17:55:05.599 | INFO | GpuTiming | GPU 50.84 ms total | cull 0.08 · raster 7.15 · HiZ 0.05 · resolve 1.03 · ReSTIR 42.52 · shadow 0.00 · post 0.99 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:55:05.602 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 50.8364 [ms] |
| 2026-09-19 17:55:05.606 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.084096 [ms] |
| 2026-09-19 17:55:05.608 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.152 [ms] |
| 2026-09-19 17:55:05.615 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.046688 [ms] |
| 2026-09-19 17:55:05.618 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.03424 [ms] |
| 2026-09-19 17:55:05.621 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 42.5194 [ms] |
| 2026-09-19 17:55:05.623 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:55:05.627 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.994465 [ms] |
| 2026-09-19 17:55:05.630 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:55:05.632 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:55:05.635 | INFO | Visibility | Clusters 2046 tested -> 1453 frustum, 1453 cone, 1453 visible | draws 1453+0, 163864 triangles |
| 2026-09-19 17:55:05.639 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:55:05.644 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1453 [count] |
| 2026-09-19 17:55:05.646 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1453 [count] |
| 2026-09-19 17:55:05.648 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1453 [count] |
| 2026-09-19 17:55:05.651 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 163864 [count] |
| 2026-09-19 17:55:05.654 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1453 [count] |
| 2026-09-19 17:55:05.660 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 84% of GPU frame |
| 2026-09-19 17:55:05.665 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:55:05.671 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:55:05.677 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:55:05.679 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:55:05.682 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:55:05.684 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:55:05.687 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 83.6396 [percent] |
| 2026-09-19 17:55:10.646 | INFO | Performance | CPU 45.84 ms/frame (21.4 fps, worst 100.00 ms over 110 frames), RSS 937 MiB |
| 2026-09-19 17:55:10.648 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 45.8355 [ms] |
| 2026-09-19 17:55:10.650 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:55:10.653 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 21.3529 [fps] |
| 2026-09-19 17:55:10.655 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 110 [count] |
| 2026-09-19 17:55:10.660 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 937.09 [MiB] |
| 2026-09-19 17:55:10.662 | INFO | GpuTiming | GPU 44.68 ms total | cull 0.08 · raster 7.41 · HiZ 0.05 · resolve 0.46 · ReSTIR 36.68 · shadow 0.00 · post 1.09 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:55:10.666 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 44.677 [ms] |
| 2026-09-19 17:55:10.669 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.079808 [ms] |
| 2026-09-19 17:55:10.672 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.41341 [ms] |
| 2026-09-19 17:55:10.677 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047648 [ms] |
| 2026-09-19 17:55:10.679 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.459744 [ms] |
| 2026-09-19 17:55:10.681 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 36.6764 [ms] |
| 2026-09-19 17:55:10.683 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:55:10.684 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.09283 [ms] |
| 2026-09-19 17:55:10.687 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:55:10.692 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:55:10.694 | INFO | Visibility | Clusters 2046 tested -> 1518 frustum, 1518 cone, 1505 visible | draws 1495+10, 170672 triangles |
| 2026-09-19 17:55:10.696 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:55:10.698 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1518 [count] |
| 2026-09-19 17:55:10.701 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1518 [count] |
| 2026-09-19 17:55:10.704 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1505 [count] |
| 2026-09-19 17:55:10.710 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 170672 [count] |
| 2026-09-19 17:55:10.711 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1505 [count] |
| 2026-09-19 17:55:10.714 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 82% of GPU frame |
| 2026-09-19 17:55:10.717 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:55:10.724 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:55:10.726 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:55:10.728 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:55:10.730 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:55:10.732 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:55:10.734 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 82.0923 [percent] |
| 2026-09-19 17:55:15.897 | INFO | Performance | CPU 50.03 ms/frame (20.5 fps, worst 100.00 ms over 100 frames), RSS 956 MiB |
| 2026-09-19 17:55:15.898 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 50.0348 [ms] |
| 2026-09-19 17:55:15.900 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:55:15.902 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 20.4665 [fps] |
| 2026-09-19 17:55:15.904 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 100 [count] |
| 2026-09-19 17:55:15.905 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 955.512 [MiB] |
| 2026-09-19 17:55:15.906 | INFO | GpuTiming | GPU 26.35 ms total | cull 0.07 · raster 0.68 · HiZ 0.04 · resolve 0.12 · ReSTIR 25.43 · shadow 0.00 · post 0.77 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:55:15.909 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 26.3487 [ms] |
| 2026-09-19 17:55:15.911 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.068608 [ms] |
| 2026-09-19 17:55:15.912 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 0.680672 [ms] |
| 2026-09-19 17:55:15.914 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.043008 [ms] |
| 2026-09-19 17:55:15.916 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.124928 [ms] |
| 2026-09-19 17:55:15.918 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 25.4315 [ms] |
| 2026-09-19 17:55:15.919 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:55:15.921 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.76656 [ms] |
| 2026-09-19 17:55:15.926 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:55:15.928 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:55:15.929 | INFO | Visibility | Clusters 2046 tested -> 143 frustum, 143 cone, 143 visible | draws 143+0, 15360 triangles |
| 2026-09-19 17:55:15.931 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:55:15.934 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 143 [count] |
| 2026-09-19 17:55:15.936 | INFO | TelemetryMetrics | Measurement: ClustersCone = 143 [count] |
| 2026-09-19 17:55:15.942 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 143 [count] |
| 2026-09-19 17:55:15.944 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 15360 [count] |
| 2026-09-19 17:55:15.946 | INFO | TelemetryMetrics | Measurement: DrawCalls = 143 [count] |
| 2026-09-19 17:55:15.947 | INFO | Performance | CPU-BOUND or presenting-limited | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 97% of GPU frame |
| 2026-09-19 17:55:15.950 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:55:15.952 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:55:15.958 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:55:15.960 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:55:15.962 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:55:15.964 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 17:55:15.966 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 96.5189 [percent] |
| 2026-09-19 17:55:20.921 | INFO | Performance | CPU 30.95 ms/frame (30.7 fps, worst 98.14 ms over 162 frames), RSS 959 MiB |
| 2026-09-19 17:55:20.922 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 30.9534 [ms] |
| 2026-09-19 17:55:20.924 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 98.1448 [ms] |
| 2026-09-19 17:55:20.926 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 30.6907 [fps] |
| 2026-09-19 17:55:20.928 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 162 [count] |
| 2026-09-19 17:55:20.930 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 959.062 [MiB] |
| 2026-09-19 17:55:20.931 | INFO | GpuTiming | GPU 35.41 ms total | cull 0.09 · raster 8.67 · HiZ 0.04 · resolve 0.54 · ReSTIR 26.06 · shadow 0.00 · post 1.05 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:55:20.935 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 35.4059 [ms] |
| 2026-09-19 17:55:20.936 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.092992 [ms] |
| 2026-09-19 17:55:20.938 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.66851 [ms] |
| 2026-09-19 17:55:20.941 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.044832 [ms] |
| 2026-09-19 17:55:20.945 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.536224 [ms] |
| 2026-09-19 17:55:20.947 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 26.0633 [ms] |
| 2026-09-19 17:55:20.948 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:55:20.950 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.04838 [ms] |
| 2026-09-19 17:55:20.951 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:55:20.953 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:55:20.955 | INFO | Visibility | Clusters 2046 tested -> 1767 frustum, 1767 cone, 1751 visible | draws 1756+1, 197916 triangles |
| 2026-09-19 17:55:20.957 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:55:20.962 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1767 [count] |
| 2026-09-19 17:55:20.963 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1767 [count] |
| 2026-09-19 17:55:20.965 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1751 [count] |
| 2026-09-19 17:55:20.967 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 197916 [count] |
| 2026-09-19 17:55:20.968 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1757 [count] |
| 2026-09-19 17:55:20.970 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 74% of GPU frame |
| 2026-09-19 17:55:20.973 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:55:20.977 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:55:20.979 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:55:20.981 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:55:20.982 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:55:20.984 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:55:20.986 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 73.613 [percent] |
| 2026-09-19 17:55:26.232 | INFO | Performance | CPU 44.32 ms/frame (22.8 fps, worst 100.00 ms over 114 frames), RSS 1096 MiB |
| 2026-09-19 17:55:26.233 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 44.3198 [ms] |
| 2026-09-19 17:55:26.235 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:55:26.237 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 22.7573 [fps] |
| 2026-09-19 17:55:26.239 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 114 [count] |
| 2026-09-19 17:55:26.241 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1096.24 [MiB] |
| 2026-09-19 17:55:26.245 | INFO | GpuTiming | GPU 56.52 ms total | cull 0.07 · raster 5.64 · HiZ 0.05 · resolve 1.26 · ReSTIR 49.50 · shadow 0.00 · post 1.09 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:55:26.250 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 56.5169 [ms] |
| 2026-09-19 17:55:26.251 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.074528 [ms] |
| 2026-09-19 17:55:26.254 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 5.64192 [ms] |
| 2026-09-19 17:55:26.255 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 17:55:26.260 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.25747 [ms] |
| 2026-09-19 17:55:26.262 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 49.4958 [ms] |
| 2026-09-19 17:55:26.265 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:55:26.266 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.09386 [ms] |
| 2026-09-19 17:55:26.269 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:55:26.270 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:55:26.272 | INFO | Visibility | Clusters 2046 tested -> 1323 frustum, 1323 cone, 1323 visible | draws 1321+2, 148768 triangles |
| 2026-09-19 17:55:26.279 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:55:26.281 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1323 [count] |
| 2026-09-19 17:55:26.283 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1323 [count] |
| 2026-09-19 17:55:26.285 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1323 [count] |
| 2026-09-19 17:55:26.286 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 148768 [count] |
| 2026-09-19 17:55:26.288 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1323 [count] |
| 2026-09-19 17:55:26.293 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 88% of GPU frame |
| 2026-09-19 17:55:26.296 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:55:26.298 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:55:26.300 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:55:26.301 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:55:26.303 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:55:26.304 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:55:26.312 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 87.5771 [percent] |
| 2026-09-19 17:55:31.595 | INFO | Performance | CPU 57.95 ms/frame (18.5 fps, worst 100.00 ms over 87 frames), RSS 794 MiB |
| 2026-09-19 17:55:31.596 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 57.9478 [ms] |
| 2026-09-19 17:55:31.597 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:55:31.598 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 18.5247 [fps] |
| 2026-09-19 17:55:31.599 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 87 [count] |
| 2026-09-19 17:55:31.600 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 794.27 [MiB] |
| 2026-09-19 17:55:31.600 | INFO | GpuTiming | GPU 66.21 ms total | cull 0.08 · raster 9.73 · HiZ 0.05 · resolve 2.18 · ReSTIR 54.17 · shadow 0.00 · post 1.07 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:55:31.602 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 66.2144 [ms] |
| 2026-09-19 17:55:31.603 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.083296 [ms] |
| 2026-09-19 17:55:31.604 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.73267 [ms] |
| 2026-09-19 17:55:31.605 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.0472 [ms] |
| 2026-09-19 17:55:31.606 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 2.18275 [ms] |
| 2026-09-19 17:55:31.607 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 54.1685 [ms] |
| 2026-09-19 17:55:31.608 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:55:31.608 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.06647 [ms] |
| 2026-09-19 17:55:31.609 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:55:31.613 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:55:31.614 | INFO | Visibility | Clusters 2046 tested -> 1994 frustum, 1994 cone, 1994 visible | draws 1994+0, 224624 triangles |
| 2026-09-19 17:55:31.617 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:55:31.619 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1994 [count] |
| 2026-09-19 17:55:31.621 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1994 [count] |
| 2026-09-19 17:55:31.622 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1994 [count] |
| 2026-09-19 17:55:31.625 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 224624 [count] |
| 2026-09-19 17:55:31.626 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1994 [count] |
| 2026-09-19 17:55:31.630 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 82% of GPU frame |
| 2026-09-19 17:55:31.633 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:55:31.634 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:55:31.635 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:55:31.638 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:55:31.639 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:55:31.641 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:55:31.646 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 81.8077 [percent] |
| 2026-09-19 17:55:36.776 | INFO | Performance | CPU 57.98 ms/frame (17.2 fps, worst 100.00 ms over 87 frames), RSS 803 MiB |
| 2026-09-19 17:55:36.777 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 57.9809 [ms] |
| 2026-09-19 17:55:36.780 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:55:36.781 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 17.2314 [fps] |
| 2026-09-19 17:55:36.783 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 87 [count] |
| 2026-09-19 17:55:36.784 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 802.648 [MiB] |
| 2026-09-19 17:55:36.787 | INFO | GpuTiming | GPU 57.07 ms total | cull 0.08 · raster 6.17 · HiZ 0.05 · resolve 1.14 · ReSTIR 49.62 · shadow 0.00 · post 1.07 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:55:36.790 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 57.0687 [ms] |
| 2026-09-19 17:55:36.792 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.082816 [ms] |
| 2026-09-19 17:55:36.794 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 6.17344 [ms] |
| 2026-09-19 17:55:36.798 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 17:55:36.799 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.14474 [ms] |
| 2026-09-19 17:55:36.803 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 49.6206 [ms] |
| 2026-09-19 17:55:36.805 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:55:36.807 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.07158 [ms] |
| 2026-09-19 17:55:36.809 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:55:36.810 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:55:36.814 | INFO | Visibility | Clusters 2046 tested -> 1060 frustum, 1060 cone, 1060 visible | draws 1027+33, 119106 triangles |
| 2026-09-19 17:55:36.815 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:55:36.819 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1060 [count] |
| 2026-09-19 17:55:36.821 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1060 [count] |
| 2026-09-19 17:55:36.824 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1060 [count] |
| 2026-09-19 17:55:36.825 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 119106 [count] |
| 2026-09-19 17:55:36.827 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1060 [count] |
| 2026-09-19 17:55:36.829 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 87% of GPU frame |
| 2026-09-19 17:55:36.832 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:55:36.835 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:55:36.839 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:55:36.841 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:55:36.842 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:55:36.844 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:55:36.845 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 86.9489 [percent] |
| 2026-09-19 17:55:42.002 | INFO | Performance | CPU 53.47 ms/frame (18.4 fps, worst 100.00 ms over 94 frames), RSS 810 MiB |
| 2026-09-19 17:55:42.003 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 53.4737 [ms] |
| 2026-09-19 17:55:42.006 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:55:42.008 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 18.4183 [fps] |
| 2026-09-19 17:55:42.010 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 94 [count] |
| 2026-09-19 17:55:42.012 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 810.316 [MiB] |
| 2026-09-19 17:55:42.016 | INFO | GpuTiming | GPU 63.63 ms total | cull 0.07 · raster 4.78 · HiZ 0.05 · resolve 0.61 · ReSTIR 58.13 · shadow 0.00 · post 0.84 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:55:42.021 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 63.6284 [ms] |
| 2026-09-19 17:55:42.025 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.073664 [ms] |
| 2026-09-19 17:55:42.026 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 4.77533 [ms] |
| 2026-09-19 17:55:42.029 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.04688 [ms] |
| 2026-09-19 17:55:42.034 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.605952 [ms] |
| 2026-09-19 17:55:42.038 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 58.1266 [ms] |
| 2026-09-19 17:55:42.040 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:55:42.042 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.837025 [ms] |
| 2026-09-19 17:55:42.043 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:55:42.045 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:55:42.048 | INFO | Visibility | Clusters 2046 tested -> 1141 frustum, 1141 cone, 1141 visible | draws 1141+0, 128086 triangles |
| 2026-09-19 17:55:42.050 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:55:42.055 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1141 [count] |
| 2026-09-19 17:55:42.057 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1141 [count] |
| 2026-09-19 17:55:42.060 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1141 [count] |
| 2026-09-19 17:55:42.062 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 128086 [count] |
| 2026-09-19 17:55:42.064 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1141 [count] |
| 2026-09-19 17:55:42.066 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 91% of GPU frame |
| 2026-09-19 17:55:42.073 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:55:42.075 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:55:42.077 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:55:42.079 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:55:42.081 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:55:42.086 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:55:42.088 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 91.3532 [percent] |
| 2026-09-19 17:55:47.544 | INFO | Performance | CPU 57.84 ms/frame (17.7 fps, worst 100.00 ms over 87 frames), RSS 868 MiB |
| 2026-09-19 17:55:47.546 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 57.8434 [ms] |
| 2026-09-19 17:55:47.548 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:55:47.550 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 17.6924 [fps] |
| 2026-09-19 17:55:47.552 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 87 [count] |
| 2026-09-19 17:55:47.555 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 867.914 [MiB] |
| 2026-09-19 17:55:47.557 | INFO | GpuTiming | GPU 64.85 ms total | cull 0.08 · raster 4.01 · HiZ 0.05 · resolve 0.59 · ReSTIR 60.13 · shadow 0.00 · post 1.18 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:55:47.564 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 64.8516 [ms] |
| 2026-09-19 17:55:47.568 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.07632 [ms] |
| 2026-09-19 17:55:47.569 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 4.00989 [ms] |
| 2026-09-19 17:55:47.573 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047328 [ms] |
| 2026-09-19 17:55:47.578 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.589312 [ms] |
| 2026-09-19 17:55:47.580 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 60.1288 [ms] |
| 2026-09-19 17:55:47.584 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:55:47.586 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.18323 [ms] |
| 2026-09-19 17:55:47.594 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:55:47.597 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:55:47.603 | INFO | Visibility | Clusters 2046 tested -> 956 frustum, 956 cone, 956 visible | draws 936+20, 107448 triangles |
| 2026-09-19 17:55:47.609 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:55:47.612 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 956 [count] |
| 2026-09-19 17:55:47.615 | INFO | TelemetryMetrics | Measurement: ClustersCone = 956 [count] |
| 2026-09-19 17:55:47.619 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 956 [count] |
| 2026-09-19 17:55:47.621 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 107448 [count] |
| 2026-09-19 17:55:47.626 | INFO | TelemetryMetrics | Measurement: DrawCalls = 956 [count] |
| 2026-09-19 17:55:47.628 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 17:55:47.632 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:55:47.636 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:55:47.642 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:55:47.645 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:55:47.647 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:55:47.650 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:55:47.653 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.7175 [percent] |
| 2026-09-19 17:55:53.026 | INFO | Performance | CPU 57.22 ms/frame (17.1 fps, worst 100.00 ms over 88 frames), RSS 940 MiB |
| 2026-09-19 17:55:53.027 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 57.2174 [ms] |
| 2026-09-19 17:55:53.029 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:55:53.030 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 17.1046 [fps] |
| 2026-09-19 17:55:53.032 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 88 [count] |
| 2026-09-19 17:55:53.034 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 940.113 [MiB] |
| 2026-09-19 17:55:53.036 | INFO | GpuTiming | GPU 58.12 ms total | cull 0.07 · raster 2.75 · HiZ 0.05 · resolve 1.04 · ReSTIR 54.21 · shadow 0.00 · post 1.27 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:55:53.039 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 58.1176 [ms] |
| 2026-09-19 17:55:53.041 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.072608 [ms] |
| 2026-09-19 17:55:53.045 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 2.74877 [ms] |
| 2026-09-19 17:55:53.047 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.049056 [ms] |
| 2026-09-19 17:55:53.052 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.03834 [ms] |
| 2026-09-19 17:55:53.056 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 54.2089 [ms] |
| 2026-09-19 17:55:53.058 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:55:53.060 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.26756 [ms] |
| 2026-09-19 17:55:53.062 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:55:53.065 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:55:53.067 | INFO | Visibility | Clusters 2046 tested -> 668 frustum, 668 cone, 668 visible | draws 619+49, 75046 triangles |
| 2026-09-19 17:55:53.069 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:55:53.072 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 668 [count] |
| 2026-09-19 17:55:53.073 | INFO | TelemetryMetrics | Measurement: ClustersCone = 668 [count] |
| 2026-09-19 17:55:53.078 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 668 [count] |
| 2026-09-19 17:55:53.081 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 75046 [count] |
| 2026-09-19 17:55:53.083 | INFO | TelemetryMetrics | Measurement: DrawCalls = 668 [count] |
| 2026-09-19 17:55:53.084 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 17:55:53.088 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:55:53.090 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:55:53.091 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:55:53.093 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:55:53.097 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:55:53.100 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:55:53.101 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.2744 [percent] |
| 2026-09-19 17:55:58.381 | INFO | Performance | CPU 59.04 ms/frame (17.3 fps, worst 100.00 ms over 85 frames), RSS 791 MiB |
| 2026-09-19 17:55:58.382 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 59.0384 [ms] |
| 2026-09-19 17:55:58.384 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:55:58.387 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 17.331 [fps] |
| 2026-09-19 17:55:58.389 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 85 [count] |
| 2026-09-19 17:55:58.390 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 790.883 [MiB] |
| 2026-09-19 17:55:58.392 | INFO | GpuTiming | GPU 61.94 ms total | cull 0.07 · raster 3.23 · HiZ 0.05 · resolve 0.87 · ReSTIR 57.73 · shadow 0.00 · post 0.97 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:55:58.396 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 61.9384 [ms] |
| 2026-09-19 17:55:58.397 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.065344 [ms] |
| 2026-09-19 17:55:58.399 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.23005 [ms] |
| 2026-09-19 17:55:58.401 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047744 [ms] |
| 2026-09-19 17:55:58.406 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.868352 [ms] |
| 2026-09-19 17:55:58.408 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 57.7269 [ms] |
| 2026-09-19 17:55:58.409 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:55:58.414 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.966717 [ms] |
| 2026-09-19 17:55:58.415 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:55:58.419 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:55:58.421 | INFO | Visibility | Clusters 2046 tested -> 826 frustum, 826 cone, 826 visible | draws 826+0, 92652 triangles |
| 2026-09-19 17:55:58.422 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:55:58.424 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 826 [count] |
| 2026-09-19 17:55:58.428 | INFO | TelemetryMetrics | Measurement: ClustersCone = 826 [count] |
| 2026-09-19 17:55:58.431 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 826 [count] |
| 2026-09-19 17:55:58.433 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 92652 [count] |
| 2026-09-19 17:55:58.436 | INFO | TelemetryMetrics | Measurement: DrawCalls = 826 [count] |
| 2026-09-19 17:55:58.438 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 17:55:58.441 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:55:58.445 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:55:58.447 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:55:58.448 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:55:58.452 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:55:58.453 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:55:58.456 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.2005 [percent] |
| 2026-09-19 17:56:03.693 | INFO | Performance | CPU 60.27 ms/frame (16.5 fps, worst 100.00 ms over 83 frames), RSS 791 MiB |
| 2026-09-19 17:56:03.695 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 60.2707 [ms] |
| 2026-09-19 17:56:03.697 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:56:03.699 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.5451 [fps] |
| 2026-09-19 17:56:03.701 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 83 [count] |
| 2026-09-19 17:56:03.703 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 790.566 [MiB] |
| 2026-09-19 17:56:03.705 | INFO | GpuTiming | GPU 61.29 ms total | cull 0.06 · raster 3.50 · HiZ 0.05 · resolve 0.75 · ReSTIR 56.93 · shadow 0.00 · post 0.91 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:56:03.710 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 61.2913 [ms] |
| 2026-09-19 17:56:03.712 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.064832 [ms] |
| 2026-09-19 17:56:03.718 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.49936 [ms] |
| 2026-09-19 17:56:03.720 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 17:56:03.722 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.751904 [ms] |
| 2026-09-19 17:56:03.724 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 56.9281 [ms] |
| 2026-09-19 17:56:03.726 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:56:03.728 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.91341 [ms] |
| 2026-09-19 17:56:03.732 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:56:03.734 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:56:03.736 | INFO | Visibility | Clusters 2046 tested -> 826 frustum, 826 cone, 826 visible | draws 826+0, 92652 triangles |
| 2026-09-19 17:56:03.738 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:56:03.740 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 826 [count] |
| 2026-09-19 17:56:03.746 | INFO | TelemetryMetrics | Measurement: ClustersCone = 826 [count] |
| 2026-09-19 17:56:03.748 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 826 [count] |
| 2026-09-19 17:56:03.750 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 92652 [count] |
| 2026-09-19 17:56:03.751 | INFO | TelemetryMetrics | Measurement: DrawCalls = 826 [count] |
| 2026-09-19 17:56:03.752 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 17:56:03.756 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:56:03.757 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:56:03.759 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:56:03.763 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:56:03.765 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:56:03.767 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:56:03.768 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.8812 [percent] |
| 2026-09-19 17:56:09.196 | INFO | Performance | CPU 60.71 ms/frame (16.4 fps, worst 100.00 ms over 84 frames), RSS 800 MiB |
| 2026-09-19 17:56:09.198 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 60.7142 [ms] |
| 2026-09-19 17:56:09.200 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:56:09.202 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.4234 [fps] |
| 2026-09-19 17:56:09.204 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 84 [count] |
| 2026-09-19 17:56:09.206 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 799.941 [MiB] |
| 2026-09-19 17:56:09.212 | INFO | GpuTiming | GPU 62.37 ms total | cull 0.07 · raster 3.24 · HiZ 0.05 · resolve 0.80 · ReSTIR 58.22 · shadow 0.00 · post 0.89 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:56:09.214 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 62.3673 [ms] |
| 2026-09-19 17:56:09.217 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.067296 [ms] |
| 2026-09-19 17:56:09.218 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.23571 [ms] |
| 2026-09-19 17:56:09.220 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 17:56:09.221 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.800992 [ms] |
| 2026-09-19 17:56:09.223 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 58.2162 [ms] |
| 2026-09-19 17:56:09.227 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:56:09.229 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.889057 [ms] |
| 2026-09-19 17:56:09.231 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:56:09.233 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:56:09.234 | INFO | Visibility | Clusters 2046 tested -> 826 frustum, 826 cone, 826 visible | draws 826+0, 92652 triangles |
| 2026-09-19 17:56:09.236 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:56:09.237 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 826 [count] |
| 2026-09-19 17:56:09.239 | INFO | TelemetryMetrics | Measurement: ClustersCone = 826 [count] |
| 2026-09-19 17:56:09.244 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 826 [count] |
| 2026-09-19 17:56:09.247 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 92652 [count] |
| 2026-09-19 17:56:09.249 | INFO | TelemetryMetrics | Measurement: DrawCalls = 826 [count] |
| 2026-09-19 17:56:09.251 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 17:56:09.253 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:56:09.255 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:56:09.259 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:56:09.260 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:56:09.263 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:56:09.264 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:56:09.266 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.3441 [percent] |
| 2026-09-19 17:56:14.771 | INFO | Performance | CPU 58.77 ms/frame (16.7 fps, worst 100.00 ms over 86 frames), RSS 844 MiB |
| 2026-09-19 17:56:14.772 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 58.7651 [ms] |
| 2026-09-19 17:56:14.774 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:56:14.775 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.6774 [fps] |
| 2026-09-19 17:56:14.777 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 86 [count] |
| 2026-09-19 17:56:14.779 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 844.191 [MiB] |
| 2026-09-19 17:56:14.783 | INFO | GpuTiming | GPU 61.94 ms total | cull 0.07 · raster 3.55 · HiZ 0.05 · resolve 0.83 · ReSTIR 57.43 · shadow 0.00 · post 0.87 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:56:14.789 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 61.9356 [ms] |
| 2026-09-19 17:56:14.790 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.068512 [ms] |
| 2026-09-19 17:56:14.793 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.55414 [ms] |
| 2026-09-19 17:56:14.794 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047296 [ms] |
| 2026-09-19 17:56:14.796 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.834464 [ms] |
| 2026-09-19 17:56:14.801 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 57.4312 [ms] |
| 2026-09-19 17:56:14.802 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:56:14.805 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.865505 [ms] |
| 2026-09-19 17:56:14.806 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:56:14.808 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:56:14.810 | INFO | Visibility | Clusters 2046 tested -> 826 frustum, 826 cone, 826 visible | draws 826+0, 92652 triangles |
| 2026-09-19 17:56:14.812 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:56:14.813 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 826 [count] |
| 2026-09-19 17:56:14.817 | INFO | TelemetryMetrics | Measurement: ClustersCone = 826 [count] |
| 2026-09-19 17:56:14.819 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 826 [count] |
| 2026-09-19 17:56:14.821 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 92652 [count] |
| 2026-09-19 17:56:14.823 | INFO | TelemetryMetrics | Measurement: DrawCalls = 826 [count] |
| 2026-09-19 17:56:14.825 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 17:56:14.828 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:56:14.829 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:56:14.833 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:56:14.835 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:56:14.837 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:56:14.839 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:56:14.841 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 92.7273 [percent] |
| 2026-09-19 17:56:20.417 | INFO | Performance | CPU 63.05 ms/frame (16.2 fps, worst 100.00 ms over 80 frames), RSS 916 MiB |
| 2026-09-19 17:56:20.419 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 63.0489 [ms] |
| 2026-09-19 17:56:20.421 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:56:20.423 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.207 [fps] |
| 2026-09-19 17:56:20.424 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 80 [count] |
| 2026-09-19 17:56:20.427 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 915.887 [MiB] |
| 2026-09-19 17:56:20.429 | INFO | GpuTiming | GPU 64.44 ms total | cull 0.08 · raster 4.76 · HiZ 0.05 · resolve 1.32 · ReSTIR 58.24 · shadow 0.00 · post 1.34 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:56:20.432 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 64.4448 [ms] |
| 2026-09-19 17:56:20.434 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.079744 [ms] |
| 2026-09-19 17:56:20.436 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 4.76163 [ms] |
| 2026-09-19 17:56:20.438 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047168 [ms] |
| 2026-09-19 17:56:20.440 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.31514 [ms] |
| 2026-09-19 17:56:20.448 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 58.2411 [ms] |
| 2026-09-19 17:56:20.449 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:56:20.452 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.33642 [ms] |
| 2026-09-19 17:56:20.453 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:56:20.460 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:56:20.461 | INFO | Visibility | Clusters 2046 tested -> 1048 frustum, 1048 cone, 1048 visible | draws 1027+21, 117884 triangles |
| 2026-09-19 17:56:20.469 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:56:20.470 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1048 [count] |
| 2026-09-19 17:56:20.475 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1048 [count] |
| 2026-09-19 17:56:20.477 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1048 [count] |
| 2026-09-19 17:56:20.479 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 117884 [count] |
| 2026-09-19 17:56:20.481 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1048 [count] |
| 2026-09-19 17:56:20.484 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 90% of GPU frame |
| 2026-09-19 17:56:20.487 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:56:20.492 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:56:20.494 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:56:20.496 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:56:20.498 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:56:20.500 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:56:20.502 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 90.3736 [percent] |
| 2026-09-19 17:56:25.772 | INFO | Performance | CPU 58.39 ms/frame (16.7 fps, worst 100.00 ms over 86 frames), RSS 942 MiB |
| 2026-09-19 17:56:25.773 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 58.3918 [ms] |
| 2026-09-19 17:56:25.775 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:56:25.776 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.6626 [fps] |
| 2026-09-19 17:56:25.778 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 86 [count] |
| 2026-09-19 17:56:25.779 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 942.152 [MiB] |
| 2026-09-19 17:56:25.781 | INFO | GpuTiming | GPU 55.49 ms total | cull 0.10 · raster 9.12 · HiZ 0.05 · resolve 1.62 · ReSTIR 44.60 · shadow 0.00 · post 0.94 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:56:25.783 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 55.4903 [ms] |
| 2026-09-19 17:56:25.785 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.095776 [ms] |
| 2026-09-19 17:56:25.788 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.12291 [ms] |
| 2026-09-19 17:56:25.792 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.046848 [ms] |
| 2026-09-19 17:56:25.794 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.62038 [ms] |
| 2026-09-19 17:56:25.796 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 44.6044 [ms] |
| 2026-09-19 17:56:25.797 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:56:25.799 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.941471 [ms] |
| 2026-09-19 17:56:25.800 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:56:25.802 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:56:25.804 | INFO | Visibility | Clusters 2046 tested -> 1878 frustum, 1878 cone, 1878 visible | draws 1875+3, 211360 triangles |
| 2026-09-19 17:56:25.808 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:56:25.809 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1878 [count] |
| 2026-09-19 17:56:25.811 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1878 [count] |
| 2026-09-19 17:56:25.813 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1878 [count] |
| 2026-09-19 17:56:25.815 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 211360 [count] |
| 2026-09-19 17:56:25.816 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1878 [count] |
| 2026-09-19 17:56:25.818 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 80% of GPU frame |
| 2026-09-19 17:56:25.825 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:56:25.827 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:56:25.829 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:56:25.830 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:56:25.833 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:56:25.834 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:56:25.836 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 80.3823 [percent] |
| 2026-09-19 17:56:31.165 | INFO | Performance | CPU 58.52 ms/frame (17.1 fps, worst 100.00 ms over 86 frames), RSS 963 MiB |
| 2026-09-19 17:56:31.166 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 58.5177 [ms] |
| 2026-09-19 17:56:31.169 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:56:31.171 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 17.1048 [fps] |
| 2026-09-19 17:56:31.173 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 86 [count] |
| 2026-09-19 17:56:31.175 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 962.875 [MiB] |
| 2026-09-19 17:56:31.179 | INFO | GpuTiming | GPU 62.16 ms total | cull 0.09 · raster 7.87 · HiZ 0.05 · resolve 1.03 · ReSTIR 53.13 · shadow 0.00 · post 0.95 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:56:31.182 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 62.1628 [ms] |
| 2026-09-19 17:56:31.184 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.085696 [ms] |
| 2026-09-19 17:56:31.189 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.86842 [ms] |
| 2026-09-19 17:56:31.191 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.048768 [ms] |
| 2026-09-19 17:56:31.194 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.03078 [ms] |
| 2026-09-19 17:56:31.195 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 53.1292 [ms] |
| 2026-09-19 17:56:31.198 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:56:31.205 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.948288 [ms] |
| 2026-09-19 17:56:31.208 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:56:31.210 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:56:31.212 | INFO | Visibility | Clusters 2046 tested -> 1873 frustum, 1873 cone, 1873 visible | draws 1873+0, 210778 triangles |
| 2026-09-19 17:56:31.214 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:56:31.216 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1873 [count] |
| 2026-09-19 17:56:31.219 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1873 [count] |
| 2026-09-19 17:56:31.221 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1873 [count] |
| 2026-09-19 17:56:31.223 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 210778 [count] |
| 2026-09-19 17:56:31.224 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1873 [count] |
| 2026-09-19 17:56:31.227 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 85% of GPU frame |
| 2026-09-19 17:56:31.232 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:56:31.236 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:56:31.240 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:56:31.241 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:56:31.243 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:56:31.245 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:56:31.246 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 85.4677 [percent] |
| 2026-09-19 17:56:36.294 | INFO | Performance | CPU 50.15 ms/frame (19.1 fps, worst 100.00 ms over 100 frames), RSS 964 MiB |
| 2026-09-19 17:56:36.295 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 50.149 [ms] |
| 2026-09-19 17:56:36.298 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:56:36.299 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 19.1219 [fps] |
| 2026-09-19 17:56:36.303 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 100 [count] |
| 2026-09-19 17:56:36.305 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 963.625 [MiB] |
| 2026-09-19 17:56:36.307 | INFO | GpuTiming | GPU 42.08 ms total | cull 0.09 · raster 6.12 · HiZ 0.05 · resolve 0.09 · ReSTIR 35.72 · shadow 0.00 · post 1.25 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:56:36.309 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 42.0786 [ms] |
| 2026-09-19 17:56:36.314 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.09184 [ms] |
| 2026-09-19 17:56:36.316 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 6.12291 [ms] |
| 2026-09-19 17:56:36.318 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 17:56:36.319 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.094208 [ms] |
| 2026-09-19 17:56:36.321 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 35.7226 [ms] |
| 2026-09-19 17:56:36.323 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:56:36.325 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.24998 [ms] |
| 2026-09-19 17:56:36.327 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:56:36.333 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:56:36.335 | INFO | Visibility | Clusters 2046 tested -> 1509 frustum, 1509 cone, 1420 visible | draws 691+729, 160154 triangles |
| 2026-09-19 17:56:36.336 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:56:36.338 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1509 [count] |
| 2026-09-19 17:56:36.340 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1509 [count] |
| 2026-09-19 17:56:36.345 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1420 [count] |
| 2026-09-19 17:56:36.347 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 160154 [count] |
| 2026-09-19 17:56:36.349 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1420 [count] |
| 2026-09-19 17:56:36.353 | INFO | Performance | CPU-BOUND or presenting-limited | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 85% of GPU frame |
| 2026-09-19 17:56:36.356 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:56:36.361 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:56:36.363 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:56:36.365 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:56:36.367 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:56:36.369 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 17:56:36.371 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 84.8948 [percent] |
| 2026-09-19 17:56:41.769 | INFO | Performance | CPU 57.52 ms/frame (19.0 fps, worst 100.00 ms over 87 frames), RSS 1105 MiB |
| 2026-09-19 17:56:41.770 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 57.5199 [ms] |
| 2026-09-19 17:56:41.772 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:56:41.774 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 18.9854 [fps] |
| 2026-09-19 17:56:41.775 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 87 [count] |
| 2026-09-19 17:56:41.777 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1104.89 [MiB] |
| 2026-09-19 17:56:41.778 | INFO | GpuTiming | GPU 70.35 ms total | cull 0.08 · raster 6.22 · HiZ 0.05 · resolve 1.09 · ReSTIR 62.91 · shadow 0.00 · post 1.00 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:56:41.783 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 70.3489 [ms] |
| 2026-09-19 17:56:41.786 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.082784 [ms] |
| 2026-09-19 17:56:41.787 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 6.22074 [ms] |
| 2026-09-19 17:56:41.789 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.050592 [ms] |
| 2026-09-19 17:56:41.791 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.0855 [ms] |
| 2026-09-19 17:56:41.793 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 62.9093 [ms] |
| 2026-09-19 17:56:41.795 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:56:41.800 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.00461 [ms] |
| 2026-09-19 17:56:41.801 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:56:41.803 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:56:41.805 | INFO | Visibility | Clusters 2046 tested -> 1434 frustum, 1434 cone, 1417 visible | draws 1417+0, 159950 triangles |
| 2026-09-19 17:56:41.807 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:56:41.809 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1434 [count] |
| 2026-09-19 17:56:41.811 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1434 [count] |
| 2026-09-19 17:56:41.817 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1417 [count] |
| 2026-09-19 17:56:41.819 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 159950 [count] |
| 2026-09-19 17:56:41.821 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1417 [count] |
| 2026-09-19 17:56:41.823 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 89% of GPU frame |
| 2026-09-19 17:56:41.829 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 17:56:41.834 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 17:56:41.836 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 17:56:41.839 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 17:56:41.841 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 17:56:41.843 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:56:41.848 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 89.4247 [percent] |
| 2026-09-19 17:56:43.087 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 9 taps. |
| 2026-09-19 17:56:43.681 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 9 taps. |
| 2026-09-19 17:56:43.988 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: Hard @ 1024 px, 1 taps. |
| 2026-09-19 17:56:44.548 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: Hard @ 1024 px, 1 taps. |
| 2026-09-19 17:56:45.298 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCF @ 1024 px, 5 taps. |
| 2026-09-19 17:56:45.733 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCF @ 1024 px, 5 taps. |
| 2026-09-19 17:56:47.484 | INFO | Performance | CPU 59.53 ms/frame (16.7 fps, worst 100.00 ms over 84 frames), RSS 813 MiB |
| 2026-09-19 17:56:47.485 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 59.5343 [ms] |
| 2026-09-19 17:56:47.487 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:56:47.489 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.6643 [fps] |
| 2026-09-19 17:56:47.491 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 84 [count] |
| 2026-09-19 17:56:47.492 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 813 [MiB] |
| 2026-09-19 17:56:47.494 | INFO | GpuTiming | GPU 62.87 ms total | cull 0.09 · raster 6.87 · HiZ 0.05 · resolve 1.34 · ReSTIR 54.53 · shadow 0.00 · post 1.06 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:56:47.497 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 62.8734 [ms] |
| 2026-09-19 17:56:47.499 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.088512 [ms] |
| 2026-09-19 17:56:47.502 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 6.86794 [ms] |
| 2026-09-19 17:56:47.503 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.052384 [ms] |
| 2026-09-19 17:56:47.505 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.33539 [ms] |
| 2026-09-19 17:56:47.510 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 54.5292 [ms] |
| 2026-09-19 17:56:47.511 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:56:47.514 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.05651 [ms] |
| 2026-09-19 17:56:47.516 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:56:47.518 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:56:47.520 | INFO | Visibility | Clusters 2046 tested -> 1502 frustum, 1502 cone, 1496 visible | draws 1496+0, 168790 triangles |
| 2026-09-19 17:56:47.522 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:56:47.526 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1502 [count] |
| 2026-09-19 17:56:47.528 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1502 [count] |
| 2026-09-19 17:56:47.529 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1496 [count] |
| 2026-09-19 17:56:47.533 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 168790 [count] |
| 2026-09-19 17:56:47.534 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1496 [count] |
| 2026-09-19 17:56:47.537 | INFO | Performance | GPU-BOUND | 963 kpx x (2 candidates + 1 extra + 1 spatial taps), 4 denoise levels, present FIFO | kernel 87% of GPU frame |
| 2026-09-19 17:56:47.542 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:56:47.543 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 2 [count] |
| 2026-09-19 17:56:47.546 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 1 [count] |
| 2026-09-19 17:56:47.550 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 1 [count] |
| 2026-09-19 17:56:47.553 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:56:47.557 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:56:47.559 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 86.7285 [percent] |
| 2026-09-19 17:56:52.953 | INFO | Performance | CPU 58.71 ms/frame (17.1 fps, worst 100.00 ms over 86 frames), RSS 829 MiB |
| 2026-09-19 17:56:52.954 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 58.7137 [ms] |
| 2026-09-19 17:56:52.957 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:56:52.958 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 17.0559 [fps] |
| 2026-09-19 17:56:52.959 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 86 [count] |
| 2026-09-19 17:56:52.961 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 828.812 [MiB] |
| 2026-09-19 17:56:52.963 | INFO | GpuTiming | GPU 55.71 ms total | cull 0.07 · raster 5.19 · HiZ 0.05 · resolve 0.76 · ReSTIR 49.64 · shadow 0.00 · post 0.96 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:56:52.966 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 55.7075 [ms] |
| 2026-09-19 17:56:52.971 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.073504 [ms] |
| 2026-09-19 17:56:52.973 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 5.18938 [ms] |
| 2026-09-19 17:56:52.974 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.04912 [ms] |
| 2026-09-19 17:56:52.977 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.759808 [ms] |
| 2026-09-19 17:56:52.978 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 49.6357 [ms] |
| 2026-09-19 17:56:52.980 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:56:52.981 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.961853 [ms] |
| 2026-09-19 17:56:52.986 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:56:52.988 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:56:52.990 | INFO | Visibility | Clusters 2046 tested -> 1177 frustum, 1177 cone, 1177 visible | draws 1165+12, 131968 triangles |
| 2026-09-19 17:56:52.991 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:56:52.994 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1177 [count] |
| 2026-09-19 17:56:52.995 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1177 [count] |
| 2026-09-19 17:56:52.997 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1177 [count] |
| 2026-09-19 17:56:53.001 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 131968 [count] |
| 2026-09-19 17:56:53.003 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1177 [count] |
| 2026-09-19 17:56:53.006 | INFO | Performance | GPU-BOUND | 963 kpx x (2 candidates + 1 extra + 1 spatial taps), 4 denoise levels, present FIFO | kernel 89% of GPU frame |
| 2026-09-19 17:56:53.008 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:56:53.010 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 2 [count] |
| 2026-09-19 17:56:53.012 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 1 [count] |
| 2026-09-19 17:56:53.013 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 1 [count] |
| 2026-09-19 17:56:53.019 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:56:53.020 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:56:53.023 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 89.1006 [percent] |
| 2026-09-19 17:56:58.028 | INFO | Performance | CPU 48.94 ms/frame (19.8 fps, worst 100.00 ms over 104 frames), RSS 919 MiB |
| 2026-09-19 17:56:58.030 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 48.938 [ms] |
| 2026-09-19 17:56:58.032 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:56:58.034 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 19.7964 [fps] |
| 2026-09-19 17:56:58.036 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 104 [count] |
| 2026-09-19 17:56:58.038 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 918.781 [MiB] |
| 2026-09-19 17:56:58.040 | INFO | GpuTiming | GPU 47.16 ms total | cull 0.07 · raster 3.18 · HiZ 0.05 · resolve 0.73 · ReSTIR 43.12 · shadow 0.00 · post 0.93 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:56:58.043 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 47.1555 [ms] |
| 2026-09-19 17:56:58.046 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.069152 [ms] |
| 2026-09-19 17:56:58.047 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.18435 [ms] |
| 2026-09-19 17:56:58.049 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 17:56:58.055 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.734176 [ms] |
| 2026-09-19 17:56:58.057 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 43.1207 [ms] |
| 2026-09-19 17:56:58.059 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:56:58.061 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.928864 [ms] |
| 2026-09-19 17:56:58.063 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:56:58.065 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:56:58.072 | INFO | Visibility | Clusters 2046 tested -> 760 frustum, 760 cone, 760 visible | draws 758+2, 85272 triangles |
| 2026-09-19 17:56:58.073 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:56:58.076 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 760 [count] |
| 2026-09-19 17:56:58.077 | INFO | TelemetryMetrics | Measurement: ClustersCone = 760 [count] |
| 2026-09-19 17:56:58.080 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 760 [count] |
| 2026-09-19 17:56:58.081 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 85272 [count] |
| 2026-09-19 17:56:58.088 | INFO | TelemetryMetrics | Measurement: DrawCalls = 760 [count] |
| 2026-09-19 17:56:58.090 | INFO | Performance | GPU-BOUND | 963 kpx x (2 candidates + 1 extra + 1 spatial taps), 4 denoise levels, present FIFO | kernel 91% of GPU frame |
| 2026-09-19 17:56:58.093 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:56:58.095 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 2 [count] |
| 2026-09-19 17:56:58.102 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 1 [count] |
| 2026-09-19 17:56:58.104 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 1 [count] |
| 2026-09-19 17:56:58.108 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:56:58.110 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:56:58.111 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 91.4437 [percent] |
| 2026-09-19 17:57:03.239 | INFO | Performance | CPU 49.64 ms/frame (20.0 fps, worst 100.00 ms over 101 frames), RSS 945 MiB |
| 2026-09-19 17:57:03.241 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 49.6401 [ms] |
| 2026-09-19 17:57:03.243 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:57:03.245 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 20.0378 [fps] |
| 2026-09-19 17:57:03.247 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 101 [count] |
| 2026-09-19 17:57:03.249 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 944.758 [MiB] |
| 2026-09-19 17:57:03.251 | INFO | GpuTiming | GPU 45.24 ms total | cull 0.10 · raster 4.87 · HiZ 0.05 · resolve 0.91 · ReSTIR 39.31 · shadow 0.00 · post 0.87 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:57:03.254 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 45.2399 [ms] |
| 2026-09-19 17:57:03.256 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.104128 [ms] |
| 2026-09-19 17:57:03.258 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 4.87014 [ms] |
| 2026-09-19 17:57:03.260 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.049152 [ms] |
| 2026-09-19 17:57:03.262 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.905216 [ms] |
| 2026-09-19 17:57:03.268 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 39.3112 [ms] |
| 2026-09-19 17:57:03.270 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:57:03.273 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.873695 [ms] |
| 2026-09-19 17:57:03.274 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:57:03.277 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:57:03.279 | INFO | Visibility | Clusters 2046 tested -> 1070 frustum, 1070 cone, 1070 visible | draws 1045+25, 120140 triangles |
| 2026-09-19 17:57:03.283 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:57:03.284 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1070 [count] |
| 2026-09-19 17:57:03.286 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1070 [count] |
| 2026-09-19 17:57:03.288 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1070 [count] |
| 2026-09-19 17:57:03.290 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 120140 [count] |
| 2026-09-19 17:57:03.292 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1070 [count] |
| 2026-09-19 17:57:03.294 | INFO | Performance | GPU-BOUND | 963 kpx x (2 candidates + 1 extra + 1 spatial taps), 4 denoise levels, present FIFO | kernel 87% of GPU frame |
| 2026-09-19 17:57:03.297 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:57:03.303 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 2 [count] |
| 2026-09-19 17:57:03.304 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 1 [count] |
| 2026-09-19 17:57:03.306 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 1 [count] |
| 2026-09-19 17:57:03.308 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:57:03.310 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:57:03.314 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 86.8951 [percent] |
| 2026-09-19 17:57:08.363 | INFO | Performance | CPU 51.89 ms/frame (19.8 fps, worst 100.00 ms over 98 frames), RSS 966 MiB |
| 2026-09-19 17:57:08.365 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 51.895 [ms] |
| 2026-09-19 17:57:08.367 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:57:08.369 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 19.8163 [fps] |
| 2026-09-19 17:57:08.371 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 98 [count] |
| 2026-09-19 17:57:08.373 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 966.48 [MiB] |
| 2026-09-19 17:57:08.375 | INFO | GpuTiming | GPU 47.64 ms total | cull 0.09 · raster 8.60 · HiZ 0.05 · resolve 1.21 · ReSTIR 37.69 · shadow 0.00 · post 0.76 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:57:08.379 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 47.6379 [ms] |
| 2026-09-19 17:57:08.383 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.090688 [ms] |
| 2026-09-19 17:57:08.385 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.59872 [ms] |
| 2026-09-19 17:57:08.387 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.04608 [ms] |
| 2026-09-19 17:57:08.389 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.21046 [ms] |
| 2026-09-19 17:57:08.391 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 37.6919 [ms] |
| 2026-09-19 17:57:08.395 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:57:08.399 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.759136 [ms] |
| 2026-09-19 17:57:08.401 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:57:08.403 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:57:08.405 | INFO | Visibility | Clusters 2046 tested -> 1980 frustum, 1980 cone, 1980 visible | draws 1980+0, 222296 triangles |
| 2026-09-19 17:57:08.407 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:57:08.408 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1980 [count] |
| 2026-09-19 17:57:08.411 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1980 [count] |
| 2026-09-19 17:57:08.414 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1980 [count] |
| 2026-09-19 17:57:08.416 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 222296 [count] |
| 2026-09-19 17:57:08.419 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1980 [count] |
| 2026-09-19 17:57:08.420 | INFO | Performance | GPU-BOUND | 963 kpx x (2 candidates + 1 extra + 1 spatial taps), 4 denoise levels, present FIFO | kernel 79% of GPU frame |
| 2026-09-19 17:57:08.424 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:57:08.426 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 2 [count] |
| 2026-09-19 17:57:08.427 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 1 [count] |
| 2026-09-19 17:57:08.431 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 1 [count] |
| 2026-09-19 17:57:08.433 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:57:08.435 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:57:08.436 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 79.1218 [percent] |
| 2026-09-19 17:57:13.385 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:13.436 | INFO | Performance | CPU 48.81 ms/frame (20.2 fps, worst 100.00 ms over 103 frames), RSS 967 MiB |
| 2026-09-19 17:57:13.437 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 48.8146 [ms] |
| 2026-09-19 17:57:13.438 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:57:13.441 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 20.2408 [fps] |
| 2026-09-19 17:57:13.443 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 103 [count] |
| 2026-09-19 17:57:13.446 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 966.668 [MiB] |
| 2026-09-19 17:57:13.447 | INFO | GpuTiming | GPU 44.13 ms total | cull 0.08 · raster 8.16 · HiZ 0.05 · resolve 1.03 · ReSTIR 34.81 · shadow 0.00 · post 0.61 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:57:13.450 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 44.1291 [ms] |
| 2026-09-19 17:57:13.451 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.082112 [ms] |
| 2026-09-19 17:57:13.453 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.16189 [ms] |
| 2026-09-19 17:57:13.455 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 17:57:13.457 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.03021 [ms] |
| 2026-09-19 17:57:13.462 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 34.8078 [ms] |
| 2026-09-19 17:57:13.464 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:57:13.466 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.605824 [ms] |
| 2026-09-19 17:57:13.468 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:57:13.470 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:57:13.475 | INFO | Visibility | Clusters 2046 tested -> 1980 frustum, 1980 cone, 1980 visible | draws 1980+0, 222296 triangles |
| 2026-09-19 17:57:13.477 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:57:13.480 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1980 [count] |
| 2026-09-19 17:57:13.481 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1980 [count] |
| 2026-09-19 17:57:13.484 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1980 [count] |
| 2026-09-19 17:57:13.485 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 222296 [count] |
| 2026-09-19 17:57:13.487 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1980 [count] |
| 2026-09-19 17:57:13.488 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 79% of GPU frame |
| 2026-09-19 17:57:13.497 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:57:13.498 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:57:13.500 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:57:13.502 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:57:13.503 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:57:13.508 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:57:13.510 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 78.8772 [percent] |
| 2026-09-19 17:57:13.847 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:15.372 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:15.781 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:18.496 | INFO | Performance | CPU 43.15 ms/frame (23.0 fps, worst 100.00 ms over 116 frames), RSS 1105 MiB |
| 2026-09-19 17:57:18.497 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 43.1487 [ms] |
| 2026-09-19 17:57:18.500 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:57:18.502 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 22.9599 [fps] |
| 2026-09-19 17:57:18.504 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 116 [count] |
| 2026-09-19 17:57:18.506 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1104.86 [MiB] |
| 2026-09-19 17:57:18.508 | INFO | GpuTiming | GPU 37.95 ms total | cull 0.09 · raster 8.99 · HiZ 0.04 · resolve 0.87 · ReSTIR 27.96 · shadow 0.00 · post 0.41 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:57:18.511 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 37.9477 [ms] |
| 2026-09-19 17:57:18.513 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.087104 [ms] |
| 2026-09-19 17:57:18.515 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.99056 [ms] |
| 2026-09-19 17:57:18.521 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.0384 [ms] |
| 2026-09-19 17:57:18.523 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.8704 [ms] |
| 2026-09-19 17:57:18.525 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 27.9612 [ms] |
| 2026-09-19 17:57:18.527 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:57:18.531 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.409695 [ms] |
| 2026-09-19 17:57:18.535 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:57:18.537 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:57:18.542 | INFO | Visibility | Clusters 2046 tested -> 1980 frustum, 1980 cone, 1980 visible | draws 1980+0, 222296 triangles |
| 2026-09-19 17:57:18.545 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:57:18.546 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1980 [count] |
| 2026-09-19 17:57:18.551 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1980 [count] |
| 2026-09-19 17:57:18.553 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1980 [count] |
| 2026-09-19 17:57:18.554 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 222296 [count] |
| 2026-09-19 17:57:18.556 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1980 [count] |
| 2026-09-19 17:57:18.558 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 74% of GPU frame |
| 2026-09-19 17:57:18.561 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:57:18.562 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:57:18.567 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:57:18.568 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:57:18.570 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:57:18.572 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:57:18.573 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 73.6836 [percent] |
| 2026-09-19 17:57:23.517 | INFO | Performance | CPU 39.09 ms/frame (25.6 fps, worst 100.00 ms over 128 frames), RSS 1106 MiB |
| 2026-09-19 17:57:23.519 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 39.0946 [ms] |
| 2026-09-19 17:57:23.521 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:57:23.522 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 25.5588 [fps] |
| 2026-09-19 17:57:23.524 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 128 [count] |
| 2026-09-19 17:57:23.530 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1105.8 [MiB] |
| 2026-09-19 17:57:23.531 | INFO | GpuTiming | GPU 36.42 ms total | cull 0.09 · raster 7.79 · HiZ 0.04 · resolve 1.14 · ReSTIR 27.36 · shadow 0.00 · post 0.58 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:57:23.534 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 36.418 [ms] |
| 2026-09-19 17:57:23.536 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.087104 [ms] |
| 2026-09-19 17:57:23.537 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.78621 [ms] |
| 2026-09-19 17:57:23.539 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.038912 [ms] |
| 2026-09-19 17:57:23.541 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.14278 [ms] |
| 2026-09-19 17:57:23.542 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 27.363 [ms] |
| 2026-09-19 17:57:23.546 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:57:23.548 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.579424 [ms] |
| 2026-09-19 17:57:23.550 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:57:23.551 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:57:23.553 | INFO | Visibility | Clusters 2046 tested -> 1741 frustum, 1741 cone, 1741 visible | draws 1693+48, 195820 triangles |
| 2026-09-19 17:57:23.554 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:57:23.556 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1741 [count] |
| 2026-09-19 17:57:23.558 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1741 [count] |
| 2026-09-19 17:57:23.563 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1741 [count] |
| 2026-09-19 17:57:23.565 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 195820 [count] |
| 2026-09-19 17:57:23.567 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1741 [count] |
| 2026-09-19 17:57:23.569 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 75% of GPU frame |
| 2026-09-19 17:57:23.571 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:57:23.573 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:57:23.579 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:57:23.581 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:57:23.583 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:57:23.586 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:57:23.588 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 75.1359 [percent] |
| 2026-09-19 17:57:28.166 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:28.547 | INFO | Performance | CPU 36.96 ms/frame (27.3 fps, worst 100.00 ms over 136 frames), RSS 824 MiB |
| 2026-09-19 17:57:28.548 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 36.9643 [ms] |
| 2026-09-19 17:57:28.550 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:57:28.552 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 27.3119 [fps] |
| 2026-09-19 17:57:28.554 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 136 [count] |
| 2026-09-19 17:57:28.556 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 823.809 [MiB] |
| 2026-09-19 17:57:28.557 | INFO | GpuTiming | GPU 28.51 ms total | cull 0.17 · raster 9.90 · HiZ 0.03 · resolve 1.34 · ReSTIR 17.06 · shadow 0.00 · post 0.26 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:57:28.562 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 28.5121 [ms] |
| 2026-09-19 17:57:28.568 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.172 [ms] |
| 2026-09-19 17:57:28.569 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.89914 [ms] |
| 2026-09-19 17:57:28.574 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.033024 [ms] |
| 2026-09-19 17:57:28.577 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.3432 [ms] |
| 2026-09-19 17:57:28.579 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 17.0647 [ms] |
| 2026-09-19 17:57:28.581 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:57:28.583 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.256895 [ms] |
| 2026-09-19 17:57:28.585 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:57:28.587 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:57:28.589 | INFO | Visibility | Clusters 2046 tested -> 1947 frustum, 1947 cone, 1947 visible | draws 1947+0, 219176 triangles |
| 2026-09-19 17:57:28.598 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:57:28.600 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1947 [count] |
| 2026-09-19 17:57:28.603 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1947 [count] |
| 2026-09-19 17:57:28.609 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1947 [count] |
| 2026-09-19 17:57:28.612 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 219176 [count] |
| 2026-09-19 17:57:28.615 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1947 [count] |
| 2026-09-19 17:57:28.617 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 60% of GPU frame |
| 2026-09-19 17:57:28.623 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:57:28.630 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:57:28.633 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:57:28.635 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:57:28.640 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:57:28.643 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 17:57:28.646 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 59.8508 [percent] |
| 2026-09-19 17:57:28.650 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:29.520 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:29.654 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:29.709 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:29.795 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:30.238 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:30.297 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:30.402 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:30.813 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:30.895 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:30.947 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:31.175 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:31.187 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:31.206 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:31.266 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:31.389 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:31.832 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:31.865 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:31.961 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:31.986 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:32.036 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:32.110 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:32.140 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:32.184 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:32.445 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:32.853 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle). Tier stage if GI is switched off: PCSS @ 1024 px, 5 taps. |
| 2026-09-19 17:57:33.575 | INFO | Performance | CPU 25.72 ms/frame (41.0 fps, worst 100.00 ms over 195 frames), RSS 809 MiB |
| 2026-09-19 17:57:33.576 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 25.7203 [ms] |
| 2026-09-19 17:57:33.581 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:57:33.583 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 40.9888 [fps] |
| 2026-09-19 17:57:33.586 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 195 [count] |
| 2026-09-19 17:57:33.593 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 808.527 [MiB] |
| 2026-09-19 17:57:33.595 | INFO | GpuTiming | GPU 20.39 ms total | cull 0.08 · raster 8.39 · HiZ 0.03 · resolve 0.46 · ReSTIR 11.43 · shadow 0.00 · post 0.18 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:57:33.599 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 20.3943 [ms] |
| 2026-09-19 17:57:33.602 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.083424 [ms] |
| 2026-09-19 17:57:33.607 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.38685 [ms] |
| 2026-09-19 17:57:33.609 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.03088 [ms] |
| 2026-09-19 17:57:33.615 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.458272 [ms] |
| 2026-09-19 17:57:33.617 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 11.4349 [ms] |
| 2026-09-19 17:57:33.621 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:57:33.623 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.183872 [ms] |
| 2026-09-19 17:57:33.625 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:57:33.627 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:57:33.629 | INFO | Visibility | Clusters 2046 tested -> 1947 frustum, 1947 cone, 1947 visible | draws 1947+0, 219176 triangles |
| 2026-09-19 17:57:33.632 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:57:33.634 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1947 [count] |
| 2026-09-19 17:57:33.638 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1947 [count] |
| 2026-09-19 17:57:33.641 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1947 [count] |
| 2026-09-19 17:57:33.643 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 219176 [count] |
| 2026-09-19 17:57:33.645 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1947 [count] |
| 2026-09-19 17:57:33.648 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 56% of GPU frame |
| 2026-09-19 17:57:33.652 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:57:33.657 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:57:33.659 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:57:33.662 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:57:33.664 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:57:33.666 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 17:57:33.671 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 56.0691 [percent] |
| 2026-09-19 17:57:38.604 | INFO | Performance | CPU 25.27 ms/frame (40.2 fps, worst 100.00 ms over 198 frames), RSS 864 MiB |
| 2026-09-19 17:57:38.606 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 25.2713 [ms] |
| 2026-09-19 17:57:38.608 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:57:38.610 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 40.1763 [fps] |
| 2026-09-19 17:57:38.613 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 198 [count] |
| 2026-09-19 17:57:38.615 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 864.336 [MiB] |
| 2026-09-19 17:57:38.617 | INFO | GpuTiming | GPU 25.67 ms total | cull 0.11 · raster 10.13 · HiZ 0.03 · resolve 0.78 · ReSTIR 14.63 · shadow 0.00 · post 0.24 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:57:38.621 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 25.6706 [ms] |
| 2026-09-19 17:57:38.623 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.108928 [ms] |
| 2026-09-19 17:57:38.628 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 10.1252 [ms] |
| 2026-09-19 17:57:38.631 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.030656 [ms] |
| 2026-09-19 17:57:38.633 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.780256 [ms] |
| 2026-09-19 17:57:38.635 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 14.6256 [ms] |
| 2026-09-19 17:57:38.638 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:57:38.639 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.235168 [ms] |
| 2026-09-19 17:57:38.644 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:57:38.646 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:57:38.648 | INFO | Visibility | Clusters 2046 tested -> 1824 frustum, 1824 cone, 1824 visible | draws 1824+0, 205408 triangles |
| 2026-09-19 17:57:38.650 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:57:38.651 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1824 [count] |
| 2026-09-19 17:57:38.653 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1824 [count] |
| 2026-09-19 17:57:38.655 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1824 [count] |
| 2026-09-19 17:57:38.660 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 205408 [count] |
| 2026-09-19 17:57:38.663 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1824 [count] |
| 2026-09-19 17:57:38.665 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 57% of GPU frame |
| 2026-09-19 17:57:38.668 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:57:38.670 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:57:38.675 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:57:38.677 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:57:38.679 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:57:38.681 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:57:38.683 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 56.9741 [percent] |
| 2026-09-19 17:57:43.605 | INFO | Performance | CPU 24.04 ms/frame (41.4 fps, worst 100.00 ms over 208 frames), RSS 940 MiB |
| 2026-09-19 17:57:43.606 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 24.0444 [ms] |
| 2026-09-19 17:57:43.609 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:57:43.611 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 41.4493 [fps] |
| 2026-09-19 17:57:43.614 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 208 [count] |
| 2026-09-19 17:57:43.616 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 939.895 [MiB] |
| 2026-09-19 17:57:43.619 | INFO | GpuTiming | GPU 22.80 ms total | cull 0.08 · raster 5.58 · HiZ 0.03 · resolve 0.87 · ReSTIR 16.24 · shadow 0.00 · post 0.24 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:57:43.622 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 22.7955 [ms] |
| 2026-09-19 17:57:43.631 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.077056 [ms] |
| 2026-09-19 17:57:43.633 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 5.57949 [ms] |
| 2026-09-19 17:57:43.636 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.030464 [ms] |
| 2026-09-19 17:57:43.640 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.867904 [ms] |
| 2026-09-19 17:57:43.642 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 16.2406 [ms] |
| 2026-09-19 17:57:43.645 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:57:43.647 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.238081 [ms] |
| 2026-09-19 17:57:43.649 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:57:43.651 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:57:43.657 | INFO | Visibility | Clusters 2046 tested -> 1212 frustum, 1212 cone, 1212 visible | draws 1212+0, 136086 triangles |
| 2026-09-19 17:57:43.659 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:57:43.661 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1212 [count] |
| 2026-09-19 17:57:43.663 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1212 [count] |
| 2026-09-19 17:57:43.666 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1212 [count] |
| 2026-09-19 17:57:43.668 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 136086 [count] |
| 2026-09-19 17:57:43.673 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1212 [count] |
| 2026-09-19 17:57:43.674 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 71% of GPU frame |
| 2026-09-19 17:57:43.678 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:57:43.680 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:57:43.683 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:57:43.687 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:57:43.689 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:57:43.692 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:57:43.694 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 71.2447 [percent] |
| 2026-09-19 17:57:48.654 | INFO | Performance | CPU 24.31 ms/frame (41.2 fps, worst 100.00 ms over 206 frames), RSS 781 MiB |
| 2026-09-19 17:57:48.656 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 24.3094 [ms] |
| 2026-09-19 17:57:48.659 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:57:48.660 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 41.2028 [fps] |
| 2026-09-19 17:57:48.662 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 206 [count] |
| 2026-09-19 17:57:48.667 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 780.789 [MiB] |
| 2026-09-19 17:57:48.668 | INFO | GpuTiming | GPU 22.64 ms total | cull 0.08 · raster 4.25 · HiZ 0.03 · resolve 0.49 · ReSTIR 17.78 · shadow 0.00 · post 0.38 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:57:48.671 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 22.6445 [ms] |
| 2026-09-19 17:57:48.675 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.084896 [ms] |
| 2026-09-19 17:57:48.676 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 4.25261 [ms] |
| 2026-09-19 17:57:48.678 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.030976 [ms] |
| 2026-09-19 17:57:48.679 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.49152 [ms] |
| 2026-09-19 17:57:48.685 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 17.7845 [ms] |
| 2026-09-19 17:57:48.687 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:57:48.689 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.377184 [ms] |
| 2026-09-19 17:57:48.690 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:57:48.692 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:57:48.694 | INFO | Visibility | Clusters 2046 tested -> 994 frustum, 994 cone, 994 visible | draws 994+0, 111504 triangles |
| 2026-09-19 17:57:48.695 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:57:48.699 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 994 [count] |
| 2026-09-19 17:57:48.701 | INFO | TelemetryMetrics | Measurement: ClustersCone = 994 [count] |
| 2026-09-19 17:57:48.702 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 994 [count] |
| 2026-09-19 17:57:48.703 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 111504 [count] |
| 2026-09-19 17:57:48.704 | INFO | TelemetryMetrics | Measurement: DrawCalls = 994 [count] |
| 2026-09-19 17:57:48.706 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 79% of GPU frame |
| 2026-09-19 17:57:48.707 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:57:48.708 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:57:48.710 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:57:48.713 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:57:48.715 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:57:48.717 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:57:48.719 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 78.5378 [percent] |
| 2026-09-19 17:57:53.634 | INFO | Performance | CPU 21.50 ms/frame (48.7 fps, worst 100.00 ms over 233 frames), RSS 756 MiB |
| 2026-09-19 17:57:53.635 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 21.5023 [ms] |
| 2026-09-19 17:57:53.636 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:57:53.639 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 48.699 [fps] |
| 2026-09-19 17:57:53.640 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 233 [count] |
| 2026-09-19 17:57:53.642 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 755.988 [MiB] |
| 2026-09-19 17:57:53.643 | INFO | GpuTiming | GPU 19.89 ms total | cull 0.08 · raster 2.81 · HiZ 0.03 · resolve 0.04 · ReSTIR 16.93 · shadow 0.00 · post 0.31 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:57:53.647 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 19.8855 [ms] |
| 2026-09-19 17:57:53.649 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.075616 [ms] |
| 2026-09-19 17:57:53.651 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 2.81264 [ms] |
| 2026-09-19 17:57:53.654 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.029024 [ms] |
| 2026-09-19 17:57:53.656 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.036736 [ms] |
| 2026-09-19 17:57:53.657 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 16.9315 [ms] |
| 2026-09-19 17:57:53.658 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:57:53.660 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.31472 [ms] |
| 2026-09-19 17:57:53.660 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:57:53.662 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:57:53.663 | INFO | Visibility | Clusters 2046 tested -> 812 frustum, 812 cone, 635 visible | draws 632+3, 71656 triangles |
| 2026-09-19 17:57:53.665 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:57:53.666 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 812 [count] |
| 2026-09-19 17:57:53.670 | INFO | TelemetryMetrics | Measurement: ClustersCone = 812 [count] |
| 2026-09-19 17:57:53.671 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 635 [count] |
| 2026-09-19 17:57:53.674 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 71656 [count] |
| 2026-09-19 17:57:53.674 | INFO | TelemetryMetrics | Measurement: DrawCalls = 635 [count] |
| 2026-09-19 17:57:53.676 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 85% of GPU frame |
| 2026-09-19 17:57:53.678 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:57:53.679 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:57:53.680 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:57:53.681 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:57:53.682 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:57:53.682 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:57:53.685 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 85.1449 [percent] |
| 2026-09-19 17:57:58.650 | INFO | Performance | CPU 21.01 ms/frame (47.7 fps, worst 56.13 ms over 238 frames), RSS 756 MiB |
| 2026-09-19 17:57:58.652 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 21.0143 [ms] |
| 2026-09-19 17:57:58.655 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 56.1325 [ms] |
| 2026-09-19 17:57:58.658 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 47.7486 [fps] |
| 2026-09-19 17:57:58.661 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 238 [count] |
| 2026-09-19 17:57:58.667 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 755.988 [MiB] |
| 2026-09-19 17:57:58.669 | INFO | GpuTiming | GPU 18.43 ms total | cull 0.07 · raster 4.65 · HiZ 0.03 · resolve 0.54 · ReSTIR 13.13 · shadow 0.00 · post 0.27 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:57:58.673 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 18.4281 [ms] |
| 2026-09-19 17:57:58.676 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.068576 [ms] |
| 2026-09-19 17:57:58.680 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 4.65414 [ms] |
| 2026-09-19 17:57:58.682 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.03072 [ms] |
| 2026-09-19 17:57:58.685 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.544768 [ms] |
| 2026-09-19 17:57:58.687 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 13.1299 [ms] |
| 2026-09-19 17:57:58.690 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:57:58.691 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.271264 [ms] |
| 2026-09-19 17:57:58.696 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:57:58.698 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:57:58.700 | INFO | Visibility | Clusters 2046 tested -> 1113 frustum, 1113 cone, 1108 visible | draws 1105+3, 124480 triangles |
| 2026-09-19 17:57:58.702 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:57:58.704 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1113 [count] |
| 2026-09-19 17:57:58.706 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1113 [count] |
| 2026-09-19 17:57:58.707 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1108 [count] |
| 2026-09-19 17:57:58.709 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 124480 [count] |
| 2026-09-19 17:57:58.714 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1108 [count] |
| 2026-09-19 17:57:58.716 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 71% of GPU frame |
| 2026-09-19 17:57:58.719 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:57:58.721 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:57:58.722 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:57:58.724 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:57:58.728 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:57:58.729 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:57:58.731 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 71.2493 [percent] |
| 2026-09-19 17:58:03.669 | INFO | Performance | CPU 24.09 ms/frame (40.7 fps, worst 100.00 ms over 208 frames), RSS 794 MiB |
| 2026-09-19 17:58:03.671 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 24.091 [ms] |
| 2026-09-19 17:58:03.674 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:58:03.676 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 40.6869 [fps] |
| 2026-09-19 17:58:03.677 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 208 [count] |
| 2026-09-19 17:58:03.679 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 794.383 [MiB] |
| 2026-09-19 17:58:03.680 | INFO | GpuTiming | GPU 25.31 ms total | cull 0.08 · raster 9.58 · HiZ 0.03 · resolve 0.68 · ReSTIR 14.93 · shadow 0.00 · post 0.19 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:58:03.684 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 25.3095 [ms] |
| 2026-09-19 17:58:03.688 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.08224 [ms] |
| 2026-09-19 17:58:03.693 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.58387 [ms] |
| 2026-09-19 17:58:03.695 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.030976 [ms] |
| 2026-09-19 17:58:03.696 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.683328 [ms] |
| 2026-09-19 17:58:03.698 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 14.9291 [ms] |
| 2026-09-19 17:58:03.699 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:58:03.701 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.187968 [ms] |
| 2026-09-19 17:58:03.703 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:58:03.704 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:58:03.709 | INFO | Visibility | Clusters 2046 tested -> 1934 frustum, 1934 cone, 1924 visible | draws 1924+0, 216564 triangles |
| 2026-09-19 17:58:03.710 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:58:03.713 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1934 [count] |
| 2026-09-19 17:58:03.714 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1934 [count] |
| 2026-09-19 17:58:03.716 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1924 [count] |
| 2026-09-19 17:58:03.720 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 216564 [count] |
| 2026-09-19 17:58:03.724 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1924 [count] |
| 2026-09-19 17:58:03.725 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 59% of GPU frame |
| 2026-09-19 17:58:03.729 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:58:03.730 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:58:03.731 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:58:03.732 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:58:03.734 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:58:03.735 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:58:03.737 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 58.986 [percent] |
| 2026-09-19 17:58:08.690 | INFO | Performance | CPU 26.18 ms/frame (39.5 fps, worst 97.98 ms over 191 frames), RSS 823 MiB |
| 2026-09-19 17:58:08.692 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 26.1826 [ms] |
| 2026-09-19 17:58:08.694 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 97.9788 [ms] |
| 2026-09-19 17:58:08.696 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 39.4687 [fps] |
| 2026-09-19 17:58:08.697 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 191 [count] |
| 2026-09-19 17:58:08.699 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 822.777 [MiB] |
| 2026-09-19 17:58:08.703 | INFO | GpuTiming | GPU 22.71 ms total | cull 0.08 · raster 8.61 · HiZ 0.03 · resolve 0.59 · ReSTIR 13.40 · shadow 0.00 · post 0.19 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:58:08.707 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 22.7147 [ms] |
| 2026-09-19 17:58:08.709 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.083776 [ms] |
| 2026-09-19 17:58:08.711 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.60733 [ms] |
| 2026-09-19 17:58:08.713 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.03072 [ms] |
| 2026-09-19 17:58:08.714 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.593472 [ms] |
| 2026-09-19 17:58:08.716 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 13.3994 [ms] |
| 2026-09-19 17:58:08.720 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:58:08.722 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.185088 [ms] |
| 2026-09-19 17:58:08.724 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:58:08.725 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:58:08.726 | INFO | Visibility | Clusters 2046 tested -> 1934 frustum, 1934 cone, 1924 visible | draws 1924+0, 216564 triangles |
| 2026-09-19 17:58:08.728 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:58:08.729 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1934 [count] |
| 2026-09-19 17:58:08.730 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1934 [count] |
| 2026-09-19 17:58:08.732 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1924 [count] |
| 2026-09-19 17:58:08.736 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 216564 [count] |
| 2026-09-19 17:58:08.738 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1924 [count] |
| 2026-09-19 17:58:08.740 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 59% of GPU frame |
| 2026-09-19 17:58:08.742 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:58:08.744 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:58:08.746 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:58:08.747 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:58:08.751 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:58:08.753 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:58:08.755 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 58.99 [percent] |
| 2026-09-19 17:58:13.691 | INFO | Performance | CPU 25.42 ms/frame (39.8 fps, worst 100.00 ms over 197 frames), RSS 873 MiB |
| 2026-09-19 17:58:13.692 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 25.4176 [ms] |
| 2026-09-19 17:58:13.695 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:58:13.696 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 39.8375 [fps] |
| 2026-09-19 17:58:13.697 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 197 [count] |
| 2026-09-19 17:58:13.699 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 873.312 [MiB] |
| 2026-09-19 17:58:13.700 | INFO | GpuTiming | GPU 25.07 ms total | cull 0.08 · raster 8.62 · HiZ 0.03 · resolve 0.85 · ReSTIR 15.49 · shadow 0.00 · post 0.18 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:58:13.703 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 25.066 [ms] |
| 2026-09-19 17:58:13.705 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.082784 [ms] |
| 2026-09-19 17:58:13.706 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.61888 [ms] |
| 2026-09-19 17:58:13.708 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.031392 [ms] |
| 2026-09-19 17:58:13.710 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.847808 [ms] |
| 2026-09-19 17:58:13.711 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 15.4852 [ms] |
| 2026-09-19 17:58:13.715 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:58:13.719 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.184736 [ms] |
| 2026-09-19 17:58:13.721 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:58:13.722 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:58:13.724 | INFO | Visibility | Clusters 2046 tested -> 1934 frustum, 1934 cone, 1924 visible | draws 1924+0, 216564 triangles |
| 2026-09-19 17:58:13.725 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:58:13.726 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1934 [count] |
| 2026-09-19 17:58:13.727 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1934 [count] |
| 2026-09-19 17:58:13.729 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1924 [count] |
| 2026-09-19 17:58:13.730 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 216564 [count] |
| 2026-09-19 17:58:13.734 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1924 [count] |
| 2026-09-19 17:58:13.735 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 62% of GPU frame |
| 2026-09-19 17:58:13.739 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:58:13.741 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:58:13.742 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:58:13.744 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:58:13.745 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:58:13.750 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:58:13.751 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 61.7775 [percent] |
| 2026-09-19 17:58:18.719 | INFO | Performance | CPU 27.48 ms/frame (36.9 fps, worst 88.52 ms over 182 frames), RSS 919 MiB |
| 2026-09-19 17:58:18.720 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 27.4772 [ms] |
| 2026-09-19 17:58:18.725 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 88.5218 [ms] |
| 2026-09-19 17:58:18.727 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 36.8981 [fps] |
| 2026-09-19 17:58:18.729 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 182 [count] |
| 2026-09-19 17:58:18.731 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 918.73 [MiB] |
| 2026-09-19 17:58:18.736 | INFO | GpuTiming | GPU 25.70 ms total | cull 0.09 · raster 10.30 · HiZ 0.03 · resolve 0.70 · ReSTIR 14.58 · shadow 0.00 · post 0.19 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:58:18.740 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 25.6971 [ms] |
| 2026-09-19 17:58:18.742 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.089024 [ms] |
| 2026-09-19 17:58:18.744 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 10.3019 [ms] |
| 2026-09-19 17:58:18.752 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.03088 [ms] |
| 2026-09-19 17:58:18.754 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.698912 [ms] |
| 2026-09-19 17:58:18.756 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 14.5764 [ms] |
| 2026-09-19 17:58:18.758 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:58:18.760 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.1856 [ms] |
| 2026-09-19 17:58:18.761 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:58:18.763 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:58:18.768 | INFO | Visibility | Clusters 2046 tested -> 1934 frustum, 1934 cone, 1924 visible | draws 1924+0, 216564 triangles |
| 2026-09-19 17:58:18.770 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:58:18.773 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1934 [count] |
| 2026-09-19 17:58:18.775 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1934 [count] |
| 2026-09-19 17:58:18.777 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1924 [count] |
| 2026-09-19 17:58:18.779 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 216564 [count] |
| 2026-09-19 17:58:18.784 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1924 [count] |
| 2026-09-19 17:58:18.786 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 57% of GPU frame |
| 2026-09-19 17:58:18.791 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:58:18.793 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:58:18.794 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:58:18.800 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:58:18.802 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:58:18.804 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:58:18.806 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 56.7238 [percent] |
| 2026-09-19 17:58:23.814 | INFO | Performance | CPU 28.36 ms/frame (34.9 fps, worst 100.00 ms over 177 frames), RSS 937 MiB |
| 2026-09-19 17:58:23.816 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 28.3553 [ms] |
| 2026-09-19 17:58:23.820 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:58:23.827 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 34.9411 [fps] |
| 2026-09-19 17:58:23.830 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 177 [count] |
| 2026-09-19 17:58:23.832 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 936.973 [MiB] |
| 2026-09-19 17:58:23.840 | INFO | GpuTiming | GPU 23.38 ms total | cull 0.08 · raster 8.89 · HiZ 0.03 · resolve 0.70 · ReSTIR 13.68 · shadow 0.00 · post 0.19 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:58:23.846 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 23.3789 [ms] |
| 2026-09-19 17:58:23.849 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.080512 [ms] |
| 2026-09-19 17:58:23.855 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.88627 [ms] |
| 2026-09-19 17:58:23.860 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.03184 [ms] |
| 2026-09-19 17:58:23.861 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.696416 [ms] |
| 2026-09-19 17:58:23.863 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 13.6839 [ms] |
| 2026-09-19 17:58:23.866 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:58:23.872 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.187615 [ms] |
| 2026-09-19 17:58:23.876 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:58:23.879 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:58:23.881 | INFO | Visibility | Clusters 2046 tested -> 1934 frustum, 1934 cone, 1924 visible | draws 1924+0, 216564 triangles |
| 2026-09-19 17:58:23.886 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:58:23.918 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1934 [count] |
| 2026-09-19 17:58:23.926 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1934 [count] |
| 2026-09-19 17:58:23.927 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1924 [count] |
| 2026-09-19 17:58:23.931 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 216564 [count] |
| 2026-09-19 17:58:23.934 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1924 [count] |
| 2026-09-19 17:58:23.936 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 59% of GPU frame |
| 2026-09-19 17:58:23.940 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:58:23.947 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:58:23.949 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:58:23.950 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:58:23.952 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:58:23.960 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 17:58:23.964 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 58.5308 [percent] |
| 2026-09-19 17:58:28.898 | INFO | Performance | CPU 27.61 ms/frame (36.9 fps, worst 100.00 ms over 182 frames), RSS 947 MiB |
| 2026-09-19 17:58:28.899 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 27.6113 [ms] |
| 2026-09-19 17:58:28.901 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:58:28.904 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 36.9061 [fps] |
| 2026-09-19 17:58:28.908 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 182 [count] |
| 2026-09-19 17:58:28.911 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 947.273 [MiB] |
| 2026-09-19 17:58:28.913 | INFO | GpuTiming | GPU 26.07 ms total | cull 0.10 · raster 10.22 · HiZ 0.03 · resolve 0.85 · ReSTIR 14.87 · shadow 0.00 · post 0.18 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:58:28.916 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 26.0713 [ms] |
| 2026-09-19 17:58:28.918 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.100352 [ms] |
| 2026-09-19 17:58:28.920 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 10.221 [ms] |
| 2026-09-19 17:58:28.927 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.031104 [ms] |
| 2026-09-19 17:58:28.929 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.848096 [ms] |
| 2026-09-19 17:58:28.931 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 14.8708 [ms] |
| 2026-09-19 17:58:28.934 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:58:28.935 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.184095 [ms] |
| 2026-09-19 17:58:28.937 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:58:28.944 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:58:28.946 | INFO | Visibility | Clusters 2046 tested -> 1934 frustum, 1934 cone, 1924 visible | draws 1924+0, 216564 triangles |
| 2026-09-19 17:58:28.948 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:58:28.950 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1934 [count] |
| 2026-09-19 17:58:28.952 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1934 [count] |
| 2026-09-19 17:58:28.956 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1924 [count] |
| 2026-09-19 17:58:28.958 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 216564 [count] |
| 2026-09-19 17:58:28.959 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1924 [count] |
| 2026-09-19 17:58:28.961 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 57% of GPU frame |
| 2026-09-19 17:58:28.964 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:58:28.966 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:58:28.968 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:58:28.972 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:58:28.973 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:58:28.975 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:58:28.977 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 57.0389 [percent] |
| 2026-09-19 17:58:33.931 | INFO | Performance | CPU 26.35 ms/frame (38.1 fps, worst 88.32 ms over 191 frames), RSS 966 MiB |
| 2026-09-19 17:58:33.933 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 26.3476 [ms] |
| 2026-09-19 17:58:33.935 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 88.3168 [ms] |
| 2026-09-19 17:58:33.937 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 38.146 [fps] |
| 2026-09-19 17:58:33.938 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 191 [count] |
| 2026-09-19 17:58:33.941 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 965.637 [MiB] |
| 2026-09-19 17:58:33.943 | INFO | GpuTiming | GPU 22.52 ms total | cull 0.08 · raster 8.11 · HiZ 0.03 · resolve 0.60 · ReSTIR 13.70 · shadow 0.00 · post 0.18 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:58:33.949 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 22.5153 [ms] |
| 2026-09-19 17:58:33.951 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.081632 [ms] |
| 2026-09-19 17:58:33.953 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.1095 [ms] |
| 2026-09-19 17:58:33.955 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.030944 [ms] |
| 2026-09-19 17:58:33.956 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.596416 [ms] |
| 2026-09-19 17:58:33.958 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 13.6968 [ms] |
| 2026-09-19 17:58:33.963 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:58:33.965 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.184512 [ms] |
| 2026-09-19 17:58:33.967 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:58:33.969 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:58:33.971 | INFO | Visibility | Clusters 2046 tested -> 1934 frustum, 1934 cone, 1924 visible | draws 1924+0, 216564 triangles |
| 2026-09-19 17:58:33.973 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:58:33.974 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1934 [count] |
| 2026-09-19 17:58:33.981 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1934 [count] |
| 2026-09-19 17:58:33.983 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1924 [count] |
| 2026-09-19 17:58:33.985 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 216564 [count] |
| 2026-09-19 17:58:33.987 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1924 [count] |
| 2026-09-19 17:58:33.988 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 61% of GPU frame |
| 2026-09-19 17:58:33.992 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:58:33.996 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:58:33.998 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:58:34.000 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:58:34.001 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:58:34.003 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:58:34.005 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 60.8334 [percent] |
| 2026-09-19 17:58:38.969 | INFO | Performance | CPU 26.60 ms/frame (38.9 fps, worst 100.00 ms over 188 frames), RSS 966 MiB |
| 2026-09-19 17:58:38.971 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 26.6031 [ms] |
| 2026-09-19 17:58:38.974 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:58:38.976 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 38.8517 [fps] |
| 2026-09-19 17:58:38.977 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 188 [count] |
| 2026-09-19 17:58:38.980 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 965.75 [MiB] |
| 2026-09-19 17:58:38.982 | INFO | GpuTiming | GPU 23.68 ms total | cull 0.09 · raster 9.56 · HiZ 0.03 · resolve 0.75 · ReSTIR 13.24 · shadow 0.00 · post 0.18 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:58:38.986 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 23.6751 [ms] |
| 2026-09-19 17:58:38.992 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.092224 [ms] |
| 2026-09-19 17:58:38.996 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.55526 [ms] |
| 2026-09-19 17:58:38.998 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.030688 [ms] |
| 2026-09-19 17:58:39.000 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.754368 [ms] |
| 2026-09-19 17:58:39.005 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 13.2426 [ms] |
| 2026-09-19 17:58:39.007 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:58:39.009 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.18368 [ms] |
| 2026-09-19 17:58:39.012 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:58:39.015 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:58:39.019 | INFO | Visibility | Clusters 2046 tested -> 1934 frustum, 1934 cone, 1924 visible | draws 1924+0, 216564 triangles |
| 2026-09-19 17:58:39.021 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:58:39.023 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1934 [count] |
| 2026-09-19 17:58:39.025 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1934 [count] |
| 2026-09-19 17:58:39.027 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1924 [count] |
| 2026-09-19 17:58:39.029 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 216564 [count] |
| 2026-09-19 17:58:39.030 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1924 [count] |
| 2026-09-19 17:58:39.035 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 56% of GPU frame |
| 2026-09-19 17:58:39.039 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:58:39.040 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:58:39.042 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:58:39.045 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:58:39.047 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:58:39.051 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:58:39.054 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 55.9345 [percent] |
| 2026-09-19 17:58:44.012 | INFO | Performance | CPU 25.43 ms/frame (40.2 fps, worst 100.00 ms over 197 frames), RSS 1006 MiB |
| 2026-09-19 17:58:44.014 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 25.4313 [ms] |
| 2026-09-19 17:58:44.016 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:58:44.018 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 40.1891 [fps] |
| 2026-09-19 17:58:44.021 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 197 [count] |
| 2026-09-19 17:58:44.022 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1005.64 [MiB] |
| 2026-09-19 17:58:44.024 | INFO | GpuTiming | GPU 22.64 ms total | cull 0.08 · raster 8.82 · HiZ 0.03 · resolve 0.65 · ReSTIR 13.05 · shadow 0.00 · post 0.18 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:58:44.028 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 22.6366 [ms] |
| 2026-09-19 17:58:44.034 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.084384 [ms] |
| 2026-09-19 17:58:44.036 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.8215 [ms] |
| 2026-09-19 17:58:44.038 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.031264 [ms] |
| 2026-09-19 17:58:44.040 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.645152 [ms] |
| 2026-09-19 17:58:44.042 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 13.0543 [ms] |
| 2026-09-19 17:58:44.044 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:58:44.049 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.183104 [ms] |
| 2026-09-19 17:58:44.051 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:58:44.052 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:58:44.054 | INFO | Visibility | Clusters 2046 tested -> 1934 frustum, 1934 cone, 1924 visible | draws 1924+0, 216564 triangles |
| 2026-09-19 17:58:44.055 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:58:44.057 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1934 [count] |
| 2026-09-19 17:58:44.058 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1934 [count] |
| 2026-09-19 17:58:44.060 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1924 [count] |
| 2026-09-19 17:58:44.065 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 216564 [count] |
| 2026-09-19 17:58:44.067 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1924 [count] |
| 2026-09-19 17:58:44.069 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 58% of GPU frame |
| 2026-09-19 17:58:44.071 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:58:44.073 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:58:44.074 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:58:44.076 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:58:44.082 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:58:44.085 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:58:44.087 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 57.669 [percent] |
| 2026-09-19 17:58:49.014 | INFO | Performance | CPU 26.05 ms/frame (37.9 fps, worst 100.00 ms over 192 frames), RSS 1107 MiB |
| 2026-09-19 17:58:49.016 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 26.0514 [ms] |
| 2026-09-19 17:58:49.018 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:58:49.021 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 37.9429 [fps] |
| 2026-09-19 17:58:49.023 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 192 [count] |
| 2026-09-19 17:58:49.026 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1106.77 [MiB] |
| 2026-09-19 17:58:49.030 | INFO | GpuTiming | GPU 25.63 ms total | cull 0.08 · raster 6.00 · HiZ 0.03 · resolve 0.57 · ReSTIR 18.95 · shadow 0.00 · post 0.30 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:58:49.037 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 25.6269 [ms] |
| 2026-09-19 17:58:49.040 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.07712 [ms] |
| 2026-09-19 17:58:49.042 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 5.99658 [ms] |
| 2026-09-19 17:58:49.044 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.031136 [ms] |
| 2026-09-19 17:58:49.046 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.567872 [ms] |
| 2026-09-19 17:58:49.051 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 18.9542 [ms] |
| 2026-09-19 17:58:49.053 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:58:49.056 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.296576 [ms] |
| 2026-09-19 17:58:49.057 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:58:49.059 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:58:49.061 | INFO | Visibility | Clusters 2046 tested -> 1194 frustum, 1194 cone, 1194 visible | draws 1194+0, 134728 triangles |
| 2026-09-19 17:58:49.063 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:58:49.069 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1194 [count] |
| 2026-09-19 17:58:49.071 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1194 [count] |
| 2026-09-19 17:58:49.073 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1194 [count] |
| 2026-09-19 17:58:49.075 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 134728 [count] |
| 2026-09-19 17:58:49.076 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1194 [count] |
| 2026-09-19 17:58:49.078 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 74% of GPU frame |
| 2026-09-19 17:58:49.085 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:58:49.086 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:58:49.088 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:58:49.089 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:58:49.091 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:58:49.092 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:58:49.094 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 73.9621 [percent] |
| 2026-09-19 17:58:54.078 | INFO | Performance | CPU 26.18 ms/frame (37.8 fps, worst 100.00 ms over 192 frames), RSS 789 MiB |
| 2026-09-19 17:58:54.080 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 26.1779 [ms] |
| 2026-09-19 17:58:54.083 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:58:54.084 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 37.7861 [fps] |
| 2026-09-19 17:58:54.086 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 192 [count] |
| 2026-09-19 17:58:54.087 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 788.789 [MiB] |
| 2026-09-19 17:58:54.089 | INFO | GpuTiming | GPU 25.79 ms total | cull 0.08 · raster 8.36 · HiZ 0.03 · resolve 0.84 · ReSTIR 16.47 · shadow 0.00 · post 0.26 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:58:54.093 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 25.7852 [ms] |
| 2026-09-19 17:58:54.094 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.083264 [ms] |
| 2026-09-19 17:58:54.100 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.35923 [ms] |
| 2026-09-19 17:58:54.102 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.03264 [ms] |
| 2026-09-19 17:58:54.105 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.842432 [ms] |
| 2026-09-19 17:58:54.106 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 16.4676 [ms] |
| 2026-09-19 17:58:54.112 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:58:54.114 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.262497 [ms] |
| 2026-09-19 17:58:54.116 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:58:54.118 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:58:54.119 | INFO | Visibility | Clusters 2046 tested -> 1919 frustum, 1919 cone, 1919 visible | draws 1919+0, 215776 triangles |
| 2026-09-19 17:58:54.120 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:58:54.122 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1919 [count] |
| 2026-09-19 17:58:54.123 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1919 [count] |
| 2026-09-19 17:58:54.124 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1919 [count] |
| 2026-09-19 17:58:54.128 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 215776 [count] |
| 2026-09-19 17:58:54.130 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1919 [count] |
| 2026-09-19 17:58:54.131 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 64% of GPU frame |
| 2026-09-19 17:58:54.134 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:58:54.136 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:58:54.137 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:58:54.139 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:58:54.140 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:58:54.144 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:58:54.146 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 63.8646 [percent] |
| 2026-09-19 17:58:59.270 | INFO | Performance | CPU 29.59 ms/frame (33.1 fps, worst 100.00 ms over 170 frames), RSS 789 MiB |
| 2026-09-19 17:58:59.271 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 29.5898 [ms] |
| 2026-09-19 17:58:59.273 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:58:59.275 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 33.0712 [fps] |
| 2026-09-19 17:58:59.277 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 170 [count] |
| 2026-09-19 17:58:59.279 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 788.855 [MiB] |
| 2026-09-19 17:58:59.280 | INFO | GpuTiming | GPU 28.54 ms total | cull 0.09 · raster 9.34 · HiZ 0.03 · resolve 0.88 · ReSTIR 18.21 · shadow 0.00 · post 0.22 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:58:59.282 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 28.5399 [ms] |
| 2026-09-19 17:58:59.285 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.088576 [ms] |
| 2026-09-19 17:58:59.286 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.33667 [ms] |
| 2026-09-19 17:58:59.287 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.032416 [ms] |
| 2026-09-19 17:58:59.289 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.87712 [ms] |
| 2026-09-19 17:58:59.291 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 18.2051 [ms] |
| 2026-09-19 17:58:59.294 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:58:59.299 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.219744 [ms] |
| 2026-09-19 17:58:59.302 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:58:59.304 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:58:59.306 | INFO | Visibility | Clusters 2046 tested -> 1919 frustum, 1919 cone, 1919 visible | draws 1919+0, 215776 triangles |
| 2026-09-19 17:58:59.308 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:58:59.311 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1919 [count] |
| 2026-09-19 17:58:59.313 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1919 [count] |
| 2026-09-19 17:58:59.317 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1919 [count] |
| 2026-09-19 17:58:59.319 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 215776 [count] |
| 2026-09-19 17:58:59.321 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1919 [count] |
| 2026-09-19 17:58:59.328 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 64% of GPU frame |
| 2026-09-19 17:58:59.331 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:58:59.332 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:58:59.335 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:58:59.336 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:58:59.337 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:58:59.339 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:58:59.340 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 63.7883 [percent] |
| 2026-09-19 17:59:04.525 | INFO | Performance | CPU 32.85 ms/frame (29.9 fps, worst 100.00 ms over 153 frames), RSS 789 MiB |
| 2026-09-19 17:59:04.527 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 32.8462 [ms] |
| 2026-09-19 17:59:04.531 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:59:04.534 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 29.8941 [fps] |
| 2026-09-19 17:59:04.543 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 153 [count] |
| 2026-09-19 17:59:04.549 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 788.855 [MiB] |
| 2026-09-19 17:59:04.553 | INFO | GpuTiming | GPU 18.12 ms total | cull 0.07 · raster 5.54 · HiZ 0.03 · resolve 0.48 · ReSTIR 12.00 · shadow 0.00 · post 0.27 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:59:04.561 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 18.1173 [ms] |
| 2026-09-19 17:59:04.563 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.070816 [ms] |
| 2026-09-19 17:59:04.566 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 5.53971 [ms] |
| 2026-09-19 17:59:04.569 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.030784 [ms] |
| 2026-09-19 17:59:04.575 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.477504 [ms] |
| 2026-09-19 17:59:04.578 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 11.9985 [ms] |
| 2026-09-19 17:59:04.582 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:59:04.585 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.26832 [ms] |
| 2026-09-19 17:59:04.590 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:59:04.593 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:59:04.596 | INFO | Visibility | Clusters 2046 tested -> 1308 frustum, 1308 cone, 1308 visible | draws 1298+10, 146648 triangles |
| 2026-09-19 17:59:04.598 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:59:04.601 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1308 [count] |
| 2026-09-19 17:59:04.607 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1308 [count] |
| 2026-09-19 17:59:04.610 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1308 [count] |
| 2026-09-19 17:59:04.613 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 146648 [count] |
| 2026-09-19 17:59:04.615 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1308 [count] |
| 2026-09-19 17:59:04.622 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 66% of GPU frame |
| 2026-09-19 17:59:04.627 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:59:04.630 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:59:04.637 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:59:04.641 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:59:04.644 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:59:04.646 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 17:59:04.652 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 66.2266 [percent] |
| 2026-09-19 17:59:09.577 | INFO | Performance | CPU 17.87 ms/frame (58.6 fps, worst 100.00 ms over 280 frames), RSS 789 MiB |
| 2026-09-19 17:59:09.728 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 17.8701 [ms] |
| 2026-09-19 17:59:09.731 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:59:09.733 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 58.622 [fps] |
| 2026-09-19 17:59:09.736 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 280 [count] |
| 2026-09-19 17:59:09.744 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 788.855 [MiB] |
| 2026-09-19 17:59:09.746 | INFO | GpuTiming | GPU 1.26 ms total | cull 0.06 · raster 0.03 · HiZ 0.03 · resolve 0.02 · ReSTIR 1.12 · shadow 0.00 · post 0.13 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:59:09.750 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 1.25555 [ms] |
| 2026-09-19 17:59:09.752 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.062208 [ms] |
| 2026-09-19 17:59:09.757 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 0.027072 [ms] |
| 2026-09-19 17:59:09.759 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.029088 [ms] |
| 2026-09-19 17:59:09.761 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.01616 [ms] |
| 2026-09-19 17:59:09.764 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 1.12102 [ms] |
| 2026-09-19 17:59:09.765 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:59:09.767 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.12848 [ms] |
| 2026-09-19 17:59:09.775 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:59:09.778 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:59:09.780 | INFO | Visibility | Clusters 2046 tested -> 7 frustum, 7 cone, 7 visible | draws 7+0, 254 triangles |
| 2026-09-19 17:59:09.782 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:59:09.790 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 7 [count] |
| 2026-09-19 17:59:09.792 | INFO | TelemetryMetrics | Measurement: ClustersCone = 7 [count] |
| 2026-09-19 17:59:09.795 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 7 [count] |
| 2026-09-19 17:59:09.796 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 254 [count] |
| 2026-09-19 17:59:09.799 | INFO | TelemetryMetrics | Measurement: DrawCalls = 7 [count] |
| 2026-09-19 17:59:09.802 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 89% of GPU frame |
| 2026-09-19 17:59:09.809 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:59:09.811 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:59:09.813 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:59:09.815 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:59:09.819 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:59:09.822 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 17:59:09.823 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 89.2853 [percent] |
| 2026-09-19 17:59:14.756 | INFO | Performance | CPU 17.69 ms/frame (54.7 fps, worst 100.00 ms over 283 frames), RSS 789 MiB |
| 2026-09-19 17:59:14.757 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 17.6927 [ms] |
| 2026-09-19 17:59:14.759 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:59:14.760 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 54.6834 [fps] |
| 2026-09-19 17:59:14.760 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 283 [count] |
| 2026-09-19 17:59:14.761 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 788.918 [MiB] |
| 2026-09-19 17:59:14.762 | INFO | GpuTiming | GPU 22.27 ms total | cull 0.07 · raster 6.00 · HiZ 0.03 · resolve 0.52 · ReSTIR 15.65 · shadow 0.00 · post 0.38 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:59:14.765 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 22.2726 [ms] |
| 2026-09-19 17:59:14.768 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.065792 [ms] |
| 2026-09-19 17:59:14.770 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 5.99795 [ms] |
| 2026-09-19 17:59:14.773 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.030688 [ms] |
| 2026-09-19 17:59:14.775 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.524 [ms] |
| 2026-09-19 17:59:14.777 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 15.6541 [ms] |
| 2026-09-19 17:59:14.778 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:59:14.784 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.38448 [ms] |
| 2026-09-19 17:59:14.786 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:59:14.788 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:59:14.791 | INFO | Visibility | Clusters 2046 tested -> 1317 frustum, 1317 cone, 1300 visible | draws 978+322, 146948 triangles |
| 2026-09-19 17:59:14.794 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:59:14.796 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1317 [count] |
| 2026-09-19 17:59:14.801 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1317 [count] |
| 2026-09-19 17:59:14.803 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1300 [count] |
| 2026-09-19 17:59:14.804 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 146948 [count] |
| 2026-09-19 17:59:14.807 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1300 [count] |
| 2026-09-19 17:59:14.808 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 70% of GPU frame |
| 2026-09-19 17:59:14.810 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:59:14.814 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:59:14.815 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:59:14.816 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:59:14.818 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:59:14.819 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:59:14.820 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 70.2844 [percent] |
| 2026-09-19 17:59:19.787 | INFO | Performance | CPU 25.75 ms/frame (39.6 fps, worst 88.49 ms over 196 frames), RSS 789 MiB |
| 2026-09-19 17:59:19.789 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 25.7499 [ms] |
| 2026-09-19 17:59:19.793 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 88.4873 [ms] |
| 2026-09-19 17:59:19.795 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 39.5981 [fps] |
| 2026-09-19 17:59:19.797 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 196 [count] |
| 2026-09-19 17:59:19.801 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 788.918 [MiB] |
| 2026-09-19 17:59:19.803 | INFO | GpuTiming | GPU 22.61 ms total | cull 0.09 · raster 8.86 · HiZ 0.03 · resolve 0.43 · ReSTIR 13.20 · shadow 0.00 · post 0.22 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:59:19.808 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 22.6106 [ms] |
| 2026-09-19 17:59:19.811 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.091104 [ms] |
| 2026-09-19 17:59:19.812 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.85642 [ms] |
| 2026-09-19 17:59:19.813 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.032384 [ms] |
| 2026-09-19 17:59:19.813 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.43344 [ms] |
| 2026-09-19 17:59:19.814 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 13.1972 [ms] |
| 2026-09-19 17:59:19.817 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:59:19.817 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.217024 [ms] |
| 2026-09-19 17:59:19.819 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:59:19.819 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:59:19.821 | INFO | Visibility | Clusters 2046 tested -> 2000 frustum, 2000 cone, 2000 visible | draws 2000+0, 224858 triangles |
| 2026-09-19 17:59:19.822 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:59:19.824 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 2000 [count] |
| 2026-09-19 17:59:19.824 | INFO | TelemetryMetrics | Measurement: ClustersCone = 2000 [count] |
| 2026-09-19 17:59:19.826 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 2000 [count] |
| 2026-09-19 17:59:19.827 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 224858 [count] |
| 2026-09-19 17:59:19.829 | INFO | TelemetryMetrics | Measurement: DrawCalls = 2000 [count] |
| 2026-09-19 17:59:19.831 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 58% of GPU frame |
| 2026-09-19 17:59:19.834 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:59:19.835 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:59:19.836 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:59:19.837 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:59:19.838 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:59:19.838 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:59:19.839 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 58.3675 [percent] |
| 2026-09-19 17:59:24.812 | INFO | Performance | CPU 25.61 ms/frame (38.6 fps, worst 61.11 ms over 196 frames), RSS 789 MiB |
| 2026-09-19 17:59:24.816 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 25.6131 [ms] |
| 2026-09-19 17:59:24.819 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 61.1069 [ms] |
| 2026-09-19 17:59:24.821 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 38.6285 [fps] |
| 2026-09-19 17:59:24.824 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 196 [count] |
| 2026-09-19 17:59:24.825 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 788.926 [MiB] |
| 2026-09-19 17:59:24.828 | INFO | GpuTiming | GPU 21.97 ms total | cull 0.10 · raster 8.38 · HiZ 0.03 · resolve 0.45 · ReSTIR 13.01 · shadow 0.00 · post 0.19 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:59:24.831 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 21.9737 [ms] |
| 2026-09-19 17:59:24.833 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.09952 [ms] |
| 2026-09-19 17:59:24.836 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.38214 [ms] |
| 2026-09-19 17:59:24.838 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.031424 [ms] |
| 2026-09-19 17:59:24.840 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.448512 [ms] |
| 2026-09-19 17:59:24.843 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 13.0121 [ms] |
| 2026-09-19 17:59:24.844 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:59:24.846 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.193184 [ms] |
| 2026-09-19 17:59:24.847 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:59:24.848 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:59:24.849 | INFO | Visibility | Clusters 2046 tested -> 1981 frustum, 1981 cone, 1981 visible | draws 1981+0, 222420 triangles |
| 2026-09-19 17:59:24.852 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:59:24.854 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1981 [count] |
| 2026-09-19 17:59:24.856 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1981 [count] |
| 2026-09-19 17:59:24.857 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1981 [count] |
| 2026-09-19 17:59:24.859 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 222420 [count] |
| 2026-09-19 17:59:24.860 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1981 [count] |
| 2026-09-19 17:59:24.861 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 59% of GPU frame |
| 2026-09-19 17:59:24.863 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:59:24.863 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:59:24.864 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:59:24.865 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:59:24.868 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:59:24.869 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:59:24.870 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 59.2166 [percent] |
| 2026-09-19 17:59:29.848 | INFO | Performance | CPU 24.91 ms/frame (40.8 fps, worst 100.00 ms over 202 frames), RSS 790 MiB |
| 2026-09-19 17:59:29.849 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 24.911 [ms] |
| 2026-09-19 17:59:29.851 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 17:59:29.853 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 40.7529 [fps] |
| 2026-09-19 17:59:29.854 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 202 [count] |
| 2026-09-19 17:59:29.855 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 789.938 [MiB] |
| 2026-09-19 17:59:29.857 | INFO | GpuTiming | GPU 23.42 ms total | cull 0.09 · raster 8.49 · HiZ 0.03 · resolve 0.43 · ReSTIR 14.38 · shadow 0.00 · post 0.21 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:59:29.863 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 23.4232 [ms] |
| 2026-09-19 17:59:29.866 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.088096 [ms] |
| 2026-09-19 17:59:29.867 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.49034 [ms] |
| 2026-09-19 17:59:29.868 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.031488 [ms] |
| 2026-09-19 17:59:29.870 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.429152 [ms] |
| 2026-09-19 17:59:29.871 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 14.3841 [ms] |
| 2026-09-19 17:59:29.872 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:59:29.880 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.212384 [ms] |
| 2026-09-19 17:59:29.881 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:59:29.883 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:59:29.884 | INFO | Visibility | Clusters 2046 tested -> 1981 frustum, 1981 cone, 1981 visible | draws 1981+0, 222420 triangles |
| 2026-09-19 17:59:29.886 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:59:29.887 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1981 [count] |
| 2026-09-19 17:59:29.891 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1981 [count] |
| 2026-09-19 17:59:29.895 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1981 [count] |
| 2026-09-19 17:59:29.896 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 222420 [count] |
| 2026-09-19 17:59:29.897 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1981 [count] |
| 2026-09-19 17:59:29.899 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 61% of GPU frame |
| 2026-09-19 17:59:29.902 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:59:29.904 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:59:29.918 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:59:29.918 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:59:29.920 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:59:29.921 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:59:29.922 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 61.4097 [percent] |
| 2026-09-19 17:59:34.905 | INFO | Performance | CPU 24.00 ms/frame (41.6 fps, worst 81.44 ms over 209 frames), RSS 790 MiB |
| 2026-09-19 17:59:34.906 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 24.0035 [ms] |
| 2026-09-19 17:59:34.907 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 81.4442 [ms] |
| 2026-09-19 17:59:34.908 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 41.5789 [fps] |
| 2026-09-19 17:59:34.909 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 209 [count] |
| 2026-09-19 17:59:34.910 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 789.957 [MiB] |
| 2026-09-19 17:59:34.911 | INFO | GpuTiming | GPU 22.16 ms total | cull 0.11 · raster 9.31 · HiZ 0.03 · resolve 0.43 · ReSTIR 12.28 · shadow 0.00 · post 0.22 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:59:34.913 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 22.1635 [ms] |
| 2026-09-19 17:59:34.914 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.110048 [ms] |
| 2026-09-19 17:59:34.915 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.31021 [ms] |
| 2026-09-19 17:59:34.915 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.032768 [ms] |
| 2026-09-19 17:59:34.916 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.432128 [ms] |
| 2026-09-19 17:59:34.918 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 12.2784 [ms] |
| 2026-09-19 17:59:34.918 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:59:34.920 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.218144 [ms] |
| 2026-09-19 17:59:34.921 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:59:34.925 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:59:34.926 | INFO | Visibility | Clusters 2046 tested -> 1981 frustum, 1981 cone, 1981 visible | draws 1981+0, 222420 triangles |
| 2026-09-19 17:59:34.929 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:59:34.930 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1981 [count] |
| 2026-09-19 17:59:34.932 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1981 [count] |
| 2026-09-19 17:59:34.933 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1981 [count] |
| 2026-09-19 17:59:34.935 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 222420 [count] |
| 2026-09-19 17:59:34.936 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1981 [count] |
| 2026-09-19 17:59:34.940 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 55% of GPU frame |
| 2026-09-19 17:59:34.942 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:59:34.944 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:59:34.946 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:59:34.948 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:59:34.949 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:59:34.950 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:59:34.952 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 55.399 [percent] |
| 2026-09-19 17:59:39.890 | INFO | Performance | CPU 23.85 ms/frame (42.9 fps, worst 94.11 ms over 210 frames), RSS 790 MiB |
| 2026-09-19 17:59:39.892 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 23.8504 [ms] |
| 2026-09-19 17:59:39.894 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 94.1116 [ms] |
| 2026-09-19 17:59:39.896 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 42.8971 [fps] |
| 2026-09-19 17:59:39.898 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 210 [count] |
| 2026-09-19 17:59:39.899 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 789.977 [MiB] |
| 2026-09-19 17:59:39.901 | INFO | GpuTiming | GPU 22.48 ms total | cull 0.11 · raster 8.68 · HiZ 0.03 · resolve 0.44 · ReSTIR 13.21 · shadow 0.00 · post 0.18 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:59:39.905 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 22.4775 [ms] |
| 2026-09-19 17:59:39.906 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.111008 [ms] |
| 2026-09-19 17:59:39.908 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.68499 [ms] |
| 2026-09-19 17:59:39.909 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.032768 [ms] |
| 2026-09-19 17:59:39.911 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.442752 [ms] |
| 2026-09-19 17:59:39.913 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 13.206 [ms] |
| 2026-09-19 17:59:39.918 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:59:39.919 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.179935 [ms] |
| 2026-09-19 17:59:39.920 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:59:39.922 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:59:39.923 | INFO | Visibility | Clusters 2046 tested -> 1981 frustum, 1981 cone, 1981 visible | draws 1981+0, 222420 triangles |
| 2026-09-19 17:59:39.924 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:59:39.925 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1981 [count] |
| 2026-09-19 17:59:39.926 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1981 [count] |
| 2026-09-19 17:59:39.927 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1981 [count] |
| 2026-09-19 17:59:39.929 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 222420 [count] |
| 2026-09-19 17:59:39.933 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1981 [count] |
| 2026-09-19 17:59:39.934 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 59% of GPU frame |
| 2026-09-19 17:59:39.937 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:59:39.939 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:59:39.940 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:59:39.941 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:59:39.942 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:59:39.943 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:59:39.947 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 58.752 [percent] |
| 2026-09-19 17:59:44.901 | INFO | Performance | CPU 23.97 ms/frame (42.3 fps, worst 81.35 ms over 209 frames), RSS 790 MiB |
| 2026-09-19 17:59:44.902 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 23.975 [ms] |
| 2026-09-19 17:59:44.904 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 81.3542 [ms] |
| 2026-09-19 17:59:44.905 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 42.3187 [fps] |
| 2026-09-19 17:59:44.905 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 209 [count] |
| 2026-09-19 17:59:44.906 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 790.035 [MiB] |
| 2026-09-19 17:59:44.907 | INFO | GpuTiming | GPU 24.98 ms total | cull 0.08 · raster 9.79 · HiZ 0.03 · resolve 0.45 · ReSTIR 14.63 · shadow 0.00 · post 0.18 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:59:44.908 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 24.9837 [ms] |
| 2026-09-19 17:59:44.909 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.08464 [ms] |
| 2026-09-19 17:59:44.909 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.78848 [ms] |
| 2026-09-19 17:59:44.910 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.03072 [ms] |
| 2026-09-19 17:59:44.911 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.451168 [ms] |
| 2026-09-19 17:59:44.912 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 14.6287 [ms] |
| 2026-09-19 17:59:44.914 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:59:44.915 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.180352 [ms] |
| 2026-09-19 17:59:44.919 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:59:44.920 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:59:44.922 | INFO | Visibility | Clusters 2046 tested -> 1981 frustum, 1981 cone, 1981 visible | draws 1981+0, 222420 triangles |
| 2026-09-19 17:59:44.923 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:59:44.924 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1981 [count] |
| 2026-09-19 17:59:44.925 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1981 [count] |
| 2026-09-19 17:59:44.926 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1981 [count] |
| 2026-09-19 17:59:44.927 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 222420 [count] |
| 2026-09-19 17:59:44.928 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1981 [count] |
| 2026-09-19 17:59:44.928 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 59% of GPU frame |
| 2026-09-19 17:59:44.931 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:59:44.933 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:59:44.934 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:59:44.935 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:59:44.936 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:59:44.937 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:59:44.938 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 58.553 [percent] |
| 2026-09-19 17:59:49.909 | INFO | Performance | CPU 24.06 ms/frame (41.3 fps, worst 68.20 ms over 208 frames), RSS 790 MiB |
| 2026-09-19 17:59:49.911 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 24.0621 [ms] |
| 2026-09-19 17:59:49.915 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 68.1975 [ms] |
| 2026-09-19 17:59:49.917 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 41.31 [fps] |
| 2026-09-19 17:59:49.918 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 208 [count] |
| 2026-09-19 17:59:49.921 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 790.035 [MiB] |
| 2026-09-19 17:59:49.928 | INFO | GpuTiming | GPU 22.48 ms total | cull 0.11 · raster 9.02 · HiZ 0.03 · resolve 0.43 · ReSTIR 12.88 · shadow 0.00 · post 0.18 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:59:49.933 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 22.4784 [ms] |
| 2026-09-19 17:59:49.935 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.110784 [ms] |
| 2026-09-19 17:59:49.936 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.0193 [ms] |
| 2026-09-19 17:59:49.937 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.033504 [ms] |
| 2026-09-19 17:59:49.938 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.432128 [ms] |
| 2026-09-19 17:59:49.941 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 12.8827 [ms] |
| 2026-09-19 17:59:49.942 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:59:49.943 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.17744 [ms] |
| 2026-09-19 17:59:49.944 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:59:49.946 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:59:49.946 | INFO | Visibility | Clusters 2046 tested -> 1981 frustum, 1981 cone, 1981 visible | draws 1981+0, 222420 triangles |
| 2026-09-19 17:59:49.949 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:59:49.949 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1981 [count] |
| 2026-09-19 17:59:49.952 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1981 [count] |
| 2026-09-19 17:59:49.956 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1981 [count] |
| 2026-09-19 17:59:49.957 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 222420 [count] |
| 2026-09-19 17:59:49.959 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1981 [count] |
| 2026-09-19 17:59:49.961 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 57% of GPU frame |
| 2026-09-19 17:59:49.963 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:59:49.964 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:59:49.965 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:59:49.966 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:59:49.968 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:59:49.969 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:59:49.972 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 57.3114 [percent] |
| 2026-09-19 17:59:54.927 | INFO | Performance | CPU 24.14 ms/frame (42.4 fps, worst 90.34 ms over 208 frames), RSS 790 MiB |
| 2026-09-19 17:59:54.928 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 24.1423 [ms] |
| 2026-09-19 17:59:54.929 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 90.3391 [ms] |
| 2026-09-19 17:59:54.930 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 42.3686 [fps] |
| 2026-09-19 17:59:54.931 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 208 [count] |
| 2026-09-19 17:59:54.933 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 790.035 [MiB] |
| 2026-09-19 17:59:54.934 | INFO | GpuTiming | GPU 25.53 ms total | cull 0.09 · raster 10.00 · HiZ 0.03 · resolve 0.59 · ReSTIR 14.82 · shadow 0.00 · post 0.18 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:59:54.936 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 25.5304 [ms] |
| 2026-09-19 17:59:54.938 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.087584 [ms] |
| 2026-09-19 17:59:54.939 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.99834 [ms] |
| 2026-09-19 17:59:54.940 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.030848 [ms] |
| 2026-09-19 17:59:54.941 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.59392 [ms] |
| 2026-09-19 17:59:54.943 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 14.8197 [ms] |
| 2026-09-19 17:59:54.944 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:59:54.946 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.1776 [ms] |
| 2026-09-19 17:59:54.947 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:59:54.949 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:59:54.951 | INFO | Visibility | Clusters 2046 tested -> 1981 frustum, 1981 cone, 1981 visible | draws 1981+0, 222420 triangles |
| 2026-09-19 17:59:54.954 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:59:54.955 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1981 [count] |
| 2026-09-19 17:59:54.956 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1981 [count] |
| 2026-09-19 17:59:54.957 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1981 [count] |
| 2026-09-19 17:59:54.958 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 222420 [count] |
| 2026-09-19 17:59:54.959 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1981 [count] |
| 2026-09-19 17:59:54.960 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 58% of GPU frame |
| 2026-09-19 17:59:54.962 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:59:54.963 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:59:54.964 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:59:54.966 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:59:54.967 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:59:54.970 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:59:54.971 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 58.0474 [percent] |
| 2026-09-19 17:59:59.937 | INFO | Performance | CPU 24.08 ms/frame (41.6 fps, worst 68.54 ms over 208 frames), RSS 790 MiB |
| 2026-09-19 17:59:59.939 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 24.0759 [ms] |
| 2026-09-19 17:59:59.941 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 68.5368 [ms] |
| 2026-09-19 17:59:59.945 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 41.5806 [fps] |
| 2026-09-19 17:59:59.949 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 208 [count] |
| 2026-09-19 17:59:59.951 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 790.043 [MiB] |
| 2026-09-19 17:59:59.952 | INFO | GpuTiming | GPU 21.63 ms total | cull 0.09 · raster 9.05 · HiZ 0.03 · resolve 0.45 · ReSTIR 12.01 · shadow 0.00 · post 0.18 · sky 0.00 · volume 0.00 |
| 2026-09-19 17:59:59.955 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 21.6336 [ms] |
| 2026-09-19 17:59:59.956 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.086464 [ms] |
| 2026-09-19 17:59:59.958 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.05286 [ms] |
| 2026-09-19 17:59:59.961 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.03072 [ms] |
| 2026-09-19 17:59:59.963 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.4536 [ms] |
| 2026-09-19 17:59:59.964 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 12.01 [ms] |
| 2026-09-19 17:59:59.965 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 17:59:59.967 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.183552 [ms] |
| 2026-09-19 17:59:59.969 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 17:59:59.970 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 17:59:59.972 | INFO | Visibility | Clusters 2046 tested -> 1981 frustum, 1981 cone, 1981 visible | draws 1981+0, 222420 triangles |
| 2026-09-19 17:59:59.973 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 17:59:59.977 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1981 [count] |
| 2026-09-19 17:59:59.978 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1981 [count] |
| 2026-09-19 17:59:59.981 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1981 [count] |
| 2026-09-19 17:59:59.982 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 222420 [count] |
| 2026-09-19 17:59:59.985 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1981 [count] |
| 2026-09-19 17:59:59.986 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 56% of GPU frame |
| 2026-09-19 17:59:59.988 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 17:59:59.989 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 17:59:59.993 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 17:59:59.994 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 17:59:59.995 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 17:59:59.996 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 17:59:59.997 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 55.5153 [percent] |
| 2026-09-19 18:00:04.956 | INFO | Performance | CPU 23.80 ms/frame (41.9 fps, worst 89.04 ms over 211 frames), RSS 790 MiB |
| 2026-09-19 18:00:04.958 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 23.8027 [ms] |
| 2026-09-19 18:00:04.960 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 89.0418 [ms] |
| 2026-09-19 18:00:04.961 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 41.8925 [fps] |
| 2026-09-19 18:00:04.963 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 211 [count] |
| 2026-09-19 18:00:04.965 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 790.062 [MiB] |
| 2026-09-19 18:00:04.969 | INFO | GpuTiming | GPU 21.12 ms total | cull 0.12 · raster 8.86 · HiZ 0.03 · resolve 0.40 · ReSTIR 11.71 · shadow 0.00 · post 0.33 · sky 0.00 · volume 0.00 |
| 2026-09-19 18:00:04.973 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 21.1216 [ms] |
| 2026-09-19 18:00:04.974 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.116608 [ms] |
| 2026-09-19 18:00:04.977 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.86291 [ms] |
| 2026-09-19 18:00:04.978 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.033376 [ms] |
| 2026-09-19 18:00:04.980 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.400032 [ms] |
| 2026-09-19 18:00:04.985 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 11.7086 [ms] |
| 2026-09-19 18:00:04.987 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 18:00:04.989 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.331552 [ms] |
| 2026-09-19 18:00:04.990 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 18:00:04.992 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 18:00:04.993 | INFO | Visibility | Clusters 2046 tested -> 1996 frustum, 1996 cone, 1996 visible | draws 1996+0, 224518 triangles |
| 2026-09-19 18:00:04.996 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 18:00:04.997 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1996 [count] |
| 2026-09-19 18:00:05.002 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1996 [count] |
| 2026-09-19 18:00:05.004 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1996 [count] |
| 2026-09-19 18:00:05.007 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 224518 [count] |
| 2026-09-19 18:00:05.009 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1996 [count] |
| 2026-09-19 18:00:05.011 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 55% of GPU frame |
| 2026-09-19 18:00:05.014 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 18:00:05.018 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 18:00:05.020 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 18:00:05.022 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 18:00:05.023 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 18:00:05.025 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 18:00:05.026 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 55.4345 [percent] |
| 2026-09-19 18:00:09.958 | INFO | Performance | CPU 18.94 ms/frame (59.8 fps, worst 92.38 ms over 265 frames), RSS 790 MiB |
| 2026-09-19 18:00:09.960 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 18.9354 [ms] |
| 2026-09-19 18:00:09.962 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 92.3775 [ms] |
| 2026-09-19 18:00:09.963 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 59.8299 [fps] |
| 2026-09-19 18:00:09.965 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 265 [count] |
| 2026-09-19 18:00:09.966 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 790.09 [MiB] |
| 2026-09-19 18:00:09.968 | INFO | GpuTiming | GPU 13.75 ms total | cull 0.08 · raster 3.26 · HiZ 0.03 · resolve 0.07 · ReSTIR 10.31 · shadow 0.00 · post 0.37 · sky 0.00 · volume 0.00 |
| 2026-09-19 18:00:09.972 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 13.7514 [ms] |
| 2026-09-19 18:00:09.973 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.082112 [ms] |
| 2026-09-19 18:00:09.975 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.26022 [ms] |
| 2026-09-19 18:00:09.979 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.030272 [ms] |
| 2026-09-19 18:00:09.981 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.070848 [ms] |
| 2026-09-19 18:00:09.983 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 10.3079 [ms] |
| 2026-09-19 18:00:09.985 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 18:00:09.988 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.372672 [ms] |
| 2026-09-19 18:00:09.990 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 18:00:09.994 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 18:00:09.995 | INFO | Visibility | Clusters 2046 tested -> 1539 frustum, 1539 cone, 761 visible | draws 758+3, 86724 triangles |
| 2026-09-19 18:00:09.997 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 18:00:09.999 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1539 [count] |
| 2026-09-19 18:00:10.000 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1539 [count] |
| 2026-09-19 18:00:10.002 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 761 [count] |
| 2026-09-19 18:00:10.003 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 86724 [count] |
| 2026-09-19 18:00:10.004 | INFO | TelemetryMetrics | Measurement: DrawCalls = 761 [count] |
| 2026-09-19 18:00:10.004 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 75% of GPU frame |
| 2026-09-19 18:00:10.005 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 18:00:10.006 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 18:00:10.006 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 18:00:10.009 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 18:00:10.009 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 18:00:10.010 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 18:00:10.011 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 74.9592 [percent] |
| 2026-09-19 18:00:14.990 | INFO | Performance | CPU 20.97 ms/frame (43.1 fps, worst 58.72 ms over 240 frames), RSS 790 MiB |
| 2026-09-19 18:00:14.992 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 20.9657 [ms] |
| 2026-09-19 18:00:14.994 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 58.722 [ms] |
| 2026-09-19 18:00:14.996 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 43.0881 [fps] |
| 2026-09-19 18:00:14.997 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 240 [count] |
| 2026-09-19 18:00:14.999 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 790.16 [MiB] |
| 2026-09-19 18:00:15.001 | INFO | GpuTiming | GPU 19.80 ms total | cull 0.08 · raster 6.95 · HiZ 0.03 · resolve 0.29 · ReSTIR 12.45 · shadow 0.00 · post 0.41 · sky 0.00 · volume 0.00 |
| 2026-09-19 18:00:15.004 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 19.8028 [ms] |
| 2026-09-19 18:00:15.009 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.076576 [ms] |
| 2026-09-19 18:00:15.011 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 6.95203 [ms] |
| 2026-09-19 18:00:15.012 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.031008 [ms] |
| 2026-09-19 18:00:15.013 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.290112 [ms] |
| 2026-09-19 18:00:15.015 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 12.4531 [ms] |
| 2026-09-19 18:00:15.017 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 18:00:15.021 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.410687 [ms] |
| 2026-09-19 18:00:15.023 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 18:00:15.025 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 18:00:15.027 | INFO | Visibility | Clusters 2046 tested -> 1625 frustum, 1625 cone, 1625 visible | draws 1625+0, 182774 triangles |
| 2026-09-19 18:00:15.028 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 18:00:15.030 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1625 [count] |
| 2026-09-19 18:00:15.031 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1625 [count] |
| 2026-09-19 18:00:15.032 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1625 [count] |
| 2026-09-19 18:00:15.034 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 182774 [count] |
| 2026-09-19 18:00:15.039 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1625 [count] |
| 2026-09-19 18:00:15.041 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 63% of GPU frame |
| 2026-09-19 18:00:15.042 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 18:00:15.043 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 18:00:15.044 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 18:00:15.045 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 18:00:15.046 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 18:00:15.046 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 18:00:15.047 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 62.8854 [percent] |
| 2026-09-19 18:00:20.012 | INFO | Performance | CPU 19.79 ms/frame (50.0 fps, worst 63.45 ms over 253 frames), RSS 790 MiB |
| 2026-09-19 18:00:20.014 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 19.7898 [ms] |
| 2026-09-19 18:00:20.016 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 63.4537 [ms] |
| 2026-09-19 18:00:20.018 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 50.0031 [fps] |
| 2026-09-19 18:00:20.019 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 253 [count] |
| 2026-09-19 18:00:20.021 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 790.16 [MiB] |
| 2026-09-19 18:00:20.023 | INFO | GpuTiming | GPU 18.96 ms total | cull 0.06 · raster 3.10 · HiZ 0.03 · resolve 0.41 · ReSTIR 15.36 · shadow 0.00 · post 0.35 · sky 0.00 · volume 0.00 |
| 2026-09-19 18:00:20.027 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 18.9617 [ms] |
| 2026-09-19 18:00:20.031 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.061216 [ms] |
| 2026-09-19 18:00:20.033 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.09798 [ms] |
| 2026-09-19 18:00:20.035 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.030592 [ms] |
| 2026-09-19 18:00:20.036 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.413888 [ms] |
| 2026-09-19 18:00:20.038 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 15.358 [ms] |
| 2026-09-19 18:00:20.040 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 18:00:20.042 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.345344 [ms] |
| 2026-09-19 18:00:20.046 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 18:00:20.047 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 18:00:20.049 | INFO | Visibility | Clusters 2046 tested -> 727 frustum, 727 cone, 727 visible | draws 727+0, 81710 triangles |
| 2026-09-19 18:00:20.051 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 18:00:20.054 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 727 [count] |
| 2026-09-19 18:00:20.055 | INFO | TelemetryMetrics | Measurement: ClustersCone = 727 [count] |
| 2026-09-19 18:00:20.056 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 727 [count] |
| 2026-09-19 18:00:20.057 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 81710 [count] |
| 2026-09-19 18:00:20.058 | INFO | TelemetryMetrics | Measurement: DrawCalls = 727 [count] |
| 2026-09-19 18:00:20.061 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 81% of GPU frame |
| 2026-09-19 18:00:20.063 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 18:00:20.064 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 18:00:20.065 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 18:00:20.066 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 18:00:20.067 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 18:00:20.067 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 18:00:20.068 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 80.995 [percent] |
| 2026-09-19 18:00:25.039 | INFO | Performance | CPU 24.38 ms/frame (39.4 fps, worst 77.77 ms over 206 frames), RSS 790 MiB |
| 2026-09-19 18:00:25.041 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 24.3826 [ms] |
| 2026-09-19 18:00:25.043 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 77.7704 [ms] |
| 2026-09-19 18:00:25.044 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 39.4119 [fps] |
| 2026-09-19 18:00:25.046 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 206 [count] |
| 2026-09-19 18:00:25.050 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 790.16 [MiB] |
| 2026-09-19 18:00:25.052 | INFO | GpuTiming | GPU 24.09 ms total | cull 0.07 · raster 5.55 · HiZ 0.03 · resolve 0.66 · ReSTIR 17.78 · shadow 0.00 · post 0.30 · sky 0.00 · volume 0.00 |
| 2026-09-19 18:00:25.058 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 24.0943 [ms] |
| 2026-09-19 18:00:25.059 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.069792 [ms] |
| 2026-09-19 18:00:25.061 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 5.5479 [ms] |
| 2026-09-19 18:00:25.066 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.030816 [ms] |
| 2026-09-19 18:00:25.068 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.663552 [ms] |
| 2026-09-19 18:00:25.070 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 17.7822 [ms] |
| 2026-09-19 18:00:25.073 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 18:00:25.074 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.29936 [ms] |
| 2026-09-19 18:00:25.077 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 18:00:25.081 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 18:00:25.083 | INFO | Visibility | Clusters 2046 tested -> 1276 frustum, 1276 cone, 1276 visible | draws 1276+0, 143410 triangles |
| 2026-09-19 18:00:25.085 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 18:00:25.086 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1276 [count] |
| 2026-09-19 18:00:25.090 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1276 [count] |
| 2026-09-19 18:00:25.092 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1276 [count] |
| 2026-09-19 18:00:25.095 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 143410 [count] |
| 2026-09-19 18:00:25.096 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1276 [count] |
| 2026-09-19 18:00:25.097 | INFO | Performance | GPU-BOUND | 963 kpx x (4 candidates + 2 extra + 2 spatial taps), 4 denoise levels, present FIFO | kernel 74% of GPU frame |
| 2026-09-19 18:00:25.099 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 18:00:25.099 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 4 [count] |
| 2026-09-19 18:00:25.100 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 2 [count] |
| 2026-09-19 18:00:25.101 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 2 [count] |
| 2026-09-19 18:00:25.102 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 18:00:25.103 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 18:00:25.103 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 73.8026 [percent] |
| 2026-09-19 18:00:27.875 | INFO | Shutdown | Render loop exited cleanly. |
