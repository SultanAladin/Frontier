# PLAN — Space Formats, Approved Registry & Derivation DAG

**Status: proposed.** Authored content is TOML; every derived artifact is keyed, verified and loaded
from disk instead of being recomputed. No code is implemented by this document.

## 0 · Precedence

Four older documents parent this one. Their **rules** stand; their **names** are subordinate to the
approved registry in §2. One of them was implemented in a way that broke its own rules (§1).

| Parent | Contribution | Standing |
| --- | --- | --- |
| `DataDrivenSpaceContentFormattingPlan.md` | `*Space` naming, `Content/` tree, wire format | Kept; names per §2 |
| `DocumentFormatsInstancePlan.md` | Container, codec/interchange/exchange split, budgets | Fully retained (§4) |
| `WorldAuthoringAssetDagPlan.md` | World authority, extensions, dependency DAG | Retained; §4.3 closes it |
| `Ongoing/SpaceFormats.md` | Encoding B, `.projectspace`, `.arc` journal | Fully retained (§4) |

🔴 **Precedence rule.** The user's approved extension list wins on naming. Where it is silent, the older
plans win on rules. Where names overlap, §2.5 rules explicitly — overlap is never equivalence.

Evidence cites this checkout (`Internal/…`, `Executables/…`) and, where the newer tree renamed files,
`Engine/…` at `arena/5922d41b-frontier`. The §1 defect exists in both.

---

## 1 · The defect this plan removes

One root cause: **the runtime decides what a world is at compile time, and re-derives state it could
load.** Naming the extensions changed nothing, because nothing in the load path asks "did this come
from a document?".

### 1.1 Hardcoded world selection

| Site | Today → required |
| --- | --- |
| H1 · `RenderExtension.h:186` | `SceneChoice = MaterialRings` enum default → `.engineconfig` names the world |
| H2 · `RenderExtension.cpp:1087-1093` | `switch (SceneChoice)` → `"SuzanneRadial.wsdoc"` → table lookup |
| H3 · `RenderExtension.cpp:1083` | `FRONTIER_SCENE_ASSET_DIR "Assets"` macro → content root from config |
| H4 · `RenderExtension.cpp:1112` | `"CheckerFloor.wsdoc"` special case → an ordinary world reference |
| H5 · `Scene/SuzanneScene.cpp:219,230` | `BuildSuzanneScene` inside `Graphics.lib` → authoring/tool links only |
| H6 · `Scene/SurfacePresetTable.cpp:44` | 14 presets hardcoded in C++ → `.materialspace` then flatten (§6 W7) |
| H7 · `Editor/EditorLifecycle.cpp` | editor opens workspaces with no world → load `.worldspace` or a blank |
| H8 · newer `ShowcaseGalleryScene.cpp:987` | load, then **silent fallback** to a built scene → surfaced failure |

🔴 **H8 is why the last plan did not stick.** A fallback that *works* deletes every symptom that would
have forced the data path to be finished.

> **No silent world fallback.** A missing, corrupt or unsupported world document yields a named error
> with the path and the failing check. A demo world exists only as content a document names.
> `DefaultContentGenerator` writes documents; it is never the runtime's scene source.

### 1.2 Recomputed every launch

| Site | Recomputed → persisted artifact |
| --- | --- |
| R1 · `SkyAtmosphere.cpp:637` from `RenderExtension.cpp:926` | LUT bake → `.skyatmosphere` derived sections |
| R2 · `RenderExtension.cpp:1164,1260` | `VoxelizeSceneOccupancy` → `.distancefield` occupancy |
| R3 · `RenderExtension.cpp:1175` | instance cull records → `.convexhull` bounds section |
| R4 · `WorkspaceDocumentDecoder.cpp:351` | BLAS every load → `.raystructure`, device keyed |
| R5 · `SurfacePresetTable.cpp:44` | preset table as code → `.materialspace` flattened |
| R6 · `SignedDistanceVolume.h:6` | `.rsdf` cache documented, unported → `.distancefield` |
| R7 · `Baking/GpuBakeDispatch.h` | surface bakes per session → `.pixelatlas`, `.irradiance` |

💡 The build system already skips fresh work (`Automation/BuildPlan.ps1`, `ShaderPlan.ps1`); the runtime
does not. §5 lifts the idea but keys on **content digests**, never modification times — a build stamp
may guess, a runtime artifact may not.

---

