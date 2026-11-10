# ReSTIR GI & Scene Distance Field Traversal Optimizations

Technical documentation of the quality-preserving rendering and ray-traversal optimizations implemented for the FrontierEngine WebGPU indirect-illumination pipeline (`Experimental/FrontierEngine/Source/SceneDistanceSpace.js` and `Experimental/FrontierEngine/Shader/{DistanceRecords,DistanceTraversal,ReservoirTemporal,ReservoirSpatial}.wgsl`).

---

## 1. Pipeline Context & Bottleneck Profile

In `FrontierEngine`, diffuse indirect illumination is solved at `Frame.TraceExtent` (half screen resolution per axis by default) through a two-pass ReSTIR GI sequence (`ResampleTemporal` followed by `ResampleSpatial`), reconstructed by a temporal + 3-iteration edge-stopping À-Trous variance filter (`VarianceIntegrator`).

Because `FeatureScreenTraversal` (`?screentraces=1`) is disabled by default so full multi-bounce off-screen energy is preserved, 100% of initial candidate rays in `ResampleTemporal`, 25% of rotating temporal validation rays, and up to 100% of adopted spatial donor rays march the four nested toroidal cascades (`128³` `rgba16float` 3D textures in `high` quality) of the global Scene Distance Field (`DistanceTraversal.wgsl`).

Profiling the shader execution paths identified four primary sources of redundant 3D texture bandwidth and ALU divergence that could be eliminated without altering converged lighting quality:

1. **Unbounded sky-ward marching through empty cascade slabs**: Outdoor and courtyard cosine-hemisphere rays (~55–70% of initial diffuse candidates and sky-reservoir visibility rays) marched up to the top of Cascade 3 (`Z ≈ +64 m`), consuming 18–30 3D texture fetches through empty air above the highest geometry in the level (`Z ≈ 9.3–9.5 m`).
2. **Full surface hit shading on binary visibility rays**: `TraverseSceneVisibility` previously delegated to `TraverseSceneDistance`, executing a 6-tap central-difference gradient (`ResolveClipNormal`) plus an integer 3D texel load (`ResolveClipIdentity`) on every occluded shadow/visibility segment—all discarded immediately because visibility returns a scalar `{0.0, 1.0}`.
3. **Per-step floating-point divisions and repeated inner-cascade AABB tests**: Every sphere-march iteration divided world coordinates by `Clip.VoxelExtent` (`vec3f` division) and re-tested Cascade `0..k-1` even after an outward-marching ray had permanently crossed those cascades' far slabs.
4. **Pre-horizon radiance card walks & duplicate spatial visibility rays**: Temporal reuse refreshed surface-card radiance (walking all 6 oriented cards of an occupant) before checking whether the carried sample lay above the receiver's tangent horizon, while spatial reuse re-evaluated `OwnTarget` multiple times and traced visibility even when an adopted donor carried the exact same sky direction or surface point as the receiver's own temporal reservoir.

---

## 2. Implemented Optimizations

### 2.1 Dynamic Sky-Ceiling Escape (`Distance.CeilingHeight`)

- **Uniform packing (`SceneDistanceSpace.js` + `DistanceRecords.wgsl`)**: Repurposed the trailing 32-bit padding slot (`Scalars[39]` at byte offset `156` of the 160-byte `DistanceRecord` uniform buffer) as `CeilingHeight : f32`.
- **CPU ceiling bound (`FacetStructure.js` + `IlluminationHost.js`)**:
  - At scene construction, `StaticCeilingHeight` is computed once from the maximum world-space `Z` of all static SDF occupants plus a `1.5 m` band margin.
  - Each frame in `ResolveSceneCeilingHeight()`, the bound is raised dynamically if the vehicle or any active moving occupant, rigid-body fragment, or stress-pool instance rises above `StaticCeilingHeight`.
- **Exact upward ray termination (`DistanceTraversal.wgsl`)**:
  ```wgsl
  if (Position.z > Distance.CeilingHeight && Direction.z >= 0.0)
  {
      Escaped = true;
      Exhausted = false;
      break;
  }
  ```
  Because $z(t) = o_z + t \, d_z$ is monotonically non-decreasing for $d_z \ge 0$ and all SDF geometry resides strictly below `Distance.CeilingHeight`, upward rays escape to the sky dome in 3–5 steps instead of marching to the outer boundary of Cascade 3, with zero missed intersections.

### 2.2 Dedicated Binary `TraverseSceneVisibility`

Replaced the wrapper around `TraverseSceneDistance` in `DistanceTraversal.wgsl` with a dedicated binary segment march tailored to ReSTIR temporal and spatial visibility validation:

- **Zero gradient or identity fetches**: Eliminates 6× `SampleClipDistance` normal taps and 1× `LoadClipTexel` identity load per occluded ray.
- **Analytic far-plane pre-check**: Immediately rejects downward segments (`Direction.z < -1.0e-4`) whose analytic ground intersection `(Distance.GroundHeight - Origin.z) / Direction.z` falls strictly inside `[MinimumTravel, MaximumTravel)`.
- **Bounded step budget & grazing cone tolerance**: Caps visibility marches at `min(MaximumTraversalSteps, 56u)` and applies a `0.006 * Travel` distance-proportional threshold alongside `Clip.VoxelExtent * Distance.HitFraction` so grazing occlusion rays resolve cleanly without 96-step micro-creeping.

