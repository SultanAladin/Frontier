# Document Containers, Sub-Formats & Instance Concepts

> **Purpose:** Conceptual blueprint for how Frontier stores assets on disk, embeds domain sub-formats inside single-file containers, records resolution-independent edit history within fixed memory budgets, and organizes world/workspace instances without object-hierarchy bloat.
> *(No folder structures or C++ class names are locked in this document.)*

---

## 1. Core Concept: One Global File Extension + Embedded Sub-Formats

Instead of scattering an asset across dozens of loose sidecar files or opaque binary blobs, every Frontier document uses a **Two-Tier Hybrid Container** under **one Global Extension** on disk (such as `.Material`, `.ImageSpace`, `.UVSpace`, `.GeometrySpace`, `.Pigment`, `.Assembly`, `.Sketch`, `.Vector`, `.Motion`, `.Render`, `.Terrain`, `.FontSpace`, or `.projectspace`):

```text
+======================================================================================+
| TIER 1 — HUMAN-READABLE UTF-8 TOML MANIFEST (Top of File)                            |
|   • Document identity, schema version, authoring units, and color primaries          |
|   • Analytic domain sub-formats (e.g. .ColorSpace, .BSDF, .Microfacet, .Subsurface)  |
|   • Binary Section Directory (TTF/SFNT-style offset, byte length, 64-bit checksum)   |
+======================================================================================+
| 64-BYTE ALIGNMENT BOUNDARY (Explicit ASCII delimiter + zero-padding to 64 B)         |
+======================================================================================+
| TIER 2 — PACKED 64-BYTE-ALIGNED BINARY SECTIONS (Bottom of File)                     |
|   • Parametric baseline data, heavy numeric streams, or tiled image blocks           |
|   • Embedded '.Revision' parametric operation stream (<= 215 MiB per document)       |
+======================================================================================+
```

- **Inspectable & Diffable:** Opening any Frontier file in a plain text editor reveals its human-readable TOML header and analytic parameters at the top.
- **Zero-Parse Binary Speed:** Heavy numeric arrays, image tiles, and operation streams sit after a `64-byte` alignment boundary with exact byte offsets and checksums — just like OpenType/TrueType (`sfnt`) font tables — allowing instant random-access reads and direct GPU staging uploads.
- **Self-Contained Composition:** A single `.Material` file can embed `.ColorSpace`, `.BSDF`, `.Microfacet`, `.Subsurface`, `.Clearcoat`, `.ThinFilm`, embedded `.ImageSpace` texture blocks, and `.Revision` inside one portable file. Sub-formats (such as `.Brush` or `.IES`) can also be exported standalone as presets using the exact same section rules.

---

## 2. Core Concept: Resolution-Independent Revision & Memory Caps

Documents do not store heavy rasterized pixel snapshots or baked polygon dumps per undo step. Instead, every workspace records **parametric operations** — compact, mathematical descriptions of *what changed*:

- **Paint Workspace (`.Pigment`):** Records normalized `[0, 1]` spline control points, stylus pressure/tilt curves, brush formulation, and PRNG seed (`~128 B – 2 KiB` per stroke).
- **CAD Workspace (`.Sketch` / `.Assembly`):** Records curve equations, dimensional constraints, or feature parameters (which topological edge was beveled, metric radius, profile shape).
- **UV & Image Workspaces (`.UVSpace` / `.ImageSpace`):** Records seam cuts, pin coordinates, relaxation parameters, or non-destructive exposure/chromaticity adjustments.

| Budget Scope | Default Limit | Behavior & Guarantees |
| :--- | :---: | :--- |
| **Per-Document Revision Limit** | **`215 MiB`** | Dedicated operation budget per open document — large enough for hundreds of thousands of parametric strokes, bevels, or UV operations. |
| **Global Application Undo Limit** | **`750 MiB`** | Shared ceiling across all simultaneously open documents in the application, ordered by a monotonic cross-document sequence stamp for global Undo/Redo. |
| **Overflow Compaction** | **Redo Purge → Prefix Fold** | When either limit is reached: ① truncated redo branches are discarded first; ② the oldest prefix operations are folded into the parametric baseline so **from-scratch reconstruction at any target resolution or tolerance never breaks**. |

💡 **Resolution Independence:** Because both the baseline and the recorded operations are parametric, the exact same `215 MiB` document history can reconstruct a fast `512 × 512` interactive preview, a `2048 × 2048` working view, or an `8192 × 8192` final export (or coarse vs. fine CAD tessellation) from scratch without quality loss.

---

## 3. Core Concept: Lean Domain Instances (Replacing `UObject` Bloat)

Traditional engine base objects (`UObject`-style hierarchies) burden every entity in memory with virtual dispatch tables, garbage-collection headers, runtime reflection pointers, and scattered heap allocations (~`128–256 B` of overhead per object even when idle).

Frontier replaces that pattern with **lean, category-grouped Instances**:

1. **Compact Shared Identity (`~32 B`, Zero Virtual Overhead):** Every instance carries only a generation-checked token, its domain category tag, compact visibility/simulation/lock directives, non-kinship tree indices (enclosing, first-nested, and next-peer slots), and a transform slot index.
2. **Contiguous Category Storage:** Instances with shared behavior live together in contiguous, cache-friendly arrays by category rather than one polymorphic heap soup:
   - **Authoring & Visual Categories:** Geometry, Material, Image, Photometric, Terrain, Cloud, Particle.
   - **Simulation & Interactive Categories:** Player, Agent, Trigger, Audio, Projectile.
