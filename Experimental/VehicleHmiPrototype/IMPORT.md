# Import provenance

Imported 2026-10-21 from
[`c7egoist/Frontier@6b153d1`](https://github.com/c7egoist/Frontier/tree/arena/86baabb5-frontier)
(branch `arena/86baabb5-frontier`), at the user's request, as the HTML Editor prototype that
carries the SDF scene work. `GEOMETRY_CLUSTERING.md` is copied from that fork's `Docs/`.

Local work on top of the import: the full geometry hierarchy (Phase 1 of the virtualized
geometry programme) in `Source/GeometryHierarchy.js`, its tests and proof tool under `Tools/`,
and the hierarchy debug overlay. Run with:

```sh
npm install
npm test          # node test suite
npm run serve     # http://0.0.0.0:8080 (WebGPU editor)
node Tools/GeometryHierarchyProof.mjs   # hierarchy evidence images
```
