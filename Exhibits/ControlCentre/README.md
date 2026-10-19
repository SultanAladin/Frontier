# Control Centre, Typography, and Themes

## Delivered scope

- Top-edge expansion tab and full-height drawer: circular FPS/Notifications quick toggles and a Settings gear.
- Settings opens a category list; Appearance contains Display, Typography and Theme tabs, with Apply and Discard.
- Native Control Centre: discovered typeface cards, available weights, type-scale previews, eight themes,
  corner radius, ten accents, and semantic colours.
- Draft edits remain separate from applied preferences; Apply saves them and Discard restores applied settings.
- Applied body face, weight, size, interface scale, and palette affect actual native UI output.
- Preferences restore font selection by family name and live inside `%LOCALAPPDATA%/Frontier/Personal.projectspace` on Windows.
- No GI, renderer quality, scene resolution, AA, shadows, materials inspector, or scene-renderer dependencies.

The inherited `AppearanceInspector`, `FontCodec`, `TypefaceRegistry`, and drawer motion were selectively adapted from
`SultanAladin/Frontier-` commit `338293863d7df4e585dec0c4acae45fe9c524ed5`.
`ControlCentrePanel` adapts the renderer-coupled host for this UI-only scope.
`PanelExpansionTab` adapts its trapezoid, axis lock, elastic drag, and release thresholds; `MotionIntegrator` reuses
the original bounded spring integration. Renderer tiles are intentionally absent. Full page-transition visual
parity is not claimed. The engine's Render directory remains blank.

## Fonts and inventory

Downloaded unmodified static faces: Inter, Archivo, Space Grotesk, and JetBrains Mono.
There are 30 faces; each family retains its original SIL Open Font License 1.1 notice.
General Sans and Clash Display are not downloaded: no open-source licence was established for those families.

`Automation/FontSources.json` pins file sources and SHA-256 digests to the approved baseline.
`Automation/PrepareFonts.py` downloads missing pinned files, refuses modified files, and atomically generates
`EngineContent/FontArchives/FontCatalogue.toml` from every local TTF/OTF using actual font metadata.
A matching JSON catalogue is retained for packaging/report tools only; native C++ reads TOML.
The catalogue includes family, weight, italic classification, relative path, and SHA-256; invalid files are reported.
Native `FontCodec` also scans current files and regenerates per-family TOML descriptors.
Rescanning removes deleted faces instead of reviving stale descriptor entries.
`TypefaceRegistry` includes discovered families beyond its preference list and permits single-weight families.

Downloaded font binaries and generated descriptors remain Git-ignored; source pins and recovery code are tracked.
Add static fonts with `Family-Weight.ttf` or `.otf` naming, regenerate the catalogue, and restart the application.
Native discovery uses the TOML catalogue for canonical font names and weights; unlisted files fall back to the
inherited filename classifier. File data is read through Unicode-capable filesystem paths before atlas registration.
Malformed OpenType headers/table bounds are rejected before calling ImGui; this is not a general font-security audit.
WOFF/WOFF2, arbitrary variable-axis instances, shaping, and ligature controls are not implemented in this batch.
Non-body type-scale settings currently drive their previews; a full editor-wide typography mapping is future work.

## Native Windows build — MSVC

Use a Visual Studio x64 developer terminal, Python 3.9+, Git, and CMake 3.21+:

```powershell
python -m pip install -r Automation/FontRequirements.txt
python Automation/PrepareUI.py
python Automation/PrepareFonts.py
cmake -S . -B Build/UI -A x64
cmake --build Build/UI --config Release
ctest --test-dir Build/UI -C Release --output-on-failure
python Automation/PackageWindows.py --build Build/UI --config Release --toolchain "MSVC x64"
```

The resulting archive is `Binaries/ControlCentre-Windows-x64.zip`. Extract the complete archive, then run
`FrontierUI.exe`. Fonts, SVGs, inventory, per-family font licences, and third-party notices are bundled.
The native MSVC commands are provided for reproduction; MSVC itself was not available for verification here.

## Cross-toolchain used here

