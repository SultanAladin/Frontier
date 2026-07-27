# Backlog — deferred migration work

Running pick-up list. Append entries; prune when done. Newest on top.

---

## FlowNetwork — deferred; blocks Terrain/TerrainFieldProgram (2026-07-27)

During the Authoring migration, `Graphics/Terrain/TerrainFieldProgram.{h,cpp}` was found to include
`FlowNetwork.h` — an authored-graph type that in the retired tree is a **UI component**, not a
geometry type: `RetiredProject/Engine/Internal/Interaction/Interface/Components/WorkspaceHost/FlowNetwork.{h,cpp}`
(with a partner `FlowNetworkPanel.{h,cpp}`). Its golden home is **`Interface/WorkspaceHost/`** (pillar 6).

Deferred by user direction — **not ported this session.** Consequence: **Terrain does not fully
compile** until FlowNetwork lands (TerrainFieldProgram's `RetrieveFlowEntry` / `FlowEntry` / `FlowLink`
/ `FlowPort` references are unresolved). Terrain source itself is ported and rehooked; only this one
cross-pillar include is dangling.

| Target | Source | Action when picked up |
|---|---|---|
| `FlowNetwork.{h,cpp}` | retired `…/WorkspaceHost/FlowNetwork.{h,cpp}` | Port to `Internal/Interface/WorkspaceHost/`; resolve its own includes; then unblock Terrain |
| `FlowNetworkPanel.{h,cpp}` | retired `…/WorkspaceHost/FlowNetworkPanel.{h,cpp}` | Optional — only if the panel UI is wanted (not needed to compile Terrain) |

## Audio — no source for the 8 spec components; design plan authored (Batch K, 2026-07-26)

Spec (`FolderStructure.md`) lists an 8-component audio service (AudioExtension, AudioConfiguration,
AudioDevice, SoundSource, SpatialEmitter, AudioListener, MixOrder, AudioSampleStore). Survey of both
trees found **none of the 8 exist as source**. Per zero-fabrication, **nothing ported, nothing invented**.
Instead the authoritative design is captured in `Internal/EngineContext/Audio/PLAN-Audio.md` (component
map, RT-thread contract, miniaudio seam, build order, house-style conformance). Author each component
from that plan when a real audio consumer lands.

The only real audio code anywhere is a GEngine engine-synth playback path
(`RetiredProject/.retired/GEngine/Internal/Runtime/Audio/` — `AudioOutput.{h,cpp}` + `EngineToneSynth.{h,cpp}`,
~356 LOC). It is **out-of-architecture** for the editor (vehicle-sound feature, depends on GEngine's
`DiagnosticLedger`, guards `GENGINE_RUNTIME_*`). Kept as a **pattern reference** in PLAN-Audio.md §7 — do
NOT port as-is; resurrect under a game runtime only if procedural audio ever returns.

| Target component | Source | Action when needed |
|---|---|---|
| `AudioDevice.{h,cpp}` | reference only (retired `AudioOutput`) | Author miniaudio seam per plan §4 |
| other 7 components | ❌ no source | Author from PLAN-Audio.md §2 build order |

## macOS windowing backend — build integration (Objective-C++ + frameworks) (2026-07-26)

`Internal/Platform/Windowing/PlatformWindowMac.cpp` is written and links the full `PlatformWindow`
seam (guarded on `__APPLE__`; the Stub already excludes `__APPLE__`, so exactly one backend compiles
per OS). Nothing to download — it uses only the macOS SDK frameworks that ship with Xcode's Command
Line Tools and the `VK_EXT_metal_surface` header already vendored in `ExternalPackages`. Two
build-infra follow-ups are needed before it does anything on a real Mac:

