# World Forge — validation results

Generated 2026-11-03T12:27:25.923Z · 22/22 checks passed.

| Check | Result | Observation |
|---|---|---|
| 01 Deterministic lattice noise | PASS | identical inputs reproduce -0.144480; seed change yields 0.131695 |
| 02 Coastline SDF and gulf carve | PASS | 963 inland / 1341 ocean samples; 191 inland cells converted to water by the gulf carve |
| 03 Blank vase is exactly flat | PASS | all 16,384 cells at -260 m (max deviation 0.00e+0) |
| 04 Continental blockout with ocean | PASS | 44.2% land, 55.8% ocean, 931 km coastline, elevation -3761…1845 m |
| 05 Heightmap sculpt / undo / redo | PASS | raise +383.3 m, undo restores 797.39 m, redo reapplies |
| 06 Ray-to-crust intersection | PASS | sphere-trace + bisection landed 0.00 m from the raster elevation |
| 07 Dual-contouring watertight genus-0 solid | PASS | V−E+F = 2, 3632 triangles, 100% outward normals |
| 08 Authored crust SDF → OBJ surface | PASS | 4,412 triangles from 8,192 voxels in 31 ms |
| 09 Camera-anchored rolling water clipmap | PASS | 9362× fewer cells than a uniform fine grid; Δx 6.25 m → 400 m |
| 10 Crust tessellation LOD | PASS | 49,152 triangles across 6 tiers, 171× cell saving |
| 11 Water volume conservation (500 steps) | PASS | relative drift 3.10e-13%, η range -0.021…0.042 m |
| 12 Wave celerity ≈ √(g·h) | PASS | measured 31.64 m/s vs theoretical 31.32 m/s (101%) |
| 13 Wet/dry shoreline stays dry | PASS | 4128 land cells, 0 flooded, 0 with velocity; 5088 wet cells |
| 14 Solver stability over sloping bathymetry | PASS | 260 steps, wind + shoaling bed, η -2.21…2.15 m, max /v/ 0.57 m/s, no non-finite cells |
| 15 Unreal-style fly camera | PASS | W 374 m, E 374 m, Shift+W 1496 m, pitch clamped, orthogonal basis |
| 16 Dependency-free PNG encoding | PASS | 16-bit heightfield 524,908 B; RGB evidence PNG validated |
| 17 Project document round trip | PASS | 5,104 B JSON, 6 SDF layers preserved |
| 18 GLSL ES 3.00 structure and declarations | PASS | 11 shader sources, 0 duplicate uniforms, 0 unbalanced scopes, ASCII-only identifiers |
| 19 Vertex/fragment varying interfaces | PASS | 6 program pairs checked, 0 type/name mismatches |
| 20 CPU→GPU uniform wiring | PASS | 55 bound uniform names all declared across 55 GLSL declarations |
| 21 WebGL2 render path (mock device) | PASS | 10 mode/view combinations composed, 95 draw calls, no CPU-side exceptions |
| 22 Editor DOM identifier contract | PASS | 62 referenced identifiers resolve against 97 markup identifiers; 0 missing, 0 absent selector groups |

## Evidence

- `Images/crust-blockout.png` — top-down raster of the authored SDF crust (512² from the real heightfield).
- `Images/water-eta-field.png` — simulated free-surface elevation η after 180 shallow-water steps (radial wave field).
- `Images/wave-propagation.svg` — crest transects at two simulated times with measured celerity.
- `Images/lod-savings.svg` — per-tier cell spacing and the computed cell-saving factor.
- `Results/validation.json` — full numeric measurements.
