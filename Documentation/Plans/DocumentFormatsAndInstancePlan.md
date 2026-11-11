# Frontier Document Containers, Sub-Formats & `Instance` Architecture Plan

> **Status:** 🚩 Active Design Plan (Saved for iterative refinement; focused Phase-1 scope targets **Documents + Workspaces + `.GeometrySpace` / `.Material` / `.ImageSpace` / `.UVSpace` + Geometry Attributes + `Instance` Base**).
>
> **Governance:** Strictly adheres to [../BannedWordsVocabulary.md](../BannedWordsVocabulary.md), [../CodingStandards.md](../CodingStandards.md), and [../AgenticInstructions/SKILL-Naming.md](../AgenticInstructions/SKILL-Naming.md) (zero `Surface`, zero `Mesh`, zero `UObject` vtable/GC bloat, zero locked `Topology`/`HalfEdge` folder names).

---

## 1. Two-Tier Container Format (1 Global Extension + Embedded Sub-Formats)

Every asset or workspace document on disk uses **1 Global PascalCase Extension** (`.GeometrySpace`, `.Material`, `.ImageSpace`, `.UVSpace`, `.Pigment`, `.Assembly`, `.Sketch`, `.Vector`, `.Motion`, `.Render`, `.Terrain`, `.projectspace`, `.FontSpace`) backed by a single unified container codec:

```text
+=============================================================================================+
| TIER 1 — UTF-8 TOML MANIFEST & ANALYTIC SUB-FORMATS (PascalCase, parsed via toml++)         |
|   ['.DocumentHeader']    FormatSignature = "FrontierDocument" · SchemaVersion = 1           |
|                          DocumentCategory = "GeometrySpace" · Units = "Meters"              |
|   ['.ColorSpace']        Primaries = "ACEScg" · WhitePoint = "D60" · Transfer = "Linear"    |
|   ['.<SubFormat>']       Analytic math / closure / constraint / camera / brush parameters   |
|   [['.BinaryDirectory']] Tag = ".Positions" · Offset = 0      · Length = 393216 · Checksum  |
|   [['.BinaryDirectory']] Tag = ".Normals"   · Offset = 393216 · Length = 393216 · Checksum  |
|   [['.BinaryDirectory']] Tag = ".UV0"       · Offset = 786432 · Length = 262144 · Checksum  |
|   [['.BinaryDirectory']] Tag = ".Revision"  · Offset = 1048576· Length = 65536  · Checksum  |
+=============================================================================================+
| 64-BYTE SIMD / GPU STAGING ALIGNMENT BOUNDARY ("\n#===FRONTIER-BINARY-SECTIONS===\n\0")     |
+=============================================================================================+
| TIER 2 — 64-BYTE-ALIGNED BINARY SECTIONS (SFNT/TTF-style random-access binary slices)       |
|   • Geometry attribute streams (Positions, Normals, Tangents, UVs, Creases, MaterialSlots)  |
|   • Radiometric / color-managed image tiles & mip blocks (.ImageSpace or embedded)          |
|   • Parametric OperationRecordEntry stream ('.Revision' <= 215 MiB per document)            |
+=============================================================================================+
```

---

## 2. Lifecycle & Undo/Redo Memory Budgets

| Unit | Default Ceiling | Lifecycle Registration & Mechanism |
| :--- | :---: | :--- |
| **`WorkspaceDocument`** | **`215 MiB`** (`225,443,840 B`) | Per-document parametric `OperationRecordEntry` log (CAD bevels, paint strokes, UV seam cuts, geometry edits). Reconstructs the document from scratch at **any target resolution or tessellation tolerance**. |
| **`RevisionExtension`** | **`750 MiB`** (`786,432,000 B`) | Registered `SuspendableParticipant` in `WorkspaceLifecycle`; tracks cross-document `GlobalSequenceStamp` for application-wide Undo/Redo. |
| **Overflow Compaction** | **Redo Purge → Prefix Fold** | ① Purge truncated redo branches; ② fold oldest prefix operations into the parametric baseline so resolution-independent reconstruction from scratch remains unbroken. |

---

## 3. `Instance` Categories (Lean C++20 Replacement for `UObject`)

All world, simulation, and asset entities use **`<Domain>Instance`** categories sharing a compact, cache-aligned header (`InstanceToken`, `InstanceCategory`, `InstanceDirectiveMask`, non-kinship `EnclosingSlot` / `FirstNestedSlot` / `NextPeerSlot` tree indices, and `Transform3D` / domain slot index) with **zero GC, zero RTTI, and zero virtual `Tick` on passive instances**:

| Instance Category | Domain Role & Shared Functionality | Primary Container / Source |
| :--- | :--- | :--- |
| **`GeometryInstance`** | Indexed polygon/CAD geometry, attribute streams (Position, Normal, Tangent, UV, Color, Crease), material slot assignments & LOD bounds | **`.GeometrySpace`** · `.Assembly` · `.Solid` |
| **`MaterialInstance`** | Evaluated BSDF/BSSRDF closure parameters, `.slang` constant block & bound `.ImageSpace` channels | **`.Material`** |
| **`ImageInstance`** | Color-managed 2D/3D/cubemap pixel & radiance tiles, mip pyramid & sampler directives | **`.ImageSpace`** |
| **`PlayerInstance`** | Possessed input routing, camera rig attachment, movement solver & interaction locus | `.Simulation` · `.projectspace` |
| **`AgentInstance`** | Autonomous navigation, steering, perception & behavior directives | `.Simulation` |
| **`ParticleInstance`** | GPU compute particle / ribbon / fibre simulation slot & force-field coupling | `.Particle` · `.Force` |
| **`CloudInstance`** | Volumetric Worley/Perlin density, phase scattering & altitude bounds | `.Volumetric` · `.Terrain` |
| **`TriggerInstance`** | Analytic spatial overlap volume (`SpatialExtent` / SDF) firing deterministic events | `.Collision` · `.Simulation` |
| **`AudioInstance`** | 3D spatialized emitter, attenuation curve, occlusion & `.Acoustic` bus routing | `.Acoustic` |
| **`ProjectileInstance`** | High-velocity swept-ray / ballistic solver with restitution & impact directives | `.Collision` · `.Simulation` |
| **`PhotometricInstance`** | Analytic area light, `.IES` luminaire, or `.HDRI` dome emitter | `.Photometry` |
| **`TerrainInstance`** | Parametric clipmap elevation, strata, `.Foliage` & `.Ecosystem` scatter | `.Terrain` |