## 2 · Approved registry (41 names)

**Tier A** = authored source of truth (TOML, editable, Git-keepable). **Tier D** = derived, disposable,
keyed, never authoritative (§5). Ownership is §3; this section is naming and content only.

### 2.1 World, gameplay, lifecycle

| Extension | Tier | Content |
| --- | --- | --- |
| `.worldspace` | A | World root: identity, folder tree, tokens, placements, links |
| `.component` | A | Reusable per-record payload defaults (see E2, §2.5) |
| `.triggerzone` | A | Sensor volumes + enter/stay/exit bindings |
| `.eventbus` | A | Named signal schemas and routing (not a DAG, §4.4) |
| `.variabletable` | A | Typed flags, counters, multipliers, cheat gates |
| `.timerprofile` | A | Cooldowns, countdowns, time-dilation presets |
| `.curve` | A | 1D/2D keys, interpolation, tangents, extrapolation |
| `.timeline` | A | Object/camera/audio/subtitle keyframe sequencing |
| `.constraintset` | A | Joints, ragdoll hinges, pistons, breakable welds |
| `.impactprofile` | A | Material-pair response table (sparks, decals, fracture) |

### 2.2 Rendering, transport, imaging

| Extension | Tier | Content |
| --- | --- | --- |
| `.skyatmosphere` | A+D | Spectral scattering params → aerial/ozone LUTs |
| `.irradiance` | A+D | Diffuse bounce probes → baked lobe atlases |
| `.radiancecache` | A+D | Probe config → validity-bound checkpoint |
| `.distancefield` | A+D | Volume params (sign, units, transform) → voxels |
| `.raystructure` | D | BLAS/TLAS artifact, device and driver keyed |
| `.convexhull` | A+D | Raw geometry → outer polyhedra / broad-phase bounds |
| `.rendergraph` | A | Pass DAG, transient VRAM aliasing, async sync |
| `.poststack` | A | Tone curves, grading LUT refs, bloom, lens distortion |
| `.computekernel` | A+D | Kernel definition → compiled variants |
| `.shadergraph` | A | Node network → SPIR-V/HLSL/GLSL (never executable itself) |
| `.noisegraph` | A | Deterministic fractal noise pipelines + seeds |
| `.lensaperture` | A | Bokeh shape, chromatic fringe, vignette, exposure |
| `.spectralcurve` | A | Wavelength samples, dispersion, thin-film profiles |
| `.pixelatlas` | A+D | Sprite/icon pages or virtual-texture page tables |
| `.iesprofile` | A | Measured photometric distribution, units, orientation |

### 2.3 Simulation, destruction, procedural motion

| Extension | Tier | Content |
| --- | --- | --- |
| `.particlesystem` | A | Master timeline, LOD fallbacks, bounding boxes |
| `.emitternode` | A | Spawn rates, burst curves, lifetime gradients, compute |
| `.vectorfield` | A+D | Recipe → 3D velocity grid (curl noise, vortices) |
| `.ribbontrail` | A | Strip generators + material binds (arcs, contrails) |
| `.swarmbehavior` | A | Boids/flocking matrices, instanced crowd rules |
| `.fluidgrid` | A+D | Eulerian domain params → velocity/pressure/vorticity |
| `.fracturetree` | A | Voronoi hierarchy, connectivity, stress thresholds |
| `.shardcluster` | A+D | Pre-fractured shards, mass/inertia, sleep thresholds |

### 2.4 Interface, typography, configuration

| Extension | Tier | Content |
| --- | --- | --- |
| `.interface` | A | Screen hierarchy, anchors, containers, z-order |
| `.fontspace` | A | Faces, metrics, variants, licensed payload (TTF/OTF = interchange) |
| `.tooltip` | A | Hover cards: descriptions, comparisons, status effects |
| `.soundscape` | A | Ambient beds, music stems, spatial mixer snapshots |
| `.engineconfig` | A | Boot settings, **startup world**, renderer API, tick rate |
| `.graphicssettings` | A | Resolution, V-Sync, FPS cap, texture quality, shadow distance |
| `.userprofile` | A | Preferences, accessibility, achievements, playtime (local) |
| `.editorlayout` | A | Panel arrangement, grid snap, gizmo scale — editor only |

### 2.5 Overlaps and non-equivalences (binding)

