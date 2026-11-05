# SessionPolicy, NetPolicy, RespawnPolicy, SpawnPolicy, Agent, InputDriver & SyntheticDriver Validation

| Status | Verification Claim |
| :--- | :--- |
| 🟢 | InputEncoder::SampleGlfwWindow(nullptr) produces a baseline InputPacket |
| 🟢 | InputEncoder detects rising edge on InputActionReady |
| 🟢 | InputDriver binds to Agent and marks AgentDriverKind::InputDriven (human controlled) |
| 🟢 | InputDriver drives Agent forward using encoded human RawInputState |
| 🟢 | SyntheticDriver binds to the same Agent and marks AgentDriverKind::SyntheticDriven (AI controlled) |
| 🟢 | SyntheticDriver autonomously steers and accelerates the Agent toward circuit waypoints |
| 🟢 | WorldSpace loads Content/Levels/Default.levelspace for Lobby session |
| 🟢 | SessionConfiguration round-trips cleanly through TOML |
| 🟢 | LobbyPolicy enforces RespawnRule::Disabled (no respawning in Lobby) and NetPolicy::AllowJoinInProgress |
| 🟢 | Human players (InputDriver) and AI player (SyntheticDriver) join the Lobby and receive distinct PlayerTokens |
| 🟢 | SessionSpace::RequestAgentRespawn returns false in Lobby because RespawnPolicy is Disabled |
| 🟢 | Peer Driver B leaves the Lobby cleanly and their preview Agent is released |
| 🟢 | Active SessionPolicy in Lobby is LobbyPolicy |
| 🟢 | LobbyPolicy switches Car Studio backdrop between WhiteCyclorama and SoftboxLoft |
| 🟢 | Car Studio turntable rotates preview Agent smoothly while waiting for ready-up |
| 🟢 | PlayerStore::AreAllConnectedReady returns true once human and AI players are ready |
| 🟢 | LevelTransition transitions SessionSpace and WorldSpace to Content/Levels/ProvingGroundVehicle.levelspace |
| 🟢 | TransitionEpoch increments to 2 after Lobby -> ProvingGrounds transition |
| 🟢 | TransferConfiguration preserves Host PlayerToken, StoredUnitToken, and Car Studio VehicleUnitConfiguration across LevelTransition |
| 🟢 | TransferConfiguration round-trips preserved PlayerProfiles, VehicleUnitConfigurations, and TargetPolicyName through TOML |
| 🟢 | RacePolicy is active on ProvingGroundVehicle.levelspace with RespawnPolicy enabled |
| 🟢 | NetworkRecordIndex binds spawned ProvingGrounds Agent RecordTokens to PlayerTokens |
| 🟢 | Host Agent (driven by InputDriver) accelerates forward on ProvingGroundVehicle.levelspace |
| 🟢 | AI Agent (driven by SyntheticDriver) accelerates autonomously along circuit waypoints on ProvingGroundVehicle.levelspace |
| 🟢 | SpawnPolicy stands a vehicle on a flat studio pad at zero slope |
| 🟢 | SpawnPolicy stands the same vehicle on a 30° bank, pitched with the surface it rests on |
| 🟢 | SpawnPolicy banks the same vehicle on a cross-slope with the surface's own roll |
| 🟢 | SpawnPolicy takes the stand's height from the surveyed ground, not from the requested point |
| 🟢 | SpawnPolicy's stand transform lifts the chassis along the surface normal it surveyed |
| 🟢 | SpawnPolicy SuspendedDrop holds the vehicle above the surface before it is released |
| 🟢 | SpawnPolicy StudioParked hands the vehicle over with its handbrake engaged and GridRelease with it released |
| 🟢 | SpawnPolicy with brakes disabled at spawn ignores the driver for BrakeHoldSeconds, then hands the pedals over |
| 🟢 | SpawnPolicy keeps a parked Agent's handbrake latched until its driver asks for drive |
| 🟢 | SpawnPolicy FrontDriven fits the front axle only, leaving the rear wheels unpowered |
| 🟢 | SessionSpace::RequestAgentRespawn succeeds in ProvingGrounds when RespawnPolicy is enabled |
| 🟢 | RacePolicy tracks checkpoint gates and declares HostToken the winner upon completing the final lap |
| 🟢 | EliminationPolicy initializes with 3 active standing players and RespawnPolicy enabled |
| 🟢 | EliminationPolicy eliminates trailing Agents each interval and crowns the leader |
| 🟢 | LevelTransition returns SessionSpace to LobbyPolicy (Epoch #3, RespawnPolicy Disabled) with player scores intact |

**Assertions:** 39 · **Failures:** 0