3. **Lifecycle Participation Only Where Needed:** Passive asset instances never pay per-tick virtual call overhead; only active simulation/runtime categories participate in ordered lifecycle ticks.

---

## 4. Master Global Extensions & Embedded Sub-Format Catalog

*(Reference catalog for iterative refinement across future domain phases; zero `Surface` naming.)*

| Domain | Global File Extension | Embedded Sub-Format Sections | Conceptual Payload |
| :--- | :--- | :--- | :--- |
| **Project Settings** | **`.projectspace`** | `.visualappearance` · `.FontSpace` · `.workspacelayout` · `.inputsettings` · `.notificationsettings` | Theme swatches, UI scale, font bindings, workspace splits, shortcut chords, alert preferences |
| **Typography** | **`.FontSpace`** | `.TypefaceGroup` · `.WeightList` · `.Metrics` | Scalable vector typefaces and optical weight mappings |
| **Geometry** | **`.GeometrySpace`** | `.Points` · `.Corners` · `.Facets` · `.Edges` · `.Normals` · `.Tangents` · `.UV0..UV7` · `.Creases` · `.UDIM` · `.UVChart` · `.Revision` | Multi-domain geometric attributes, seam/crease directives, material slot bindings, parametric edit log |
| **Materials & Shading** | **`.Material`** | `.ColorSpace` · `.BSDF` · `.BSSRDF` · `.Microfacet` · `.Fresnel` · `.Anisotropy` · `.ThinFilm` · `.Clearcoat` · `.Sheen` · `.Transmission` · `.Dispersion` · `.Absorption` · `.Subsurface` · `.Phase` · `.Reflectance` · `.Roughness` · `.Dielectric` · `.Conductor` · `.Closure` · `.ClosureNetwork` · `.Albedo` · `.Irradiance` · `.LayerStack` · `.Decal` · *(embedded `.ImageSpace` blocks)* · `.Revision` | Analytic physical reflection/transmission/subsurface math, procedural layers, and embedded texture channels |
| **Images** | **`.ImageSpace`** | `.ColorSpace` · `.Exposure` · `.Tone` · `.HDRI` · `.LayerStack` · `.Tiles` · `.Mips` · `.Revision` | Color-managed SDR/HDR multi-channel tiled images and non-destructive radiometric adjustments |
| **UV Layout** | **`.UVSpace`** | `.UDIM` · `.UVChart` · `.Revision` | Seam cuts, pinned chart coordinates, UDIM tile assignments, texel-density targets |
| **CAD Solid & Sketch** | **`.Assembly`** · **`.Sketch`** | `.Part` · `.Solid` · `.BRep` · `.Feature` · `.Sketch` · `.Section` · `.Curve` · `.Collision` · `.Revision` | Exact analytic 2D/3D curves, dimensional constraints, watertight volumes, booleans, bevels, mates |
| **Paint & Media** | **`.Pigment`** | `.Stroke` · `.StrokeHistory` · `.Impasto` · `.Watercolor` · `.Bristle` · `.Dab` · `.Brush` · `.PigmentRamp` · `.PaintLayer` · `.Ink` · `.Airbrush` · `.Stencil` · `.Decal` · `.Splat` · `.Smudge` · `.Glaze` · `.Charcoal` · `.Gouache` · `.Media` · `.Stylus` · `.ColorSpace` · `.Revision` | Natural media physics, bristle/dab dynamics, stylus response curves, resolution-free stroke splines |
| **Vector Graphics** | **`.Vector`** | `.Curve` · `.Stencil` · `.LayerStack` · `.ColorSpace` · `.Revision` | Exact Bézier/polynomial contours, boolean winding, variable-width strokes |
| **Animation & Motion** | **`.Motion`** | `.Timeline` · `.Sequence` · `.Track` · `.Keyframe` · `.FCurve` · `.Curve` · `.Revision` | Timecode lanes, shot timing, cubic Hermite/Bézier function curves |
| **Rendering & Optics** | **`.Render`** | `.RenderPreset` · `.RenderQueue` · `.RenderSequence` · `.Camera` · `.SensorGate` · `.Shading` · `.Optics` · `.Integrator` · `.Radiance` · `.AOV` · `.Viewport` · `.Spectral` · `.Scatter` · `.Atmosphere` · `.RayBudget` · `.Exposure` · `.Tone` · `.Denoiser` · `.ColorSpace` · `.Revision` | Physical camera/lens optics, integrator bounce budgets, AOV routing, multi-shot render queues |
| **Photometry** | **`.Photometry`** | `.AreaLight` · `.IES` · `.HDRI` · `.Spectral` · `.ColorSpace` · `.Revision` | Calibrated luminaire candela distributions, area emitters, spherical sky radiance |
| **Simulation & Volumes** | **`.Simulation`** · **`.Volumetric`** | `.Collision` · `.Force` · `.Absorption` · `.Scatter` · `.Phase` · `.Cloud` · `.Revision` | Physical solver parameters, force fields, sparse density/distance volumes |
| **World & Weather** | **`.Terrain`** | `.HeightField` · `.Foliage` · `.Ecosystem` · `.WorldSynthesis` · `.Weather` · `.Cloud` · `.Sky` · `.Atmosphere` · `.Revision` | Parametric elevation strata, biome competition rules, atmospheric & cloud synthesis |
| **Acoustics** | **`.Acoustic`** | `.Acoustic` · `.Curve` · `.Timeline` · `.Revision` | Room impulse responses, frequency-band wall absorption, spatial attenuation |
