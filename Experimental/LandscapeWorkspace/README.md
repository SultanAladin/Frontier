# Landscape workspace

Mask-first volumetric landscape **browser prototype** with independent XYZ regions, Clay/Carve sculpting,
always-high field-refined geometry and sparse 3D export. This does not replace or validate the native editor.

**Known blocker:** the broad editor regression still hits the four-second GPU fence stop while hiding all masks.
Focused sculpt checks pass in development and built mode; overall GPU stability is not established.
See [actual captures, passing checks and retained failures](../../Exhibits/LandscapeWorkspace/README.md).

## Run

Requires Node.js **22.12+**, npm, WebGL2, OffscreenCanvas and dedicated browser threads.
Landscape is GPU-only. Missing or failed graphics stops the preview; it never starts a CPU pixel renderer.

```bash
cd Experimental/LandscapeWorkspace
npm ci
npm run dev -- --port 5173
```

| Command                       | Purpose                                                        |
| ----------------------------- | -------------------------------------------------------------- |
| `npm test`                    | 183 field, sculpt, refinement, lattice, ocean and codec checks |
| `npm run build`               | Production application; generated `dist/` remains ignored      |
| `npm run test:sculpt`          | Actual Clay/Carve, underside, undo, save, export and high mode  |
| `npm run test:preview`         | GPU pixels, XYZ arch, loss and explicit restart                |
| `npm run test:sampling`        | Completed high/native sampling, diagnostics and local zoom     |
| `npm run test:working`         | Per-region XYZ pitch, working navigation, editing and export   |
| `npm run test:gpu-quality`     | High geometry and distance-stable linked GPU wave inputs       |
| `npm run test:gpu-failure`     | Missing GL/APIs, silent protocol and no-animation path          |
| `npm run test:browser`         | Broad editor regression; currently blocked by the GPU stop     |
| `npm run test:production`      | Built UI/fonts/GPU and bundled metric export                   |
| `npm run test:metric`          | Independent distance/gradient fixtures and grid refinement     |
| `npm run test:metric-browser`  | Metric export, immutable snapshot, failure and cancellation    |
| `npm run test:volume`          | Full 1024 × 128 × 1024 metric export and 3D occupancy          |
| `npm run preview`             | Serve the production build, default port 4173                  |

Browser checks require a running server and Playwright Chromium. `PLAYWRIGHT_EXECUTABLE_PATH` selects Chromium;
`LANDSCAPE_URL` overrides the development address. Built GPU checks use port 4173 and a distinct report prefix.

## Authoring workflow

- **Mask editor** opens initially, with no hidden 3D graphics workload. Create/select a mask, paint world-space
  vector strokes, box-select with **R**, then **Add region**. Painting alone creates no solid.
- Regions limit allocation; painted outlines define coastlines. Pan/zoom never resize the authored vector geometry.
  **New world** uses a non-blocking HTML confirmation with Cancel and Escape.
- The initial coastal study has already-applied ridged multifractal, sand-dune and fractal relief. Its foundation
  remains at sea level, with thickness below Y=0 rather than an elevated platform.
- Select a mask to edit its independent stack. **Add generator** / **Shift+A** inserts into that stack, not a preset
  replacement. Scope can target the mask or one boundary; boundary operations inherit the mask-wide stack.
- Drag to reorder masks or generators. Copies own independent arrays; naming, duplication, deletion, mute and undo
  preserve ownership. Ctrl-click masks and use **Add to selected masks** for explicit batch insertion.
- Shape controls edit relief, frequency, roughness, seed, CSG and radial influence. Geometry changes on release.
  Coast feather fades generated relief inward; zero deliberately preserves a sharp edge.
- **Bounds & volume** edits independent region **X/Y/Z counts**. X/Z linking is optional. Legacy documents without
  overrides inherit mask defaults. Export uses the selected region, progress and cancellation.
- **Save project / Open** retain vector geometry, generators, region counts and XYZ sculpt strokes. Browser storage
  autosaves changes; undo/redo applies to document edits. Invalid imports do not replace the world.

## 3D sculpting

Select a visible allocated region, open Landscape and choose the brush button or **B**.
The inspector offers **Clay · add** and **Carve · remove**, world radius and strength.

1. Start sculpting; the editor focuses a native-pitch working area automatically.
2. Left-drag over a real surface. Release to apply **one atomic undoable 3D stroke**.
3. Alt-drag orbits; Shift/MMB pans. Ctrl/Cmd+Z undoes the entire released stroke.
4. For an underside, finish sculpting, orbit below it, then start/refocus there. Negative camera pitch is supported.
5. Save the project or bake the region; both retain the same sculpted field and ownership.

These are XYZ spherical/capsule signed operations, not height painting, normal-only detail or triangle displacement.
Canonical picking, field normals, GPU geometry and sparse export use the same region-owned `SculptStrokes`.
Other regions/layers are protected. Cancelled pointers, navigation or changed selections do not finalize edits.

