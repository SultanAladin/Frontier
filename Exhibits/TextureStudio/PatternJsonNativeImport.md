# Web Pattern geometry import (in progress)

**Overall Pattern-to-PBR progress: 95%.** This phase adds a fail-closed geometry converter; it does not complete the web Pattern JSON import or certify the native GPU preview.

## Convert a Pattern JSON export

```sh
node Automation/ConvertWebPattern.mjs input.pattern.json output.vector.toml
```

The output is the native `frontier.vector_artwork` TOML document consumed by the C++ Vector Authoring codec. The converter uses Texture Studio's own Pattern normalization and path geometry routines, so cubic paths, SVG path commands (including elliptical arcs), outlined text, primitive silhouettes, rotations, flips, literal sRGB colours, fill rules, stroke/fill colours, selected rectangular/elliptical masks, textile roles, per-channel contribution states, and relief reach the native vector representation. It maps Pattern roughness/metalness targets and occlusion into the native scalar contributions, and opacity into shape coverage. Stitch and weave identity is retained separately from the vector paint colour. Text is imported as its stored outline; the editable text/font metadata is not retained. Derived normals require both Height and Normal contributions, and Normal strength scales the height contribution used for the normal map without changing the Height map.

Run the JavaScript converter tests with:

```sh
node --test Automation/ConvertWebPattern.test.mjs
```

CMake additionally generates `Automation/Fixtures/PatternNativeImport.json`, decodes it with the native C++ TOML codec, and compiles all seven Float32 surface-channel programs as dependent CTest checks when Node is available.

## Deliberate limits

This is a first geometry-import increment, not a claim of full Pattern fidelity. It rejects rather than silently discarding unsupported content: embedded images/SVG, masks other than un-stroked rectangle/ellipse masks on rectangle/ellipse layers, painted/image masks, gradient or linked paints, folders/groups/linked instances, non-straight or non-XY tiling, non-UV mapping, document-level rotation, tile-edge wrapping, non-round caps/joins, and patterns beyond native instruction/segment/layer limits. Full colour-source semantics, editable text metadata, Pattern folder/instance construction and background compositing still need native representation and round-trip tests. Material roughness/metalness target values are represented, but broader finish semantics remain unverified. Stamp import remains deferred.

Persistent or scene-level Apply-to-material remains disabled. The Windows build and CPU/native UI checks passing is not evidence of the integrated GPU submission/capture path; capture on a real Windows GPU and physical-height calibration remain required gates.
