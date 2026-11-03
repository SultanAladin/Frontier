# WorldOccupantRegister — world-wide resident occupant correlation specification

Status: **architectural specification** (2026-11-03, branch `arena/aa46fa2a-frontier`). Stored as a `.md` design
document prior to C++ rollout under `Frontier/Engine/Scene/WorldOccupantRegister.{h,cpp}`. Companion runtime units
already live in this branch: `Frontier/Engine/PhysicalDynamics/JoltAdapter.{h,cpp}` and
`Frontier/Engine/PhysicalDynamics/PhysicsExecutionThread.{h,cpp}`.

---

## 1. Mechanism in one sentence

`WorldOccupantRegister` maintains a dense, generation-authenticated contiguous-memory correlation table that links every
resident world entry across **authoring** (`SceneWorld` / `SceneDirectory` `RecordToken`), **physical dynamics**
(`JoltAdapter` `RigidBodyIdentity` and `VehicleRigSlot`), and **GPU illumination & picking** (`IlluminationHost`
`OccupantEntry` spans, reserve slots, fracture pools, and `VehicleRegion`), publishing interpolated world transforms and
dirty relocation spans without cross-thread stalls.

---

## 2. Why a dedicated register is required

Today the three domains hold partial views of the same world inhabitants:

| Domain | Current store | Identity | Gap without `WorldOccupantRegister` |
|---|---|---|---|
| Scene authoring & Outliner | `SceneWorld` + `SceneDirectory` (`EnclosureAtlas`) | `RecordToken` (`uint32_t Index` + `uint32_t Generation`) | Knows authored `GeometryRecord`, `VehicleRecord`, `TyreRecord`, `ForceFieldRecord`, and Array modifiers, but does not own live Jolt body slots or reserve GPU occupant spans in one dense table. |
| Physical dynamics (`JoltAdapter` + `PhysicsExecutionThread`) | Jolt v5.2.0 broadphase + `VehicleJoltRig` | `RigidBodyIdentity` / `VehicleRigSlot` | Steps static, kinematic, dynamic, trigger, fracture, and articulated vehicle bodies at 240 Hz substeps; needs `O(1)` bidirectional correlation with `RecordToken` and GPU `OccupantEntry` indices. |
| Rendering (`IlluminationHost` + `SceneViewport`) | `SceneStructure::Occupants` (`2 094` entries in ProvingGround) + `PickProjection` | `int32_t OccupantIndex` | GPU picking (`PickProjection`) returns an `OccupantIndex`; `OccupantRelocation` consumes moved transforms; reserve copies and smash-alley fragment pools need deterministic slot reclamation. |

`WorldOccupantRegister` closes all three gaps with a single contiguous Structure-of-Arrays (SoA) register owned at the
world boundary (`Initialize` -> `Integrate` -> `Finalize`).

---

## 3. Naming & vocabulary alignment (`SKILL-Naming.md`)

- **Register name:** `WorldOccupantRegister` (matches `LuminaireRegister` in `GlobalIllumination/`; avoids barred words
  `Object`, `Entity`, `Element`, `Manager`, `Registry`-as-singleton, `Cache`, `Buffer`, `Model`).
- **Handle:** `OccupantSlotToken` (`SlotIndex` + `Generation`), issued by `TokenIssuer` and recycled through
  `VacancyTable`.
- **Connectivity (non-kinship):** `EnclosureToken` (`EnclosureOf`), `NestedRegionToken`, `LateralSuccessorToken`, and
  `AbsoluteBoundaryToken` — identical to `SceneDirectory`'s `EnclosureAtlas`. For example, `Vehicle · GT-04` encloses
  its four `Tyre · FL/FR/RL/RR` records; detaching a tyre at a pit stop clears `EnclosureToken` (relocating it to the
  Pit Lane group) while preserving its `OccupantSlotToken` and `RecordToken`.

---

## 4. Contiguous memory layout (Structure of Arrays)

Every registered world inhabitant occupies one dense slot `s` in `[0, Capacity)`. Cold authoring metadata lives in a
side table so hot per-frame physics-to-render sweeps touch only 64-byte-aligned spatial lanes:

| Lane array | Element type | Bytes / slot | Purpose |
|---|---|---:|---|
| `Generations` | `uint32_t` | 4 | Authenticates `OccupantSlotToken`; incremented on `ReclaimSlot`. |
| `ActivityCategories` | `OccupantMobilityCategory` (`uint8_t`) | 1 | `Static`, `Kinematic`, `DynamicRigid`, `VehicleChassis`, `EnclosedWheel`, `FractureFragment`, `VolumeTrigger`, `DormantReserve`. |
| `VisibilityFlags` | `uint8_t` | 1 | Bitfield: `VisibleBit`, `CastShadowBit`, `DistanceFieldBit`, `ShreddedBit`, `EnclosedBit`, `LockedBit`. |
| `DirtyRelocationEpoch` | `uint16_t` | 2 | Set whenever authoring or `PhysicsExecutionThread` mutates the slot's world transform. |
| `RecordTokens` | `RecordToken` | 8 | Owning `SceneWorld` record (`Invalid` for pure procedural debris until promoted). |
| `EnclosureTokens` | `OccupantSlotToken` | 8 | Enclosing slot (`AbsoluteBoundaryToken` when top-tier; e.g. `VehicleChassis` slot for `Tyre · FL..RR`). |
| `PhysicsIdentities` | `RigidBodyIdentity` | 4 | Bound body in `JoltAdapter` (`0` when unsimulated or enclosed wheel). |
| `VehicleSlots` | `uint16_t` | 2 | Index into `JoltAdapter` vehicle table (`0xFFFF` for non-vehicle occupants). |
| `RenderSpanFirst` | `int32_t` | 4 | First `IlluminationHost::Scene.Occupants` index claimed by this slot (`-1` if unrendered). |
| `RenderSpanCount` | `uint16_t` | 2 | Number of contiguous or indexed GPU occupants driven (parts x Array modifier copies). |
| `PreviousTransforms` | `WorldAffineTransform` (`3x4` float) | 48 | Pose at start of latest completed outer physics tick $t_{k-1}$. |
| `CurrentTransforms` | `WorldAffineTransform` (`3x4` float) | 48 | Pose at end of latest completed outer physics tick $t_k$. |
| `InterpolatedTransforms` | `WorldAffineTransform` (`3x4` float) | 48 | Render pose $\text{Slerp}(T_{k-1}, T_k, \alpha)$ for accumulator fraction $\alpha \in [0, 1]$. |
| `WorldBounds` | `AxisAlignedBox` (`min` + `max`) | 24 | World-space AABB updated on relocation for frustum, portal, and region queries. |

Reverse lookup tables maintained in lockstep:

1. `RecordToSlotTable`: open-addressed hash / sparse page mapping `RecordToken.Value -> OccupantSlotToken` in `O(1)`.
2. `RenderOccupantToSlot`: flat `std::vector<OccupantSlotToken>` sized to `Scene.Occupants.size()` (`2 094` entries in
   ProvingGround = `16.7 KiB`), turning a GPU `PickProjection` hit index directly into `(OccupantSlotToken, RecordToken,
   RigidBodyIdentity)` in a single array load.
3. `PhysicsBodyToSlot`: flat or sparse map from `RigidBodyIdentity.Identifier` to `OccupantSlotToken` for contact,
   fracture-threshold, and trigger-overlap callbacks.

---

## 5. Multi-rate synchronization with `JoltAdapter` and `PhysicsExecutionThread`

```text
  Authoring / Inspector UI (Main Thread)          PhysicsExecutionThread (240 Hz substeps)
  ──────────────────────────────────────          ────────────────────────────────────────
  SceneWorld edits, gizmo W/E/R drags,   ──►      Drains AuthoringMutationBatch at outer
  pit-stop tyre detach/fit, ForceField            tick boundary; steps JoltAdapter across
  parameters, DriverCommand inputs                N substeps (e.g. 4 x 1/240 s = 1/60 s);
                                                  publishes immutable WorldPhysicsSnapshot
                                                               │
                                                               ▼
                                         WorldOccupantRegister::IntegratePhysicsSnapshot(α)
                                         ──────────────────────────────────────────────────
                                         1. Updates PreviousTransforms / CurrentTransforms
                                         2. Computes InterpolatedTransforms(α) for moved slots
                                         3. Appends moved RenderSpan indices to DirtySpanList
                                                               │
                                                               ▼
                                         SceneViewport / IlluminationHost (Render Submission)
                                         ────────────────────────────────────────────────────
                                         Relocates only DirtySpanList occupants (BLAS/TLAS,
                                         SceneDistanceSpace, SurfaceCardDepot, LuminaireRegister)
                                         and writes 10 448-byte VehicleProfile to VehicleRegion
```

### Key invariants

1. **Zero torn transforms:** `PhysicsExecutionThread` never mutates `SceneStructure::Occupants` directly; it publishes a
   self-contained `WorldPhysicsSnapshot` at outer-step completion, and `WorldOccupantRegister` interpolates with
   $\alpha = \text{AccumulatorRemainder} / \Delta t_{\text{outer}}$ on the presentation thread.
2. **Array-modifier & multi-part expansion:** A single `GeometryRecord` with $P$ archive parts and $M$ Array-modifier
   copies owns one logical `RecordToken` entry with `InstanceCount = M`, mapping either to static compound/instanced
   colliders in `JoltAdapter` or (when `Dynamic && Physics` is enabled) per-copy dynamic rigid bodies drawn from the
   archive's reserve slots.
