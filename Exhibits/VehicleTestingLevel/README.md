# Vehicle Testing Level — lobby and single-level scope verification

Evidence for `Experimental/VehicleTestingLevel/`, the reduced vehicle HMI demo: one lobby, one level (the proving
grounds). Tested on the `arena/0ae367fc-frontier` working tree, based on `e1ce562` of `SultanAladin/Frontier`, on
Linux x86_64 with Node 22.22.3 and Dawn's null backend. No GitHub Actions were run.

## What was verified

| Check | Command (from `Experimental/VehicleTestingLevel`) | Result |
| --- | --- | --- |
| Unit, scope, level and column checks | `npm test` | **51 / 51 pass** (42 inherited + 9 lobby/scope/levels/columns/skidpad) |
| Tyre, damage and stability scenarios | `npm --prefix Tools run test:stability` | **all passed** (16 scenarios) |
| Bring-up, shaders, pipelines, commands, 12 frames, lobby up | `npm --prefix Tools run validate` | **12 frames, 0 errors, 0 warnings** |
| Same path with the lobby skipped | `cd Tools && node HeadlessHarness.mjs .. 12 "?capture=1&lobby=0"` | **12 frames, 0 errors, 0 warnings** |
| Effects integration, 36 frames | `npm --prefix Tools run validate:effects` | **36 frames, 0 errors, 0 warnings** |
| Slate cabin/HMI integration, 48 frames | `npm --prefix Tools run validate:slate` | **48 frames, 0 errors, 0 warnings** |
| Lobby hand-over through the real host | `cd Tools && HOOKS=../../../Exhibits/VehicleTestingLevel/Code/LobbyProbe.mjs node HeadlessHarness.mjs .. 12 "?capture=1"` | card ready → Enter → lobby hidden, car live |
| Level list and independent columns through the real host | `cd Tools && HOOKS=../../../Exhibits/VehicleTestingLevel/Code/UiProbe.mjs node HeadlessHarness.mjs .. 12 "?capture=1&level=proving"` | lobby row "Here" + scene row "Current"; each column closes alone; P folds/restores both |

Raw output: `Results/Tests.txt`, `Results/Stability.txt`, `Results/DawnValidation.txt`, `Results/LobbyProbe.txt`, `Results/UiProbe.txt`.

## The lobby probe

The probe reads the card through the same DOM ids the browser uses, presses Enter, and checks the hand-over. Actual
output (`Results/LobbyProbe.txt`):

```text
card before Enter · visible=true ready=true buttonDisabled=false status="Ready · proving grounds loaded" bar=100%
after Enter · visible=false lobbyHidden=true

=== harness: 12 frames, 0 errors, 0 warnings, 1.5 s
final · ready=true frames=12 lobbyVisible=false
```

This confirms the card is driven by real bring-up progress, the entry button only enables once the scene is ready, and
the hand-over that clears held keys and hands the keyboard to the car runs exactly once.

## The level UI and column probe

`Code/UiProbe.mjs` reads the lobby's generated level list and toggles the editor columns through the real host. Actual
output (`Results/UiProbe.txt`):

```text
level list · rows=2 hubHere=true sceneCurrent=true sceneKey=proving hero="Proving Grounds"
boot · outlinerHidden=false inspectorHidden=false
close outliner · outlinerHidden=true inspectorHidden=false
close inspector · outlinerHidden=true inspectorHidden=true
reopen outliner · outlinerHidden=false inspectorHidden=true
P (restore both) · outlinerHidden=false inspectorHidden=false
P (fold both) · outlinerHidden=true inspectorHidden=true
entered level · lobbyVisible=false

=== harness: 12 frames, 0 errors, 0 warnings, 1.6 s
final · ready=true frames=12 level=proving lobbyVisible=false
```

The list comes from `Source/LevelCatalog.js`: the lobby is the first row (`hubHere=true`, it is a level in the list
even though it is not a scene) followed by the single drivable level. Closing the outliner leaves
`inspectorHidden=false` — the columns are no longer one switch.

## Scope checks inside the test suite

`Tools/TestingLevelLobby.test.mjs` asserts, against the actual files:

- no `DDayBattleSequence.js`, `DDayStructure.js`, `CityPursuitStructure.js`, `PolicePursuitSequence.js`,
  `InversionArenaStructure.js`, `RoadStudioAdapter.js`, `CollapseSequence.js` or `TramSequence.js` in `Source/`;
- no D-Day, Inversion, City/Pursuit or Road Studio structure imports in any `Source/*.js`;
- one lobby card and one entry point in `index.html`, with no multi-level workspace tabs or D-Day/City actions;
- the lobby refuses Enter until ready, reports the real caption and percentage, hands over once, returns on Escape, and
  bypasses itself under `?lobby=0`;
- the lobby renders one row per catalogue entry, marks the current level, and fills the hero from that entry;
- unknown level keys resolve to the shipped level rather than to a blank scene;
- each editor column closes on its own, `P` folds and restores both, and the notice names arrive in order;
- the lobby is listed as a level (`Kind: "hub"`) and marked "Here", but `?level=lobby` still resolves to the drivable
  level, so the hub can never be loaded as a scene;
- the skidpad drill and the drift scorer are gone: no `Id: "skidpad"`, no `AdvanceDrift`, no `Unit: "pts"` anywhere.

## Limitations — not passing, not run

| Item | State |
| --- | --- |
| Rendered lobby appearance in a browser | **Not verified.** This host has no Chromium/Chrome, no Vulkan ICD and no SVG rasteriser, so no screenshot of the HTML/CSS could be produced. The layout is `Style/Lobby.css` in the existing editor tokens; it needs a look in Chrome or Edge. |
| WebGPU runtime and GPU performance | **Not verified.** Dawn's null backend validates shader/pipeline/command recording without executing or rasterising; there is no GPU in this environment. |
| Windows build/run | **Not run.** This prototype is browser JavaScript/WASM (Jolt) and ships no native binary; the repository's Windows binary rules do not apply to it. |
| Touch/pointer behaviour on the lobby card | **Not verified** on a touch device. |

## Reproduction

```sh
cd Experimental/VehicleTestingLevel
npm ci && npm --prefix Tools ci
npm test
npm --prefix Tools run test:stability
npm --prefix Tools run validate
cd Tools && node HeadlessHarness.mjs .. 12 "?capture=1&lobby=0"
cd .. && npm --prefix Tools run validate:effects && npm --prefix Tools run validate:slate
cd Tools && HOOKS=../../../Exhibits/VehicleTestingLevel/Code/LobbyProbe.mjs node HeadlessHarness.mjs .. 12 "?capture=1"
cd Tools && HOOKS=../../../Exhibits/VehicleTestingLevel/Code/UiProbe.mjs node HeadlessHarness.mjs .. 12 "?capture=1&level=proving"
```

`node_modules/` is installed locally and ignored by Git; the pinned versions come from the two lockfiles in the demo.
