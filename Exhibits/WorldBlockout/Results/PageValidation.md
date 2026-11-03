# World blockout page validation

Generated 2026-11-03T09:51:43.771Z · Node v22.22.3 · jsdom 24.1.3 · 76/76 checks passed.

The harness builds the real `index.html` in jsdom and evaluates `TerrainField.js`, `Raster.js`, `OceanField.js`,
`OceanMesh.js`, `Scenes.js` and `WorldBlockout.js` in page order. It clicks the real tools, chips, toggles and sliders,
dispatches real pointer events at the map, and reads the DOM and the record back.

| Check | Result | Observed |
|-------|--------|----------|
| no uncaught error while building | ✔️ pass | none |
| the masthead names the editor | ✔️ pass | World Blockout |
| the record line names the planet and its grid | ✔️ pass | Terra-01 · 6371 km radius · 512 × 256 cells |
| six vitals read the cursor | ✔️ pass | 6 vitals |
| the elevation vital carries its unit | ✔️ pass | Elevation753m |
| the stage bar names the tool | ✔️ pass | Raise |
| the status line reports the blockout | ✔️ pass | Terra-01 blocked out: four continents, three gulfs, one settled ocean patch. |
| five sculpt tools are offered | ✔️ pass | 5 tools |
| a tool is selected at boot | ✔️ pass | 1 pressed |
| eight stamps are offered | ✔️ pass | 8 chips |
| the continents are stamped as chips | ✔️ pass | 4 |
| the gulfs are stamped as chips | ✔️ pass | 3 |
| brushes have a radius and a strength | ✔️ pass | two sliders |
| the pad radius is adjustable | ✔️ pass | pad slider |
| clicking a tool selects it | ✔️ pass | Lower |
| the number keys select tools | ✔️ pass | Plateau |
| the first key returns to the raise brush | ✔️ pass | Raise |
| the radius slider writes through | ✔️ pass | 1500 km |
| the radius is shown in megametres | ✔️ pass | 1.50 Mm |
| the planet map drew | ✔️ pass | #map-frame svg |
| the map is a raster with an overlay | ✔️ pass | image present |
| the map carries a graticule | ✔️ pass | 23 lines |
| the map has an embedded PNG | ✔️ pass | data URL |
| the city pads are drawn on the map | ✔️ pass | 5 circles |
| a click moves the cursor | ✔️ pass | 0.0°, 0.0° |
| a click sculpts | ✔️ pass | revision 10 → 11 |
| a stroke raises the ground | ✔️ pass | 1871 m at 20°, 6° |
| the vitals follow the cursor | ✔️ pass | Elevation1,871m |
| undo reports in the status line | ✔️ pass | Undid one stroke (821 cells). |
| undo restores the ground | ✔️ pass | 1871 → 902 m |
| a continent chip stamps land | ✔️ pass | 17.9% → 18.0% |
| the stamp is reported | ✔️ pass | Stamped Eurasia: 121 cells, peak 1144 m. |
| a gulf chip carves water | ✔️ pass | Carved RedSea: 0 cells back under water. |
| the archipelago chip sows islands | ✔️ pass | Sowed 20 islands in the Sunda strait. |
| a pad on open water is refused | ✔️ pass | Below the waterline |
| the refusal explains itself | ✔️ pass | No pad here: below the waterline. |
| a refused pad is not recorded | ✔️ pass | 2 pads |
| a pad on dry ground is accepted | ✔️ pass | Dry ground, gentle slope |
| an accepted pad is recorded | ✔️ pass | 3 pads |
| the city sheet drew the pads | ✔️ pass | pad sheet |
| the pad carries its height | ✔️ pass | 772 m |
| the ocean readouts are live | ✔️ pass | 4 readouts |
| the ocean panel drew both figures | ✔️ pass | 2 figures |
| the resolution map is drawn | ✔️ pass | plan image |
| the resolution caption reports tiles | ✔️ pass | 1,762 tiles · 12 m near · 375 m far · 7168 fan triangles |
| the cross-section drew the surface | ✔️ pass | slice present |
| the triangle meter exists | ✔️ pass | meter |
| the mesh is inside its budget | ✔️ pass | 119936 / 120000 |
| the near field matches the solve | ✔️ pass | 63232 near triangles |
| the three tiers are all present | ✔️ pass | near/mid/far |
| a step advances the solve | ✔️ pass | 199.9 → 200.1 s |
| the solve peaked above zero | ✔️ pass | 1.99 m |
| far-field-only is on | ✔️ pass | pressed |
| far-field-only is reported | ✔️ pass | Far field only: the patch is still solving, but nothing on screen uses it. |
| far-field-only is flagged in the snapshot | ✔️ pass | flagged |
| far-field-only leaves nothing near | ✔️ pass | all weight 1 |
| the near field comes back | ✔️ pass | Near field back in the blend. |
| the c key toggles the far field again | ✔️ pass | on |
| the camera slider rebuilds the plan | ✔️ pass | plan rebuilt |
| a high camera asks for less than a low one | ✔️ pass | 119936 triangles |
| the record names the planet | ✔️ pass | planet stanza |
| the record carries the ocean | ✔️ pass | ocean stanza |
| the record names the solver | ✔️ pass | solver line |
| the record has no NaN | ✔️ pass | clean |
| the page offers a 3D tab | ✔️ pass | Map · 3D |
| the 3D canvas exists | ✔️ pass | canvas present |
| the map is the view the page opens on | ✔️ pass | map |
| switching to 3D marks the stage | ✔️ pass | stage is-3d |
| the 3D view draws both layers | ✔️ pass | 17384 land · 7444 water |
| the 3D view reports its frame | ✔️ pass | 3D view · drag to orbit · wheel to zoom |
| dragging turns the globe | ✔️ pass | 16.0° → 24.8° |
| the wheel changes the altitude | ✔️ pass | 9000 km → 6464 km |
| the three presets render | ✔️ pass | globe · coast · waves |
| the LOD paint is a mode, not a label | ✔️ pass | chip lit |
| the map comes back | ✔️ pass | map |
| detaching stops the ticker | ✔️ pass | ticker cleared |

**🟢 all checks passed**