- **Compile this ONE unit as Objective-C++ (`.mm`) with `-DFRONTIER_APPLE_COCOA`.** All Cocoa /
  `CAMetalLayer` code sits behind `#if defined(FRONTIER_APPLE_COCOA)` (same seam technique as
  Wayland's `FRONTIER_WAYLAND_PROTOCOLS`). Compiled as plain C++ (the only mode on the Windows box)
  it builds against core `<vulkan/vulkan.h>` alone: the target still links, but every window reports
  failure. The real layer switches on only when the unit is built `.mm` on a Mac with the define set.
- **Link `-framework Cocoa -framework QuartzCore`** on Apple targets (QuartzCore supplies
  `CAMetalLayer`; Cocoa supplies `NSWindow` / `NSApplication` / `NSView`). MoltenVK provides
  `vkCreateMetalSurfaceEXT`.

Status: **UNVERIFIED until built on macOS** (no Mac available here; the enum / signature references
were checked against `InputPacket.h` by hand and match). When the target build infra is migrated, add
the `.mm` compile rule + framework link flags for the Apple target, then compile-verify.

## Navigation ViewCube — second half not yet ported (Batch J, 2026-07-26)

`Navigation/Camera/` (12 files) is ported + build-verified. The second half of the Navigation pillar,
`ViewportOrientationManipulator/` (the live ViewCube — 17 files under Configuration/, Intersection/,
Projection/, Topology/, AuxiliaryTriggers/ + the two roots), is **not yet ported**. Source is in the
reorg tree at `Internal/Interface/ViewportOrientationManipulator/`. It depends on ImGui
(`Context.DrawList`), reads/writes the now-ported `ViewportCamera`, and its source guards are
`FRONTIER_INTERFACE_*` while the target re-homes it under `Navigation/` (guards → `FRONTIER_ENGINECONTEXT_NAVIGATION_*`).
Note the source already carries a `Projection/CoordinateProjection.h` that collides by name with
MetricSpace's `CoordinateProjection.h` — confirm which is canonical before porting. Port as its own batch.

## Configuration — no real source, not ported (Batch G, 2026-07-26)

Spec (`FolderStructure.md` lines 39–41) lists `Configuration/` = `EngineConfiguration.{h,cpp}` +
`RenderConfiguration.{h,cpp}`. Survey: `EngineConfiguration` is an empty scaffold stub
(`RetiredProject/.retired/Attic-Geometry/.../Root/`); `RenderConfiguration` has **no source anywhere**.
Per zero-fabrication, **deferred, not invented**. (`CameraConfiguration.{h,cpp}` exists in several
trees but is Navigation-pillar, rank 8 — not this folder.)

| Target component | Source | Action when needed |
|---|---|---|
| `EngineConfiguration.{h,cpp}` | 🔴 stub | Author when engine-wide settings land |
| `RenderConfiguration.{h,cpp}` | ❌ absent | Author when render settings consumer appears |

## EngineHost — empty scaffold, not ported (Batch F, 2026-07-26)

Spec (`FolderStructure.md` lines 33–36) lists `EngineHost/` = `EngineHost.{h,cpp}` (was
`RootExtension`, "initialize → loop → finalize"), `ExtensionRegistry.{h,cpp}` (was
`InternalRegistry`), `TimingExtension.{h,cpp}` (delta accumulation). Survey of the source
(`RetiredProject/.retired/Attic-Geometry/Engine/Internal/EngineInfrastructure/Root/`) found all
three are **empty scaffold placeholders** — bare `struct X {};` with "implementation pending"
banners, no logic. Per zero-fabrication, **deferred, not invented**.

| Target component | Source | Action when needed |
|---|---|---|
| `EngineHost.{h,cpp}` | 🔴 `RootExtension` stub | Author the Initialize→loop→Finalize lifecycle when the runtime entry point lands |
| `ExtensionRegistry.{h,cpp}` | 🔴 `InternalRegistry` stub | Author the active-extension registry alongside EngineHost |
| `TimingExtension.{h,cpp}` | 🔴 `TimingExtension` stub | Author delta accumulation when the frame loop needs it |

Also present in the same source folder (not spec'd for EngineHost): `EngineConfiguration.{h,cpp}`
(→ Configuration pillar, rank 5) and `MemorySegment.{h,cpp}` — verify state before porting.

## SpatialAcceleration — placeholder home only (Batch E, 2026-07-26)

No source exists. `Internal/EngineContext/SpatialAcceleration/README.md` marks the reserved home for
future `SpatialPartition.{h,cpp}` (octree/BVH) + toroidal structures. Nothing ported; nothing invented.

## MicroUtils — canonical utilities not yet authored (Batch C, 2026-07-26)

The spec (`FolderStructure.md` lines 56–61) lists 8 MicroUtils components. Survey of the source
trees found only **one** with real logic; the rest are stubs or absent. Per the zero-fabrication
rule these were **deferred, not invented**. Author each when its first real consumer needs it.

| Component | Source state | Action when needed |
|---|---|---|
| `VacancyTable` ★ | ✅ ported this batch (de-forked from `InstrumentVacancyTable`) | — done |
| `TokenIssuer` ★ | ❌ no source anywhere | Author when Scene/Revision identity lands (design: monotonic issue + generation) |
| `TokenAuthentication` ★ | ❌ no source anywhere | Author alongside `TokenIssuer` (stale-token check) |
| `IdentifierAllocation` | 🔴 stub (`struct {};`, "implementation pending") in Attic-Geometry | Author when a real ID allocator is required |
| `ColorConversion` | 🔴 stub | Author when Theme/ColorEntry needs it |
| `StringFormatting` | 🔴 stub | Author when a real formatter consumer appears |
| `HashComputation` | 🔴 stub | Author when hashing consumer appears (xxHash is vendored) |
| `RangeArithmetic` | 🔴 stub | Author when a real range util consumer appears |

Stub sources (do **not** port as-is — no logic):
`RetiredProject/.retired/Attic-Geometry/Engine/Internal/EngineInfrastructure/MicroUtils/*.{h,cpp}`

## Instrumentation VacancyTable — rewire to canonical (deferred, needs confirmation)

`Internal/Interface/Instrumentation/Registry/InstrumentVacancyTable.{h,cpp}` (in the SOURCE tree)
is a prefixed fork of the now-canonical `EngineContext/MicroUtils/VacancyTable`. When the
Instrumentation pillar is ported, rewire it to `VacancyTable<InstrumentCapacity>` (64) and drop the
fork. **Copy-first**: the original stays until the rewire is confirmed to build.

## MicroUtils extras found but not spec'd (flagged, not ported)

Real code in `RetiredProject/.retired/Engine-Deprecated/Internal/MicroUtils/`:
`SparseSolver.{h,cpp}` (~10 KB) and `PortableCRuntime.h`. Not named in the FolderStructure.md
MicroUtils row. Decide placement before porting (SparseSolver likely belongs to a solver/math home,
not MicroUtils).

## Motion — stub components not ported (Batch B, 2026-07-26)

Spec lists MotionExtension, Interpolation/ (Scalar/Vector/Color), BezierEasing, EaseTable,
InertiaDynamics, DampingDynamics, SequenceAnimator, TimelineAnimator. All are empty stubs in the
source. Only the 4 real files were ported (EasingProfile, SpringDynamics, TransitionDescriptor,
MotionEvaluator). Author the rest when a real consumer needs them.
