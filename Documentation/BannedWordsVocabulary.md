# Frontier Banned Words & Naming Vocabulary

> **Enforcement Scope**: Blacklisted **everywhere** — C++ and shader identifiers (types, structs, classes, functions, parameters, local/member variables, namespaces, macros, enums), file names, folder names, and comment concepts.
>
> **Core Principle**: Banned words are **semantic voids that hide what code actually does**. This blacklist is **NOT exhaustive** — apply the `§0` naming gates to every identifier even when a candidate word does not appear below.

Related governance documents: [FolderStructure.md](FolderStructure.md) · [../README.md](../README.md)

---

## 1. Categorized Banned Vocabulary

### 1.1 Primary Blacklist

- `action`
- `affinity`
- `arena`
- `badge`
- `binding`
- `bloom`
- `card`
- `carousel`
- `classification`
- `concurrency` / `concurrence` / `concurrent`
- `context`
- `continuum`
- `contract`
- `dev`
- `element`
- `emitter`
- `evaluator`
- `extension` *(strictly restricted: permitted **only** for designated subsystem lifecycle coordinators)*
- `fence`
- `floating`
- `flow`
- `frame`
- `glyph`
- `graph`
- `hook`
- `hub`
- `inscription`
- `item`
- `listener`
- `map`
- `member`
- `menu`
- `mesh`
- `mutation`
- `nexus`
- `occupant`
- `palette`
- `pass`
- `pill`
- `pipeline`
- `pool`
- `popup` / `popups`
- `probe`
- `reservoir`
- `stage`
- `table`

### 1.2 Thematic & Structural Trope Categories

| Category | Banned Terms | Notes & Guidance |
| :--- | :--- | :--- |
| **Sci-fi / Power-trip** | `Apex`, `Sentinel`, `Vanguard`, `Oracle`, `Titan`, `Goliath` | Grandiose metaphors obscure concrete mechanical behavior. |
| **Center-of-universe** | `Nexus`, `Hub`, `Maestro`, `Core`, `Matrix`, `Catalyst`, `Conductor` | Encourages god-objects and vague central bottlenecks. |
| **Pseudo-space / Mythology** | `Vortex`, `Aether`, `Zenith`, `Nova`, `Horizon`, `Genesis`, `Odyssey`, `Helios`, `Orion`, `Kronos` | Mythological and cosmic labels convey zero architectural meaning. |
| **Generic-programmer / AI tropes** | `Manager`, `Handler`, `Dispatcher`, `Processor`, `Controller`, `Service`, `Utility`, `Helper`, `Representation`, `Traversal`\*, `Kernel`, `Graph`, `Record`†, `Frame`, `Node`, `Module`, `Data`, `Info`, `Object`, `Stuff`, `Thing`, `Item`, `Common`, `Base` | Default placeholder nouns that hide ownership, memory layout, and exact transformation. |
| **Human job titles** | `Manager`, `Worker`, `Provider`, `Dispatcher`, `Conductor`, `Listener`, `Observer` | Barred unless a strict, canonical named design pattern is genuinely in use. |
| **Kinship / Family** *(whole category barred)* | `Parent`, `Child`, `Children`, `Sibling`, `Ancestor`, `Descendant`, `lineage`, `family`, `kin`, `Orphan` | **Entire category barred.** A tree link is a technical memory link (`up-link` / `down-link region` / `same-tier link`); name it via `§4`. |
| **State-change verbs** | `Commit`, `Compose` *(verb AND noun)* | `Commit` → `Finalize` / `Enforce` / `Align` / `Resolve`.<br>`Compose` (verb and noun) → `Stack` / `Order`. |
| **Banned variable words** | `flag`, `state`, `value` | Describe what the variable actually holds instead of its storage meta-category. |

*\* `Traversal` is exempt **only** at `Topology/Selection/SelectionTraversal.{h,cpp}` (explicit user direction).*
*† Bare `Record` is banned; composite `RecordEntry`, `RecordStore`, and `RecordToken` (`§4`) are approved.*

---

## 2. Approved Replacements & Naming Directives

When replacing or avoiding a banned concept, name the **concrete data layout, memory link, or hardware mechanism**:

| Banned Term / Concept | Approved Frontier Mechanism / Replacement |
| :--- | :--- |
| `Commit` | `Finalize`, `Enforce`, `Align`, or `Resolve` |
| `Compose` / `Composite` (verb or noun) | `Stack` or `Order` (e.g., `RadiometricToneStack`) |
| `Parent` / `Child` / `Sibling` / `Orphan` / kinship | Technical memory link terminology (`up-link`, `down-link region`, `same-tier link`; named via `§4`) |
| `flag`, `state`, `value` | Concrete domain name describing the exact condition, quantity, or payload held |
| `harness`, `Trial`, `Trials` | `…Validation` (test scaffolds are validations, never harnesses or courtroom metaphors) |
| `backend` (raw-ingestion / hardware layer) | `…Substrate` (e.g., `RenderDeviceSubstrate`, `AudioDeviceSubstrate`) |
| `stage`, `Pass`, `Emission`, `Emit` | Mechanism-specific GPU render-unit suffix (`§2`, render-unit table) |
| `Inscription` (composite-onto-target GPU unit) | `…Rasterization` (still rasterizes ribbon / glyph / decal geometry into the target) |
| `bloom` | Physical optical glare / luminance scatter terminology (`VeilingGlareFilter`, `ApertureGlareFilter`, `LuminanceGlareDownsample`) |
| `Hub` | Designated `…Extension` when acting as a domain lifecycle coordinator (e.g., `AnnotationExtension`), or concrete router/intake mechanism |
| Bare `Record` | Composite `RecordEntry`, `RecordStore`, or `RecordToken` (`§4`), or concrete `Register` / `Layout` |