| Overlap | Ruling |
| --- | --- |
| `.component` vs banned `Component` | Extension authorized; C++ stays `…Profile` (E2) |
| `.timeline` vs editor variant | One name only; editor tracks flagged inside it |
| `.rendergraph` vs `.RenderSpace` | Pass DAG vs viewport record; neither absorbs |
| `.skyatmosphere` vs `.VolumetricSpace` | Sky never replaces cloud/fog authoring |
| `.iesprofile` vs `.PhotometrySpace` | Measured distribution vs solar/area photometry |
| `.pixelatlas` vs `.TextureSpace` | Atlas = packing + regions; one texture is not |
| `.distancefield` vs `.raystructure` | Authored grid vs device-keyed blob; never one |
| `.component` vs `.ComponentSpace` | Suffix equality is never migration |
| Casing | Write as approved, read case-insensitive (E3); never both |

### 2.6 Retained domains (no approved replacement)

`.GeometrySpace` · `.MaterialSpace` · `.TextureSpace` · `.ImageSpace` · `.ColourSpace` ·
`.ProjectSpace` · `.SurfaceParameterSpace` · `.UVSpace` · `.RenderSpace` · `.PhotometrySpace` ·
`.VolumetricSpace` · `.Acoustic` · `.Pigment` · `.Sketch` · `.Assembly` · `.Motion` · `.Terrain` ·
`.wsdoc` (scene document v1).

---

## 3 · Engine → component ownership

Every registry row names one owning folder. A component with no owner is not admitted.

| Pillar | Owns | Guardrail |
| --- | --- | --- |
| `EngineContext/Scene` | `.worldspace` `.eventbus` `.variabletable` `.triggerzone` | no second scene |
| `EngineContext/Motion` | `.timeline` `.curve` `.timerprofile` | no baked keyframes |
| `EngineContext/Interface` | `.interface` `.tooltip` `.fontspace` `.editorlayout` | editor kinds never ship |
| `EngineContext/Audio` | `.soundscape` | no device handles persisted |
| `EngineContext/SpatialAcceleration` | `.convexhull` + occupancy | no re-voxelizing |
| `Graphics/Atmosphere` | `.skyatmosphere` | no silent re-bake |
| `Graphics/Acceleration` | `.raystructure` | never treated as portable |
| `Graphics/DistanceField`, `Clipmap` | `.distancefield` | sign/units recorded |
| `Graphics/Surfel` | `.radiancecache` `.irradiance` | validity proof required |
| `Graphics/RenderSchedule` | `.rendergraph` | cyclic graph refused |
| `Graphics/Render`, `Visibility` | `.poststack` `.lensaperture` `.pixelatlas`(D) | no presets in shaders |
| `Authoring/**` | geometry/material/surface/paint/sketch | absent from runtime |

---

## 4 · Container and retained rules (TTF/SFNT-style)

The two-tier container is not redesigned; it is the older plan's, restated as the contract this plan
may extend. It is a **TTF-style table directory, not a font file** — `.fontspace` is the only extension
carrying font data.

### 4.1 Anatomy

```text
+-----------------------------------------------------------------------------+
| TIER 1 · UTF-8 TOML MANIFEST                                                |
+-----------------------------------------------------------------------------+
|  ['.DocumentHeader']   ContainerExtension · DomainSubFormat · SchemaVersion |
|  domain tables         authored parameters, units, colour space, references |
|  [['.SectionDirectory']]                                                    |
|      identifier · SubFormatTag · RelativeAlignedOffset                      |
|      ByteLength · ChecksumFnv1a64 · ByteOrder · Provenance                  |
+-----------------------------------------------------------------------------+
| # === FRONTIER_BINARY_BOUNDARY ===   0..63 zero padding to 64-byte boundary |
+-----------------------------------------------------------------------------+
| TIER 2 · 64-BYTE-ALIGNED BINARY SECTIONS                                    |
|  payload streams, LUTs, voxel grids, packed pages                           |
+-----------------------------------------------------------------------------+
```

### 4.2 Retained rules — still binding

