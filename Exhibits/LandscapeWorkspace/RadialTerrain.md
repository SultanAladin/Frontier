# Separate workspace stacks and radial terrain

## Result — 2026-11-06

Mask editor and Landscape now expose different primary sequences. Terrain generators introduce continuous relief
through a compact radial gradient on the flat painted sea-level foundation; they no longer union scattered upright solids.

| Workspace   | Primary left stack               | Inspector                                      |
|-------------|----------------------------------|------------------------------------------------|
| Mask editor | Painted mask documents           | Brush, region bounds, edge and volume settings |
| Landscape   | Active mask's landform operations| Generator, CSG and radial influence controls    |

- Mask selection persists across tabs; each mask retains its independent operations and operation selection.
- The mask-stack plus button creates a mask. Landscape's source-style plus menu adds a landform to that mask.
- Optional batch mask selection remains available in Landscape's mask selector. It is not the primary outliner.
- The pinned source rows, operation menu, drag ordering and inspector styling remain in use.

## Actual application captures

[Landscape: two overlapping dune generators fading onto a flat foundation](SeaLevelDuneInfluence.png)

[Mask editor: a different mask-document stack and brush inspector](SeaLevelMaskStack.png)

These are browser captures, not generated imagery. The dune capture uses a 720-pixel software-rendered viewport.

## Radial controls

| Property           | Range             | Meaning                                                    |
|--------------------|-------------------|------------------------------------------------------------|
| Influence          | 0–1               | Strength; zero bypasses this terrain generator              |
| InfluenceRadius    | 0.05–2            | Multiple of the shorter boundary half-span                  |
| InfluenceFalloff   | 0.05–1            | Fraction of radius used by the smooth transition            |
| InfluenceX/Z       | −1–1              | Centre offset relative to each boundary's half-span         |

Defaults are strength 1, radius 0.9, falloff 1 and centred influence. Each operation stores its own controls.
The preview depicts the gradient; the signed field independently evaluates the same compact radial support.
Imported operations without these properties use the defaults without rewriting or discarding their JSON.
Consequently, older terrain generators intentionally render differently: scattered-solid composition is corrected.

## Signed-volume mechanism

- Terrain operations start with the analytic plane `Y` (mean sea level is exactly zero), not a collection of isolated primitives.
- Evaluate seeded procedural noise at all three world coordinates. No sampled height arrays or height textures.
- Compute radial weight `strength × smoothstep(0, 1, (radius − distance) / feather)`.
- Apply bounded, weighted world-unit field changes. Outside the radius, weight and its first derivative are zero.
- Add/union raises relief; subtract excavates through the implicit profile rather than cutting with a floating solid.
  Intersect and blend retain their ordered field meanings. CSG smoothing stays in world units before normalization.
- Limit vertical noise frequency so `amplitude × verticalFrequency × profileGradientBound ≤ 0.45`.
  Terrain-only stacks cannot fold into disconnected slabs; their profiles still depend on Y.
- Normalize the resulting field by a conservative gradient bound. A separately bounded radial envelope accelerates
  marching without changing its zero contour. Finally clip to the painted mask, region edge and foundation floor.
- Explicit arches, caves, hoodoos and ledges keep their true 3D primitive/CSG behavior. They do not receive these terrain
  controls. Existing legacy boundary refinements, sparse export and baking use the same CPU field.

`LandformStack.js` compiles support, gradients and bounds. `TerrainInfluence.js` defines matching CPU/GLSL profiles.
The GPU primitive texture has five RGBA rows; the fifth carries bounded vertical frequency and primitive gradient.
The region texture has two rows, including stack normalization and envelope parameters. Baking imports the same field
code, not a separately approximated terrain formula.

## Verification

| Check                                      | Result                                      |
|--------------------------------------------|---------------------------------------------|
| Local field / stack / volume tests          | 56 passed, including 10 radial regressions   |
| Focused separate-stack / radial browser suite| 9 passed; no page errors                   |
| Radial CPU/GPU field comparison             | 540 points; max absolute error 0.000057776  |
| Earlier mask ownership / CSG / bake suite | 15 passed before the sea-level correction   |
| Earlier source row-style comparison       | 11 checks passed; 24 style properties match |
| Earlier general / full-resolution bake    | 12 passed before the sea-level correction   |
| Production build                           | Passed                                     |

Regression coverage includes flat terrain outside support for every terrain generator and combine mode, C1 radial
contours, connected overlapping dunes, bounded gradients with mixed SDF structures and narrow falloffs, vertical
non-folding at maximum settings, cache invalidation, malformed property rejection, save/load, undo, independent mask
selection and zero-strength bypass. CPU/GPU comparisons include negative coordinates, all twelve terrain profiles,
four ordered combine modes, overlapping influences and an explicit arch in a terrain stack.

Scope: Chromium 153 / WebGL2 / ANGLE SwiftShader on Linux. No native Windows, hardware GPU performance, physical
geological simulation, or WebGPU implementation claim is made by these checks.

## Reproduce

```bash
cd Experimental/LandscapeWorkspace
npm test
npm run build
npm exec vite preview -- --host 0.0.0.0 --port 5174
```

From the repository root, with the local Chromium validation tooling installed:

```bash
LD_LIBRARY_PATH=/tmp/lib LANDSCAPE_URL=http://localhost:5174 \
  node Exhibits/LandscapeWorkspace/RadialStackBrowserValidation.mjs
```

Reports: `RadialStackBrowserResults.json`, `LandformFieldResults.tap`, `ReferenceStackBrowserResults.json`,
`MaskStackBrowserResults.json` and `BrowserResults.json`. The source pin and retained reference files are unchanged.

## Sea-level correction

The previous foundation had its top at `Depth × 0.2`, creating a visible above-water slab. It now spans `−Depth` to
`0`; every generator is anchored at zero. Depth changes only the underwater floor. This applies to existing saved
projects automatically without deleting strokes, masks or operations. Explicit structure recipes remain structures.

The renderer now uses a 0.02 field tolerance and up to 1024 noise-marching iterations, reducing false surface elevation
at the ocean intersection. The depth slider is labelled **Underwater depth**. Export bounds retain empty padding above
sea level for reconstruction; this padding does not create solid geometry.

[Actual flat foundation at sea level, both terrain influences bypassed](SeaLevelFlatFoundation.png).
Ocean waves cross mean sea level and can naturally wash over the flat areas; the flat-only capture sets waves to zero
so the absence of a raised pedestal is directly visible. The dune capture retains the procedural waves.

The current 56 local and 9 focused browser checks include zero surface at Y=0, positive field above it, negative below,
correct bottom at −Depth, and invariant hill height when changing depth from 30 to 300 metres. The 540 CPU/GPU points
were rerun after this correction. Older broad-suite reports/captures are historical, not sea-level reruns.