The earlier LLVM-MinGW release-asset download was network-blocked. The PyPI Zig distribution succeeded.
This is a toolchain for generating Windows binaries, not a Linux application deliverable.

```text
python -m pip install ziglang==0.13.0
cmake -S . -B _ClaudeScratch/build/windows-ui -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=Automation/WindowsToolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build _ClaudeScratch/build/windows-ui
python Automation/PackageWindows.py --build _ClaudeScratch/build/windows-ui \
  --toolchain "Zig 0.13.0 / Clang 18.1.6"
```

The Windows toolchain file is for a Linux editing host; Windows users can keep their normal MSVC build.
Zig's bundled Windows headers/import libraries avoid downloading a separate Windows SDK here.
GLFW 3.4 is statically linked; `GLFW_NO_API` prevents an accidental OpenGL context.
The Windows link uses `d3dcompiler_47`; native MSVC uses its SDK's `d3dcompiler` import library.
ThorVG uses the Windows C++ thread implementation, not a guessed pthread import library.
No GitHub Actions are required or authorised.

## Verification and evidence

Local editing host: Linux, GCC 12.2, Python 3.11, ImGui/ThorVG/toml++ at the locked revisions.
The following host-side tools exercise native C++ UI code; they are not distributed Linux product binaries:

```text
cmake -S . -B _ClaudeScratch/build/ui -G Ninja \
  -DFRONTIER_HOST_VERIFICATION=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build _ClaudeScratch/build/ui
ctest --test-dir _ClaudeScratch/build/ui --output-on-failure
```

Sanitizer configuration additionally uses `-fsanitize=address,undefined -fno-omit-frame-pointer`
and links executables with `-fsanitize=address,undefined`.

| Check                        | Observed result                                      |
|------------------------------|------------------------------------------------------|
| Debug tests                  | 12/12 passed.                                          |
| Address/undefined checks     | 12/12 passed.                                          |
| Font catalogue unit tests    | Stable offline scan, removal, corrupt/modified rejection. |
| Native font discovery        | Four families, 30 faces; no duplication on rescan.     |
| Native pointer interaction   | Gear, categories, Back, tabs, font/theme/scale controls, quick toggles and swipe. |
| Keyboard shortcut            | Default/custom chords; Escape dismisses or cancels capture. |
| Applied settings             | Actual palette, font face, weight, size, and scale.   |
| Scaled interaction           | Theme pointer coordinates remain correct at 125%.    |
| Persistence                  | Apply/discard, reload by family name, invalid-save preservation. |
| Windows cross-compilation    | Succeeded; Release PE32+ x64 executable linked.       |
| Windows runtime / GPU        | Not run: no Windows runtime/display available.       |

`Images/ClosedNotch.png`, `QuickPanel.png`, `SwipeUp.png`, `Fonts.png`, `TypeScale.png`,
`Theme.png`, `LightTheme.png`, `Settings.png`, `SettingsTransition.png`, `Input.png`,
`Notifications.png`, and `Display.png` are actual native ImGui draw commands
rasterized by the inherited exhibit-only CPU fixture. They are not mockups or Windows GPU screenshots.
`Code/ControlCentreValidation.cpp` generates PPM pixels; `Exhibits/UIFoundation/Code/EncodeEvidence.py`
losslessly encodes those pixels as PNG without changing the design.
The CPU rasterization code lives only in this exhibit, never under `Engine/Render/`.

To reproduce package/import inspection, install `pefile==2024.8.26`, then run
`python Exhibits/ControlCentre/Code/InspectWindowsPackage.py`. It verifies ZIP integrity, bundled file hashes,
current source hashes, font catalogue hashes, PE architecture, and Windows-only import dependencies.

Results include Debug/sanitizer logs, the font inventory, Windows executable/import metadata, and source hashes.
The previous UI foundation report is historical; this exhibit describes the current Control Centre batch.
The development executable is unsigned. Cross-compilation and import inspection do not prove Windows startup,
DPI/device behaviour, GPU correctness, or native MSVC compatibility.

## Drawer reference and regression coverage

