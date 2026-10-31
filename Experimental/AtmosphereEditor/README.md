# Environment editor workspace

Standalone experimental HTML design inspired by the dark dashboards in `SourceImages/General UI/`: a charcoal
workspace, rounded data cards and colourful infographics. Atmosphere and Geometry detail share the same workspace. Use the ‹ / › buttons in the dock to switch editors; each keeps its settings while
switching. The preview and right-hand controls stay visibly split on compact screens. Scroll the dock to reach all
controls. Both previews have eased camera movement. Drag horizontally on a chart in a control card to adjust it;
these controls also retain native keyboard range input. Each card has its own coloured visual: a sun arc, molecular
bars, particles, ozone band, aperture, clouds, distance curve, threshold markers, triangles, falloff curves or
material pattern. The distance graph plots the
simulated retention curve; falloff shows a family of normalized transition shapes. The other graphics are schematic
value indicators, not measured spectra, particle counts, physical sun paths or material metrics. The larger
density and geometry summary graphs display the simulated profiles described below.

## Run

From the repository root:

```sh
python3 Experimental/AtmosphereEditor/Serve.py
```

Open `http://localhost:4173/`. Set `PORT` to change the port. The server only exposes the prototype and its optional
DM Sans font files. A WebGL 2 browser is required; a visible notice appears when it is unavailable. The page also
works at `/Experimental/AtmosphereEditor/index.html` on a standard repository-root static server. If the local font
archives have not been prepared, it falls back to a system sans-serif.

## Atmosphere preview

The canvas evaluates spherical atmospheric **single scattering** in WebGL 2: Rayleigh molecular and Mie aerosol
density, ozone absorption, solar-path attenuation, planetary shadow and a solar disc. Earth, Mars and Venus presets
change the approximate radius, density profiles, scattering coefficients, surface colours and cloud defaults. Their
appearance is visually guided, not a measured planetary simulation. The density infographic plots the preset's
exponential molecular profile over four scale heights.

The cloud layer is a low-cost, static 512 × 256 procedural atlas baked at startup and sampled on a spherical layer.
Surface view uses a locally tiled projection to show individual clouds; it will not exactly match the orbital pattern.
Clouds are not volumetric, animated or a weather simulation. Coverage and visibility are editable. The orbital
atmospheric rim has a clearly labelled, optional **3× scattered-light enhancement** to make the thin atmosphere easier
to see; switch it off for the unenhanced scattering. The enhancement does not apply to the surface sky.

Drag the orbital view to move the camera: it eases towards the pointer target with a 0.2-second lag. Switch between
orbital and 100 m surface views, change the sun elevation, molecular scattering, aerosols, ozone and exposure, or
reset the currently selected planet. Sun elevation is relative to the surface-view location, not a date/location
celestial solver. The dock can scroll when its contents exceed the available height.

This is a real-time approximation, **not a full atmospheric or weather simulation**: 24 view and 8 solar samples,
no multiple scattering, cloud volume, terrain, weather, refraction or physically calibrated camera. Planet albedo and
stars are procedural rather than real maps. Rendering uses a maximum device scale of 1 for performance. It does not
integrate with the C++ renderer or persist changes.

See [`Exhibits/AtmosphereEditor`](../../Exhibits/AtmosphereEditor/README.md) for browser interaction and numeric
checks, screenshots, reproduction and limitations.

## Geometry detail preview

The geometry editor builds and draws an actual procedural mesh with distance-dependent tessellation. Set preview
distance, full-detail near distance, reduced-detail far distance, far triangle retention and falloff shape. The curve
and drawn-triangle count show the approximation; counts change in discrete steps because the mesh uses integer rings.
Shaded and topology views show the surface or its triangles; drag to orbit. The preview distance also eases toward
the chosen value, although the object is deliberately kept large enough for inspection rather than following physical
projection at every distance.

Material detail controls a separate illustrative shader effect and does not automatically follow polygon count. In
real systems material quality and geometry LOD can be controlled independently; actual behavior depends on the engine.
This is a demonstration, not a production LOD algorithm: it has no mesh import, engine integration, streaming,
visibility culling or persistence. See [`Exhibits/GeometryDetail`](../../Exhibits/GeometryDetail/README.md) for
interaction tests and browser screenshots.
