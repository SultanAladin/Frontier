# FrontierEditor — HTML Shadow-Box Mirror for the C++ Frontier Engine

`FrontierEditor` is the **HTML shadow-box workspace** that mirrors the C++ Frontier Engine's authoring shell, scene outliner, inspector surfaces, and render/diagnostic controls.

## Why This Workspace Exists

AI coding agents frequently overestimate substandard UI, layout, and interaction work—especially when generating or modifying native C++ editor code directly, where visual regressions, awkward control density, missing domain parameters, and broken ergonomics are slower to spot and costlier to unwind.

To prevent unvetted work from landing in C++, **Frontier uses a strict two-stage gate**:

1. **Shadow-Box & Vet in HTML (`Experimental/FrontierEditor`)**
   - Level hierarchies, scene organization, inspector cards, interactive diagrams, lighting/atmosphere controls, and render/debug channels are built and iterated here first.
   - Every layout decision, slider range, icon binding, and workflow interaction is reviewed and approved live in the browser against the `ProjectZeroEditor` design language (`#090909` shell, dockable `Left` / `Centre` / `Right` panes, `Lighting` `.lp-card` rails, and `Folders` `.workspace-card` hierarchy).
2. **Convert Only Approved Surfaces to C++ (`Engine/`)**
   - Once a feature, level layout, or inspector surface is vetted and signed off in `FrontierEditor`, its exact hierarchy, state model, and vector/icon bindings (`EngineContent/Icons/*.svg` and `Engine/Interface/VectorCodec.cpp`) are ported 1:1 into the C++ engine.

## Architecture & Engine Parity

- **Project-Zero Shell & Design System (1:1 Parity)**: Mirrors `ProjectZeroEditor`'s 3-column dockable workspace (`Outliner`, `Viewport`, `Inspector` with draggable slanted `.document-tab` tabs, `+` dock menu, and double-click `Inspector` workspace expansion), top pull-down **Control Center** (`Notch` with 4×2 64px round quick-toggles, render-scale pill, and `Settings` hub), bottom pull-up **Asset Browser** (`+ Material`, `Save selected material`, `Import files`, category rail, catalogue, and `Standard PBR` preview), and viewport **`Edit` / `Sim` / `Play`** run-mode pills.
- **Shared Engine Icons & Vector Glyphs (`project-zero-icons.jsx`)**: Bundles the same `EngineContent/Icons/*.svg` assets and `Engine/Interface/VectorCodec.cpp` vector paths used by the C++ engine and `ProjectZeroEditor`, with botanical/fluid fallbacks only for domain types not yet present in `ProjectZeroEditor`.
- **Cameras & Post Process (`Cameras` Collection)**: Ships a dedicated `Cameras` folder containing permanent `Editor Camera` (navigation speed + thin-lens sensor), `Cine Camera` (3:2 optical framing), and `Post Process` (`EV Compensation` `-4..+4 EV`, `Saturation`, `Contrast`, `Bloom Intensity`, and `Vignette`).
- **Outdoor & Enclosed Sunless Studio Scenes**: Supports switching between outdoor `Evergreen valley.scene` (full `Environment`, `World`, `Water bodies`, `Foliage`, `Terrain`, `Materials`) and enclosed **`Lobby · Studio.scene`** (40 m studio cyclorama illuminated exclusively by `Overhead Softbox` and `Rim Strip Array` with **no** `Sun`, `Moon`, `Stars`, or outdoor `Atmosphere`).
- **Display & Debug Channels (`display-debug-inspectors.jsx`)**: Mirrors the engine's multi-bounce ReSTIR GI, default-on BVH ray-traced reflections, SVGF spatial/temporal stabilization, auto-exposure/EV grading, and diagnostic channels `00..10` (`F3` diagnostics overlay).

## Running Locally

```bash
cd Experimental/FrontierEditor
npm install
npm run dev
```

Build verification:

```bash
npx vite build
```
