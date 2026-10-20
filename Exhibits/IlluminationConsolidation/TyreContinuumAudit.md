# Existing tyre code: FEM reuse audit

Requested change: **retain XPBD and add selectable 3D shell FEM**, reusing an existing FEM implementation.
No tyre solver or runtime selection was changed during this audit.

## Result

**Neither shell FEM nor solid FEM was found in the inspected donor tyre paths.**
Their README terminology is misleading: four describe “GPU Chaos Flesh physics” and
“PB-FEM deformation continuum”, but their implementation is a scalar spring response plus analytic deformation.
The fifth, older c6 browser vehicle explicitly uses point contact and has no corresponding tyre visual shader.
This result does not establish absence from other branches, repositories or historical revisions.

| Inspected path | Mechanism actually present |
| --- | --- |
| Donor `Source/VehicleSolver.js` | Point contact; pressure-dependent scalar tyre stiffness in series with suspension |
| Donor `Source/TyreThermalSolver.js` | Gas/temperature/leak model; explicitly says no XPBD carcass, uses rest volume |
| Donor `Shader/VehicleVisual.wgsl` | Closed-form patch flattening, sidewall bulge, shoulder wave and lateral shear |
| Combined `Source/TyreCarcassProjection.js` | Newly added reduced radial XPBD; not a reused FEM solver |

The shader computes, for example:

```wgsl
let PatchSpan = clamp(0.44 / sqrt(max(0.15, PNorm)), 0.20, 1.15);
DR = -Deflection * Envelope * RadialFrac;
let BulgeIntensity = 0.48 / (0.4 + 0.6 * sqrt(PNorm));
DY = sign(Position.y) * (Deflection * BulgeIntensity * Envelope * SidewallFactor);
```

These are prescribed displacements, not a solved finite-element displacement field.
Calling the surface deformation “volumetric” does not make it solid FEM.
The inspected paths do not implement shell membrane/bending finite elements or solid volume elements.
The existing `VolumetricTyrePhysics.test.mjs` checks encoded deformation parameters, not FEM mechanics.

The current checkout was also searched for FEM/finite-element/co-rotational mechanics;
no reusable tyre FEM implementation was found. The remote branch heads for the five requested sources still
match the pinned revisions in `Code/Sources.json`; this is not a stale-pin versus updated-head discrepancy.

## Executed verification

Linux, Python 3 and authenticated `gh`; pinned revisions, with local uncommitted combined app unchanged:

```sh
python Exhibits/IlluminationConsolidation/Code/InspectTyreContinuum.py
```

Result: **PASS: 5 pinned vehicle solvers inspected; 4 analytic tyre shaders confirmed.**
`Results/TyreContinuumAudit.json` retains exact revisions, SHA-256 content digests and relevant source quotes.
The script fetches the implementation paths directly through GitHub API and fails on fetch/assertion errors.
This is a source audit, not a FEM validation test or an application runtime test.

## Subsequent scope decision

The user subsequently authorised **new shell and solid FEM implementations**, retaining spring and XPBD.
These are now implemented in `Experimental/IlluminationPrototype/Source/TyreContinuumElasticity.js`.
This audit's finding about the pinned donors is unchanged; the new FEM code is not a recovered donor solver.

## Historical dependency at audit time

XPBD must remain available. A genuine FEM option must not simply select the old analytic deformation.
To reuse existing FEM, its repository/ref/path is needed. If the intended code is elsewhere, inspect and port it
before deciding shell versus solid. If it cannot be supplied, a new shell FEM implementation would require explicit
scope agreement rather than presenting the analytic donor shader as reused FEM.
