# Web Pattern geometry import (in progress)

**Overall Pattern-to-PBR progress: 95%.** This phase adds a fail-closed geometry converter; it does not complete the web Pattern JSON import or certify the native GPU preview.

## Convert a Pattern JSON export

```sh
node Automation/ConvertWebPattern.mjs input.pattern.json output.vector.toml
```

The output is the native `frontier.vector_artwork` TOML document consumed by the C++ Vector Authoring codec. The converter uses Texture Studio's own Pattern normalization and path geometry routines, so cubic paths, SVG path commands (including elliptical arcs), primitive silhouettes, rotations, flips, literal colours, fill rules, stroke/fill colours, textile roles, surface-channel contribution toggles/strengths, and normalized relief reach the native vector representation. Stitch and weave identity is retained separately from the vector paint colour.

Run its independent tests with:

```sh
node --test Automation/ConvertWebPattern.test.mjs
```

## Deliberate limits

This is a first geometry-import increment, not a claim of full Pattern fidelity. It rejects rather than silently discarding unsupported content: embedded images/SVG/text, artwork masks, gradient or linked paints, folders/groups/linked instances, non-straight or non-XY tiling, non-UV mapping, document-level rotation, tile-edge wrapping, non-round caps/joins, and patterns beyond native instruction/segment/layer limits. Per-finish roughness/metalness values, full colour-source semantics, full Pattern construction and background compositing still need native representation and round-trip tests. Stamp import remains deferred.

Persistent or scene-level Apply-to-material remains disabled. The Windows build and CPU/native UI checks passing is not evidence of the integrated GPU submission/capture path; capture on a real Windows GPU and physical-height calibration remain required gates.