| # | Rule | Source |
| --- | --- | --- |
| C1 | TOML manifest + boundary + 64-byte sections, per-section FNV-1a 64 | both format plans |
| C2 | Encoding B: editable TOML authority, binary runtime envelope | `SpaceFormats` §3 |
| C3 | 215 MiB per document, 750 MiB global; redo purge, prefix fold | `DocumentFormats` §3 |
| C4 | Codec / Interchange / Exchange are three roles | `DocumentFormats` §2 |
| C5 | Revision ancestry DAG separate from the build DAG | both plans |
| C6 | FNV detects corruption; identity is SHA-256 | `AssetDagPlan` §6.2 |
| C7 | Bounded parse: offsets, overlap, truncation, path containment | `SpaceFormats` §3 |
| C8 | Unknown forward keys preserved or rewrite refused | `AssetDagPlan` §5 |
| C9 | Migration via versioned decoders, original kept, report written | C7 + §2.5 |
| C10 | `.arc` CAD journals supported; recipes and undo kept | `SpaceFormats` §1 |
| C11 | One `.projectspace` for project + workspace preferences | `SpaceFormats` §3 |
| C12 | No invented byte layout, no wholesale migration | `SpaceFormats` header |
| C13 | toml++ parsed exceptions-off; errors via `parse_result` | `Geometry/Build.bat:62` |
| C14 | No massive derived artifact committed to Git | `AssetDagPlan` §6.5 |

### 4.3 Older open decisions — resolved

| Decision | Ruling |
| --- | --- |
| Canonical casing | Write as approved, read case-insensitive (E3) |
| Empty `Environment` folder after last child | Persists unless explicitly deleted |
| `.audio` as canonical format | Never; `.soundscape` covers it, clips stay interchange |
| HTML reference gate for editor UI | Unchanged; gates UI work, not codecs |
| First legacy conversions | Only `.wsdoc` → `.worldspace` in Phase 1 |

### 4.4 Deliberately not a DAG

Scene parentage, `.eventbus` traffic and time-feedback loops are not forced into the derivation DAG.
The DAG orders *authored input → derived output* only. A signal that loops back is a temporal resource
with an explicit lag edge, not a cycle to reject.

---

## 5 · Derivation DAG — load, verify, else compute once

### 5.1 Units

| Unit | Responsibility |
| --- | --- |
| `SpaceFormatRegistry` | suffix ↔ type ↔ owner ↔ tier; case-insensitive resolve |
| `SpaceContainerDecoder` | tier 1 + 2 read, bounds, checksums, unknown-key preservation |
| `SpaceContainerEncoder` | tier 1 + 2 write, deterministic ordering, atomic publish |
| `DerivationStep` | one node: input keys → one artifact kind + its toolchain |
| `DerivationEdge` | "artifact B consumes A", carrying the subresource scope |
| `DerivationKey` | SHA-256 over the canonical input set (§5.2) |
| `DerivedArtifactStore` | immutable content-addressed files + `DerivationIndex.toml` |
| `ReuseVerdict` | accept / rebuild / refuse, with the reason surfaced |
| `DerivationSchedule` | topological order, cancellation, dedup, matched publish |
| `InvalidationPropagation` | dirty-marking along edges only |
| `ToolchainProvenance` | compiler/algorithm/schema/device versions in the key |
| `GarbageReclamation` | refcount + quota sweep; never deletion by age |

### 5.2 `DerivationKey` contents

| Component | Failure if omitted |
| --- | --- |
| Recursive input digests (SHA-256) | stale artifact silently reused |
| Subresource scope (section, face, UDIM tile) | one texel edit rebuilds the world |
| Canonicalized params (units, colour, precision) | distinct results collide |
| Algorithm + schema version | upgrade serves old bytes |
| Toolchain version (glslc, MSVC, packers) | shader change not rebuilt |
| Output format, alignment, byte order | cross-platform mismatch |
| Device/driver/API — device-keyed kinds only | blob loaded on the wrong GPU |

🔴 **Canonicalization is versioned.** A change in how inputs are serialised changes every key, so the
canonicalization profile number is part of the key; otherwise "the digest moved" is indistinguishable
from "we changed the canonicalizer".

### 5.3 Store layout

```text
<ProjectFolder>/
├── <ProjectName>.projectspace      # C11 — one settings container
├── Content/                        # Tier A, TOML, Git-keepable
│   ├── Worlds/<Name>.worldspace
│   ├── Materials/<Name>.materialspace
│   └── Atmosphere/<Name>.skyatmosphere
└── Derived/                        # Tier D, disposable, Git-ignored (C14)
    ├── DerivationIndex.toml        # key → file, kind, digest, provenance, refcount
    └── Artifacts/<ab>/<key>.space  # two-tier container, tier 2 only
```

