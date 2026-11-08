# Pinned landscape and editor sources

Immutable source snapshots are retained for provenance and regression checks, not imported wholesale at runtime.
The user requested the SDF reference and the ProjectZero editor presentation separately.

## SDF, geology and stack reference

[c7egoist/Frontier, requested branch][SdfBranch], commit `09b7064f282f4df5c5352f2bc97c82fb1c06a098`.
`SourcePin.json` records paths, byte lengths and Git blob hashes of six unmodified files:

| Original path                          | Use in this prototype                                        |
| -------------------------------------- | ------------------------------------------------------------ |
| `src/erosion/terrain-stack-presets.js` | Generator catalogue/default/ordering reference               |
| `src/erosion/pipeline-shaders.js`      | Inspected height-channel pipeline; deliberately not imported |
| `src/geology/polyhedron.js`            | Conservative volumetric-geometry reference                   |
| `src/erosion/layer-stack-ui.js`        | Historical operation-row and stack reference                 |
| `src/app/style.css`                    | Historical reference only; not the active editor theme       |
| `index.html`                           | Historical layout reference                                  |

The corrected runtime keeps signed 3D CSG and volumetric lattice noise. It does **not** replace terrain with the
reference erosion pipeline's height textures or claim its full geology/erosion/simulation implementation.

## ProjectZero editor presentation

[SultanAladin/Frontier-][EditorRepository], commit `28ec0657a23ed6bbf680f17cc06b464a53639dae`.
`ProjectZeroPin.json` records both unmodified source snapshots and their Git blob hashes:

| Original path                           | Local snapshot          |
| --------------------------------------- | ----------------------- |
| `Experimental/FrontierEditor/style.css` | `ProjectZeroStyle.css`  |
| `Experimental/FrontierEditor/src.jsx`   | `ProjectZeroSource.jsx` |

The corrected `Workspace.css` adapts the source's final **neutral graphite** cascade: `#151515` outliner,
`#101010` workspace background, monochrome selected-row gradients, subdued borders, DM Sans typography and rounded
inspector cards. `StackGlyphs.js` uses the pinned Lucide package. The source React component and stylesheet are
retained byte-for-byte; the runtime uses plain DOM controls connected to the independent SDF stacks.

The previous erosion-style `ReferenceStack.css` / `StackDock.css` extraction is no longer used or retained.
This is an adaptation of the requested editor source, not a claim of pixel-identical native UI parity.

## Provenance and checks

- `LandformStack.test.js` verifies the six SDF-reference snapshots against their original Git blobs.
- `ReferenceValidation.test.js` verifies both ProjectZero snapshots and the active graphite-layout markup.
- Both references and their immutable identities are pinned in the root `CLAUDE.md`.
- Actual captures and reproducible browser checks live in [Exhibits/LandscapeWorkspace][Evidence].
- Source snapshots were retained at the user's explicit request. No additional upstream redistribution licence is
  asserted. Lucide and the reused local fonts retain their own package/font licences.

[SdfBranch]: https://github.com/c7egoist/Frontier/tree/arena%2F68c84c0d-frontier
[EditorRepository]: https://github.com/SultanAladin/Frontier-
[Evidence]: ../../../Exhibits/LandscapeWorkspace/README.md
