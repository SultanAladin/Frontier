# Sugarwyrm — a candy dragon in 3D

A rearing dragon sculpted entirely from confectionery, rendered in WebGL.

It is **not** a low-poly dinosaur: there is no box-and-sphere assembly anywhere.
The creature is generated from anatomy — a parametric spine integrated through
space, a real cross-section swept along it with a propagated frame, a lofted
skull over a hinged jaw, scalloped bat-style wing planforms, and sugar scatter
welded to the same surface the body uses, so every detail breathes with it.

## Run it

The scene is a static page with no build step and no network dependencies
(three.js is vendored under `vendor/three`).

```bash
cd Experimental/CandyDragon
python3 -m http.server 8099
# open http://localhost:8099/
```

Any static file server works. Opening `index.html` from `file://` will not work
because ES modules and the import map require HTTP.

## Controls

| Input | Action |
| --- | --- |
| drag | orbit |
| scroll / pinch | zoom |
| `space` or **Roar** | open the jaw, roar, camera push-in |
| **Wings** | gate the wing beat |
| **Auto-orbit** | idle turntable |
| **Sugar dust** | toggle the floating sugar motes |

## What it is made of

Every surface is candy, but each part is a different sweet:

| Part | Confection | Why |
| --- | --- | --- |
| Hide | raspberry fondant, sugar-crust bump, waxy sheen | the two-tone cream belly runs on vertex colours so the material stays a single glossy hide |
| Horns, brow | butterscotch candy cane, spiral fluted | swept back over the neck, dense spiral rings read as pulled sugar |
| Dorsal crest | mint rock candy shards | translucent, iridescent, faceted |
| Wings | grape taffy sheet, millimetres thick | convex shell with finger struts and candy-cane bones, so the sail has volume instead of reading as paper |
| Teeth, claws | clear hard candy | transmission with a high IOR |
| Eyes | glowing sherbet lamp | emissive with a slit pupil and icing lid |
| Skin detail | jimmies, sugar pearls, gummy drips | instanced, procedurally seeded, deformed with the body |
| Floor | poured icing | clearcoat 1, sharp reflection, loose candy scattered for scale |

## Architecture

```text
index.html          shell, styling, import map, loader, error surface
src/rng.js          deterministic hashes, keyframe curves (monotone), helpers
src/anatomy.js      spine, swept torso, skull, jaw, mouth, wings, limbs,
                    horns, teeth, crest, tail blade, surface samplers
src/materials.js    candy PBR materials + procedural canvas textures
src/sugar.js        instanced sugar scatter, crystal crest, candy drips
src/dragon.js       scene graph, limb/wing rig, animation, roar
src/main.js         renderer, studio lighting, bloom, controls, interaction
tools/              offline review renderers (not shipped)
```

Notable implementation points:

- **Monotone keyframes** (`smoothCurve`) rather than Catmull-Rom for skull and
  torso profiles. Catmull-Rom overshoots, which turned the muzzle into a
  tumour in early iterations.
- **Live deformation without rebuilding geometry.** `updateBodyGeometry` writes
  straight into the existing position buffer each frame (~7 ms at the shipped
  resolution), so breathing, S-sway and tail whip are real vertex motion.
- **Cached surface sampler.** Sprinkle placement needs thousands of surface
  points per frame; `makeSurfaceSampler` caches frames on a bucket grid once per
  frame and interpolates, which took the per-frame cost from ~22 ms to ~7 ms.
- **Instance colours, not vertex colours, for sprinkles.** Declaring
  `vertexColors` there would demand a colour attribute the capsules do not have
  and render them black.

## Verification performed

No GPU or browser was available in the build sandbox, so the geometry and
material were validated with two offline renderers written for this exercise:

- `tools/raster.js` — z-buffered raster with interpolated normals and a
  three-point rig, used for silhouette iteration.
- `tools/trace.js` — a path tracer with a triangle BVH, soft area lights, shadow
  rays, ambient occlusion and a compact PBR approximation of each three.js
  material. Used to judge the candy palette, translucency and candy-cane stripes.

`tools/trace-dragon.mjs` renders the **shipped** `src/dragon.js` scene graph, so
the review images reflect the real deliverable rather than a mock-up. Those two
renderers found and fixed, among other things: a spiral spine bug that collapsed
the whole creature, an inverted skull frame, degenerate wing patches, a
missing-normals crash in the geometry merger, blown-out exposure, and the
vertex-colour bug above.

A headless smoke test loads every module (`three` through `src/main.js`) with a
DOM stub and steps 30 animation frames to catch construction-time crashes.

Re-run the checks:

```bash
npm i pngjs --no-audit --no-fund     # local review tooling only
node tools/trace-dragon.mjs hero side face
# images land in tools/out/
```

## Known limitations

- Verified offline, not in a real browser session. WebGL-specific behaviour
  (transmission sorting, bloom tuning, mobile thermal throttling) is untested.
- Transmission-heavy materials cost real GPU time; there is a pixel-ratio
  fallback, but no lower-quality material tier.
- The creature is a static hero pose with idle animation — it does not fly or
  walk.
- `tools/` requires `pngjs`; it is not part of the runtime.
