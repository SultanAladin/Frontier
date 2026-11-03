# World Forge — verification report

Validation code, generated images, graphs and numeric results for the
[`Experimental/WorldForge`](../../Experimental/WorldForge/README.md) prototype.

## Environment

| Item | Value |
|---|---|
| Host | Linux sandbox, UTC 2026-11-03 |
| Runtime | Node.js v22.22.3, npm 10.9.8 |
| Tested revision | `arena/23caea4c-frontier` (branched from `e1ce562`) |
| GPU/browser | none available — WebGL2 capture **blocked** in this sandbox |
| Dependencies | none for the prototype and the validation suite |
| Additional tool | `@shaderfrog/glsl-parser` 2.x installed transiently in `/tmp/glslcheck` for a full GLSL parse |

## Reproduction

```bash
cd /home/user/Frontier
node Exhibits/WorldForge/Code/RunValidation.mjs          # or: npm test in Experimental/WorldForge

cd Experimental/WorldForge && node serve.mjs              # live preview on 0.0.0.0:5174
curl -I http://127.0.0.1:5174/                           # 200, text/html

# optional full GLSL parse (needs npm access to registry.npmjs.org)
cd /tmp/glslcheck && npm install @shaderfrog/glsl-parser
node --input-type=module -e "import {parser} from '@shaderfrog/glsl-parser'; import * as S from '/home/user/Frontier/Experimental/WorldForge/src/Shaders.js'; for (const [n,s] of Object.entries(S)) if (typeof s==='string' && s.startsWith('#version')) { parser.parse(s,{quiet:true}); console.log('OK', n); }"
```

`RunValidation.mjs` imports the prototype modules directly (`../../../Experimental/WorldForge/src/`);
there is no duplicated algorithm in the exhibit. It rewrites all evidence files on each run.

## Results — 22/22 passed

| Check | Result | Observation |
|---|---|---|
| 01 Deterministic lattice noise | 🟢 PASS | identical inputs reproduce −0.144480; seed change yields 0.131695 |
| 02 Coastline SDF and gulf carve | 🟢 PASS | 947 inland / 1357 ocean samples; 255 inland cells converted to water |
| 03 Blank vase is exactly flat | 🟢 PASS | all 16,384 cells at −260 m, deviation 0 |
| 04 Continental blockout with ocean | 🟢 PASS | 43.5% land, 56.5% ocean, 1040 km coastline, −3761…1845 m |
| 05 Heightmap sculpt / undo / redo | 🟢 PASS | +383.3 m raise, exact restore, exact reapply |
| 06 Ray-to-crust intersection | 🟢 PASS | 0.00 m error against raster elevation |
| 07 Dual-contouring genus-0 solid | 🟢 PASS | `V − E + F = 2`, 3632 triangles, 100% outward normals |
| 08 Authored crust SDF → OBJ | 🟢 PASS | 4332 triangles from 8192 voxels in 53 ms |
| 09 Rolling water clipmap | 🟢 PASS | 9362× fewer cells, Δx 6.25 m → 400 m, toroidal mapping verified |
| 10 Crust tessellation LOD | 🟢 PASS | 49,152 triangles across 6 tiers, 171× cell saving |
| 11 Water volume conservation | 🟢 PASS | 500 steps, relative drift 3.10 × 10⁻¹³ % |
| 12 Wave celerity ≈ √(g·h) | 🟢 PASS | 31.64 m/s measured vs 31.32 m/s theoretical (101%) |
| 13 Wet/dry shoreline | 🟢 PASS | 4128 land cells, 0 flooded, 0 with velocity |
| 14 Stability over sloping bed | 🟢 PASS | 260 steps, η −2.21…2.15 m, max \|v\| 0.57 m/s, no non-finite cells |
| 15 Unreal-style fly camera | 🟢 PASS | W 374 m, E 374 m, Shift+W 1496 m, pitch clamp, orthogonal basis |
| 16 PNG encoding | 🟢 PASS | 16-bit heightfield 524,908 B; RGB evidence PNG validated |
| 17 Project round trip | 🟢 PASS | 5104 B JSON, 6 SDF layers preserved |
| 18 GLSL structure | 🟢 PASS | 11 shaders, 0 duplicate uniforms, 0 unbalanced scopes, ASCII identifiers |
| 19 Varying interfaces | 🟢 PASS | 6 vertex/fragment pairs, 0 mismatches |
| 20 CPU→GPU uniform wiring | 🟢 PASS | 55 bound names all declared in GLSL |
| 21 Render-path smoke test | 🟢 PASS | 10 mode/view combinations, 95 draw calls, no CPU-side exceptions |
| 22 Editor DOM contract | 🟢 PASS | 62 controller identifiers resolve against 97 markup identifiers, 0 missing |

Full numeric measurements: [`Results/validation.json`](Results/validation.json).
Machine-readable table: [`Results/validation.md`](Results/validation.md).

## Evidence

| File | What it proves |
|---|---|
| `Images/crust-blockout.png` | Top-down raster of the real authored SDF heightfield: filled continental mass, west-coast gulf, inland sea, relief ridge, offshore island, abyssal gradient. Generated from `CrustHeightfield`, not a mock-up. |
| `Images/water-eta-field.png` | Free-surface elevation η after 90 shallow-water steps from the reference solver — a visible expanding radial wave ring. |
| `Images/wave-propagation.svg` | Crest transects at t = 12.68 s and 25.35 s with the measured celerity 31.64 m/s. |
| `Images/lod-savings.svg` | Per-tier Δx and extent; 9362× cell reduction against a uniform fine ocean. |
| `Results/validation.json` | All 22 measurements, including volume drift, Euler characteristic, adaptivity report and frame statistics. |

```text
Reference solver at t = 12.7 s              Reference solver at t = 25.4 s
        ╭──── ripple ring ────╮                     ╭──────── ripple ring ────────╮
   distance travelled ≈ 398 m            →      distance travelled ≈ 796 m
   c = Δx / Δt = 31.64 m/s                     √(g·h) = √(9.81 × 100) = 31.32 m/s
```

## What was NOT verified

| Check | Status | Reason |
|---|---|---|
| Real WebGL2 GPU frame capture | 🔴 blocked | No browser binary in sandbox; `npx playwright install chromium` fails with `ECONNRESET` to `release-assets.githubusercontent.com` / Playwright CDN (egress allowlist excludes it) |
| GLSL compilation by a driver | 🔴 blocked | Same reason; replaced by a full `@shaderfrog/glsl-parser` parse (11/11 shaders OK), duplicate/varying/uniform static audit (checks 18–20) and a mock-device render smoke test (check 21) |
| Production C++/Vulkan editor rendering | 🔴 not in scope | This is an `Experimental/` prototype; a browser capture would not prove native parity even if available |
| Windows binary | 🔴 not in scope | No binary deliverable was requested for this prototype |

The live preview (`node serve.mjs`, port 5174, bound `0.0.0.0`) is the GPU-rendered evidence available
to the user; the server responded `200` for `index.html`, CSS and all ES modules. No preview warnings
were reported. No Linux or Windows product binary is delivered.
