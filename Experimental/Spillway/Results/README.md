# Spillway — retained evidence

| File | Produced by | What it shows |
|---|---|---|
| `Verification.md` | `node tools/validate.mjs` | Course, meshing and ride measurements with pass/fail assertions |
| `CourseMap.svg` | `node tools/validate.mjs` | First 2 400 m of the generated course, coloured by section |
| `Preview*.png` | `node tools/preview.mjs` | Software rasterisations of the real chunk geometry from the chase camera |

## Reproduction

```text
cd Experimental/Spillway
node tools/validate.mjs
node tools/preview.mjs
```

Environment for the retained run: Linux x64 sandbox, Node v22.22.3, three.js r170.
Seed `20261107`.

## What the previews are

`tools/preview.mjs` builds the same chunks the game builds, then rasterises them in software:
z-buffered triangles, flat sun shading, exponential haze, no textures or shadows. They exist to
check geometry, scale and camera framing without a GPU. They are **not** browser screenshots and
do not demonstrate the final shaded appearance.

`PreviewTurn.png` — a banked 90° elbow seen from the chase camera, with the inside line flat and
the outside climbing under the rust rail.

`PreviewTunnel.png` — ribbed sluice, slit roof, red/cream edge marking, exit ahead.

`PreviewBridge.png` — overpass slab and piers with the banner out of the racing line.

`PreviewBasin.png` — sheet water riding the crown of the settling basin.

`PreviewKicker.png` — kicker lip with rumble chequers and the crest band, rollers beyond.

`PreviewBowl.png` — parabolic bowl with banks, rollers and a wall board.

## Verified outside this folder

- Full boot, menu demo, run, 900 frames, streaming, disposal, pause, resume: exercised headlessly
  against a stub WebGL context with the real `index.html` and jsdom DOM. No exceptions.
- HUD: driven through a real DOM and checked field by field.
- ESLint `no-undef`/`no-dupe-keys`/`no-unreachable` across every module: clean.

## Not verified

GPU rasterisation, shader compilation, real audio output, real pointer and keyboard input, and
frame rate on the user's machine. Those require a browser, which the build sandbox does not
provide.
