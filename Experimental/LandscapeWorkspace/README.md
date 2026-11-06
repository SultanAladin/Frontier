# Landscape workspace prototype

Browser prototype requested for mask-first, volumetric landscape authoring. It does not modify the native editor.

## Run

Requires Node.js 20.19+ or 22.12+, npm, and a WebGL2-capable browser.

```bash
cd Experimental/LandscapeWorkspace
npm ci
npm run dev -- --port 5173
```

Open the displayed server address. `npm test` runs distance-field checks; `npm run build` creates ignored `dist/`.

## Workflow

- Fresh documents open the landscape with fractal, ridged multifractal and sand-dune generators already applied.
  **New empty world** removes all authored terrain.
- Paint with **B**, erase with **E**, pan with **H**, middle mouse, or Space-drag. Wheel zoom anchors to the pointer.
- **R** selects a rectangular storage region; **Add region to layer** retains it on the active layer.
- Select layers on the left; toggle their visibility independently. Add layers with **+**.
- **Build landscape** evaluates all visible, region-bounded masks as closed 3D signed-distance solids.
- Drag to orbit, choose **Pan** or use MMB/Shift-drag to pan, wheel to dolly, **F** to fit.
- Enable **WASD camera** for optional yaw-relative movement; Q/E moves vertically, Shift accelerates. Speed is in m/s.
- **Applied terrain examples** loads eight already-applied generator masks after confirmation; undo restores the document.
- Underwater depth extends the foundation from Y = 0 down to −Depth; it never raises the terrain. Mask stack operations add relief and genuine 3D overhangs/cavities.
- The legacy layer-wide cavity switch remains for older documents; use a tunnel operation in the active mask’s stack.
- Ctrl-Z / Ctrl-Shift-Z undo and redo document changes. Strokes and bounds autosave locally.
- Export/Open preserves vector strokes, region coordinates, material colours, and sampling preferences.
- Choose **Region to bake**, then **Bake selected region at full resolution** to download `Landscape.sdf.gz`.
- Baking runs in a background thread with progress and cancellation. Continue editing while its captured snapshot bakes.
- **Export small voxel preview** retains the earlier 64 × 32 × 64 JSON preview option.

## Mechanism

```text
World-space capsule strokes → ordered union/subtraction → per-region foundation
                          → ordered 3D landform CSG → rounded boundary intersection
                          → GPU ray marching / sparse signed-distance sampling
```

No heightmap or global voxel array exists. The rectangle clips storage extent; the painted shape defines the coast.
Each mask layer owns one independent landform stack; its regions share that stack and retain individual edge widths.
The browser canvas is only a display raster; authoritative strokes remain world-space vectors.
The field is resolution-independent. Standard CSG min/max operations preserve solid boundaries and conservative
marching distances; this is not a globally re-distanced exact Euclidean field at CSG seams.

The WebGL2 ocean uses nine world-space wave directions with different frequencies, amplitudes, phases, and speeds.
Normals, Fresnel reflection, sun highlights, fog, and shoreline foam are procedural; no ocean texture tiles are loaded.
This is an approximate raster/ray-marched ocean, not a fluid simulation or validated spectral ocean solver.

## Deliberate prototype limits

| Concern                  | Current implementation                                           |
|--------------------------|------------------------------------------------------------------|
| Full-resolution baking   | Selected dimensions sampled adaptively; 1024 × 128 × 1024 verified |
| Export safety budget     | 256 MiB raw; larger/complex regions can fail explicitly            |
| Actual voxel export      | Float32 truncated SDF, gzip binary; small JSON preview also kept   |
| GPU authoring capacity   | 512 capsule samples total, 16 layers, 16 regions per layer        |
| Infinite world           | Unbounded navigation; finite floating-point coordinate precision  |
| Landscape surface        | Mask-owned signed 3D landforms; no physical erosion port yet         |
| Biomes                   | Layer names/colours, not biome-specific erosion/material solvers  |
| Mask operation limit     | Eight combined mask/legacy operations per boundary; 2048 compiled primitives globally  |
| Physical GPU performance | Not measured; browser checks use software SwiftShader            |
| Native editor integration| Not implemented; this is an isolated browser prototype           |

