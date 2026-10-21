# ControlVehicle · owned systems and wheel assemblies

## Organization

The Outliner keeps **one ControlVehicle assembly row**. Its Inspector has **Overview, Chassis & collision, Engine, Tyres, Suspension, Lights, HMI & cabin, Mirrors**. Only the active system/component is built; leaving it disposes its inspector callbacks. Arrow keys and Home/End navigate the system/component tabs.

Tyres has **Front left / Front right / Rear left / Rear right**, each with **Tyre & rim / Tyre marks / Smoke & fire / Rim sparks / Burnout**. Puncture, blowout and shred target the selected wheel. Cold pressure is explicitly axle-wide; model, wear, failure and fresh-tyre reset remain shared. All four wheels now have a matching inspection camera, including mirrored right-side rigs.

The formerly standalone Cabin, Tyre marks and Smoke & fire rows are hidden aliases, not extra copies of components. Existing `cabin`, `marks` and `gas` inspector links resolve into the assembly's corresponding section. Generic particle authoring and unrelated environment inspectors remain separate.

## Actual geometry grouping—not duplicate meshes

`Scene.VehicleComponents` explicitly groups the original geometry by owner. Each wheel has distinct **rubber and rim mesh-range children**. The archive supplies **3,968 rubber triangles + 1,568 rim triangles per wheel**; these ranges completely cover the original combined wheel facet set without gaps or overlaps. Left/right buffers remain shared and each wheel retains its independent existing occupant transform and damage/deformation state. The vehicle still has five dynamic draw occupants: body + four wheels. No rendering, BLAS, distance-field, shadow, material or tyre-deformation shader is replaced.

Body geometry has disjoint material-family reference groups for chassis, cabin, HMI, mirrors and lights. These are semantic groups, not a claim that every body fixture has been reconstructed as a separate Blender object. Hardware such as mirror housings sharing body paint remains part of the chassis family group. Mesh picking now returns a facet index and component owner as well as the original nearest material/distance result.

**Engine and suspension are real solver components, not invented mesh objects.** Their inspector explicitly states that the supplied asset has no dedicated engine or animated strut meshes. Engine shows actual RPM/boost/throttle/gear, authored torque curve, turbo and transmission specifications. Suspension shows four live strut lengths, compression, corner loads, contacts, calibrated spring/damper rates and authored mounts. Immutable physical specifications are read-only rather than misleading tuning controls that do not recalibrate the solver.

## Effect ownership versus shared services

- Tyre → rubber contact → **Tyre marks** → existing shared road-deposit atlas.
- Tyre → thermal/combustion source → **Smoke & fire** → existing shared vehicle-following GPU gas domain.
- Rim → actual metal contact → **Rim sparks** → existing shared spark renderer.
- Chassis → body scrape → the same shared spark renderer.

The registry records these relationships. Existing physical emission/source indexing and renderer paths are unchanged. Inspectors label global/shared enable, strength, variation and clear operations; selecting a wheel does not pretend to create an independent gas domain, mark atlas or per-wheel renderer override. The gas diagnostic test emitter is explicitly not a tyre.

## Collision inspection

Chassis shows actual hull sample count, support radius, contact count, mass, CoM and Jolt body status. Its body-local reference overlays all sampled hull centres over a sampled original-mesh wireframe. The optional viewport **collision x-ray** projects those hull centres, current wheel-radius rings and available contact normals using the current camera and live occupant transforms.

Course/static contacts use the existing sampled-point/support-radius hull. Jolt loose-body interaction uses a convex hull constructed from those points. The overlay is labelled as sample centres and wheel radii, **not the tessellated Jolt convex surface or a new scene mesh**. It is opt-in diagnostic work; no desktop FPS guarantee is made.

## Preservation and validation

Existing lights, HMI/tablet applications, glass/mirror controls, cameras, tyre FEM/shell/XPBD/spring modes, marks, smoke/fire, spark variation and damage/reset actions are retained under their owners. Lighting targets now read the live lighting system rather than stale inspector snapshots, including subsequent keyboard changes. The chassis owns the vehicle transform card.

Unit tests verify geometry partitioning, effect ownership, facet picking owners, aliases, four camera rigs and **real archive** triangle coverage while retaining five dynamic occupants. Browser proof exercises all eight systems and all tyre subcomponents, selected-wheel-only damage, selected wheel camera, mark strength, shared gas controls, spark variation, actual headlights/mirrors, collision overlay, old links and tab keyboard navigation. Proof files are kept outside Git.

Implementation: `Source/VehicleComponents.js`, `Source/VehicleSystemsInspector.js`, `Source/VehicleCollisionOverlay.js`, scoped `Style/VehicleSystems.css`; selective existing builders in `InspectorPages.js`. The headless harness received DOM `replaceChildren` support needed by the existing vehicle gizmo; production gizmo code is unchanged.

### Recorded result

- **199 tests passed, 0 failed.**
- Full exhibit/Dawn traversal of all eight systems and five tyre components: **12 frames, 0 errors, 0 warnings**.
- Browser proof: selected RR puncture leaves the other three leak states unchanged; selected-wheel camera resolves to Tyre RR; real mark/spark/light/mirror edits and shared-domain controls pass; collision overlay and alias/keyboard navigation pass; **no page or console errors**. A subsequent real frame and submitted GPU work also complete.
- Original Stars/shared celestial hashes remain unchanged. Geometry tests retain five dynamic occupants and verify the original tyre/rim archive ranges, rather than relying on an invented geometry count.
