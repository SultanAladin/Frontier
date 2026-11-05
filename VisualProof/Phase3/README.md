# Phase 3 proof images

Pictures of what the Phase 3 ports actually output, rendered on the host — no GPU, no device, and no
re-implementation of any physics. Every spark streak in these frames is a row of
`SparkSequence::Instance()`; every dot over the car is a row of `SpreadEffectCells()`. The renderer
around them (a perspective camera, a z-buffer, flat Lambert shading, tone mapping and a PNG writer) is
about three hundred lines in `Validation/Phase3ProofImages.cpp` and exists only to put those numbers
on screen.

## Regenerating

```bash
g++ -std=c++20 -O2 -Wall -Wextra -I. -IFrontier Validation/Phase3ProofImages.cpp \
    Frontier/Engine/Rendering/SparkCollision.cpp Frontier/Engine/Rendering/SparkSequence.cpp \
    Frontier/Engine/Rendering/EffectSequence.cpp \
    Frontier/Engine/Diagnostics/LogCategories.cpp Frontier/Engine/Diagnostics/LogStream.cpp \
    Frontier/Engine/Diagnostics/LogRingStore.cpp -o /tmp/phase3proof

/tmp/phase3proof VisualProof/Phase3        # writes four PNGs
python3 Validation/CompressProofImages.py  # recompresses them in place, losslessly
```

The renderer writes PNGs whose IDAT is a run of zlib *stored* (uncompressed) blocks, which keeps it
free of any dependency — but a 1280×800 frame then costs 2.9 MB. `CompressProofImages.py` re-filters
each scanline and re-emits the IDAT with zlib at level 9: 11.7 MB becomes 162 kB, and the pixels are
unchanged (verified by decoding both forms and comparing).

## The images

### `3d-sparks-rim.png`

A flat tyre has let the rim touch. One `SparkScrape` — contact at 16 m/s, the wheel's own surface speed
behind it, `Rim = true` — is emitted for eight frames and integrated against the flat course. Each
frame's instance table is added into the framebuffer, so what you see is the trajectory the port
integrates rather than one arbitrary instant.

What to look for: the shower leaves from the **trailing** edge of the wheel and streams backwards,
because a rim spark is born an angle α behind the contact patch and inherits that point's own velocity
(`Slide + S(1 − cos α)·F + |S|·sin α·N`) — not the contact's. The streaks skip off the road and keep
going, each skip costing 55 % of the tangential speed and 28 % of the approach. The colour runs from
white at the wheel through yellow to orange as the spark cools radiatively; the dimmest have fallen
below 950 K and are gone.

The tail of each streak is drawn where `SparkRaster.slang` draws it — lerped toward the head by the
spark's shutter — so a spark born late in a frame leaves a shorter streak than one that flew the whole
frame, exactly as it does on the device.

### `3d-sparks-hull.png`

The same system, hull scrape instead of rim: the car's flank is laid against a barrier at 24 m/s. Here
the sparks leave the scraping point itself, fanned ±35° around the slide and kicked 1–4 m/s off the
surface, so the plume is a broad sheet rather than a rope. Note that none of them end up inside the
barrier — the sweep is continuous, resolved in 2 cm intervals and bisected, with a 3 mm skin.

### `3d-sparks-closeup.png`

Fourteen frames of a light scrape, close enough to read individual streaks: their length is the
distance travelled in one frame, their colour is the blackbody radiance at the spark's temperature, and
their width is bounded in pixels rather than in metres — the same reason `SparkRaster` clamps its width
to 0.65–1.35 px.

### `3d-effect-anchors.png`

The derez's anchor table, drawn over the car: 1 536 points spread over the body's facets and 96 over
each wheel, **by area**, each carrying the family of the facet it came from — the colour of the cube
that will take its place when the cell lets go. The body reads blue-white (family 6, body coat), the
greenhouse tan (70, cabin leather), the treads grey (9, tyre) and the rim faces bright (81, rim base).

The spread is deterministic: the same facets give the same anchors on every run, so the host can build
the table once at bring-up and the GPU can rely on it. It is `SpreadEffectCells()` output, plotted.

## What is *not* in these pictures

The derez and rez **cube flight** — the release wave, the linear-drag ballistics, the impact scan and
the eight children — is closed-form in the clock and lives in `EffectRecords.slang` /
`EffectRaster.slang`, on the device. Rendering it here would mean re-implementing that maths on the
host, and a second implementation proves nothing about the first. Those shaders are verified the way
every other Phase 3 shader is: compiled to SPIR-V by the external Slang toolchain.

## Verification

| Check | Result |
|---|---|
| `Phase3Validation` | 2558 checks, 0 failures |
| `SessionValidation` | 62 / 0 |
| `npm test` (prototype) | 106 pass, 1 fail, 9 skipped — the one failure is an IES line-ending artifact of a fresh clone, not a code fault |
| Headless harness, 30 frames | 0 errors, 0 warnings |
| Slang → SPIR-V | 15 shaders; the 3D pair (`SparkRaster`, `EffectRaster`) compile clean with 2 entry points each |