Hardware drawing is capped at 30 updates/s and 1200 internal horizontal pixels; software rendering uses 360 horizontal
pixels (240 for documents above three masks) and at most one update/s to keep validation and fallback operation
practical. The viewport quality selector
can override internal width to 360, 720, or 1200 pixels. GPU fences allow only one application render in flight.
No performance guarantee is claimed.
The sparse preview omits all-positive bricks; absent bricks mean empty occupancy, not reconstructed exact distances.

## Full-resolution volume export

The sampler subdivides an integer octree, skipping space outside the rectangular volume dimensions. A distance-bound
sphere test collapses blocks provably outside the truncation band into constant positive or negative leaves.
Other leaves sample every cell of an 8³ brick. Uniform interior and exterior leaves explicitly cover their cells;
they are not missing geometry. This is a truncated 3D signed-distance field, not a heightmap or occupancy-only volume.

- Dimensions follow **X × Y × Z**; horizontal resolution controls X and Z independently of vertical resolution.
- Truncation band is two times the largest world-space cell pitch. Distances inside the band retain Float32 samples;
  farther distances saturate at the signed band limit.
- A bake captures the active layer and selected region at start. Later layer, stroke, and resolution edits do not
  change the file in progress. Cancel terminates sampling and compression and discards partial chunks.
- Gzip compression streams in the background; the UI retains compressed chunks only until the completed download.
- The 256 MiB limit is on uncompressed encoded bytes. No dense 1024³ allocation is performed.
- High resolutions and intricate masks may exceed the budget. This fails visibly; resolution is never silently reduced.
- The viewport still evaluates the analytic field. Loading baked volumes into the renderer is not implemented.

### Binary format

Decompress `Landscape.sdf.gz` with any gzip decoder. The first UTF-8 JSON line gives format, dimensions, world origin,
cell pitch, truncation band, source layer, selected region, and encoding details. Each following leaf stores:

| Offset | Type       | Meaning                                                    |
|--------|------------|------------------------------------------------------------|
| 0      | uint32 LE  | Category: 1 = constant leaf, 2 = sampled brick              |
| 4–12   | 3 uint32 LE| Cell-space X, Y, Z origin                                  |
| 16     | uint32 LE  | Cubic width in cells                                       |
| 20     | Float32 LE | One constant, or width³ samples; X fastest, then Y, then Z |

All in-bounds cells are covered exactly once. Sample position is `Origin + (Cell + 0.5) * Pitch`.
`SparseVolume.js` includes a decoder and cell-query routine; its tests compare reconstructed samples against the
analytic field, including cavities and erased strokes. The older JSON preview remains a separate, lower-resolution
occupancy-brick format and should not be confused with the full-resolution TSDF format.

## UI reference and evidence

Theme reference inspected: `SultanAladin/Frontier-`, `Experimental/FrontierEditor/style.css` through GitHub API.
The prototype follows its charcoal surfaces, muted borders, small uppercase labels, and restrained controls, with the
requested layer-stack / centre viewport / right inspector arrangement. It is not a pixel-identical native UI port.

Retained captures, interaction checks, environment details, and reproduction:
[`Exhibits/LandscapeWorkspace/README.md`](../../Exhibits/LandscapeWorkspace/README.md).

## Mask-owned landform stacks — 2026-11-06

Select a mask layer on the left. Its own landform stack expands directly beneath it; **+ Add landform layer** or
**Add to active mask stack** adds to that mask, even before it has painted regions. Selecting a boundary changes only
its edge controls. Newest operations appear on top and evaluate last; the fixed mask foundation evaluates first.
Use the arrows to reorder, the checkbox to bypass, and × to delete. Relief/size, spatial density, structural detail,
azimuth, CSG blending, and deterministic seed affect the actual field and persist in project export and voxel bakes.

