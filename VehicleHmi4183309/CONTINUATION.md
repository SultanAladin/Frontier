# Continuation from 4183309 (unassignedinbox/Frontier, branch arena/bc01c1ed-frontier)

This tree is commit **4183309** vendored verbatim into `VehicleHmi4183309/`, then continued here.
The four later commits on that branch (078881e, 0abef14, b5a7aba, 523b7ce) were **not** copied —
per the owner they approximate; everything below was written fresh from the 4183309 sources.

## 1 · HMI demo — configure the vehicle (paint only)
- `Experimental/VehicleHmiPrototype/HmiDemo.html` (+ `Style/HmiDemo.css`, `Source/HmiDemo.js`):
  full-screen viewport = the renderer's own ReSTIR Lobby render, paint panel = the eight named
  finishes from the reference configurator (`PaintOptions`). Click a swatch, or drag it onto the
  car — both call `Cabin.Appearance.Apply({Colour})`, the same shared-material path the in-cabin
  tablet uses, so the live/driven vehicle recolours. No other vehicle controls (per brief).
- Single vehicle: `?hmi=1` (set by HmiDemo.html) makes `FacetStructure` skip constructing the
  four display cars entirely — draw runs, TLAS and distance field see the hero alone.
- Camera: the Lobby orbit (drag to orbit around the vehicle, scroll to zoom) — no chase/driving cam.
- Run: `python3 Tools/Serve.py` in `Experimental/VehicleHmiPrototype`, open
  `http://localhost:8080/HmiDemo.html` (WebGPU browser required).

## 2 · X-ray scan — where it lives
Visual effects only: `Frontier/Engine/Effects/VehicleInspection.h` (already at 4183309) —
Solid / Ghost 45% / Wireframe / Scan (travelling band), read by presentations, never by physics
or session. CPU proof: `LobbySession --xray`, still in `VisualProof/XRayScanPreview.png`.
The HTML side is the same family in `VehicleAppearance.View` + `InspectionMarch.wgsl`.

## 3 · Fluid/gas + particles — C++ 1:1 ports (fresh, from the JS/WGSL at 4183309)
- `Frontier/Engine/Effects/GasParameters.{h,cpp}` — all 33 catalogue entries from
  `SmokeSequence.js GasParameterCatalogue` (key/label/unit/default/min/max/step, in order),
  slider clamp+snap, the six `GasPresets` from `InspectorPages.js`, and `ResolvePlumeSettings`
  = `MapPlumeSettings` verbatim (incl. `PlumeDensity = 1.2×Density`,
  `PlumeFlameDecay = −ln(max(0.005, 1−FlameLoss))`, pressure sweeps `max(4, 2·round(n/2))`).
- `Frontier/Engine/Effects/PlumeSolver.{h,cpp}` — CPU mirror of `PlumeAdvection.wgsl` +
  `PlumeVorticity.wgsl` + `PlumePressure.wgsl`: semi-Lagrangian advection, saturated buoyancy
  `B·g/(1+0.125g) − 0.12·(density+soot)`, curl confinement, warm-started Jacobi with the **h²**
  factor (`q′ = (Σq − ∇·u·h²)/6`), gradient projection, `EncodeRecord` retention factors,
  40 m/s speed ceiling, PlumeGrades Low→Ultra verbatim.
- `Frontier/Engine/Effects/ParticleSpace.{h,cpp}` — Particle Editor runtime from
  `EffectSequence.js`: the three presets verbatim, settings defaults, 8-emitter pool,
  1920 vehicle cells / 216 box cells, closed-form drag-limited flight along anchor normals.
- Validation: `Validation/SessionValidation.cpp` — **84 checks, 0 failures** (build below).

## 4 · Render optimisation — TLAS static skip (the careful version)
`RayTracingSpace.UpdateTlas` now caches the exact leaf inputs (occupant index, BLAS root,
world bounds in Float64, ground included). Bit-identical inputs ⇒ the deterministic builder's
output is byte-identical, so the rebuild **and** upload are skipped (`RayTracing.TlasSkips`
counts them). Any mover — including the ~6 mm parked-car settle — changes a bound and the full
rebuild runs; the vehicle is **not** frozen/slept to force skips (the earlier breakage route).
Tests: `Tools/TlasSkip.test.mjs` (skip invisibility, 6 mm mover, BLAS-root change).
JS suite: 99 pass / 3 fail — the same 3 environment failures as pristine 4183309 (96 pass).

## Build & test
```sh
g++ -std=c++17 -O2 -I. Validation/SessionValidation.cpp Frontier/Engine/Input/*.cpp \
  Frontier/Engine/Scene/*.cpp Frontier/Engine/Session/*.cpp Frontier/Engine/Effects/*.cpp \
  Frontier/Game/SoftwarePresentation.cpp Projects/Everest/Source/Session/*.cpp -o /tmp/SessionValidation
/tmp/SessionValidation   # 84 checks, 0 failures
cd Experimental/VehicleHmiPrototype/Tools && node --test TlasSkip.test.mjs VehicleAppearance.test.mjs
```
Note: GPU frame timing still needs the owner's GTX — this sandbox has no GPU, and SwiftShader
FPS says nothing about real hardware (per the TL;DR). No FPS claim is made here.
