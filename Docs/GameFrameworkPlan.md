# Frontier editor game framework — plan (no implementation)

## What this is

A plan for the gameplay layer the editor still lacks: an **input system**, a
**controller/possession system** (Unreal-like), **game mode/state/player state**,
and a **game instance** — composed so the existing login (EOS Auth+Connect),
lobby/session/voice plumbing (Project-Networking) and vehicle simulation can run
**login → lobby → match → results** flows across **multiple game modes**.

This document is the deliverable. It proposes no code changes; all paths below
are where code would go. Conventions follow the engine (`Frontier::` namespaces,
record-based scene, snapshot readings, validation exhibits with assertions).

## Where the framework lands

| Piece | Proposed home | Why |
|---|---|---|
| Input core (devices, actions, contexts) | `Frontier/Engine/Input/` (new) | Engine-level: editor, exhibits and game share devices and binding |
| Controllers, pawns, possession | `Frontier/Engine/Game/` (new) | Gameplay-owned, engine-shared; pawns adapt scene records |
| GameInstance, app flow machine, mode registry | `Frontier/Engine/Game/` (new) | Lives above worlds; drives `FrontierMain` host states |
| Replicated state records | `Frontier/Engine/Scene/` record types | Reuses `NetworkingExtension::Replicate/Absorb` unchanged |
| EOS binding (identity → player, lobby → match) | `Frontier/Projects/Project-Networking/` | Keeps vendor SDK code in the project overlay, out of the engine |
| Validation exhibit | `Exhibits/GameFramework/` (new) | Headless login→lobby→match→results + determinism, repo style |

The framework is engine code, not an extension: extensions (`Physics`,
`Networking`, …) attach capabilities to a world, while the framework owns *who
plays in the world and how matches run*. It must not depend on EOS/Photon —
those stay behind the `NetworkTransport` abstraction and the project overlay.

## Ground the plan touches

- **Host**: `Frontier/Applications/Editor/FrontierMain.cpp` — the app entry the
  flow machine will drive (today: project browser → scene).
- **World**: `Frontier/Engine/Scene/WorldSpace` — record-based scene with
  `AdvanceSimulation`; one world per match; worlds load/unload under the flow.
- **State patterns to copy**: `VehicleReadings` (snapshot struct published per
  tick) and `VehicleRecordCodec` (record ↔ TOML). Game state will follow the
  same snapshot + codec shape so replication and replays fall out for free.
- **Replication that already exists**: `NetworkingExtension::Replicate/Absorb`
  ships changed record TOML host→clients. Game/match state rides this as data;
  no new replication protocol is needed for state (input messages are the only
  addition — see §5).
- **Flows that already exist**: EOS login/logout, auto-login, lobby create/
  browse/join/settings, match-session split, RTC voice, history records
  (`Docs/NetworkingLogin.md`, `Docs/NetworkingLobbyPlan.md`). The framework
  consumes these (identity, member lists, session start) rather than rebuilding
  them. The lobby already publishes `map`/`mode` settings — the mode registry
  keys off those strings.
- **First pawn**: the vehicle solver already takes `AssignVehicleInput`
  (throttle/brake/steer/handbrake) — a `VehiclePawn` adapter is thin, and the
  vehicle validation suite (78/78) is the physics backstop.

## 1 · Input system

**Shape**: Unreal Enhanced-Input-like, three layers.

1. **Devices** — keyboard, mouse, gamepad through one `InputDevice` interface.
   GLFW already ships (`ExternalPackages/glfw`) and provides all three
   (joystick/gamepad API included), so phase 1 binds GLFW only. No engine code
   touches GLFW event callbacks directly after this — devices normalize to:
   - **Actions** (discrete: pressed/released, e.g. `Handbrake`, `ShiftUp`),
   - **Axes** (continuous with deadzone/scale/curve, e.g. `Throttle`,
     `Steer`, `LookYaw`).
2. **Contexts** — a stack of binding sets with priority: `UI` (editor panels
   and menus) > `Cinematic` > `Gameplay` > `Vehicle`. Only the top gameplay
   context feeds the possessed pawn; UI input never leaks into the car. Mode
   switches (driving vs. on-foot vs. menu) are context pushes/pops, not
   re-binds. Touch/gyro are later devices, not a redesign.
3. **Sinks** — actions/axes route to exactly one consumer: the possessed pawn
   (via its controller), the UI layer, or a recorder. AI controllers and
   replays inject into the same action/axis stream as devices, so bots,
   netplay and replays share one path (this is what makes input replay for
   determinism checks trivial).

**Persistence**: bindings serialize to TOML beside app prefs (same shape as the
portable `Charge.local.ini` handling: strict parse, no secrets). Remapping UI
comes after the core; the data model must support chords and per-mode overrides
from day one.