| Landform                 | Volumetric construction                                      |
|--------------------------|--------------------------------------------------------------|
| Arête ridges             | Seeded conic peaks and side spurs with 3D structural displacement |
| Mesa / plateau           | Rounded structural blocks                                    |
| Fault escarpment         | Normalised half-space solid                                  |
| Aeolian dunes            | Conservative ellipsoid solids                                |
| Impact crater bowls      | Ellipsoid excavation                                         |
| Rock stacks & hoodoos    | Stems and wider overhanging caps                              |
| Stratigraphic ledges     | Alternating-width 3D beds                                     |
| SDF caves & undercuts    | Volumetric capsule subtraction                               |
| Volumetric arch          | Vertical torus solid with genuinely empty space under its roof |

Union, subtract, intersect, and field blend are signed-volume operations, not height-channel arithmetic. This is a
basic geometric adaptation, not numerical/visual parity with the complete reference terrain generator collection.
Seed, defaults, five original procedural family labels, and bottom-to-top stack conventions come from the pinned
source; the newly adapted volumetric implementation remains local and independently tested.

### Edge interpolation

**Boundary edge width = 0** preserves the original sharp region/mask intersection. Increasing the width rounds the
rectangular corners and uses smooth signed-field intersection for an inward solid-to-coast transition. This changes
geometry, not merely canvas opacity or a blurred image. It never adds terrain outside the mask, erased holes, or
selected bounds. Large widths can remove narrow islands; lower the width when preservation is preferred.
Operation **CSG blending** separately rounds joins between features; zero keeps hard joins. These widths are expressed
in the conservative field's units, so they are not guaranteed to equal an exact Euclidean fillet radius on every shape.

### Multi-layer scope and future simulation

Tick mask layers on the left or Ctrl/Cmd-click their rows. **Add to selected mask stacks** appends independent
operation copies to those selected masks, never to individual boundaries or unselected masks. The scope summary
states the selected mask and boundary counts. A limit/validation failure rejects the whole batch atomically.
This same explicit scope enumeration is available for future simulations; no hydraulic, thermal, glacial, or other
physical simulation has been imported or run in this change.

### Geometry / baking contract

Region fields are intersected with their own rounded boundary and painted mask after stack evaluation. Domain-displaced
primitives are divided by analytic gradient bounds, preserving conservative unit-Lipschitz fields for sparse culling.
A selected-boundary bake includes the parent mask stack, but excludes other boundaries’ legacy refinements, even
where their rectangles overlap. Old `Region.Landforms` arrays remain explicit imported modifiers after the mask stack;
use **Clear imported boundary modifiers** only when you deliberately want to remove them.
Export vertical bounds account for landform support, domain displacement, and cumulative CSG blend expansion, rather
than silently clipping relief to the old foundation slab. The same compiled primitives drive CPU baking and GPU preview.
Legacy projects with no `Landforms` or `Edge` fields continue to open as the original sharp foundation volumes.

Pinned source and attribution: [`ReferenceSnapshot/README.md`](ReferenceSnapshot/README.md), also recorded in root
`CLAUDE.md`. Focused browser checks: `Exhibits/LandscapeWorkspace/MaskStackBrowserValidation.mjs`.


### Secondary saved stacks

The bottom library shows actual WebGL2 renders of six recipes: **Desert mesas**, **Alpine ridges**, **Dune sea**,
**Basalt terraces**, **Impact basin**, and **Coastal arches**. Select a card, then use **Replace active stack with
preset**
or **Append preset layers**. Only `Layer.Landforms` changes: strokes, bounds, edges, colours and bake settings stay
intact.
Selecting cards alone never modifies the document. Double-click a card or use **Enlarge** for the rendered preview and
operation list. **Replace selected mask stacks** is a separate, explicit batch action; every mask gets independent
copies.

**Load example world** asks for confirmation before replacing the document with six separately editable preset masks.
It is undoable using the always-visible header controls or Ctrl-Z. Export first to keep a separate project file.
**Focus active mask** frames the selected mask; **Fit world** frames all visible masks.

