# Geometry detail and workspace validation

## Evidence

- [`geometry-shaded.png`](Images/geometry-shaded.png) and [`geometry-topology.png`](Images/geometry-topology.png) —
  the same procedural mesh with shaded and triangle-overlay previews at 1440 × 900.
- [`geometry-far.png`](Images/geometry-far.png) — fewer actual drawn triangles at a far preview distance.
- [`geometry-compact.png`](Images/geometry-compact.png) and [`atmosphere-compact.png`](Images/atmosphere-compact.png)
  — the two editors at 758 × 548 with a split, right-docked control panel.
- [`geometry-mobile.png`](Images/geometry-mobile.png) — 390 × 844 split workspace.
- [`Results.json`](Results.json) — observed triangle counts, pixel differences and damping checks.

These are browser captures from the experimental HTML workspace, not a production renderer or a real engine LOD
implementation. The distance graph shows the desired retention; discrete tessellation steps mean actual drawn counts
can differ slightly. Material detail is a separate preview shader control, not automatically tied to geometry.

## Reproduce

Start the server from the [prototype README](../../Experimental/AtmosphereEditor/README.md), then run with Node.js,
Playwright Core and Chromium installed:

```sh
node Exhibits/GeometryDetail/GeometryDetailValidation.cjs
```

The local run used Linux, Chromium 153.0.8010.0, Playwright Core 1.64.1 and WebGL 2 on SwiftShader. It checks
navigation and state preservation, the split dock, chart dragging, keyboard access, accessible range semantics, marker
and response-curve updates, actual rendered pixels, shaded/topology modes, triangle
count versus distance and retention, distance constraints, independent material changes, camera lag and convergence,
reset, and browser/WebGL errors. Hardware GPUs, other browsers and production integration were not tested.
