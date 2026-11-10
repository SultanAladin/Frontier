# Engine Naming — Master Skill Document

The **single authoritative naming construction procedure** for **Frontier**: C++20 types, templates,
concepts, functions, variables, `.slang` shaders, UI controls, and file/folder names.
**Zero-Tolerance:** eradicate generic programming placeholders, AI clichés, and anthropomorphic
(kinship / job-title) terms. Names must be **concrete, technical, and domain-specific** — grounded in
contiguous memory, geometry, topology, radiometry, acoustics, and hardware execution.
All identifiers use **PascalCase** (with the sole mathematical Greek/Unicode scalar exception in
`SKILL-Formatting.md` §4).

🔴 **Companion Blacklist:** Always audit candidate names against `Documentation/BannedWordsVocabulary.md`
before writing code or documentation.

---

## 0. How to Construct a Name (Run EVERY Time Before Proposing)

🔴 A blacklist tells you what a name must **NOT** be; this procedure builds what it **IS**. Never
reach for a familiar generic word under pressure — construct from the physical/memory mechanism first,
then run the 6-gate rejection check.

1. **State the mechanism in one plain sentence** — what it physically does in memory, math, or GPU silicon.
   *"Recycles preallocated indexed instance slots." "Submits batched indirect draw packets."*
2. **Extract the technical verb + substrate noun and fuse them** — recycles + indexed slots →
   `IndexedSlotRecycler`; submits + draw batches → `DrawBatchSubmitter`.
3. **Draw nouns from concrete domain registers** (never OO / web / org-chart / kinship):

   | Register          | Approved Structural Nouns                                                   |
   | ----------------- | --------------------------------------------------------------------------- |
   | Contiguous memory | `Slot` · `Vacancy` · `Region` · `Store` · `Reclamation` · `Atlas` · `Slice` |
   | Topology/geometry | `Complex` · `Loop` · `Patch` · `Contour` · `Cycle` · `Boundary` · `Field`   |
   | Math / analysis   | `Linearizer` · `Projection` · `Predicate` · `Interpolant` · `Solver`        |
   | Connectivity      | `Adjacency` · `Enclosure` · `Lateral` · `Tier` · `Nesting`                  |
   | Hardware / GPU    | `Substrate` · `Rasterization` · `Submission` · `Sequence` · `Register`      |

4. **6-Gate Reject Test** — INVALID (restart at Step 1) if the candidate hits ANY gate:
   - ① Appears in `Documentation/BannedWordsVocabulary.md` or is a direct synonym (`bloom`, `hub`, `arena`, `pool`, `reservoir`, `stage`, `pass`, `mesh`, `context`, `binding`, …).
   - ② Sci-fi / center-of-universe / mythology (`Apex`, `Nexus`, `Core`, `Matrix`, `Vortex`, `Horizon`, `Sentinel`, …).
   - ③ Framework / web jargon (`SceneGraph`, `ECS`, `Backend`, `Actor`, `Shell`, `Popover`, `Widget`, `Kit`).
   - ④ Generic OO / AI trope (`Manager`, `Service`, `Controller`, `Handler`, `Dispatcher`, `Processor`, `Helper`, `Utility`).
   - ⑤ Human job title or kinship term (`Worker`, `Provider`, `Listener` / `Parent`, `Child`, `Sibling`, `Orphan`).
   - ⑥ Empty placeholder (`Data`, `Info`, `Object`, `Item`, `Element`, `Thing`, `Common`, `Base`, `flag`, `state`, `value`).
5. **Name the REALITY, not the CATEGORY.** If the name could apply to any enterprise web app, discard it.

---

## 1. Banned Vocabulary Cross-Reference

`Documentation/BannedWordsVocabulary.md` is the canonical, maintained blacklist enforced across all
identifiers, `.h`/`.cpp`/`.slang` files, folders, and comments. Key highlights:

- **Primary &Dated Bans:** `action`, `affinity`, `arena`, `backend` (→ `…Substrate`), `badge`, `Base`,
  `binding`, `bloom` (→ `VeilingGlareFilter` / `LuminanceGlareDownsample`), `Cache` (→ `Atlas`/`Store`/`Resident`/`Lut`),
  `card`, `carousel`, `classification`, `Commit` (→ `Finalize`/`Enforce`/`Align`/`Resolve`), `Common`,
  `Compose`/`Composite` (→ `Stack`/`Order`), `concurrency`/`concurrent`, `context`, `continuum`, `contract`,
  `Core`, `Data`, `Designation` (→ `…Token`), `dev`, `Element`, `Emission`/`Emit`/`emitter`, `Entity`
  (→ `RecordEntry`), `evaluator`, `fence`, `flag`, `floating`, `flow`, `Frame`, `glyph`, `Graph`,
  `harness`/`Trial` (→ `…Validation`), `Hierarchy` (→ `SceneDirectory`/`DepthTier`), `hook`, `Hub`
  (→ designated `…Extension` or router), `Info`, `Inscription` (→ `…Rasterization`), `Item`, `Kernel`,
  `Kind` (→ `…Category`), `Listener`, `map`, `Matrix`, `member`, `menu`, `mesh` (→ `PolygonComplex`/`PolygonSurface`),
  `Model`, `Module`, `mutation`, `Nexus`, `Node`, `Object`, `occupant`, `palette`, `Pass`, `pill`,
  `pipeline`, `pool`, `popup`/`popover`, `probe`, `Record` *(bare barred; `RecordEntry`/`RecordStore`/`RecordToken` approved)*,
  `reservoir`, `Role`, `shell` (→ `WorkspaceHost`), `stage`, `state`, `Subsystem`/`system`, `table`,
  `Traversal` *(exempt only at `Topology/Selection/SelectionTraversal.{h,cpp}`)*, `Tween`, `Utility`,
  `value`, `Widget`.