Limits: 256 strokes per region, 2,048 XYZ points per document and 256 points per gesture.
Edits appear on release, not continuously during dragging. Blending currently uses a fixed radius-relative width.
**Smooth, Flatten and adjustable falloff are not implemented.** This is not a fully paged baked-volume editor.

| Input                            | Behaviour                                      |
| -------------------------------- | ---------------------------------------------- |
| `1` / `2`                        | Mask editor / Landscape                        |
| `B` in Landscape                 | Toggle 3D sculpting                            |
| `B` / `E` / `R` / `H` in Mask   | Brush / erase / region / pan                   |
| Space/MMB/right-drag in Mask     | Pan the vector canvas                          |
| Scroll in Mask                  | Pointer-centred zoom                           |
| Double-click landscape surface  | Focus authored-pitch working detail            |
| Drag in Landscape               | Orbit; Alt-drag while sculpting                |
| MMB / Shift-drag in Landscape   | Pan camera target                              |
| Scroll in Landscape             | Dolly, minimum 0.5 m orbit                     |
| Alt+Up / Alt+Down on mask row   | Keyboard mask reordering                       |
| `F` / `Shift+F`                  | Focus active mask / fit world                  |
| Enabled WASD + `W/A/S/D/Q/E`    | Optional translation; Shift increases speed    |
| Ctrl/Cmd+Z / Ctrl/Cmd+Shift+Z   | Undo / redo                                    |

## High detail is the default

**Field, authored lattice, resident geometry and image pixels are different quantities.**
The old 16→48→64 whole-view surface did not satisfy high-resolution editing. It is no longer the default path.

| Quantity              | Current meaning                                                        |
| --------------------- | ---------------------------------------------------------------------- |
| Region counts         | X × Y × Z; **512 × 96 × 512** means 512 horizontal, 96 vertical       |
| Whole-scene geometry  | Shared-edge adaptive refinement and actual XYZ field projection          |
| High diagnostic code  | Method `96`, not a uniform 96³ grid or the whole authored lattice       |
| Native working area   | Up to **64³ cells at the region's authored pitch**                     |
| Working budgets       | Coverage can shrink to 48³/32³; pitch is not silently coarsened          |
| Image pixels          | Fixed 1440-wide ceiling; adaptive density under the 1.5-million-pixel cap |

High geometry is prepared directly; the internal coarse contour is not displayed first. The former quality selector
is hidden and cannot lower the normal UI setting. Image density still adapts to viewport and measured cost, but it
does not lower geometry detail. The coastal example now has **119,922 triangles / 801,701 samples**, compared with
96,798 triangles / 308,700 samples in the preceding actual build. The authored document and camera are unchanged.

Starting sculpting opens native detail. Zooming in requests it automatically below
`min(PitchX, PitchZ) × 64 × .85` orbit. Double-click or **View authored detail** provides explicit focus.
The viewport and Bounds & volume show last **GPU-fence-completed** geometry, pitch, coverage and pending/budget states.
Preview diagnostics include actual image dimensions, driver, document/displayed revisions, epoch and `sculpt-high-2`.

Native pitch applies to the working interior, not the full landscape or four-cell transition rim. Coarser context
remains outside. For the coastal 512×96×512 example, pitch is **1.357×3.427×1.143 m** and 64³ coverage is
**86.875×219.333×73.125 m**. A dense target Float32 array would already consume 96 MiB; it is not allocated.
Derived vertical bounds can change Y pitch. Automatic focus may clamp to a region edge when aimed at empty space;
refocus an actual surface in that case. Complete paging and exact subvoxel/topology guarantees remain open.

## Geometry, water and GPU policy

```text
Vector masks ∩ allocated rectangles
    → sea-level foundation + independent XYZ operation stacks + region sculpt strokes
    → bounded adaptive high surface or native-pitch working area
    → ordinary GPU triangles + reverse-Z float depth → displayed image
    └→ independent canonical sampling → sparse 3D metric/conservative export
```

| Resource / behaviour        | Policy                                                        |
| --------------------------- | ------------------------------------------------------------- |
| Terrain                     | 120,000 triangles, 1.1 million samples, 256 allocated regions  |
| Per terrain draw            | At most 4,095 vertices, local position origins                |
| Geometry preparation        | Off-thread, cancellable; cooperative 6 ms target, not a bound |
| Image                       | 1.5 million pixels, at most 1440 wide and 2048 high           |
| In-flight work              | One image/fence, one queued view, at most 30 Hz submissions   |
| Fence / current-image limit | 4 / 10 visible seconds; then stop, no CPU fallback            |
| Inspection reply            | 4 seconds; protocol silence stops the session                 |

- Genuine XYZ geometry includes caves, overhangs and solid–air–solid columns at the same X/Z. It is not a heightmap.
  Approximate extraction can still miss sub-pitch features; adaptive projection is not universal topology recovery.
- Camera/ocean images reuse terrain buffers. There is no terrain-generator evaluation per screen pixel.
- Water keeps 24 world-space wave components and a fixed 8 km grid. Dolly never retunes physical displacement,
  wavelength, amplitude or phase. Footprint filtering affects reflection detail, not geometry.
