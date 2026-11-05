# Frontier engine — session & game mechanics

The standalone session/game-mechanics slice and its software presenter require no Vulkan device or
GLFW window for validation. The complete standalone build, including HMI validation, uses C++20.
`SessionValidation` currently runs 62 checks; `Phase3Validation` covers the HMI state, frame declarations,
and approximate software images, not an integrated native GPU renderer.

```sh
cmake -S Frontier -B /tmp/frontier-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/frontier-build
ctest --test-dir /tmp/frontier-build --output-on-failure
```

The browser prototype, shader execution probes, commands, and remaining parity gaps are documented in
`Exhibits/VehicleHmi/README.md`.

## How we got here (audit of `SultanAladin/Frontier` @ `bd738ed`)

The other agent's session work was read file by file before any of it was kept. What was sound: the
`InputPacket` contract, the encoder's pedal/steer ramps, the footprint-survey spawn stand, the policy
family (Lobby/ProvingGrounds/Race/Elimination) and Everest keeping its own overrides in the project.
What was not kept, and why:

- **It cannot build outside their tree.** Every class is welded to `WorldSpace`/`RecordToken`/
  `PlayerStore`/`PhysicsSpace`, none of which exist here — so none of it was ever verifiable on its own.
  This redesign depends on nothing outside `Frontier/Engine` + `Projects/Everest`.
- **`DeployableUnit` was an alias** (`using DeployableUnit = Agent;`). Here it is the actual pawn class,
  as specified, and `Agent` is what the name should mean: the control stand between a driver and a unit.
- **One class did five jobs.** Their `Agent` was pawn, appearance, spawn stand, brake rules and world
  record at once. Here: `DeployableUnit` (pawn + deployment), `Agent` (possession), drivers (packets),
  policies (rules).
- **The Everest `RaceProcedure`/`EliminationProcedure` files from the brief do not exist on that
  branch** — only a `SessionPolicy` pair does. Game rules here are policies (`RacePolicy`,
  `EliminationPolicy`), never procedures, per the naming rules.

## The contract

- **Input is `InputPacket`, full stop.** Devices encode straight into packets (`InputEncoder`; GLFW
  polling compiles in with `FRONTIER_WITH_GLFW`). There is no action-map layer.
- **`InputDriver` drives an Agent (human); `SyntheticDriver` drives an Agent (AI)** — identical
  packets, so possession can swap without the unit noticing.
- **`DeployableUnit` deploys itself**: `Deploy()` stands it under the session's `SpawnPolicy`
  (footprint survey — the same stand on a pad, a kerb, a bank, a cross-slope), including brake rules
  at the start (enabled, disabled for a hold, or parked on the handbrake: `StudioParked`,
  `GridRelease`, `SuspendedDrop`).
- **Rules are policies in `Session/SessionPolicy.h`**: `NetPolicy`, `RespawnPolicy` (never in the
  Lobby; grid or last-checkpoint elsewhere) and the game rules (`RacePolicy`, `EliminationPolicy`).
  Engine defaults live in the engine; **Everest's own numbers live in
  `Projects/Everest/Source/Session`** and reach the session through its factory.
- **`SessionSpace`** is the room: players join/leave, deploy at studio slots (host centre, guests on
  the ring — the Lobby arrangement), ready up, and `LevelTransition` carries their profiles
  (nametag + Rank — the same pair the Lobby's number plates wear) into the next level's policy.

## The game (C++ Lobby)

`Frontier/Engine/Session/LobbySession` is the game at this step. It opens on the **Lobby by default**,
builds its room by reading the assigned policy (`LobbyPolicy`: at most **5 players**, **no
respawn**, **no derez**, parked spawn), and stands **one vehicle** in the studio — the same
ControlVehicle, from the same `FCT1` facet archives as the HTML renderer
(`Engine/Scene/FacetArchive`), with a steering wheel that follows the steer axis and a
**tablet that is powered but empty** (no HMI yet). Other policies (ProvingGrounds, Race,
Elimination, Everest's) plug in through the same creation path. Level switching beyond the
normal all-ready transition is a **debug-only popup**: `Ctrl+Shift+L`, then a digit
(`FRONTIER_DEBUG_LEVEL_SWITCH=0` compiles it out).

```sh
/tmp/frontier-build/LobbySession --guests 4 --render /tmp/lobby.ppm
/tmp/frontier-build/LobbySession --xray --render /tmp/lobby-scan.ppm
```

These are CPU presentations of the facet archives, not native GPU captures. The standalone scan
shader and HMI frame declarations are not connected to a native renderer.

## Where things live

- **Visual effects** (anything that changes how a vehicle is *looked at*, never how it behaves):
  `Frontier/Engine/Effects/` — first resident is `VehicleInspection` (Solid / Ghost / Wireframe /
  **X-ray Scan**, the travelling band; render the approximate CPU version with `LobbySession --xray`).
  Presentations read it; physics and the session never do.
- **Session and its policies**: `Frontier/Engine/Session/` — including `LobbySession`, the game
  session that opens on the Lobby. (There is deliberately no `LobbyGame`.)
- **Current HMI evidence and limits** are in `Exhibits/VehicleHmi/`; the PNGs are early prototype GPU
  captures and do not establish full reference parity.