- **Restricted Lifecycle Suffix (`Extension`):** Permitted **solely** on designated domain lifecycle
  coordinators: `RenderExtension`, `PhysicsExtension`, `InputExtension`, `AnnotationExtension`,
  `WorkspaceExtension`, `RevisionExtension`, `NetworkingExtension`, `AudioExtension`.

---

## 2. GPU Render-Unit & Hardware Suffixes

Choose suffixes strictly by hardware mechanism:

| Suffix            | Mechanism                                                                                          | Examples                                             |
| ----------------- | -------------------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| `…Substrate`      | Raw OS / silicon / hardware ingestion boundary (replaces `backend`)                                | `RenderDeviceSubstrate` · `AudioDeviceSubstrate`     |
| `…Rasterization`  | GPU unit rasterizing geometry, grids, gizmos, decals, or vector overlays via `.slang` shaders      | `GizmoRasterization` · `GridOverlayRasterization`    |
| `…Submission`     | Records and submits batched direct or indirect GPU workloads                                       | `ShadowBatchSubmission` · `DrawBatchSubmitter`       |
| `…Sequence`       | Ordered multi-step GPU execution chain                                                             | `PostProcessSequence`                                |

🔴 **Viewport Overlays Rule:** Grids, gizmos, debug wireframes, and callouts are **always** GPU
`.slang` `…Rasterization` units — never CPU pixel loops and never ImGui `ImDrawList` primitives.

---

## 3. Tree & Connectivity Vocabulary (Zero Kinship Terms)

| Technical Role            | Approved Identifier                     | Barred                          |
| ------------------------- | --------------------------------------- | ------------------------------- |
| Owning spatial/scene tree | `SceneDirectory`                        | `SceneGraph`, `Hierarchy`       |
| Single entry              | `RecordEntry`                           | `Node`, `Element`, `Entity`     |
| Contiguous entry storage  | `RecordStore`                           | `EntityPool`, `ObjectReservoir` |
| Stale-detecting handle    | `RecordToken` (`TokenIssuer`)           | `Handle`, `Designation`         |
| Enclosing up-link         | `EnclosureToken`                        | `Parent`, `Ancestor`            |
| Nested down-link span     | `SubtreeRegion` / `NestedRegionToken`   | `Children`, `Descendants`       |
| Same-tier link            | `LateralSuccessorToken`                 | `Sibling`                       |
| Tree depth                | `DepthTier` / `IndentDepth`             | `HierarchyLevel`                |
| Re-linking operation      | `Relocate` / `ConnectivityTranslation`  | `Reparent`                      |

---

## 4. Function & Concept Verb Vocabulary

All functions and C++20 `concept` predicates use **PascalCase**.
❌ **Barred verbs:** `Get`, `Set`, `Handle`, `Manage`, `Process`, `Compose`, `Update`, `Do`, `Perform`,
`Watch`, `Listen`, `Spawn`, `Kill`, `Commit`, `Emit`.

| Action Domain             | Approved Frontier Verbs                                                                                      |
| ------------------------- | ------------------------------------------------------------------------------------------------------------ |
| Retrieval / inspection    | `Retrieve` · `Resolve` · `Extract` · `Decode` · `Interpret` · `Enumerate` · `Inspect` · `Query` · `Derive`   |
| Lifecycle / configuration | `Initialize` · `Configure` · `Reconfigure` · `Register` · `Activate` · `Suspend` · `Resume` · `Finalize`     |
| Allocation / reclamation  | `Issue` · `Authenticate` · `Acquire` · `Reclaim` · `Reset` · `Invalidate`                                    |
| Geometry / math           | `Construct` · `Assemble` · `Partition` · `Subdivide` · `Collapse` · `Solve` · `Evaluate` · `Project` · `Align` |
| Routing / encoding        | `Route` · `Intake` · `Submit` · `Broadcast` · `Compile` · `Encode` · `Serialize`                             |
| Verification              | `Validate` · `Verify` · `Assert` · `Enforce` · `Constrain`                                                   |

Lifecycle sequence is always **Initialize → Tick / Submit → Finalize**.

---

## 5. Variable & Boolean Naming

- **PascalCase everywhere:** local variables, parameters, struct fields, template type/non-type parameters, and `constexpr` constants.
- **No shorthand or Hungarian prefixes:** never `kMin`, `m_count`, `s_inst`, `tmp`, `val`, `idx`, `ptr`. Write `MinimumExtent`, `RegisteredSlotCount`, `ProjectedCoordinate`.
- **No type-name mirroring:** ❌ `WorkspaceToken workspaceToken` → ✔️ `WorkspaceToken ActiveDocumentToken`.
- **Booleans:** avoid `is`/`has`/`can` prefixes; describe the condition directly (`VisibilityEnabled`, `CaptureActive`, `EditorIncluded`, `RepeatSuppressed`).
- **Modular Reuse:** design GPU and UI structures once to serve arbitrary instance counts via per-call parameters/push-constants rather than hardcoding single-purpose types.