The inspected non-session branches of `SultanAladin/Frontier` contained no named notch/ControlCentreHost sources.
The original executable implementation is preserved in the already-approved `Frontier-` baseline at
`338293863d7df4e585dec0c4acae45fe9c524ed5`: `Engine/DisplayPresentation/ControlCentreHost.{h,cpp}`.
That is the source used for the 400×36 notch, 8% lower-corner inset, full-height sheet, 48×6 bottom grip,
6 px tap travel, 350 ms tap duration, 5% elasticity, 20 px/s flick, and 50 px displacement thresholds.
The original `MotionIntegrator.{h,cpp}` supplies the spring integration. Threshold distances scale with the interface.
No claim is made that a different unnamed branch was located or copied.

`PanelExpansionValidation` exercises taps, slow upward dismissal, both flick directions, horizontal/diagonal axis lock,
small-drag retention, focus-loss release, resize, scale, and narrow viewports.
`ControlCentreValidation` sends actual ImGui pointer events through the native drawer and records the intermediate
upward swipe. It also loads fonts from a Unicode directory with an unusual filename via TOML metadata,
rejects a corrupt font without an ImGui assertion, and checks archive TOML round-trip.

The existing `Appearance.toml` schema is preserved. No separately named upstream theme-file extension was found
in the inspected appearance/theme implementation; none is invented here. These UI preference formats remain TOML;
approved format policy B separately permits
compiled binary runtime packages while retaining editable authoring sources.
The display host now uses GLFW rather than a hand-written Win32 message loop. Primary engine rendering remains
Vulkan with Slang shaders; DX11 remains limited to the ImGui exhibit.

## Navigation correction — 2026-10-17

```text
Quick controls → Settings gear → category list
                               ├─ Appearance → Display / Typography / Theme
                               ├─ Input (persistent preferences and working UI shortcuts)
                               └─ Notifications (FPS overlay, interface alerts and session history)
```

The incorrect rectangular Fonts/Theme quick tiles are removed. Reuse is pinned to the seven reference files in
`Results/NavigationReferencePins.json`: the same approved Frontier- commit, not a guessed newer branch.
The quick grid keeps 64 px discs, 24 px SVGs, 12 px labels and 16 px column gaps. The category list keeps 44 px
icon discs, 20 px SVGs, 76 px rows, 15/13 px text and 24 px list rounding. Pages use inherited spring sizing and
0.2 s exit/entry transitions, a Back affordance, underlined tabs and a sticky Apply/Discard footer.
Back preserves unsaved drafts. The Display tab retains only interface scaling, not renderer settings.

Excluded: GI/reflection/AA/quality/patch tiles, Render Settings, Materials, render-scale/audio pill, and unconnected
network indicators. Do not interpret the filtered UI as a complete copy of the baseline renderer dashboard.
The inherited Input form geometry is retained and improved with working UI command capture, conflict checks,
profiles, persistent preferences and a live applied pointer-response preview. Scene camera/tools are not hosted;
no tool-command implementation or viewport integration is implied. Notification widgets cover real host events only;
FPS state is shared with the quick tile, and preference alerts use the selected hold duration.

Reproduce the complete local verification and Windows package (existing build trees must be configured first):

```sh
_ClaudeScratch/venv/bin/python Exhibits/ControlCentre/Code/ValidateNavigation.py
```

The script uses CMake/CTest on PATH beside its Python interpreter. Configure the Windows tree with
`Automation/WindowsToolchain.cmake` and Zig 0.13.0; the earlier setup commands remain applicable.
`Results/NavigationChecks.json` retains exact commands, source/image hashes, checkout revision, uncommitted state,
reference pins, test counts, Windows package inspection and limitations. `NavigationDebug.xml` and
`NavigationSanitized.xml` are new seven-test results; build/test/package logs are retained beside them.
The generated PNGs were visually inspected as native CPU output. No Windows runtime/GPU or full-browser
visual test was run. No GitHub Actions, commit or push was performed.

## Input and range-control port

- One range style everywhere: 28 px rail, 26 px thumb, shared colours, shadow, pressed sizing and pointer mapping.
  Hold Duration, corner radius, type scale, interface scale, sensitivity and the foundation gallery use that primitive.