`DerivationIndex.toml` is tier-1 TOML, so "why did this rebuild?" is answerable without a debugger.
Every artifact re-verifies its own FNV checksums on load (C1); the index is an accelerator, not proof.

### 5.4 Boot protocol — six steps, no shortcuts

| Step | Action → refusal |
| --- | --- |
| ① | Read `.engineconfig` startup world → missing config is a named error |
| ② | Registry resolves suffix, decode `.worldspace` → bad version/checksum refused |
| ③ | Resolve every reference → unresolved reference refused, path printed |
| ④ | Per step: build `DerivationKey`, query the store → nothing to refuse |
| ⑤ | `ReuseVerdict` verify kind/key/checksum/schema/device → rebuild, never "use anyway" |
| ⑥ | Bind GPU resources by decoding artifacts → bind failure, never silent re-bake |

**Publication rule:** publish only if the source generation still matches the key's generation. A
rebuild racing an edit is discarded, not written.

### 5.5 Reuse matrix

| Kind | Launches / devices — reason |
| --- | --- |
| Authored params (`.curve`, `.materialspace`) | yes / yes — they are the input |
| Transmittance, multi-scatter LUTs | yes / yes — input-determined |
| Occupancy voxels, distance volumes | yes / yes — units recorded |
| `.raystructure` BLAS/TLAS | yes / **no** — device, driver, API keyed |
| `.radiancecache` checkpoints | conditional / no — temporal dependence |
| Surfel residency, GPU handles, playback | never / never — live state |
| Frame-local culling, shading | n/a — correct by definition |

---

## 6 · Removing the hardcoded world — ordered items

| Item | Change → done when |
| --- | --- |
| W1 | `SpaceFormatRegistry` + `.worldspace` decoder → a world round-trips verified |
| W2 | Author the first `.worldspace` from today's `SuzanneScene` + `.wsdoc` pair → identical |
| W3 | `RenderExtension` reads the decoded world; delete enum, switch, macro → §8 grep empty |
| W4 | `.engineconfig` names the world; editor opens it or a blank → deleting it errors |
| W5 | Delete the fallback; generator becomes a fixture writer → failure names the check |
| W6 | Move `BuildSuzanneScene` into authoring/tool links → `Graphics.lib` cannot build |
| W7 | Presets → `.materialspace` with load-time flatten → flatten is the only producer |

---

## 7 · Phases and exit gates

| Phase | Deliverable → gate |
| --- | --- |
| 0 | Registry + container codec → round-trip and fault suite green |
| 1 | `.worldspace` load path, showcase deleted (W1-W6) → startup proved data-driven |
| 2 | Key, index, store, `ReuseVerdict` → one slice with hit/miss evidence |
| 3 | Migrate R2-R4, R7 → second launch reuses all; edits stay local |
| 4 | Tier A codecs in order, each with a consumer → none mocked |
| 5 | Retirement, `Derived/` ignore, Shipping audit → no artifact in Git |

---

## 8 · Acceptance checks

| Check | Method |
| --- | --- |
| No compile-time world | grep `SceneChoice`, `BuildSuzanneScene`, `FRONTIER_SCENE_ASSET_DIR` → empty |
| No silent fallback | rename the startup world → run fails with path and check |
| Reuse proven | second launch logs `accepted` per step, zero re-bakes |
| Transitive invalidation | edit one material → only its dependents rebuild |
| Cycle rejection | cyclic graph refused with the cycle path printed |
| Corruption | flip one section byte → FNV mismatch, rebuild not crash |
| Bounds | truncation, overlap, `../`, unknown version — four errors |
| Casing | `.WorldSpace` and `.worldspace` open alike; write emits lowercase |
| Determinism | same inputs → byte-identical artifact (§5.2) |
| Budgets | 215 MiB / 750 MiB → redo purge, then prefix fold (C3) |
| Shipping | runtime links no editor code and no authoring tool |

---

## 9 · Open decisions (owner: user)

| # | Question | Proposed default |
| --- | --- | --- |
| D1 | `Derived/` beside the project or in `EngineContent/`? | Beside the project |
| D2 | Retire `.wsdoc` or keep it beside `.worldspace`? | Readable one phase, then retire |
| D3 | Do `.curve`/`.timeline` serve editor motion? | Runtime-only |
| D4 | Can `.irradiance` ship inside a project? | Derived-only for now |
| D5 | Does the index travel with a shared project? | No — artifacts, not cache entries |