3. **Smash-alley fracture promotion:** When a `Breakable` occupant's contact impulse in `JoltAdapter` exceeds
   `FractureConfiguration::ImpulseThresholdNs`, `WorldOccupantRegister` transitions the intact slot to `Inactive` in
   `Scene.Occupants`, activates its pre-allocated fragment pool span (`FragmentCapacity`), and registers dynamic rigid
   bodies for each active shard.
4. **Pit-stop tyre enclosure:** Each `TyreRecord` (`FL`, `FR`, `RL`, `RR`) holds its own slot with
   `EnclosureToken = VehicleSlot`. While `EnclosedBit = 1`, `VehicleScenePublisher` drives its wheel occupant matrix and
   `CarcassDisplacements` (`160` nodes per wheel) from `VehicleSolver`. Clearing `EnclosedBit` detaches the wheel from
   `VehicleSolver`, parks its world transform in the Pit Lane bay (or promotes it to an independent rolling Jolt
   cylinder), and updates Outliner grouping without invalidating its `RecordToken`.

---

## 6. Planned C++ public surface (`Frontier/Engine/Scene/WorldOccupantRegister.h`)

```cpp
namespace Frontier
{

struct OccupantSlotToken
{
    uint32_t Index      = 0u;
    uint32_t Generation = 0u;
    [[nodiscard]] constexpr bool Valid() const noexcept { return Generation != 0u; }
};

enum class OccupantMobilityCategory : uint8_t
{
    Static          = 0,
    Kinematic       = 1,
    DynamicRigid    = 2,
    VehicleChassis  = 3,
    EnclosedWheel   = 4,
    FractureFragment= 5,
    VolumeTrigger   = 6,
    DormantReserve  = 7,
};

class WorldOccupantRegister
{
  public:
    void Initialize(uint32_t InitialSlotCapacity, uint32_t RenderOccupantCount);
    void Finalize() noexcept;
    void Reset() noexcept;

    // Registration & reclamation
    [[nodiscard]] OccupantSlotToken RegisterOccupant(RecordToken               Record,
                                                     OccupantMobilityCategory  Mobility,
                                                     const WorldAffineTransform& InitialTransform,
                                                     int32_t                   RenderFirst = -1,
                                                     uint16_t                  RenderCount = 0u);
    bool ReclaimOccupant(OccupantSlotToken Slot) noexcept;
    [[nodiscard]] bool Authenticate(OccupantSlotToken Slot) const noexcept;

    // Non-kinship enclosure (e.g. Vehicle enclosing 4 Tyres; Pit-lane detachment)
    bool EncloseOccupant(OccupantSlotToken InnerSlot, OccupantSlotToken EnclosingSlot) noexcept;
    bool DetachOccupant(OccupantSlotToken InnerSlot, const WorldAffineTransform& DetachedWorldPose) noexcept;

    // O(1) cross-domain correlation
    [[nodiscard]] OccupantSlotToken ResolveByRecord(RecordToken Record) const noexcept;
    [[nodiscard]] OccupantSlotToken ResolveByRenderOccupant(int32_t RenderOccupantIndex) const noexcept;
    [[nodiscard]] OccupantSlotToken ResolveByPhysicsBody(RigidBodyIdentity Body) const noexcept;

    // Synchronization with JoltAdapter + PhysicsExecutionThread + IlluminationHost
    void SynchronizeAuthoring(const SceneWorld& World, JoltAdapter& Physics);
    void IntegratePhysicsSnapshot(const WorldPhysicsSnapshot& Snapshot, float InterpolationAlpha) noexcept;
    uint32_t FlushDirtyRelocations(IlluminationHost& Host);

    // Spatial queries
    void EnumerateOverlappingBox(const AxisAlignedBox& QueryRegion,
                                 std::vector<OccupantSlotToken>& OutSlots) const;
};

} // namespace Frontier
```

---

## 7. Rollout sequence

1. **Done in this branch:** `RigidBodySolver`, `JoltAdapter` (multi-occupant static / kinematic / dynamic / force-field /
   vehicle world integration + pose interpolation), `PhysicsExecutionThread` (dedicated background `std::jthread` with
   fixed outer step + `N`-substep accumulator & snapshot publication), `VehicleScenePublisher`, and `VehicleInspector` /
   `TyreInspector`.
2. **Next C++ step:** Promote `EverestSceneViewport`'s geometry resolution loop + `JoltAdapter`'s occupant map into
   `Frontier/Engine/Scene/WorldOccupantRegister.{h,cpp}` so `PickProjection` clicks, Array-modifier reserve allocation,
   smash-alley fracture pools, and Jolt rigid bodies share the single SoA register above.