**Out of scope for phase 1**: haptics/FFB, Steam Input, mobile sensors,
gesture recognition.

## 2 · Controller and possession (Unreal-like)

**Roles** (names mirror Unreal so hires ramp fast, semantics fit Frontier):

- **Pawn** — anything possessable. Minimal interface: `Possess/Release`,
  `ApplyAction`, `ApplyAxis`, `Tick`. It adapts a scene record, it is not one:
  `VehiclePawn` wraps a vehicle record + solver input; a future `WalkerPawn`
  wraps a character record. Pawns never read devices directly.
- **Controller** — the brain. `PlayerController` (owns an input context +
  sink, routes device actions to its pawn) and `AIController` (writes the same
  action/axis stream from behavior code). Exactly one pawn per controller;
   swapping vehicles/characters is unpossess→possess, which is also how
  spectating and seat-switching work.
- **Possession protocol** — `Controller::Possess(Pawn)` binds context→pawn and
  transfers the input sink; `Unpossess` parks the pawn (handbrake-on equivalent:
  zeros its inputs, keeps simulating). Possession changes are host-authoritative
  and replicate as state (see §4), never as RPCs.

**Camera**: controllers own a camera rig (chase/hood/orbit for vehicles), fed by
the pawn's snapshot readings. No camera code inside pawns.

**Phase 1 must prove**: possess vehicle → drive with keyboard AND gamepad →
unpossess → AI drives the same car through the same actions → input recording
of the run replays bit-identically (determinism culture: hash the snapshots).

## 3 · Game mode / game state / player state — redundancy analysis

Unreal ships four classes: `GameMode` (server-only rules), `GameState`
(replicated match state), `PlayerController` (per-player control) and
`PlayerState` (replicated per-player data). Frontier needs **two concepts, not
four** — because record replication already solves what `GameState` and
`PlayerState` are for:

| Unreal class | Frontier verdict | Rationale |
|---|---|---|
| `GameMode` | **Keep** (host-only director) | Rules, spawning, win conditions, timers. Exists only on the host; never replicates. One active per match, from the mode registry. |
| `GameState` | **Fold into a replicated record** | A `MatchState` record (mode id, phase, clock, scores, standings) replicated by the existing `Replicate/Absorb`. No bespoke class — it is data with a codec, like every other record. |
| `PlayerController` | **Keep** (§2) | Control + input routing are genuinely distinct from data. |
| `PlayerState` | **Fold into a replicated record** | A `PlayerRow` record per member (name, PUID hash, pawn binding, ready flag, score). The lobby member list seeds it; the match updates it. |

So the framework has **directors** (host-side logic: `GameMode`,
`PlayerController`) and **records** (replicated data: `MatchState`,
`PlayerRow`). This halves the API surface versus Unreal with zero loss: anything
Unreal expresses as GameState/PlayerState, Frontier expresses as record fields
that replicate, persist to history, and round-trip through level TOML for free.

**`GameMode` contract** (the multiple-modes requirement): a registry
`mode-id → factory` (ids match the lobby `mode` setting strings). Each mode
declares: min/max players, pawn kind(s), spawn/possession rules, phase machine
(countdown → live → finish), scoring, win conditions, and join-in-progress
policy. Ship two modes in phase 1 to prove the registry is real (e.g.
`time-trial` solo + `circuit-race` multiplayer); modes 3+ are content, not
framework work.

## 4 · Game instance and the app flow machine

**`GameInstance`** outlives worlds. It owns: the login identity (PUID/display
name from the EOS layer, opaquely — no tokens held), the active transport
selection (EOS direct vs Photon, per `NetworkingLobbyPlan`), the mode registry,
the flow machine, and the transition payload (lobby id → session → world +
mode + roster). Exactly one per process, created by `FrontierMain` before any
world exists.

**Flow machine** (states; every transition is explicit and logged):

```text
Boot → Login → Lobby → MatchLoading → Match → Results → Lobby → …
  │       │        │          │            │         │
  │       │        │          │            │         └── history write (existing format)
  │       │        │          │            └── mode's finish phase → standings record
  │       │        │          └── open world, spawn pawns, bind roster→controllers
  │       │        └── lobby members + ready + mode/map settings (+ session start for net matches)
  │       └── EOS Auth+Connect (existing app) → identity handoff to GameInstance
  └── transports/SDK init (existing checks)
```