- Footers have a straight upper seam and 32 px lower corners matching the page. Native pixel assertions check both.
- Input retains the reference form layout, but not its nonfunctional Blender/Maya/Unreal preset names or unhosted
  Select/Translate/Rotate/Frame fields. Editor Default / Custom configure commands this host actually executes.
- Defaults: Ctrl+Comma opens/closes Control Centre; Ctrl+Alt+F opens Typography; Ctrl+Alt+T opens Theme;
  Escape dismisses. Custom chords support Ctrl/Alt/Shift/Super. OS-reserved chords may be intercepted externally.
- Click a shortcut to capture it. Modifier-only input waits; Escape cancels; focus loss/navigation also cancel.
  Duplicate/reserved chords are refused without replacing the draft. Captured input does not also execute a command.
- Apply persists `Input.<appearance filename>` beside appearance preferences (normally `Input.Appearance.toml`).
  Discard restores applied input, and Defaults only changes the draft. Saving validates first and shares the existing
  atomic text-file replacement mechanism with appearance persistence, including Unicode path decoding.
- Sensitivity maps 0/50/100 to 0.5/1/2 times the logical pointer delta. Inversion requires Advanced Controls.
  The UI displays the applied response; drawer travel remains 1:1 and no scene-camera consumer is claimed.
- Default reset/Apply/Discard remain accessible in a stacked footer on narrow viewports. A 375x700 viewport at
  125 percent scale is covered. Native Windows DPI, IME/layout and GPU behaviour still require Windows validation.

Scoped naming cleanup: `ConstructInterface` / `Construct…Layout` replace the touched `Record…` entry points;
page enums use `…Category`; input/notification predicates use `Query…`; preference mutation uses
`InitializePreferences`, `FinalizePreferences`, `ResetDraft` and `ConfigureFrameRateOverlay`.
New parameter lists, headers, Allman braces and local names follow the mandatory skills. This is not a claim that
all older imported code has been audited or renamed; tracker finding 012 remains open.

New native captures: `InputCapture.png`, `InputConflict.png`, `InputConfigured.png`, `InputPointerResponse.png`.
`InputPreferencesValidation` adds token, gain/inversion, Unicode round-trip, malformed-schema, duplicate-chord and
failed-replacement preservation tests. `ControlCentreValidation` covers real pointer/key events, conflict/cancel,
focus loss, profile selection, persisted reload, actual command execution, footer pixels and scaled narrow bounds.

## Shared expansion tab and Content Browser

The production control is now `PanelExpansionTab`, shared by the top Control Centre and bottom Content Browser.
`../ContentBrowser/README.md` records the pinned React reference, native adaptation, controls and limitations.
The verification driver includes two browser tests and seven new native captures, including narrow/scaled output.

## Single-file projectspace persistence

The Windows host now uses Personal.projectspace and accepts --project / --create-project arguments.
Project appearance, input and notification settings are embedded, with explicit save scope and optional overrides.
See `../ProjectSpace/README.md` for the exact editor encoding, compatibility, reproduction and remaining work.
The current full driver runs twelve Debug and twelve sanitizer tests and regenerates 41 native captures.

## Top-right notification cards and history

`../Notifications/README.md` documents the shared bounded sequence, supplied light/dark card presentation, real
preference save/apply/error events, dismissal/input isolation, expiry/suppression, and responsive Quick Controls
Notification Stream and real Apply/Cancel confirmation UI.
Notification preferences retain the existing embedded projectspace contract; history is transient.

## Shared panel docking continuation

The Windows exhibit now includes the native patched dockspace with the original Content Browser and optional
Notification Stream. Settings → Workspace adds a default-off personal detached-OS-window preference.
See `../Docking/README.md` for the approved phased scope, five native CPU captures, verification and limitations.
Layout restore, monitor recovery, Vulkan platform-window rendering and Windows runtime execution remain pending.

Workspace also selects **Content Browser → Draggable / Dockable**. Draggable is the default; the preference is
personal, staged, confirmed and retained inside projectspace. Switching presentation preserves the original content.
The patched native `+` now sits directly after the tab with centred strokes/disc/hitbox. Shared top/bottom notches
and native tabs use the darkest named theme surface with full opacity, including light themes.
