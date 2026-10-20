# Preview startup repair — 2026-10-20

User observed a 404 for `node_modules/jolt-physics/dist/jolt-physics.wasm-compat.js`
and an indefinite “Starting” overlay. The static import failed before the host's existing
startup error handler could run. Dependencies under ignored node_modules are not persisted;
serving the page without restoring them was unsafe. Arena analytics/session messages are separate.

## Repair

- `Tools/Serve.py` checks the required Jolt module and runs locked `npm ci --ignore-scripts`
  when absent, before binding port 8080. Missing npm, failed install or incomplete restore stops startup.
- Dependency-free `Source/Bootstrap.js` dynamically imports the app and displays import errors,
  hiding the progress overlay even when the host module never executes.
- Restarted the no-store server on 0.0.0.0:8080. Jolt module returns HTTP 200.

## Executed verification

| Check | Outcome |
| --- | --- |
| Python dependency restore unit tests | 3 passed |
| Real npm restoration in isolated scratch folder initially missing node_modules | Passed |
| Chromium deliberately aborts Jolt request | Visible error, progress overlay hidden |
| Same browser reloads with Jolt available | App ready, rendered frames, actual light controls pass |
| Successful browser run errors | Empty list |

[Browser results](Results/Startup/Startup.json) ·
[Missing-module error capture](Results/Startup/MissingModule.png) ·
[Resumed rendering capture](Results/Startup/Resumed.png).
Rendered capture inspected: vehicle/course and actual light controls visible.
Chrome/SwiftShader is software rendering; this is startup/interaction evidence, not target-GPU FPS.

Reproduce from repository root:

```sh
python Experimental/IlluminationPrototype/Tools/Serve.test.py
python Experimental/IlluminationPrototype/Tools/Serve.py --port 8080
# Separate terminal, with Tools dependencies installed:
cd Experimental/IlluminationPrototype/Tools
BROWSER_VALIDATION=BrowserStartup.mjs node BrowserEnvironment.mjs
```

The existing 88-test/physics/null run predates this focused startup change; it was not rerun here.

## Follow-up: preview-route failure

User still saw the old Starting page and a Jolt `/node_modules/` 404, despite the direct server
returning 200. Direct localhost verification did not establish that the user's preview route worked.
The outer Arena page also reported network changes/session failures; those are outside this app's control.
The exact proxy/browser cause of the divergent 404 is not established.

The browser now imports `/Runtime/Jolt.js?v=1.1.0`, not `/node_modules/`.
Server startup copies the exact locked standalone module and its licence into ignored generated Runtime/;
no vendored dependency source is tracked. Node tests keep using the installed package.
Bootstrap/host entry URLs are versioned; initial caption identifies preview r3 to distinguish stale HTML.

Follow-up verification passed:

- 4 Python server tests, including byte-identical public module copy and retained licence.
- 4 real Jolt WASM tests.
- Chromium startup test explicitly blocks **all `/node_modules/` browser requests**, verifies a visible
  error when the public runtime request is blocked, unblocks the public route and successfully renders
  and exercises actual-light controls. Successful-run Errors is empty.
- Browser evidence in Results/Startup was refreshed. Public module endpoint returns HTTP 200.
- Port 8080 restarted. This verifies the route-independent browser dependency change locally;
  the user's remote preview/network still requires their reload and confirmation.

## Current repair: retained standalone distribution (preview r4)

The user then reached the new bootstrap but dynamic import still failed for the host module chain.
A dynamic-import error naming the host does not establish which transitive dependency failed.
The exact failing remote request remains unknown; it is not justified to declare the proxy at fault.

To eliminate that dependency on many source requests and ignored runtime files, the current loader
uses one retained **Archive/Application.js** browser distribution, including the pinned Jolt module/WASM.
Unlike the prior generated ignored Runtime/ file, this required public deliverable is retained with the
project. No browser request to node_modules, Runtime, or Source/IlluminationHost is needed.
The 4.3 MiB distribution is reproducible using Tools/BuildBrowser.mjs and locked esbuild 0.25.12;
source/output SHA-256 hashes are recorded in Archive/ApplicationBuild.json, Jolt's licence retained adjacent.
Server startup restores build dependencies as needed and rebuilds before binding the port.
Source code remains authoritative; the bundle is a derived browser deliverable, not a separate implementation.

The bootstrap checks the public bundle endpoint and reports HTTP status/MIME errors and full URL.
Initial loading caption identifies **preview r4**. Previous r3 Runtime routing is superseded.

Verification: 4 Python server tests and 4 real Jolt tests pass. Browser startup tests are refreshed to
simulate a bundle HTTP 404 and assert the exact resource/status is shown, then unblock the bundle while
blocking all node_modules, Runtime, and old host-module URLs and exercise rendering/light controls.
The browser evidence in Results/Startup is the latest execution. User's remote browser confirmation
remains pending; local success is not proof that their Arena transport is working.

Final r4 Chromium execution passed (exit 0): exact bundle 404 display, subsequent successful rendering
with the legacy dependency URLs blocked, and actual-light controls; successful-run Errors is empty.