- Mask/background suspension pauses work and visible deadlines. Ordinary workspace switches reuse one context.
- Missing APIs, loss, timeout or silence stop and terminate graphics. Mask/save survive. Only explicit
  **Restart GPU preview** creates another session; no automatic context loop or software pixel renderer exists.
  Terminating a thread does not guarantee cancellation of already-submitted GPU work.
- The broad editor regression still stopped at the four-second fence. Focused passing checks do not erase that
  failure or certify physical Windows performance. Retained evidence is linked above.

## Sparse export

Full exports are `Landscape.sdf.gz`. Choose the distance field under **Bounds & volume → Sample & export volume**:

| Mode                 | Setting                | Format                 | Meaning                            |
| -------------------- | ---------------------- | ---------------------- | ---------------------------------- |
| Metric TSDF          | `DistanceMode: metric` | `FrontierSparseTSDF/2` | Approximate metric signed distance |
| Conservative, legacy | `bound` or omitted     | `FrontierSparseTSDF/1` | Conservative signed CSG bound      |

New masks default to metric; existing documents do not silently change mode. Distance-mode edits remain export-only;
region-resolution edits also invalidate working geometry. The completed-export card identifies the captured
mask/region, dimensions, cycle count
and sign changes, even if live authoring continues while that snapshot is baking.

| Property     | Meaning                                                                   |
| ------------ | ------------------------------------------------------------------------- |
| Dimensions   | Selected region X × Y × Z; optional X/Z linking in the inspector          |
| Cell centres | `Origin + (Cell + 0.5) * Pitch`                                           |
| Samples      | Float32, clamped to ±Band; negative solid, positive air                   |
| Band         | Twice the largest world-space voxel pitch                                 |
| Allocation   | Integer octree; constant leaves and sampled 8³ bricks                     |
| Archive cap  | 256 MiB uncompressed; failures never silently reduce requested resolution |
| Metric cap   | Estimated 384 MiB working storage; not a measured process-RSS limit       |

The metric pipeline is genuinely three-dimensional:

```text
Conservative sparse samples
    → original cell signs + axis sign-crossing brackets
    → 20 analytic bisections per crossing + local interface normals
    → anisotropic Godunov Eikonal stencil, eight directional sweeps per cycle
    → convergence and sign checks → collapse saturated bricks
    → V2 specification + binary leaves → streamed gzip
```

- Horizontal and vertical voxel pitches enter the stencil independently. There is no height-only distance transform.
- The original analytic residual refines interface placement; it is not substituted for metric propagation.
- Default maximum: six cycles. Stopping tolerance: `min(Pitch) × 1e−4` metres of maximum Float32 **sweep update**.
  This is not a guarantee of that error against the analytic surface. Failure to converge produces no metric archive.
- First-order distance and cell-centre sign preservation do not guarantee exact subcell surfaces or topology.
  Sharp ridges, thin features and cropped-domain fronts retain grid errors; independent measurements are in Exhibits.
- Conservative mode retains streaming sampling/encoding. Metric mode holds sparse working bricks until reinitialization
  succeeds; its archive header is not written before convergence.
- Cancel terminates the sampling thread, discards chunks and ignores expired completion/error callbacks.

Decompressed files start with one UTF-8 JSON specification line. Binary leaves follow, little-endian:
`u32 category, u32 X, u32 Y, u32 Z, u32 width`, then one Float32 constant (category 1) or `width³` Float32 samples
(category 2), with X fastest, then Y, then Z. All in-bounds cells are covered, including constant interior/exterior.
V2 adds method, convergence, interface and sign-preservation metadata; it does not change the binary leaf layout.

`SparseVolume.js` supplies structured sampling, encoding, V1/V2 decoding, coverage validation and cell queries.
`DistanceReinitialization.js` consumes a structured V1 volume **in place**: raw buffers are released after seeding.
Discard that volume after failure; do not retry a partially consumed object. Its optional `Surface` callback must have
exactly the source zero set; without it, the solver reinitializes linearly reconstructed sampled crossings instead.
The worker uses the original analytic residual and one-cell analytic exterior seeding.

## Sources and remaining work

- Immutable [reference snapshots](ReferenceSnapshot/README.md) and [CLAUDE.md](../../CLAUDE.md) retain source pins.
- The UI adapts the pinned ProjectZero graphite tokens, DM Sans typography and inspector structure; it is not a
  pixel-identical native port or the erosion reference's coloured stack.
- Mask capacities remain 512 capsule samples, 16 masks, 16 regions/mask and eight combined operations per boundary.
  Complex exports may exceed explicit budgets; the full-size export uses a geometric CSG fixture, not every noise stack.
- GPU stability, physical Windows verification, Smooth/Flatten, complete volume paging, baked-volume loading,
  erosion/wind/weathering, Rock & Cliff Studio, WebGPU and native integration remain unfinished.
  Metric export and ownership alone do not implement simulation. No physical simulation solver was added.
