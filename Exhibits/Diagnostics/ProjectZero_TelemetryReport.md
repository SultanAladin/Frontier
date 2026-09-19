# ProjectZero_TelemetryReport — Telemetry Log

| Timestamp | Severity | Category | Record Description |
|:---|:---:|:---|:---|
| 2026-09-19 22:05:52.708 | INFO | Bootstrap | Project-Zero windowed ReSTIR renderer starting. |
| 2026-09-19 22:05:53.387 | INFO | Scene | Showcase: 229506 triangles, 499 instances, 2045 clusters, 241 materials, 3368 luminaires, bounds [-40.00 -40.00 0.00]..[40.00 40.00 6.00] m |
| 2026-09-19 22:05:54.417 | INFO | Textures | Textures: 6 resident (0 placeholder), 64.0 MB with mips, decoded in 1029 ms |
| 2026-09-19 22:05:54.417 | INFO | Materials | Materials: 241 descriptors -> 241 records, 241 slabs (limit 1, 0 folded), 499 placements, 0 cameras, 0 punctual lights |
| 2026-09-19 22:05:54.453 | INFO | Interface | Panel light Low: rgb (0.000 0.008 0.042) from 4 figures, 5% coverage, 3370 luminaires now. |
| 2026-09-19 22:05:55.389 | INFO | Traversal | CWBVH: 229508 triangles → 39627 nodes, 3095.9 KB nodes + 16137.3 KB leaves (85.8 B/tri), SAH 13.84, built in 932.9 ms (spatial splits) |
| 2026-09-19 22:08:01.751 | INFO | Bootstrap | Window and Vulkan swapchain ready. |
| 2026-09-19 22:08:03.861 | INFO | Traversal | Two-level: 500 instances -> 500 BLASes over 229508 triangles, top level 598 nodes, shared blobs 2631.4 KB + 10758.2 KB, built in 603.9 ms |
| 2026-09-19 22:08:03.912 | INFO | Moons | 6 textures resident, moon slots 0..5. |
| 2026-09-19 22:08:03.914 | INFO | Stars | 9683 stars in 1024 cells uploaded to binding 23. |
| 2026-09-19 22:08:05.312 | INFO | Bootstrap | Entering render loop. |
| 2026-09-19 22:08:05.404 | INFO | Interface | Director ready: TAB switches screens. The card carries 2 converted vector segments. |
| 2026-09-19 22:08:05.783 | INFO | Audio | Panel bound to audio: drag the progress bar to change the engine note. |
| 2026-09-19 22:08:05.784 | INFO | Interface | Spatial interface ready: 14 figures, depth test off. |
| 2026-09-19 22:08:05.816 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: PCSS @ 1024 px, 7 taps. |
| 2026-09-19 22:08:17.877 | INFO | Performance | CPU 75.84 ms/frame (13.2 fps, worst 100.00 ms over 66 frames), RSS 822 MiB |
| 2026-09-19 22:08:17.879 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 75.8438 [ms] |
| 2026-09-19 22:08:17.881 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:08:17.887 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 13.185 [fps] |
| 2026-09-19 22:08:17.888 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 66 [count] |
| 2026-09-19 22:08:17.891 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 821.809 [MiB] |
| 2026-09-19 22:08:17.893 | INFO | GpuTiming | GPU 158.47 ms total | cull 0.08 · raster 8.48 · HiZ 0.08 · resolve 1.88 · ReSTIR 147.94 · shadow 0.00 · post 3.68 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:08:17.898 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 158.472 [ms] |
| 2026-09-19 22:08:17.904 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.08336 [ms] |
| 2026-09-19 22:08:17.908 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.48256 [ms] |
| 2026-09-19 22:08:17.910 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.084544 [ms] |
| 2026-09-19 22:08:17.913 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.87773 [ms] |
| 2026-09-19 22:08:17.914 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 147.943 [ms] |
| 2026-09-19 22:08:17.920 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:08:17.923 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 3.67699 [ms] |
| 2026-09-19 22:08:17.926 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:08:17.927 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:08:17.932 | INFO | Visibility | Clusters 2046 tested -> 1996 frustum, 1996 cone, 1996 visible | draws 1996+0, 224480 triangles |
| 2026-09-19 22:08:17.941 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:08:17.944 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1996 [count] |
| 2026-09-19 22:08:17.946 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1996 [count] |
| 2026-09-19 22:08:17.948 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1996 [count] |
| 2026-09-19 22:08:17.953 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 224480 [count] |
| 2026-09-19 22:08:17.956 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1996 [count] |
| 2026-09-19 22:08:17.958 | INFO | Performance | GPU-BOUND | 922 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 22:08:17.967 | INFO | TelemetryMetrics | Measurement: RenderPixels = 921600 [px] |
| 2026-09-19 22:08:17.974 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 22:08:17.977 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 22:08:17.981 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 22:08:17.984 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 22:08:17.987 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:08:17.988 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.3564 [percent] |
| 2026-09-19 22:08:29.474 | INFO | Performance | CPU 76.74 ms/frame (13.0 fps, worst 100.00 ms over 66 frames), RSS 946 MiB |
| 2026-09-19 22:08:29.475 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 76.7389 [ms] |
| 2026-09-19 22:08:29.478 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:08:29.479 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 13.0063 [fps] |
| 2026-09-19 22:08:29.480 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 66 [count] |
| 2026-09-19 22:08:29.481 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 945.785 [MiB] |
| 2026-09-19 22:08:29.483 | INFO | GpuTiming | GPU 157.75 ms total | cull 0.09 · raster 8.99 · HiZ 0.09 · resolve 1.72 · ReSTIR 146.85 · shadow 0.00 · post 3.51 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:08:29.487 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 157.748 [ms] |
| 2026-09-19 22:08:29.492 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.093504 [ms] |
| 2026-09-19 22:08:29.495 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.99446 [ms] |
| 2026-09-19 22:08:29.496 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.088096 [ms] |
| 2026-09-19 22:08:29.498 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.71827 [ms] |
| 2026-09-19 22:08:29.501 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 146.854 [ms] |
| 2026-09-19 22:08:29.502 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:08:29.507 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 3.51205 [ms] |
| 2026-09-19 22:08:29.510 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:08:29.512 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:08:29.514 | INFO | Visibility | Clusters 2046 tested -> 1972 frustum, 1972 cone, 1972 visible | draws 1972+0, 221682 triangles |
| 2026-09-19 22:08:29.516 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:08:29.519 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1972 [count] |
| 2026-09-19 22:08:29.523 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1972 [count] |
| 2026-09-19 22:08:29.529 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1972 [count] |
| 2026-09-19 22:08:29.530 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 221682 [count] |
| 2026-09-19 22:08:29.534 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1972 [count] |
| 2026-09-19 22:08:29.538 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 93% of GPU frame |
| 2026-09-19 22:08:29.543 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:08:29.544 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 22:08:29.550 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 22:08:29.554 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 22:08:29.556 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 22:08:29.559 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:08:29.561 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.0938 [percent] |
| 2026-09-19 22:08:37.949 | INFO | Performance | CPU 82.11 ms/frame (12.5 fps, worst 100.00 ms over 61 frames), RSS 969 MiB |
| 2026-09-19 22:08:37.951 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 82.1078 [ms] |
| 2026-09-19 22:08:37.953 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:08:37.954 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 12.5222 [fps] |
| 2026-09-19 22:08:37.956 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 61 [count] |
| 2026-09-19 22:08:37.958 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 969.125 [MiB] |
| 2026-09-19 22:08:37.960 | INFO | GpuTiming | GPU 169.70 ms total | cull 0.08 · raster 7.29 · HiZ 0.09 · resolve 1.25 · ReSTIR 160.98 · shadow 0.00 · post 3.54 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:08:37.962 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 169.696 [ms] |
| 2026-09-19 22:08:37.964 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.083168 [ms] |
| 2026-09-19 22:08:37.966 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.2872 [ms] |
| 2026-09-19 22:08:37.968 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.08912 [ms] |
| 2026-09-19 22:08:37.969 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.25341 [ms] |
| 2026-09-19 22:08:37.971 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 160.983 [ms] |
| 2026-09-19 22:08:37.978 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:08:37.980 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 3.5365 [ms] |
| 2026-09-19 22:08:37.982 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:08:37.983 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:08:37.985 | INFO | Visibility | Clusters 2046 tested -> 1633 frustum, 1633 cone, 1633 visible | draws 1539+94, 183102 triangles |
| 2026-09-19 22:08:37.987 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:08:37.993 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1633 [count] |
| 2026-09-19 22:08:37.996 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1633 [count] |
| 2026-09-19 22:08:37.998 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1633 [count] |
| 2026-09-19 22:08:37.999 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 183102 [count] |
| 2026-09-19 22:08:38.001 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1633 [count] |
| 2026-09-19 22:08:38.007 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 95% of GPU frame |
| 2026-09-19 22:08:38.012 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:08:38.014 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 22:08:38.016 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 22:08:38.024 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 22:08:38.026 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 22:08:38.031 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:08:38.032 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 94.8656 [percent] |
| 2026-09-19 22:08:52.152 | INFO | Performance | CPU 82.26 ms/frame (12.1 fps, worst 100.00 ms over 62 frames), RSS 790 MiB |
| 2026-09-19 22:08:52.154 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 82.2554 [ms] |
| 2026-09-19 22:08:52.157 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:08:52.160 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 12.1193 [fps] |
| 2026-09-19 22:08:52.161 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 62 [count] |
| 2026-09-19 22:08:52.163 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 790.402 [MiB] |
| 2026-09-19 22:08:52.165 | INFO | GpuTiming | GPU 294.85 ms total | cull 0.08 · raster 6.38 · HiZ 0.09 · resolve 1.99 · ReSTIR 286.31 · shadow 0.00 · post 2.55 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:08:52.168 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 294.852 [ms] |
| 2026-09-19 22:08:52.169 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.078144 [ms] |
| 2026-09-19 22:08:52.171 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 6.37891 [ms] |
| 2026-09-19 22:08:52.176 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.09152 [ms] |
| 2026-09-19 22:08:52.179 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.99475 [ms] |
| 2026-09-19 22:08:52.181 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 286.309 [ms] |
| 2026-09-19 22:08:52.182 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:08:52.185 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 2.55478 [ms] |
| 2026-09-19 22:08:52.186 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:08:52.191 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:08:52.192 | INFO | Visibility | Clusters 2046 tested -> 1624 frustum, 1624 cone, 1616 visible | draws 1616+0, 182502 triangles |
| 2026-09-19 22:08:52.196 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:08:52.198 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1624 [count] |
| 2026-09-19 22:08:52.200 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1624 [count] |
| 2026-09-19 22:08:52.201 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1616 [count] |
| 2026-09-19 22:08:52.206 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 182502 [count] |
| 2026-09-19 22:08:52.209 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1616 [count] |
| 2026-09-19 22:08:52.210 | INFO | Performance | GPU-BOUND | 963 kpx x (8 candidates + 3 extra + 3 spatial taps), 5 denoise levels, present FIFO | kernel 97% of GPU frame |
| 2026-09-19 22:08:52.214 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:08:52.217 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 8 [count] |
| 2026-09-19 22:08:52.218 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 3 [count] |
| 2026-09-19 22:08:52.221 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 3 [count] |
| 2026-09-19 22:08:52.226 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 22:08:52.227 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:08:52.229 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 97.1025 [percent] |
| 2026-09-19 22:08:56.056 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: PCSS @ 1024 px, 9 taps. |
| 2026-09-19 22:08:57.761 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: PCSS @ 1024 px, 9 taps. |
| 2026-09-19 22:09:11.094 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: PCSS @ 1024 px, 9 taps. |
| 2026-09-19 22:09:11.757 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: PCSS @ 1024 px, 9 taps. |
| 2026-09-19 22:09:12.132 | INFO | Performance | CPU 77.13 ms/frame (12.5 fps, worst 100.00 ms over 65 frames), RSS 837 MiB |
| 2026-09-19 22:09:12.134 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 77.1306 [ms] |
| 2026-09-19 22:09:12.136 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:09:12.137 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 12.5001 [fps] |
| 2026-09-19 22:09:12.139 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 65 [count] |
| 2026-09-19 22:09:12.141 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 836.742 [MiB] |
| 2026-09-19 22:09:12.145 | INFO | GpuTiming | GPU 69.56 ms total | cull 0.09 · raster 7.66 · HiZ 0.04 · resolve 1.34 · ReSTIR 60.44 · shadow 0.00 · post 0.92 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:09:12.150 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 69.5617 [ms] |
| 2026-09-19 22:09:12.151 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.085856 [ms] |
| 2026-09-19 22:09:12.155 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.66323 [ms] |
| 2026-09-19 22:09:12.159 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.038944 [ms] |
| 2026-09-19 22:09:12.163 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.3353 [ms] |
| 2026-09-19 22:09:12.164 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 60.4383 [ms] |
| 2026-09-19 22:09:12.167 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:09:12.168 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.918945 [ms] |
| 2026-09-19 22:09:12.171 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:09:12.175 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:09:12.177 | INFO | Visibility | Clusters 2046 tested -> 1624 frustum, 1624 cone, 1615 visible | draws 1615+0, 182374 triangles |
| 2026-09-19 22:09:12.181 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:09:12.182 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1624 [count] |
| 2026-09-19 22:09:12.185 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1624 [count] |
| 2026-09-19 22:09:12.187 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1615 [count] |
| 2026-09-19 22:09:12.191 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 182374 [count] |
| 2026-09-19 22:09:12.193 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1615 [count] |
| 2026-09-19 22:09:12.196 | INFO | Performance | GPU-BOUND | 963 kpx x (16 candidates + 4 extra + 4 spatial taps), 5 denoise levels, present FIFO | kernel 87% of GPU frame |
| 2026-09-19 22:09:12.200 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:09:12.201 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 16 [count] |
| 2026-09-19 22:09:12.207 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 4 [count] |
| 2026-09-19 22:09:12.209 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 4 [count] |
| 2026-09-19 22:09:12.212 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 5 [count] |
| 2026-09-19 22:09:12.213 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:09:12.218 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 86.8846 [percent] |
| 2026-09-19 22:09:12.443 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: PCSS @ 1024 px, 9 taps. |
| 2026-09-19 22:09:13.891 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: Hard @ 1024 px, 1 taps. |
| 2026-09-19 22:09:14.435 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: Hard @ 1024 px, 1 taps. |
| 2026-09-19 22:09:17.790 | INFO | Performance | CPU 61.33 ms/frame (15.0 fps, worst 100.00 ms over 82 frames), RSS 915 MiB |
| 2026-09-19 22:09:17.792 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 61.3312 [ms] |
| 2026-09-19 22:09:17.794 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:09:17.796 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 15.0321 [fps] |
| 2026-09-19 22:09:17.798 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 82 [count] |
| 2026-09-19 22:09:17.800 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 914.988 [MiB] |
| 2026-09-19 22:09:17.802 | INFO | GpuTiming | GPU 59.63 ms total | cull 0.10 · raster 8.29 · HiZ 0.04 · resolve 1.37 · ReSTIR 49.84 · shadow 0.00 · post 0.59 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:09:17.805 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 59.6263 [ms] |
| 2026-09-19 22:09:17.807 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.096832 [ms] |
| 2026-09-19 22:09:17.809 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.28643 [ms] |
| 2026-09-19 22:09:17.810 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.040224 [ms] |
| 2026-09-19 22:09:17.815 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.36602 [ms] |
| 2026-09-19 22:09:17.818 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 49.8368 [ms] |
| 2026-09-19 22:09:17.820 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:09:17.821 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.591133 [ms] |
| 2026-09-19 22:09:17.824 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:09:17.825 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:09:17.829 | INFO | Visibility | Clusters 2046 tested -> 1624 frustum, 1624 cone, 1615 visible | draws 1615+0, 182374 triangles |
| 2026-09-19 22:09:17.833 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:09:17.834 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1624 [count] |
| 2026-09-19 22:09:17.836 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1624 [count] |
| 2026-09-19 22:09:17.838 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1615 [count] |
| 2026-09-19 22:09:17.840 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 182374 [count] |
| 2026-09-19 22:09:17.841 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1615 [count] |
| 2026-09-19 22:09:17.846 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 84% of GPU frame |
| 2026-09-19 22:09:17.852 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:09:17.854 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:09:17.855 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:09:17.858 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:09:17.862 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:09:17.866 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:09:17.869 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 83.5819 [percent] |
| 2026-09-19 22:09:23.397 | INFO | Performance | CPU 61.19 ms/frame (16.7 fps, worst 100.00 ms over 83 frames), RSS 942 MiB |
| 2026-09-19 22:09:23.403 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 61.1926 [ms] |
| 2026-09-19 22:09:23.404 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:09:23.406 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.667 [fps] |
| 2026-09-19 22:09:23.409 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 83 [count] |
| 2026-09-19 22:09:23.411 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 942.305 [MiB] |
| 2026-09-19 22:09:23.413 | INFO | GpuTiming | GPU 65.35 ms total | cull 0.08 · raster 8.34 · HiZ 0.04 · resolve 1.18 · ReSTIR 55.72 · shadow 0.00 · post 0.58 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:09:23.420 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 65.3526 [ms] |
| 2026-09-19 22:09:23.425 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.079008 [ms] |
| 2026-09-19 22:09:23.429 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.33715 [ms] |
| 2026-09-19 22:09:23.431 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.038912 [ms] |
| 2026-09-19 22:09:23.436 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.18166 [ms] |
| 2026-09-19 22:09:23.438 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 55.7158 [ms] |
| 2026-09-19 22:09:23.440 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:09:23.442 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.583073 [ms] |
| 2026-09-19 22:09:23.444 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:09:23.445 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:09:23.447 | INFO | Visibility | Clusters 2046 tested -> 1624 frustum, 1624 cone, 1615 visible | draws 1615+0, 182374 triangles |
| 2026-09-19 22:09:23.455 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:09:23.458 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1624 [count] |
| 2026-09-19 22:09:23.459 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1624 [count] |
| 2026-09-19 22:09:23.462 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1615 [count] |
| 2026-09-19 22:09:23.463 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 182374 [count] |
| 2026-09-19 22:09:23.475 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1615 [count] |
| 2026-09-19 22:09:23.478 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 85% of GPU frame |
| 2026-09-19 22:09:23.481 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:09:23.486 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:09:23.489 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:09:23.490 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:09:23.492 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:09:23.495 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:09:23.506 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 85.2542 [percent] |
| 2026-09-19 22:09:28.650 | INFO | Performance | CPU 57.47 ms/frame (16.9 fps, worst 100.00 ms over 87 frames), RSS 969 MiB |
| 2026-09-19 22:09:28.652 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 57.473 [ms] |
| 2026-09-19 22:09:28.655 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:09:28.656 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 16.9226 [fps] |
| 2026-09-19 22:09:28.658 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 87 [count] |
| 2026-09-19 22:09:28.660 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 968.98 [MiB] |
| 2026-09-19 22:09:28.661 | INFO | GpuTiming | GPU 45.16 ms total | cull 0.07 · raster 3.43 · HiZ 0.04 · resolve 0.36 · ReSTIR 41.26 · shadow 0.00 · post 0.78 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:09:28.664 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 45.1641 [ms] |
| 2026-09-19 22:09:28.667 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.07168 [ms] |
| 2026-09-19 22:09:28.673 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.43485 [ms] |
| 2026-09-19 22:09:28.675 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.038912 [ms] |
| 2026-09-19 22:09:28.677 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.356352 [ms] |
| 2026-09-19 22:09:28.679 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 41.2623 [ms] |
| 2026-09-19 22:09:28.680 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:09:28.682 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.781055 [ms] |
| 2026-09-19 22:09:28.686 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:09:28.689 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:09:28.693 | INFO | Visibility | Clusters 2046 tested -> 816 frustum, 816 cone, 816 visible | draws 816+0, 91750 triangles |
| 2026-09-19 22:09:28.694 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:09:28.696 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 816 [count] |
| 2026-09-19 22:09:28.698 | INFO | TelemetryMetrics | Measurement: ClustersCone = 816 [count] |
| 2026-09-19 22:09:28.704 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 816 [count] |
| 2026-09-19 22:09:28.706 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 91750 [count] |
| 2026-09-19 22:09:28.708 | INFO | TelemetryMetrics | Measurement: DrawCalls = 816 [count] |
| 2026-09-19 22:09:28.710 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 91% of GPU frame |
| 2026-09-19 22:09:28.712 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:09:28.717 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:09:28.719 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:09:28.722 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:09:28.724 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:09:28.726 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 22:09:28.727 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 91.3609 [percent] |
| 2026-09-19 22:09:33.897 | INFO | Performance | CPU 46.36 ms/frame (21.3 fps, worst 100.00 ms over 109 frames), RSS 969 MiB |
| 2026-09-19 22:09:33.900 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 46.3621 [ms] |
| 2026-09-19 22:09:33.907 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:09:33.911 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 21.3272 [fps] |
| 2026-09-19 22:09:33.912 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 109 [count] |
| 2026-09-19 22:09:33.915 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 969.367 [MiB] |
| 2026-09-19 22:09:33.928 | INFO | GpuTiming | GPU 56.47 ms total | cull 0.08 · raster 7.92 · HiZ 0.04 · resolve 0.93 · ReSTIR 47.49 · shadow 0.00 · post 0.66 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:09:33.932 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 56.4694 [ms] |
| 2026-09-19 22:09:33.935 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.081664 [ms] |
| 2026-09-19 22:09:33.938 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.92006 [ms] |
| 2026-09-19 22:09:33.941 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.038912 [ms] |
| 2026-09-19 22:09:33.944 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.934016 [ms] |
| 2026-09-19 22:09:33.947 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 47.4947 [ms] |
| 2026-09-19 22:09:33.960 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:09:33.973 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.660065 [ms] |
| 2026-09-19 22:09:33.975 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:09:33.984 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:09:33.985 | INFO | Visibility | Clusters 2046 tested -> 1862 frustum, 1862 cone, 1825 visible | draws 1825+0, 205682 triangles |
| 2026-09-19 22:09:33.991 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:09:33.993 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1862 [count] |
| 2026-09-19 22:09:34.000 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1862 [count] |
| 2026-09-19 22:09:34.002 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1825 [count] |
| 2026-09-19 22:09:34.012 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 205682 [count] |
| 2026-09-19 22:09:34.015 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1825 [count] |
| 2026-09-19 22:09:34.018 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 84% of GPU frame |
| 2026-09-19 22:09:34.027 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:09:34.031 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:09:34.034 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:09:34.036 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:09:34.047 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:09:34.050 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:09:34.052 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 84.107 [percent] |
| 2026-09-19 22:09:39.149 | INFO | Performance | CPU 54.61 ms/frame (18.6 fps, worst 100.00 ms over 92 frames), RSS 1115 MiB |
| 2026-09-19 22:09:39.151 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 54.6121 [ms] |
| 2026-09-19 22:09:39.153 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:09:39.155 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 18.5591 [fps] |
| 2026-09-19 22:09:39.159 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 92 [count] |
| 2026-09-19 22:09:39.161 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1114.79 [MiB] |
| 2026-09-19 22:09:39.165 | INFO | GpuTiming | GPU 54.27 ms total | cull 0.09 · raster 8.99 · HiZ 0.04 · resolve 1.23 · ReSTIR 43.92 · shadow 0.00 · post 0.47 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:09:39.168 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 54.2717 [ms] |
| 2026-09-19 22:09:39.170 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.0864 [ms] |
| 2026-09-19 22:09:39.178 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.99408 [ms] |
| 2026-09-19 22:09:39.179 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.039808 [ms] |
| 2026-09-19 22:09:39.181 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.23146 [ms] |
| 2026-09-19 22:09:39.184 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 43.9199 [ms] |
| 2026-09-19 22:09:39.185 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:09:39.222 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.472736 [ms] |
| 2026-09-19 22:09:39.233 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:09:39.255 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:09:39.263 | INFO | Visibility | Clusters 2046 tested -> 1862 frustum, 1862 cone, 1825 visible | draws 1825+0, 205682 triangles |
| 2026-09-19 22:09:39.268 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:09:39.271 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1862 [count] |
| 2026-09-19 22:09:39.273 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1862 [count] |
| 2026-09-19 22:09:39.278 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1825 [count] |
| 2026-09-19 22:09:39.283 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 205682 [count] |
| 2026-09-19 22:09:39.286 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1825 [count] |
| 2026-09-19 22:09:39.288 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 81% of GPU frame |
| 2026-09-19 22:09:39.295 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:09:39.301 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:09:39.303 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:09:39.305 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:09:39.309 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:09:39.313 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:09:39.315 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 80.9261 [percent] |
| 2026-09-19 22:09:44.283 | INFO | Performance | CPU 48.27 ms/frame (20.0 fps, worst 100.00 ms over 104 frames), RSS 1116 MiB |
| 2026-09-19 22:09:44.284 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 48.2691 [ms] |
| 2026-09-19 22:09:44.286 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:09:44.288 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 20.0092 [fps] |
| 2026-09-19 22:09:44.290 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 104 [count] |
| 2026-09-19 22:09:44.294 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 1115.76 [MiB] |
| 2026-09-19 22:09:44.295 | INFO | GpuTiming | GPU 45.53 ms total | cull 0.09 · raster 7.90 · HiZ 0.04 · resolve 0.98 · ReSTIR 36.53 · shadow 0.00 · post 0.46 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:09:44.297 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 45.5346 [ms] |
| 2026-09-19 22:09:44.300 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.08544 [ms] |
| 2026-09-19 22:09:44.301 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.89718 [ms] |
| 2026-09-19 22:09:44.303 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.039072 [ms] |
| 2026-09-19 22:09:44.305 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.983136 [ms] |
| 2026-09-19 22:09:44.310 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 36.5297 [ms] |
| 2026-09-19 22:09:44.312 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:09:44.315 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.458241 [ms] |
| 2026-09-19 22:09:44.316 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:09:44.319 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:09:44.320 | INFO | Visibility | Clusters 2046 tested -> 1862 frustum, 1862 cone, 1825 visible | draws 1825+0, 205682 triangles |
| 2026-09-19 22:09:44.324 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:09:44.327 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1862 [count] |
| 2026-09-19 22:09:44.329 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1862 [count] |
| 2026-09-19 22:09:44.331 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1825 [count] |
| 2026-09-19 22:09:44.333 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 205682 [count] |
| 2026-09-19 22:09:44.335 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1825 [count] |
| 2026-09-19 22:09:44.337 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 80% of GPU frame |
| 2026-09-19 22:09:44.343 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:09:44.345 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:09:44.348 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:09:44.350 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:09:44.351 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:09:44.353 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:09:44.357 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 80.2242 [percent] |
| 2026-09-19 22:09:49.399 | INFO | Performance | CPU 42.45 ms/frame (23.5 fps, worst 100.00 ms over 119 frames), RSS 750 MiB |
| 2026-09-19 22:09:49.401 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 42.4486 [ms] |
| 2026-09-19 22:09:49.403 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:09:49.405 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 23.5449 [fps] |
| 2026-09-19 22:09:49.407 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 119 [count] |
| 2026-09-19 22:09:49.408 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 750.234 [MiB] |
| 2026-09-19 22:09:49.414 | INFO | GpuTiming | GPU 51.22 ms total | cull 0.07 · raster 6.04 · HiZ 0.04 · resolve 0.90 · ReSTIR 44.17 · shadow 0.00 · post 0.62 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:09:49.419 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 51.2196 [ms] |
| 2026-09-19 22:09:49.420 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.074432 [ms] |
| 2026-09-19 22:09:49.424 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 6.03789 [ms] |
| 2026-09-19 22:09:49.426 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.038784 [ms] |
| 2026-09-19 22:09:49.430 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.902144 [ms] |
| 2026-09-19 22:09:49.432 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 44.1663 [ms] |
| 2026-09-19 22:09:49.435 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:09:49.436 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.621887 [ms] |
| 2026-09-19 22:09:49.439 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:09:49.440 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:09:49.446 | INFO | Visibility | Clusters 2046 tested -> 1424 frustum, 1424 cone, 1424 visible | draws 1424+0, 160254 triangles |
| 2026-09-19 22:09:49.448 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:09:49.451 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1424 [count] |
| 2026-09-19 22:09:49.452 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1424 [count] |
| 2026-09-19 22:09:49.455 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1424 [count] |
| 2026-09-19 22:09:49.456 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 160254 [count] |
| 2026-09-19 22:09:49.462 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1424 [count] |
| 2026-09-19 22:09:49.465 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 86% of GPU frame |
| 2026-09-19 22:09:49.469 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:09:49.471 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:09:49.473 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:09:49.477 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:09:49.479 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:09:49.481 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:09:49.485 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 86.2294 [percent] |
| 2026-09-19 22:09:54.591 | INFO | Performance | CPU 48.90 ms/frame (20.3 fps, worst 100.00 ms over 103 frames), RSS 813 MiB |
| 2026-09-19 22:09:54.593 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 48.9 [ms] |
| 2026-09-19 22:09:54.595 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:09:54.596 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 20.2845 [fps] |
| 2026-09-19 22:09:54.598 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 103 [count] |
| 2026-09-19 22:09:54.599 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 813.453 [MiB] |
| 2026-09-19 22:09:54.601 | INFO | GpuTiming | GPU 48.22 ms total | cull 0.10 · raster 8.37 · HiZ 0.04 · resolve 1.00 · ReSTIR 38.71 · shadow 0.00 · post 0.69 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:09:54.609 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 48.2179 [ms] |
| 2026-09-19 22:09:54.612 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.102432 [ms] |
| 2026-09-19 22:09:54.614 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.36822 [ms] |
| 2026-09-19 22:09:54.621 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.038912 [ms] |
| 2026-09-19 22:09:54.623 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.00278 [ms] |
| 2026-09-19 22:09:54.627 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 38.7055 [ms] |
| 2026-09-19 22:09:54.631 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:09:54.634 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.686462 [ms] |
| 2026-09-19 22:09:54.638 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:09:54.640 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:09:54.643 | INFO | Visibility | Clusters 2046 tested -> 1928 frustum, 1928 cone, 1923 visible | draws 1923+0, 215912 triangles |
| 2026-09-19 22:09:54.645 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:09:54.646 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1928 [count] |
| 2026-09-19 22:09:54.648 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1928 [count] |
| 2026-09-19 22:09:54.654 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1923 [count] |
| 2026-09-19 22:09:54.656 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 215912 [count] |
| 2026-09-19 22:09:54.657 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1923 [count] |
| 2026-09-19 22:09:54.658 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 80% of GPU frame |
| 2026-09-19 22:09:54.661 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:09:54.662 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:09:54.664 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:09:54.665 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:09:54.670 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:09:54.671 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:09:54.673 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 80.2721 [percent] |
| 2026-09-19 22:09:59.634 | INFO | Performance | CPU 49.97 ms/frame (19.8 fps, worst 100.00 ms over 101 frames), RSS 872 MiB |
| 2026-09-19 22:09:59.636 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 49.9749 [ms] |
| 2026-09-19 22:09:59.638 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:09:59.639 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 19.8166 [fps] |
| 2026-09-19 22:09:59.642 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 101 [count] |
| 2026-09-19 22:09:59.643 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 871.695 [MiB] |
| 2026-09-19 22:09:59.645 | INFO | GpuTiming | GPU 49.29 ms total | cull 0.12 · raster 10.33 · HiZ 0.04 · resolve 1.31 · ReSTIR 37.48 · shadow 0.00 · post 0.63 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:09:59.648 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 49.2869 [ms] |
| 2026-09-19 22:09:59.650 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.118848 [ms] |
| 2026-09-19 22:09:59.652 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 10.3324 [ms] |
| 2026-09-19 22:09:59.656 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.038624 [ms] |
| 2026-09-19 22:09:59.657 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.31277 [ms] |
| 2026-09-19 22:09:59.660 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 37.4842 [ms] |
| 2026-09-19 22:09:59.661 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:09:59.663 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.625088 [ms] |
| 2026-09-19 22:09:59.665 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:09:59.668 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:09:59.669 | INFO | Visibility | Clusters 2046 tested -> 1928 frustum, 1928 cone, 1923 visible | draws 1923+0, 215912 triangles |
| 2026-09-19 22:09:59.673 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:09:59.675 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1928 [count] |
| 2026-09-19 22:09:59.678 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1928 [count] |
| 2026-09-19 22:09:59.679 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1923 [count] |
| 2026-09-19 22:09:59.681 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 215912 [count] |
| 2026-09-19 22:09:59.683 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1923 [count] |
| 2026-09-19 22:09:59.688 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 76% of GPU frame |
| 2026-09-19 22:09:59.691 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:09:59.693 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:09:59.694 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:09:59.697 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:09:59.699 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:09:59.705 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:09:59.707 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 76.0531 [percent] |
| 2026-09-19 22:10:03.545 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: Hard @ 1024 px, 1 taps. |
| 2026-09-19 22:10:03.879 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: Hard @ 1024 px, 1 taps. |
| 2026-09-19 22:10:04.338 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: Hard @ 1024 px, 1 taps. |
| 2026-09-19 22:10:04.830 | INFO | Performance | CPU 53.09 ms/frame (19.0 fps, worst 100.00 ms over 95 frames), RSS 892 MiB |
| 2026-09-19 22:10:04.832 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 53.0919 [ms] |
| 2026-09-19 22:10:04.834 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:10:04.836 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 18.9583 [fps] |
| 2026-09-19 22:10:04.837 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 95 [count] |
| 2026-09-19 22:10:04.839 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 891.559 [MiB] |
| 2026-09-19 22:10:04.841 | INFO | GpuTiming | GPU 65.31 ms total | cull 0.09 · raster 8.26 · HiZ 0.05 · resolve 1.03 · ReSTIR 55.89 · shadow 0.00 · post 1.01 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:10:04.844 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 65.3146 [ms] |
| 2026-09-19 22:10:04.847 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.08832 [ms] |
| 2026-09-19 22:10:04.853 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.26237 [ms] |
| 2026-09-19 22:10:04.855 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047616 [ms] |
| 2026-09-19 22:10:04.857 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.0281 [ms] |
| 2026-09-19 22:10:04.858 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 55.8882 [ms] |
| 2026-09-19 22:10:04.861 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:10:04.862 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.01117 [ms] |
| 2026-09-19 22:10:04.868 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:10:04.869 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:10:04.872 | INFO | Visibility | Clusters 2046 tested -> 1928 frustum, 1928 cone, 1909 visible | draws 1909+0, 214184 triangles |
| 2026-09-19 22:10:04.873 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:10:04.876 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1928 [count] |
| 2026-09-19 22:10:04.877 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1928 [count] |
| 2026-09-19 22:10:04.883 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1909 [count] |
| 2026-09-19 22:10:04.885 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 214184 [count] |
| 2026-09-19 22:10:04.888 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1909 [count] |
| 2026-09-19 22:10:04.889 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 86% of GPU frame |
| 2026-09-19 22:10:04.892 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:10:04.896 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:10:04.898 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:10:04.901 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:10:04.902 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:10:04.904 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:10:04.905 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 85.5677 [percent] |
| 2026-09-19 22:10:06.902 | INFO | Shadows | Shadow path: rasterised maps (GI off) - Hard @ 1024 px, 1 taps. |
| 2026-09-19 22:10:07.324 | INFO | Shadows | Shadow path: rasterised maps (GI off) - Hard @ 1024 px, 1 taps. |
| 2026-09-19 22:10:10.050 | INFO | Performance | CPU 38.47 ms/frame (27.2 fps, worst 100.00 ms over 130 frames), RSS 960 MiB |
| 2026-09-19 22:10:10.052 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 38.4711 [ms] |
| 2026-09-19 22:10:10.055 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:10:10.056 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 27.1552 [fps] |
| 2026-09-19 22:10:10.058 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 130 [count] |
| 2026-09-19 22:10:10.060 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 959.941 [MiB] |
| 2026-09-19 22:10:10.062 | INFO | GpuTiming | GPU 28.75 ms total | cull 0.08 · raster 8.43 · HiZ 0.05 · resolve 1.08 · ReSTIR 19.10 · shadow 0.00 · post 0.84 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:10:10.064 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 28.7453 [ms] |
| 2026-09-19 22:10:10.067 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.083744 [ms] |
| 2026-09-19 22:10:10.071 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 8.43229 [ms] |
| 2026-09-19 22:10:10.073 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 22:10:10.075 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.08438 [ms] |
| 2026-09-19 22:10:10.077 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 19.0978 [ms] |
| 2026-09-19 22:10:10.078 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:10:10.080 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.83744 [ms] |
| 2026-09-19 22:10:10.082 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:10:10.083 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:10:10.089 | INFO | Visibility | Clusters 2046 tested -> 1928 frustum, 1928 cone, 1909 visible | draws 1909+0, 214184 triangles |
| 2026-09-19 22:10:10.091 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:10:10.092 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1928 [count] |
| 2026-09-19 22:10:10.094 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1928 [count] |
| 2026-09-19 22:10:10.096 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1909 [count] |
| 2026-09-19 22:10:10.098 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 214184 [count] |
| 2026-09-19 22:10:10.103 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1909 [count] |
| 2026-09-19 22:10:10.104 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 66% of GPU frame |
| 2026-09-19 22:10:10.107 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:10:10.110 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:10:10.111 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:10:10.114 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:10:10.119 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:10:10.121 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 22:10:10.122 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 66.4379 [percent] |
| 2026-09-19 22:10:15.060 | INFO | Performance | CPU 35.02 ms/frame (28.2 fps, worst 100.00 ms over 143 frames), RSS 810 MiB |
| 2026-09-19 22:10:15.061 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 35.0182 [ms] |
| 2026-09-19 22:10:15.063 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:10:15.065 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 28.2198 [fps] |
| 2026-09-19 22:10:15.066 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 143 [count] |
| 2026-09-19 22:10:15.070 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 809.984 [MiB] |
| 2026-09-19 22:10:15.072 | INFO | GpuTiming | GPU 25.81 ms total | cull 0.08 · raster 4.16 · HiZ 0.05 · resolve 0.48 · ReSTIR 21.04 · shadow 0.00 · post 0.86 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:10:15.075 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 25.8069 [ms] |
| 2026-09-19 22:10:15.077 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.078912 [ms] |
| 2026-09-19 22:10:15.078 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 4.15946 [ms] |
| 2026-09-19 22:10:15.080 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.048352 [ms] |
| 2026-09-19 22:10:15.086 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.483328 [ms] |
| 2026-09-19 22:10:15.087 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 21.0369 [ms] |
| 2026-09-19 22:10:15.091 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:10:15.093 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.860353 [ms] |
| 2026-09-19 22:10:15.094 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:10:15.096 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:10:15.099 | INFO | Visibility | Clusters 2046 tested -> 939 frustum, 939 cone, 939 visible | draws 939+0, 105118 triangles |
| 2026-09-19 22:10:15.101 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:10:15.103 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 939 [count] |
| 2026-09-19 22:10:15.104 | INFO | TelemetryMetrics | Measurement: ClustersCone = 939 [count] |
| 2026-09-19 22:10:15.107 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 939 [count] |
| 2026-09-19 22:10:15.108 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 105118 [count] |
| 2026-09-19 22:10:15.111 | INFO | TelemetryMetrics | Measurement: DrawCalls = 939 [count] |
| 2026-09-19 22:10:15.116 | INFO | Performance | CPU-BOUND or presenting-limited | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 82% of GPU frame |
| 2026-09-19 22:10:15.119 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:10:15.121 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:10:15.124 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:10:15.126 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:10:15.130 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:10:15.132 | INFO | TelemetryMetrics | Measurement: GpuBound = 0 [bool] |
| 2026-09-19 22:10:15.134 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 81.5164 [percent] |
| 2026-09-19 22:10:15.734 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: Hard @ 1024 px, 1 taps. |
| 2026-09-19 22:10:16.304 | INFO | Shadows | Shadow path: ReSTIR ray-traced (GI on - shadow maps idle; inline shadow rays are counted inside GpuReSTIRMs, so GpuShadowMs=0 is expected). Opaque rays use the bounded closest-hit path and spatial winners are revalidated at the current pixel. Tier stage if GI is switched off: Hard @ 1024 px, 1 taps. |
| 2026-09-19 22:10:20.699 | INFO | Performance | CPU 55.37 ms/frame (20.4 fps, worst 100.00 ms over 91 frames), RSS 841 MiB |
| 2026-09-19 22:10:20.701 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 55.3681 [ms] |
| 2026-09-19 22:10:20.703 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:10:20.705 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 20.3756 [fps] |
| 2026-09-19 22:10:20.707 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 91 [count] |
| 2026-09-19 22:10:20.712 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 841.43 [MiB] |
| 2026-09-19 22:10:20.714 | INFO | GpuTiming | GPU 69.61 ms total | cull 0.07 · raster 3.70 · HiZ 0.05 · resolve 0.45 · ReSTIR 65.34 · shadow 0.00 · post 0.93 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:10:20.720 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 69.6067 [ms] |
| 2026-09-19 22:10:20.725 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.068352 [ms] |
| 2026-09-19 22:10:20.726 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 3.70195 [ms] |
| 2026-09-19 22:10:20.729 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047904 [ms] |
| 2026-09-19 22:10:20.730 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.447296 [ms] |
| 2026-09-19 22:10:20.735 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 65.3411 [ms] |
| 2026-09-19 22:10:20.736 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:10:20.739 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.931328 [ms] |
| 2026-09-19 22:10:20.741 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:10:20.745 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:10:20.749 | INFO | Visibility | Clusters 2046 tested -> 939 frustum, 939 cone, 939 visible | draws 939+0, 105118 triangles |
| 2026-09-19 22:10:20.750 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:10:20.753 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 939 [count] |
| 2026-09-19 22:10:20.754 | INFO | TelemetryMetrics | Measurement: ClustersCone = 939 [count] |
| 2026-09-19 22:10:20.757 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 939 [count] |
| 2026-09-19 22:10:20.758 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 105118 [count] |
| 2026-09-19 22:10:20.765 | INFO | TelemetryMetrics | Measurement: DrawCalls = 939 [count] |
| 2026-09-19 22:10:20.766 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 94% of GPU frame |
| 2026-09-19 22:10:20.769 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:10:20.773 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:10:20.775 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:10:20.778 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:10:20.783 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:10:20.785 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:10:20.787 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 93.872 [percent] |
| 2026-09-19 22:10:21.858 | INFO | Shadows | Shadow path: rasterised maps (GI off) - Hard @ 1024 px, 1 taps. |
| 2026-09-19 22:10:22.275 | INFO | Shadows | Shadow path: rasterised maps (GI off) - Hard @ 1024 px, 1 taps. |
| 2026-09-19 22:10:25.911 | INFO | Performance | CPU 32.83 ms/frame (35.4 fps, worst 100.00 ms over 153 frames), RSS 819 MiB |
| 2026-09-19 22:10:25.912 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 32.8336 [ms] |
| 2026-09-19 22:10:25.914 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:10:25.916 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 35.4219 [fps] |
| 2026-09-19 22:10:25.917 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 153 [count] |
| 2026-09-19 22:10:25.919 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 818.883 [MiB] |
| 2026-09-19 22:10:25.920 | INFO | GpuTiming | GPU 29.77 ms total | cull 0.08 · raster 7.99 · HiZ 0.05 · resolve 0.79 · ReSTIR 20.85 · shadow 0.00 · post 0.75 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:10:25.923 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 29.7674 [ms] |
| 2026-09-19 22:10:25.925 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.081504 [ms] |
| 2026-09-19 22:10:25.926 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 7.9921 [ms] |
| 2026-09-19 22:10:25.928 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.047104 [ms] |
| 2026-09-19 22:10:25.931 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.79456 [ms] |
| 2026-09-19 22:10:25.933 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 20.8521 [ms] |
| 2026-09-19 22:10:25.936 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:10:25.937 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.750113 [ms] |
| 2026-09-19 22:10:25.940 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:10:25.941 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:10:25.943 | INFO | Visibility | Clusters 2046 tested -> 1808 frustum, 1808 cone, 1807 visible | draws 1789+18, 203714 triangles |
| 2026-09-19 22:10:25.947 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:10:25.948 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1808 [count] |
| 2026-09-19 22:10:25.949 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1808 [count] |
| 2026-09-19 22:10:25.951 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1807 [count] |
| 2026-09-19 22:10:25.952 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 203714 [count] |
| 2026-09-19 22:10:25.954 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1807 [count] |
| 2026-09-19 22:10:25.955 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 70% of GPU frame |
| 2026-09-19 22:10:25.958 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:10:25.959 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:10:25.963 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:10:25.964 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:10:25.965 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:10:25.966 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:10:25.968 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 70.0502 [percent] |
| 2026-09-19 22:10:30.905 | INFO | Performance | CPU 37.60 ms/frame (26.4 fps, worst 100.00 ms over 133 frames), RSS 870 MiB |
| 2026-09-19 22:10:30.907 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 37.5957 [ms] |
| 2026-09-19 22:10:30.909 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:10:30.911 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 26.4122 [fps] |
| 2026-09-19 22:10:30.912 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 133 [count] |
| 2026-09-19 22:10:30.914 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 869.602 [MiB] |
| 2026-09-19 22:10:30.916 | INFO | GpuTiming | GPU 33.82 ms total | cull 0.08 · raster 10.22 · HiZ 0.05 · resolve 0.38 · ReSTIR 23.08 · shadow 0.00 · post 0.82 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:10:30.918 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 33.8171 [ms] |
| 2026-09-19 22:10:30.922 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.084832 [ms] |
| 2026-09-19 22:10:30.923 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 10.2226 [ms] |
| 2026-09-19 22:10:30.925 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.0472 [ms] |
| 2026-09-19 22:10:30.927 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 0.382528 [ms] |
| 2026-09-19 22:10:30.928 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 23.0799 [ms] |
| 2026-09-19 22:10:30.930 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:10:30.931 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 0.818655 [ms] |
| 2026-09-19 22:10:30.933 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:10:30.938 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:10:30.940 | INFO | Visibility | Clusters 2046 tested -> 1992 frustum, 1992 cone, 1976 visible | draws 1976+0, 222904 triangles |
| 2026-09-19 22:10:30.943 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:10:30.943 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1992 [count] |
| 2026-09-19 22:10:30.945 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1992 [count] |
| 2026-09-19 22:10:30.947 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1976 [count] |
| 2026-09-19 22:10:30.949 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 222904 [count] |
| 2026-09-19 22:10:30.953 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1976 [count] |
| 2026-09-19 22:10:30.955 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 68% of GPU frame |
| 2026-09-19 22:10:30.959 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:10:30.961 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:10:30.962 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:10:30.964 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:10:30.969 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:10:30.971 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:10:30.972 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 68.2493 [percent] |
| 2026-09-19 22:10:36.133 | INFO | Performance | CPU 40.31 ms/frame (24.7 fps, worst 100.00 ms over 125 frames), RSS 889 MiB |
| 2026-09-19 22:10:36.135 | INFO | TelemetryMetrics | Measurement: FrameTimeMeanMs = 40.3089 [ms] |
| 2026-09-19 22:10:36.137 | INFO | TelemetryMetrics | Measurement: FrameTimePeakMs = 100 [ms] |
| 2026-09-19 22:10:36.139 | INFO | TelemetryMetrics | Measurement: FramesPerSecond = 24.744 [fps] |
| 2026-09-19 22:10:36.141 | INFO | TelemetryMetrics | Measurement: FrameSampleCount = 125 [count] |
| 2026-09-19 22:10:36.146 | INFO | TelemetryMetrics | Measurement: ResidentMemory = 888.895 [MiB] |
| 2026-09-19 22:10:36.147 | INFO | GpuTiming | GPU 38.99 ms total | cull 0.10 · raster 9.39 · HiZ 0.05 · resolve 1.81 · ReSTIR 27.64 · shadow 0.00 · post 1.18 · sky 0.00 · volume 0.00 |
| 2026-09-19 22:10:36.150 | INFO | TelemetryMetrics | Measurement: GpuFrameTotalMs = 38.9895 [ms] |
| 2026-09-19 22:10:36.152 | INFO | TelemetryMetrics | Measurement: GpuCullMs = 0.097536 [ms] |
| 2026-09-19 22:10:36.154 | INFO | TelemetryMetrics | Measurement: GpuRasterMs = 9.38906 [ms] |
| 2026-09-19 22:10:36.155 | INFO | TelemetryMetrics | Measurement: GpuHiZMs = 0.051584 [ms] |
| 2026-09-19 22:10:36.164 | INFO | TelemetryMetrics | Measurement: GpuResolveMs = 1.81453 [ms] |
| 2026-09-19 22:10:36.166 | INFO | TelemetryMetrics | Measurement: GpuReSTIRMs = 27.6368 [ms] |
| 2026-09-19 22:10:36.168 | INFO | TelemetryMetrics | Measurement: GpuShadowMs = 0 [ms] |
| 2026-09-19 22:10:36.169 | INFO | TelemetryMetrics | Measurement: GpuPostMs = 1.18374 [ms] |
| 2026-09-19 22:10:36.171 | INFO | TelemetryMetrics | Measurement: GpuSkyMs = 0 [ms] |
| 2026-09-19 22:10:36.175 | INFO | TelemetryMetrics | Measurement: GpuVolumeMs = 0 [ms] |
| 2026-09-19 22:10:36.177 | INFO | Visibility | Clusters 2046 tested -> 1877 frustum, 1877 cone, 1877 visible | draws 1877+0, 211576 triangles |
| 2026-09-19 22:10:36.181 | INFO | TelemetryMetrics | Measurement: ClustersTested = 2046 [count] |
| 2026-09-19 22:10:36.182 | INFO | TelemetryMetrics | Measurement: ClustersFrustum = 1877 [count] |
| 2026-09-19 22:10:36.184 | INFO | TelemetryMetrics | Measurement: ClustersCone = 1877 [count] |
| 2026-09-19 22:10:36.185 | INFO | TelemetryMetrics | Measurement: ClustersVisible = 1877 [count] |
| 2026-09-19 22:10:36.187 | INFO | TelemetryMetrics | Measurement: TrianglesDrawn = 211576 [count] |
| 2026-09-19 22:10:36.191 | INFO | TelemetryMetrics | Measurement: DrawCalls = 1877 [count] |
| 2026-09-19 22:10:36.196 | INFO | Performance | GPU-BOUND | 963 kpx x (1 candidates + 0 extra + 0 spatial taps), 4 denoise levels, present FIFO | kernel 71% of GPU frame |
| 2026-09-19 22:10:36.198 | INFO | TelemetryMetrics | Measurement: RenderPixels = 963030 [px] |
| 2026-09-19 22:10:36.200 | INFO | TelemetryMetrics | Measurement: ReSTIRCandidates = 1 [count] |
| 2026-09-19 22:10:36.202 | INFO | TelemetryMetrics | Measurement: ReSTIRExtra = 0 [count] |
| 2026-09-19 22:10:36.203 | INFO | TelemetryMetrics | Measurement: ReSTIRSpatialTaps = 0 [count] |
| 2026-09-19 22:10:36.207 | INFO | TelemetryMetrics | Measurement: DenoiseLevels = 4 [count] |
| 2026-09-19 22:10:36.208 | INFO | TelemetryMetrics | Measurement: GpuBound = 1 [bool] |
| 2026-09-19 22:10:36.213 | INFO | TelemetryMetrics | Measurement: ReSTIRShareOfFrame = 70.8827 [percent] |