Preview source is `PresetExamples.html`; reproduce the six PNGs with
`LD_LIBRARY_PATH=/tmp/lib node Exhibits/LandscapeWorkspace/GeneratePresetPreviews.mjs` from the repository root,
with the dev server running and the browser dependencies in the exhibit instructions installed. Images are rendered
in memory before files are written, avoiding Vite reloads during capture. These are basic geometric recipes, not
physical erosion simulations or complete visual parity with the pinned reference.


## Procedural terrain generators and source-aligned UI — 2026-11-06

The latest correction is about generators applied to terrain, not a preset-gallery showcase. The primary example
scene uses real 3D lattice-noise fractal, multifractal, ridged multifractal, badlands, ridge-noise, fractal-ridge and
sand-dune fields, plus signed crater excavation. **Applied terrain examples** loads eight editable masks with those
operations already applied. Existing saved documents are preserved until you explicitly choose replacement.

The active operation inspector exposes **Generator / operation**, amplitude, frequency, roughness, seed and CSG mode.
Changing the generator edits that operation and uses its default CSG mode; other numeric parameters are retained.
Arches/caves/stacks/ledges are separated as optional SDF structures, not the terrain examples. Saved-stack recipes
remain compatible but are secondary and collapsed initially.

`TerrainNoise.js` shares matching seeded 3D lattice interpolation/octave formulas with GLSL; conservative gradient
bounds preserve volumetric field sampling. This is not a heightmap or a full reference-generator/physical erosion port.
Noise fields use up to 640 marching steps; geometric-only scenes retain 160. Software fallback may be slow.

The UI was revised against the pinned source's actual `index.html` and `src/app/style.css`: neutral charcoal docks,
squared outliner entries, cyan selection, active/muted counts and explicit generator properties. It is source-aligned,
not pixel-identical or feature-complete parity. Navigation now has explicit Orbit/Pan buttons and opt-in WASD controls.

Current verification: 44 local tests; 9 new terrain/navigation, 15 mask-stack and 12 general browser checks. All passed
against production output. Source details, controls, measured CPU/GPU errors and actual applied-terrain screenshots:
[`TerrainGenerators.md`](../../Exhibits/LandscapeWorkspace/TerrainGenerators.md).


## Actual reference layer-stack UI — 2026-11-06

This supersedes the earlier source-colour-only skin. The active mask's stack now occupies the primary outliner using
upstream row/menu/inspector structure, extracted CSS cascade and the same Lucide glyphs. Choose **Mask document** to
switch independent stacks; its adjacent menu opens batch-mask selection. The outliner has the source's search, step
numbers, eye controls, inherited mask rows and selected-only **Up / Down / Duplicate / Delete** actions. Drag rows to
reorder; the painted foundation and global Environment keep locked positions. Edit operation names in the inspector.

Use the outliner **+** menu to add a real generator/operation to the active mask. Secondary batch/saved-stack controls
are collapsed beneath the property inspector. The source's final styling is preserved: square entries, coloured glyphs
without tiles and rounded surrounding controls, rather than the earlier custom nested cards.

`ReferenceStack.css` is generated from `ReferenceSnapshot/style.css` by
`node Exhibits/LandscapeWorkspace/ExtractReferenceStackStyle.mjs` from root after dependencies are installed.
`StackDock.css` contains layout integration, not a replacement row design. Six pinned snapshots include the actual
upstream stack UI, full stylesheet and page layout; original reference files remain unchanged.

Current verification: 46 local tests and 47 browser checks (11 new reference UI, 15 mask-stack, 9 terrain/navigation,
12 general). All 24 checked style values match a fixture rendered from the original source row template/full stylesheet.
[Current actual captures and comparison scope](../../Exhibits/LandscapeWorkspace/ReferenceLayerStack.md).

## Separate workspace sequences and radial terrain

Mask editor lists painted masks; Landscape lists only the active mask's operations. Terrain generators fade onto the
flat foundation through editable strength, radius, falloff and centre offsets. Explicit SDF structures remain separate.
See [current mechanism, tests and actual captures](../../Exhibits/LandscapeWorkspace/RadialTerrain.md).
