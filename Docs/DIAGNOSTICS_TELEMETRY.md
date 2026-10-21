# Session diagnostics, transforms and cluster inspection

Implemented in `Experimental/VehicleHmiPrototype` on 2026-10-21. Refresh the existing port-8080 preview.

## Diagnostics in the Render outliner group

The optional recorder starts when the editor shell is constructed. Select **Diagnostics** for:

- Session recording/pause, clear, remove/discard and reinstall.
- Recent frame interval / CPU update-and-encode / editor refresh graphs; recent median and p99 frame time.
- A separate editor/recorder CPU graph.
- Selectable raw GPU-pass graph, recent per-pass mean ranking, and per-pass-normalized heatmap. Missing pass samples are absent/gaps, not fabricated zeros.
- Current captured quality, screen/cloud dimensions, cloud controls and settings revision, plus recent event history.
- **Copy all CSV** and **Download CSV**. If iframe/browser clipboard permission blocks copying, a selected text area provides manual Ctrl+C / Cmd+C fallback.

The charts display bounded recent windows, but CSV exports **all retained rows and all columns**, not just the selected chart. Data is in memory; reload loses it. The 60,000-row cap stops recording with a visible status rather than silently overwriting early session data. Export or clear before continuing a full recorder. Removal detaches observers/GPU callback and discards rows; pause preserves rows. Recording remains browser-local. On an explicit **Download CSV** action, the CSV is sent to the same-origin preview server to create a normal HTTP attachment (helpful for embedded/proxied previews). Exports remain only in server memory for up to 10 minutes, with at most 8 files / 128 MiB total and a 64 MiB per-file limit. They are never written into the checkout or sent to a third-party service. Static hosting falls back to a browser Blob file.

### Data format

CSV uses a union of all row keys and standard quoted-cell escaping; vectors/objects are JSON inside quoted cells. Rows have a session ID, clear epoch, session-relative arrival time and settings revision:

- `frame`: rendered-frame ID, **unclamped** interval, FPS, CPU frame work, editor refresh JS duration, refresh flag, draw/instance/cluster counters, camera position and partial recorder self-timings.
- `gpu`: raw timestamp durations of passes actually measured, originating frame/settings revision, and originating sample time separate from asynchronous arrival time.
- `settings`: immutable full host-settings snapshot plus grade, actual screen/trace/cloud extents, device pixel ratio, cluster leaf budget and overlay state. Only changed snapshots are retained, checked each rendered frame and on editor-setting edits.
- `event`: session metadata, recording controls, inspector builds, object-transform commits, cluster edits, visibility changes, browser errors, uncaptured GPU errors/device loss, and browser long tasks where supported.

Late GPU readbacks from a cleared or removed session cannot contaminate the replacement recording. The raw-sample callback is captured before asynchronous mapping, so replacing/removing a recorder does not route an old sample to a new callback.

### Measurement limits

Existing HUD GPU values remain smoothed. Diagnostics uses raw **available** timestamp samples; it does not synchronously stall the device to collect every frame. New separate timestamp pairs cover **Present** (including cloud reconstruction) and **Cluster culling**. `Clouds` alone is not the complete cloud system cost; shadows and presentation are separate. Do not add pass values collected from different frames to claim total GPU frame time.

`ui_js_ms` wraps the editor's frame presentation work, including the gizmo and, when refreshed, readouts/charts/outliner. `ui_inspector_build` records inspector construction. These do **not** measure full layout/paint/compositor work or all event-handler time. Page long tasks cannot be attributed exclusively to UI. Recorder self-timings are partial, not a complete overhead benchmark. Charts refresh with existing 5 Hz editor updates; recorder removal and cluster-overlay disable support comparisons. No zero-cost or desktop-FPS claim.

## Object movement

Select an object from **Objects** (or vehicle/cone/prop/local-weather entry). Supported translation adapters expose actual scene/physics/settings state, not proxy-only transforms:

- World XYZ fields and **Frame selection** in non-native object inspectors.
- Drag coloured world axes, plane pads, or the central free-move handle.
- `G`, then optional `X/Y/Z`; Ctrl snaps, Enter/click confirms, Escape/right-click restores. `F` frames the target; `S` scales local weather volumes.
- Static shapes use world-bounds centres for the handle. This fixes baked-coordinate shapes previously presenting their handle at world origin.
- Static moves upload occupant records, invalidate affected distance bounds/lighting and refresh obstacle bounds on commit. Rigid bodies use the physics placement path.

The vector/modal interaction is adapted to this Z-up WebGPU editor from the hosted Frontier celestial demo's SVG fallback, inspected at `SultanAladin/Frontier-` commit **eaab23ef83075755dcf9246668d2740508826d31**, `docs/celestial/index.html` (transform section around lines 3525–3605). The root hosted page is a launcher; celestial holds the gizmo. No Three.js/WebGL overlay or external CDN dependency was introduced. This is not a complete port of the reference's 3D gizmo: arbitrary rigid-body rotation/scaling is not added. Analytic ground/card-only helper records are not offered as independent movable scene shapes.

## Geometry clusters in the Render outliner group

- Toggle the actual clustered/legacy render path.
- Optional projected **cluster AABB** overlay: green GPU-visible, red GPU-culled, grey pending/legacy. These are bounds, not triangle wireframes or simplified LOD colours.
- Visibility comes from actual indirect GPU command `InstanceCount` fields, copied/mapped asynchronously at most 4 Hz while the overlay is enabled. Rebuild revisions guard against stale results; recorder rows include sample revision/age. The overlay draws up to 2,000 candidate bounds; this display cap does not change rendering or full candidate statistics.
- Choose 64/128/256 maximum triangles per leaf to repartition, or use **Rebuild clusters** to invalidate cached partitions. Default remains 128. Camera movement updates GPU culling normally.

These remain lossless static clusters, **not full Nanite**: no simplification/error hierarchy, streamed pages or Hi-Z visibility. See `GEOMETRY_CLUSTERING.md`.

## Executed validation

- `npm test`: **108 passed**, including CSV escaping, settings immutability/revisions, limits, pause/clear/removal/reinstall, late-readback isolation, asynchronous callback ownership and baked-shape-centre translation.
- Production software-Vulkan cluster proof: **8 candidates / 4 visible / 4 culled**, exact legacy pixel parity, diagnostics readback matches commands. Repartition to 64 triangles produces **16 candidates**, still pixel-exact; **0 GPU errors**.
- Null-backend full-app six-frame integration: recorder capture/pause/resume/remove, cluster rebuild and inspector routing; **0 errors / warnings**.
- Real Chromium software-WebGPU integration: four initially recorded frames, raw Present/Cluster-culling samples, full CSV serialization, real-scene numeric move, axis drag, Escape restoration, leaf-budget control, pause/remove; **0 page errors**. Six total rendered frames, low quality / quarter render scale. The live scene initially reported 32 candidates / 30 GPU-visible. This is functional validation, not a hardware benchmark.
- Existing cloud/fog GPU regression still passes, **0 GPU errors**.

Optional tools: `Tools/DiagnosticsBrowserProof.mjs`, `DiagnosticsHooks.mjs`, `GeometryClusterGpuProof.mjs`, `CloudGpuProof.mjs`. Browser proof exports screenshots and CSV outside the repository by default (`/home/user/diagnostics-proof`).

## CSV download reliability follow-up

Download CSV now shows progress/errors, uses a normal HTTP attachment on the preview server, attaches the save link to the DOM, and retains a visible **Save CSV** retry link. **Show CSV / copy manually** exposes the complete selected text if a browser/iframe policy blocks file downloads. Browser-file fallback URLs are retained for up to ten minutes, not revoked after one second. Status says “download requested,” not “download completed,” because the page cannot confirm the browser saved a file.

Executed checks: actual Chromium downloads from the diagnostics button inside an iframe, UTF-8/escaped content, manual retry, selected full-text fallback, missing-API Blob fallback and download-disallowed sandbox copy fallback; **zero page errors**. Four Python endpoint/cache tests pass. Prototype suite remains **113 passing**; null-backend diagnostics bring-up **3 frames / zero errors or warnings**. The enclosing Arena frame's permissions cannot be overridden by application code.

