# Record registration + Scene Directory validation

Headless proof for plan §5/§6 and roadmap Phase 2: **the constructor
registers, the destructor unregisters** — nothing registers by hand anywhere
else.

- `Engine/Scene/RecordToken` — monotonic session identity from `TokenIssuer`
  (0 invalid, never reused, so stale tokens simply miss).
- `Engine/Scene/RecordClassification` — the Outliner's category vocabulary.
- `Engine/Scene/RecordStore` — runtime token→record registry with
  authentication, present in **every** profile (decision D3: runtime code
  resolves records by token in Shipping too).
- `Engine/Scene/RecordEntry` — base of every scene object: ctor issues the
  token + inserts into the store, and (Editor/Development `#if`) registers
  with the directory; dtor reverses both. Non-copyable, non-movable.
- `Engine/Scene/SceneDirectory` — editor-machinery Outliner model:
  display names, nesting (cycle/self refusals, logged; children re-root when
  a parent dies), registration order. Its translation unit is empty under
  Shipping and Shipping builds do not link it — no directory at all.
- `Engine/Interface/SceneDirectoryPanel` — portable view-model (flattened
  rows, category tabs with counts, classification filter, selection →
  inspector summary). Native drawing joins it at the SDK phase against the
  upstream kit, exactly like the Log Stream did.

Records are constructed on the main thread and must not have static storage
duration (both registries are function-local statics). Refusals are written
to the log stream's Interface channel; the suite reads one back.

## Build & run

```sh
cmake -S . -B Build && cmake --build Build && ctest --test-dir Build --output-on-failure
```

Profile matrix (CMake options): default Development;
`-DFRONTIER_EDITOR_PROFILE=1`; `-DFRONTIER_SHIPPING_PROFILE=1` (directory and
panel leave the link set; the suite proves the store still resolves tokens).
Or directly: `g++ -std=c++17 -Wall -Wextra -Werror -I ../../Frontier` over the
engine `.cpp` files listed in `CMakeLists.txt` plus the validation main,
`-pthread`. At delivery: 45 checks in Development and Editor, 17 in
Shipping; the Shipping binary carries zero `SceneDirectory` symbols (`nm`).

## Inspector (added with the per-record editor specification)

- `Engine/Scene/RecordEditorSpecification` — Editor/Development-only
  registry of each record's own Inspector construction: cards of bound
  controls (toggle / slider / segmented / readout / action). A record
  declares them in its constructor through `RecordEntry::DeclareEditor*`
  inside `#if defined(FRONTIER_EDITOR_PROFILE) ||
  defined(FRONTIER_DEVELOPMENT_PROFILE)`; its destructor unregisters them.
  Shipping declares, links, and pays for none of it.
- `Engine/Interface/SceneInspectorPanel` — portable view-model resolving
  the Outliner selection to live control values; every `Apply*` writes
  through the binding to the record itself. Native drawing
  (`SceneInspectorPanelPresentation.cpp`) is the HTML Inspector at its CSS
  pixel values — see `PARITY.md`. `InspectorCapture.cpp` CPU-renders the
  production draw code: the HTML Render settings page (all eight
  `BuildRender` cards, 53 controls) in `Capture/inspector-render.png` and
  `Capture/inspector-render-full.png`; the generic `SceneRecord` page in
  `Capture/inspector-vehicle.png`. The harness takes a page argument
  (`render|sun|sun-daylight|sun-light|sun-disc|moon|flare|wind|fog|fog-medium|fog-colour|aerial|aerial-medium|aerial-spectrum|clouds|clouds-shape|clouds-render|clouds-weather|field|cards|particles|derez|fracture|training|cones|level|stress|diagnostics|clusters`) and renders the
  other HTML inspector pages the same way
  (`Capture/inspector-<page>.png`) — see `PARITY.md` for what the generic
  vocabulary does not yet draw (bespoke dashboard instruments).
- The suite now covers the full loop: constructor-built specification →
  inspector resolution → slider clamp/write, toggle, segmented, action,
  readout refresh → destructor unregisters. At delivery: 56 checks in
  Development and Editor, 17 in Shipping, zero `SceneInspector` /
  `RecordEditor` symbols in the Shipping binary.
