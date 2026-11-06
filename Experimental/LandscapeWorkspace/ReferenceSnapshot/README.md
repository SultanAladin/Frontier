# Pinned landscape and layer-stack sources

Source requested by the user: [c7egoist/Frontier](https://github.com/c7egoist/Frontier),
branch `arena/68c84c0d-frontier`, immutable commit `09b7064f282f4df5c5352f2bc97c82fb1c06a098`.
`SourcePin.json` retains the original paths, sizes and verified Git blob identities of six unmodified files.

| Original file                         | Purpose in the local prototype                       |
|---------------------------------------|------------------------------------------------------|
| `src/erosion/terrain-stack-presets.js`  | Catalogue/defaults/ordering reference                 |
| `src/erosion/pipeline-shaders.js`       | Height-texture reference; not a runtime shader import |
| `src/geology/polyhedron.js`            | Conservative volumetric-geometry adaptation reference|
| `src/erosion/layer-stack-ui.js`         | Actual layer-stack row/menu/inspector UI structure    |
| `src/app/style.css`                    | Actual stack CSS and final cascade extraction        |
| `index.html`                          | Actual dock/outliner layout                          |

The source does not expose a repository licence in the GitHub metadata inspected during this work. These snapshots
are retained at the user's explicit request; no additional redistribution licence is asserted. Upstream files remain
unchanged. Lucide is used through its own pinned package and licence, not re-vendored.

The terrain reference shaders are not imported into the runtime: local geometry remains signed volumetric CSG and 3D
lattice noise, with no heightmap replacement. No full geological-generator, erosion or simulation parity is claimed.

For the user's UI correction, `ReferenceStack.css` mechanically extracts the actual upstream stack/inspector CSS,
preserving its cascade. `StackGlyphs.js` uses the same Lucide glyphs and operation colours. Local bindings connect that
presentation to each mask's SDF operation stack, not the upstream height-channel pipeline.
The original row template and complete stylesheet are also used by a retained component comparison.
See [`ReferenceLayerStack.md`](../../../Exhibits/LandscapeWorkspace/ReferenceLayerStack.md) for actual UI captures,
matched styles, interactions, precise scope and reproduction.
