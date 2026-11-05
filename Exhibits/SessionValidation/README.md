# SessionPolicy, NetPolicy, RespawnPolicy, SpawnPolicy, Agent, InputDriver & SyntheticDriver Validation

Headless C++ verification exhibit for Frontier's session policy and agent driving architecture:

- **Controllable Scene Unit (`Frontier/Engine/Scene/Agent.{h,cpp}`)**:
  - `Agent` and `VehicleUnitConfiguration` bound to a `WorldSpace` `RecordToken`.
- **Human & AI Drivers (`Frontier/Engine/Input/`)**:
  - `InputPacket.h` & `InputEncoder.{h,cpp}` — Normalized per-step vehicle and session control frame.
  - `InputDriver.{h,cpp}` — Human driver binding a `PlayerToken` and `InputEncoder` to a possessed `Agent`.
  - `SyntheticDriver.{h,cpp}` — Autonomous AI driver steering and throttling an `Agent` along circuit waypoints using the same `InputPacket` interface.
- **Session, Net & Respawn Policies (`Frontier/Engine/Session/SessionPolicy.{h,cpp}`, `Projects/Everest/Source/Session/SessionPolicy.{h,cpp}`)**:
  - `NetPolicy` — Host authority, join-in-progress, and synthetic AI backfill policy.
  - `RespawnPolicy` — Enforces `RespawnRule::Disabled` in Lobby (no respawning in Lobby) and `RespawnRule::SpawnGrid` / `RespawnRule::LastCheckpoint` in ProvingGrounds.
  - `LobbyPolicy`, `ProvingGroundsPolicy`, `RacePolicy`, and `EliminationPolicy` — Default Engine rules in `Frontier/Engine/Session/SessionPolicy.{h,cpp}` and Project Everest specializations in `Projects/Everest/Source/Session/SessionPolicy.{h,cpp}`.
- **Vehicle Spawn (`Frontier/Engine/Session/SpawnPolicy.{h,cpp}`, `Frontier/Extensions/Physics/VehicleDeployment.{h,cpp}`)**:
  - `SpawnPolicy` — Where a vehicle stands regardless of slope (`SpawnSupportRule::SurveySlope` probes the whole footprint and the Agent
    rests on the surface's own pitch and roll) plus the rules a session varies: brakes enabled or disabled at the start
    (`BrakesEnabledAtSpawn`, `BrakeHoldSeconds`), the parked handbrake, and which wheels drive (`DrivenWheelRule`).
  - `SpawnPolicy::{StudioParked, GridRelease, SuspendedDrop}` — the studio bay, the proving-ground grid, and an air drop.
  - `VehicleDeployment` — the vehicle's own deployment function against `PhysicsSpace`: footprint survey, stand transform, and the
    spawn brake/driven-wheel handover the solver receives.
- **Seamless Level Transitions (`Frontier/Engine/Session/{SessionSpace,LevelTransition,TransferConfiguration}.{h,cpp}`)**:
  - Preserves `PlayerStore` profiles, `PlayerToken`s, and customized `VehicleUnitConfiguration`s across transitions between `Content/Levels/Default.levelspace` (Lobby) and `Content/Levels/ProvingGroundVehicle.levelspace` (ProvingGrounds).