Offline matches skip `Lobby` (`Login → MatchLoading` with a solo roster of one
real + N AI players — dummies stay out of stats, per the lobby plan's rule).
Leaving a match runs the lobby plan's exit cascade (session first, then lobby)
before returning to `Lobby`; quitting to desktop retires the platform once, per
the SDK-lifetime rule.

**World loading**: `MatchLoading` opens the mode's world (`.levelspace`),
constructs `MatchState` + `PlayerRow` records, spawns pawns, and binds
controllers. `Match` ticks `WorldSpace::AdvanceSimulation` with controller
inputs applied; `Results` freezes the world and presents standings. Net matches
start ticking only after the session-start callback the lobby plan defines.

## 5 · Networking shape (framework side only)

The framework assumes, but does not implement, the transport work listed as
remaining in `Docs/NetworkingLobbyPlan.md` (P2P + replication depth). Its
contract with that work:

- **State down**: `MatchState` + `PlayerRow` + pawn snapshots replicate via the
  existing record channel. No new protocol.
- **Input up**: clients send action/axis frames (tick-stamped) to the host; the
  host applies them to controllers and simulates. This is the one new message
  family, and it is what makes AI/replay/netplay one path (§1).
- **Authority**: host simulates everything; clients interpolate snapshots.
  Possession changes and phase transitions are host-only writes to the state
  records. No client prediction in phase 1 (vehicles at 240 Hz interpolate
  cleanly; prediction is a later optimization, not a framework change).
- **Join-in-progress / migration / backfill**: per-mode policies (§3) executed
  against session features when they exist; modes must degrade gracefully when
  they do not (solo/AI backfill).

## 6 · UI/UX binding

- Menus (login card, lobby tabs, settings, results) are UI-context input
  consumers; gameplay input isHint: the context stack (§1) is what prevents
  "typing in chat steers the car."
- HUD reads snapshot records only (never solver internals) — same rule the
  vehicle exhibit follows with `VehicleReadings`.
- Mode select, roster/ready display, countdown, standings, and results screens
  are framework-provided against `MatchState`/`PlayerRow`; visual skin follows
  the reference palette already chosen for the lobby tabs.

## 7 · Validation (repo culture: exhibits + assertions)

`Exhibits/GameFramework/` proves the framework headless:

1. **Flow test**: scripted identity → mock lobby (member list + ready + mode
   setting, no EOS) → load world → run a 2-mode roster (1 player + AI) through
   countdown→live→finish → results standings match the mode's scoring exactly.
2. **Possession test**: possess → drive 5 s → unpossess → AI drives same pawn;
   assert input streams are structurally identical and the pawn never reads a
   device.
3. **Input replay determinism**: record action/axis frames of a scripted run,
   replay twice, assert identical snapshot hashes (extends the I6 pattern).
4. **Mode-registry test**: both shipped modes construct from their lobby `mode`
   strings; an unknown mode string fails closed with a diagnostic, never a
   default match.
5. **State-record round-trip**: `MatchState`/`PlayerRow` → TOML → records,
   assert equality (extends the E-section levelspace pattern).

EOS-bound assertions (real lobby → real session → match start) stay in the
Project-Networking checks, which own the vendor SDK; the framework exhibit uses
mock transports with the same interfaces.

## 8 · Phased delivery

1. **Input core** — devices (GLFW KB/mouse/gamepad), actions/axes, context
   stack, TOML bindings, recorder. Prove with a scratch pawn in the exhibit.
2. **Possession** — pawn interface, `VehiclePawn` adapter, player + AI
   controllers, camera rig. Prove: drive → unpossess → AI drives → replay hash.
3. **Instance + flows + modes** — `GameInstance`, flow machine, mode registry,
   `time-trial` + `circuit-race`, `MatchState`/`PlayerRow` records. Prove: full
   offline loop with standings.
4. **Net state + input messages** — record replication of match rows (mostly
   wiring), tick-stamped input frames up. Prove: 2-peer mock-transport match.
5. **EOS binding** — identity handoff, lobby roster → controllers, session
   start → `MatchLoading`, exit cascade, history write. Prove in
   Project-Networking checks with the framework exhibit's mock lobby replaced
   by the real one.
6. **Hardening** — remapping UI, join-in-progress per mode, backfill,
   gamepad hot-plug, diagnostics/redaction pass (no tokens in logs, per the
   login docs' rule).

## 9 · Non-goals (explicit)

Reverse driving rules (physics scope already closed this), dedicated-server
binary, client prediction, anti-cheat, leaderboards/stats/achievements,
commerce/entitlements (Xsolla docs own this), voice (exists), new UI skin.
Ranked play needs §5 + the lobby plan's remaining EOS items; the framework must
not promise it in phase 1.

## 10 · Open questions for the owner

1. Mode ids 1 and 2 — confirm `time-trial` + `circuit-race`, or name others
   (drift? rallycross?) so the registry keys match the lobby `mode` values.
2. Offline AI backfill: allowed to fill any roster shortfall, or only with
   explicit consent per match?
3. Input recording privacy: action/axis replays are gameplay telemetry — keep
   local-only, or may the game instance upload them with history records?
4. Gamepad support bar for phase 1: XInput-classic mapping only, or full SDL
   GameControllerDB-style remapping?

## 11 · Implemented SessionPolicy, NetPolicy, RespawnPolicy, Agent, InputDriver & SyntheticDriver Architecture (2026-11-05)

All concrete engine, extension, and Everest project classes follow the repository naming rules (`SKILL-Naming.md`):

- **Input & Drivers (`Frontier/Engine/Input/`)**:
  - `InputPacket.h` — Normalized per-step vehicle and session control frame (`SequenceIndex`, `ThrottleAxis`, `BrakeAxis`, `SteerAxis`, `HandbrakeAxis`, `LookYawDelta`, `LookPitchDelta`, `ActionBits`).
  - `InputEncoder.{h,cpp}` — Samples GLFW keyboard, cursor deltas, and gamepad axes or headless `RawInputState` and encodes smoothed `InputPacket` frames.
  - `InputDriver.{h,cpp}` — Human driver binding a `PlayerToken` and `InputEncoder` to a possessed `Agent`.
  - `SyntheticDriver.{h,cpp}` — Autonomous AI driver steering, accelerating, and braking an `Agent` along circuit waypoints using the same `InputPacket` interface.
- **Controllable Scene Unit (`Frontier/Engine/Scene/Agent.{h,cpp}`)**:
  - `Agent` and `VehicleUnitConfiguration` — Represents a player-owned or AI-driven controllable scene unit bound to a `WorldSpace` `RecordToken`, carrying its Car Studio paint, rim, caliper, optics, studio backdrop (`WhiteCyclorama` or `SoftboxLoft`), ride height, camber, and cold tyre pressures (`kPa`).
- **Session, Policies & Level Transitions (`Frontier/Engine/Session/`)**:
  - `PlayerToken.h`, `PlayerProfile.h`, `PlayerStore.{h,cpp}` — Session player roster tracking join/leave, ready toggles, `StoredUnitToken`, and `VehicleUnitConfiguration`.
  - `SessionConfiguration.{h,cpp}` & `TransferConfiguration.{h,cpp}` — TOML-serialized session settings and seamless travel state preserving player tokens, profiles, and customized vehicle setups across level loads.
  - `SessionPolicy.{h,cpp}` — Engine default session and game-rule policies:
    - `NetPolicy` (`NetAuthorityRule`, `AllowJoinInProgress`, `AllowSyntheticBackfill`, `ReplicationTickHz`)
    - `SpawnPolicy` (`SpawnSupportRule`, `SpawnFootprint`, `DrivenWheelRule`, `BrakesEnabledAtSpawn`, `HandbrakeEngagedAtSpawn`,
      `BrakeHoldSeconds`) with the `StudioParked`, `GridRelease`, and `SuspendedDrop` presets
    - `RespawnPolicy` (`RespawnRule::Disabled` in Lobby so no respawning occurs in Lobby; `RespawnRule::SpawnGrid` and `RespawnRule::LastCheckpoint` in ProvingGrounds)
    - `LobbyPolicy`, `ProvingGroundsPolicy`, `RacePolicy`, and `EliminationPolicy`
  - `SessionSpace.{h,cpp}` & `LevelTransition.{h,cpp}` — Host-authoritative session lifecycle, `InputDriver` / `SyntheticDriver` orchestration, and `Default.levelspace` <-> `ProvingGroundVehicle.levelspace` transition execution.
- **Extensions (`Frontier/Extensions/Networking/NetworkRecordIndex.{h,cpp}`, `Frontier/Extensions/Session/SessionExtension.{h,cpp}`)**:
  - `NetworkRecordIndex` maps local `RecordToken`s and owner `PlayerToken`s to deterministic network identifiers across replication frames.
  - `SessionExtension` bridges `SessionSpace`, `LevelTransition`, `InputEncoder`, `WorldSpace`, `PhysicsExtension`, and `NetworkRecordIndex`.
- **Everest Custom Policies (`Projects/Everest/Source/Session/SessionPolicy.{h,cpp}`)**:
  - `EverestLobbyPolicy`, `EverestProvingGroundsPolicy`, `EverestRacePolicy`, `EverestEliminationPolicy`, and `ConstructEverestSessionPolicy`.
- **Validation & WebGPU Prototype (`Exhibits/SessionValidation/`, `Experimental/CarStudioLobby/`)**:
  - `Exhibits/SessionValidation/Code/SessionValidation.cpp` — 39/39 headless C++ assertions (`Exhibits/SessionValidation/Results/SessionValidation.md`).
  - `Experimental/CarStudioLobby/` — Interactive WebGPU + 3D fallback Car Studio Configurator Multiplayer Lobby -> ProvingGrounds Transition prototype.

