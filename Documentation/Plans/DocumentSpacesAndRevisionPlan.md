# Active Plan: Workspace Documents, `.Space` Extensions & Document Revision

> **Active Scope:** Document container format, `.Space` / asset extension rules, and lifecycle-managed parametric document revision (`215 MiB` per document / `750 MiB` application-wide).
> **Excluded Here:** Geometry representations and geometric topology *(deferred to a dedicated later plan)*.

---

## TL;DR

Every workspace document is a **modular lifecycle-tracked unit** saved under a single **`.Space` or domain extension** using a **TOML Manifest + 64-Byte-Aligned Binary Section** container. Edits are recorded as **parametric operations** (never fixed-resolution pixel or polygon snapshots) inside a **`215 MiB` per-document** budget and a **`750 MiB` global application** undo budget, enabling full document reconstruction at **any target resolution or precision**.

---

## 1. Hybrid Container Format (TOML Top + TTF-Style Binary Bottom)

Every `.Space` and workspace document shares one universal file framing:

```text
+-----------------------------------------------------------------------------------+
| 1. UTF-8 PASCALCASE TOML HEADER                                                   |
|    ['.DocumentHeader']                                                            |
|    FormatSignature       = "FrontierSpace"                                        |
|    ExtensionTag          = ".ImageSpace"     # Identifies owning workspace type   |
|    SchemaVersion         = 1                                                      |
|    ResolutionIndependent = true                                                   |
|                                                                                   |
|    ['.ColorSpace']                           # Embedded analytic sub-format       |
|    Primaries             = "ACEScg"                                               |
|    WhitePoint            = "D60"                                                  |
|    Transfer              = "Linear"                                               |
|                                                                                   |
|    [['.SectionDirectory']]                   # TTF/SFNT-style binary directory    |
|    SectionTag            = ".Baseline"       # Named binary block                 |
|    ByteOffset            = 0                 # Relative to binary start (64B)     |
|    ByteLength            = 16384                                                  |
|    ChecksumFnv64         = 0x9E3779B97F4A7C15                                     |
|                                                                                   |
|    [['.SectionDirectory']]                                                        |
|    SectionTag            = ".Revision"       # Recorded parametric operations     |
|    ByteOffset            = 16384                                                  |
|    ByteLength            = 65536                                                  |
|    ChecksumFnv64         = 0xC2B2AE3D27D4EB4F                                     |
+-----------------------------------------------------------------------------------+
| 2. EXPLICIT BOUNDARY + ZERO-PADDING TO 64-BYTE ALIGNMENT                          |
|    "\n#===FRONTIER-BINARY-SECTIONS===\n\0" + zero bytes until (offset % 64 == 0)  |
+-----------------------------------------------------------------------------------+
| 3. BINARY SECTION '.Baseline'   (64-byte aligned, random-access slice)            |
+-----------------------------------------------------------------------------------+
| 4. BINARY SECTION '.Payload..'  (Embedded domain binary streams / tiles / curves) |
+-----------------------------------------------------------------------------------+
| 5. BINARY SECTION '.Revision'   (Contiguous parametric operation entries)         |
+-----------------------------------------------------------------------------------+
```

### Why This Format Works Across Every Workspace
- **Readable Metadata, Binary Throughput:** Text tools and version control can read the header and analytic settings directly, while large binary payloads load at raw memory speed with zero text-parsing overhead.
- **64-Byte Alignment:** Every binary section starts on a 64-byte boundary so CPU SIMD instructions and GPU staging buffers can read slices directly without unaligned copies.
- **Integrity Verification:** Each binary section carries a 64-bit checksum in the directory so truncated or corrupted sections are detected immediately on load.

---

## 2. `.Space` Extensions & Embedded Sub-Formats

A document on disk has **one outer extension** that tells Frontier which workspace opens it, and **embedded sub-format sections** inside the container:

| Workspace Focus | Outer File Extension | Embedded Sections Inside the Same File | What Gets Reconstructed at Any Resolution |
| :--- | :--- | :--- | :--- |
| **Project Configuration** | **`.projectspace`** | `.visualappearance` · `.FontSpace` · `.workspacelayout` · `.inputsettings` · `.notificationsettings` | Theme colors, typography, layout splits, and input chords at any display DPI |
| **Font Repository** | **`.FontSpace`** | `.TypefaceGroup` · `.WeightList` · `.Metrics` | Scalable vector typefaces across arbitrary pixel sizes |
| **Image Workspace** | **`.ImageSpace`** | `.ColorSpace` · `.Exposure` · `.Tone` · `.HDRI` · `.LayerStack` · `.Tiles` · `.Revision` | Color-managed layers, exposure/tone curves, and non-destructive edits at any output dimensions |
| **UV Workspace** | **`.UVSpace`** | `.UDIM` · `.UVChart` · `.Revision` | Continuous `[0, 1]` / UDIM island coordinates scaled to any target texel density |
| **Material Workspace** | **`.Material`** *(or `.MaterialSpace`)* | `.ColorSpace` · `.BSDF` · `.Microfacet` · `.Subsurface` · `.ThinFilm` · `.Clearcoat` · `.Albedo` · *(embedded `.ImageSpace`)* · `.Revision` | Analytic shading closures and procedural channels evaluated at `256²` preview up to `8192²` bake |
| **Paint Workspace** | **`.Pigment`** *(or `.PigmentSpace`)* | `.Stroke` · `.StrokeHistory` · `.Brush` · `.Bristle` · `.Dab` · `.PigmentRamp` · `.PaintLayer` · `.Stylus` · `.Revision` | Parametric brush splines, pressure/tilt curves, and deterministic PRNG seeds replayed at any canvas resolution |
| **Vector Workspace** | **`.Vector`** *(or `.VectorSpace`)* | `.Curve` · `.Stencil` · `.LayerStack` · `.ColorSpace` · `.Revision` | Exact Bézier/polynomial contours and boolean shapes rasterized at any zoom or DPI |

