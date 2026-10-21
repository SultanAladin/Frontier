# Clustered geometry and local-fog banding correction

## Research basis and scope

Vulcanite was inspected at commit [`3a48aa479a288b656f04ac9fb1ea5a1272050bfa`](https://github.com/bdwhst/Vulcanite/tree/3a48aa479a288b656f04ac9fb1ea5a1272050bfa). The actual files inspected were its README (builder, runtime error selection, visibility buffer, hybrid rasterizer), `mesh/Cluster.cpp`, and `shaders/glsl/pbrtexture/culling.comp`. The project is MIT licensed. This implementation is newly written; it does not transplant their Vulkan/OpenMesh/METIS code or assets.

The README describes the Nanite approach and links Brian Karis's [2021 Nanite talk](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf). That primary PDF and Epic's implementation were not directly retrieved in this sandbox's GitHub-only network. Do not interpret this note as a fresh audit of Unreal Engine source. Relevant public design principles: cluster triangles; group/simplify/recluster to build a shared-boundary LOD hierarchy; project geometric error for cut selection; GPU visibility/culling; retain instancing; rasterize only selected geometry. Vulcanite implements far more of this than the initial integration below.

## Implemented production path

- Static meshes with at least 256 triangles are partitioned by recursive longest-axis spatial median splits into **at most 128-triangle leaves**. This is a deterministic lossless partition, not METIS adjacency clustering or QEM simplification.
- Leaves carry local AABBs and indexed ranges into the **unchanged** existing non-indexed vertex archive. Every original triangle, material and vertex attribute is retained. Index storage is shared across instances/runs of each eligible mesh.
- Per-instance cluster records refer to current occupant transforms. A WebGPU compute pass transforms all eight AABB corners and rejects a cluster only when every corner is outside the same homogeneous clip plane. It supports reversed-Z's `0 <= clip.z <= clip.w` interval and near-plane crossings conservatively.
- The GPU writes standard 20-byte indexed-indirect commands. First instance remains zero; an instance-rate integer vertex buffer supplies the original occupant identity, so `indirect-first-instance` is not required. Primary surface rasterization calls **real `drawIndexedIndirect`**, not only a statistics simulation.
- Dynamic/deformed vehicle meshes, mutable reserved fragment/composite sets, small meshes and all other ineligible runs keep the original instanced draw path. Effects, shadow generation, ray tracing, physics and geometry archives are not replaced.
- Render settings exposes **GPU cluster frustum culling** for clustered/legacy A/B routing and reports cluster candidates/index payload. Candidate count is not a GPU visible-count measurement. The current live stresspool=0 fixture creates **32 candidates**, using **43,968 index bytes**, plus 48 bytes per candidate bounds record, 20 bytes per candidate command and four bytes per scene occupant identity.
- No per-frame readback is used. Bounds/index records rebuild when draw runs change; static per-mesh cluster partitions are cached. In-place mutable geometry stays on the legacy path, avoiding stale cluster caches on fracture.

## Explicitly not full Nanite

No hierarchy of simplified geometry / crack-free LOD DAG cuts, projected-error LOD selection, Hi-Z occlusion, compressed pages or streaming, vertex deduplication, visibility-buffer shading, mesh-shader path, software micro-triangle rasterizer or GPU work compaction. The median partition's internal nodes are not retained as a runtime BVH. CPU submission still visits candidate indirect draw slots, including culled slots. Tiny or mostly-visible meshes can cost more commands than instanced whole-mesh rendering. No desktop FPS improvement is claimed.

## Local fog: banding correction first

The upsampler previously rejected all guides beyond `max(.5 m, 2% of surface depth)` and returned the unfogged original if none remained. On sloping surfaces this can create abrupt fog/no-fog contours. Continuous inverse-quartic depth weights replace that hard rejection. A fog-only full-resolution analytic intersection guard prevents reconstructed fog from appearing on surfaces entirely in front of the fog volume. Sky/geometry guide classes remain separate. Mixed cloud/fog edge reconstruction is still approximate rather than a per-pixel full re-march.

Fog marching now limits spacing using volume size/selected quality, noise feature size and optical depth. Its lattice is anchored at the medium entry; shortening the surface endpoint changes only the final partial segment rather than re-spacing every sample along the ray. Work is capped at **192 samples** for fog-bearing intervals (96 otherwise). Early termination residual is .0005 for fog versus the prior .015. The two low-resolution RGBA16F targets are retained. More fog samples have real cost; no free quality/performance claim.

## Validation

- **99 unit tests passed**, including exact triangle coverage, conservative bounds and cluster capacity.
- Production cloud/fog compute + presentation on software Vulkan: all four fog shapes, self-shadow toggle, camera-inside, overlap, depth clipping, cabin/glass masking and lamp contribution passed with **0 GPU errors**.
- New presentation regression straddles the old guide rejection threshold: measured centre-pixel colour jump **0**, and fog persists on both sides. This isolates a concrete contour bug; it is not proof of absence of every possible band in the user's scene.
- Production cluster compute and indirect submission probe: **8 candidates, 4 visible, 4 culled**, **byte-exact rendered pixel parity** versus full-geometry legacy rasterization, **0 GPU errors**.
- Real full application runs clustered → legacy → clustered routing with local fog enabled: **3 frames, 0 errors / warnings**, both null validation and software Vulkan execution. Vulkan total 85.0 s at 480×270 requested / 288×162 internal. The run preceded the final anchored-lattice refinement; subsequent final-shader isolated GPU tests passed. These are correctness checks, not desktop FPS measurements.

Repeat from `Experimental/VehicleHmiPrototype`:

```sh
npm test
LD_LIBRARY_PATH=/tmp VK_ICD_FILENAMES=/tmp/vk_swiftshader_icd.json node Tools/CloudGpuProof.mjs
LD_LIBRARY_PATH=/tmp VK_ICD_FILENAMES=/tmp/vk_swiftshader_icd.json node Tools/GeometryClusterGpuProof.mjs
SIZE=320x180 HOOKS=Tools/ClusterHooks.mjs node Tools/HeadlessHarness.mjs . 3 '?capture=1&quality=low&stresspool=0'
```
