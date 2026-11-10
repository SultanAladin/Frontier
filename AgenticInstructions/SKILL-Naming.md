# SKILL-Naming — Engine Naming Generation (Authoritative, Strict)

Never recall names from memory. Construct every name strictly from physical or mathematical mechanism.
Where any older note or exhibit disagrees, this file is the sole authority.

## Generation Procedure

① State the physical mechanism in one plain sentence (memory layout, geometric transform, numerical
process, spatial relation).
② Extract the technical verb and the target noun; fuse them into a compound identifier that describes
the mechanism and reality, not the category.
③ Modules and folders MUST follow the two-word PascalCase formula `<Subject><Role>` using the closed
Role suffix list below and nothing else.

If a candidate is rejected at any gate, restart from step ①.

## Closed Role Suffixes — Final, 19 Entries

| Suffix           | Technical meaning                                                          | Owns memory |
|------------------|----------------------------------------------------------------------------|-------------|
| `Sequence`       | Ordered workflow — strokes, layer order, revisions, poses                  | Yes         |
| `Codec`          | Stream translation — image, vector, audio, binary                          | No          |
| `Exchange`       | Transfer across a system, vendor or hardware edge                          | Yes         |
| `Interchange`    | Standardised protocol or platform-abstracted translation                   | Yes         |
| `Extension`      | A vendor-declared optional capability, negotiated and then held            | Yes         |
| `Solver`         | Equation systems — linear, constraint, unwrap, boolean                     | No          |
| `Integrator`     | Quadrature and accumulation — time stepping, coverage, moments             | No          |
| `Classifier`     | Geometric and statistical predicates                                       | No          |
| `Projection`     | Transform between spaces — geometric, spectral, colour                     | No          |
| `Specification`  | Parametric schema                                                          | Yes         |
| `Structure`      | Topology — B-rep, polyhedral, skeletal, sparse                             | Yes         |
| `Space`          | Spatial subdivision — tile, voxel, byte extent, recursive subdivision      | Yes         |
| `Index`          | Slot ledger — generational tokens, interning, spatial hashing              | Yes         |
| `Metrics`        | Diagnostic measurement — log sinks, markers, timing                        | Yes         |
| `Scheduler`      | Task and priority ordering                                                 | Yes         |
| `Queue`          | Task execution — thread pools, command and event queues                    | Yes         |
| `Panel`          | UI presentation — workspace, control, overlay                              | No          |
| `Host`           | Executable target entry point                                              | Yes         |
| `Depot`          | A store of derived, evictable, reconstructible artefacts keyed by content  | Yes         |

`Extension` is narrow: it names a queried, enabled and owned optional vendor capability
(`DiagnosticExtension` holds `VK_EXT_debug_utils`). It is not a general spelling for "an edge" — use
`Exchange` for that.

## 🔴 Retired Suffixes — Never Use as a Role

`Boundary`, `Region`, and `Tree` are retired from the suffix list. `Boundary` named every category of edge
at once — OS, vendor, hardware, protocol — which made it the least informative suffix; it is replaced by
`Exchange` / `Interchange` / `Extension`, chosen by *what crosses*.

| Retired                        | Replacement                                                       |
|--------------------------------|-------------------------------------------------------------------|
| `Region` (allocation extent)   | `Space` — a bounded storage extent that is sliced and reclaimed   |
| `Tree` (topology)              | `Structure` — for example `BoundingStructure`                     |
| `Tree` (recursive subdivision) | `Space` — for example `OctantSpace`, `AxisSpace`                  |
| `Boundary` (any edge)          | `Exchange` / `Interchange` / `Extension` per the table above      |

## Word Substitutions — Fixed Once, Mechanically

| Retired         | Replacement                  | Mechanism the replacement states                         |
|-----------------|------------------------------|----------------------------------------------------------|
| `Vacancy…`      | `Occupancy…`                 | States the occupied set; the free set is its complement  |
| `Binding…`      | `Descriptor…`                | Names the Vulkan object actually held                    |
| `Submission…`   | `Command…`                   | Names what is recorded, not the verb                     |
| `Cadence…`      | `Cycle…` / `Slot…`           | Completion-ordered reuse, or one reusable position       |
| `Footprint…`    | `Extent…` / `Slot…`          | Measured extent, or the slot it occupies                 |
| `…Contract`     | `…Guarantee`                 | What the declaration promises, in the caller's terms     |
| `Outcome<T>`    | `Deliver<T>`                 | Content delivered across one fallible call, or a refusal |
| `Prelude`       | `ToolchainInterchange`       | One source translated for host and shader toolchains     |
| `Draft…`        | `SketchTool…` / `…Placement` | A tool taking anchors, and how far it has got            |
| `Paint…`        | `Texture…` / `Texturing…`    | Texels written into a layer, and the discipline          |

