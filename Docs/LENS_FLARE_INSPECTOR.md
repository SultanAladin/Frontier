# Lens flare · camera-optics editor

Lens Flare now has an optics-specific charcoal/glass layout and four categories: **Compose / Ghosts / Halo / Rays & streaks**. The live procedural composite remains above the category controls, with source coordinates, reset, scene enable and HDR export. Existing accepted inspectors and rendering shaders are unchanged.

## Instruments and actual bindings

- **Compose:** original presets, global intensity, spread and chromatic separation. The thumbnail is the existing worker-rendered native kernel, display-mapped for viewing; it is not a decorative image or scene-camera capture.
- **Ghosts:** internal-reflection positions computed with the actual kernel's optical-axis law. Drag/arrow keys edit spacing. Original round/hexagonal/octagonal shapes, count, gain and spacing remain. The geometry guide omits off-sensor centres; guide outlines are not exact radiance or size.
- **Halo:** enlarged radius and ±width response-support guide with direct drag/keyboard radius editing, plus paired number/slider controls for radius, width and brightness. The composite is the procedural result; the guide is not a physical lens cross-section.
- **Rays & streaks:** independent anamorphic, warm-streak and burst switches, rotation, ray pairs, streak gain and legacy aperture blades. The effective ray-pair readout follows the renderer: presets use aperture blades, Custom layers uses ray pairs. Rotation leaves the cool anamorphic streak horizontal, as in the existing kernel.

Layer geometry and layer switches select Custom layers. Global intensity/spread/chromatic edits retain the selected preset. Aperture blades retain legacy semantics. Scene enable does not suppress the isolated authoring preview. Scene flare still follows Sun/camera visibility and cabin-overlay transmission.

HDR export retains the existing 640×280 bottom-up linear float RGB PFM path. The browser proof verifies the file signature and full 2,150,416-byte payload. Preview coordinates are authoring-only, not scene Sun placement.

## Performance and lifecycle

The existing worker and cached HDR data are retained. New geometry guides skip hidden panes and cache settings keys. Workers are terminated on inspector disposal. No additional GPU passes, shader changes or desktop-FPS guarantees are introduced.

## Validation

- **194 tests pass**, including existing native kernel/HDR cache regression tests and new shader-law ghost positioning / effective ray-pair tests.
- Browser proof exercises all categories, live source keyboard editing, preset application, ghost count/shape/keyboard spacing, halo keyboard radius, ray count/layer switching, enable toggle and real PFM download; **zero page errors**.
- Dawn full exhibit smoke: **3 frames, 0 errors, 0 warnings**.
- Overview, ghost-axis and halo screenshots visually reviewed.

Implementation: `Experimental/VehicleHmiPrototype/Source/NativeFlarePanel.js`, scoped `Style/LensOptics.css`, `Tools/LensOptics.test.mjs` and `Tools/LensOpticsBrowserProof.mjs`.