*(Note: `.GeometrySpace` is reserved as an extension identifier only and will be detailed later when we work on Geometry.)*

---

## 3. Document Revision & Lifecycle Memory Management

Both individual documents and the global undo coordinator participate in Frontier's deterministic lifecycle (`Initialize → Tick → Suspend/Resume → Finalize`):

```text
Application Undo Budget (750 MiB Default — Global Across All Open Tabs)
 ├── Open Document Tab A (.Pigment)     [Document Cap: <= 215 MiB]  ──►  Seq #101, #102, #105
 ├── Open Document Tab B (.ImageSpace)  [Document Cap: <= 215 MiB]  ──►  Seq #103, #104
 └── Open Document Tab C (.UVSpace)     [Document Cap: <= 215 MiB]  ──►  Seq #106, #107 (Active Tip)
```

### Key Revision Guarantees

| Requirement | Mechanism |
| :--- | :--- |
| **Per-Document Limit (`215 MiB` default)** | Each open document caps its parametric operation buffer at `215 MiB` (`225,443,840 B`). Because operations store compact math/parameters (`~48 B – 2 KiB`) rather than pixel buffers, `215 MiB` holds `100,000+` operations. |
| **Global Application Limit (`750 MiB` default)** | A central lifecycle-registered revision coordinator tracks the combined undo/redo memory of all open documents and enforces the `750 MiB` (`786,432,000 B`) ceiling. |
| **Per-Tab & Global Undo/Redo** | Every recorded operation receives both a **local document index** (for tab-local Undo/Redo) and a **monotonic global sequence stamp** (for application-wide Undo/Redo across tabs). |
| **Overflow Compaction (`Redo Purge → Prefix Fold`)** | When adding an operation would exceed `215 MiB` (document) or `750 MiB` (global): ① inactive redo branches are purged first (starting with least-recently-used documents for global pressure); ② if more room is still needed, the oldest prefix operations (`#1 .. #K`) are applied into the **parametric baseline**, freeing operation memory without losing resolution independence. |
| **Arbitrary-Resolution Reconstruction** | Replaying `Parametric Baseline + Operations[1 .. Cursor]` against a requested target (`Width`, `Height`, `DPI`, `Tolerance`) rebuilds the exact document at preview, authoring, or high-resolution export scale. |

---

## 4. Decisions to Confirm Before Implementation

*(Legend — Consistency / Clarity / Portability: 🔴 = highest/best, 🚩 = moderate, ✔️ = lowest)*

### Decision 1 — Naming of Non-Spatial Global Extensions

| Rank | Choice | Extension Set | Consistency | Clarity | Notes |
| :--- | :--- | :--- | :---: | :---: | :--- |
| 🏆 🥇 | **Option 1A — Natural Domain Mix** | `.projectspace` · `.FontSpace` · `.ImageSpace` · `.UVSpace` · `.GeometrySpace` + `.Material` · `.Pigment` · `.Vector` · `.Sketch` · `.Assembly` | 🚩 | 🔴 | Uses `*Space` for coordinate/image/font/project spaces and concise studio nouns for physical/artistic documents. |
| 🥈 | **Option 1B — Uniform `*Space` Suffix** | `.ProjectSpace` · `.FontSpace` · `.ImageSpace` · `.UVSpace` · `.GeometrySpace` · `.MaterialSpace` · `.PigmentSpace` · `.VectorSpace` | 🔴 | 🚩 | Every top-level Frontier container on disk ends in `Space`. |

---

### Decision 2 — Extension Casing on Disk & Load

| Rank | Choice | Behavior | Cross-Platform Safety |
| :--- | :--- | :--- | :---: |
| 🏆 🥇 | **Option 2A — Case-Insensitive Load, Canonical PascalCase Write** | Accepts both `.projectspace` and `.ProjectSpace` when opening files; writes canonical casing (`.ImageSpace`, `.UVSpace`, `.FontSpace`, `.projectspace`) so Windows, Linux, and Android never break on casing differences. | 🔴 |
| 🥈 | **Option 2B — Strict Case-Sensitive Extension Check** | Rejects a file if uppercase/lowercase letters in the extension differ on disk. | ✔️ |