## Recording Cycle Vocabulary

`Rotation` remains reserved for actual angular or quaternion mechanisms.

| Spelling              | Mechanism                                                    |
|-----------------------|--------------------------------------------------------------|
| `CycleScheduler`      | Orders completion-gated reuse of recording slots             |
| `CycleSlot`           | Fence and semaphores ordering one reusable position          |
| `RecordingSlotCount`  | Number of reusable recording positions                       |
| `SlotOrdinal`         | One position within that count                               |
| `RecordingOrdinal`    | Monotonic recording number counted from bring-up             |
| `PerSlot`             | Content replicated once for every reusable position          |
| `RecordedSlot`        | Measurements retained for one reusable position              |
| `TimestampsPerSlot`   | Timestamp count claimed by each reusable position            |
| `CompletedRecordings` | Number of recordings advanced since bring-up                 |

## Fallible Return Vocabulary

| Spelling              | Standing | Mechanism                                                                           |
|-----------------------|----------|-------------------------------------------------------------------------------------|
| `Deliver<Content>`    | Approved | One fallible call either delivers `Content` or carries a `Refusal`.                 |
| `Response<Content>`   | Reserved | Content answering a previously declared request.                                    |
| `Status<Content>`     | Reserved | A diagnostic reading observed at one declared instant.                              |
| `Expected<Content>`   | Reserved | Resource content anticipated from an asynchronous acquisition.                      |
| `Resolution<Content>` | Reserved | Content produced by a declared resolution procedure where `Resolve` is the operation.|

## Banned Words

- **OO / AI tropes:** Manager, Handler, Processor, Controller, Service, Utility, Helper, Node, Frame,
  Module, Core, System, Backend, Pass, Stage, Harness, Shell, Entity, Element, Subsystem, Hierarchy,
  Data, Info (use Information), Object, Item, Thing, Kind, Base, flag, state, value.
- **Kinship / family (whole category barred):** Parent, Child, Sibling, Sister, Neighbor, Ancestor,
  Descendant, Orphan.
- **Structural words:** Table, Map, Block, Digest, Model, Modelling, Handle, Store, Bridge, Atlas,
  Substrate, Fabric, Cache, Evaluator, Evaluate, Journal, Resolver, Mesh, Pool, Registry, Catalog,
  Repository, Directory (except `ContentDirectory`), Vault, Arena, Inventory, Ledger, Plan, Filter,
  Grid, Array, Dispatcher, Memory, Buffer, Pipeline, Flow, Composite, Compose, Composition, Allocation,
  Tier, Nesting, Stratum, Mip, Messenger, Probe, Blend, History, Bake, Stamp, Contract, Outcome,
  Prelude, Preamble, Epilogue, Header, Common, Global, Misc, Cadence, Binding, Submission, Footprint,
  Region, Tree, Vacancy, Ordinates, Draft, Draught, Paint.

## Mathematical Vocabulary Exemption

Permitted when naming a **defined mathematical or physical object** (never as a vague software category):
`Graph`, `Predicate`, `Quadrature`, `Interpolant`, `Field`, `Kernel` (convolution/integration kernel only),
`Partition`, `Region` (never as a module Role), `Space`, `Tree` (never as a module Role), `Simplex`, `Intrinsic`.

## Identifier & Formatting Rules

- ALL internal identifiers (folders, files, classes, variables, functions) use **PascalCase**.
- Zero shorthand — full words only: `Parameter` not `Param`, `Specification` not `Spec`, `Information` not `Info`.
- Zero single-letter names (except sanctioned Unicode mathematical symbols in `SKILL-Formatting.md`).
- Zero `k` constant prefixes — use `MinimumBoundary`, `ToleranceThreshold`.
- Variables never mirror their type name (`ActiveTopologyCluster`, never `TopologyCluster TopologyCluster`).
- Booleans never start with `is` / `has` / `can` — state the property (`ClosedEnabled`, `VisibilityEnabled`).
- Functions use PascalCase domain-specific verbs (`Solve`, `Integrate`, `Traverse`, `Construct`, `Resolve`,
  `Reclaim`, `Linearize`, `Classify`, `Project`). CRUD and vague verbs are banned (`Get`, `Set`, `Process`,
  `Handle`, `Manage`, `Commit`, `Compose`, `Draw`, `Update`, `Evaluate`).
- Direct Vulkan / SDK calls mirror vendor spelling verbatim (`VkDeviceCreateInfo`, `ImDrawData`).
