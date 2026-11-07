# Spillway headless verification

Generated: 2026-11-07T22:14:26.744Z
Node: v22.22.3
three.js: r170

## Measurements

| Check | Result |
|---|---|
| materials | concrete, paint, steel, water, beam |
| course | 68 chunks over 6082 m |
| kinds | deck×5 kicker×6 bowl×5 turn×19 chute×4 basin×4 bridge×7 tunnel×7 cascade×4 pump×5 lift×2 |
| mesh | 187 meshes, 78321 vertices, 77856 triangles over 40 chunks |
| ride(drift) | 8983 m, 107651 pts, 226 km/h, 6 wall hits, 28 slingshots, 54 airs, best carve 4.22 s |
| ride(steer) | 8255 m, 41046 pts, 4 wall hits |
| ride(idle) | 8093 m, 32 wall hits, 20 bails |

## Ride comparison (180 s each, seed 20261107)

| Run | Distance | Score | Top speed | Wall hits | Slingshots | Airs | Bails | Best carve |
|---|---|---|---|---|---|---|---|---|
| drift | 8983 m | 107651 | 226 km/h | 6 | 28 | 54 | 13 | 4.22 s |
| steer | 8255 m | 41046 | 216 km/h | 4 | 0 | 49 | 10 | 0.00 s |
| idle | 8093 m | 27828 | 212 km/h | 32 | 0 | 54 | 20 | 0.00 s |

## Assertions

All assertions passed.

## Not covered here

WebGL rasterisation, shader compilation, browser input and audio scheduling are exercised only in a real browser.
