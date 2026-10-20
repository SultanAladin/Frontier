# Rendering diagnostic log — 2026-10-20

User reports the original demos ran at 60 FPS on their PC while the combined BVH mode runs at 19 FPS.
This is a reported regression, not a proven matched-settings comparison or an optimal renderer.
60 FPS is a 16.7 ms frame budget; 19 FPS corresponds to 52.6 ms, about 3.16× as long per frame.
Earlier tiny-resolution software-adapter pass measurements do not explain the user's hardware result.

## Control

Port 8080 → Render settings → **Rendering diagnostics → Copy diagnostic log**.
Capture after warm-up while the slowdown occurs, then paste into the conversation.
Clipboard permissions in embedded previews may be denied; a modal offers selected, read-only JSON
for manual Ctrl+C / Cmd+C instead. Closing the modal removes it.

Snapshot includes adapter/browser, DPR/visibility, actual output/trace pixel dimensions, all settings,
smoothed FPS and frame intervals (median/P95/P99), CPU frame work and wheel-BLAS update time,
last submitted and smoothed GPU pass timings, sample index, scene/light/card counts, GPU errors
and the latest paired comparison if available. Session URLs and credential/environment contents are excluded.
This is app diagnostics, not a capture of Arena's outer-page console or every JavaScript error.

CPU frame-work measurement includes app scheduling/command submission, not GPU completion.
The wheel timer includes update/refit/upload submission. Frame intervals include scheduling and GPU waits.
GPU passes are asynchronous and not whole-frame totals; smoothed skipped-pass history can decay.
Missing timing data remains null/unavailable rather than zero. No frame rate target or speedup is claimed.

## Verification

- 90 Node tests passed, including diagnostic structure, snapshot independence, privacy and missing-data tests.
- Actual Chromium controls: mocked clipboard success captures parseable real host diagnostics;
  deliberately denied clipboard displays a selectable JSON modal. Both paths pass.
- Browser rendering/interaction completes with zero reported errors.
- Standalone public application rebuilt; port 8080 serves updated distribution.

[Actual copied snapshot](Results/Diagnostics/CopiedLog.json) ·
[Clipboard-denied modal capture](Results/Diagnostics/CopyLog.png) ·
[Browser report](Results/Diagnostics/Startup.json) ·
[Unit output](Results/Diagnostics/UnitTests.txt).

Reproduce: `node --test Experimental/IlluminationPrototype/Tools/*.test.mjs`.
From app Tools, run `BROWSER_VALIDATION=BrowserDiagnostics.mjs node BrowserEnvironment.mjs`
with port 8080 running and browser dependencies installed. Actual browser uses Chromium/SwiftShader;
that is functional evidence, not validation of the user's PC performance.
