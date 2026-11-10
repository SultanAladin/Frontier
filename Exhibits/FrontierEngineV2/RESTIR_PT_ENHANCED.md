# FrontierEngine V2 — ReSTIR PT Enhanced

Implementation of **ReSTIR PT Enhanced** (Daqi Lin, Markus Kettunen, and Chris Wyman, *"Enhancing Spatiotemporal Resampling for Path Tracing"*, PACMCGIT Vol. 9, No. 1, Article 13, May 2026, `doi:10.1145/3804494`) inside `Exhibits/FrontierEngineV2/`, preserving complete feature parity with `Exhibits/FrontierEngine/` (Proving Grounds, Lobby Studio, `ControlVehicle`, Jolt Physics + XPBD tyres, and the Slate World Editor).

---

## 1. Architecture & Paper Mapping

| Paper Section | Technique | FrontierEngine V2 Implementation |
|---|---|---|
| **§3 / §3.2** | Reciprocal Spatial Reuse via Self-Inverting Gaussian Pairing Maps | `Source/PairingSpace.js` builds `N = 3` deterministic self-inverting involutions (`254²`, `230²`, `210²` layers in `rgba8uint`) matching Eq. (3) with per-frame `D_4` dihedral symmetries and toroidal offsets. |
| **§3 Pass 1** | Coupled Neighbor Shifts (`X_{B_i} -> A` and `X_A -> B_i`) | `Shader/ReservoirPairedShift.wgsl` + `EvaluateAnalyticPairedShift(...)` in `Shader/ReservoirRecords.wgsl`: pure-ALU zero-ray pass (`7` bindings, `0` BVH/SDF includes) writing compact `16 B` (`vec4f`) slots into `PairedShiftExchange`. |
| **§3 Pass 2** | Canonical Pairwise-MIS GRIS Merge (`∑ m_i = 1`) | `Shader/ReservoirSpatial.wgsl`: symmetric surface-similarity, depth, plane-distance, and non-empty donor guards before evaluating pairwise MIS, followed by at most **1** deterministic post-merge visibility ray (`ShiftReservoirPath`) across the entire spatial neighborhood. |
| **§4 / Eq. (5)** | Scene-Independent Dual Ray-Footprint + Single-Vertex Roughness Criterion | `ResolvePrimaryFootprintThreshold` and `EvaluateDualFootprintRatio` in `Shader/ReservoirRecords.wgsl` replace world-distance heuristics and allow reconnecting onto smooth vertices (`x_k`) whenever the prefix vertex `x_{k-1}` is rough (`α_{k-1} >= 0.20`). |
| **§5** | Sample Duplication Map & Adaptive Temporal Confidence Cap (`c_Cap`) | `Shader/ReservoirDuplication.wgsl` (`24×24` shared-memory tile over a `17×17` window of winning `PathSeed`s) feeds `ResolveAdaptiveConfidenceCap` in `Shader/ReservoirTemporal.wgsl`, ignoring healthy local sharing (`D <= 6/288`) so stationary views stay rock-steady while boiling/firefly duplication clamps rapidly. |
| **§6.1 & §6.3** | Unified Path-Tree Candidate + Marginalized RGB Vector Shading | Single-loop `TraceUnifiedPathCandidate` in `Shader/RayTraversal.wgsl` (`64 B` `ReservoirRecord`) with `1/B²` deeper-bounce stochastic NEE and marginalized RGB vector weights (`OwnVec + NeighborVec`) in `ReservoirSpatial.wgsl`. |
| **§6.2.3 & §6.4** | Forced NEE Light Reconnection & Dual Motion Vectors | Terminal bounce-1 NEE light reconnection (`PathFlagForcedNee`) and primary-motion + static-camera disocclusion fallback (`PreviousUnjitteredViewProjection`) in `ReservoirTemporal.wgsl`. |

---

## 2. Rendering & Stability Fixes Verified by GPU Execution

1. **Exposure `NaN`/`Inf` Collapse Prevention (`PackingCodec.wgsl`, `SurfaceReflectance.wgsl`, `DisplayExposure.wgsl`)**:
   - Guarded `PackHalfPair` / `PackHalfTriple` against `NaN`, negative values, and `f16` overflow (`> 65504 -> +Inf`), preventing poisoned reservoir texels from corrupting `DisplayExposure` log-luminance histograms into permanent black/white screens.
   - Guarded grazing `normalize(Light + View)` (`dot(Sum, Sum) > 1.0e-8`) in `SurfaceReflectance.wgsl` and added non-finite recovery in `DisplayExposure.wgsl`.