---

## 3. Restricted Terms & Explicit Exemptions

Only the following narrow, explicitly authorized exceptions exist:

1. **`Extension` (Strictly Restricted)**:
   Permitted **exclusively** for designated lifecycle coordinators (`RenderExtension`, `PhysicsExtension`, `InputExtension`, `AnnotationExtension`, `WorkspaceExtension`, `NetworkingExtension`, `AudioExtension`). Barred for general plugins, helpers, utilities, or feature add-ons.
2. **`Traversal` (Single-File Exemption)**:
   Exempt **only** at `Topology/Selection/SelectionTraversal.{h,cpp}` per explicit user direction. Barred in all other files, types, functions, and comments.
3. **`Record` (Composite-Only Exemption)**:
   Bare `Record` is banned. Only the explicit composite forms `RecordEntry`, `RecordStore`, and `RecordToken` (`§4`) are approved.
4. **Named Design Patterns (`Observer`, etc.)**:
   Human job-title nouns remain barred unless a strict, textbook named design pattern is genuinely implemented.

---

## 4. Chronological Rulings & Amendments

- **2026-06-20**: Barred `system`, `compose`, `mesh`, `harness`, `shell`, `popover`, `Tween`, `Kind`, `proof`, `glide`, `binding`, `Role`, `Common`.
- **2026-07-15**: Barred `stage` (render jargon).
- **2026-07-25**: Barred `Hierarchy`, `Entity`, `Element`, `Subsystem`, `Designation` (`Designation` was previously reuse-authorized, now barred); `system` re-affirmed barred as any noun; `Trial` / `Trials` barred (was the `harness` proposal — courtroom metaphor) → a test scaffold is a `…Validation`.
- **2026-07-25**: Barred `backend` (web "layer beneath" jargon) → raw-ingestion layer = `…Substrate`.
- **2026-07-26**: Barred `Pass` (borrowed from `VkRenderPass`; revokes the old `stage` → `Pass` ruling) and `Emission` / `Emit` (AI-default) → a GPU render unit takes a mechanism suffix (`§2`, render-unit table).
- **2026-08-02**: Barred `Inscription` (was the `§2` approved `Pass` replacement for composite-onto-target units; now barred outright) → a GPU render unit that composites onto an existing target takes `…Rasterization` (it still rasterizes ribbon / glyph / decal geometry into the target). New code never uses `Inscription`; existing `…Inscription` files are deferred renames (see `§2`).
- **2026-11-10**: Barred `bloom` (colloquial post-process jargon → name the physical optical glare / luminance scatter mechanism, e.g. `VeilingGlareFilter`, `LuminanceGlareDownsample`); re-affirmed `Hub` barred across all coordinators (`AnnotationHub` → `AnnotationExtension`).

---

## 5. Consolidated A–Z Quick-Lookup Index

Use this combined index to audit identifiers, filenames, directory names, and comments prior to finalizing changes:

- **A**: `action`, `Aether`, `affinity`, `Ancestor`, `Apex`, `arena`
- **B**: `backend`, `badge`, `Base`, `binding`, `bloom`
- **C**: `card`, `carousel`, `Catalyst`, `Child`, `Children`, `classification`, `Commit`, `Common`, `Compose` / `compose`, `concurrence` / `concurrency` / `concurrent`, `Conductor`, `context`, `continuum`, `contract`, `Controller`, `Core`
- **D**: `Data`, `Descendant`, `Designation`, `dev`, `Dispatcher`
- **E**: `Element` / `element`, `Emission` / `Emit` / `emitter`, `Entity`, `evaluator`, `extension` *(restricted — §3)*
- **F**: `family`, `fence`, `flag`, `floating`, `flow`, `Frame` / `frame`
- **G**: `Genesis`, `glide`, `glyph`, `Goliath`, `Graph` / `graph`
- **H**: `Handler`, `harness`, `Helios`, `Helper`, `Hierarchy`, `hook`, `Horizon`, `Hub` / `hub`
- **I**: `Info`, `Inscription` / `inscription`, `Item` / `item`
- **K**: `Kernel`, `kin`, `Kind`, `Kronos`
- **L**: `lineage`, `Listener` / `listener`
- **M**: `Maestro`, `Manager`, `map`, `Matrix`, `member`, `menu`, `Mesh` / `mesh`, `Module`, `mutation`
- **N**: `Nexus` / `nexus`, `Node`, `Nova`
- **O**: `Object`, `Observer` *(restricted — §3)*, `occupant`, `Odyssey`, `Oracle`, `Orion`, `Orphan`
- **P**: `palette`, `Parent`, `Pass` / `pass`, `pill`, `pipeline`, `pool`, `popover`, `popup` / `popups`, `probe`, `Processor`, `proof`, `Provider`
- **R**: `Record` *(bare barred — §3)*, `Representation`, `reservoir`, `Role`
- **S**: `Sentinel`, `Service`, `shell`, `Sibling`, `stage`, `state`, `Stuff`, `Subsystem`, `system`
- **T**: `table`, `Thing`, `Titan`, `Traversal` *(exempt only at `SelectionTraversal` — §3)*, `Trial` / `Trials`, `Tween`
- **U**: `Utility`
- **V**: `value`, `Vanguard`, `Vortex`
- **W**: `Worker`
- **Z**: `Zenith`
