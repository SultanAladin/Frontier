# Visibility resolve — visual evidence (Phase 3)

Renders produced by `Tools/VisibilityResolveProof.mjs` using the CPU reference path in
`Source/VisibilityResolve.js` — the same maths the GPU programs (`Shader/VisibilityRaster.wgsl`,
`Shader/MaterialResolve.wgsl`) run. Regenerate with:

```
node Tools/VisibilityResolveProof.mjs
```

Per scene (`glass-sphere`, `uv-creased-vessel`):

- `<scene>-visbuffer.png` — visibility buffer coloured by cluster record identity (brighter = glass
  layer). Every non-background pixel decodes to a (cluster, triangle) pair that was actually drawn.
- `<scene>-resolved.png` — deferred opaque resolve only: family palette + UV checker modulation +
  one directional light, computed purely from visbuffer identity and barycentrics.
- `<scene>-final.png` — final composite: glass pixels refract through their front (entry) and
  depth-peeled back (exit) layers, gather the resolved opaque image at the exit projection, and mix
  Beer absorption tint with a Schlick Fresnel sky reflection.

`stats.json` records visible clusters/triangles and the glass pixel counts (total and two-layer).

Glass discipline visible in the renders: transmissive geometry never writes the opaque visibility or
depth buffers, is itself occluded by nearer opaque geometry, and only the front two glass layers along
a ray take part in the refraction gather.