## CPU attribution and live-refresh A/B isolation

Frame rows now include `cpu_phase_ms.physics_effects`, `camera_environment`, `scene_uploads_weather`, `tlas_update`, `lighting_schedule`, `gpu_encode`, `submit_readback` and `hud_editor`. These divide the existing synchronous update/encode block. They are wall-clock JavaScript timings, including synchronous API/driver work, not thread/process-wide CPU utilization. Physics/effects remains an aggregate, not individual tyre-solver attribution. Phase timers are installed only while the recorder is recording.

Nested `ui_phase_ms.gizmo`, `status_overlay`, `inspector_live` and `outliner` split editor presentation. Non-refresh calls normally include only gizmo work; `inspector_live` includes current inspector callbacks/charts. These fields are **inside** CPU `hud_editor` and `cpu_submit_ms`, not additional costs. Separate browser layout/paint/compositor work remains unattributed.

Each frame additionally records `editor_inspector`, `document_hidden`, `frames_in_flight`, `raf_skipped_gpu`, `physics_steps`, and `lightning_segments`. GPU skips count callbacks rejected by the two-frames-in-flight guard since the previous submitted frame. They are not recorded as zero-cost rendered frames.

Diagnostics now has **Pause live editor refresh (A/B)** / **Resume live editor refresh**. This pauses live readouts/charts/outliner/gizmo redraw while rendering and RAM recording continue. It records an `editor_live_refresh` event plus settings revision. The button remains interactive. It does not hide/remove the DOM or stop every event handler or HUD-string construction, so it is specifically a live-refresh A/B test, not a complete “no editor” benchmark. `?editorrefresh=0` starts with this switch off. Export before reloading: RAM sessions are still lost on reload, and recording stops rather than overwriting at the existing row cap.

Surface-deposit foundation fields: `deposit_chunks`, `deposit_volume_m3`, `deposit_pending_particles`, `deposit_recycled_particles`, `deposit_rebuild_ms`, `deposit_draws`. These report the particle→grid→mesh accumulation path, now fed by actual falling snow as well as the optional test patch. Snowfall fields: `snow_falling`, `snow_landed`, `snow_retained_at_contact`, `snow_cpu_ms`. Settings snapshots include snowfall parameters, enabled state and world centre; start/stop/clear/settings changes are events. `Snowflakes` measures the instanced GPU weather draw; it is not part of the cloud timer. `snow_cpu_ms` and `deposit_rebuild_ms` are already included in CPU `scene_uploads_weather`; do not add it twice. See `PARTICLE_SURFACE_GRID.md` for budgets, lifecycle and current rendering limitations.


The active snow path is now GPU precipitation (`GPU_PRECIPITATION.md`), not the legacy CPU shower. Fields: `precipitation_gpu_live`, `precipitation_cloud_births`, `precipitation_ground_landings`, `precipitation_vehicle_landings`, `precipitation_retained_slots`; asynchronous counters are read at most once per second. Settings snapshots include precipitation parameters and world region. GPU passes: `Precipitation simulation`, `Snow mounds`, `Cloud precipitation`. Compatibility `snow_falling`/`snow_landed`/`snow_retained_at_contact` fields now derive from those GPU counters. `snow_cpu_ms` is zero because the old CPU simulation is removed from the running app, not because submission/UI/readback costs disappear.


Celestial GPU port adds `precipitation_births`, `precipitation_bouncing`, `precipitation_bounce_events`, `precipitation_at_rest`, `precipitation_mesh_grains`. `precipitation_cloud_births` is a compatibility field populated only with cloud-masked emission enabled. The old `Snow mounds` GPU timer now measures the opaque settled-grain indirect draw, not a coating/height-field pass. Old `deposit_*` columns are compatibility zeros: that renderer is no longer constructed by the app. Counters remain asynchronous ≤1 Hz snapshots.
