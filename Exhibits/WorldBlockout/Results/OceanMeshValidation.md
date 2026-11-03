# Ocean mesh validation

Generated 2026-11-03T09:48:51.651Z · Node v22.22.3 · 35/35 checks passed.

At the settings under test (12 km adaptive span, 2 km camera, 2.5 px budget, 120000 triangle budget) the mesh came out at **119,936 triangles** — 63,232 near, 49,536 mid, 7,168 far — against 7,372,800 for a uniform grid at the near cell size.

The harness requires the three ocean modules in Node, builds a plan from a camera, then asserts the budget, the tier
counts, the near field matching the solve resolution, resolution falling with distance, skirts covering the widest crack,
and a height refresh matching a full rebuild.

| Check | Result | Observed |
|-------|--------|----------|
| the plan produces tiles | ✔️ pass | 1762 tiles |
| the plan splits more than once | ✔️ pass | 587 splits |
| every tile has a screen error | ✔️ pass | finite |
| the plan prefers the solve footprint | ✔️ pass | 988 near tiles |
| near tiles reach the patch cell | ✔️ pass | 11.7 m near cell |
| the plan knows its pixel scale | ✔️ pass | 1037 px/rad |
| the mesh holds its triangle budget | ✔️ pass | 119936 / 120000 |
| the mesh spent most of its budget | ✔️ pass | 99.9% |
| the mesh has three tiers | ✔️ pass | 63232 near / 49536 mid / 7168 far |
| the near tier is a real share of the budget | ✔️ pass | 63232 triangles |
| the far tier is cheap | ✔️ pass | 7168 fan triangles |
| the mesh is cheaper than a uniform grid | ✔️ pass | 61× cheaper |
| the vertices are finite | ✔️ pass | finite |
| the mesh has vertices and indices | ✔️ pass | 76082 / 119936 |
| every index is in range | ✔️ pass | in range |
| every triangle has three corners | ✔️ pass | 359808 indices |
| tile triangles add up to the mesh | ✔️ pass | 119936 vs 119936 |
| near tiles are fine | ✔️ pass | worst 11.7 m |
| far tiles are coarse | ✔️ pass | best 375 m |
| resolution falls away with distance | ✔️ pass | 12 m near vs 375 m far |
| the finest cell is the near cell | ✔️ pass | 11.7 m vs patch 12.5 m |
| the plan reports its worst screen error | ✔️ pass | worst 57.8 px at budget 2.5 px |
| an unbinding budget meets the pixel target | ✔️ pass | worst 11.99 px over 4332 tiles |
| more budget means more tiles | ✔️ pass | 6796 vs 1762 |
| neighbouring tiles can differ by a level | ✔️ pass | 187.5 m widest step |
| the skirt covers the widest step | ✔️ pass | skirt 563 m vs step 187.5 m |
| a lower camera asks for more detail | ✔️ pass | 13948 vs 2044 tiles |
| a higher camera costs less | ✔️ pass | monotonic |
| a refresh moves the surface | ✔️ pass | moved |
| a refresh keeps the topology | ✔️ pass | 119936 triangles |
| a refresh matches a rebuild | ✔️ pass | 8.1e-4 m worst difference |
| the near vertices are the solve | ✔️ pass | weight 0.00 |
| the far vertices are the spectrum | ✔️ pass | weight 1.00 |
| the near vertices know the water depth | ✔️ pass | depth sampled |
| far-field-only leaves every vertex spectral | ✔️ pass | all weight 1 |

**🟢 all checks passed**
