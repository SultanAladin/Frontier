# Celestial transform gizmo port

The previous original SVG approximation has been removed. `Source/CelestialGizmoRenderer.js` ports the actual Three.js gizmo construction, material palette, camera-facing ring, axis line, ray picking and hover highlighting from `SultanAladin/Frontier-`, `docs/celestial/index.html`, commit `da3720b353dc0e4d2421f064f65cbfeb41cdb5f2` (the deployed celestial panel).

The source excerpt is archived in `ReferenceApps/Working/Gizmo/CelestialOriginal.txt`. Three.js **0.160.0**, matching the reference import, is vendored locally under `Source/ThirdParty/three`, including its MIT license; there is no runtime CDN dependency.

## Design retained

- Exact source palette: red X, green Y, blue Z; cyan/magenta/yellow plane pads.
- Cone move handles `.06 × .18`, 24 segments; cylindrical scale handles `.06 × .14`, 24 segments.
- 31° rotation arc bars, radius `.95 × .62`, band `.038`, 24 segments.
- White camera-facing torus centre ring `.16 / .008`, 12 × 48 segments.
- Source roughness `.45`, metalness `.1`, ambient `.75`, directional light `.9`, depth-disabled overlay handles.
- Source constant screen sizing `depth × tan(fov/2) × .22` and raycaster hit testing against real 3D handle meshes.
- Compact translucent modal HUD and long axis constraint line. Hover brightens the original handle material.

## Host adaptations

The gizmo uses the editor viewport dimensions and native Z-up camera. **It can only manipulate an explicitly clicked Outliner selection while the camera is in Free mode.** A stored/default inspector or programmatic selection is not enough to arm it. Objects without a transform adapter have no gizmo. Returning to a driving camera hides the HUD/handles and disables picking. The same explicitly selected object can remain selected across camera-mode changes.

Use the HUD **Move / Rotate / Scale** buttons and drag a highlighted handle. Bare G/R/S/F keyboard-modal activation was removed: S must remain camera reverse, while R/F already have application actions. WASD/QE/arrow navigation passes through untouched. If navigation starts during a drag, the edit is cancelled/restored before the camera receives the key. Right/middle-button and off-handle drags remain camera look; handle capture is suppressed while navigation keys are held or camera look is active.

**Axes follow object rotation.** Physics/vehicle targets derive an orthonormal basis from their quaternion; static objects use normalized transform columns, excluding their scale. Translation axes and planes, rotation constraints and rendered axis lines use that same local basis. The centre ring counter-rotates to remain camera-facing. Nonrotatable axis-aligned volume settings retain their actual identity basis. Scale handles change only the selected local scale component where scaling is supported; static scaling preserves the world-space bounds centre.

Hover changes the actual Three material to a brighter warm highlight and redraws immediately, with a grab cursor; the cursor becomes grabbing during manipulation. Clearing the gizmo also disables picking against stale handle geometry. The overlay is not re-cleared/rendered each frame when inactive.

**Ctrl** snaps translation to .25 m, rotation to 5°, scaling to .1×. During a handle drag, X/Y/Z changes the local constraint, Enter confirms and Esc restores; release commits. Selection changes, pointer cancellation, camera exit and window blur restore unfinished edits. Merely pressing a handle without moving it does not call a target mutation. Vehicle simulation is frozen only during its explicitly requested Free-mode drag, then restored to its previous frozen state.

Volumes support translation and per-axis extent scaling but no invented volume rotation. Physics cones/props and the vehicle expose translation/rotation, not unsupported collision-shape scaling. Static adapters refresh GPU records/distance bounds and obstacle AABBs; their collision remains AABB-based, not an oriented collider.

The real gizmo needs WebGL alongside the main WebGPU renderer, as in the reference. It does not replace the scene renderer. If WebGL initialization fails it reports that failure rather than silently displaying the rejected custom SVG approximation. The Node/Dawn DOM stub skips the WebGL overlay; actual overlay rendering/picking is verified in Chromium.

## Validation

`npm test` includes projection, actual renderer settings/physics mutation, quaternion rotation/restore, static transform pivot/restore, and exact source geometry/palette assertions. The browser proof uses actual pointer hits and drags on the 3D geometry, validates constrained translation, scale changes, physics-body rotation, and cancellation, and checks that no old SVG axis handles remain.

```sh
# from Experimental/VehicleHmiPrototype; optional tooling installation:
npm install --no-save --prefix Tools playwright @sparticuz/chromium@138.0.2
# with Tools/Serve.py running on 8080:
node Tools/CelestialGizmoBrowserProof.mjs
```

Proof screenshots/results are stored outside Git in `/home/user/local-gizmo-proof/`. Software rendering correctness tests do not establish desktop FPS cost or identical pixels across graphics drivers.

### Free-mode/local-space validation

202 tests pass. The Chromium proof checks driving-mode suppression, explicit Outliner gating, immediate material hover redraw, rotated-axis translation, quaternion-aligned handles, all six free-flight keys, real free-look, navigation cancellation, Esc restore, single-axis scaling, and stale-pick suppression after leaving Free mode. A rotated local-X drag changes world Y, not world X. All three rendered object-basis dot products exceed .99999. No page/console errors. Dawn exhibit smoke: 3 frames, 0 errors, 0 warnings.
