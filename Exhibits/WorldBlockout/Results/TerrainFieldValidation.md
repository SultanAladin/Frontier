# Terrain field validation

Generated 2026-11-03T08:27:48.980Z · Node v22.22.3 · 82/82 checks passed.

The harness requires `Experimental/WorldBlockout/TerrainField.js` in Node and exercises the planet record, the height
field and its vertical SDF, the authored continent and gulf stamps, the brushes and their undo stack, the city pad rules,
the extracted mesh and the TOML record. Nothing here touches the DOM: this is the model the editor draws.

| Check | Result | Observed |
|-------|--------|----------|
| planet is a globe, not a plane | ✔️ pass | 6371 km |
| the grid is a real planet grid | ✔️ pass | 512 × 256 |
| one cell is under a degree | ✔️ pass | 0.703° |
| sea level is zero | ✔️ pass | 0 m |
| the floor is abyssal | ✔️ pass | -4800 m |
| the seed is fixed | ✔️ pass | 20261103 |
| a new field starts at the floor | ✔️ pass | min -4800 max -4800 |
| a new field is revision zero | ✔️ pass | 0 |
| the field carries its own sampling API | ✔️ pass | Height/Distance/Band |
| the field starts with no stamps or pads | ✔️ pass | empty |
| cell to longitude round-trips | ✔️ pass | 100.000 |
| cell to latitude round-trips | ✔️ pass | 50.000 |
| a cell reads back what was written | ✔️ pass | 800 |
| sampling at a cell centre returns that cell | ✔️ pass | 800.0 |
| the SDF is height above sea level | ✔️ pass | 800.0 |
| sampling wraps in longitude | ✔️ pass | wrapped |
| flat ground has no slope | ✔️ pass | 0.00° |
| a one-cell step is a slope | ✔️ pass | 1.53° |
| the band ladder names every depth and height | ✔️ pass | Abyss Deep Shelf Coast Plain Highland Peak |
| the band ladder is ordered from abyss to peak | ✔️ pass | Abyss → Deep → Deep → Shelf → Coast → Plain → Highland → Peak |
| noise is deterministic for a seed | ✔️ pass | equal |
| noise is bounded | ✔️ pass | in [0,1] |
| ridged noise stays inside its envelope | ✔️ pass | 0.575 |
| the outline test knows inside from outside | ✔️ pass | inside/outside |
| the outline distance is in kilometres | ✔️ pass | 221.8 km |
| stamping a continent moves cells | ✔️ pass | 7072 cells |
| the continent stands above the sea | ✔️ pass | 1110 m |
| the stamp is recorded for the outliner | ✔️ pass | 1 record |
| the stamp bumps the revision | ✔️ pass | 1 |
| the Sahara is dry | ✔️ pass | 895 m |
| the Congo basin is dry | ✔️ pass | 566 m |
| the Cape is inside the authored outline | ✔️ pass | 831 m |
| the Indian Ocean is still abyss | ✔️ pass | -4800 m |
| the interior stands above the coast | ✔️ pass | Sahara 895 vs Gulf of Guinea -2332 |
| stamping twice never digs a hole | ✔️ pass | 895 → 895 m |
| the second stamp only raised the record | ✔️ pass | 2 records |
| the archipelago lays twenty islands | ✔️ pass | 20 islands |
| an island is above the waterline | ✔️ pass | 587 m |
| carving a gulf lowers the ground | ✔️ pass | 314 → -138 m |
| the carved gulf is under water | ✔️ pass | -138 m |
| the gulf is recorded as a gulf | ✔️ pass | gulf record |
| a gulf deeper than the bed does nothing | ✔️ pass | 121 cells |
| coverage counts every cell | ✔️ pass | 131072 cells |
| the world is mostly ocean | ✔️ pass | 14.5% |
| the deepest point is the floor | ✔️ pass | -4800 m |
| land area is a plausible share of a globe | ✔️ pass | 231.6 M km² land |
| the raise brush lifts the ground | ✔️ pass | 295 cells to -3532 m |
| the brush has a radius | ✔️ pass | -4800 m outside |
| the lower brush digs down | ✔️ pass | -4166 m |
| the plateau brush flattens | ✔️ pass | 133 cells |
| the smooth brush runs | ✔️ pass | 132 cells |
| three strokes are on the undo stack | ✔️ pass | 4 snapshots |
| undo reports what it restored | ✔️ pass | 133 cells |
| undo puts the height back | ✔️ pass | 240 → 240 |
| the undo stack is capped at forty | ✔️ pass | 40 snapshots |
| open dry ground accepts a pad | ✔️ pass | Dry ground, gentle slope |
| a placed pad is recorded | ✔️ pass | 1 pads |
| the pad records its area | ✔️ pass | 180956 km² |
| the pad is levelled | ✔️ pass | 17.6 m spread across a degree of pad |
| a pad below the waterline is rejected | ✔️ pass | Below the waterline |
| a cliff rejects a pad | ✔️ pass | Below the waterline at 0.7° |
| a pad whose rim reaches water is rejected | ✔️ pass | Below the waterline |
| a pad can be removed | ✔️ pass | removed |
| the mesh counts its columns and rows | ✔️ pass | 222 × 111 |
| the mesh is triangulated | ✔️ pass | 49284 triangles |
| the mesh carries a vertex per grid point | ✔️ pass | 24976 vertices |
| the mesh positions are finite | ✔️ pass | finite |
| the mesh is kilometre-scaled | ✔️ pass | span > 2 km |
| the mesh is a closed sheet | ✔️ pass | 0 over-used edges |
| the only open edges are its border | ✔️ pass | 666 open / 666 border |
| watertightness is reported | ✔️ pass | true |
| a finer spacing makes more triangles | ✔️ pass | 198025 vs 49284 |
| the record names the planet | ✔️ pass | Terra-01 |
| the record carries the radius | ✔️ pass | 6371 |
| the record carries the grid | ✔️ pass | 512 × 256 |
| the record lists its continents | ✔️ pass | continent stanza |
| the record has no NaN or undefined | ✔️ pass | clean |
| the record lists its cities | ✔️ pass | city stanza |
| the same record sculpts the same planet | ✔️ pass | identical arrays |
| four continents are authored | ✔️ pass | Africa, Eurasia, SouthAmerica, Australia |
| the continents are dense outlines | ✔️ pass | 50/50/29/19 vertices |
| the gulfs are authored | ✔️ pass | PersianGulf, GulfOfGuinea, RedSea |

**🟢 all checks passed**