---

## 4. Master Global Container & Embedded Sub-Format Catalog

*(Saved from planning session for future domain phases; banned words replaced per `BannedWordsVocabulary.md`)*

### 4.1 Phase-1 Active Focus — Geometry, Materials, Images, UVs & Project

| Global Extension | Embedded / Companion Sub-Formats | Stored Payload |
| :--- | :--- | :--- |
| **`.projectspace`** | `.visualappearance` · `.FontSpace` · `.workspacelayout` · `.inputsettings` · `.notificationsettings` | Project-wide UI, typography, docking, input chords & telemetry settings |
| **`.GeometrySpace`** | `.Points` · `.Corners` · `.Facets` · `.Edges` · `.Normals` · `.Tangents` · `.UV0..UV7` · `.VertexColor` · `.Creases` · `.MaterialSlots` · `.UDIM` · `.UVChart` · `.Revision` | Complete resolution-independent / multi-attribute geometry & parametric edit log |
| **`.Material`** | `.ColorSpace` · `.BSDF` · `.BSSRDF` · `.Microfacet` · `.Fresnel` · `.Anisotropy` · `.ThinFilm` · `.Clearcoat` · `.Sheen` · `.Transmission` · `.Dispersion` · `.Absorption` · `.Subsurface` · `.Phase` · `.Reflectance` · `.Roughness` · `.Dielectric` · `.Conductor` · `.Closure` · `.ClosureNetwork` · `.Albedo` · `.Irradiance` · `.LayerStack` · `.Decal` · *(embedded `.ImageSpace` blocks)* · `.Revision` | Analytic PBR / spectral material closures, embedded textures & procedural layers |
| **`.ImageSpace`** | `.ColorSpace` · `.Exposure` · `.Tone` · `.HDRI` · `.LayerStack` · `.Tiles` · `.Mips` · `.Revision` | Radiometric SDR/HDR multi-channel tiled images with non-destructive grading |
| **`.UVSpace`** | `.UDIM` · `.UVChart` · `.Revision` | Standalone or `.GeometrySpace`-embedded parameterization, seam pins & UDIM layout |

### 4.2 Deferred Domain Containers & Sub-Formats (Saved for Later Refinement)

| Domain | Global Extension | Sub-Formats |
| :--- | :--- | :--- |
| **CAD Solid & Sketch** | **`.Assembly`** · **`.Sketch`** | `.Part` · `.Solid` · `.BRep` · `.Feature` · `.Sketch` · `.Section` · `.Curve` · `.Collision` · `.Revision` |
| **Paint & Natural Media** | **`.Pigment`** | `.Stroke` · `.StrokeHistory` · `.Impasto` · `.Watercolor` · `.Bristle` · `.Dab` · `.Brush` · `.PigmentRamp` · `.PaintLayer` · `.Ink` · `.Airbrush` · `.Stencil` · `.Decal` · `.Splat` · `.Smudge` · `.Glaze` · `.Charcoal` · `.Gouache` · `.Media` · `.Stylus` · `.ColorSpace` · `.Revision` |
| **Vector Graphics** | **`.Vector`** | `.Curve` · `.Stencil` · `.LayerStack` · `.ColorSpace` · `.Revision` |
| **Animation & Motion** | **`.Motion`** | `.Timeline` · `.Sequence` · `.Track` · `.Keyframe` · `.FCurve` · `.Curve` · `.Revision` |
| **Rendering & Optics** | **`.Render`** | `.RenderPreset` · `.RenderQueue` · `.RenderSequence` · `.Camera` · `.SensorGate` · `.Shading` · `.Optics` · `.Integrator` · `.Radiance` · `.AOV` · `.Viewport` · `.Spectral` · `.Scatter` · `.Atmosphere` · `.RayBudget` · `.Exposure` · `.Tone` · `.Denoiser` · `.ColorSpace` · `.Revision` |
| **Photometry** | **`.Photometry`** | `.AreaLight` · `.IES` · `.HDRI` · `.Spectral` · `.ColorSpace` · `.Revision` |
| **Simulation & Volumetrics** | **`.Simulation`** · **`.Volumetric`** | `.Collision` · `.Force` · `.Absorption` · `.Scatter` · `.Phase` · `.Cloud` · `.Revision` |
| **World & Atmosphere** | **`.Terrain`** | `.HeightField` · `.Foliage` · `.Ecosystem` · `.WorldSynthesis` · `.Weather` · `.Cloud` · `.Sky` · `.Atmosphere` · `.Revision` |
| **Acoustics & Fonts** | **`.Acoustic`** · **`.FontSpace`** | `.Acoustic` · `.Curve` · `.Timeline` · `.FontSpace` |
