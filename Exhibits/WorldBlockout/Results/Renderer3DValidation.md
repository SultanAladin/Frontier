# Renderer 3D validation

Generated 2026-11-03T09:51:10.152Z · 37 checks · all passed

World: 23031 land cells of 131072, 6371 km radius. Patch: 112² at 14.3 m, anchored at 8.00° 4.00°.

| Frame | Altitude / distance | Land triangles | Water triangles | Milliseconds |
|-------|--------------------|----------------|-----------------|--------------|
| Globe | 9000 km altitude | 17,384 | 7,447 | 78 |
| Coast | 520.0 km out | 20,958 | 16,069 | 107 |
| Waves | 2.4 km out | 12,243 | 9,759 | 94 |

| Check | Result | Detail |
|-------|--------|--------|
| focal length matches the field of view | pass | 521.018 px against 521.018 px |
| the view basis is orthogonal | pass | forward · right · up are pairwise perpendicular |
| the view basis is unit length | pass | 1.000000 / 1.000000 / 1.000000 |
| the aim point projects to the centre of the frame | pass | worst 0.000000000 / 0.000000000 / 0.000000000 px |
| globe mode places the camera at its altitude | pass | 9000.000 km |
| globe mode looks at the planet centre | pass | cos 1.000000000 |
| globe mode has a usable right vector | pass | the straight-down basis does not collapse to NaN |
| Globe: the disc reaches past the horizon | pass | disc 18000 km against a horizon of 13988 km |
| Globe: the innermost ring is not a point | pass | first ring at 1800.0 m |
| Globe: the mesh has triangles | pass | 29,568 triangles, 84 rings of 176 |
| Globe: rings grow outwards | pass | outer ring 161.6° from the centre, ring eight 2.97° |
| Coast: the disc reaches past the horizon | pass | disc 2335 km against a horizon of 1839 km |
| Coast: the innermost ring is not a point | pass | first ring at 233.5 m |
| Coast: the mesh has triangles | pass | 29,568 triangles, 84 rings of 176 |
| Coast: rings grow outwards | pass | outer ring 21.0° from the centre, ring eight 0.39° |
| Waves: the disc reaches past the horizon | pass | disc 135 km against a horizon of 131 km |
| Waves: the innermost ring is not a point | pass | first ring at 13.5 m |
| Waves: the mesh has triangles | pass | 29,568 triangles, 84 rings of 176 |
| Waves: rings grow outwards | pass | outer ring 1.2° from the centre, ring eight 0.02° |
| the layer samples the field it was built from | pass | -3197.3 m against -3197.8 m |
| drawn depth is clamped against the view distance | pass | abyss draws at -28.80 km from 520 km out and -0.432 km from 2.4 km out |
| height above sea level is not clamped | pass | land keeps its sign and its order |
| Globe: the frame is not empty | pass | 64.0% of pixels carry scene |
| Globe: both layers contribute | pass | 17,384 land · 7,447 water |
| the globe is centred in the frame | pass | centroid 319.5, 199.5 of 640 × 400 |
| Coast: the frame is not empty | pass | 88.7% of pixels carry scene |
| Coast: both layers contribute | pass | 20,958 land · 16,069 water |
| Waves: the frame is not empty | pass | 100.0% of pixels carry scene |
| Waves: both layers contribute | pass | 12,243 land · 9,759 water |
| a frame is deterministic | pass | the same camera drew the same pixels twice |
| a frame renders inside its budget | pass | slowest of six frames 107 ms |
| no sea over a continent | pass | 0 water triangles over the Sahara |
| the sea changes the coast frame | pass | 66.8% of channels differ with water off |
| the LOD paint is a different picture | pass | 98.8% of channels differ |
| the water surface is not flat | pass | σ 0.467 m across 0.08° of patch |
| the viewport samples the solve at the patch and the spectrum away from it | pass | weight 0.000 at the focus, 1.000 four degrees out |
| the sea shows its waves in the pixels | pass | σ 7.5 of the green channel |