2. **Rough-Asphalt Specular Blotch & Delayed-Reconnection Radiometry Fix (`ReservoirRecords.wgsl`, `RayTraversal.wgsl`, `DisplayRecombination.wgsl`, `IlluminationHost.js`)**:
   - Kept primary ReSTIR GI reservoirs strictly demodulated diffuse irradiance (`E = L · cos θ`) while skipping dedicated multi-bounce reflection rays on rough surfaces (`Roughness >= 0.80`, e.g. asphalt `R = 0.82`).
   - Unified `ReconnectVertex == 3u` (`DelayedSample`) direction (`Sample.PrefixPosition - Position`), BSDF-weighted exitant radiance (`DelayedExitRad`), and `SelectionPdf` (`Pdf1`) across `TraceUnifiedPathCandidate`, `EvaluatePrimaryPathIntegrand`, `EvaluateAnalyticPairedShift`, and `ShiftReservoirPath`.
3. **Curved-Wall / Indoor Studio Blotch Elimination (`ReservoirTemporal.wgsl`, `ReservoirSpatial.wgsl`, `RayTraversal.wgsl`)**:
   - Prevented non-invertible temporal shifts (`RatioY < 1.0` on curved cyclorama coves or out-of-range Jacobians) from inflating `Current.Tally` by `+20` (`95%` darkening), counting `Prior.Tally` with zero weight only when an analytically valid shift is occluded or when `Prior` itself carried a zero-radiance sample.
   - Applied symmetric normal, depth, plane-distance, and valid-weight guards to `DonorPx` before accumulating `CanonicalDenom` in `ReservoirSpatial.wgsl`, and matched FrontierEngine's `* Pi` direct exitance convention across Sun, Fire, and clustered IES/studio floodlights.
4. **Pipeline & Frame-Schedule Acceleration (`IlluminationHost.js`, `ResamplingSequence.js`, `RayTraversal.wgsl`)**:
   - Compacted `TraceUnifiedPathCandidate` into a single loop (`1` `TraceSceneRay` + `1` NEE call site) and stripped unused ray/SDF includes from `ReservoirPairedShift.wgsl` (`0.21 s` Vulkan compile).
   - Replaced per-neighbor spatial shadow rays with `0` rays in Pass 1 + `<= 1` post-merge visibility ray in Pass 2, skipped redundant raster shadow cascades when RT shadows handle exterior smokeless lighting, and amortized static/bounded `Distance` + `Cards` updates post-warmup.

---

## 3. Verified SwiftShader Vulkan Benchmarks (V2 vs V1)

All measurements below were executed headlessly on Dawn's **Vulkan backend over SwiftShader** (`SIZE=320x180`, trace resolution `96×54`, `?capture=1&quality=low&tyres=analytic`, `0` WebGPU/WGSL errors, `0` warnings).

| Metric | FrontierEngine (V1) | FrontierEngine V2 (ReSTIR PT Enhanced) | Delta |
|---|---:|---:|---|
| **Proving Grounds Steady-State Mean GPU Frame Time** | `4,951.2 ms` | **`3,575.6 ms`** | **`1.38×` faster (`-27.8%`)** |
| **Proving Grounds Steady Path-Tracing Frame Time** | `4,951 ms` | **`3,237 ms`** | **`1.53×` faster (`-34.6%`)** |
| **Lobby Studio 8-Frame Total Render Time** | `207.7 s` | **`181.5 s`** | **`1.14×` faster (`-12.6%`)** |
| **Proving Grounds Mean Luminance (`0..255`)** | `148.74` | **`149.73`** | `+0.66%` radiometric match |
| **Pure Black / Pure White Pixels (`NaN` / Blowout)** | `0.00% / 0.00%` | **`0.00% / 0.00%`** | Zero exposure collapse |
| **Proving Grounds Full-Frame `MeanAbs` Diff vs V1** | — | **`1.75 / 255`** | `< 0.69%` deviation |
| **Temporal Frame-to-Frame Jitter (`Frame 9 -> 10 MeanAbs`)** | `1.63 / 255` | **`1.63 / 255`** | **Identical stability (`0` added jitter)** |
| **Automated Unit Suites (`npm test`)** | `115 / 115` pass | **`118 / 118` pass** | `+3` ReSTIR PT Enhanced suites |
| **Mode Validation Matrix (`Tools/ValidateModes.mjs`)** | `21 / 21` pass | **`25 / 25` pass** | `0` errors, `0` warnings |
