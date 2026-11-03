# World Blockout

A separate editor for blocking out a planet: sculpt the ground, stamp continents and gulfs at planetary scale, place
city
pads on ground that has been settled, and solve the ocean as water rather than as texture.

It is not a tab of the vehicle inspector. Blocking out a world is its own job, and a city needs its base before anything
about the city matters.

```
Experimental/WorldBlockout/
  index.html            the editor page (open it in a browser, no build step)
  WorldBlockout.css     InspectorPalette dark theme, DM Sans, the same tokens as the vehicle inspector
  WorldBlockout.js      tools, stage, ocean panel, pointer and keyboard handling
  TerrainField.js       planet record, height field, vertical SDF, stamps, brushes, city pads, mesh extraction, TOML
  OceanField.js         2.5D shallow-water solve, spectral far field, probes, blend, TOML
  OceanMesh.js          screen-space-error plan, quadtree tiers, skirts, far fan, budget accounting
  Raster.js             software rasteriser and a small deflate/PNG writer (no canvas, no dependencies)
  Scenes.js             every drawing the editor shows, as SVG — the page and the evidence renderer share it
```

## The workflow

1. **Sculpt the flat ground.** Raise, lower, plateau and smooth brushes over the height field. A stroke moves a third
of the
   planet's vertical range at full strength, so a coastline is three drags away. Every stroke is undoable; the stack
holds
   forty.
2. **Stamp continents.** Four authored outlines — Africa, Eurasia, South America, Australia — each with a plateau
height, a
   ramp in kilometres and a ridge amplitude. Stamps blend with `max`, so a second continent never digs a hole in the
first.
3. **Carve the gulfs.** Persian Gulf, Red Sea and Gulf of Guinea lower the ground along their outlines and blend back
up over a
   ramp: water where the planet says water is.
4. **Settle the ocean.** The patch under the viewer is a real 2.5D solve, driven at its boundary by the spectral sea
state.
5. **Place the pads.** A city pad is refused below the waterline, on ground steeper than nine degrees, or where its
rim would
   reach open water. An accepted pad is levelled flat inside three quarters of its radius.

## The planet record

| Field | Value |
|-------|-------|
| Name | Terra-01 · Ocean world · blocking pass |
| Radius | 6371 km |
| Grid | 512 × 256 (0.703° per cell, 78 km at the equator) |
| Sea level | 0 m |
| Floor | −4800 m |
| Shelf break | −110 m |
| Mesh spacing | 180 km |
| Seed | 20261103 |

The grid is coarse on purpose. A continent is a shape you can read at a glance, and one cell is wider than most of the
features a blockout author is placing. Anything finer than 78 km belongs to the mesh pass, not this one.

## Terrain: the SDF is the source of truth

The field stores heights; the interface the rest of the editor uses is a **vertical signed distance**:

```
SampleDistance(Lon, Lat) = SampleHeight(Lon, Lat) − SeaLevel
```

Negative is water, and the magnitude is the depth. That is what the ocean reads for its bathymetry, what the city
rules test
for a waterline, and what the band classifier maps onto a colour:

| Band | Height |
|------|--------|
| Abyss | below −3000 m |
| Deep | below −110 m |
| Shelf | below 0 m |
| Coast | 0–250 m |
| Plain | 250–900 m |
| Highland | 900–2000 m |
| Peak | above 2000 m |

A heightmap is allowed here precisely because the SDF is what everything else consumes: `ExtractMesh` triangulates the
field at
a spacing in kilometres, and the result is checked edge by edge (`ResolveManifold`) so a graph mesh is watertight
along every
interior edge and open only along its border.

## The ocean: two fields, one surface

The owner's requirement was explicit — a 2.5D water simulation, not a wave texture and not a coarse grid.

**① The solve.** A Saint-Venant patch around the viewer: surface elevation on cells, transport on faces, continuity and
momentum, wet and dry cells coupled to the terrain SDF, volume and energy tracked every substep. The Courant limit is
`0.45 · dx / sqrt(g·h)`, computed from the *deepest* water in the patch; a frame is subdivided until the step fits.

**② The spectrum.** Nine directional wave trains with deep-water dispersion (`ω = sqrt(g·k)`), evaluated analytically
at any
longitude, latitude and time. It drives the patch boundary — the wave maker — and stands in for everything past the
patch.

**Honest limitation.** Saint-Venant is non-dispersive. At four kilometres of water `sqrt(g·h)` is 198 m/s and the
patch could
only carry hundred-kilometre waves, so the solve runs on a mixed-layer depth clamped by `SolvableDepth` (90 m), which
keeps
celerity in a believable band. Dispersion is the spectral field's job. This is stated in the file, in the record and
here
because it is a modelling choice, not a bug.

Blending: weight 0 at the focus to weight 1 at 0.78 km, smoothstepped, with the blend finishing *inside* the patch —
beyond
that the spectrum is the higher-resolution thing on screen.

## The adaptive mesh

"Like Nanite, but for water" is a triangle-budget question, so the plan is a pure function of the camera:

- **Near** — the footprint of the solve. Always at the patch's own cell size (14 m), because a coarse grid there would
  throw
  the solve away. The near field bids for no budget; it is simply mandatory.
- **Mid** — a quadtree over a square around the viewer, split worst-screen-error-first by a max-heap until the
  triangle budget
  or the pixel target is met. Every tile carries a skirt deeper than the widest step to its neighbour, so a level
difference
  cannot open a crack.
- **Far** — a radial fan of concentric rings to the horizon: 7,168 triangles for the whole far field, shaded from the
  spectral
  field.

At the evidence settings (12 km span, 2 km camera, 2.5 px budget, 120,000 triangle budget):

| Tier | Triangles |
|------|-----------|
| near | 63,232 |
| mid | 49,536 |
| far fan | 7,168 |
| **total** | **119,936** |
| uniform grid at the near cell | 5,644,800 |

That is **47× fewer triangles** for the same surface, with the worst accepted screen error reported as 57.8 px in the
budget-limited mid field — the near field is exact, and the number is printed rather than hidden.

## Running it

Open `index.html` from a static server (the scripts are classic scripts, no modules, no build):

```
python3 -m http.server 8000 --directory Experimental/WorldBlockout
```

Then `http://localhost:8000/`. Keys: `1`–`5` select tools, `Z` undoes a stroke, `C` toggles far-field-only.

The record is TOML: `[planet]` with coverage, stamps and city pads from `TerrainField.EncodeRecord`, `[ocean]` with
the sea
state and solver settings from `OceanField.EncodeRecord`. The editor's **Export** button shows both.

## Limitations, stated plainly

- The solve is non-dispersive; see above. The mixed-layer clamp is a deliberate simplification.
- Water is a surface: no spray, foam, breaking waves or underwater refraction.
- The mid field is budget-limited at planet scale. Screen error is reported, not eliminated; the near field is the
  part that is
  never sacrificed.
- A city pad cannot be flatter than the grid it sits on. At 78 km per cell a small pad is one cell, which is why the
  editor
  levels pads of a few hundred kilometres.
- The terrain mesh is extracted and checked but not rendered in the editor — the stage draws the field as a shaded
  map, which is
  what a blockout pass reads.
- No GPU path yet. The solve, the spectral field and the plan are all CPU code that a renderer can port directly; the
  mesh
  arrays are already in the shape a vertex buffer wants.

## Validation

`Exhibits/WorldBlockout` holds the harnesses: 82 terrain checks, 54 ocean checks, 35 mesh checks and 65 page checks,
plus six
rendered figures. See `Exhibits/WorldBlockout/README.md` for the commands and the results.