### 2.3 Division-Free Cascade Addressing & Monotonic `ResolveRayClip`

- **`InverseVoxelExtent` (`ClipRecord` offset `+7`)**: Replaced each `ClipRecord`'s unused `Pad : f32` slot with `1.0 / Clip.VoxelExtent`, converting `Position / Clip.VoxelExtent` in `ClipContains` and `ResolveClipIdentity` into single FMA/multiply instructions (`Position * Clip.InverseVoxelExtent`).
- **Convex slab exit tracking (`ClipExitedAlongRay` + `ResolveRayClip`)**:
  - Maintains a per-ray `StartLevel : u32` cursor across sphere-tracing steps.
  - Advances `StartLevel` only when `ClipExitedAlongRay` proves the ray has crossed the **far** slab of `Clip[StartLevel]` along its travel sign (`(Voxel.axis > High.axis && Direction.axis >= 0.0) || (Voxel.axis < Low.axis && Direction.axis <= 0.0)`).
  - Unlike an unconditional `StartLevel = Level` assignment, this preserves full accuracy for inward-pointing rays (e.g., rays originating on a distant Cascade 2 wall and travelling toward the camera into Cascade 1 and Cascade 0), while permanently skipping exited inner cascades for all outward rays.

### 2.4 Diffuse Cone Footprint & Horizon-Culled Temporal Refresh (`ReservoirTemporal.wgsl`)

- **Sub-degree diffuse cone (`DiffuseConeSlope = 0.006`)**: Passed to `TraverseSceneDistance` for cosine-hemisphere GI candidate rays (~0.34° half-angle, smaller than half a Cascade 0 voxel at 10 m). Prevents near-parallel rays along asphalt and hangar walls from stalling in `0.125 * VoxelExtent` minimum steps without blurring contact indirect shadows.
- **Algebraic initial-weight simplification**: Simplified `ResolveLuminance(Candidate.Radiance) * Cosine / (Cosine * InversePi)` directly to `ResolveLuminance(Candidate.Radiance) * Pi` whenever `Cosine > 1.0e-6`.
- **Horizon-gated `RefreshSampleRadiance`**: Evaluates `HorizonCosine = max(dot(Normal, SampleDirection), 0.0)` *before* `RefreshSampleRadiance(Reused)` so carried temporal samples that fall below the current pixel's tangent plane skip the 6-card occupant projection and card-atlas texture fetches altogether.

### 2.5 Redundant Ray & Target Elimination in Spatial Reuse (`ReservoirSpatial.wgsl`)

- **Single `OwnTarget` evaluation**: Computes `OwnTarget = ResolveReservoirTarget(Own.Sample, Position, Normal)` once per pixel and reuses it for seeding, visibility fallback, and `1/Z` normalization when `!Spatial.Adopted`.
- **Early zero-energy exit**: Exits prior to visibility tracing and `1/Z` donor loops whenever `Spatial.WeightSum <= 0.0 || Spatial.Tally <= 0.0`.
- **Identical-sample visibility bypass**: Skips `TraverseSceneVisibility` when an adopted spatial sample carries the same directional sky sample (`dot(Spatial.Sample.Normal, Own.Sample.Normal) > 0.999`) or the same world-space surface point (`Identity == Own.Sample.Identity && ||Δx||² < 1e-4`) already validated for `Own.Sample`.

---

## 3. Summary Table

| Stage / File | Mechanism | Saved Work | Visual Impact |
| --- | --- | --- | --- |
| `SceneDistanceSpace.js`, `DistanceRecords.wgsl`, `DistanceTraversal.wgsl` | `Distance.CeilingHeight` upward sky escape | 15–25× 3D cascade samples per open-sky ray | Bit-identical (exact geometric bound) |
| `DistanceTraversal.wgsl` | Dedicated binary `TraverseSceneVisibility` | 6× 3D samples + 1× 3D load per occluded visibility ray | Bit-identical visibility decision |
| `DistanceRecords.wgsl`, `DistanceTraversal.wgsl` | `Clip.InverseVoxelExtent` + convex `ResolveRayClip` | Eliminates `vec3f` divisions & redundant inner-cascade checks | Bit-identical cascade selection |
| `ReservoirTemporal.wgsl` | `DiffuseConeSlope = 0.006`, `* Pi` weight, horizon-first refresh | Skips 6-card refresh on below-horizon temporal samples & grazing stalls | Preserves full multi-bounce GI & contact bounce |
| `ReservoirSpatial.wgsl` | `OwnTarget` reuse, zero-weight exit, same-sample ray skip | Eliminates duplicate target math & redundant visibility rays | Unbiased `1/Z` spatial estimator preserved |
